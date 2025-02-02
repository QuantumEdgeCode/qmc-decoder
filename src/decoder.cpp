/*
 * Author: mayusheng - mayusheng@huawei.com
 * Last modified: 2020-06-29 10:56
 * Filename: decoder.cpp
 *
 * Description: qmc file auto decode
 *
 *
 */
#include <iostream>
#include <memory>
#include <regex>
#include <vector>
#include <fstream>
#include <filesystem>
#include <windows.h>
#include "seed.hpp"

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

namespace {
void close_file(std::FILE* fp) { std::fclose(fp); }
using smartFilePtr = std::unique_ptr<std::FILE, decltype(&close_file)>;

enum class openMode { read, write };

// 打开文件
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

// 匹配的文件类型
static const std::regex mp3_regex{"\\.(qmc3|qmc0)$"};
static const std::regex ogg_regex{"\\.qmcogg$"};
static const std::regex flac_regex{"\\.qmcflac$"};

// 处理文件的解码过程
void sub_process(std::string dir) {
    std::cout << "Decoding: " + dir << std::endl;
    std::string outloc(dir);

    auto mp3_outloc = std::regex_replace(outloc, mp3_regex, ".mp3");
    auto flac_outloc = std::regex_replace(outloc, flac_regex, ".flac");
    auto ogg_outloc = std::regex_replace(outloc, ogg_regex, ".ogg");

    if (mp3_outloc != outloc)
        outloc = mp3_outloc;
    else if (flac_outloc != outloc)
        outloc = flac_outloc;
    else
        outloc = ogg_outloc;

    auto infile = openFile(dir, openMode::read);

    if (infile == nullptr) {
        std::cerr << "Failed to read file: " << outloc << std::endl;
        return;
    }

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

    auto fres = fread(buffer.get(), 1, len, infile.get());
    if (fres != len) {
        std::cerr << "Read file error" << std::endl;
    }

    qmc_decoder::seed seed_;
    for (int i = 0; i < len; ++i) {
        buffer[i] = seed_.next_mask() ^ buffer[i];
    }

    auto outfile = openFile(outloc, openMode::write);

    if (outfile == nullptr) {
        std::cerr << "Failed to write file: " << outloc << std::endl;
        return;
    }

    fres = fwrite(buffer.get(), 1, len, outfile.get());
    if (fres != len) {
        std::cerr << "Write file error" << std::endl;
    }
}

// 文件类型正则表达式
static const std::regex qmc_regex{"^.+\\.(qmc3|qmc0|qmcflac|qmcogg)$"};
}  // namespace

// 设置控制台编码
void setConsoleEncoding() {
    char localeInfo[LOCALE_NAME_MAX_LENGTH];
    if (GetLocaleInfoA(LOCALE_USER_DEFAULT, LOCALE_SISO639LANGNAME, localeInfo, LOCALE_NAME_MAX_LENGTH)) {
        std::string locale(localeInfo);
        if (locale == "zh") {
            SetConsoleOutputCP(936); // GBK 编码
            SetConsoleCP(936); // 输入也设置为 GBK 编码
            std::cout << "Detected Chinese environment. Console encoding set to GBK." << std::endl;
        } else {
            SetConsoleOutputCP(CP_UTF8);
            SetConsoleCP(CP_UTF8);
            std::cout << "Non-Chinese environment detected. Console encoding set to UTF-8." << std::endl;
        }
    }
}

int main(int argc, char** argv) {
    // 设置控制台编码
    setConsoleEncoding();

    // 处理命令行参数
    if (argc > 2) {
        std::cout << "Usage: qmc-decoder /path/to/file" << std::endl;
        return 1;
    }

    if (argc == 1) {
        if ((fs::status(fs::path(".")).permissions() & fs::perms::owner_write) == fs::perms::none) {
            std::cerr << "Please check if you have write permissions on this directory." << std::endl;
            return -1;
        }
        std::vector<std::string> qmc_paths;

        for (auto& p : fs::recursive_directory_iterator(fs::path("."))) {
            auto file_path = p.path().string();
            if ((fs::status(p).permissions() & fs::perms::owner_read) != fs::perms::none &&
                fs::is_regular_file(p) && std::regex_match(file_path, qmc_regex)) {
                qmc_paths.emplace_back(std::move(file_path));
            }
        }

        for (const auto& path : qmc_paths) {
            sub_process(path);
        }

        return 0;
    }

    // 如果提供了文件路径
    auto qmc_path = argv[1];
    if ((fs::status(fs::path(qmc_path)).permissions() & fs::perms::owner_read) == fs::perms::none) {
        std::cerr << "Please check if you have read permissions on this file." << std::endl;
        return -1;
    }

    sub_process(qmc_path);

    return 0;
}
