// SPDX-License-Identifier: GPL-3.0-or-later
//
// AC-P0-04：fieldlink_preset.json 通过校验，3 个信号 / 1 个端点 / 1 个设备全部装载。
// AC-P0-05：缺 schemaVersion 被拒；路径穿越被拒；覆盖前生成备份。

#include "protosim/configbundle.h"
#include "protosim/databank.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
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

QJsonObject presetJson()
{
    QFile file(presetPath());
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

QByteArray toPayload(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Indented);
}

} // namespace

class TestConfigBundle : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void presetLoadsFully()
    {
        const auto result = ConfigBundle::loadFromFile(presetPath());
        QVERIFY2(result.ok(), qPrintable(result.error().toString()));

        const ConfigBundle &bundle = result.value();
        QCOMPARE(bundle.schemaVersion, QStringLiteral("1.0"));
        QCOMPARE(bundle.meta.seed, qint64(20261111));
        QCOMPARE(bundle.meta.tickMs, 100);
        QVERIFY(bundle.meta.simulationClock);

        QCOMPARE(bundle.devices.size(), 1);
        QCOMPARE(bundle.devices.first().id, QStringLiteral("dev-01"));
        QCOMPARE(bundle.signals.size(), 3);
        QCOMPARE(bundle.endpoints.size(), 1);

        const EndpointInfo &endpoint = bundle.endpoints.first();
        QCOMPARE(endpoint.protocol, ProtocolKind::Modbus);
        QCOMPARE(endpoint.transport, QStringLiteral("tcp"));
        QCOMPARE(endpoint.port, 1502);
        QCOMPARE(endpoint.maxConnections, 8);
        QCOMPARE(endpoint.unitIds, QVector<int>({1}));

        const Signal *temperature = bundle.signalById(QStringLiteral("temperature"));
        QVERIFY(temperature != nullptr);
        QCOMPARE(temperature->dataType, DataType::Float32);
        QCOMPARE(temperature->registerCount(), 2);
        QCOMPARE(temperature->unit, QStringLiteral("°C"));
        QCOMPARE(temperature->bindings.size(), 1);
        QCOMPARE(temperature->bindings.first().address, 0);
        QCOMPARE(temperature->behavior.value(QStringLiteral("type")).toString(),
                 QStringLiteral("sine"));
    }

    void presetPopulatesDataBank()
    {
        const auto result = ConfigBundle::loadFromFile(presetPath());
        QVERIFY(result.ok());

        DataBank bank;
        const auto populateResult = result.value().populate(&bank);
        QVERIFY2(populateResult.ok(), qPrintable(populateResult.error().toString()));
        QCOMPARE(bank.count(), 3);
        QVERIFY(bank.contains(QStringLiteral("temperature")));
        QVERIFY(bank.contains(QStringLiteral("pressure")));
        QVERIFY(bank.contains(QStringLiteral("magic")));
    }

    void missingSchemaVersionIsRejected()
    {
        QJsonObject root = presetJson();
        QVERIFY(!root.isEmpty());
        root.remove(QStringLiteral("schemaVersion"));

        const auto result = ConfigBundle::fromJson(toPayload(root), QDir::currentPath());
        QVERIFY(!result.ok());
        QCOMPARE(result.error().code, ErrorCode::Schema);
        QVERIFY2(result.error().toString().contains(QStringLiteral("schemaVersion")),
                 qPrintable(result.error().toString()));
    }

    void malformedSchemaVersionIsRejected()
    {
        QJsonObject root = presetJson();
        root.insert(QStringLiteral("schemaVersion"), QStringLiteral("v1"));

        const auto result = ConfigBundle::fromJson(toPayload(root), QDir::currentPath());
        QVERIFY(!result.ok());
        QCOMPARE(result.error().code, ErrorCode::Schema);
    }

    void futureMajorVersionIsRejected()
    {
        QJsonObject root = presetJson();
        root.insert(QStringLiteral("schemaVersion"), QStringLiteral("2.0"));

        const auto result = ConfigBundle::fromJson(toPayload(root), QDir::currentPath());
        QVERIFY(!result.ok());
        QCOMPARE(result.error().code, ErrorCode::Unsupported);
    }

    void pathTraversalIsRejected_data()
    {
        QTest::addColumn<QString>("badPath");
        QTest::newRow("相对越界") << QStringLiteral("../../evil.json");
        QTest::newRow("双层越界") << QStringLiteral("data/../../evil.csv");
        QTest::newRow("绝对路径") << QStringLiteral("C:/Windows/System32/drivers/etc/hosts");
    }

    void pathTraversalIsRejected()
    {
        QFETCH(QString, badPath);

        QJsonObject root = presetJson();
        QJsonArray signals = root.value(QStringLiteral("signals")).toArray();
        QJsonObject first = signals.at(0).toObject();
        QJsonObject behavior = first.value(QStringLiteral("behavior")).toObject();
        behavior.insert(QStringLiteral("csvPath"), badPath);
        first.insert(QStringLiteral("behavior"), behavior);
        signals.replace(0, first);
        root.insert(QStringLiteral("signals"), signals);

        const QString baseDir = QFileInfo(presetPath()).absolutePath();
        const auto result = ConfigBundle::fromJson(toPayload(root), baseDir);
        QVERIFY2(!result.ok(), "越界路径必须被拒绝");
        QCOMPARE(result.error().code, ErrorCode::PathTraversal);
    }

    void saveCreatesBackupBeforeOverwrite()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        const QString target = tempDir.filePath(QStringLiteral("scenario.json"));
        {
            QFile file(target);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(toPayload(presetJson()));
        }
        const QByteArray original = [&target] {
            QFile file(target);
            file.open(QIODevice::ReadOnly);
            return file.readAll();
        }();

        const auto loaded = ConfigBundle::loadFromFile(target);
        QVERIFY(loaded.ok());

        const auto saved = loaded.value().saveToFile(target);
        QVERIFY2(saved.ok(), qPrintable(saved.error().toString()));

        const QString backup = target + QStringLiteral(".bak");
        QVERIFY2(QFile::exists(backup), "覆盖前必须留下 .bak");

        QFile backupFile(backup);
        QVERIFY(backupFile.open(QIODevice::ReadOnly));
        QVERIFY(!backupFile.readAll().isEmpty());
        QVERIFY(backupFile.size() > 0);

        // 备份内容应当是覆盖前的原文，不是新写的内容
        QCOMPARE(QJsonDocument::fromJson(original).object().value(QStringLiteral("meta")).toObject()
                     .value(QStringLiteral("seed"))
                     .toInt(),
                 presetJson().value(QStringLiteral("meta")).toObject()
                     .value(QStringLiteral("seed"))
                     .toInt());
    }

    void saveFirstTimeDoesNotCreateBackup()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        const QString target = tempDir.filePath(QStringLiteral("fresh.json"));
        const auto loaded = ConfigBundle::loadFromFile(presetPath());
        QVERIFY(loaded.ok());

        const auto saved = loaded.value().saveToFile(target);
        QVERIFY2(saved.ok(), qPrintable(saved.error().toString()));
        QVERIFY(QFile::exists(target));
        QVERIFY(!QFile::exists(target + QStringLiteral(".bak")));
    }

    void roundTripPreservesContent()
    {
        const auto loaded = ConfigBundle::loadFromFile(presetPath());
        QVERIFY(loaded.ok());

        const auto reloaded =
            ConfigBundle::fromJson(toPayload(loaded.value().toJson()), QFileInfo(presetPath()).absolutePath());
        QVERIFY2(reloaded.ok(), qPrintable(reloaded.error().toString()));

        QCOMPARE(reloaded.value().devices.size(), loaded.value().devices.size());
        QCOMPARE(reloaded.value().signals.size(), loaded.value().signals.size());
        QCOMPARE(reloaded.value().endpoints.size(), loaded.value().endpoints.size());

        const Signal *before = loaded.value().signalById(QStringLiteral("temperature"));
        const Signal *after = reloaded.value().signalById(QStringLiteral("temperature"));
        QVERIFY(before != nullptr && after != nullptr);
        QCOMPARE(after->dataType, before->dataType);
        QCOMPARE(after->byteOrder, before->byteOrder);
        QCOMPARE(after->scale, before->scale);
        QCOMPARE(after->offset, before->offset);
        QCOMPARE(after->unit, before->unit);
        QCOMPARE(after->range.min, before->range.min);
        QCOMPARE(after->range.max, before->range.max);
        QCOMPARE(after->behavior, before->behavior);
        QCOMPARE(after->bindings.size(), before->bindings.size());
    }

    void unknownDeviceIsRejected()
    {
        QJsonObject root = presetJson();
        QJsonArray signals = root.value(QStringLiteral("signals")).toArray();
        QJsonObject first = signals.at(0).toObject();
        first.insert(QStringLiteral("deviceId"), QStringLiteral("dev-404"));
        signals.replace(0, first);
        root.insert(QStringLiteral("signals"), signals);

        const auto result = ConfigBundle::fromJson(toPayload(root), QDir::currentPath());
        QVERIFY(!result.ok());
        QCOMPARE(result.error().code, ErrorCode::Semantic);
    }

    void overlappingAddressesAreRejected()
    {
        QJsonObject root = presetJson();
        QJsonArray signals = root.value(QStringLiteral("signals")).toArray();
        // magic 原先在 HR[3]，改成 HR[1] 就会压到 temperature 的 HR[0..1] 上
        QJsonObject magic = signals.at(2).toObject();
        QJsonArray bindings = magic.value(QStringLiteral("bindings")).toArray();
        QJsonObject binding = bindings.at(0).toObject();
        binding.insert(QStringLiteral("address"), 1);
        bindings.replace(0, binding);
        magic.insert(QStringLiteral("bindings"), bindings);
        signals.replace(2, magic);
        root.insert(QStringLiteral("signals"), signals);

        const auto result = ConfigBundle::fromJson(toPayload(root), QDir::currentPath());
        QVERIFY(!result.ok());
        QCOMPARE(result.error().code, ErrorCode::Semantic);
        QVERIFY2(result.error().toString().contains(QStringLiteral("重叠")),
                 qPrintable(result.error().toString()));
    }

    void bindingWithoutEndpointIsRejected()
    {
        QJsonObject root = presetJson();
        QJsonArray endpoints = root.value(QStringLiteral("endpoints")).toArray();
        QJsonObject endpoint = endpoints.at(0).toObject();
        endpoint.insert(QStringLiteral("protocol"), QStringLiteral("iec104"));
        endpoint.remove(QStringLiteral("transport"));
        endpoints.replace(0, endpoint);
        root.insert(QStringLiteral("endpoints"), endpoints);

        const auto result = ConfigBundle::fromJson(toPayload(root), QDir::currentPath());
        QVERIFY(!result.ok());
        QCOMPARE(result.error().code, ErrorCode::Semantic);
    }

    void countSmallerThanDataTypeIsRejected()
    {
        QJsonObject root = presetJson();
        QJsonArray signals = root.value(QStringLiteral("signals")).toArray();
        QJsonObject temperature = signals.at(0).toObject();
        QJsonArray bindings = temperature.value(QStringLiteral("bindings")).toArray();
        QJsonObject binding = bindings.at(0).toObject();
        binding.insert(QStringLiteral("count"), 1); // float32 至少要 2 个寄存器
        bindings.replace(0, binding);
        temperature.insert(QStringLiteral("bindings"), bindings);
        signals.replace(0, temperature);
        root.insert(QStringLiteral("signals"), signals);

        const auto result = ConfigBundle::fromJson(toPayload(root), QDir::currentPath());
        QVERIFY(!result.ok());
        QCOMPARE(result.error().code, ErrorCode::Schema);
    }

    void brokenJsonReportsParseError()
    {
        const auto result = ConfigBundle::fromJson(QByteArrayLiteral("{ \"meta\": "),
                                                  QDir::currentPath());
        QVERIFY(!result.ok());
        QCOMPARE(result.error().code, ErrorCode::Parse);
    }
};

QTEST_GUILESS_MAIN(TestConfigBundle)
#include "tst_configbundle.moc"
