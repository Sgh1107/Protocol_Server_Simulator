# SPDX-License-Identifier: GPL-3.0-or-later
#
# ProtoSim — 工业协议服务端模拟器
# 构建：cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=<Qt6.6.3/msvc2019_64>
# 纪律：sim-core 不链接任何协议库（AC-P0-01 / AC-P0-19）。

cmake_minimum_required(VERSION 3.23)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ProjectVersions)

project(ProtoSim VERSION ${PROTOSIM_VERSION} LANGUAGES CXX)

message(STATUS "ProtoSim ${PROJECT_VERSION} | Qt 基线 ${PROTOSIM_QT_VERSION} | C++${PROTOSIM_CXX_STANDARD}")

# ---------------------------------------------------------------- 全局设置

set(CMAKE_CXX_STANDARD ${PROTOSIM_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# /MD —— 与 Qt 官方 msvc2019_64 预编译库同一个 CRT，混用会炸
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC OFF)   # 不用 .ui 文件，界面全部手写代码

option(PROTOSIM_WERROR "把警告当错误（CI 打开）" OFF)
option(PROTOSIM_BUILD_TESTS "构建测试" ON)

# 产物集中到 build/bin、build/lib，多配置生成器也压平，避免 bin/Debug 套娃
set(PROTOSIM_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin")
set(PROTOSIM_LIB_DIR "${CMAKE_BINARY_DIR}/lib")
foreach(_cfg IN ITEMS DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_cfg} "${PROTOSIM_OUTPUT_DIR}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${_cfg} "${PROTOSIM_OUTPUT_DIR}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${_cfg} "${PROTOSIM_LIB_DIR}")
endforeach()

# ---------------------------------------------------------------- Qt

# EXACT 是刻意的：全项目必须是同一 Qt 版本，出现混装立刻失败。
find_package(Qt6 ${PROTOSIM_QT_VERSION} EXACT REQUIRED COMPONENTS
    Core
    Network
    SerialBus
    Widgets
    Quick
    QuickWidgets
    Charts
    Test
)

include(CompilerOptions)

# ---------------------------------------------------------------- 子目录

add_subdirectory(sim-core)     # 纯逻辑：不依赖协议库、不依赖 GUI
add_subdirectory(adapters)     # 协议适配器（唯一允许链接 SerialBus 的地方）
add_subdirectory(ui)           # Widgets + QML
add_subdirectory(app)          # 可执行入口

if(PROTOSIM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
