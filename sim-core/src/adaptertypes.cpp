// SPDX-License-Identifier: GPL-3.0-or-later
//
// IProtocolAdapter 相关枚举的文本映射。放在 core 里，
// 保证 UI / 日志 / 自检用的是同一套字面量（避免各处自己拼字符串）。

#include "protosim/iprotocoladapter.h"

namespace protosim {

QString adapterStateToString(AdapterState state)
{
    switch (state) {
    case AdapterState::Created:
        return QStringLiteral("Created");
    case AdapterState::Bound:
        return QStringLiteral("Bound");
    case AdapterState::Running:
        return QStringLiteral("Running");
    case AdapterState::Stopped:
        return QStringLiteral("Stopped");
    case AdapterState::Fault:
        return QStringLiteral("Fault");
    }
    return QStringLiteral("Unknown");
}

QString writeOutcomeToString(WriteOutcome outcome)
{
    switch (outcome) {
    case WriteOutcome::Accepted:
        return QStringLiteral("Accepted");
    case WriteOutcome::RejectedWriteProtected:
        return QStringLiteral("RejectedWriteProtected");
    case WriteOutcome::RejectedInterlock:
        return QStringLiteral("RejectedInterlock");
    case WriteOutcome::RejectedOutOfRange:
        return QStringLiteral("RejectedOutOfRange");
    case WriteOutcome::RejectedUnknownSignal:
        return QStringLiteral("RejectedUnknownSignal");
    }
    return QStringLiteral("Unknown");
}

} // namespace protosim
