# QMC Decoder — CMake 构建配置文件
# 支持的构建目标：
#   Windows : MSVC x86 / x64 / ARM64（ARM64 可在 x64 主机交叉编译，也可原生编译）
#   Linux   : x64 / arm64（GCC/Clang，静态链接）
#   macOS   : arm64 / x86_64 / universal2（Apple Silicon 与 Intel 通吃）
cmake_minimum_required(VERSION 3.15)
project(qmc-decoder)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 统一使用 C++17：三平台标准库均提供 std::filesystem，
# 不再强依赖 ghc::filesystem（该子模块仅作为 C++11/14 环境下的回退保留）。
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 编译器选项
if (MSVC)
  # 静态 CRT：exe 不依赖 VC++ Redistributable，下载即用
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  # /utf-8          源码为无 BOM 的 UTF-8，避免 MSVC 按本地代码页（936/1252）
  #                 解析中文注释与字面量时触发 C4819，甚至 C2001（字符串字面量跨行）
  # /Zc:__cplusplus 让 __cplusplus 正确报告 201703L，从而启用 std::filesystem 分支
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /O2 /utf-8 /Zc:__cplusplus")
else(MSVC)
  # Unix/Linux/macOS: GCC/Clang
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -pipe")
endif()

# Linux 下静态链接，产物可直接分发到同架构机器运行
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static -pthread -static-libgcc -static-libstdc++")
endif()

# ghc::filesystem 回退头文件路径（C++17 下不会用到；子模块未初始化时自动跳过）
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/filesystem/include/ghc/filesystem.hpp")
  include_directories(3rdparty/filesystem/include)
endif()

aux_source_directory(src SRC)
add_executable(qmc-decoder ${SRC})

# Windows 额外依赖：CommandLineToArgvW 位于 shell32.lib
if (MSVC)
  target_link_libraries(qmc-decoder shell32.lib)
endif()

install(TARGETS qmc-decoder DESTINATION bin)
