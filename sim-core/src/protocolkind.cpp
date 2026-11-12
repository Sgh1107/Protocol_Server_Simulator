// SPDX-License-Identifier: GPL-3.0-or-later
#include "protosim/protocolkind.h"

namespace protosim {

QString protocolKindToString(ProtocolKind kind)
{
    switch (kind) {
    case ProtocolKind::Modbus:
        return QStringLiteral("modbus");
    case ProtocolKind::Iec104:
        return QStringLiteral("iec104");
    case ProtocolKind::Mqtt:
        return QStringLiteral("mqtt");
    case ProtocolKind::Iec61850:
        return QStringLiteral("iec61850");
    }
    return QStringLiteral("unknown");
}

bool protocolKindFromString(const QString &text, ProtocolKind *out)
{
    ProtocolKind kind = ProtocolKind::Modbus;
    bool matched = true;

    if (text == QLatin1String("modbus")) {
        kind = ProtocolKind::Modbus;
    } else if (text == QLatin1String("iec104")) {
        kind = ProtocolKind::Iec104;
    } else if (text == QLatin1String("mqtt")) {
        kind = ProtocolKind::Mqtt;
    } else if (text == QLatin1String("iec61850")) {
        kind = ProtocolKind::Iec61850;
    } else {
        matched = false;
    }

    if (matched && out)
        *out = kind;
    return matched;
}

} // namespace protosim
