// SPDX-License-Identifier: GPL-3.0-or-later
#include "protosim/simulationengine.h"

#include <QtGlobal>

namespace protosim {

SimulationEngine::SimulationEngine(DataBank *bank, QObject *parent)
    : QObject(parent)
    , m_bank(bank)
{
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &SimulationEngine::tickOnce);
}

SimulationEngine::~SimulationEngine()
{
    m_timer.stop();
}

Result<void> SimulationEngine::load(const ConfigBundle &bundle)
{
    m_timer.stop();
    m_entries.clear();
    m_unsupported.clear();

    if (m_bank == nullptr) {
        return Result<void>::failure(ErrorCode::InvalidArgument,
                                     QStringLiteral("仿真引擎没有绑定 DataBank"));
    }

    m_tickMs = bundle.meta.tickMs > 0 ? bundle.meta.tickMs : 100;
    m_seed = static_cast<quint64>(bundle.meta.seed);

    for (const Signal &signal : bundle.signals) {
        QString error;
        std::unique_ptr<Curve> curve = createCurve(signal, m_seed, &error);
        if (!curve) {
            m_unsupported.append(error.isEmpty() ? signal.id : error);
            continue;
        }
        m_entries.push_back(Entry{signal.id, signal, std::move(curve)});
    }

    resetClock();
    m_state = State::Idle;
    applyTimerInterval();
    Q_EMIT stateChanged(m_state);

    return {};
}

void SimulationEngine::start()
{
    resetClock();
    applyTimerInterval();
    m_timer.start();
    setState(State::Running);
}

void SimulationEngine::pause()
{
    if (m_state != State::Running)
        return;
    m_timer.stop();
    setState(State::Paused);
}

void SimulationEngine::resume()
{
    if (m_state != State::Paused)
        return;
    applyTimerInterval();
    m_timer.start();
    setState(State::Running);
}

void SimulationEngine::stop()
{
    m_timer.stop();
    resetClock();
    setState(State::Idle);
}

void SimulationEngine::stepOnce()
{
    // 单步不启动定时器：暂停态可单步，Running 态也允许（相当于手动补一帧）
    tickOnce();
}

void SimulationEngine::advance(int ticks)
{
    for (int i = 0; i < ticks; ++i)
        tickOnce();
}

void SimulationEngine::setSpeed(double multiplier)
{
    const double clamped = qBound(0.1, multiplier, 100.0);
    if (qFuzzyCompare(clamped, m_speed))
        return;

    m_speed = clamped;
    applyTimerInterval();
}

void SimulationEngine::applyTimerInterval()
{
    if (m_tickMs <= 0)
        return;

    const int interval = qMax(1, static_cast<int>(qRound(static_cast<double>(m_tickMs) / m_speed)));
    m_timer.setInterval(interval);
}

void SimulationEngine::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    Q_EMIT stateChanged(state);
}

void SimulationEngine::resetClock()
{
    m_virtualTimeMs = 0;
    m_tickCount = 0;
    for (Entry &entry : m_entries)
        entry.curve->reset();
}

void SimulationEngine::tickOnce()
{
    if (m_bank == nullptr)
        return;

    // 虚拟时间只按 tickMs 前进，与倍速无关 —— 这是"倍速不改序列"的根
    m_virtualTimeMs += m_tickMs;
    ++m_tickCount;

    QVector<SignalUpdate> updates;
    updates.reserve(static_cast<int>(m_entries.size()));

    for (const Entry &entry : m_entries) {
        // 手工强制优先：forced 的信号这一帧不由仿真产生
        if (m_bank->snapshot(entry.signalId).forced)
            continue;

        const double raw = entry.curve->tick(m_virtualTimeMs, m_tickMs);
        bool outOfRange = false;
        const double engineering = entry.signal.clampToRange(raw, &outOfRange);

        SignalUpdate update;
        update.signalId = entry.signalId;
        update.value = engineering;
        update.quality = outOfRange ? Quality::OutOfRange : Quality::Good;
        updates.append(update);
    }

    if (!updates.isEmpty()) {
        m_bank->writeBatch(updates,
                           m_virtualTimeMs,
                           QStringLiteral("engine"));
    }

    Q_EMIT ticked(m_virtualTimeMs, m_tickCount);
}

} // namespace protosim
