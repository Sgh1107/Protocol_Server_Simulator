// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "protosim/databank.h"
#include "protosim/iprotocoladapter.h"

#include <QObject>
#include <QVector>

namespace protosim {

/// 联动总线：任一路径改了实时库，除来源协议之外的所有适配器都收到 pushTelemetry。
///
/// 这条是「一个实时库，多协议同时对外」的落地机制，
/// 也是 AC-P0-11（FakeAdapter A 写、B 收到）与 P0 之后 104 / MQTT 联动的底座。
class EventBus : public QObject
{
    Q_OBJECT

public:
    explicit EventBus(QObject *parent = nullptr);

    /// 接管 DataBank 的变化事件。重复调用以最后一次为准。
    void attachDataBank(DataBank *bank);

    void registerAdapter(IProtocolAdapter *adapter);
    void unregisterAdapter(IProtocolAdapter *adapter);
    QVector<IProtocolAdapter *> adapters() const;

    /// 广播次数统计，自检与压测用。
    quint64 broadcastCount() const;

private:
    void onSignalChanged(const protosim::SignalChange &change);

    DataBank *m_bank = nullptr;
    QVector<IProtocolAdapter *> m_adapters;
    quint64 m_broadcastCount = 0;
};

} // namespace protosim
