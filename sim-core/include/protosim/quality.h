// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

namespace protosim {

/// 质量码。随快照一路传递到协议出口，质量非 Good 时各适配器按自己的规范表达
/// （Modbus 无原生品质位，由 UI/审计体现；104 上送无效品质描述词）。
/// 禁止静默把非 Good 当正常值（AC-P0-12）。
enum class Quality {
    Good,
    Bad,
    Timeout,
    OutOfRange,
    Unknown,
};

QString qualityToString(Quality quality);
Quality qualityFromString(const QString &text, Quality fallback = Quality::Unknown);

/// 是否可用于对外呈现为"有效值"。
inline bool isGood(Quality quality)
{
    return quality == Quality::Good;
}

} // namespace protosim
