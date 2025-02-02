cmake_minimum_required(VERSION 3.10)  # 更新到更高版本
project(qmc-decoder)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 设置 C++ 标准为 C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC 特定设置
if (MSVC)
  # 启用优化
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /O2")
  # 可以根据需要在这里添加更多的 MSVC 特定设置
else()
  # 对于非 MSVC 系统（例如 Linux 和其他平台）
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -pipe -std=c++11")
endif()

# 针对不同操作系统的设置
if (CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
  # 静态链接和其他 Linux 特定的设置
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static -pthread -static-libgcc -static-libstdc++")
elseif (CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
  # 这里可以添加 Windows 特定的编译选项（如果需要的话）
  # 例如，您可以设置对 Windows 特性的特定支持
endif()

# 3rdparty 目录中的外部依赖
include_directories(3rdparty/filesystem/include)

# 查找源文件并添加
aux_source_directory(src SRC)

# 创建可执行文件
add_executable(qmc-decoder ${SRC})

# 如果有安装需求，执行安装操作
install(TARGETS qmc-decoder DESTINATION bin)

# 针对 Windows 特定的设置，您可以在这里添加更多库或链接设置
if (MSVC)
  # MSVC 可能需要一些特定的库链接
  # target_link_libraries(qmc-decoder <libs>)
endif()

# 在 Linux 上，您可能需要链接到一些特定的库（例如 pthread）
if (CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
  target_link_libraries(qmc-decoder pthread)
endif()
