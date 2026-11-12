// SPDX-License-Identifier: GPL-3.0-or-later
//
// L2 假主站 E2E：真实 socket、进程内，覆盖 8 个功能码与异常路径。
//
// 注意：服务端和主站跑在同一个线程 / 同一个事件循环里，
// 所以这里绝不能用 socket.waitForReadyRead()（会把服务端的事件循环堵死），
// 统一用"processEvents 轮询 + 非阻塞读"的 pump 方式。

#include "protosim/adapters/modbusframe.h"
#include "protosim/adapters/modbusadapter.h"
#include "protosim/byteorder.h"
#include "protosim/configbundle.h"
#include "protosim/databank.h"
#include "protosim/eventbus.h"
#include "protosim/iprotocoladapter.h"
#include "protosim/simulationengine.h"
#include "protosim/telemetrylog.h"
#include "protosim/writepipeline.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QTcpSocket>
#include <QtTest>

using namespace protosim;
using namespace protosim::modbus;

namespace {

/// 覆盖四个寄存器区的最小场景，让 8 个功能码都有落点。
const char *kFixtureJson = R"JSON({
  "schemaVersion": "1.0",
  "meta": { "name": "e2e-fixture", "seed": 20261111, "tickMs": 100, "timeSource": "simulation" },
  "devices": [ { "id": "dev-01", "name": "虚拟设备" } ],
  "signals": [
    { "id": "temperature", "name": "温度", "deviceId": "dev-01", "category": "AI",
      "dataType": "float32", "byteOrder": "ABCD", "scale": 1.0, "offset": 0.0, "unit": "C",
      "range": { "min": -40.0, "max": 120.0 },
      "behavior": { "type": "sine", "amplitude": 10.0, "periodMs": 10000, "min": 15.0, "max": 35.0 },
      "bindings": [ { "protocol": "modbus", "unitId": 1, "registerType": "HoldingRegisters", "address": 0, "count": 2 } ] },
    { "id": "pressure", "name": "压力", "deviceId": "dev-01", "category": "AI",
      "dataType": "uint16", "byteOrder": "ABCD", "scale": 0.1, "offset": 0.0, "unit": "kPa",
      "range": { "min": 0.0, "max": 1000.0 },
      "behavior": { "type": "randomWalk", "min": 300.0, "max": 700.0, "step": 5.0, "intervalMs": 500 },
      "bindings": [ { "protocol": "modbus", "unitId": 1, "registerType": "HoldingRegisters", "address": 2, "count": 1 } ] },
    { "id": "magic", "name": "常量", "deviceId": "dev-01", "category": "AI",
      "dataType": "uint16", "byteOrder": "ABCD", "scale": 1.0, "offset": 0.0,
      "range": { "min": 0.0, "max": 65535.0 },
      "behavior": { "type": "constant", "value": 42 },
      "bindings": [ { "protocol": "modbus", "unitId": 1, "registerType": "HoldingRegisters", "address": 3, "count": 1 } ] },
    { "id": "coil-a", "name": "线圈A", "deviceId": "dev-01", "category": "DO",
      "dataType": "bool", "scale": 1.0, "offset": 0.0,
      "behavior": { "type": "constant", "value": 0 },
      "bindings": [ { "protocol": "modbus", "unitId": 1, "registerType": "Coils", "address": 0, "count": 1 } ] },
    { "id": "coil-b", "name": "线圈B", "deviceId": "dev-01", "category": "DO",
      "dataType": "bool", "scale": 1.0, "offset": 0.0,
      "behavior": { "type": "constant", "value": 1 },
      "bindings": [ { "protocol": "modbus", "unitId": 1, "registerType": "Coils", "address": 1, "count": 1 } ] },
    { "id": "di-a", "name": "遥信A", "deviceId": "dev-01", "category": "DI",
      "dataType": "bool", "scale": 1.0, "offset": 0.0,
      "behavior": { "type": "constant", "value": 1 },
      "bindings": [ { "protocol": "modbus", "unitId": 1, "registerType": "DiscreteInputs", "address": 0, "count": 1 } ] },
    { "id": "ir-a", "name": "输入寄存器A", "deviceId": "dev-01", "category": "AI",
      "dataType": "uint16", "scale": 1.0, "offset": 0.0,
      "range": { "min": 0.0, "max": 65535.0 },
      "behavior": { "type": "constant", "value": 100 },
      "bindings": [ { "protocol": "modbus", "unitId": 1, "registerType": "InputRegisters", "address": 0, "count": 1 } ] }
  ],
  "endpoints": [
    { "protocol": "modbus", "transport": "tcp", "bindAddress": "127.0.0.1",
      "port": 15020, "unitIds": [1], "maxConnections": 8 }
  ]
})JSON";

/// 最小假主站：非阻塞收发，靠 processEvents 驱动服务端。
class ModbusMaster
{
public:
    explicit ModbusMaster(quint16 port)
        : m_port(port)
    {
    }

    bool connectToServer(int timeoutMs = 3000)
    {
        m_socket.connectToHost(QHostAddress::LocalHost, m_port);
        return pump([this] { return m_socket.state() == QAbstractSocket::ConnectedState; },
                    timeoutMs);
    }

    void disconnect()
    {
        m_socket.abort();
    }

    bool isConnected() const { return m_socket.state() == QAbstractSocket::ConnectedState; }

    /// 发一帧收一帧。返回完整响应 ADU；超时返回空。
    QByteArray transact(const QByteArray &request, int timeoutMs = 3000)
    {
        m_buffer.clear();
        m_socket.write(request);
        m_socket.flush();

        const bool complete = pump(
            [this] {
                m_buffer += m_socket.readAll();
                return frameComplete();
            },
            timeoutMs);

        if (!complete)
            return {};

        return takeFrame();
    }

    // ---- 报文构造 ----

    QByteArray request(quint8 functionCode, const QByteArray &data = QByteArray(), quint8 unit = 1)
    {
        const quint16 txId = ++m_transactionId;
        QByteArray pdu;
        pdu.append(static_cast<char>(functionCode));
        pdu.append(data);

        QByteArray adu;
        adu.append(static_cast<char>((txId >> 8) & 0xFF));
        adu.append(static_cast<char>(txId & 0xFF));
        adu.append(char(0));
        adu.append(char(0));
        const quint16 length = static_cast<quint16>(pdu.size() + 1);
        adu.append(static_cast<char>((length >> 8) & 0xFF));
        adu.append(static_cast<char>(length & 0xFF));
        adu.append(static_cast<char>(unit));
        adu.append(pdu);
        return adu;
    }

    QByteArray read(quint8 functionCode, quint16 start, quint16 count)
    {
        QByteArray data;
        appendUInt16(data, start);
        appendUInt16(data, count);
        return transact(request(functionCode, data));
    }

    QByteArray writeSingleRegister(quint16 address, quint16 value)
    {
        QByteArray data;
        appendUInt16(data, address);
        appendUInt16(data, value);
        return transact(request(kWriteSingleRegister, data));
    }

    QByteArray writeMultipleRegisters(quint16 address, const QVector<quint16> &values)
    {
        QByteArray data;
        appendUInt16(data, address);
        appendUInt16(data, static_cast<quint16>(values.size()));
        data.append(static_cast<char>(values.size() * 2));
        for (quint16 value : values)
            appendUInt16(data, value);
        return transact(request(kWriteMultipleRegisters, data));
    }

    QByteArray writeSingleCoil(quint16 address, bool on)
    {
        QByteArray data;
        appendUInt16(data, address);
        appendUInt16(data, on ? 0xFF00 : 0x0000);
        return transact(request(kWriteSingleCoil, data));
    }

    QByteArray writeMultipleCoils(quint16 address, const QVector<bool> &values)
    {
        QByteArray data;
        appendUInt16(data, address);
        appendUInt16(data, static_cast<quint16>(values.size()));
        const int byteCount = (values.size() + 7) / 8;
        data.append(static_cast<char>(byteCount));
        QByteArray packed(byteCount, char(0));
        for (int i = 0; i < values.size(); ++i) {
            if (values.at(i))
                packed[i / 8] = static_cast<char>(packed.at(i / 8) | (1 << (i % 8)));
        }
        data.append(packed);
        return transact(request(kWriteMultipleCoils, data));
    }

    static bool isException(const QByteArray &response)
    {
        const Adu adu = parseAdu(response);
        return adu.valid && adu.isException();
    }

    static quint8 exceptionCode(const QByteArray &response)
    {
        const Adu adu = parseAdu(response);
        if (!adu.valid || adu.pdu.size() < 2)
            return 0;
        return static_cast<quint8>(adu.pdu.at(1));
    }

    static QVector<quint16> registersOf(const QByteArray &response)
    {
        QVector<quint16> values;
        const Adu adu = parseAdu(response);
        if (!adu.valid || adu.pdu.size() < 2)
            return values;
        const int byteCount = static_cast<quint8>(adu.pdu.at(1));
        for (int i = 0; i < byteCount; i += 2) {
            values.append(static_cast<quint16>((static_cast<quint8>(adu.pdu.at(2 + i)) << 8)
                                              | static_cast<quint8>(adu.pdu.at(3 + i))));
        }
        return values;
    }

private:
    bool frameComplete() const
    {
        if (m_buffer.size() < 6)
            return false;
        const int length = (static_cast<quint8>(m_buffer.at(4)) << 8)
            | static_cast<quint8>(m_buffer.at(5));
        return m_buffer.size() >= 6 + length;
    }

    QByteArray takeFrame()
    {
        const int length = (static_cast<quint8>(m_buffer.at(4)) << 8)
            | static_cast<quint8>(m_buffer.at(5));
        const int total = 6 + length;
        const QByteArray frame = m_buffer.left(total);
        m_buffer.remove(0, total);
        return frame;
    }

    template <typename Predicate>
    bool pump(Predicate predicate, int timeoutMs)
    {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            if (predicate())
                return true;
        }
        return predicate();
    }

    static void appendUInt16(QByteArray &out, quint16 value)
    {
        out.append(static_cast<char>((value >> 8) & 0xFF));
        out.append(static_cast<char>(value & 0xFF));
    }

    quint16 m_port;
    QTcpSocket m_socket;
    QByteArray m_buffer;
    quint16 m_transactionId = 0;
};

/// 一个只为了"被通知"而存在的假协议，验证跨协议联动。
class SpyAdapter : public IProtocolAdapter
{
public:
    Capabilities capabilities() const override
    {
        Capabilities caps;
        caps.kind = ProtocolKind::Mqtt;
        caps.supportsPush = true;
        return caps;
    }
    Result<void> start(const EndpointConfig &) override { return {}; }
    Result<void> stop() override { return {}; }
    Result<void> bind(const QVector<Binding> &) override { return {}; }
    AdapterState state() const override { return AdapterState::Running; }
    ReadResult onRead(const ReadRequest &) override { return {}; }
    WriteResult onWrite(const WriteRequest &) override { return {}; }
    void pushTelemetry(const QVector<SignalChange> &changes) override { pushed.append(changes); }
    Result<void> injectFault(const FaultSpec &) override { return {}; }
    AdapterStats stats() const override { return {}; }

    QVector<SignalChange> pushed;
};

/// 一套完整装配：库 + 管线 + 日志 + 总线 + 引擎 + Modbus 适配器。
struct Fixture
{
    DataBank bank;
    WritePipeline pipeline{&bank};
    TelemetryLog log{4000};
    EventBus bus;
    SimulationEngine engine{&bank};
    ModbusTcpAdapter adapter{&bank, &pipeline, &log};
    ConfigBundle bundle;
    bool ready = false;
    QString error;

    explicit Fixture(quint16 port)
    {
        auto parsed = ConfigBundle::fromJson(QByteArray(kFixtureJson), QStringLiteral("."));
        if (!parsed.ok()) {
            error = parsed.error().toString();
            return;
        }
        bundle = parsed.value();

        if (!bundle.populate(&bank).ok()) {
            error = QStringLiteral("点表装载失败");
            return;
        }
        if (!engine.load(bundle).ok()) {
            error = QStringLiteral("引擎装载失败");
            return;
        }

        engine.setSpeed(1.0);
        // 仿真时钟作为统一时标源：写给 DataBank 的时间戳与引擎同源
        adapter.setClockSource([this] { return engine.virtualTimeMs(); });

        bus.attachDataBank(&bank);
        bus.registerAdapter(&adapter);

        if (!adapter.bind(ModbusTcpAdapter::bindingsFromBundle(bundle)).ok()) {
            error = QStringLiteral("绑定失败");
            return;
        }

        EndpointConfig endpoint;
        endpoint.kind = ProtocolKind::Modbus;
        endpoint.raw = bundle.endpoints.first().raw;
        endpoint.raw.insert(QStringLiteral("port"), static_cast<int>(port));

        const Result<void> started = adapter.start(endpoint);
        if (!started.ok()) {
            error = started.error().toString();
            return;
        }
        ready = true;
    }

    ~Fixture() { adapter.stop(); }

    QVector<quint16> floatRegisters(double value) const
    {
        const Signal definition = bank.definition(QStringLiteral("temperature"));
        return byteorder::encodeNumber(value, definition.dataType, definition.byteOrder);
    }
};

} // namespace

class TestModbusE2E : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void readHoldingRegistersReflectsBank()
    {
        Fixture fixture(15030);
        QVERIFY2(fixture.ready, qPrintable(fixture.error));
        fixture.adapter.setClockSource([] { return qint64(0); });

        // 直接置库，读回来必须与库一致（适配器不做自己的缓存）
        fixture.bank.write(QStringLiteral("temperature"), 23.5, Quality::Good, 0);
        fixture.bank.write(QStringLiteral("magic"), 42, Quality::Good, 0);

        ModbusMaster master(15030);
        QVERIFY(master.connectToServer());

        const QByteArray response = master.read(kReadHoldingRegisters, 0, 4);
        QVERIFY2(!response.isEmpty(), "FC03 无响应");
        QVERIFY(!ModbusMaster::isException(response));

        const QVector<quint16> registers = ModbusMaster::registersOf(response);
        QCOMPARE(registers.size(), 4);
        QCOMPARE(registers.mid(0, 2), fixture.floatRegisters(23.5));
        QCOMPARE(registers.at(3), quint16(42));
    }

    void writeSingleRegisterRoundTrip()
    {
        Fixture fixture(15031);
        QVERIFY2(fixture.ready, qPrintable(fixture.error));

        ModbusMaster master(15031);
        QVERIFY(master.connectToServer());

        const QByteArray writeResponse = master.writeSingleRegister(3, 77);
        QVERIFY(!ModbusMaster::isException(writeResponse));

        // 立刻回读，必须一致（AC-P0-10 的最小形态）
        const QByteArray readResponse = master.read(kReadHoldingRegisters, 3, 1);
        QCOMPARE(ModbusMaster::registersOf(readResponse), QVector<quint16>{77});
        QCOMPARE(fixture.bank.snapshot(QStringLiteral("magic")).toDouble(), 77.0);

        // 审计要记到来源协议与客户端
        const QVector<AuditRecord> audit = fixture.pipeline.auditTrail();
        QVERIFY(!audit.isEmpty());
        const AuditRecord last = audit.last();
        QCOMPARE(last.signalId, QStringLiteral("magic"));
        QCOMPARE(last.sourceProtocol, QStringLiteral("modbus"));
        QVERIFY2(!last.clientId.isEmpty() && last.clientId != QStringLiteral("-"),
                 qPrintable(last.clientId));
        QVERIFY(last.accepted);
    }

    void writeFloatViaFc16AndReadBack()
    {
        Fixture fixture(15032);
        QVERIFY2(fixture.ready, qPrintable(fixture.error));

        ModbusMaster master(15032);
        QVERIFY(master.connectToServer());

        const QVector<quint16> target = fixture.floatRegisters(23.5);
        QCOMPARE(target.size(), 2);

        const QByteArray writeResponse = master.writeMultipleRegisters(0, target);
        QVERIFY(!ModbusMaster::isException(writeResponse));

        const QByteArray readResponse = master.read(kReadHoldingRegisters, 0, 2);
        QCOMPARE(ModbusMaster::registersOf(readResponse), target);

        // 工程值也必须是 23.5（float32 解码 + scale/offset）
        QCOMPARE(fixture.bank.snapshot(QStringLiteral("temperature")).toDouble(), 23.5);
    }

    void allEightFunctionCodesRespond()
    {
        Fixture fixture(15033);
        QVERIFY2(fixture.ready, qPrintable(fixture.error));

        ModbusMaster master(15033);
        QVERIFY(master.connectToServer());

        struct Case
        {
            const char *name;
            QByteArray response;
        };

        const QVector<Case> cases = {
            {"FC01 读线圈", master.read(kReadCoils, 0, 2)},
            {"FC02 读离散输入", master.read(kReadDiscreteInputs, 0, 1)},
            {"FC03 读保持寄存器", master.read(kReadHoldingRegisters, 0, 4)},
            {"FC04 读输入寄存器", master.read(kReadInputRegisters, 0, 1)},
            {"FC05 写单线圈", master.writeSingleCoil(0, true)},
            {"FC06 写单寄存器", master.writeSingleRegister(3, 7)},
            {"FC0F 写多线圈", master.writeMultipleCoils(0, {true, false})},
            {"FC10 写多寄存器", master.writeMultipleRegisters(0, {0x1234, 0x5678})},
        };

        for (const Case &testCase : cases) {
            QVERIFY2(!testCase.response.isEmpty(),
                     qPrintable(QStringLiteral("%1 无响应").arg(QLatin1String(testCase.name))));
            const Adu adu = parseAdu(testCase.response);
            QVERIFY2(adu.valid, qPrintable(QStringLiteral("%1 响应非法").arg(
                                     QLatin1String(testCase.name))));
            QVERIFY2(!adu.isException(),
                     qPrintable(QStringLiteral("%1 返回异常 0x%2")
                                    .arg(QLatin1String(testCase.name))
                                    .arg(QString::number(ModbusMaster::exceptionCode(testCase.response),
                                                         16)
                                             .toUpper()
                                             .rightJustified(2, QLatin1Char('0')))));
        }
    }

    void illegalFunctionReturns0x01()
    {
        Fixture fixture(15034);
        QVERIFY2(fixture.ready, qPrintable(fixture.error));

        ModbusMaster master(15034);
        QVERIFY(master.connectToServer());

        const QByteArray response = master.transact(master.request(0x2B, QByteArray("\x0E\x01\x00", 3)));
        QVERIFY(!response.isEmpty());
        QVERIFY(ModbusMaster::isException(response));
        QCOMPARE(ModbusMaster::exceptionCode(response), quint8(kIllegalFunction));
    }

    void unmappedAddressReturns0x02()
    {
        Fixture fixture(15035);
        QVERIFY2(fixture.ready, qPrintable(fixture.error));

        ModbusMaster master(15035);
        QVERIFY(master.connectToServer());

        const QByteArray readResponse = master.read(kReadHoldingRegisters, 40000, 1);
        QVERIFY(ModbusMaster::isException(readResponse));
        QCOMPARE(ModbusMaster::exceptionCode(readResponse), quint8(kIllegalDataAddress));

        const QByteArray writeResponse = master.writeSingleRegister(40000, 1);
        QVERIFY(ModbusMaster::isException(writeResponse));
        QCOMPARE(ModbusMaster::exceptionCode(writeResponse), quint8(kIllegalDataAddress));
    }

    void illegalQuantityReturns0x03()
    {
        Fixture fixture(15036);
        QVERIFY2(fixture.ready, qPrintable(fixture.error));

        ModbusMaster master(15036);
        QVERIFY(master.connectToServer());

        // 读 126 个寄存器超过规范上限 125
        const QByteArray response = master.read(kReadHoldingRegisters, 0, 126);
        QVERIFY(ModbusMaster::isException(response));
        QCOMPARE(ModbusMaster::exceptionCode(response), quint8(kIllegalDataValue));

        // 写单线圈值既不是 0x0000 也不是 0xFF00
        QByteArray data;
        data.append(char(0));
        data.append(char(0));
        data.append(char(0x12));
        data.append(char(0x34));
        const QByteArray coilResponse = master.transact(master.request(kWriteSingleCoil, data));
        QVERIFY(ModbusMaster::isException(coilResponse));
        QCOMPARE(ModbusMaster::exceptionCode(coilResponse), quint8(kIllegalDataValue));
    }

    void connectionLimitRejectsNinthClient()
    {
        Fixture fixture(15037);
        QVERIFY2(fixture.ready, qPrintable(fixture.error));

        QVector<ModbusMaster *> masters;
        for (int i = 0; i < 8; ++i) {
            auto *master = new ModbusMaster(15037);
            QVERIFY2(master->connectToServer(), qPrintable(QStringLiteral("第 %1 个连不上").arg(i + 1)));
            masters.append(master);
        }
        QCOMPARE(fixture.adapter.activeClients(), 8);

        // 第 9 个必须被拒
        ModbusMaster ninth(15037);
        ninth.connectToServer(800);
        QTRY_VERIFY_WITH_TIMEOUT(fixture.log.toText().contains(QStringLiteral("连接被拒")), 2000);
        QCOMPARE(fixture.adapter.activeClients(), 8);

        // 已有 8 条连接仍然正常工作
        QVERIFY(!ModbusMaster::isException(masters.first()->read(kReadHoldingRegisters, 3, 1)));

        for (ModbusMaster *master : masters) {
            master->disconnect();
            delete master;
        }
    }

    void frameLogCarriesAllRequiredFields()
    {
        Fixture fixture(15038);
        QVERIFY2(fixture.ready, qPrintable(fixture.error));

        ModbusMaster master(15038);
        QVERIFY(master.connectToServer());

        QVERIFY(!master.read(kReadHoldingRegisters, 0, 2).isEmpty());

        const QVector<FrameRecord> records = fixture.log.records();
        QVERIFY(records.size() >= 2);

        FrameRecord inbound;
        FrameRecord outbound;
        for (const FrameRecord &record : records) {
            if (record.direction == FrameDirection::Inbound && record.functionCode == kReadHoldingRegisters)
                inbound = record;
            if (record.direction == FrameDirection::Outbound && record.functionCode == kReadHoldingRegisters)
                outbound = record;
        }

        QVERIFY2(inbound.timestampMs > 0, "入向缺少时间戳");
        QVERIFY2(!inbound.clientId.isEmpty(), "入向缺少客户端");
        QVERIFY2(!inbound.bytes.isEmpty(), "入向缺少原始报文");
        QVERIFY2(inbound.hexString().startsWith(QStringLiteral("00 01")), qPrintable(inbound.hexString()));
        QVERIFY2(inbound.summary.contains(QStringLiteral("FC03")), qPrintable(inbound.summary));
        QVERIFY2(inbound.signalIds.contains(QStringLiteral("temperature")),
                 qPrintable(inbound.signalIds.join(QLatin1Char(','))));

        QVERIFY2(outbound.elapsedUs >= 0, "响应缺少耗时");
        QVERIFY2(!outbound.bytes.isEmpty(), "响应缺少原始报文");
        QVERIFY2(outbound.hexString().size() > 10, qPrintable(outbound.hexString()));

        // 按信号过滤与文本导出
        QVERIFY(!fixture.log.recordsOf(QStringLiteral("temperature")).isEmpty());
        const QString text = fixture.log.toText();
        QVERIFY2(text.contains(QStringLiteral("FC03")), qPrintable(text));
    }

    void modbusWriteNotifiesOtherProtocol()
    {
        Fixture fixture(15039);
        QVERIFY2(fixture.ready, qPrintable(fixture.error));

        SpyAdapter spy;
        fixture.bus.registerAdapter(&spy);
        spy.pushed.clear();

        ModbusMaster master(15039);
        QVERIFY(master.connectToServer());

        QVERIFY(!ModbusMaster::isException(master.writeSingleRegister(3, 99)));

        // AC-P0-11：从 Modbus 写入后，另一个协议收到 pushTelemetry 且能读到新值
        QVERIFY2(!spy.pushed.isEmpty(), "另一个协议没有收到联动推送");
        bool found = false;
        for (const SignalChange &change : spy.pushed) {
            if (change.signalId == QStringLiteral("magic") && change.newValue.toInt() == 99)
                found = true;
        }
        QVERIFY2(found, "联动推送里没有新值");
        QCOMPARE(fixture.bank.snapshot(QStringLiteral("magic")).toDouble(), 99.0);
    }
};

QTEST_GUILESS_MAIN(TestModbusE2E)
#include "tst_modbus_e2e.moc"
