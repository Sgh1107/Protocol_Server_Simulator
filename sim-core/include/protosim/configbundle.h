// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "protosim/databank.h"
#include "protosim/protocolkind.h"
#include "protosim/result.h"
#include "protosim/signal.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace protosim {

struct Device
{
    QString id;
    QString name;
    QString description;
    QString type;
};

struct ScenarioMeta
{
    QString name;
    QString description;
    qint64 seed = 0;
    int tickMs = 100;
    bool simulationClock = true; // timeSource=simulation
};

struct EndpointInfo
{
    ProtocolKind protocol = ProtocolKind::Modbus;
    QString transport; // tcp / rtu（Modbus）；其余协议留空
    QString bindAddress = QStringLiteral("127.0.0.1");
    int port = 0;
    int maxConnections = 8;
    QVector<int> unitIds;
    QJsonObject raw;
};

struct FaultDef
{
    QString id;
    QString target;
    QString type;
    QJsonObject params;
    bool enabled = false;
    int durationMs = 0;
};

/// 一个场景配置包。P0 只用到 meta / devices / signals / endpoints，
/// faults 与 controlApi 原样保留，留给 P1 的故障注入与 HTTP 控制 API。
class ConfigBundle
{
public:
    ConfigBundle() = default;

    /// 从文件加载并做结构 + 语义校验。相对路径一律相对配置包所在目录解析。
    static Result<ConfigBundle> loadFromFile(const QString &path);

    /// 纯字符串入口，便于单测和"从剪贴板导入"。
    static Result<ConfigBundle> fromJson(const QByteArray &json,
                                        const QString &baseDir,
                                        const QString &sourcePath = QString());

    /// 保存；目标已存在时先写 `<path>.bak`（AC-P0-05）。保存前会重新校验一遍。
    Result<void> saveToFile(const QString &path) const;

    /// 只做校验不构建对象，供 UI「导入前预检」用。
    static Result<void> validate(const QJsonObject &root, const QString &baseDir);

    /// 把点表灌进 DataBank。重复 id 视为错误，不做静默覆盖。
    Result<void> populate(DataBank *bank) const;

    QJsonObject toJson() const;

    // ---- 数据 ----

    QString schemaVersion;
    QString sourcePath;
    ScenarioMeta meta;
    QVector<Device> devices;
    QVector<Signal> signals;
    QVector<EndpointInfo> endpoints;
    QVector<FaultDef> faults;
    QJsonObject controlApi; // 原样保留，P1 解释

    // ---- 查询 ----

    const Signal *signalById(const QString &signalId) const;
    const Device *deviceById(const QString &deviceId) const;
    const EndpointInfo *endpointFor(ProtocolKind kind) const;
    QVector<Signal> signalsOfDevice(const QString &deviceId) const;
    bool controlApiEnabled() const;

private:
    static Result<ConfigBundle> build(const QJsonObject &root,
                                      const QString &baseDir,
                                      const QString &sourcePath);
};

} // namespace protosim
