# SPDX-License-Identifier: GPL-3.0-or-later
#
# 全仓唯一的版本真相源。
# 任何依赖升级必须同步：本文件 -> README §3.1 -> THIRD_PARTY_NOTICES.md。
# Qt 6.6.3 已 EOL（知情接受，见 TECH_STACK_DECISIONS §3.5）：将来切换版本只改这里，
# 并且禁止使用 6.7+ API（tools/check_forbidden_api.py 会在 CI 里拦）。

set(PROTOSIM_VERSION "0.1.0")

# Qt 基线：全项目单基线，所有模块必须同版本，故 find_package 一律加 EXACT。
set(PROTOSIM_QT_VERSION "6.6.3" CACHE STRING "Qt 基线版本（全项目唯一）")

# CMake 最低版本（README §2；Qt 6.6 自身最低 3.16）
set(PROTOSIM_CMAKE_MIN_VERSION "3.23")

# C++ 标准
set(PROTOSIM_CXX_STANDARD "17")
