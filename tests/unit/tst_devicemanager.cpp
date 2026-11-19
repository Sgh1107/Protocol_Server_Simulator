// SPDX-License-Identifier: GPL-3.0-or-later
//
// P1 第一阶段: 虚拟设备 / 端点管理的纯逻辑验收
// 不碰 socket、不碰 GUI，只验证配置模型、持久化与语义约束

#include "protosim/configbundle.h"
#include "protosim/devicemanager.h"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

using namespace protosim;

namespace {

QString presetPath()
{
    return QStringLiteral(PROTOSIM_SOURCE_DIR "/config/examples/fieldlink_preset.json");
}

bool loadPreset(ConfigBundle *bundle)
{
    const auto loaded = ConfigBundle::loadFromFile(presetPath());
    if (!loaded.ok())
        return false;
    *bundle = loaded.value();
    return true;
}

VirtualDeviceSpec secondDeviceSpec()
{
    VirtualDeviceSpec spec;
    spec.id = QStringLiteral("dev-02");
    spec.name = QStringLiteral("备用 PLC");
    spec.description = QStringLiteral("P1 新增设备");
    spec.type = QStringLiteral("PLC");
    spec.bindAddress = QStringLiteral("127.0.0.1");
    spec.port = 1503;
    spec.unitId = 2;
    spec.enabled = true;
    return spec;
}

} // namespace

class TestDeviceManager : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void presetGetsStableEndpointIds()
    {
        ConfigBundle bundle;
        QVERIFY(loadPreset(&bundle));

        QCOMPARE(bundle.devices.size(), 1);
        QCOMPARE(bundle.endpoints.size(), 1);
        QVERIFY(!bundle.endpoints.first().id.isEmpty());
        QVERIFY(bundle.devices.first().enabled);
    }

    void addDeviceCreatesEndpointAndSignal()
    {
        ConfigBundle bundle;
        QVERIFY(loadPreset(&bundle));

        const auto added = DeviceManager::addVirtualDevice(&bundle, secondDeviceSpec());
        QVERIFY2(added.ok(), qPrintable(added.error().toString()));

        QCOMPARE(bundle.devices.size(), 2);
        QCOMPARE(bundle.endpoints.size(), 2);
        QCOMPARE(bundle.signals.size(), 4);

        const Device *device = bundle.deviceById(QStringLiteral("dev-02"));
        QVERIFY(device != nullptr);
        QCOMPARE(device->endpointId, QStringLiteral("ep-dev-02"));
        QVERIFY(device->enabled);

        const EndpointInfo *endpoint = bundle.endpointById(QStringLiteral("ep-dev-02"));
        QVERIFY(endpoint != nullptr);
        QCOMPARE(endpoint->port, 1503);
        QCOMPARE(endpoint->unitIds, QVector<int>({2}));
        QVERIFY(endpoint->enabled);
        QCOMPARE(endpoint->raw.value(QStringLiteral("id")).toString(),
                 QStringLiteral("ep-dev-02"));
        QCOMPARE(endpoint->raw.value(QStringLiteral("port")).toInt(), 1503);

        const Signal *signal = bundle.signalById(QStringLiteral("dev-02-hr0"));
        QVERIFY(signal != nullptr);
        QCOMPARE(signal->deviceId, QStringLiteral("dev-02"));
        QCOMPARE(signal->bindings.size(), 1);
        QCOMPARE(signal->bindings.first().unitId, 2);
        QCOMPARE(signal->bindings.first().address, 0);
        QCOMPARE(signal->bindings.first().registerType, QStringLiteral("HoldingRegisters"));
    }

    void addDeviceRejectsDuplicateIdWithoutMutation()
    {
        ConfigBundle bundle;
        QVERIFY(loadPreset(&bundle));

        VirtualDeviceSpec duplicate = secondDeviceSpec();
        duplicate.id = QStringLiteral("dev-01");

        const auto added = DeviceManager::addVirtualDevice(&bundle, duplicate);
        QVERIFY(!added.ok());
        QCOMPARE(added.error().code, ErrorCode::Semantic);
        QCOMPARE(bundle.devices.size(), 1);
        QCOMPARE(bundle.endpoints.size(), 1);
        QCOMPARE(bundle.signals.size(), 3);
    }

    void addDeviceRejectsPortConflict()
    {
        ConfigBundle bundle;
        QVERIFY(loadPreset(&bundle));

        VirtualDeviceSpec conflict = secondDeviceSpec();
        conflict.port = 1502;

        const auto added = DeviceManager::addVirtualDevice(&bundle, conflict);
        QVERIFY(!added.ok());
        QCOMPARE(added.error().code, ErrorCode::Semantic);
        QCOMPARE(bundle.devices.size(), 1);
    }

    void addDevicePicksFreeUnitId()
    {
        ConfigBundle bundle;
        QVERIFY(loadPreset(&bundle));

        VirtualDeviceSpec spec = secondDeviceSpec();
        spec.unitId = 1;

        const auto added = DeviceManager::addVirtualDevice(&bundle, spec);
        QVERIFY2(added.ok(), qPrintable(added.error().toString()));

        const EndpointInfo *endpoint = bundle.endpointById(QStringLiteral("ep-dev-02"));
        QVERIFY(endpoint != nullptr);
        QCOMPARE(endpoint->unitIds, QVector<int>({2}));
    }

    void removeDeviceRemovesOwnedEndpointAndSignals()
    {
        ConfigBundle bundle;
        QVERIFY(loadPreset(&bundle));
        QVERIFY(DeviceManager::addVirtualDevice(&bundle, secondDeviceSpec()).ok());

        const auto removed = DeviceManager::removeVirtualDevice(&bundle, QStringLiteral("dev-01"));
        QVERIFY2(removed.ok(), qPrintable(removed.error().toString()));

        QCOMPARE(bundle.devices.size(), 1);
        QVERIFY(bundle.deviceById(QStringLiteral("dev-01")) == nullptr);
        QCOMPARE(bundle.endpoints.size(), 1);
        QVERIFY(bundle.endpointById(QStringLiteral("ep-dev-01")) == nullptr);
        QCOMPARE(bundle.signals.size(), 1);
        QVERIFY(bundle.signalById(QStringLiteral("temperature")) == nullptr);
        QVERIFY(bundle.signalById(QStringLiteral("dev-02-hr0")) != nullptr);
    }

    void removeLastDeviceIsRejected()
    {
        ConfigBundle bundle;
        QVERIFY(loadPreset(&bundle));

        const auto removed = DeviceManager::removeVirtualDevice(&bundle, QStringLiteral("dev-01"));
        QVERIFY(!removed.ok());
        QCOMPARE(bundle.devices.size(), 1);
    }

    void setEnabledUpdatesDeviceAndEndpoint()
    {
        ConfigBundle bundle;
        QVERIFY(loadPreset(&bundle));

        const auto disabled =
            DeviceManager::setDeviceEnabled(&bundle, QStringLiteral("dev-01"), false);
        QVERIFY2(disabled.ok(), qPrintable(disabled.error().toString()));
        QVERIFY(!bundle.deviceById(QStringLiteral("dev-01"))->enabled);
        QVERIFY(!bundle.endpoints.first().enabled);
        QVERIFY(!bundle.endpoints.first().raw.value(QStringLiteral("enabled")).toBool(true));

        const auto enabled =
            DeviceManager::setDeviceEnabled(&bundle, QStringLiteral("dev-01"), true);
        QVERIFY2(enabled.ok(), qPrintable(enabled.error().toString()));
        QVERIFY(bundle.deviceById(QStringLiteral("dev-01"))->enabled);
        QVERIFY(bundle.endpoints.first().enabled);
    }

    void setEndpointRejectsConflictAndUpdatesOnSuccess()
    {
        ConfigBundle bundle;
        QVERIFY(loadPreset(&bundle));
        QVERIFY(DeviceManager::addVirtualDevice(&bundle, secondDeviceSpec()).ok());

        VirtualDeviceSpec conflict = secondDeviceSpec();
        conflict.port = 1502;
        const auto bad = DeviceManager::setEndpoint(
            &bundle, QStringLiteral("dev-02"), conflict.bindAddress, conflict.port, 2);
        QVERIFY(!bad.ok());
        QCOMPARE(bundle.endpointById(QStringLiteral("ep-dev-02"))->port, 1503);

        const auto good = DeviceManager::setEndpoint(
            &bundle, QStringLiteral("dev-02"), QStringLiteral("0.0.0.0"), 1603, 7);
        QVERIFY2(good.ok(), qPrintable(good.error().toString()));

        const EndpointInfo *endpoint = bundle.endpointById(QStringLiteral("ep-dev-02"));
        QVERIFY(endpoint != nullptr);
        QCOMPARE(endpoint->bindAddress, QStringLiteral("0.0.0.0"));
        QCOMPARE(endpoint->port, 1603);
        QCOMPARE(endpoint->unitIds, QVector<int>({7}));
        QCOMPARE(endpoint->raw.value(QStringLiteral("port")).toInt(), 1603);

        const Signal *signal = bundle.signalById(QStringLiteral("dev-02-hr0"));
        QVERIFY(signal != nullptr);
        QCOMPARE(signal->bindings.first().unitId, 7);
    }

    void jsonRoundTripKeepsDeviceState()
    {
        ConfigBundle bundle;
        QVERIFY(loadPreset(&bundle));
        QVERIFY(DeviceManager::addVirtualDevice(&bundle, secondDeviceSpec()).ok());
        QVERIFY(DeviceManager::setDeviceEnabled(&bundle, QStringLiteral("dev-02"), false).ok());

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path() + QStringLiteral("/scene.json");

        const auto saved = bundle.saveToFile(path);
        QVERIFY2(saved.ok(), qPrintable(saved.error().toString()));

        const auto reloaded = ConfigBundle::loadFromFile(path);
        QVERIFY2(reloaded.ok(), qPrintable(reloaded.error().toString()));

        const Device *device = reloaded.value().deviceById(QStringLiteral("dev-02"));
        QVERIFY(device != nullptr);
        QCOMPARE(device->endpointId, QStringLiteral("ep-dev-02"));
        QVERIFY(!device->enabled);

        const EndpointInfo *endpoint =
            reloaded.value().endpointById(QStringLiteral("ep-dev-02"));
        QVERIFY(endpoint != nullptr);
        QCOMPARE(endpoint->port, 1503);
        QVERIFY(!endpoint->enabled);
    }

    void jsonSchemaKeepsOldConfigValid()
    {
        ConfigBundle bundle;
        QVERIFY(loadPreset(&bundle));

        const QJsonObject json = bundle.toJson();
        QVERIFY(json.contains(QStringLiteral("devices")));
        QVERIFY(json.contains(QStringLiteral("endpoints")));
        QVERIFY(json.contains(QStringLiteral("signals")));
    }
};

QTEST_GUILESS_MAIN(TestDeviceManager)
#include "tst_devicemanager.moc"
