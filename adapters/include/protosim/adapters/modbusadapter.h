// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "protosim/adapters/modbusframe.h"
#include "protosim/adapters/modbustap.h"
#include "protosim/configbundle.h"
#include "protosim/databank.h"
#include "protosim/iprotocoladapter.h"
#include "protosim/telemetrylog.h"
#include "protosim/writepipeline.h"

#include <QElapsedTimer>
#include <QHash>
#include <QModbusDataUnit>
#include <QModbusPdu>
#include <QModbusTcpServer>
#include <QObject>
#include <QQueue>

#include <functional>

namespace protosim {

class ModbusTcpAdapter;

/// QModbusTcpServer 的子类：只做三件 Qt 原生不做的事 ——
///   1. 在 processRequest 里按 Modbus 规范给出精确异常码（0x01/0x02/0x03）；
///   2. 把 readData / writeData 桥到 DataBank 与 WritePipeline；
///   3. 记录「本次请求来自哪个客户端」的归属，供审计与报文日志使用。
///
/// 协议编解码（MBAP、功能码解析、响应组帧）全部留给 Qt，不自研。
class ModbusServerCore : public QModbusTcpServer
{
public:
    explicit ModbusServerCore(ModbusTcpAdapter *owner);

public:
    /// 交给 Qt 的原始实现做编解码。processRequest 是 protected，
    /// 适配器自己不是 QModbusTcpServer 的子类，因此留这个转调入口。
    QModbusResponse delegateToBase(const QModbusPdu &request);

protected:
    QModbusResponse processRequest(const QModbusPdu &request) override;
    bool readData(QModbusDataUnit *newData) const override;
    bool writeData(const QModbusDataUnit &unit) override;

private:
    ModbusTcpAdapter *m_owner;
};

/// Modbus TCP 服务端适配器。
///
/// 结构：对外端口 -> ModbusFrameTap（帧观察层）-> 内部端口上的 QModbusTcpServer。
/// 主站侧看到的还是一个标准 Modbus TCP 从站，端口就是配置里的 1502。
class ModbusTcpAdapter : public QObject, public IProtocolAdapter
{
    Q_OBJECT

public:
    ModbusTcpAdapter(DataBank *bank, WritePipeline *pipeline, TelemetryLog *log,
                     QObject *parent = nullptr);
    ~ModbusTcpAdapter() override;

    // ---- IProtocolAdapter ----
    Capabilities capabilities() const override;
    Result<void> start(const EndpointConfig &config) override;
    Result<void> stop() override;
    Result<void> bind(const QVector<Binding> &bindings) override;
    AdapterState state() const override;
    ReadResult onRead(const ReadRequest &request) override;
    WriteResult onWrite(const WriteRequest &request) override;
    void pushTelemetry(const QVector<SignalChange> &changes) override;
    Result<void> injectFault(const FaultSpec &spec) override;
    AdapterStats stats() const override;

    // ---- 供 ModbusServerCore 回调 ----

    /// 处理一个 PDU 请求：先做规范级校验并给出精确异常码，再交给 Qt。
    QModbusResponse handleRequest(const QModbusPdu &request);

    /// 从 DataBank 取快照填进 QModbusDataUnit；地址未映射返回 false（-> 0x02）。
    bool readDataUnit(QModbusDataUnit *unit) const;

    /// 把写入的原始寄存器拼回信号、解码成工程值，提交统一写回管线。
    bool writeDataUnit(const QModbusDataUnit &unit);

    /// 当前请求的客户端标识；无归属时返回 "-"。
    QString currentClientId() const { return m_currentClientId; }

    /// 统一时钟源：写回时标。默认墙上时间；主程序会接到仿真时钟上。
    void setClockSource(std::function<qint64()> clock);
    qint64 nowMs() const;

    // ---- 配置装配 ----

    /// 把配置包的 modbus 绑定翻成接口层的 Binding 列表。
    static QVector<Binding> bindingsFromBundle(const ConfigBundle &bundle);

    quint16 listenPort() const;
    int activeClients() const;

Q_SIGNALS:
    void clientCountChanged(int activeClients);

private:
    struct Cell
    {
        QString signalId;
        int offset = 0; // 在信号寄存器向量里的下标
    };

    struct Pending
    {
        QString clientId;
        quint16 transactionId = 0;
        qint64 arrivedMs = 0;
    };

    void onTapFrame(const QByteArray &aduBytes, const QString &clientId, bool inbound,
                    qint64 timestampMs);
    void logFrame(const QByteArray &aduBytes, const modbus::Adu &adu, const QString &clientId,
                  bool inbound, qint64 timestampMs, qint64 elapsedUs,
                  const QStringList &signalIds);
    QStringList signalsForRange(quint8 functionCode, quint16 start, quint16 count) const;

    Cell cellAt(int unitId, QModbusDataUnit::RegisterType type, int address) const;
    QVector<quint16> rawRegistersOf(const QString &signalId) const;
    static QString cellKey(int unitId, QModbusDataUnit::RegisterType type, int address);
    static QModbusDataUnit::RegisterType registerTypeOf(quint8 functionCode);
    static bool registerTypeFromName(const QString &name, QModbusDataUnit::RegisterType *out);

    DataBank *m_bank = nullptr;
    WritePipeline *m_pipeline = nullptr;
    TelemetryLog *m_log = nullptr;

    ModbusFrameTap m_tap;
    ModbusServerCore *m_server = nullptr;

    QHash<QString, Cell> m_cells;
    QQueue<Pending> m_pending;
    /// 请求发出时记下"耗时起点 + 涉及的点"，响应回来时用它算耗时、并让响应行也能按信号过滤
    struct PendingFrame
    {
        QElapsedTimer timer;
        QStringList signalIds;
    };
    QHash<QString, PendingFrame> m_pendingElapsed; // key = "client|txid"
    QString m_currentClientId = QStringLiteral("-");

    int m_unitId = 1;
    quint16 m_port = 1502;
    int m_maxClients = 8;
    AdapterState m_state = AdapterState::Created;
    std::function<qint64()> m_clock;

    quint64 m_framesRx = 0;
    quint64 m_framesTx = 0;
    quint64 m_errorFrames = 0;
    qint64 m_startedAtMs = 0;
    int m_lastPushCount = 0; // 联动广播收到的条数，仅用于自检与 stats
};

} // namespace protosim
