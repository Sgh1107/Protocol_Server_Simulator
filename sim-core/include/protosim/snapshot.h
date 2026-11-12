// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "protosim/quality.h"

#include <QMetaType>
#include <QString>
#include <QVariant>

namespace protosim {

/// DataBank 对外暴露的唯一读视图：值 + 质量 + 时标 + 强制标记。
/// 适配器读点必须拿这个，不允许自己缓存旧值。
struct SignalSnapshot
{
    QString signalId;
    QVariant value;
    Quality quality = Quality::Unknown;
    qint64 timestampMs = 0;
    bool forced = false;

    bool isValid() const { return !signalId.isEmpty(); }
    double toDouble(bool *ok = nullptr) const;
    qint64 toLongLong(bool *ok = nullptr) const;
};

/// 一次值变化的完整描述。写回管线与 EventBus 都靠它驱动联动与 UI 刷新。
struct SignalChange
{
    QString signalId;
    QVariant oldValue;
    QVariant newValue;
    Quality quality = Quality::Unknown;
    qint64 timestampMs = 0;
    QString sourceProtocol; // "engine" / "modbus" / "iec104" / "ui" ...
    QString clientId;       // 谁写的，审计与报文日志都要（AC-P0-10）
    bool forced = false;
};

} // namespace protosim

Q_DECLARE_METATYPE(protosim::SignalSnapshot)
Q_DECLARE_METATYPE(protosim::SignalChange)
