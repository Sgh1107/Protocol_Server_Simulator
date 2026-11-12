// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

namespace protosim {

/// 应用版本号，来自 cmake/ProjectVersions.cmake（编译期注入）。
QString versionString();

/// 版本 + 编译器 + Qt 运行时版本，用于「关于」页与 --selftest / 缺陷复现。
QString buildInfoString();

} // namespace protosim
