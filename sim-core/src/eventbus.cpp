// SPDX-License-Identifier: GPL-3.0-or-later
#include "protosim/eventbus.h"

namespace protosim {

EventBus::EventBus(QObject *parent)
    : QObject(parent)
{
}

void EventBus::attachDataBank(DataBank *bank)
{
    if (m_bank == bank)
        return;

    if (m_bank)
        disconnect(m_bank, &DataBank::signalChanged, this, &EventBus::onSignalChanged);

    m_bank = bank;

    if (m_bank) {
        connect(m_bank, &DataBank::signalChanged, this, &EventBus::onSignalChanged,
                Qt::DirectConnection);
    }
}

void EventBus::registerAdapter(IProtocolAdapter *adapter)
{
    if (adapter == nullptr || m_adapters.contains(adapter))
        return;

    m_adapters.append(adapter);

    // 新适配器上线，立刻把当前快照补一遍，避免"后半场加入读不到值"
    if (m_bank) {
        const QVector<SignalSnapshot> snapshots = m_bank->snapshots();
        QVector<SignalChange> changes;
        changes.reserve(snapshots.size());
        for (const SignalSnapshot &snapshot : snapshots) {
            SignalChange change;
            change.signalId = snapshot.signalId;
            change.newValue = snapshot.value;
            change.quality = snapshot.quality;
            change.timestampMs = snapshot.timestampMs;
            change.forced = snapshot.forced;
            change.sourceProtocol = QStringLiteral("core");
            changes.append(change);
        }
        if (!changes.isEmpty())
            adapter->pushTelemetry(changes);
    }
}

void EventBus::unregisterAdapter(IProtocolAdapter *adapter)
{
    m_adapters.removeAll(adapter);
}

QVector<IProtocolAdapter *> EventBus::adapters() const
{
    return m_adapters;
}

quint64 EventBus::broadcastCount() const
{
    return m_broadcastCount;
}

void EventBus::onSignalChanged(const protosim::SignalChange &change)
{
    if (m_adapters.isEmpty())
        return;

    ++m_broadcastCount;

    // 只推 diff，且不推回来源协议自己（它刚写完，不需要再吃一遍）
    const QVector<SignalChange> batch{change};
    for (IProtocolAdapter *adapter : m_adapters) {
        if (adapter == nullptr)
            continue;
        if (!change.sourceProtocol.isEmpty()
            && protocolKindToString(adapter->capabilities().kind) == change.sourceProtocol) {
            continue;
        }
        adapter->pushTelemetry(batch);
    }
}

} // namespace protosim
