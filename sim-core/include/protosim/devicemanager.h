// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "protosim/configbundle.h"
#include "protosim/result.h"

#include <QString>

namespace protosim {

/// 新增虚拟设备的输入。P1 第一阶段只做 Modbus TCP，RTU 等协议沿用 config/schema 预留。
struct VirtualDeviceSpec
{
    QString id;
    QString name;
    QString description;
    QString type = QStringLiteral("PLC");
    QString bindAddress = QStringLiteral("127.0.0.1");
    int port = 1502;
    int unitId = 1;
    bool enabled = true;
    QString signalName = QStringLiteral("保持寄存器 0");
};

/// 纯逻辑的设备/端点管理。只操作 ConfigBundle，不碰 socket、不碰 GUI。
///
/// 每个写操作都先在副本上改完并整体 validate，成功后才替换调用方传入的 bundle。
/// 这样 UI 弹出"操作失败"时，内存里的配置不会被改坏。
class DeviceManager
{
public:
    static QString makeDeviceId(const QString &name);
    static QString makeEndpointId(const QString &deviceId);

    /// 找一个 1~247 之间未被任何端点占用的 unitId；找不到返回 -1。
    static int nextFreeUnitId(const ConfigBundle &bundle);

    static Result<void> addVirtualDevice(ConfigBundle *bundle, const VirtualDeviceSpec &spec);
    static Result<void> removeVirtualDevice(ConfigBundle *bundle, const QString &deviceId);

    /// 同步切换 device.enabled 与对应 endpoint.enabled。
    static Result<void> setDeviceEnabled(ConfigBundle *bundle,
                                         const QString &deviceId,
                                         bool enabled);

    /// 修改设备专属端点的监听地址、端口、unitId，并同步该设备点位的绑定。
    static Result<void> setEndpoint(ConfigBundle *bundle,
                                    const QString &deviceId,
                                    const QString &bindAddress,
                                    int port,
                                    int unitId);
};

} // namespace protosim
