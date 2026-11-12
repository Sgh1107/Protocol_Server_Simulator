// SPDX-License-Identifier: GPL-3.0-or-later
//
// DataBank：读写原子、带质量码与时标、批量写要么全成要么全不动。

#include "protosim/databank.h"

#include <QSignalSpy>
#include <QThread>
#include <QtTest>

#include <atomic>

using namespace protosim;

namespace {

Signal makeSignal(const QString &id)
{
    Signal s;
    s.id = id;
    s.name = id;
    s.deviceId = QStringLiteral("dev-01");
    return s;
}

} // namespace

class TestDataBank : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void registerRejectsDuplicateAndInvalid()
    {
        DataBank bank;
        QVERIFY(bank.registerSignal(makeSignal(QStringLiteral("a"))));
        QVERIFY(!bank.registerSignal(makeSignal(QStringLiteral("a"))));
        QCOMPARE(bank.count(), 1);

        Signal nameless;
        nameless.id = QStringLiteral("b");
        QVERIFY(!bank.registerSignal(nameless));
        QCOMPARE(bank.count(), 1);
    }

    void writeUpdatesValueQualityAndTimestamp()
    {
        DataBank bank;
        bank.registerSignal(makeSignal(QStringLiteral("a")));

        QSignalSpy spy(&bank, &DataBank::signalChanged);
        QVERIFY(bank.write(QStringLiteral("a"),
                           42,
                           Quality::Good,
                           1234,
                           false,
                           QStringLiteral("modbus"),
                           QStringLiteral("127.0.0.1:50000")));

        const SignalSnapshot snap = bank.snapshot(QStringLiteral("a"));
        QCOMPARE(snap.value.toInt(), 42);
        QCOMPARE(snap.quality, Quality::Good);
        QCOMPARE(snap.timestampMs, 1234);
        QVERIFY(!snap.forced);

        QCOMPARE(spy.count(), 1);
        const auto change = spy.at(0).at(0).value<SignalChange>();
        QCOMPARE(change.signalId, QStringLiteral("a"));
        QCOMPARE(change.newValue.toInt(), 42);
        QCOMPARE(change.sourceProtocol, QStringLiteral("modbus"));
        QCOMPARE(change.clientId, QStringLiteral("127.0.0.1:50000"));
    }

    void writeUnknownSignalFails()
    {
        DataBank bank;
        QVERIFY(!bank.write(QStringLiteral("nope"), 1, Quality::Good, 1));
        QCOMPARE(bank.count(), 0); // 不隐式建点
    }

    void batchIsAllOrNothing()
    {
        DataBank bank;
        bank.registerSignal(makeSignal(QStringLiteral("a")));
        bank.registerSignal(makeSignal(QStringLiteral("b")));
        bank.write(QStringLiteral("a"), 1, Quality::Good, 10);
        bank.write(QStringLiteral("b"), 2, Quality::Good, 10);

        QVector<SignalUpdate> updates;
        updates.append({QStringLiteral("a"), 99, Quality::Good, false});
        updates.append({QStringLiteral("missing"), 99, Quality::Good, false});

        QSignalSpy spy(&bank, &DataBank::signalChanged);
        QVERIFY(!bank.writeBatch(updates, 20, QStringLiteral("engine")));

        QCOMPARE(bank.snapshot(QStringLiteral("a")).value.toInt(), 1);
        QCOMPARE(bank.snapshot(QStringLiteral("a")).timestampMs, 10);
        QCOMPARE(spy.count(), 0);
    }

    void batchAppliesSingleTimestamp()
    {
        DataBank bank;
        bank.registerSignal(makeSignal(QStringLiteral("a")));
        bank.registerSignal(makeSignal(QStringLiteral("b")));

        QVector<SignalUpdate> updates;
        updates.append({QStringLiteral("a"), 7, Quality::Good, false});
        updates.append({QStringLiteral("b"), 8, Quality::Good, false});
        QVERIFY(bank.writeBatch(updates, 500, QStringLiteral("engine")));

        QCOMPARE(bank.snapshot(QStringLiteral("a")).timestampMs, 500);
        QCOMPARE(bank.snapshot(QStringLiteral("b")).timestampMs, 500);
    }

    void qualityAndForceLifecycle()
    {
        DataBank bank;
        bank.registerSignal(makeSignal(QStringLiteral("a")));
        bank.write(QStringLiteral("a"), 1, Quality::Good, 10);

        // AC-P0-12：手工置 Bad
        QVERIFY(bank.setQuality(QStringLiteral("a"), Quality::Bad, 20));
        QCOMPARE(bank.snapshot(QStringLiteral("a")).quality, Quality::Bad);
        QCOMPARE(bank.snapshot(QStringLiteral("a")).timestampMs, 20);

        // AC-P0-10：强制写带 forced 标记
        QVERIFY(bank.force(QStringLiteral("a"), 23.5, 30, QStringLiteral("modbus"),
                           QStringLiteral("client-1")));
        QCOMPARE(bank.snapshot(QStringLiteral("a")).value.toDouble(), 23.5);
        QVERIFY(bank.snapshot(QStringLiteral("a")).forced);

        QVERIFY(bank.clearForce(QStringLiteral("a"), 40));
        QVERIFY(!bank.snapshot(QStringLiteral("a")).forced);
        QCOMPARE(bank.snapshot(QStringLiteral("a")).value.toDouble(), 23.5);
    }

    void snapshotsKeepRegistrationOrder()
    {
        DataBank bank;
        bank.registerSignal(makeSignal(QStringLiteral("z")));
        bank.registerSignal(makeSignal(QStringLiteral("a")));
        bank.registerSignal(makeSignal(QStringLiteral("m")));

        QCOMPARE(bank.signalIds(), QStringList({"z", "a", "m"}));
        const QVector<SignalSnapshot> all = bank.snapshots();
        QCOMPARE(all.size(), 3);
        QCOMPARE(all.at(0).signalId, QStringLiteral("z"));
    }

    void concurrentWritesNeverTearTheTuple()
    {
        DataBank bank;
        bank.registerSignal(makeSignal(QStringLiteral("x")));

        std::atomic<int> torn{0};
        const int iterations = 20000;

        QThread *positive = QThread::create([&bank, iterations] {
            for (int i = 1; i <= iterations; ++i)
                bank.write(QStringLiteral("x"), i, Quality::Good, i);
        });
        QThread *negative = QThread::create([&bank, iterations] {
            for (int i = 1; i <= iterations; ++i)
                bank.write(QStringLiteral("x"), -i, Quality::Bad, i);
        });

        positive->start();
        negative->start();

        for (int n = 0; n < iterations * 2; ++n) {
            const SignalSnapshot snap = bank.snapshot(QStringLiteral("x"));
            const int value = snap.value.toInt();
            const bool consistent = (value >= 0 && snap.quality == Quality::Good)
                || (value < 0 && snap.quality == Quality::Bad);
            if (!consistent)
                torn.fetch_add(1);
        }

        positive->wait();
        negative->wait();
        delete positive;
        delete negative;

        // 值与质量码必须来自同一次写入，不能读到"新值 + 旧质量"这种半帧
        QCOMPARE(torn.load(), 0);
    }
};

QTEST_GUILESS_MAIN(TestDataBank)
#include "tst_databank.moc"
