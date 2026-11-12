// SPDX-License-Identifier: GPL-3.0-or-later
#include "protosim/configbundle.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QStringList>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace protosim {
namespace {

constexpr auto kSchemaVersionPattern = "^[0-9]+\\.[0-9]+(\\.[0-9]+)?$";

Error schemaError(const QString &path, const QString &what)
{
    return Error::make(ErrorCode::Schema, QStringLiteral("配置结构不合法：%1").arg(what), path);
}

Error semanticError(const QString &path, const QString &what)
{
    return Error::make(ErrorCode::Semantic, QStringLiteral("配置语义不合法：%1").arg(what), path);
}

bool isInteger(const QJsonValue &value)
{
    if (!value.isDouble())
        return false;
    const double d = value.toDouble();
    return std::floor(d) == d;
}

bool isNonEmptyString(const QJsonValue &value)
{
    return value.isString() && !value.toString().trimmed().isEmpty();
}

/// 配置包内的相对路径是否越出包目录（AC-P0-05 的 ../../evil.json）。
/// 绝对路径一律拒绝：绿色包要能整目录拷走，路径不能指到包外。
bool pathEscapesBundle(const QString &baseDir, const QString &relative, QString *resolved)
{
    QString normalized = relative;
    normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));

    if (QFileInfo(normalized).isAbsolute())
        return true;
    if (normalized.startsWith(QLatin1Char('/')))
        return true;

    const QString base = QDir(baseDir).absolutePath();
    const QString joined = QDir::cleanPath(base + QLatin1Char('/') + normalized);
    if (resolved)
        *resolved = joined;

    if (joined == base)
        return false;
    return !joined.startsWith(base + QLatin1Char('/'));
}

const QStringList &modbusRegisterTypes()
{
    static const QStringList types = {
        QStringLiteral("Coils"),
        QStringLiteral("DiscreteInputs"),
        QStringLiteral("HoldingRegisters"),
        QStringLiteral("InputRegisters"),
    };
    return types;
}

const QStringList &behaviorTypes()
{
    static const QStringList types = {
        QStringLiteral("constant"),
        QStringLiteral("sine"),
        QStringLiteral("randomWalk"),
        QStringLiteral("ramp"),
        QStringLiteral("stepSequence"),
        QStringLiteral("csvReplay"),
        QStringLiteral("script"),
        QStringLiteral("forced"),
    };
    return types;
}

const QStringList &faultTypes()
{
    static const QStringList types = {
        QStringLiteral("rejectConnection"),
        QStringLiteral("linkDown"),
        QStringLiteral("latencyMs"),
        QStringLiteral("dropPacket"),
        QStringLiteral("exceptionCode"),
        QStringLiteral("badQuality"),
        QStringLiteral("freezeValue"),
        QStringLiteral("timestampRollback"),
    };
    return types;
}

/// 判断 script 字段到底是 JSEngine 源码还是配置文件路径。
/// 含代码特征字符时按源码处理，不做路径穿越检查（源码本来就不是路径）。
bool looksLikeScriptSource(const QString &text)
{
    static const QString markers = QStringLiteral("();{}\n=");
    for (QChar ch : markers) {
        if (text.contains(ch))
            return true;
    }
    return false;
}

class Validator
{
public:
    Validator(const QJsonObject &root, const QString &baseDir)
        : m_root(root)
        , m_baseDir(baseDir)
    {
    }

    Result<void> run()
    {
        Error error;
        if (!checkSchemaVersion(&error) || !checkMeta(&error) || !checkDevices(&error)
            || !checkEndpoints(&error) || !checkSignals(&error) || !checkFaults(&error)
            || !checkControlApi(&error) || !checkPathEscapes(&error)
            || !checkAddressOverlap(&error)) {
            return Result<void>::failure(error);
        }
        return {};
    }

private:
    bool checkSchemaVersion(Error *error)
    {
        const QJsonValue value = m_root.value(QStringLiteral("schemaVersion"));
        if (value.isUndefined()) {
            *error = schemaError(QStringLiteral("schemaVersion"),
                                 QStringLiteral("缺少 schemaVersion，无法做版本路由"));
            return false;
        }
        if (!value.isString()) {
            *error = schemaError(QStringLiteral("schemaVersion"),
                                 QStringLiteral("schemaVersion 必须是字符串，如 \"1.0\""));
            return false;
        }

        const QString version = value.toString();
        static const QRegularExpression pattern(QString::fromLatin1(kSchemaVersionPattern));
        if (!pattern.match(version).hasMatch()) {
            *error = schemaError(QStringLiteral("schemaVersion"),
                                 QStringLiteral("\"%1\" 不符合 x.y 或 x.y.z 格式").arg(version));
            return false;
        }

        const int major = version.section(QLatin1Char('.'), 0, 0).toInt();
        if (major != 1) {
            *error = Error::make(ErrorCode::Unsupported,
                                 QStringLiteral("不支持的 schemaVersion 主版本 %1").arg(major),
                                 QStringLiteral("当前只认 1.x，迁移逻辑待 P1 补齐"));
            return false;
        }

        return true;
    }

    bool checkMeta(Error *error)
    {
        const QJsonValue metaValue = m_root.value(QStringLiteral("meta"));
        if (!metaValue.isObject()) {
            *error = schemaError(QStringLiteral("meta"), QStringLiteral("meta 必须是对象"));
            return false;
        }

        const QJsonObject meta = metaValue.toObject();

        if (!isNonEmptyString(meta.value(QStringLiteral("name")))) {
            *error = schemaError(QStringLiteral("meta.name"),
                                 QStringLiteral("meta.name 必填且不能为空"));
            return false;
        }

        const QJsonValue seed = meta.value(QStringLiteral("seed"));
        if (!isInteger(seed)) {
            *error = schemaError(QStringLiteral("meta.seed"),
                                 QStringLiteral("meta.seed 必须是整数（确定性复现的根）"));
            return false;
        }

        const QJsonValue tickMs = meta.value(QStringLiteral("tickMs"));
        if (!isInteger(tickMs) || tickMs.toDouble() < 10 || tickMs.toDouble() > 10000) {
            *error = schemaError(QStringLiteral("meta.tickMs"),
                                 QStringLiteral("meta.tickMs 必须是 10~10000 之间的整数"));
            return false;
        }

        const QJsonValue timeSource = meta.value(QStringLiteral("timeSource"));
        if (!timeSource.isUndefined() && timeSource.toString() != QLatin1String("simulation")
            && timeSource.toString() != QLatin1String("system")) {
            *error = schemaError(QStringLiteral("meta.timeSource"),
                                 QStringLiteral("meta.timeSource 只能是 simulation 或 system"));
            return false;
        }

        return true;
    }

    bool checkDevices(Error *error)
    {
        const QJsonValue devicesValue = m_root.value(QStringLiteral("devices"));
        if (!devicesValue.isArray() || devicesValue.toArray().isEmpty()) {
            *error = schemaError(QStringLiteral("devices"),
                                 QStringLiteral("devices 必须是非空数组"));
            return false;
        }

        const QJsonArray devices = devicesValue.toArray();
        QSet<QString> seen;
        for (int i = 0; i < devices.size(); ++i) {
            const QString path = QStringLiteral("devices[%1]").arg(i);
            if (!devices.at(i).isObject()) {
                *error = schemaError(path, QStringLiteral("设备项必须是对象"));
                return false;
            }
            const QJsonObject device = devices.at(i).toObject();
            if (!isNonEmptyString(device.value(QStringLiteral("id")))) {
                *error = schemaError(path + QStringLiteral(".id"), QStringLiteral("设备 id 必填"));
                return false;
            }
            if (!isNonEmptyString(device.value(QStringLiteral("name")))) {
                *error = schemaError(path + QStringLiteral(".name"), QStringLiteral("设备 name 必填"));
                return false;
            }
            const QString id = device.value(QStringLiteral("id")).toString();
            if (seen.contains(id)) {
                *error = semanticError(path + QStringLiteral(".id"),
                                       QStringLiteral("设备 id \"%1\" 重复").arg(id));
                return false;
            }
            seen.insert(id);
            m_deviceIds.insert(id);
        }

        return true;
    }

    bool checkEndpoints(Error *error)
    {
        const QJsonValue value = m_root.value(QStringLiteral("endpoints"));
        if (!value.isArray() || value.toArray().isEmpty()) {
            *error = schemaError(QStringLiteral("endpoints"),
                                 QStringLiteral("endpoints 必须是非空数组"));
            return false;
        }

        const QJsonArray endpoints = value.toArray();
        for (int i = 0; i < endpoints.size(); ++i) {
            const QString path = QStringLiteral("endpoints[%1]").arg(i);
            if (!endpoints.at(i).isObject()) {
                *error = schemaError(path, QStringLiteral("端点项必须是对象"));
                return false;
            }
            const QJsonObject endpoint = endpoints.at(i).toObject();

            ProtocolKind kind = ProtocolKind::Modbus;
            if (!protocolKindFromString(endpoint.value(QStringLiteral("protocol")).toString(), &kind)) {
                *error = schemaError(path + QStringLiteral(".protocol"),
                                     QStringLiteral("未知协议 \"%1\"")
                                         .arg(endpoint.value(QStringLiteral("protocol")).toString()));
                return false;
            }

            const QJsonValue port = endpoint.value(QStringLiteral("port"));
            if (!port.isUndefined()
                && (!isInteger(port) || port.toDouble() < 1 || port.toDouble() > 65535)) {
                *error = schemaError(path + QStringLiteral(".port"),
                                     QStringLiteral("端口必须是 1~65535"));
                return false;
            }

            if (kind == ProtocolKind::Modbus) {
                const QString transport = endpoint.value(QStringLiteral("transport")).toString();
                if (transport != QLatin1String("tcp") && transport != QLatin1String("rtu")) {
                    *error = schemaError(path + QStringLiteral(".transport"),
                                         QStringLiteral("Modbus 端点 transport 只能是 tcp 或 rtu"));
                    return false;
                }
                if (port.isUndefined()) {
                    *error = schemaError(path + QStringLiteral(".port"),
                                         QStringLiteral("Modbus 端点必须给 port（默认 1502）"));
                    return false;
                }
            }

            const QJsonValue maxConnections = endpoint.value(QStringLiteral("maxConnections"));
            if (!maxConnections.isUndefined()
                && (!isInteger(maxConnections) || maxConnections.toDouble() < 1)) {
                *error = schemaError(path + QStringLiteral(".maxConnections"),
                                     QStringLiteral("maxConnections 必须是正整数"));
                return false;
            }

            m_endpointKinds.insert(static_cast<int>(kind));
        }

        return true;
    }

    bool checkSignals(Error *error)
    {
        const QJsonValue value = m_root.value(QStringLiteral("signals"));
        if (!value.isArray() || value.toArray().isEmpty()) {
            *error = schemaError(QStringLiteral("signals"), QStringLiteral("signals 必须是非空数组"));
            return false;
        }

        const QJsonArray signals = value.toArray();
        QSet<QString> seen;
        for (int i = 0; i < signals.size(); ++i) {
            const QString path = QStringLiteral("signals[%1]").arg(i);
            if (!signals.at(i).isObject()) {
                *error = schemaError(path, QStringLiteral("信号项必须是对象"));
                return false;
            }
            const QJsonObject signal = signals.at(i).toObject();

            for (const QString &field : {QStringLiteral("id"), QStringLiteral("name"),
                                         QStringLiteral("deviceId"), QStringLiteral("category")}) {
                if (!isNonEmptyString(signal.value(field))) {
                    *error = schemaError(path + QLatin1Char('.') + field,
                                         QStringLiteral("%1 必填且不能为空").arg(field));
                    return false;
                }
            }

            const QString id = signal.value(QStringLiteral("id")).toString();
            if (seen.contains(id)) {
                *error = semanticError(path + QStringLiteral(".id"),
                                       QStringLiteral("信号 id \"%1\" 重复").arg(id));
                return false;
            }
            seen.insert(id);

            const QString deviceId = signal.value(QStringLiteral("deviceId")).toString();
            if (!m_deviceIds.contains(deviceId)) {
                *error = semanticError(path + QStringLiteral(".deviceId"),
                                       QStringLiteral("引用了不存在的设备 \"%1\"").arg(deviceId));
                return false;
            }

            DataType dataType = DataType::Uint16;
            if (!dataTypeFromString(signal.value(QStringLiteral("dataType")).toString(), &dataType)) {
                *error = schemaError(path + QStringLiteral(".dataType"),
                                     QStringLiteral("未知数据类型 \"%1\"")
                                         .arg(signal.value(QStringLiteral("dataType")).toString()));
                return false;
            }

            const QJsonValue byteOrder = signal.value(QStringLiteral("byteOrder"));
            if (!byteOrder.isUndefined()) {
                ByteOrder parsed = ByteOrder::ABCD;
                if (!byteOrderFromString(byteOrder.toString(), &parsed)) {
                    *error = schemaError(path + QStringLiteral(".byteOrder"),
                                         QStringLiteral("byteOrder 只能是 ABCD/BADC/CDAB/DCBA"));
                    return false;
                }
            }

            const QJsonValue quality = signal.value(QStringLiteral("quality"));
            if (!quality.isUndefined()) {
                static const QStringList qualities = {QStringLiteral("Good"), QStringLiteral("Bad"),
                                                      QStringLiteral("Timeout"),
                                                      QStringLiteral("OutOfRange"),
                                                      QStringLiteral("Unknown")};
                if (!qualities.contains(quality.toString())) {
                    *error = schemaError(path + QStringLiteral(".quality"),
                                         QStringLiteral("未知质量码 \"%1\"").arg(quality.toString()));
                    return false;
                }
            }

            const QJsonValue range = signal.value(QStringLiteral("range"));
            if (!range.isUndefined()) {
                if (!range.isObject() || !range.toObject().value(QStringLiteral("min")).isDouble()
                    || !range.toObject().value(QStringLiteral("max")).isDouble()) {
                    *error = schemaError(path + QStringLiteral(".range"),
                                         QStringLiteral("range 必须含数值型 min/max"));
                    return false;
                }
            }

            const QJsonValue behavior = signal.value(QStringLiteral("behavior"));
            if (!behavior.isObject()) {
                *error = schemaError(path + QStringLiteral(".behavior"),
                                     QStringLiteral("behavior 必须是对象"));
                return false;
            }
            const QString behaviorType = behavior.toObject().value(QStringLiteral("type")).toString();
            if (!behaviorTypes().contains(behaviorType)) {
                *error = schemaError(path + QStringLiteral(".behavior.type"),
                                     QStringLiteral("未知行为类型 \"%1\"").arg(behaviorType));
                return false;
            }

            const QJsonValue bindings = signal.value(QStringLiteral("bindings"));
            if (!bindings.isArray() || bindings.toArray().isEmpty()) {
                *error = schemaError(path + QStringLiteral(".bindings"),
                                     QStringLiteral("每个信号至少一个协议绑定"));
                return false;
            }

            const QJsonArray bindingArray = bindings.toArray();
            for (int b = 0; b < bindingArray.size(); ++b) {
                const QString bindingPath =
                    path + QStringLiteral(".bindings[%1]").arg(b);
                if (!bindingArray.at(b).isObject()) {
                    *error = schemaError(bindingPath, QStringLiteral("绑定项必须是对象"));
                    return false;
                }
                const QJsonObject binding = bindingArray.at(b).toObject();

                ProtocolKind kind = ProtocolKind::Modbus;
                if (!protocolKindFromString(binding.value(QStringLiteral("protocol")).toString(),
                                            &kind)) {
                    *error = schemaError(bindingPath + QStringLiteral(".protocol"),
                                         QStringLiteral("未知协议"));
                    return false;
                }

                if (!m_endpointKinds.contains(static_cast<int>(kind))) {
                    *error = semanticError(
                        bindingPath + QStringLiteral(".protocol"),
                        QStringLiteral("信号 \"%1\" 绑定了 %2，但配置里没有对应端点")
                            .arg(id, protocolKindToString(kind)));
                    return false;
                }

                if (kind != ProtocolKind::Modbus)
                    continue;

                const QJsonValue unitId = binding.value(QStringLiteral("unitId"));
                if (!isInteger(unitId) || unitId.toDouble() < 0 || unitId.toDouble() > 247) {
                    *error = schemaError(bindingPath + QStringLiteral(".unitId"),
                                         QStringLiteral("unitId 必须是 0~247 的整数"));
                    return false;
                }

                if (!modbusRegisterTypes().contains(
                        binding.value(QStringLiteral("registerType")).toString())) {
                    *error = schemaError(bindingPath + QStringLiteral(".registerType"),
                                         QStringLiteral("registerType 只能是 Coils/DiscreteInputs/"
                                                        "HoldingRegisters/InputRegisters"));
                    return false;
                }

                const QJsonValue address = binding.value(QStringLiteral("address"));
                if (!isInteger(address) || address.toDouble() < 0 || address.toDouble() > 65535) {
                    *error = schemaError(bindingPath + QStringLiteral(".address"),
                                         QStringLiteral("address 必须是 0~65535 的整数"));
                    return false;
                }

                const QJsonValue count = binding.value(QStringLiteral("count"));
                int resolvedCount = 1;
                if (!count.isUndefined()) {
                    if (!isInteger(count) || count.toDouble() < 1) {
                        *error = schemaError(bindingPath + QStringLiteral(".count"),
                                             QStringLiteral("count 必须是正整数"));
                        return false;
                    }
                    resolvedCount = static_cast<int>(count.toDouble());
                } else if (dataType != DataType::Ascii) {
                    resolvedCount = dataTypeRegisterCount(dataType);
                }

                if (dataType != DataType::Ascii) {
                    const int required = dataTypeRegisterCount(dataType);
                    if (resolvedCount < required) {
                        *error = schemaError(
                            bindingPath + QStringLiteral(".count"),
                            QStringLiteral("数据类型 %1 需要 %2 个寄存器，count 只给了 %3")
                                .arg(dataTypeToString(dataType))
                                .arg(required)
                                .arg(resolvedCount));
                        return false;
                    }
                }

                if (address.toDouble() + resolvedCount > 65536) {
                    *error = schemaError(bindingPath + QStringLiteral(".address"),
                                         QStringLiteral("地址越界：%1 + %2 超过 65535")
                                             .arg(address.toDouble())
                                             .arg(resolvedCount));
                    return false;
                }
            }
        }

        return true;
    }

    bool checkFaults(Error *error)
    {
        const QJsonValue value = m_root.value(QStringLiteral("faults"));
        if (value.isUndefined())
            return true;
        if (!value.isArray()) {
            *error = schemaError(QStringLiteral("faults"), QStringLiteral("faults 必须是数组"));
            return false;
        }

        const QJsonArray faults = value.toArray();
        for (int i = 0; i < faults.size(); ++i) {
            const QString path = QStringLiteral("faults[%1]").arg(i);
            if (!faults.at(i).isObject()) {
                *error = schemaError(path, QStringLiteral("故障项必须是对象"));
                return false;
            }
            const QJsonObject fault = faults.at(i).toObject();
            for (const QString &field :
                 {QStringLiteral("id"), QStringLiteral("target"), QStringLiteral("type")}) {
                if (!isNonEmptyString(fault.value(field))) {
                    *error = schemaError(path + QLatin1Char('.') + field,
                                         QStringLiteral("%1 必填").arg(field));
                    return false;
                }
            }
            const QString type = fault.value(QStringLiteral("type")).toString();
            if (!faultTypes().contains(type)) {
                *error = schemaError(path + QStringLiteral(".type"),
                                     QStringLiteral("未知故障类型 \"%1\"").arg(type));
                return false;
            }
            const QJsonValue durationMs = fault.value(QStringLiteral("durationMs"));
            if (!durationMs.isUndefined()
                && (!isInteger(durationMs) || durationMs.toDouble() < 1)) {
                *error = schemaError(path + QStringLiteral(".durationMs"),
                                     QStringLiteral("durationMs 必须是正整数"));
                return false;
            }
        }

        return true;
    }

    bool checkControlApi(Error *error)
    {
        const QJsonValue value = m_root.value(QStringLiteral("controlApi"));
        if (value.isUndefined())
            return true;
        if (!value.isObject()) {
            *error = schemaError(QStringLiteral("controlApi"),
                                 QStringLiteral("controlApi 必须是对象"));
            return false;
        }
        const QJsonObject object = value.toObject();
        if (!object.value(QStringLiteral("enabled")).isBool()) {
            *error = schemaError(QStringLiteral("controlApi.enabled"),
                                 QStringLiteral("controlApi.enabled 必填且为布尔值"));
            return false;
        }
        return true;
    }

    bool checkPathEscapes(Error *error)
    {
        QString resolved;
        const QJsonArray signals = m_root.value(QStringLiteral("signals")).toArray();
        for (int i = 0; i < signals.size(); ++i) {
            const QJsonObject behavior =
                signals.at(i).toObject().value(QStringLiteral("behavior")).toObject();
            const QString path = QStringLiteral("signals[%1].behavior").arg(i);

            const QString csvPath = behavior.value(QStringLiteral("csvPath")).toString();
            if (!csvPath.isEmpty() && pathEscapesBundle(m_baseDir, csvPath, &resolved)) {
                *error = Error::make(ErrorCode::PathTraversal,
                                     QStringLiteral("配置包内路径越界，拒绝加载"),
                                     path + QStringLiteral(".csvPath = ") + csvPath);
                return false;
            }

            const QString script = behavior.value(QStringLiteral("script")).toString();
            if (!script.isEmpty() && !looksLikeScriptSource(script)
                && pathEscapesBundle(m_baseDir, script, &resolved)) {
                *error = Error::make(ErrorCode::PathTraversal,
                                     QStringLiteral("配置包内路径越界，拒绝加载"),
                                     path + QStringLiteral(".script = ") + script);
                return false;
            }
        }

        const QJsonArray endpoints = m_root.value(QStringLiteral("endpoints")).toArray();
        for (int i = 0; i < endpoints.size(); ++i) {
            const QString scl =
                endpoints.at(i).toObject().value(QStringLiteral("sclPath")).toString();
            if (scl.isEmpty())
                continue;
            if (pathEscapesBundle(m_baseDir, scl, &resolved)) {
                *error = Error::make(ErrorCode::PathTraversal,
                                     QStringLiteral("配置包内路径越界，拒绝加载"),
                                     QStringLiteral("endpoints[%1].sclPath = %2").arg(i).arg(scl));
                return false;
            }
        }

        return true;
    }

    bool checkAddressOverlap(Error *error)
    {
        struct Range
        {
            int start;
            int end;
            QString signalId;
        };

        QHash<QString, QVector<Range>> occupied;
        const QJsonArray signals = m_root.value(QStringLiteral("signals")).toArray();
        for (int i = 0; i < signals.size(); ++i) {
            const QJsonObject signal = signals.at(i).toObject();
            const QString signalId = signal.value(QStringLiteral("id")).toString();

            DataType dataType = DataType::Uint16;
            dataTypeFromString(signal.value(QStringLiteral("dataType")).toString(), &dataType);

            const QJsonArray bindings = signal.value(QStringLiteral("bindings")).toArray();
            for (const QJsonValue &bindingValue : bindings) {
                const QJsonObject binding = bindingValue.toObject();
                if (binding.value(QStringLiteral("protocol")).toString()
                    != QLatin1String("modbus")) {
                    continue;
                }

                const int address = static_cast<int>(binding.value(QStringLiteral("address")).toDouble());
                int count = 1;
                if (binding.value(QStringLiteral("count")).isDouble())
                    count = static_cast<int>(binding.value(QStringLiteral("count")).toDouble());
                else if (dataType != DataType::Ascii)
                    count = dataTypeRegisterCount(dataType);

                const QString key = QStringLiteral("modbus|%1|%2")
                                        .arg(binding.value(QStringLiteral("unitId")).toInt())
                                        .arg(binding.value(QStringLiteral("registerType")).toString());
                occupied[key].append({address, address + count, signalId});
            }
        }

        for (auto it = occupied.constBegin(); it != occupied.constEnd(); ++it) {
            QVector<Range> ranges = it.value();
            std::sort(ranges.begin(), ranges.end(), [](const Range &lhs, const Range &rhs) {
                return lhs.start < rhs.start;
            });
            for (int i = 1; i < ranges.size(); ++i) {
                if (ranges.at(i).start < ranges.at(i - 1).end) {
                    *error = semanticError(
                        QStringLiteral("signals"),
                        QStringLiteral("地址重叠：信号 \"%1\" 与 \"%2\" 在 %3 上重叠")
                            .arg(ranges.at(i - 1).signalId, ranges.at(i).signalId, it.key()));
                    return false;
                }
            }
        }

        return true;
    }

    const QJsonObject &m_root;
    QString m_baseDir;
    QSet<QString> m_deviceIds;
    QSet<int> m_endpointKinds;
};

} // namespace

Result<ConfigBundle> ConfigBundle::loadFromFile(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        return Result<ConfigBundle>::failure(ErrorCode::NotFound,
                                             QStringLiteral("配置包不存在"),
                                             info.absoluteFilePath());
    }

    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return Result<ConfigBundle>::failure(ErrorCode::Io,
                                             QStringLiteral("配置包打不开"),
                                             file.errorString());
    }

    const QByteArray payload = file.readAll();
    file.close();

    return fromJson(payload, info.absolutePath(), info.absoluteFilePath());
}

Result<ConfigBundle> ConfigBundle::fromJson(const QByteArray &json,
                                            const QString &baseDir,
                                            const QString &sourcePath)
{
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return Result<ConfigBundle>::failure(
            ErrorCode::Parse,
            QStringLiteral("JSON 语法错误"),
            QStringLiteral("%1（offset %2）").arg(parseError.errorString()).arg(parseError.offset));
    }
    if (!document.isObject()) {
        return Result<ConfigBundle>::failure(ErrorCode::Schema,
                                             QStringLiteral("配置包根节点必须是 JSON 对象"));
    }

    return build(document.object(), baseDir, sourcePath);
}

Result<ConfigBundle> ConfigBundle::build(const QJsonObject &root,
                                         const QString &baseDir,
                                         const QString &sourcePath)
{
    const Result<void> validation = validate(root, baseDir);
    if (!validation.ok())
        return Result<ConfigBundle>::failure(validation.error());

    ConfigBundle bundle;
    bundle.schemaVersion = root.value(QStringLiteral("schemaVersion")).toString();
    bundle.sourcePath = sourcePath;

    const QJsonObject meta = root.value(QStringLiteral("meta")).toObject();
    bundle.meta.name = meta.value(QStringLiteral("name")).toString();
    bundle.meta.description = meta.value(QStringLiteral("description")).toString();
    bundle.meta.seed = static_cast<qint64>(meta.value(QStringLiteral("seed")).toDouble());
    bundle.meta.tickMs = meta.value(QStringLiteral("tickMs")).toInt(100);
    bundle.meta.simulationClock =
        meta.value(QStringLiteral("timeSource")).toString() != QLatin1String("system");

    const QJsonArray devices = root.value(QStringLiteral("devices")).toArray();
    for (const QJsonValue &value : devices) {
        const QJsonObject object = value.toObject();
        Device device;
        device.id = object.value(QStringLiteral("id")).toString();
        device.name = object.value(QStringLiteral("name")).toString();
        device.description = object.value(QStringLiteral("description")).toString();
        device.type = object.value(QStringLiteral("type")).toString();
        bundle.devices.append(device);
    }

    const QJsonArray signals = root.value(QStringLiteral("signals")).toArray();
    for (const QJsonValue &value : signals) {
        const QJsonObject object = value.toObject();

        Signal signal;
        signal.id = object.value(QStringLiteral("id")).toString();
        signal.name = object.value(QStringLiteral("name")).toString();
        signal.deviceId = object.value(QStringLiteral("deviceId")).toString();
        signal.description = object.value(QStringLiteral("description")).toString();
        signal.category = object.value(QStringLiteral("category")).toString();
        signal.unit = object.value(QStringLiteral("unit")).toString();
        dataTypeFromString(object.value(QStringLiteral("dataType")).toString(), &signal.dataType);
        byteOrderFromString(object.value(QStringLiteral("byteOrder")).toString(),
                            &signal.byteOrder);
        signal.scale = object.value(QStringLiteral("scale")).toDouble(1.0);
        signal.offset = object.value(QStringLiteral("offset")).toDouble(0.0);
        signal.behavior = object.value(QStringLiteral("behavior")).toObject();
        signal.reserved61850 = object.value(QStringLiteral("reserved61850")).toObject();
        signal.initialQuality = qualityFromString(
            object.value(QStringLiteral("quality")).toString(), Quality::Good);

        const QJsonValue range = object.value(QStringLiteral("range"));
        if (range.isObject()) {
            const QJsonObject rangeObject = range.toObject();
            signal.range.min = rangeObject.value(QStringLiteral("min")).toDouble();
            signal.range.max = rangeObject.value(QStringLiteral("max")).toDouble();
            signal.range.valid = true;
        }

        const QJsonArray bindings = object.value(QStringLiteral("bindings")).toArray();
        for (const QJsonValue &bindingValue : bindings) {
            const QJsonObject bindingObject = bindingValue.toObject();

            ProtocolBinding binding;
            protocolKindFromString(bindingObject.value(QStringLiteral("protocol")).toString(),
                                   &binding.protocol);
            binding.unitId = bindingObject.value(QStringLiteral("unitId")).toInt(1);
            binding.registerType = bindingObject.value(QStringLiteral("registerType")).toString();
            binding.address = bindingObject.value(QStringLiteral("address")).toInt(0);
            binding.count = bindingObject.value(QStringLiteral("count")).toInt(
                signal.dataType == DataType::Ascii ? 1 : dataTypeRegisterCount(signal.dataType));
            binding.raw = bindingObject;
            signal.bindings.append(binding);
        }

        bundle.signals.append(signal);
    }

    const QJsonArray endpoints = root.value(QStringLiteral("endpoints")).toArray();
    for (const QJsonValue &value : endpoints) {
        const QJsonObject object = value.toObject();

        EndpointInfo endpoint;
        protocolKindFromString(object.value(QStringLiteral("protocol")).toString(),
                               &endpoint.protocol);
        endpoint.transport = object.value(QStringLiteral("transport")).toString();
        if (object.contains(QStringLiteral("bindAddress")))
            endpoint.bindAddress = object.value(QStringLiteral("bindAddress")).toString();
        endpoint.port = object.value(QStringLiteral("port")).toInt(0);
        endpoint.maxConnections = object.value(QStringLiteral("maxConnections")).toInt(8);
        for (const QJsonValue &unitId : object.value(QStringLiteral("unitIds")).toArray())
            endpoint.unitIds.append(unitId.toInt());
        endpoint.raw = object;
        bundle.endpoints.append(endpoint);
    }

    const QJsonArray faults = root.value(QStringLiteral("faults")).toArray();
    for (const QJsonValue &value : faults) {
        const QJsonObject object = value.toObject();
        FaultDef fault;
        fault.id = object.value(QStringLiteral("id")).toString();
        fault.target = object.value(QStringLiteral("target")).toString();
        fault.type = object.value(QStringLiteral("type")).toString();
        fault.params = object.value(QStringLiteral("params")).toObject();
        fault.enabled = object.value(QStringLiteral("enabled")).toBool(false);
        fault.durationMs = object.value(QStringLiteral("durationMs")).toInt(0);
        bundle.faults.append(fault);
    }

    bundle.controlApi = root.value(QStringLiteral("controlApi")).toObject();

    return bundle;
}

Result<void> ConfigBundle::validate(const QJsonObject &root, const QString &baseDir)
{
    Validator validator(root, baseDir);
    return validator.run();
}

Result<void> ConfigBundle::populate(DataBank *bank) const
{
    if (bank == nullptr)
        return Result<void>::failure(ErrorCode::InvalidArgument, QStringLiteral("DataBank 为空"));

    for (const Signal &signal : signals) {
        if (!bank->registerSignal(signal)) {
            return Result<void>::failure(
                ErrorCode::Semantic,
                QStringLiteral("信号注册失败，id 重复或无效"),
                signal.id);
        }
    }

    return {};
}

QJsonObject ConfigBundle::toJson() const
{
    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), schemaVersion);

    QJsonObject metaObject;
    metaObject.insert(QStringLiteral("name"), meta.name);
    if (!meta.description.isEmpty())
        metaObject.insert(QStringLiteral("description"), meta.description);
    metaObject.insert(QStringLiteral("seed"), meta.seed);
    metaObject.insert(QStringLiteral("tickMs"), meta.tickMs);
    metaObject.insert(QStringLiteral("timeSource"),
                      meta.simulationClock ? QStringLiteral("simulation")
                                           : QStringLiteral("system"));
    root.insert(QStringLiteral("meta"), metaObject);

    QJsonArray deviceArray;
    for (const Device &device : devices) {
        QJsonObject object;
        object.insert(QStringLiteral("id"), device.id);
        object.insert(QStringLiteral("name"), device.name);
        if (!device.description.isEmpty())
            object.insert(QStringLiteral("description"), device.description);
        if (!device.type.isEmpty())
            object.insert(QStringLiteral("type"), device.type);
        deviceArray.append(object);
    }
    root.insert(QStringLiteral("devices"), deviceArray);

    QJsonArray signalArray;
    for (const Signal &signal : signals) {
        QJsonObject object;
        object.insert(QStringLiteral("id"), signal.id);
        object.insert(QStringLiteral("name"), signal.name);
        object.insert(QStringLiteral("deviceId"), signal.deviceId);
        if (!signal.description.isEmpty())
            object.insert(QStringLiteral("description"), signal.description);
        if (!signal.category.isEmpty())
            object.insert(QStringLiteral("category"), signal.category);
        object.insert(QStringLiteral("dataType"), dataTypeToString(signal.dataType));
        object.insert(QStringLiteral("byteOrder"), byteOrderToString(signal.byteOrder));
        object.insert(QStringLiteral("scale"), signal.scale);
        object.insert(QStringLiteral("offset"), signal.offset);
        if (!signal.unit.isEmpty())
            object.insert(QStringLiteral("unit"), signal.unit);
        if (signal.range.valid) {
            QJsonObject range;
            range.insert(QStringLiteral("min"), signal.range.min);
            range.insert(QStringLiteral("max"), signal.range.max);
            object.insert(QStringLiteral("range"), range);
        }
        object.insert(QStringLiteral("quality"), qualityToString(signal.initialQuality));

        QJsonArray bindingArray;
        for (const ProtocolBinding &binding : signal.bindings)
            bindingArray.append(binding.raw);
        object.insert(QStringLiteral("bindings"), bindingArray);
        object.insert(QStringLiteral("behavior"), signal.behavior);
        if (!signal.reserved61850.isEmpty())
            object.insert(QStringLiteral("reserved61850"), signal.reserved61850);

        signalArray.append(object);
    }
    root.insert(QStringLiteral("signals"), signalArray);

    QJsonArray endpointArray;
    for (const EndpointInfo &endpoint : endpoints)
        endpointArray.append(endpoint.raw);
    root.insert(QStringLiteral("endpoints"), endpointArray);

    if (!faults.isEmpty()) {
        QJsonArray faultArray;
        for (const FaultDef &fault : faults) {
            QJsonObject object;
            object.insert(QStringLiteral("id"), fault.id);
            object.insert(QStringLiteral("target"), fault.target);
            object.insert(QStringLiteral("type"), fault.type);
            object.insert(QStringLiteral("params"), fault.params);
            object.insert(QStringLiteral("enabled"), fault.enabled);
            if (fault.durationMs > 0)
                object.insert(QStringLiteral("durationMs"), fault.durationMs);
            faultArray.append(object);
        }
        root.insert(QStringLiteral("faults"), faultArray);
    }

    if (!controlApi.isEmpty())
        root.insert(QStringLiteral("controlApi"), controlApi);

    return root;
}

Result<void> ConfigBundle::saveToFile(const QString &path) const
{
    const QJsonObject root = toJson();

    // 落盘前再校验一遍：不允许把一份坏配置写出去
    const Result<void> validation = validate(root, QFileInfo(path).absolutePath());
    if (!validation.ok())
        return validation;

    const QFileInfo info(path);
    const QString absolutePath = info.absoluteFilePath();

    // 覆盖前备份（AC-P0-05）
    if (info.exists()) {
        const QString backupPath = absolutePath + QStringLiteral(".bak");
        QFile::remove(backupPath);
        if (!QFile::copy(absolutePath, backupPath)) {
            return Result<void>::failure(ErrorCode::Backup,
                                         QStringLiteral("覆盖前备份失败，已中止保存"),
                                         backupPath);
        }
    }

    QSaveFile file(absolutePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return Result<void>::failure(ErrorCode::Io,
                                     QStringLiteral("配置包写入失败"),
                                     file.errorString());
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        return Result<void>::failure(ErrorCode::Io,
                                     QStringLiteral("配置包提交失败"),
                                     file.errorString());
    }

    return {};
}

const Signal *ConfigBundle::signalById(const QString &signalId) const
{
    for (const Signal &signal : signals) {
        if (signal.id == signalId)
            return &signal;
    }
    return nullptr;
}

const Device *ConfigBundle::deviceById(const QString &deviceId) const
{
    for (const Device &device : devices) {
        if (device.id == deviceId)
            return &device;
    }
    return nullptr;
}

const EndpointInfo *ConfigBundle::endpointFor(ProtocolKind kind) const
{
    for (const EndpointInfo &endpoint : endpoints) {
        if (endpoint.protocol == kind)
            return &endpoint;
    }
    return nullptr;
}

QVector<Signal> ConfigBundle::signalsOfDevice(const QString &deviceId) const
{
    QVector<Signal> out;
    for (const Signal &signal : signals) {
        if (signal.deviceId == deviceId)
            out.append(signal);
    }
    return out;
}

bool ConfigBundle::controlApiEnabled() const
{
    return controlApi.value(QStringLiteral("enabled")).toBool(false);
}

} // namespace protosim
