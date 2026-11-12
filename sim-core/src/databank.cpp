// SPDX-License-Identifier: GPL-3.0-or-later
#include "protosim/databank.h"

#include <QMutexLocker>

namespace protosim {

double SignalSnapshot::toDouble(bool *ok) const
{
    bool good = false;
    const double d = value.toDouble(&good);
    if (ok)
        *ok = good;
    return d;
}

qint64 SignalSnapshot::toLongLong(bool *ok) const
{
    bool good = false;
    const qint64 v = value.toLongLong(&good);
    if (ok)
        *ok = good;
    return v;
}

DataBank::DataBank(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<protosim::SignalSnapshot>("protosim::SignalSnapshot");
    qRegisterMetaType<protosim::SignalChange>("protosim::SignalChange");
}

DataBank::~DataBank() = default;

bool DataBank::registerSignal(const Signal &signal)
{
    if (!signal.isValid())
        return false;

    {
        QMutexLocker locker(&m_mutex);
        if (m_definitions.contains(signal.id))
            return false;

        m_definitions.insert(signal.id, signal);

        SignalSnapshot initial;
        initial.signalId = signal.id;
        initial.value = QVariant(0);
        initial.quality = signal.initialQuality;
        initial.timestampMs = 0;
        initial.forced = false;
        m_values.insert(signal.id, initial);
        m_order.append(signal.id);
    }

    Q_EMIT signalRegistered(signal.id);
    return true;
}

bool DataBank::contains(const QString &signalId) const
{
    QMutexLocker locker(&m_mutex);
    return m_definitions.contains(signalId);
}

Signal DataBank::definition(const QString &signalId) const
{
    QMutexLocker locker(&m_mutex);
    return m_definitions.value(signalId);
}

QVector<Signal> DataBank::definitions() const
{
    QMutexLocker locker(&m_mutex);
    QVector<Signal> out;
    out.reserve(m_order.size());
    for (const QString &id : m_order)
        out.append(m_definitions.value(id));
    return out;
}

QStringList DataBank::signalIds() const
{
    QMutexLocker locker(&m_mutex);
    return m_order;
}

int DataBank::count() const
{
    QMutexLocker locker(&m_mutex);
    return m_order.size();
}

SignalSnapshot DataBank::snapshot(const QString &signalId) const
{
    QMutexLocker locker(&m_mutex);
    return m_values.value(signalId);
}

QVector<SignalSnapshot> DataBank::snapshots() const
{
    QMutexLocker locker(&m_mutex);
    QVector<SignalSnapshot> out;
    out.reserve(m_order.size());
    for (const QString &id : m_order)
        out.append(m_values.value(id));
    return out;
}

QVector<SignalSnapshot> DataBank::snapshotsOf(const QStringList &signalIds) const
{
    QMutexLocker locker(&m_mutex);
    QVector<SignalSnapshot> out;
    out.reserve(signalIds.size());
    for (const QString &id : signalIds) {
        if (m_values.contains(id))
            out.append(m_values.value(id));
    }
    return out;
}

bool DataBank::write(const QString &signalId,
                     const QVariant &value,
                     Quality quality,
                     qint64 timestampMs,
                     bool forced,
                     const QString &sourceProtocol,
                     const QString &clientId)
{
    SignalUpdate update;
    update.signalId = signalId;
    update.value = value;
    update.quality = quality;
    update.forced = forced;

    QVector<SignalChange> changes;
    bool ok = false;
    {
        QMutexLocker locker(&m_mutex);
        ok = applyUpdatesLocked({update}, timestampMs, sourceProtocol, clientId, &changes);
    }

    for (const SignalChange &change : changes)
        Q_EMIT signalChanged(change);

    return ok;
}

bool DataBank::writeBatch(const QVector<SignalUpdate> &updates,
                          qint64 timestampMs,
                          const QString &sourceProtocol,
                          const QString &clientId)
{
    QVector<SignalChange> changes;
    bool ok = false;
    {
        QMutexLocker locker(&m_mutex);
        ok = applyUpdatesLocked(updates, timestampMs, sourceProtocol, clientId, &changes);
    }

    for (const SignalChange &change : changes)
        Q_EMIT signalChanged(change);

    return ok;
}

bool DataBank::setQuality(const QString &signalId, Quality quality, qint64 timestampMs)
{
    QVector<SignalChange> changes;
    bool ok = false;
    {
        QMutexLocker locker(&m_mutex);
        auto it = m_values.find(signalId);
        if (it == m_values.end())
            return false;

        SignalChange change;
        change.signalId = signalId;
        change.oldValue = it->value;
        change.newValue = it->value;
        change.quality = quality;
        change.timestampMs = timestampMs;
        change.sourceProtocol = QStringLiteral("core");
        change.forced = it->forced;

        it->quality = quality;
        it->timestampMs = timestampMs;

        changes.append(change);
        ok = true;
    }

    for (const SignalChange &change : changes)
        Q_EMIT signalChanged(change);

    return ok;
}

bool DataBank::force(const QString &signalId,
                     const QVariant &value,
                     qint64 timestampMs,
                     const QString &sourceProtocol,
                     const QString &clientId)
{
    return write(signalId, value, Quality::Good, timestampMs, true, sourceProtocol, clientId);
}

bool DataBank::clearForce(const QString &signalId, qint64 timestampMs)
{
    QVector<SignalChange> changes;
    bool ok = false;
    {
        QMutexLocker locker(&m_mutex);
        auto it = m_values.find(signalId);
        if (it == m_values.end())
            return false;

        if (it->forced) {
            SignalChange change;
            change.signalId = signalId;
            change.oldValue = it->value;
            change.newValue = it->value;
            change.quality = it->quality;
            change.timestampMs = timestampMs;
            change.sourceProtocol = QStringLiteral("core");
            change.forced = false;
            changes.append(change);
        }

        it->forced = false;
        it->timestampMs = timestampMs;
        ok = true;
    }

    for (const SignalChange &change : changes)
        Q_EMIT signalChanged(change);

    return ok;
}

void DataBank::reset()
{
    QMutexLocker locker(&m_mutex);
    m_definitions.clear();
    m_values.clear();
    m_order.clear();
}

bool DataBank::applyUpdatesLocked(const QVector<SignalUpdate> &updates,
                                  qint64 timestampMs,
                                  const QString &sourceProtocol,
                                  const QString &clientId,
                                  QVector<SignalChange> *changes)
{
    // 先全量校验，再统一落地：任一 id 不存在则整批不动，避免半帧。
    for (const SignalUpdate &update : updates) {
        if (!m_values.contains(update.signalId))
            return false;
    }

    changes->reserve(changes->size() + updates.size());

    for (const SignalUpdate &update : updates) {
        auto it = m_values.find(update.signalId);

        SignalChange change;
        change.signalId = update.signalId;
        change.oldValue = it->value;
        change.newValue = update.value;
        change.quality = update.quality;
        change.timestampMs = timestampMs;
        change.sourceProtocol = sourceProtocol;
        change.clientId = clientId;
        change.forced = update.forced;

        it->value = update.value;
        it->quality = update.quality;
        it->timestampMs = timestampMs;
        it->forced = update.forced;

        changes->append(change);
    }

    return true;
}

} // namespace protosim
