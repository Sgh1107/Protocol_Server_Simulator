# SPDX-License-Identifier: GPL-3.0-or-later
#
# 全仓统一的编译选项。interface library 形式，谁 link 谁生效。
# 纪律：Qt 预编译库、主程序、将来 lib60870/libiec61850 必须同一编译器、同一 CRT（/MD）、同一架构。

add_library(protosim_compile_options INTERFACE)

target_compile_features(protosim_compile_options INTERFACE cxx_std_${PROTOSIM_CXX_STANDARD})

# 关掉 Qt 的 signals / slots / emit 宏：本项目的核心名词就是「信号」，
# 留着这些宏会让 signal/signals 这类业务命名直接编译失败。
# 代价是信号槽声明写 Q_SIGNALS / Q_SLOTS、发信号写 Q_EMIT，语义更明确。
target_compile_definitions(protosim_compile_options INTERFACE QT_NO_KEYWORDS)

if(MSVC)
    target_compile_options(protosim_compile_options INTERFACE
        /W4                 # 高警告等级；Qt 是 imported target，头文件按 /external 处理不报噪音
        /permissive-        # 标准一致性
        /utf-8              # 源码与执行字符集统一 UTF-8，中文字面量不乱码
        /Zc:__cplusplus     # __cplusplus 报真实值，避免被当成 199711L
        /external:anglebrackets
        /external:W0
        /MP                 # 并行编译
    )
    target_compile_definitions(protosim_compile_options INTERFACE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        UNICODE
        _UNICODE
    )
    if(PROTOSIM_WERROR)
        target_compile_options(protosim_compile_options INTERFACE /WX)
    endif()
else()
    target_compile_options(protosim_compile_options INTERFACE
        -Wall -Wextra -Wpedantic
    )
    if(PROTOSIM_WERROR)
        target_compile_options(protosim_compile_options INTERFACE -Werror)
    endif()
endif()
