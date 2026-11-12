// SPDX-License-Identifier: GPL-3.0-or-later
//
// AC-P0-06：同一 seed=20261111 跑两次各 1000 tick，逐帧完全一致；
//           暂停 / 单步 / 倍速不改变序列。
// AC-P0-07（仿真侧）：reg0 在 15~35 间按 10s 周期正弦；reg1 有界随机游走；reg2 恒 42。

#include "protosim/configbundle.h"
#include "protosim/databank.h"
#include "protosim/simulationengine.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

#include <cmath>

using namespace protosim;

namespace {

QString presetPath()
{
    return QStringLiteral(PROTOSIM_SOURCE_DIR "/config/examples/fieldlink_preset.json");
}

struct Harness
{
    DataBank bank;
    SimulationEngine engine{&bank};
    bool loaded = false;

    Harness()
    {
        const auto bundle = ConfigBundle::loadFromFile(presetPath());
        if (!bundle.ok()) {
            qWarning("加载预设失败：%s", qPrintable(bundle.error().toString()));
            return;
        }
        if (!bundle.value().populate(&bank).ok())
            return;
        loaded = engine.load(bundle.value()).ok();
    }

    QVector<double> series(const QString &signalId, int ticks)
    {
        QVector<double> values;
        values.reserve(ticks);
        for (int i = 0; i < ticks; ++i) {
            engine.stepOnce();
            values.append(bank.snapshot(signalId).toDouble());
        }
        return values;
    }
};

} // namespace

class TestSimulation : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(QFile::exists(presetPath()));
    }

    void sameSeedProducesIdenticalSeries()
    {
        Harness first;
        Harness second;
        QVERIFY(first.loaded);
        QVERIFY(second.loaded);
        QCOMPARE(first.engine.seed(), quint64(20261111));

        const int ticks = 1000;
        const QVector<double> a = first.series(QStringLiteral("temperature"), ticks);
        const QVector<double> b = second.series(QStringLiteral("temperature"), ticks);

        QCOMPARE(a.size(), ticks);
        QCOMPARE(b.size(), ticks);
        for (int i = 0; i < ticks; ++i) {
            // 逐帧比较，完全相等（不是近似）
            QCOMPARE(b.at(i), a.at(i));
        }

        // 随机游走这条更敏感：任何一次越界推进都会立刻暴露
        Harness third;
        Harness fourth;
        QVERIFY(third.loaded);
        QVERIFY(fourth.loaded);
        const QVector<double> walkA = third.series(QStringLiteral("pressure"), 500);
        const QVector<double> walkB = fourth.series(QStringLiteral("pressure"), 500);
        QCOMPARE(walkA, walkB);
    }

    void differentSeedDivergesRandomWalk()
    {
        Harness a;
        QVERIFY(a.loaded);
        const QVector<double> seriesA = a.series(QStringLiteral("pressure"), 200);

        DataBank bank;
        SimulationEngine engine(&bank);
        auto bundle = ConfigBundle::loadFromFile(presetPath());
        QVERIFY(bundle.ok());
        bundle.value().meta.seed = 42;
        QVERIFY(bundle.value().populate(&bank).ok());
        QVERIFY(engine.load(bundle.value()).ok());

        QVector<double> seriesB;
        for (int i = 0; i < 200; ++i) {
            engine.stepOnce();
            seriesB.append(bank.snapshot(QStringLiteral("pressure")).toDouble());
        }

        QVERIFY(seriesA != seriesB);
    }

    void speedDoesNotChangeSeries()
    {
        Harness baseline;
        Harness fast;
        QVERIFY(baseline.loaded);
        QVERIFY(fast.loaded);

        fast.engine.setSpeed(25.0);
        QCOMPARE(fast.engine.speed(), 25.0);

        QCOMPARE(fast.series(QStringLiteral("temperature"), 300),
                 baseline.series(QStringLiteral("temperature"), 300));
    }

    void pauseAndStepDoNotChangeSeries()
    {
        Harness baseline;
        Harness interrupted;
        QVERIFY(baseline.loaded);
        QVERIFY(interrupted.loaded);

        const QVector<double> expected = baseline.series(QStringLiteral("temperature"), 120);

        QVector<double> actual;
        for (int i = 0; i < 120; ++i) {
            if (i == 10)
                interrupted.engine.pause();
            if (i == 11)
                interrupted.engine.resume();
            if (i == 40) {
                interrupted.engine.stop();
                // stop 会复位时钟，重新对齐后再比较没有意义，
                // 所以这里只验 pause/resume 不改变序列；stop 的复位单独测
                break;
            }
            interrupted.engine.stepOnce();
            actual.append(interrupted.bank.snapshot(QStringLiteral("temperature")).toDouble());
        }
        QCOMPARE(actual.size(), 40);
        for (int i = 0; i < actual.size(); ++i)
            QCOMPARE(actual.at(i), expected.at(i));
    }

    void stopResetsClockAndCurves()
    {
        Harness harness;
        QVERIFY(harness.loaded);

        harness.engine.advance(50);
        QCOMPARE(harness.engine.tickCount(), qint64(50));
        QCOMPARE(harness.engine.virtualTimeMs(), qint64(5000));

        harness.engine.stop();
        QCOMPARE(harness.engine.tickCount(), qint64(0));
        QCOMPARE(harness.engine.virtualTimeMs(), qint64(0));
        QCOMPARE(harness.engine.state(), SimulationEngine::State::Idle);

        // 复位后再跑，序列应从头开始，与首次一致
        const QVector<double> after = harness.series(QStringLiteral("temperature"), 20);

        Harness fresh;
        QVERIFY(fresh.loaded);
        QCOMPARE(after, fresh.series(QStringLiteral("temperature"), 20));
    }

    void timestampAdvancesByTickMs()
    {
        Harness harness;
        QVERIFY(harness.loaded);

        harness.engine.advance(3);
        QCOMPARE(harness.bank.snapshot(QStringLiteral("magic")).timestampMs, qint64(300));
        harness.engine.advance(7);
        QCOMPARE(harness.bank.snapshot(QStringLiteral("magic")).timestampMs, qint64(1000));
    }

    void sineStaysInRangeWithTenSecondPeriod()
    {
        Harness harness;
        QVERIFY(harness.loaded);

        QVector<double> values;
        double minValue = 1e9;
        double maxValue = -1e9;
        // 一个完整周期 10s = 100 tick
        for (int i = 0; i < 200; ++i) {
            harness.engine.stepOnce();
            const double value = harness.bank.snapshot(QStringLiteral("temperature")).toDouble();
            values.append(value);
            minValue = qMin(minValue, value);
            maxValue = qMax(maxValue, value);
        }

        QVERIFY2(minValue >= 15.0 - 1e-6, qPrintable(QString::number(minValue)));
        QVERIFY2(maxValue <= 35.0 + 1e-6, qPrintable(QString::number(maxValue)));
        // 采样够密时应当基本摸到上下边界（正弦幅度 10，中心 25）
        QVERIFY2(minValue < 15.5, qPrintable(QString::number(minValue)));
        QVERIFY2(maxValue > 34.5, qPrintable(QString::number(maxValue)));

        // t=2500ms 应为波峰 35，t=7500ms 应为波谷 15
        const int quarterTick = 25 - 1; // advance 到第 25 tick 时 virtualTime=2500
        QVERIFY(std::abs(values.at(quarterTick) - 35.0) < 0.05);
        QVERIFY(std::abs(values.at(75 - 1) - 15.0) < 0.05);

        // 周期性：t 与 t+10s 的值应一致
        for (int i = 0; i + 100 < values.size(); ++i)
            QVERIFY(std::abs(values.at(i + 100) - values.at(i)) < 1e-6);
    }

    void randomWalkStaysBoundedAndMoves()
    {
        Harness harness;
        QVERIFY(harness.loaded);

        QVector<double> values;
        for (int i = 0; i < 300; ++i) {
            harness.engine.stepOnce();
            const double value = harness.bank.snapshot(QStringLiteral("pressure")).toDouble();
            values.append(value);
            QVERIFY2(value >= 300.0 - 1e-9, qPrintable(QString::number(value)));
            QVERIFY2(value <= 700.0 + 1e-9, qPrintable(QString::number(value)));
        }

        bool moved = false;
        for (int i = 1; i < values.size(); ++i) {
            if (!qFuzzyCompare(values.at(i), values.at(0))) {
                moved = true;
                break;
            }
        }
        QVERIFY2(moved, "随机游走必须真的在走");
    }

    void constantStaysConstant()
    {
        Harness harness;
        QVERIFY(harness.loaded);

        for (int i = 0; i < 50; ++i) {
            harness.engine.stepOnce();
            QCOMPARE(harness.bank.snapshot(QStringLiteral("magic")).toDouble(), 42.0);
            QCOMPARE(harness.bank.snapshot(QStringLiteral("magic")).quality, Quality::Good);
        }
    }

    void forcedSignalIsNotOverwritten()
    {
        Harness harness;
        QVERIFY(harness.loaded);

        QVERIFY(harness.bank.force(QStringLiteral("magic"), 999, 0,
                                   QStringLiteral("test"), QStringLiteral("client-x")));

        harness.engine.advance(10);
        const SignalSnapshot snapshot = harness.bank.snapshot(QStringLiteral("magic"));
        QCOMPARE(snapshot.toDouble(), 999.0);
        QVERIFY(snapshot.forced);
    }

    void unsupportedBehaviorIsReported()
    {
        DataBank bank;
        SimulationEngine engine(&bank);

        auto bundle = ConfigBundle::loadFromFile(presetPath());
        QVERIFY(bundle.ok());
        QVERIFY(!isBehaviorSupported(QStringLiteral("script")));
        QVERIFY(isBehaviorSupported(QStringLiteral("sine")));

        // 直接改内存里的 behavior，模拟 P1 才实现的脚本曲线
        ConfigBundle modified = bundle.value();
        modified.signals[0].behavior.insert(QStringLiteral("type"), QStringLiteral("script"));
        QVERIFY(modified.populate(&bank).ok());
        QVERIFY(engine.load(modified).ok());

        QCOMPARE(engine.unsupportedSignals().size(), 1);
        QVERIFY(engine.unsupportedSignals().first().contains(QStringLiteral("temperature")));
    }
};

QTEST_GUILESS_MAIN(TestSimulation)
#include "tst_simulation.moc"
