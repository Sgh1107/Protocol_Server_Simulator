// SPDX-License-Identifier: GPL-3.0-or-later
//
// `--selftest` 的 9 项检查。思路来自 Python 从站的"启动即自证"：
// 每一项目标单一、失败信息能直接定位，不依赖外部服务、不往工作区写文件。
//
// 之所以单独写一份最小 Modbus 客户端而不复用测试代码：
// 绿色包里的 exe 不能带测试框架，自检必须自包含。

#include "selftest.h"

#include "protosim/adapters/modbusframe.h"
#include "protosim/adapters/modbusadapter.h"
#include "protosim/byteorder.h"
#include "protosim/configbundle.h"
#include "protosim/databank.h"
#include "protosim/eventbus.h"
#include "protosim/iprotocoladapter.h"
#include "protosim/simulationengine.h"
#include "protosim/telemetrylog.h"
#include "protosim/version.h"
#include "protosim/writepipeline.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>

#include <cmath>
#include <cstdio>
#include <functional>

namespace protosim {
namespace {

struct Outcome
{
    bool ok = false;
    QString detail;
};

int g_passed = 0;
int g_failed = 0;

void report(int index, const QString &name, const Outcome &outcome)
{
    const char *tag = outcome.ok ? "PASS" : "FAIL";
    std::printf("[%s] %02d  %s", tag, index, name.toUtf8().constData());
    if (!outcome.detail.isEmpty())
        std::printf("  —— %s", outcome.detail.toUtf8().constData());
    std::printf("\n");
    std::fflush(stdout);

    if (outcome.ok)
        ++g_passed;
    else
        ++g_failed;
}

/// 允许直接传 lambda：report(i, name, [&] { ... return Outcome; })
template <typename Check>
void report(int index, const QString &name, Check check)
{
    report(index, name, check());
}

Outcome pass(const QString &detail = QString())
{
    return Outcome{true, detail};
}

Outcome fail(const QString &detail)
{
    return Outcome{false, detail};
}

QString presetPath()
{
    const QStringList candidates = {
        QCoreApplication::applicationDirPath()
            + QStringLiteral("/config/examples/fieldlink_preset.json"),
        QStringLiteral(PROTOSIM_SOURCE_DIR "/config/examples/fieldlink_preset.json"),
    };
    for (const QString &candidate : candidates) {
        if (QFile::exists(candidate))
            return candidate;
    }
    return QString();
}

/// 在固定区间里找一对空闲端口：对外端口 + 它的内部回环口（port+1000）。
///
/// 为什么不用系统临时分配：Windows 的临时端口在 49152~65535，
/// 经常落在 64536 以上，`port+1000` 会溢出成非法端口，内部口必然绑不上。
/// 固定区间既能避开这个坑，也方便出问题时按端口号定位。
bool portPairFree(quint16 port)
{
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, port))
        return false;
    probe.close();

    QTcpServer internal;
    if (!internal.listen(QHostAddress::LocalHost, static_cast<quint16>(port + 1000)))
        return false;
    internal.close();
    return true;
}

quint16 freePort()
{
    for (quint16 port = 21000; port < 21200; ++port) {
        if (portPairFree(port))
            return port;
    }
    return 0;
}

bool pump(const std::function<bool()> &predicate, int timeoutMs)
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

/// 最小 Modbus 主站：只够自检用（FC03 读 + FC16 写）。
class MiniMaster
{
public:
    bool connectTo(quint16 port)
    {
        m_socket.connectToHost(QHostAddress::LocalHost, port);
        return pump([this] { return m_socket.state() == QAbstractSocket::ConnectedState; }, 2000);
    }

    QByteArray readHolding(quint16 start, quint16 count, QString *error)
    {
        QByteArray data;
        append(data, start);
        append(data, count);
        return transact(modbus::kReadHoldingRegisters, data, error);
    }

    QByteArray writeRegisters(quint16 start, const QVector<quint16> &values, QString *error)
    {
        QByteArray data;
        append(data, start);
        append(data, static_cast<quint16>(values.size()));
        data.append(static_cast<char>(values.size() * 2));
        for (quint16 value : values)
            append(data, value);
        return transact(modbus::kWriteMultipleRegisters, data, error);
    }

    static QVector<quint16> registersOf(const QByteArray &response)
    {
        QVector<quint16> values;
        const modbus::Adu adu = modbus::parseAdu(response);
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
    QByteArray transact(quint8 functionCode, const QByteArray &data, QString *error)
    {
        QByteArray pdu;
        pdu.append(static_cast<char>(functionCode));
        pdu.append(data);

        const quint16 txId = ++m_transactionId;
        QByteArray adu;
        adu.append(static_cast<char>((txId >> 8) & 0xFF));
        adu.append(static_cast<char>(txId & 0xFF));
        adu.append(char(0));
        adu.append(char(0));
        const quint16 length = static_cast<quint16>(pdu.size() + 1);
        adu.append(static_cast<char>((length >> 8) & 0xFF));
        adu.append(static_cast<char>(length & 0xFF));
        adu.append(char(1));
        adu.append(pdu);

        m_buffer.clear();
        m_socket.write(adu);
        m_socket.flush();

        const bool complete = pump(
            [this] {
                m_buffer += m_socket.readAll();
                return frameComplete();
            },
            2000);

        if (!complete) {
            if (error)
                *error = QStringLiteral("响应超时");
            return {};
        }

        const int total = 6 + ((static_cast<quint8>(m_buffer.at(4)) << 8)
                               | static_cast<quint8>(m_buffer.at(5)));
        const QByteArray frame = m_buffer.left(total);
        m_buffer.remove(0, total);
        return frame;
    }

    bool frameComplete() const
    {
        if (m_buffer.size() < 6)
            return false;
        const int length = (static_cast<quint8>(m_buffer.at(4)) << 8)
            | static_cast<quint8>(m_buffer.at(5));
        return m_buffer.size() >= 6 + length;
    }

    static void append(QByteArray &out, quint16 value)
    {
        out.append(static_cast<char>((value >> 8) & 0xFF));
        out.append(static_cast<char>(value & 0xFF));
    }

    QTcpSocket m_socket;
    QByteArray m_buffer;
    quint16 m_transactionId = 0;
};

/// 只为验证联动而存在的假协议。
class LinkageSpy : public IProtocolAdapter
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

} // namespace

int runSelfTest()
{
    std::printf("ProtoSim 自检 —— %s\n", buildInfoString().toUtf8().constData());
    std::printf("--------------------------------------------------------------\n");

    // 01 构建信息
    report(1, QStringLiteral("构建与版本信息"), [] {
        const QString info = buildInfoString();
        if (!info.contains(QStringLiteral("ProtoSim")))
            return fail(QStringLiteral("构建信息缺少产品名"));
        if (!info.contains(QStringLiteral("Qt 6.6")))
            return fail(QStringLiteral("Qt 版本不是 6.6.x 基线：%1").arg(info));
        return pass(info);
    });

    // 02 配置包加载
    const QString configPath = presetPath();
    ConfigBundle bundle;
    report(2, QStringLiteral("配置包加载与校验"), [&] {
        if (configPath.isEmpty())
            return fail(QStringLiteral("找不到 config/examples/fieldlink_preset.json"));
        const auto loaded = ConfigBundle::loadFromFile(configPath);
        if (!loaded.ok())
            return fail(loaded.error().toString());
        bundle = loaded.value();
        if (bundle.signals.size() != 3 || bundle.endpoints.size() != 1)
            return fail(QStringLiteral("点表数量不符：%1 信号 / %2 端点")
                            .arg(bundle.signals.size())
                            .arg(bundle.endpoints.size()));
        return pass(QStringLiteral("%1 个信号 / %2 个端点")
                        .arg(bundle.signals.size())
                        .arg(bundle.endpoints.size()));
    });

    // 03 坏配置必须被拒
    report(3, QStringLiteral("坏配置拒绝（缺 schemaVersion）"), [] {
        const QByteArray broken = R"({"meta":{"name":"x","seed":1,"tickMs":100},
            "devices":[{"id":"d","name":"d"}],"signals":[],"endpoints":[]})";
        const auto result = ConfigBundle::fromJson(broken, QDir::currentPath());
        if (result.ok())
            return fail(QStringLiteral("缺 schemaVersion 竟然通过了"));
        if (result.error().code != ErrorCode::Schema)
            return fail(QStringLiteral("错误码不对：%1")
                            .arg(static_cast<int>(result.error().code)));
        return pass(result.error().message);
    });

    // 04 点表灌入实时库
    DataBank bank;
    report(4, QStringLiteral("点表装载进 DataBank"), [&] {
        if (configPath.isEmpty())
            return fail(QStringLiteral("没有可用配置"));
        const auto result = bundle.populate(&bank);
        if (!result.ok())
            return fail(result.error().toString());
        if (bank.count() != 3)
            return fail(QStringLiteral("装载了 %1 个信号，期望 3").arg(bank.count()));
        return pass(QStringLiteral("%1 个信号").arg(bank.count()));
    });

    // 05 四种字节序往返
    report(5, QStringLiteral("字节序 ABCD/BADC/CDAB/DCBA 往返"), [] {
        for (auto order : {ByteOrder::ABCD, ByteOrder::BADC, ByteOrder::CDAB, ByteOrder::DCBA}) {
            const QVector<quint16> registers = byteorder::encodeBits(0x12345678u, 4, order);
            if (byteorder::decodeBits(registers, 4, order) != 0x12345678u)
                return fail(QStringLiteral("字节序 %1 往返失败")
                                .arg(byteOrderToString(order)));
        }
        return pass(QStringLiteral("0x12345678 四种序均还原"));
    });

    // 06 工程值换算与钳位
    report(6, QStringLiteral("工程值换算与越界钳位"), [] {
        Signal signal;
        signal.id = QStringLiteral("t");
        signal.name = QStringLiteral("t");
        signal.scale = 0.1;
        signal.offset = -40.0;
        signal.range.min = 0.0;
        signal.range.max = 100.0;
        signal.range.valid = true;

        if (!qFuzzyCompare(signal.rawToEngineering(500), 10.0))
            return fail(QStringLiteral("scale/offset 换算不对"));

        bool outOfRange = false;
        const double clamped = signal.clampToRange(150.0, &outOfRange);
        if (!qFuzzyCompare(clamped, 100.0) || !outOfRange)
            return fail(QStringLiteral("越界钳位或 OutOfRange 标记失效"));

        return pass(QStringLiteral("原始 500 -> 10.0；150 -> 100.0 + OutOfRange"));
    });

    // 07 仿真确定性
    report(7, QStringLiteral("仿真确定性（同种子逐帧一致）"), [&] {
        if (configPath.isEmpty())
            return fail(QStringLiteral("没有可用配置"));

        auto runSeries = [](int ticks) -> QVector<double> {
            DataBank localBank;
            SimulationEngine engine(&localBank);
            const auto loaded = ConfigBundle::loadFromFile(presetPath());
            if (!loaded.ok())
                return {};
            ConfigBundle copy = loaded.value();
            if (!copy.populate(&localBank).ok())
                return {};
            if (!engine.load(copy).ok())
                return {};

            QVector<double> series;
            for (int i = 0; i < ticks; ++i) {
                engine.stepOnce();
                series.append(localBank.snapshot(QStringLiteral("pressure")).toDouble());
            }
            return series;
        };

        const QVector<double> first = runSeries(200);
        const QVector<double> second = runSeries(200);
        if (first.isEmpty() || first != second)
            return fail(QStringLiteral("两次运行序列不一致"));
        return pass(QStringLiteral("%1 tick 逐帧一致").arg(first.size()));
    });

    // 08 / 09 需要真实端点，装配一套完整的运行时
    WritePipeline pipeline(&bank);
    TelemetryLog log(1000);
    EventBus bus;
    bus.attachDataBank(&bank);
    SimulationEngine engine(&bank);
    ModbusTcpAdapter adapter(&bank, &pipeline, &log);
    adapter.setClockSource([&engine] { return engine.virtualTimeMs(); });
    bus.registerAdapter(&adapter);

    const quint16 port = freePort();
    bool endpointUp = false;
    QString endpointError;

    report(8, QStringLiteral("Modbus 端点启停与 FC16->FC03 回读"), [&] {
        if (configPath.isEmpty())
            return fail(QStringLiteral("没有可用配置"));
        if (!engine.load(bundle).ok())
            return fail(QStringLiteral("引擎装载失败"));
        if (!adapter.bind(ModbusTcpAdapter::bindingsFromBundle(bundle)).ok())
            return fail(QStringLiteral("绑定失败"));

        EndpointConfig endpoint;
        endpoint.kind = ProtocolKind::Modbus;
        endpoint.raw = bundle.endpoints.first().raw;
        endpoint.raw.insert(QStringLiteral("port"), static_cast<int>(port));

        const Result<void> started = adapter.start(endpoint);
        if (!started.ok())
            return fail(started.error().toString());
        endpointUp = true;

        MiniMaster master;
        if (!master.connectTo(port)) {
            endpointError = QStringLiteral("主站连不上 %1").arg(port);
            return fail(endpointError);
        }

        // 写 float32 23.5 到 HR[0..1]，再读回来
        const Signal definition = bank.definition(QStringLiteral("temperature"));
        const QVector<quint16> target =
            byteorder::encodeNumber(23.5, definition.dataType, definition.byteOrder);

        QString error;
        const QByteArray writeResponse = master.writeRegisters(0, target, &error);
        if (writeResponse.isEmpty())
            return fail(error);
        if (modbus::parseAdu(writeResponse).isException())
            return fail(QStringLiteral("FC16 返回异常"));

        const QByteArray readResponse = master.readHolding(0, 2, &error);
        if (readResponse.isEmpty())
            return fail(error);
        if (MiniMaster::registersOf(readResponse) != target)
            return fail(QStringLiteral("回读寄存器与写入不一致"));

        const double engineering = bank.snapshot(QStringLiteral("temperature")).toDouble();
        if (std::abs(engineering - 23.5) > 1e-6)
            return fail(QStringLiteral("工程值不是 23.5，而是 %1").arg(engineering));

        return pass(QStringLiteral("HR[0..1] = 23.5，FC03 回读一致"));
    });

    report(9, QStringLiteral("写回管线联动（其他协议收到新值）"), [&] {
        if (!endpointUp)
            return fail(QStringLiteral("端点未启动，跳过"));

        LinkageSpy spy;
        bus.registerAdapter(&spy);
        spy.pushed.clear();

        WriteIntent intent;
        intent.signalId = QStringLiteral("magic");
        intent.value = 321;
        intent.sourceProtocol = QStringLiteral("selftest");
        intent.clientId = QStringLiteral("selftest");

        const WriteOutcome outcome = pipeline.submit(intent, engine.virtualTimeMs());
        if (outcome != WriteOutcome::Accepted)
            return fail(writeOutcomeToString(outcome));

        // 来源是 "selftest"（非任何协议），所以两个适配器都应收到
        bool seen = false;
        for (const SignalChange &change : spy.pushed) {
            if (change.signalId == QStringLiteral("magic") && change.newValue.toInt() == 321)
                seen = true;
        }
        if (!seen)
            return fail(QStringLiteral("联动推送里没有新值"));

        const QVector<AuditRecord> audit = pipeline.auditTrail();
        if (audit.isEmpty() || !audit.last().accepted)
            return fail(QStringLiteral("审计记录缺失"));

        return pass(QStringLiteral("FakeAdapter 收到 pushTelemetry，审计 %1 条")
                        .arg(pipeline.auditCount()));
    });

    adapter.stop();

    std::printf("--------------------------------------------------------------\n");
    std::printf("自检结果：%d 通过 / %d 失败\n", g_passed, g_failed);

    return g_failed == 0 ? 0 : 1;
}

} // namespace protosim
