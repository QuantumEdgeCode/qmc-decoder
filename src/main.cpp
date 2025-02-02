#include <algorithm>
#include <iostream>
#include <memory>
#include <regex>
#include <vector>
#include <filesystem>
#include <cstdio>

#include "seed.hpp"

// 文件操作支持跨平台
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

// 用于文件操作的智能指针
void close_file(std::FILE* fp) {
  std::fclose(fp);
}

using smartFilePtr = std::unique_ptr<std::FILE, decltype(&close_file)>;

// 打开文件的函数
enum class openMode { read, write };

smartFilePtr openFile(const std::string& aPath, openMode aOpenMode) {
#ifndef _WIN32
  std::FILE* fp =
      fopen(aPath.c_str(), aOpenMode == openMode::read ? "rb" : "wb");
#else
  std::wstring aPath_w;
  aPath_w.resize(aPath.size());
  int newSize = MultiByteToWideChar(
      CP_UTF8, 0, aPath.c_str(), static_cast<int>(aPath.length()),
      const_cast<wchar_t*>(aPath_w.c_str()), static_cast<int>(aPath_w.size()));
  aPath_w.resize(newSize);
  std::FILE* fp = NULL;
  _wfopen_s(&fp, aPath_w.c_str(), aOpenMode == openMode::read ? L"rb" : L"wb");
#endif
  return smartFilePtr(fp, &close_file);
}

// 正则表达式匹配不同格式的文件
static const std::regex mp3_regex{"\\.(qmc3|qmc0)$"};
static const std::regex ogg_regex{"\\.qmcogg$"};
static const std::regex flac_regex{"\\.qmcflac$"};
static const std::regex qmc_regex{"^.+\\.(qmc3|qmc0|qmcflac|qmcogg)$"};

// 解码文件的处理函数
void sub_process(std::string dir) {
  std::cout << "Decoding: " + dir << std::endl;
  std::string outloc(dir);

  // 生成不同的输出文件路径
  auto mp3_outloc = std::regex_replace(outloc, mp3_regex, ".mp3");
  auto flac_outloc = std::regex_replace(outloc, flac_regex, ".flac");
  auto ogg_outloc = std::regex_replace(outloc, ogg_regex, ".ogg");

  if (mp3_outloc != outloc)
    outloc = mp3_outloc;
  else if (flac_outloc != outloc)
    outloc = flac_outloc;
  else
    outloc = ogg_outloc;

  // 打开输入文件
  auto infile = openFile(dir, openMode::read);

  if (infile == nullptr) {
    std::cerr << "Failed to read file: " << outloc << std::endl;
    return;
  }

  // 获取文件的长度
  int res = fseek(infile.get(), 0, SEEK_END);
  if (res != 0) {
    std::cerr << "Seek file failed" << std::endl;
    return;
  }

  auto len = ftell(infile.get());
  res = fseek(infile.get(), 0, SEEK_SET);

  std::unique_ptr<char[]> buffer(new (std::nothrow) char[len]);
  if (buffer == nullptr) {
    std::cerr << "Create buffer error" << std::endl;
    return;
  }

  // 读取文件内容到缓冲区
  auto fres = fread(buffer.get(), 1, len, infile.get());
  if (fres != len) {
    std::cerr << "Read file error" << std::endl;
  }

  // 初始化种子生成器
  qmc_decoder::seed seed_;
  
  // 使用种子生成器对文件内容进行解码
  for (int i = 0; i < len; ++i) {
    buffer[i] = seed_.next_mask() ^ buffer[i];
  }

  // 打开输出文件
  auto outfile = openFile(outloc, openMode::write);

  if (outfile == nullptr) {
    std::cerr << "Failed to write file: " << outloc << std::endl;
    return;
  }

  // 写入解码后的数据
  fres = fwrite(buffer.get(), 1, len, outfile.get());
  if (fres != len) {
    std::cerr << "Write file error" << std::endl;
  }
}

int main(int argc, char** argv) {
  // 命令行参数解析
  if (argc > 2) {
    printf(
        "Put the binary in the same directory as your qmc files then run it, "
        "or use the CLI interface: qmc-decoder /PATH/TO/SONG\n");
    return 1;
  }

  if (argc == 1) {
    // 检查当前目录是否具有写权限
    if ((fs::status(fs::path(".")).permissions() & fs::perms::owner_write) ==
        fs::perms::none) {
      std::cerr << "Please check if you have write permissions on this directory."
                << std::endl;
      return -1;
    }

    std::vector<std::string> qmc_paths;

    // 遍历目录，查找符合条件的文件
    for (auto& p : fs::recursive_directory_iterator(fs::path("."))) {
      auto file_path = p.path().string();

      if ((fs::status(p).permissions() & fs::perms::owner_read) !=
              fs::perms::none &&
          fs::is_regular_file(p) && std::regex_match(file_path, qmc_regex)) {
        qmc_paths.emplace_back(std::move(file_path));
      }
    }

    // 依次处理文件
    std::for_each(qmc_paths.begin(), qmc_paths.end(), sub_process);

    return 0;
  }

  // 处理传入的单个文件路径
  auto qmc_path = argv[1];

  // 检查文件的读权限
  if ((fs::status(fs::path(qmc_path)).permissions() & fs::perms::owner_read) ==
      fs::perms::none) {
    std::cerr << "Please check if you have read permissions on this file."
              << std::endl;
    return -1;
  }

  sub_process(qmc_path);

  return 0;
}
