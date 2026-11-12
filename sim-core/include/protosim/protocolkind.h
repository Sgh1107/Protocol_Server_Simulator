// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

namespace protosim {

/// 协议种类。新增协议在此加一项，UI 通过 capabilities() 动态渲染，不改 UI 代码。
enum class ProtocolKind {
    Modbus,
    Iec104,
    Mqtt,
    Iec61850,
};

QString protocolKindToString(ProtocolKind kind);

/// 配置包里的协议名（"modbus" / "iec104" / "mqtt" / "iec61850"）。
/// 无法识别时返回 false，由调用方给出可展示的错误，不做默认猜测。
bool protocolKindFromString(const QString &text, ProtocolKind *out);

} // namespace protosim
