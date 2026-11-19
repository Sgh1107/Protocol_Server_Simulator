// SPDX-License-Identifier: GPL-3.0-or-later
#include "protosim/devicemanager.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

namespace protosim {
namespace {

QString bundleBaseDir(const ConfigBundle &bundle)
{
    if (!bundle.sourcePath.isEmpty())
        return QFileInfo(bundle.sourcePath).absolutePath();
    return QDir::currentPath();
}

Result<void> commit(ConfigBundle *bundle, ConfigBundle candidate)
{
    const Result<void> checked =
        ConfigBundle::validate(candidate.toJson(), bundleBaseDir(candidate));
    if (!checked.ok())
        return checked;

    *bundle = candidate;
    return {};
}

bool portOccupied(const ConfigBundle &bundle,
                  const QString &bindAddress,
                  int port,
                  const QString &exceptEndpointId)
{
    for (const EndpointInfo &endpoint : bundle.endpoints) {
        if (!endpoint.enabled || endpoint.port != port || endpoint.id == exceptEndpointId)
            continue;
        if (endpoint.bindAddress.compare(bindAddress, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

bool unitIdOccupied(const ConfigBundle &bundle, int unitId, const QString &exceptEndpointId)
{
    for (const EndpointInfo &endpoint : bundle.endpoints) {
        if (endpoint.id == exceptEndpointId)
            continue;
        if (endpoint.unitIds.contains(unitId))
            return true;
    }
    return false;
}

void writeEndpointJson(EndpointInfo *endpoint)
{
    QJsonObject raw = endpoint->raw;
    raw.insert(QStringLiteral("id"), endpoint->id);
    raw.insert(QStringLiteral("enabled"), endpoint->enabled);
    raw.insert(QStringLiteral("protocol"), protocolKindToString(endpoint->protocol));
    raw.insert(QStringLiteral("transport"), endpoint->transport);
    raw.insert(QStringLiteral("bindAddress"), endpoint->bindAddress);
    raw.insert(QStringLiteral("port"), endpoint->port);
    raw.insert(QStringLiteral("maxConnections"), endpoint->maxConnections);

    QJsonArray unitIds;
    for (int unitId : endpoint->unitIds)
        unitIds.append(unitId);
    raw.insert(QStringLiteral("unitIds"), unitIds);

    endpoint->raw = raw;
}

Result<void> badArgument(const QString &message, const QString &detail = QString())
{
    return Result<void>::failure(ErrorCode::InvalidArgument, message, detail);
}

Result<void> badSemantic(const QString &message, const QString &detail = QString())
{
    return Result<void>::failure(ErrorCode::Semantic, message, detail);
}

} // namespace

QString DeviceManager::makeDeviceId(const QString &name)
{
    QString id = name.trimmed().toLower();
    id.replace(QRegularExpression(QStringLiteral("[^a-z0-9_-]+")), QStringLiteral("-"));
    id.remove(QRegularExpression(QStringLiteral("^-+|-+$")));
    return id.isEmpty() ? QStringLiteral("device") : id;
}

QString DeviceManager::makeEndpointId(const QString &deviceId)
{
    return QStringLiteral("ep-%1").arg(deviceId);
}

int DeviceManager::nextFreeUnitId(const ConfigBundle &bundle)
{
    for (int unitId = 1; unitId <= 247; ++unitId) {
        if (!unitIdOccupied(bundle, unitId, QString()))
            return unitId;
    }
    return -1;
}

Result<void> DeviceManager::addVirtualDevice(ConfigBundle *bundle, const VirtualDeviceSpec &spec)
{
    if (bundle == nullptr)
        return badArgument(QStringLiteral("ConfigBundle 为空"));

    if (spec.id.trimmed().isEmpty())
        return badArgument(QStringLiteral("设备 id 不能为空"));
    if (spec.name.trimmed().isEmpty())
        return badArgument(QStringLiteral("设备名称不能为空"));
    if (spec.port < 1 || spec.port > 65535)
        return badArgument(QStringLiteral("端口必须在 1~65535"),
                           QStringLiteral("port=%1").arg(spec.port));
    if (spec.bindAddress.trimmed().isEmpty())
        return badArgument(QStringLiteral("监听地址不能为空"));
    if (spec.unitId < 0 || spec.unitId > 247)
        return badArgument(QStringLiteral("unitId 必须在 0~247"),
                           QStringLiteral("unitId=%1").arg(spec.unitId));

    ConfigBundle candidate = *bundle;
    const QString deviceId = spec.id.trimmed();
    const QString endpointId = makeEndpointId(deviceId);

    if (candidate.deviceById(deviceId) != nullptr)
        return badSemantic(QStringLiteral("设备 id 已存在"), deviceId);
    if (candidate.endpointById(endpointId) != nullptr)
        return badSemantic(QStringLiteral("端点 id 已存在"), endpointId);
    if (portOccupied(candidate, spec.bindAddress.trimmed(), spec.port, QString())) {
        return badSemantic(QStringLiteral("监听端口已被其他启用端点占用"),
                           QStringLiteral("%1:%2")
                               .arg(spec.bindAddress.trimmed())
                               .arg(spec.port));
    }

    int unitId = spec.unitId;
    if (unitId == 0 || unitIdOccupied(candidate, unitId, QString())) {
        unitId = nextFreeUnitId(candidate);
        if (unitId < 0)
            return badSemantic(QStringLiteral("没有可用的 unitId(1~247 已占满)"));
    }

    Device device;
    device.id = deviceId;
    device.name = spec.name.trimmed();
    device.description = spec.description;
    device.type = spec.type.isEmpty() ? QStringLiteral("PLC") : spec.type;
    device.endpointId = endpointId;
    device.enabled = spec.enabled;
    candidate.devices.append(device);

    EndpointInfo endpoint;
    endpoint.id = endpointId;
    endpoint.protocol = ProtocolKind::Modbus;
    endpoint.transport = QStringLiteral("tcp");
    endpoint.bindAddress = spec.bindAddress.trimmed();
    endpoint.port = spec.port;
    endpoint.maxConnections = 8;
    endpoint.unitIds.append(unitId);
    endpoint.enabled = spec.enabled;
    writeEndpointJson(&endpoint);
    candidate.endpoints.append(endpoint);

    QJsonObject behavior;
    behavior.insert(QStringLiteral("type"), QStringLiteral("constant"));
    behavior.insert(QStringLiteral("value"), 0);

    QJsonObject bindingJson;
    bindingJson.insert(QStringLiteral("protocol"), QStringLiteral("modbus"));
    bindingJson.insert(QStringLiteral("unitId"), unitId);
    bindingJson.insert(QStringLiteral("registerType"), QStringLiteral("HoldingRegisters"));
    bindingJson.insert(QStringLiteral("address"), 0);
    bindingJson.insert(QStringLiteral("count"), 1);

    ProtocolBinding binding;
    binding.protocol = ProtocolKind::Modbus;
    binding.unitId = unitId;
    binding.registerType = QStringLiteral("HoldingRegisters");
    binding.address = 0;
    binding.count = 1;
    binding.raw = bindingJson;

    Signal signal;
    signal.id = QStringLiteral("%1-hr0").arg(deviceId);
    signal.name = spec.signalName.isEmpty() ? QStringLiteral("保持寄存器 0") : spec.signalName;
    signal.deviceId = deviceId;
    signal.category = QStringLiteral("AI");
    signal.dataType = DataType::Uint16;
    signal.behavior = behavior;
    signal.bindings.append(binding);
    candidate.signals.append(signal);

    return commit(bundle, candidate);
}

Result<void> DeviceManager::removeVirtualDevice(ConfigBundle *bundle, const QString &deviceId)
{
    if (bundle == nullptr)
        return badArgument(QStringLiteral("ConfigBundle 为空"));

    const Device *device = bundle->deviceById(deviceId);
    if (device == nullptr)
        return Result<void>::failure(ErrorCode::NotFound,
                                     QStringLiteral("设备不存在"),
                                     deviceId);
    if (bundle->devices.size() <= 1)
        return badSemantic(QStringLiteral("至少保留一个设备"));

    ConfigBundle candidate = *bundle;
    const QString endpointId = device->endpointId.isEmpty()
                                   ? (bundle->endpoints.isEmpty() ? QString() : bundle->endpoints.first().id)
                                   : device->endpointId;

    for (int i = candidate.devices.size() - 1; i >= 0; --i) {
        if (candidate.devices.at(i).id == deviceId)
            candidate.devices.removeAt(i);
    }
    for (int i = candidate.signals.size() - 1; i >= 0; --i) {
        if (candidate.signals.at(i).deviceId == deviceId)
            candidate.signals.removeAt(i);
    }

    // 老配置可能多台设备共用一个端点；只有没人再引用时才删端点。
    QSet<QString> referencedEndpoints;
    for (const Device &item : candidate.devices) {
        if (!item.endpointId.isEmpty())
            referencedEndpoints.insert(item.endpointId);
    }
    for (int i = candidate.endpoints.size() - 1; i >= 0; --i) {
        if (!endpointId.isEmpty() && candidate.endpoints.at(i).id == endpointId
            && !referencedEndpoints.contains(endpointId))
            candidate.endpoints.removeAt(i);
    }

    return commit(bundle, candidate);
}

Result<void> DeviceManager::setDeviceEnabled(ConfigBundle *bundle,
                                             const QString &deviceId,
                                             bool enabled)
{
    if (bundle == nullptr)
        return badArgument(QStringLiteral("ConfigBundle 为空"));

    const Device *device = bundle->deviceById(deviceId);
    if (device == nullptr)
        return Result<void>::failure(ErrorCode::NotFound,
                                     QStringLiteral("设备不存在"),
                                     deviceId);

    ConfigBundle candidate = *bundle;
    for (Device &item : candidate.devices) {
        if (item.id == deviceId)
            item.enabled = enabled;
    }

    const QString endpointId = device->endpointId.isEmpty()
                                   ? (bundle->endpoints.isEmpty() ? QString() : bundle->endpoints.first().id)
                                   : device->endpointId;
    bool sharedByEnabledDevice = false;
    for (const Device &item : candidate.devices) {
        if (item.id != deviceId && item.enabled && item.endpointId == endpointId) {
            sharedByEnabledDevice = true;
            break;
        }
    }
    for (EndpointInfo &endpoint : candidate.endpoints) {
        if (endpoint.id == endpointId && !sharedByEnabledDevice) {
            endpoint.enabled = enabled;
            writeEndpointJson(&endpoint);
        }
    }

    return commit(bundle, candidate);
}

Result<void> DeviceManager::setEndpoint(ConfigBundle *bundle,
                                        const QString &deviceId,
                                        const QString &bindAddress,
                                        int port,
                                        int unitId)
{
    if (bundle == nullptr)
        return badArgument(QStringLiteral("ConfigBundle 为空"));
    if (bindAddress.trimmed().isEmpty())
        return badArgument(QStringLiteral("监听地址不能为空"));
    if (port < 1 || port > 65535)
        return badArgument(QStringLiteral("端口必须在 1~65535"),
                           QStringLiteral("port=%1").arg(port));
    if (unitId < 1 || unitId > 247)
        return badArgument(QStringLiteral("unitId 必须在 1~247"),
                           QStringLiteral("unitId=%1").arg(unitId));

    const Device *device = bundle->deviceById(deviceId);
    if (device == nullptr)
        return Result<void>::failure(ErrorCode::NotFound,
                                     QStringLiteral("设备不存在"),
                                     deviceId);

    const QString endpointId = device->endpointId.isEmpty()
                                   ? (bundle->endpoints.isEmpty() ? QString() : bundle->endpoints.first().id)
                                   : device->endpointId;
    if (endpointId.isEmpty())
        return badSemantic(QStringLiteral("设备没有关联端点"), deviceId);

    if (portOccupied(*bundle, bindAddress.trimmed(), port, endpointId)) {
        return badSemantic(QStringLiteral("监听端口已被其他启用端点占用"),
                           QStringLiteral("%1:%2").arg(bindAddress.trimmed()).arg(port));
    }
    if (unitIdOccupied(*bundle, unitId, endpointId)) {
        return badSemantic(QStringLiteral("unitId 已被其他端点占用"),
                           QString::number(unitId));
    }

    ConfigBundle candidate = *bundle;
    for (EndpointInfo &endpoint : candidate.endpoints) {
        if (endpoint.id != endpointId)
            continue;
        endpoint.bindAddress = bindAddress.trimmed();
        endpoint.port = port;
        endpoint.unitIds.clear();
        endpoint.unitIds.append(unitId);
        writeEndpointJson(&endpoint);
    }

    for (Signal &signal : candidate.signals) {
        if (signal.deviceId != deviceId)
            continue;
        for (ProtocolBinding &binding : signal.bindings) {
            if (binding.protocol != ProtocolKind::Modbus)
                continue;
            binding.unitId = unitId;
            QJsonObject raw = binding.raw;
            raw.insert(QStringLiteral("unitId"), unitId);
            binding.raw = raw;
        }
    }

    return commit(bundle, candidate);
}

} // namespace protosim
