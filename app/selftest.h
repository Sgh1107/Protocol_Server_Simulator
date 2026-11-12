// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace protosim {

/// 命令行 `--selftest` 入口：9 项内置自检，全部通过返回 0。
/// 设计原则：不依赖任何外部服务、不写文件到工作区、端口用临时端口。
/// 这是"解压即可自证"的最后一公里（AC-P0-16）。
int runSelfTest();

} // namespace protosim
