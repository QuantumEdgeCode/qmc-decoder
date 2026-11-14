/*
 * Author: mayusheng - mayusheng@huawei.com
 * Last modified: 2020-06-29 10:56
 * Filename: decoder.cpp
 *
 * Description: QMC 加密音频文件自动解码器
 * 将 QQ音乐下载的 .qmc3/.qmc0/.qmcflac/.qmcogg 文件还原为标准的
 * MP3/FLAC/OGG 格式。
 *
 * 编码兼容性说明：
 * - 所有文件路径统一通过 fs::path 传递，避免手动编码转换
 * - Windows 下使用 fs::path::wstring() 获取 UTF-16LE 路径，
 *   通过 _wfopen_s 打开文件，不受系统活动代码页（CP936/GBK）影响
 * - 命令行参数使用 CommandLineToArgvW 获取原生宽字符参数
 * - 控制台输出编码切换为 UTF-8，确保中文文件名显示正常
 */

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "seed.hpp"

/*
 * 优先使用 C++17 std::filesystem（MSVC 默认支持），
 * 否则回退到 ghc::filesystem 第三方库（支持 C++11/14）。
 * ghc::filesystem 以 git submodule 形式放在 3rdparty/ 目录下。
 */
#if defined(__cplusplus) && __cplusplus >= 201703L && defined(__has_include)
#if __has_include(<filesystem>)
#define GHC_USE_STD_FS
#include <filesystem>
namespace fs = std::filesystem;
#endif
#endif
#ifndef GHC_USE_STD_FS
#include <ghc/filesystem.hpp>
namespace fs = ghc::filesystem;
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
/* CommandLineToArgvW 声明在 shellapi.h；WIN32_LEAN_AND_MEAN 会让 windows.h
 * 不再包含它，必须显式引入，否则 MSVC 报 C3861 找不到标识符 */
#include <shellapi.h>
#endif

namespace {
/* 将文件指针包装为智能指针，确保异常安全地自动关闭 */
void close_file(std::FILE* fp) { std::fclose(fp); }
using smartFilePtr = std::unique_ptr<std::FILE, decltype(&close_file)>;

/**
 * @brief 将 path 转为 UTF-8 字节串，专用于控制台输出
 *
 * 为什么不能直接用 path::string()：
 * Windows 上 path::string() 返回的是"本机窄编码"，即系统 ANSI 代码页
 * （简体中文 = CP936/GBK）。而 main() 已把控制台切到 CP_UTF8，
 * GBK 字节会被控制台当成 UTF-8 解释，中文文件名必然乱码；
 * 更糟的是文件名含 CP936 无法表示的字符（emoji、日文、韩文）时，
 * string() 的 codecvt 转换会抛 std::system_error，进程直接终止。
 *
 * 因此显示层统一走 WideCharToMultiByte(CP_UTF8)，与代码页彻底解耦。
 */
#ifdef _WIN32
std::string toUtf8(const fs::path& aPath) {
  const std::wstring w = aPath.wstring();
  if (w.empty()) return std::string();
  const int len = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(),
                                        static_cast<int>(w.size()), nullptr, 0,
                                        nullptr, nullptr);
  if (len <= 0) return std::string();
  std::string out(static_cast<std::string::size_type>(len), '\0');
  ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                        &out[0], len, nullptr, nullptr);
  return out;
}
#else
/* POSIX 下 path::string() 原生就是 UTF-8，直接返回 */
std::string toUtf8(const fs::path& aPath) { return aPath.string(); }
#endif

enum class openMode { read, write };

/**
 * @brief 以编码安全的方式打开文件（跨平台）
 *
 * 核心设计：不依赖窄字符串编码假设，而是利用 fs::path 的内部表示：
 * - Windows：fs::path 内部存储 UTF-16LE，通过 wstring() 取出后
 *   直接传给 _wfopen_s，无论路径来源是 GBK 还是 UTF-8 都正确
 * - Unix/Linux：fs::path 内部存储 UTF-8，通过 string() 取出后传给 fopen
 *
 * @param aPath      文件系统路径（编码无关的 fs::path）
 * @param aOpenMode  打开模式：read 读取 / write 写入
 * @return smartFilePtr 自动关闭的文件指针，打开失败时返回 nullptr
 */
smartFilePtr openFile(const fs::path& aPath, openMode aOpenMode) {
#ifdef _WIN32
  /*
   * Windows 平台：使用系统原生宽字符 API
   * fs::path::wstring() 返回 UTF-16LE 编码的宽字符串，
   * 这是 Windows 内核和 _wfopen_s 的原生编码格式，
   * 因此无需任何编码转换，彻底杜绝 GBK/UTF-8 混淆问题。
   */
  std::FILE* fp = nullptr;
  _wfopen_s(&fp, aPath.wstring().c_str(),
            aOpenMode == openMode::read ? L"rb" : L"wb");
#else
  /*
   * Unix/Linux 平台：path::string() 原生返回 UTF-8，
   * 直接传给标准的 fopen 即可。
   */
  std::FILE* fp =
      fopen(aPath.string().c_str(), aOpenMode == openMode::read ? "rb" : "wb");
#endif
  return smartFilePtr(fp, &close_file);
}

/**
 * @brief 处理单个 QMC 文件：解码并写出目标格式文件
 *
 * 处理流程：
 *   1. 根据文件扩展名（大小写不敏感）判断输出格式
 *   2. 打开源文件，读取全部字节到内存缓冲区
 *   3. 使用 seed 算法逐字节异或解密
 *   4. 将解密后的数据写入扩展名替换后的输出文件
 *
 * @param dir  QMC 源文件路径（fs::path，编码无关）
 */
void sub_process(fs::path dir) {
  auto outloc = dir;

  /*
   * 格式检测：比较扩展名（转为小写后）
   * 使用 path::extension() 和 replace_extension() 替代正则表达式，
   * 避免引入编码相关的字符串操作风险。
   */
  auto ext = toUtf8(dir.extension());
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  if (ext == ".qmc3" || ext == ".qmc0")
    outloc.replace_extension(".mp3");
  else if (ext == ".qmcflac")
    outloc.replace_extension(".flac");
  else if (ext == ".qmcogg")
    outloc.replace_extension(".ogg");
  else
    return;  /* 未知格式，跳过 */

  std::cout << "decode: " << toUtf8(dir.filename()) << std::endl;

  /* 以二进制只读模式打开源文件 */
  auto infile = openFile(dir, openMode::read);

  if (infile == nullptr) {
    std::cerr << "failed read file: " << toUtf8(outloc.filename())
              << std::endl;
    return;
  }

  /* 获取文件大小 */
  int res = fseek(infile.get(), 0, SEEK_END);
  if (res != 0) {
    std::cerr << "seek file failed" << std::endl;
    return;
  }

  auto len = ftell(infile.get());
  res = fseek(infile.get(), 0, SEEK_SET);

  /* 分配内存缓冲区，一次性读取整个文件 */
  std::unique_ptr<char[]> buffer(new (std::nothrow) char[len]);
  if (buffer == nullptr) {
    std::cerr << "create buffer error" << std::endl;
    return;
  }

  /* 读取源文件全部字节到缓冲区 */
  auto fres = fread(buffer.get(), 1, len, infile.get());
  if (fres != len) {
    std::cerr << "read file error" << std::endl;
  }

  /*
   * QMC 加密算法：使用固定的 8×7 查找表生成伪随机掩码序列，
   * 将每个字节与掩码异或实现加/解密。
   * 异或操作的对称性使加密和解密使用同一套逻辑。
   */
  qmc_decoder::seed seed_;
  for (int i = 0; i < len; ++i) {
    buffer[i] = seed_.next_mask() ^ buffer[i];
  }

  /* 以二进制写入模式打开目标输出文件 */
  auto outfile = openFile(outloc, openMode::write);

  if (outfile == nullptr) {
    std::cerr << "failed write file: " << toUtf8(outloc.filename())
              << std::endl;
    return;
  }

  /* 将解密后的数据写入输出文件 */
  fres = fwrite(buffer.get(), 1, len, outfile.get());
  if (fres != len) {
    std::cerr << "write file error" << std::endl;
  }
  /* 文件指针在 outfile 析构时自动关闭（RAII） */
}

/**
 * @brief 打印帮助信息（-h / --help）
 *
 * 内容为纯 ASCII 与中文混排，输出编码依赖 main() 已把控制台切到 UTF-8。
 */
void print_help() {
  printf(
      "qmc-decoder — QQ 音乐 QMC 加密音频解码器\n"
      "\n"
      "用法:\n"
      "  qmc-decoder                 扫描当前目录（含子目录）并解码全部 QMC 文件\n"
      "  qmc-decoder <文件路径>       解码指定的单个文件\n"
      "  qmc-decoder -h | --help     显示本帮助\n"
      "\n"
      "支持的格式:\n"
      "  .qmc3 / .qmc0   ->  .mp3\n"
      "  .qmcflac        ->  .flac\n"
      "  .qmcogg         ->  .ogg\n"
      "\n"
      "示例:\n"
      "  qmc-decoder\n"
      "  qmc-decoder \"D:\\Music\\song.qmcflac\"\n"
      "\n"
      "说明:\n"
      "  输出文件与源文件同目录、同名，仅替换扩展名；源文件保留不删除。\n"
      "  已存在的输出文件会被直接覆盖。\n"
      "  退出码: 0 正常 / 1 用法错误 / -1 目录不可写或参数解析失败。\n");
}
}  // namespace

/**
 * @brief 程序入口
 *
 * 两种使用模式：
 *   1. 无参数模式：自动递归扫描当前目录下所有 .qmc* 文件并解码
 *   2. 单文件模式：qmc-decoder /PATH/TO/FILE.qmcflac
 *
 * Windows 编码处理策略：
 *   - main() 入口立即将控制台代码页切换为 UTF-8，确保中文路径可正常显示
 *   - 命令行参数用 CommandLineToArgvW 获取 UTF-16LE 宽字符，
 *     避免 argv 的 ACP（GBK 936）编码问题
 *
 * @param argc  命令行参数个数
 * @param argv  命令行参数数组（Windows 下为 ACP 编码，不直接使用）
 * @return int 退出码（0=成功，非0=错误）
 */
int main(int argc, char** argv) {
#ifdef _WIN32
  /*
   * 将控制台输出/输入代码页切换为 UTF-8。
   * 这样后续 std::cout / std::cerr 输出的中文文件名可以正常显示，
   * 不再受系统活动代码页（简体中文 = CP936/GBK）的限制。
   * Windows 10 及以上版本 + Windows Terminal 完美支持；
   * 旧版 cmd.exe 可能需要将字体设置为支持中文的 TrueType 字体。
   */
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);
#endif

  /* 参数过多时提示正确的用法 */
  if (argc > 2) {
    printf(
        "用法：将本程序放入 QMC 文件所在目录后直接运行，"
        "或通过命令行指定文件路径：qmc-decoder /PATH/TO/SONG\n");
    return 1;
  }

  /*
   * 帮助参数：-h / --help
   * 两者均为纯 ASCII，直接比较 argv[1] 即可，不受 Windows ACP 编码影响。
   */
  if (argc == 2 && (std::strcmp(argv[1], "-h") == 0 ||
                    std::strcmp(argv[1], "--help") == 0)) {
    print_help();
    return 0;
  }

  /* 模式一：无参数 — 自动扫描当前目录 */
  if (argc == 1) {
    /* 检查当前目录是否可写（需要输出解码后的文件） */
    if ((fs::status(fs::path(".")).permissions() & fs::perms::owner_write) ==
        fs::perms::none) {
      std::cerr << "错误：当前目录无写入权限，请检查目录权限。"
                << std::endl;
      return -1;
    }

    /* 递归遍历当前目录，收集所有 QMC 格式的文件 */
    std::vector<fs::path> qmc_paths;

    for (auto& p : fs::recursive_directory_iterator(fs::path("."))) {
      auto file_path = p.path();

      /* 只处理可读的普通文件 */
      if ((fs::status(p).permissions() & fs::perms::owner_read) !=
              fs::perms::none &&
          fs::is_regular_file(p)) {
        /* 大小写不敏感地匹配扩展名 */
        auto ext = toUtf8(file_path.extension());
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (ext == ".qmc3" || ext == ".qmc0" || ext == ".qmcflac" ||
            ext == ".qmcogg") {
          qmc_paths.emplace_back(std::move(file_path));
        }
      }
    };

    /* 逐个解码所有收集到的 QMC 文件 */
    std::for_each(qmc_paths.begin(), qmc_paths.end(), sub_process);

    return 0;
  }

  /* 模式二：指定文件路径 — 解码单个文件 */
  /*
   * Windows 上 argv[1] 是系统活动代码页（ACP）编码的窄字符串。
   * 例如简体中文系统上为 GBK (CP936) 编码，直接使用会导致含
   * 中文的文件路径无法正确传递给 fs::path。
   *
   * 解决方案：用 GetCommandLineW() 获取完整命令行宽字符串，
   * 再用 CommandLineToArgvW() 解析为 UTF-16LE 的参数数组，
   * 然后直接构造 fs::path，全程不经过窄字符串编码转换。
   */
#ifdef _WIN32
  int wargc;
  LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
  if (wargv == nullptr || wargc < 2) {
    std::cerr << "错误：无法解析命令行参数" << std::endl;
    return -1;
  }
  fs::path qmc_path(wargv[1]);
  LocalFree(wargv);  /* 释放 CommandLineToArgvW 分配的内存 */
#else
  /* Unix 系统上 argv 本身就是 UTF-8 编码，可直接使用 */
  fs::path qmc_path(argv[1]);
#endif

  /* 检查参数指定路径的可读性 */
  if ((fs::status(qmc_path).permissions() & fs::perms::owner_read) ==
      fs::perms::none) {
    std::cerr << "错误：文件无读取权限，请检查文件路径和权限。"
              << std::endl;
    return -1;
  }

  /* 解码指定的文件 */
  sub_process(qmc_path);

  return 0;
}
