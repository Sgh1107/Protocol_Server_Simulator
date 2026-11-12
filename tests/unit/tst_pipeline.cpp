// SPDX-License-Identifier: GPL-3.0-or-later
//
// L1：统一写回管线 + 联动总线（AC-P0-11 的核心机制）。
// AC-P0-10 的「审计含来源协议与客户端」也在这里验，不依赖 socket。

#include "protosim/databank.h"
#include "protosim/eventbus.h"
#include "protosim/iprotocoladapter.h"
#include "protosim/telemetrylog.h"
#include "protosim/writepipeline.h"

#include <QtTest>

using namespace protosim;

namespace {

Signal makeSignal(const QString &id, double min = -1000.0, double max = 1000.0)
{
    Signal signal;
    signal.id = id;
    signal.name = id;
    signal.deviceId = QStringLiteral("dev-01");
    signal.dataType = DataType::Uint16;
    signal.range.min = min;
    signal.range.max = max;
    signal.range.valid = true;
    return signal;
}

/// 一个"假协议"适配器：验证「不改 core 任何代码即可注册、收发、注入」。
class FakeAdapter : public IProtocolAdapter
{
public:
    explicit FakeAdapter(ProtocolKind kind, QString name)
        : m_kind(kind)
        , m_name(std::move(name))
    {
    }

    Capabilities capabilities() const override
    {
        Capabilities caps;
        caps.kind = m_kind;
        caps.supportsPush = true;
        return caps;
    }

    Result<void> start(const EndpointConfig &) override
    {
        m_state = AdapterState::Running;
        return {};
    }
    Result<void> stop() override
    {
        m_state = AdapterState::Stopped;
        return {};
    }
    Result<void> bind(const QVector<Binding> &bindings) override
    {
        m_bindings = bindings;
        return {};
    }
    AdapterState state() const override { return m_state; }

    ReadResult onRead(const ReadRequest &) override { return {}; }
    WriteResult onWrite(const WriteRequest &) override { return {}; }

    void pushTelemetry(const QVector<SignalChange> &changes) override
    {
        pushed.append(changes);
    }

    Result<void> injectFault(const FaultSpec &) override { return {}; }
    AdapterStats stats() const override { return {}; }

    QVector<SignalChange> pushed;
    QString name() const { return m_name; }

private:
    ProtocolKind m_kind;
    QString m_name;
    AdapterState m_state = AdapterState::Created;
    QVector<Binding> m_bindings;
};

} // namespace

class TestPipeline : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void writeUpdatesBankAndAudits()
    {
        DataBank bank;
        bank.registerSignal(makeSignal(QStringLiteral("a")));
        WritePipeline pipeline(&bank);

        WriteIntent intent;
        intent.signalId = QStringLiteral("a");
        intent.value = 12.5;
        intent.sourceProtocol = QStringLiteral("modbus");
        intent.clientId = QStringLiteral("127.0.0.1:50000");

        QCOMPARE(pipeline.submit(intent, 1000), WriteOutcome::Accepted);
        QCOMPARE(bank.snapshot(QStringLiteral("a")).toDouble(), 12.5);
        QCOMPARE(bank.snapshot(QStringLiteral("a")).timestampMs, qint64(1000));

        const QVector<AuditRecord> audit = pipeline.auditTrail();
        QCOMPARE(audit.size(), 1);
        QCOMPARE(audit.first().sourceProtocol, QStringLiteral("modbus"));
        QCOMPARE(audit.first().clientId, QStringLiteral("127.0.0.1:50000"));
        QCOMPARE(audit.first().newValue.toDouble(), 12.5);
        QVERIFY(audit.first().accepted);
    }

    void unknownSignalIsRejected()
    {
        DataBank bank;
        WritePipeline pipeline(&bank);

        WriteIntent intent;
        intent.signalId = QStringLiteral("nope");
        intent.value = 1;

        QCOMPARE(pipeline.submit(intent, 0), WriteOutcome::RejectedUnknownSignal);
        QCOMPARE(pipeline.auditCount(), 1);
        QVERIFY(!pipeline.auditTrail().first().accepted);
        QCOMPARE(bank.count(), 0);
    }

    void writeProtectionBlocksWrites()
    {
        DataBank bank;
        bank.registerSignal(makeSignal(QStringLiteral("a")));
        WritePipeline pipeline(&bank);
        pipeline.setWriteProtected(true);

        WriteIntent intent;
        intent.signalId = QStringLiteral("a");
        intent.value = 5;

        QCOMPARE(pipeline.submit(intent, 0), WriteOutcome::RejectedWriteProtected);
        QCOMPARE(bank.snapshot(QStringLiteral("a")).toDouble(), 0.0);
    }

    void outOfRangeIsClampedAndMarked()
    {
        DataBank bank;
        bank.registerSignal(makeSignal(QStringLiteral("a"), 0.0, 100.0));
        WritePipeline pipeline(&bank);

        WriteIntent intent;
        intent.signalId = QStringLiteral("a");
        intent.value = 150.0;

        QCOMPARE(pipeline.submit(intent, 0), WriteOutcome::Accepted);
        const SignalSnapshot snapshot = bank.snapshot(QStringLiteral("a"));
        QCOMPARE(snapshot.toDouble(), 100.0);
        QCOMPARE(snapshot.quality, Quality::OutOfRange);
    }

    void forcedWriteBypassesClampAndSetsFlag()
    {
        DataBank bank;
        bank.registerSignal(makeSignal(QStringLiteral("a"), 0.0, 100.0));
        WritePipeline pipeline(&bank);

        WriteIntent intent;
        intent.signalId = QStringLiteral("a");
        intent.value = 999.0;
        intent.forced = true;
        intent.sourceProtocol = QStringLiteral("ui");

        QCOMPARE(pipeline.submit(intent, 0), WriteOutcome::Accepted);
        const SignalSnapshot snapshot = bank.snapshot(QStringLiteral("a"));
        QCOMPARE(snapshot.toDouble(), 999.0);
        QVERIFY(snapshot.forced);
    }

    void eventBusFansOutToOtherProtocols()
    {
        DataBank bank;
        bank.registerSignal(makeSignal(QStringLiteral("a")));

        EventBus bus;
        bus.attachDataBank(&bank);

        FakeAdapter modbus(ProtocolKind::Modbus, QStringLiteral("modbus"));
        FakeAdapter mqtt(ProtocolKind::Mqtt, QStringLiteral("mqtt"));
        bus.registerAdapter(&modbus);
        bus.registerAdapter(&mqtt);

        modbus.pushed.clear();
        mqtt.pushed.clear();

        WritePipeline pipeline(&bank);
        WriteIntent intent;
        intent.signalId = QStringLiteral("a");
        intent.value = 42;
        intent.sourceProtocol = QStringLiteral("modbus");
        pipeline.submit(intent, 100);

        // 来源协议自己不收（它刚写完），其他协议必须收到新值 —— AC-P0-11
        QVERIFY2(modbus.pushed.isEmpty(), "来源协议不该收到自己写的回声");
        QCOMPARE(mqtt.pushed.size(), 1);
        QCOMPARE(mqtt.pushed.first().signalId, QStringLiteral("a"));
        QCOMPARE(mqtt.pushed.first().newValue.toInt(), 42);
        QVERIFY(bus.broadcastCount() >= 1);
    }

    void newAdapterReceivesCurrentSnapshot()
    {
        DataBank bank;
        bank.registerSignal(makeSignal(QStringLiteral("a")));
        bank.write(QStringLiteral("a"), 7, Quality::Good, 10);

        EventBus bus;
        bus.attachDataBank(&bank);

        FakeAdapter late(ProtocolKind::Iec104, QStringLiteral("iec104"));
        bus.registerAdapter(&late);

        QCOMPARE(late.pushed.size(), 1);
        QCOMPARE(late.pushed.first().newValue.toInt(), 7);
    }

    void telemetryLogKeepsRingBufferAndFilters()
    {
        TelemetryLog log(3);
        FrameRecord record;
        record.clientId = QStringLiteral("c1");

        for (int i = 0; i < 5; ++i) {
            record.timestampMs = i;
            record.summary = QStringLiteral("frame %1").arg(i);
            record.signalIds = i % 2 == 0 ? QStringList{QStringLiteral("temperature")}
                                          : QStringList{QStringLiteral("pressure")};
            log.append(record);
        }

        QCOMPARE(log.count(), 3);
        QCOMPARE(log.records().first().summary, QStringLiteral("frame 2"));

        const QVector<FrameRecord> filtered = log.recordsOf(QStringLiteral("temperature"));
        QCOMPARE(filtered.size(), 2); // frame 2 与 frame 4
        QVERIFY(log.toText().contains(QStringLiteral("frame 4")));
        QVERIFY(log.toTextOf(QStringLiteral("pressure")).contains(QStringLiteral("frame 3")));
    }

    void frameRecordFormatsHex()
    {
        FrameRecord record;
        record.bytes = QByteArray::fromHex("000100000006010300000002");
        QCOMPARE(record.hexString(), QStringLiteral("00 01 00 00 00 06 01 03 00 00 00 02"));
    }
};

QTEST_GUILESS_MAIN(TestPipeline)
#include "tst_pipeline.moc"
