// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "protosim/configbundle.h"
#include "protosim/curve.h"
#include "protosim/databank.h"
#include "protosim/result.h"

#include <QObject>
#include <QStringList>
#include <QTimer>

#include <memory>
#include <vector>

namespace protosim {

/// 确定性仿真时钟。
///
/// 三条不可让步的规矩（AC-P0-06）：
///   1. 每个 tick 虚拟时间固定推进 tickMs，与倍速无关 —— 倍速只改墙上时间节奏；
///   2. 所有随机性来自场景种子，禁止 rand() / 系统时间；
///   3. 暂停 / 单步 / 变速都不改变 tick 序列本身。
///
/// 引擎写 DataBank 一律走 writeBatch（同一 tick 一个时标），
/// 且跳过 forced 的信号 —— 手工强制优先于仿真。
class SimulationEngine : public QObject
{
    Q_OBJECT

public:
    enum class State {
        Idle,    // 未启动或已 stop
        Running, // 定时器在跑
        Paused,  // 时钟停住，可单步
    };
    Q_ENUM(State)

    explicit SimulationEngine(DataBank *bank, QObject *parent = nullptr);
    ~SimulationEngine() override;

    /// 按场景建曲线并复位时钟。不认识的 behavior.type 会被记入 unsupportedSignals()。
    Result<void> load(const ConfigBundle &bundle);

    void start();
    void pause();
    void resume();
    void stop();

    /// 单步一个 tick（暂停状态下也允许，用于逐步复现）。
    void stepOnce();

    /// 连续推进 n 个 tick，不依赖定时器。自检与回归用。
    void advance(int ticks);

    /// 0.1 ~ 100 倍速。只影响定时器间隔，不影响每 tick 的虚拟时间增量。
    void setSpeed(double multiplier);

    State state() const { return m_state; }
    double speed() const { return m_speed; }
    qint64 virtualTimeMs() const { return m_virtualTimeMs; }
    qint64 tickCount() const { return m_tickCount; }
    int tickMs() const { return m_tickMs; }
    quint64 seed() const { return m_seed; }
    QStringList unsupportedSignals() const { return m_unsupported; }

Q_SIGNALS:
    void ticked(qint64 virtualTimeMs, qint64 tickCount);
    void stateChanged(protosim::SimulationEngine::State state);

private:
    struct Entry
    {
        QString signalId;
        Signal signal;
        std::unique_ptr<Curve> curve;
    };

    void tickOnce();
    void applyTimerInterval();
    void setState(State state);
    void resetClock();

    DataBank *m_bank = nullptr;
    std::vector<Entry> m_entries;
    QTimer m_timer;
    QStringList m_unsupported;

    int m_tickMs = 100;
    quint64 m_seed = 0;
    qint64 m_virtualTimeMs = 0;
    qint64 m_tickCount = 0;
    double m_speed = 1.0;
    State m_state = State::Idle;
};

} // namespace protosim
