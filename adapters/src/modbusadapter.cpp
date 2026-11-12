// SPDX-License-Identifier: GPL-3.0-or-later
#include "protosim/adapters/modbusadapter.h"

#include "protosim/byteorder.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

#include <utility>

namespace protosim {
namespace {

/// 内部回环端口 = 对外端口 + 1000；被占用就往后挪。
constexpr int kBackendPortOffset = 1000;
constexpr int kBackendPortScanLimit = 20;

const QString kNoClient = QStringLiteral("-");

QModbusExceptionResponse exceptionResponse(quint8 functionCode,
                                           QModbusPdu::ExceptionCode code)
{
    return QModbusExceptionResponse(static_cast<QModbusPdu::FunctionCode>(functionCode), code);
}

bool isSupportedFunctionCode(quint8 functionCode)
{
    static const QSet<int> codes = {
        modbus::kReadCoils,
        modbus::kReadDiscreteInputs,
        modbus::kReadHoldingRegisters,
        modbus::kReadInputRegisters,
        modbus::kWriteSingleCoil,
        modbus::kWriteSingleRegister,
        modbus::kWriteMultipleCoils,
        modbus::kWriteMultipleRegisters,
    };
    return codes.contains(functionCode);
}

quint16 readUInt16(const QByteArray &bytes, int offset)
{
    return static_cast<quint16>((static_cast<quint8>(bytes.at(offset)) << 8)
                                | static_cast<quint8>(bytes.at(offset + 1)));
}

} // namespace

// ------------------------------------------------------------------ 服务端核心

ModbusServerCore::ModbusServerCore(ModbusTcpAdapter *owner)
    : QModbusTcpServer(nullptr)
    , m_owner(owner)
{
}

QModbusResponse ModbusServerCore::processRequest(const QModbusPdu &request)
{
    if (m_owner == nullptr)
        return QModbusTcpServer::processRequest(request);
    return m_owner->handleRequest(request);
}

QModbusResponse ModbusServerCore::delegateToBase(const QModbusPdu &request)
{
    return QModbusTcpServer::processRequest(request);
}

bool ModbusServerCore::readData(QModbusDataUnit *newData) const
{
    return m_owner != nullptr && m_owner->readDataUnit(newData);
}

bool ModbusServerCore::writeData(const QModbusDataUnit &unit)
{
    return m_owner != nullptr && m_owner->writeDataUnit(unit);
}

// ------------------------------------------------------------------ 适配器

ModbusTcpAdapter::ModbusTcpAdapter(DataBank *bank, WritePipeline *pipeline, TelemetryLog *log,
                                   QObject *parent)
    : QObject(parent)
    , m_bank(bank)
    , m_pipeline(pipeline)
    , m_log(log)
{
    m_server = new ModbusServerCore(this);
    m_server->setParent(this);

    m_clock = [] { return QDateTime::currentMSecsSinceEpoch(); };

    connect(&m_tap, &ModbusFrameTap::frameArrived, this, &ModbusTcpAdapter::onTapFrame);
    connect(&m_tap, &ModbusFrameTap::clientConnected, this, [this](const QString &) {
        Q_EMIT clientCountChanged(m_tap.activeClients());
    });
    connect(&m_tap, &ModbusFrameTap::clientDisconnected, this, [this](const QString &) {
        Q_EMIT clientCountChanged(m_tap.activeClients());
    });
    connect(&m_tap, &ModbusFrameTap::clientRejected, this, [this](const QString &peer) {
        if (m_log == nullptr)
            return;
        FrameRecord record;
        record.timestampMs = QDateTime::currentMSecsSinceEpoch();
        record.clientId = peer;
        record.protocol = QStringLiteral("modbus");
        record.direction = FrameDirection::Inbound;
        record.summary = QStringLiteral("连接被拒：已达上限 %1").arg(m_maxClients);
        record.error = true;
        m_log->append(record);
    });
}

ModbusTcpAdapter::~ModbusTcpAdapter()
{
    stop();
}

Capabilities ModbusTcpAdapter::capabilities() const
{
    Capabilities caps;
    caps.kind = ProtocolKind::Modbus;
    caps.supportsRead = true;
    caps.supportsWrite = true;
    caps.supportsPush = false; // Modbus 是轮询式，没有主动上送
    caps.supportsFaultInjection = false;
    caps.supportsMultiClient = true;
    caps.readFunctionCodes = {modbus::kReadCoils, modbus::kReadDiscreteInputs,
                              modbus::kReadHoldingRegisters, modbus::kReadInputRegisters};
    caps.writeFunctionCodes = {modbus::kWriteSingleCoil, modbus::kWriteSingleRegister,
                               modbus::kWriteMultipleCoils, modbus::kWriteMultipleRegisters};
    caps.faultTypes = {};
    caps.maxBindings = 0;
    return caps;
}

Result<void> ModbusTcpAdapter::start(const EndpointConfig &config)
{
    if (m_bank == nullptr || m_pipeline == nullptr) {
        return Result<void>::failure(ErrorCode::InvalidArgument,
                                     QStringLiteral("适配器缺少 DataBank / WritePipeline"));
    }

    if (m_state == AdapterState::Running)
        stop();

    const QJsonObject raw = config.raw;
    const QString bindAddress = raw.value(QStringLiteral("bindAddress"))
                                    .toString(QStringLiteral("127.0.0.1"));
    m_port = static_cast<quint16>(raw.value(QStringLiteral("port")).toInt(1502));
    m_maxClients = raw.value(QStringLiteral("maxConnections")).toInt(8);
    if (m_maxClients < 1)
        m_maxClients = 1;

    const QJsonArray unitIds = raw.value(QStringLiteral("unitIds")).toArray();
    m_unitId = unitIds.isEmpty() ? 1 : unitIds.first().toInt(1);

    // 内部回环端口：从 端口+1000 起找一个能绑上的
    quint16 backendPort = 0;
    bool serverUp = false;
    for (int attempt = 0; attempt < kBackendPortScanLimit; ++attempt) {
        const int candidate = m_port + kBackendPortOffset + attempt;
        if (candidate > 65535)
            break;

        m_server->setConnectionParameter(QModbusDevice::NetworkAddressParameter,
                                         QStringLiteral("127.0.0.1"));
        m_server->setConnectionParameter(QModbusDevice::NetworkPortParameter, candidate);
        m_server->setServerAddress(m_unitId);

        QModbusDataUnitMap map;
        for (auto type : {QModbusDataUnit::Coils, QModbusDataUnit::DiscreteInputs,
                          QModbusDataUnit::HoldingRegisters, QModbusDataUnit::InputRegisters}) {
            map.insert(type, QModbusDataUnit(type, 0, 65535));
        }
        m_server->setMap(map);

        if (m_server->connectDevice()) {
            backendPort = static_cast<quint16>(candidate);
            serverUp = true;
            break;
        }
        m_server->disconnectDevice();
    }

    if (!serverUp) {
        m_state = AdapterState::Fault;
        return Result<void>::failure(ErrorCode::Io,
                                     QStringLiteral("Modbus 服务端内部端口绑定失败"),
                                     m_server->errorString());
    }

    ModbusFrameTap::Config tapConfig;
    tapConfig.bindAddress = bindAddress;
    tapConfig.listenPort = m_port;
    tapConfig.backendPort = backendPort;
    tapConfig.maxClients = m_maxClients;

    const Result<void> tapResult = m_tap.start(tapConfig);
    if (!tapResult.ok()) {
        m_server->disconnectDevice();
        m_state = AdapterState::Fault;
        return tapResult;
    }

    m_startedAtMs = QDateTime::currentMSecsSinceEpoch();
    m_state = AdapterState::Running;

    if (m_log != nullptr) {
        FrameRecord record;
        record.timestampMs = m_startedAtMs;
        record.clientId = kNoClient;
        record.direction = FrameDirection::Inbound;
        record.summary = QStringLiteral("端点启动：%1:%2（unit %3，上限 %4 连接）")
                             .arg(bindAddress)
                             .arg(m_tap.listenPort())
                             .arg(m_unitId)
                             .arg(m_maxClients);
        m_log->append(record);
    }

    return {};
}

Result<void> ModbusTcpAdapter::stop()
{
    m_tap.stop();
    if (m_server != nullptr && m_server->state() != QModbusDevice::UnconnectedState)
        m_server->disconnectDevice();

    m_pending.clear();
    m_pendingElapsed.clear();
    m_state = AdapterState::Stopped;
    return {};
}

Result<void> ModbusTcpAdapter::bind(const QVector<Binding> &bindings)
{
    if (m_bank == nullptr) {
        return Result<void>::failure(ErrorCode::InvalidArgument, QStringLiteral("DataBank 为空"));
    }

    QHash<QString, Cell> cells;

    for (const Binding &binding : bindings) {
        if (binding.protocol != ProtocolKind::Modbus)
            continue;

        if (!m_bank->contains(binding.signalId)) {
            return Result<void>::failure(ErrorCode::Semantic,
                                         QStringLiteral("绑定引用了未注册的信号"),
                                         binding.signalId);
        }

        QModbusDataUnit::RegisterType type;
        if (!registerTypeFromName(binding.address.kind, &type)) {
            return Result<void>::failure(ErrorCode::InvalidArgument,
                                         QStringLiteral("未知寄存器类型"),
                                         binding.address.kind);
        }

        const QJsonObject fields = binding.address.fields;
        const int address = fields.value(QStringLiteral("address")).toInt(0);
        const int count = qMax(1, fields.value(QStringLiteral("count")).toInt(1));
        const int unitId = fields.value(QStringLiteral("unitId")).toInt(m_unitId);

        if (address + count > 65536) {
            return Result<void>::failure(ErrorCode::Semantic,
                                         QStringLiteral("绑定地址越界"),
                                         binding.signalId);
        }

        for (int offset = 0; offset < count; ++offset) {
            const QString key = cellKey(unitId, type, address + offset);
            if (cells.contains(key)) {
                return Result<void>::failure(
                    ErrorCode::Semantic,
                    QStringLiteral("寄存器地址被重复绑定"),
                    QStringLiteral("%1:%2").arg(key).arg(binding.signalId));
            }
            cells.insert(key, Cell{binding.signalId, offset});
        }
    }

    m_cells = cells;
    if (m_state != AdapterState::Running)
        m_state = AdapterState::Bound;

    return {};
}

AdapterState ModbusTcpAdapter::state() const
{
    return m_state;
}

void ModbusTcpAdapter::setClockSource(std::function<qint64()> clock)
{
    m_clock = std::move(clock);
}

qint64 ModbusTcpAdapter::nowMs() const
{
    return m_clock ? m_clock() : QDateTime::currentMSecsSinceEpoch();
}

quint16 ModbusTcpAdapter::listenPort() const
{
    return m_tap.listenPort();
}

int ModbusTcpAdapter::activeClients() const
{
    return m_tap.activeClients();
}

// ------------------------------------------------------------------ 请求处理

QModbusResponse ModbusTcpAdapter::handleRequest(const QModbusPdu &request)
{
    const quint8 functionCode = request.functionCode();

    // 归属：帧观察层按到达顺序排队，这里取队首（同一连接的请求天然保序）
    m_currentClientId = m_pending.isEmpty() ? kNoClient : m_pending.dequeue().clientId;

    // 1. 功能码支持性 -> 0x01
    if (!isSupportedFunctionCode(functionCode))
        return exceptionResponse(functionCode, QModbusPdu::IllegalFunction);

    // 复用 ADU 解析，避免在 PDU 上再写一遍字段读取
    modbus::Adu synthetic;
    synthetic.valid = true;
    synthetic.functionCode = functionCode;
    synthetic.pdu = QByteArray(1, static_cast<char>(functionCode)) + QByteArray(request.data());
    const modbus::AddressRange range = modbus::addressRangeOf(synthetic);

    if (!range.valid)
        return exceptionResponse(functionCode, QModbusPdu::IllegalDataValue);

    // 2. 数量越界 -> 0x03（读 126 个寄存器、写 0 个寄存器都算非法数据值）
    if (!modbus::isQuantityValid(functionCode, range.count))
        return exceptionResponse(functionCode, QModbusPdu::IllegalDataValue);

    // 3. 写报文的数据长度与数量必须自洽 -> 0x03
    if (functionCode == modbus::kWriteMultipleRegisters
        || functionCode == modbus::kWriteMultipleCoils) {
        const int expected = functionCode == modbus::kWriteMultipleRegisters
            ? range.count * 2
            : (range.count + 7) / 8;
        if (range.dataBytes != expected)
            return exceptionResponse(functionCode, QModbusPdu::IllegalDataValue);
    }

    // 4. 单线圈的合法值只有 0x0000 / 0xFF00 -> 0x03
    if (functionCode == modbus::kWriteSingleCoil) {
        if (synthetic.pdu.size() < 5)
            return exceptionResponse(functionCode, QModbusPdu::IllegalDataValue);
        const quint16 value = readUInt16(synthetic.pdu, 3);
        if (value != 0x0000 && value != 0xFF00)
            return exceptionResponse(functionCode, QModbusPdu::IllegalDataValue);
    }

    // 5. 地址必须全部映射且不越界 -> 0x02
    const QModbusDataUnit::RegisterType type = registerTypeOf(functionCode);
    if (range.start + range.count > 65536)
        return exceptionResponse(functionCode, QModbusPdu::IllegalDataAddress);

    for (int i = 0; i < range.count; ++i) {
        if (cellAt(m_unitId, type, range.start + i).signalId.isEmpty())
            return exceptionResponse(functionCode, QModbusPdu::IllegalDataAddress);
    }

    // 6. 交给 Qt 做真正的编解码（组响应、异常兜底）
    return m_server->delegateToBase(request);
}

bool ModbusTcpAdapter::readDataUnit(QModbusDataUnit *unit) const
{
    if (unit == nullptr || m_bank == nullptr)
        return false;

    const QModbusDataUnit::RegisterType type = unit->registerType();
    const int start = unit->startAddress();
    const int count = unit->valueCount();

    QVector<quint16> values;
    values.reserve(count);
    QHash<QString, QVector<quint16>> cache;

    for (int i = 0; i < count; ++i) {
        const Cell cell = cellAt(m_unitId, type, start + i);
        if (cell.signalId.isEmpty())
            return false;

        if (!cache.contains(cell.signalId))
            cache.insert(cell.signalId, rawRegistersOf(cell.signalId));

        const QVector<quint16> registers = cache.value(cell.signalId);
        values.append(cell.offset >= 0 && cell.offset < registers.size() ? registers.at(cell.offset)
                                                                        : 0);
    }

    unit->setValues(values);
    return true;
}

bool ModbusTcpAdapter::writeDataUnit(const QModbusDataUnit &unit)
{
    if (m_bank == nullptr || m_pipeline == nullptr)
        return false;

    const QModbusDataUnit::RegisterType type = unit.registerType();
    const int start = unit.startAddress();
    const int count = unit.valueCount();

    // 按信号归并：一次 FC16 可能横跨两个信号的寄存器
    QHash<QString, QHash<int, quint16>> patches;
    for (int i = 0; i < count; ++i) {
        const Cell cell = cellAt(m_unitId, type, start + i);
        if (cell.signalId.isEmpty())
            return false;
        patches[cell.signalId].insert(cell.offset, unit.value(i));
    }

    const QString clientId = currentClientId();
    const qint64 timestamp = nowMs();

    QVector<WriteIntent> intents;
    intents.reserve(patches.size());

    for (auto it = patches.constBegin(); it != patches.constEnd(); ++it) {
        const QString signalId = it.key();
        const Signal definition = m_bank->definition(signalId);
        if (definition.id.isEmpty())
            return false;

        QVector<quint16> registers = rawRegistersOf(signalId);
        for (auto patch = it.value().constBegin(); patch != it.value().constEnd(); ++patch) {
            const int offset = patch.key();
            if (offset < 0)
                return false;
            while (registers.size() <= offset)
                registers.append(0);
            registers[offset] = patch.value();
        }

        bool decoded = false;
        const double raw = byteorder::decodeNumber(registers, definition.dataType,
                                                   definition.byteOrder, &decoded);
        if (!decoded)
            return false;

        WriteIntent intent;
        intent.signalId = signalId;
        intent.value = definition.rawToEngineering(raw);
        intent.sourceProtocol = QStringLiteral("modbus");
        intent.clientId = clientId;
        intents.append(intent);
    }

    const QVector<WriteOutcome> outcomes = m_pipeline->submitBatch(intents, timestamp);
    for (WriteOutcome outcome : outcomes) {
        if (outcome != WriteOutcome::Accepted)
            return false;
    }

    return true;
}

// ------------------------------------------------------------------ IProtocolAdapter 的读写入口

ReadResult ModbusTcpAdapter::onRead(const ReadRequest &request)
{
    ReadResult result;

    if (m_bank == nullptr) {
        result.data = Result<QVector<SignalSnapshot>>::failure(ErrorCode::InvalidArgument,
                                                              QStringLiteral("DataBank 为空"));
        return result;
    }

    QVector<SignalSnapshot> snapshots;
    for (const AddressRef &address : request.addresses) {
        QModbusDataUnit::RegisterType type;
        if (!registerTypeFromName(address.kind, &type)) {
            result.protocolErrorCode = modbus::kIllegalDataAddress;
            continue;
        }
        const int start = address.fields.value(QStringLiteral("address")).toInt(0);
        const int count = qMax(1, address.fields.value(QStringLiteral("count")).toInt(1));

        for (int i = 0; i < count; ++i) {
            const Cell cell = cellAt(m_unitId, type, start + i);
            if (cell.signalId.isEmpty()) {
                result.protocolErrorCode = modbus::kIllegalDataAddress;
                continue;
            }
            if (cell.offset != 0)
                continue; // 信号整体只返回一份快照
            snapshots.append(m_bank->snapshot(cell.signalId));
        }
    }

    result.data = snapshots;
    return result;
}

WriteResult ModbusTcpAdapter::onWrite(const WriteRequest &request)
{
    WriteResult result;

    if (m_pipeline == nullptr) {
        result.protocolErrorCode = modbus::kServerDeviceFailure;
        return result;
    }

    QVector<WriteIntent> intents;
    QVector<QString> order;
    for (const auto &pair : request.values) {
        QModbusDataUnit::RegisterType type;
        if (!registerTypeFromName(pair.first.kind, &type)) {
            result.protocolErrorCode = modbus::kIllegalDataAddress;
            continue;
        }
        const int address = pair.first.fields.value(QStringLiteral("address")).toInt(0);
        const Cell cell = cellAt(m_unitId, type, address);
        if (cell.signalId.isEmpty()) {
            result.protocolErrorCode = modbus::kIllegalDataAddress;
            result.outcomes.append({QString(), WriteOutcome::RejectedUnknownSignal});
            continue;
        }

        WriteIntent intent;
        intent.signalId = cell.signalId;
        intent.value = pair.second;
        intent.sourceProtocol = QStringLiteral("modbus");
        intent.clientId = request.clientId;
        intents.append(intent);
        order.append(cell.signalId);
    }

    const QVector<WriteOutcome> outcomes = m_pipeline->submitBatch(intents, nowMs());
    for (int i = 0; i < outcomes.size() && i < order.size(); ++i)
        result.outcomes.append({order.at(i), outcomes.at(i)});

    return result;
}

void ModbusTcpAdapter::pushTelemetry(const QVector<SignalChange> &changes)
{
    // Modbus 是主站轮询式：不主动上送。这里只记录，供 stats / 自检确认联动确实发生过。
    if (changes.isEmpty())
        return;
    m_lastPushCount = changes.size();
}

Result<void> ModbusTcpAdapter::injectFault(const FaultSpec &spec)
{
    Q_UNUSED(spec)
    return Result<void>::failure(ErrorCode::Unsupported,
                                 QStringLiteral("P0 不提供 Modbus 故障注入（P1 随故障面板一起做）"));
}

AdapterStats ModbusTcpAdapter::stats() const
{
    AdapterStats stats;
    stats.kind = ProtocolKind::Modbus;
    stats.activeClients = m_tap.activeClients();
    stats.framesRx = m_framesRx;
    stats.framesTx = m_framesTx;
    stats.errorFrames = m_errorFrames;
    stats.uptimeMs = m_startedAtMs > 0
        ? QDateTime::currentMSecsSinceEpoch() - m_startedAtMs
        : 0;

    QJsonObject extra;
    extra.insert(QStringLiteral("port"), static_cast<int>(m_tap.listenPort()));
    extra.insert(QStringLiteral("unitId"), m_unitId);
    extra.insert(QStringLiteral("maxConnections"), m_maxClients);
    extra.insert(QStringLiteral("boundCells"), m_cells.size());
    extra.insert(QStringLiteral("lastPushCount"), m_lastPushCount);
    stats.extra = extra;
    return stats;
}

// ------------------------------------------------------------------ 帧观察

void ModbusTcpAdapter::onTapFrame(const QByteArray &aduBytes, const QString &clientId,
                                  bool inbound, qint64 timestampMs)
{
    const modbus::Adu adu = modbus::parseAdu(aduBytes);
    if (!adu.valid) {
        if (m_log != nullptr) {
            FrameRecord record;
            record.timestampMs = timestampMs;
            record.clientId = clientId;
            record.protocol = QStringLiteral("modbus");
            record.direction = inbound ? FrameDirection::Inbound : FrameDirection::Outbound;
            record.bytes = aduBytes;
            record.summary = QStringLiteral("非法报文：%1").arg(adu.error);
            record.error = true;
            m_log->append(record);
        }
        ++m_errorFrames;
        return;
    }

    const QString key = QStringLiteral("%1|%2").arg(clientId).arg(adu.transactionId);

    if (inbound) {
        ++m_framesRx;
        m_pending.enqueue(Pending{clientId, adu.transactionId, timestampMs});
        while (m_pending.size() > 128)
            m_pending.dequeue();
        QElapsedTimer timer;
        timer.start();
        m_pendingElapsed.insert(key, timer);
        logFrame(aduBytes, adu, clientId, true, timestampMs, -1);
        return;
    }

    ++m_framesTx;
    if (adu.isException())
        ++m_errorFrames;

    qint64 elapsedUs = -1;
    auto timerIt = m_pendingElapsed.find(key);
    if (timerIt != m_pendingElapsed.end()) {
        elapsedUs = timerIt.value().nsecsElapsed() / 1000;
        m_pendingElapsed.erase(timerIt);
    }

    logFrame(aduBytes, adu, clientId, false, timestampMs, elapsedUs);
}

void ModbusTcpAdapter::logFrame(const QByteArray &aduBytes, const modbus::Adu &adu,
                                const QString &clientId, bool inbound, qint64 timestampMs,
                                qint64 elapsedUs)
{
    if (m_log == nullptr)
        return;

    FrameRecord record;
    record.timestampMs = timestampMs;
    record.elapsedUs = elapsedUs;
    record.clientId = clientId;
    record.protocol = QStringLiteral("modbus");
    record.direction = inbound ? FrameDirection::Inbound : FrameDirection::Outbound;
    record.bytes = aduBytes;
    record.transactionId = adu.transactionId;
    record.functionCode = adu.functionCode;
    record.error = adu.isException();

    const modbus::AddressRange range = modbus::addressRangeOf(adu);
    if (range.valid)
        record.signalIds = signalsForRange(adu.functionCode, range.start, range.count);

    record.summary = modbus::describe(adu, record.signalIds);
    m_log->append(record);
}

QStringList ModbusTcpAdapter::signalsForRange(quint8 functionCode, quint16 start,
                                              quint16 count) const
{
    QStringList ids;
    const QModbusDataUnit::RegisterType type = registerTypeOf(functionCode);
    for (int i = 0; i < count; ++i) {
        const Cell cell = cellAt(m_unitId, type, start + i);
        if (!cell.signalId.isEmpty() && !ids.contains(cell.signalId))
            ids.append(cell.signalId);
    }
    return ids;
}

// ------------------------------------------------------------------ 工具

ModbusTcpAdapter::Cell ModbusTcpAdapter::cellAt(int unitId, QModbusDataUnit::RegisterType type,
                                                int address) const
{
    return m_cells.value(cellKey(unitId, type, address));
}

QVector<quint16> ModbusTcpAdapter::rawRegistersOf(const QString &signalId) const
{
    if (m_bank == nullptr)
        return {};

    const Signal definition = m_bank->definition(signalId);
    if (definition.id.isEmpty())
        return {};

    const SignalSnapshot snapshot = m_bank->snapshot(signalId);
    bool ok = false;
    const double engineering = snapshot.value.toDouble(&ok);
    if (!ok)
        return {};

    const double raw = definition.engineeringToRaw(engineering);
    return byteorder::encodeNumber(raw, definition.dataType, definition.byteOrder);
}

QString ModbusTcpAdapter::cellKey(int unitId, QModbusDataUnit::RegisterType type, int address)
{
    return QStringLiteral("%1|%2|%3").arg(unitId).arg(static_cast<int>(type)).arg(address);
}

QModbusDataUnit::RegisterType ModbusTcpAdapter::registerTypeOf(quint8 functionCode)
{
    switch (functionCode) {
    case modbus::kReadCoils:
    case modbus::kWriteSingleCoil:
    case modbus::kWriteMultipleCoils:
        return QModbusDataUnit::Coils;
    case modbus::kReadDiscreteInputs:
        return QModbusDataUnit::DiscreteInputs;
    case modbus::kReadInputRegisters:
        return QModbusDataUnit::InputRegisters;
    default:
        break;
    }
    return QModbusDataUnit::HoldingRegisters;
}

bool ModbusTcpAdapter::registerTypeFromName(const QString &name,
                                           QModbusDataUnit::RegisterType *out)
{
    QModbusDataUnit::RegisterType type = QModbusDataUnit::HoldingRegisters;
    bool matched = true;

    if (name == QLatin1String("Coils"))
        type = QModbusDataUnit::Coils;
    else if (name == QLatin1String("DiscreteInputs"))
        type = QModbusDataUnit::DiscreteInputs;
    else if (name == QLatin1String("HoldingRegisters"))
        type = QModbusDataUnit::HoldingRegisters;
    else if (name == QLatin1String("InputRegisters"))
        type = QModbusDataUnit::InputRegisters;
    else
        matched = false;

    if (matched && out)
        *out = type;
    return matched;
}

QVector<Binding> ModbusTcpAdapter::bindingsFromBundle(const ConfigBundle &bundle)
{
    QVector<Binding> bindings;

    for (const Signal &signal : bundle.signals) {
        for (const ProtocolBinding &binding : signal.bindings) {
            if (binding.protocol != ProtocolKind::Modbus)
                continue;

            Binding out;
            out.signalId = signal.id;
            out.protocol = ProtocolKind::Modbus;
            out.address.kind = binding.registerType;
            out.address.fields.insert(QStringLiteral("address"), binding.address);
            out.address.fields.insert(QStringLiteral("count"), binding.count);
            out.address.fields.insert(QStringLiteral("unitId"), binding.unitId);
            out.extension = binding.raw;
            bindings.append(out);
        }
    }

    return bindings;
}

} // namespace protosim
