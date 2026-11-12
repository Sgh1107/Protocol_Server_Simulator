// SPDX-License-Identifier: GPL-3.0-or-later
#include "protosim/curve.h"

#include <QJsonObject>

#include <cmath>
#include <random>

namespace protosim {
namespace {

constexpr double kPi = 3.14159265358979323846;

/// 场景种子 + 信号 id 混合成该信号专属种子。
/// 目的是「增删信号不改变其他信号的随机序列」，让复现只依赖种子和 tick 顺序。
quint64 mixSeed(quint64 seed, const QString &signalId)
{
    quint64 hash = 1469598103934665603ULL; // FNV-1a offset basis
    const QByteArray bytes = signalId.toUtf8();
    for (char byte : bytes) {
        hash ^= static_cast<quint8>(byte);
        hash *= 1099511628211ULL;
    }
    return seed ^ hash;
}

/// 曲线的中心值。
///
/// 优先级：behavior.value -> behavior.{min,max} 中点 -> signal.range 中点 -> 0。
/// 注意 behavior 的 min/max 是**行为输出范围**（如正弦 15~35），
/// 而 signal.range 是**工程量程**（如传感器 -40~120），两者不能混用 ——
/// 混用会让正弦中心跑到 40 上，输出变成 30~50。
double deriveBase(const QJsonObject &behavior, const Signal &signal)
{
    if (behavior.contains(QStringLiteral("value")))
        return behavior.value(QStringLiteral("value")).toDouble();

    if (behavior.contains(QStringLiteral("min")) && behavior.contains(QStringLiteral("max"))) {
        return (behavior.value(QStringLiteral("min")).toDouble()
                + behavior.value(QStringLiteral("max")).toDouble())
            / 2.0;
    }

    if (signal.range.valid)
        return (signal.range.min + signal.range.max) / 2.0;

    return 0.0;
}

class ConstantCurve : public Curve
{
public:
    explicit ConstantCurve(double value)
        : m_value(value)
    {
    }

    QString kind() const override { return QStringLiteral("constant"); }

    double tick(qint64, qint64) override { return m_value; }

    void reset() override {}

private:
    double m_value;
};

class SineCurve : public Curve
{
public:
    SineCurve(const Signal &signal, const QJsonObject &behavior)
        : m_amplitude(behavior.value(QStringLiteral("amplitude")).toDouble(0.0))
        , m_periodMs(behavior.value(QStringLiteral("periodMs")).toInt(1000))
        , m_phaseRad(behavior.value(QStringLiteral("phaseDeg")).toDouble(0.0) * kPi / 180.0)
        , m_base(deriveBase(behavior, signal))
        , m_hasBounds(behavior.contains(QStringLiteral("min"))
                      && behavior.contains(QStringLiteral("max")))
        , m_lo(behavior.value(QStringLiteral("min")).toDouble(0.0))
        , m_hi(behavior.value(QStringLiteral("max")).toDouble(0.0))
    {
        if (m_periodMs <= 0)
            m_periodMs = 1000;
        if (m_hasBounds && m_hi < m_lo)
            std::swap(m_lo, m_hi);
    }

    QString kind() const override { return QStringLiteral("sine"); }

    double tick(qint64 virtualTimeMs, qint64) override
    {
        const double angle =
            2.0 * kPi * (static_cast<double>(virtualTimeMs) / static_cast<double>(m_periodMs))
            + m_phaseRad;
        const double value = m_base + m_amplitude * std::sin(angle);
        if (!m_hasBounds)
            return value;
        return value < m_lo ? m_lo : (value > m_hi ? m_hi : value);
    }

    void reset() override {}

private:
    double m_amplitude;
    int m_periodMs;
    double m_phaseRad;
    double m_base;
    bool m_hasBounds;
    double m_lo;
    double m_hi;
};

class RandomWalkCurve : public Curve
{
public:
    RandomWalkCurve(const Signal &signal, const QJsonObject &behavior, quint64 seed)
        : m_lo(behavior.value(QStringLiteral("min")).toDouble(signal.range.valid ? signal.range.min : 0.0))
        , m_hi(behavior.value(QStringLiteral("max")).toDouble(signal.range.valid ? signal.range.max : 100.0))
        , m_step(std::abs(behavior.value(QStringLiteral("step")).toDouble(1.0)))
        , m_intervalMs(behavior.value(QStringLiteral("intervalMs")).toInt(500))
        , m_start(deriveBase(behavior, signal))
        , m_seed(mixSeed(seed, signal.id))
        , m_rng(m_seed)
        , m_current(m_start)
    {
        if (m_intervalMs <= 0)
            m_intervalMs = 500;
        if (m_hi < m_lo)
            std::swap(m_lo, m_hi);

        m_current = clamp(m_start);
        m_nextStepAtMs = m_intervalMs;
    }

    QString kind() const override { return QStringLiteral("randomWalk"); }

    double tick(qint64 virtualTimeMs, qint64) override
    {
        if (virtualTimeMs >= m_nextStepAtMs) {
            std::uniform_real_distribution<double> distribution(-m_step, m_step);
            m_current = clamp(m_current + distribution(m_rng));
            m_nextStepAtMs = virtualTimeMs + m_intervalMs;
        }
        return m_current;
    }

    void reset() override
    {
        m_rng.seed(m_seed);
        m_current = clamp(m_start);
        m_nextStepAtMs = m_intervalMs;
    }

private:
    double clamp(double value) const { return value < m_lo ? m_lo : (value > m_hi ? m_hi : value); }

    double m_lo;
    double m_hi;
    double m_step;
    int m_intervalMs;
    double m_start;
    quint64 m_seed;
    std::mt19937_64 m_rng;
    double m_current;
    qint64 m_nextStepAtMs = 0;
};

} // namespace

bool isBehaviorSupported(const QString &behaviorType)
{
    return behaviorType == QLatin1String("constant") || behaviorType == QLatin1String("sine")
        || behaviorType == QLatin1String("randomWalk");
}

std::unique_ptr<Curve> createCurve(const Signal &signal, quint64 seed, QString *errorOut)
{
    const QString type = signal.behavior.value(QStringLiteral("type")).toString();

    if (type == QLatin1String("constant")) {
        const double value = signal.behavior.value(QStringLiteral("value")).toDouble(0.0);
        return std::make_unique<ConstantCurve>(value);
    }
    if (type == QLatin1String("sine"))
        return std::make_unique<SineCurve>(signal, signal.behavior);
    if (type == QLatin1String("randomWalk"))
        return std::make_unique<RandomWalkCurve>(signal, signal.behavior, seed);

    if (errorOut) {
        *errorOut = QStringLiteral("P0 尚未实现 behavior.type \"%1\"（%2），该信号不参与仿真")
                        .arg(type, signal.id);
    }
    return nullptr;
}

} // namespace protosim
