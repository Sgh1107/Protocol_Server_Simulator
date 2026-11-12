// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "protosim/protocolkind.h"
#include "protosim/quality.h"
#include "protosim/result.h"
#include "protosim/snapshot.h"

#include <QJsonObject>
#include <QString>
#include <QVariant>
#include <QVector>

#include <utility>

namespace protosim {

/// 适配器生命周期：Created -> Bound -> Running -> Stopped / Fault。
enum class AdapterState {
    Created,
    Bound,
    Running,
    Stopped,
    Fault,
};

QString adapterStateToString(AdapterState state);

/// 一次写入的结果。拒绝原因要能原样回给主站（映射成协议错误码）。
enum class WriteOutcome {
    Accepted,
    RejectedWriteProtected,
    RejectedInterlock,
    RejectedOutOfRange,
    RejectedUnknownSignal,
};

QString writeOutcomeToString(WriteOutcome outcome);

/// 能力声明：UI 据此动态渲染，新增协议不改 UI 代码。
struct Capabilities
{
    ProtocolKind kind = ProtocolKind::Modbus;
    bool supportsRead = true;
    bool supportsWrite = true;
    bool supportsPush = false;
    bool supportsFaultInjection = false;
    bool supportsMultiClient = false;
    QVector<int> readFunctionCodes;
    QVector<int> writeFunctionCodes;
    QVector<QString> faultTypes;
    int maxBindings = 0; // 0 = 不限
};

/// 协议地址。具体含义由适配器解释（modbus: registerType+address；104: ioa；61850: iedPath）。
struct AddressRef
{
    QString kind;
    QJsonObject fields;
};

struct Binding
{
    QString signalId;
    ProtocolKind protocol = ProtocolKind::Modbus;
    AddressRef address;
    QJsonObject extension; // 61850 等后期协议的专属字段，接口不解释
};

struct ReadRequest
{
    QString clientId;
    QVector<AddressRef> addresses;
    QJsonObject context;
};

struct WriteRequest
{
    QString clientId;
    QVector<QPair<AddressRef, QVariant>> values;
    QJsonObject context;
};

struct ReadResult
{
    Result<QVector<SignalSnapshot>> data;
    int protocolErrorCode = 0; // 协议层错误码（Modbus 0x01~0x04），与 Error::code 分开
};

struct WriteResult
{
    QVector<QPair<QString, WriteOutcome>> outcomes;
    int protocolErrorCode = 0;
};

struct FaultSpec
{
    QString id;
    QString target;
    QString type;
    QJsonObject params;
    bool enabled = false;
    int durationMs = 0;
};

struct AdapterStats
{
    ProtocolKind kind = ProtocolKind::Modbus;
    int activeClients = 0;
    quint64 framesRx = 0;
    quint64 framesTx = 0;
    quint64 errorFrames = 0;
    qint64 uptimeMs = 0;
    QJsonObject extra;
};

/// 端点配置，来自配置包 endpoints[]，原样交给适配器解释。
struct EndpointConfig
{
    ProtocolKind kind = ProtocolKind::Modbus;
    QJsonObject raw;
};

/// 四层架构里唯一能让 61850 平滑接入的接缝（docs/INTERFACES.md）。
///
/// 铁律：
///   1. 只依赖 QtCore —— sim-core 不链接任何协议库；
///   2. 写必须走 WritePipeline，禁止直接改 DataBank；
///   3. 不抛异常跨线程/进程，一律 Result。
class IProtocolAdapter
{
public:
    virtual ~IProtocolAdapter() = default;

    virtual Capabilities capabilities() const = 0;

    virtual Result<void> start(const EndpointConfig &config) = 0;
    virtual Result<void> stop() = 0;
    virtual Result<void> bind(const QVector<Binding> &bindings) = 0;
    virtual AdapterState state() const = 0;

    virtual ReadResult onRead(const ReadRequest &request) = 0;
    virtual WriteResult onWrite(const WriteRequest &request) = 0;

    /// 实时库变化 -> 主动上送 / 突发 / publish。只推 diff。
    virtual void pushTelemetry(const QVector<SignalChange> &changes) = 0;

    virtual Result<void> injectFault(const FaultSpec &spec) = 0;
    virtual AdapterStats stats() const = 0;
};

} // namespace protosim
