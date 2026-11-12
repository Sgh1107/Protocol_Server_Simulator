// SPDX-License-Identifier: GPL-3.0-or-later
#include "protosim/telemetrylog.h"

#include <QDateTime>
#include <QMutexLocker>
#include <QStringList>

namespace protosim {
namespace {

QString formatTimestamp(qint64 timestampMs)
{
    const QDateTime moment = QDateTime::fromMSecsSinceEpoch(timestampMs);
    return moment.toString(QStringLiteral("HH:mm:ss.zzz"));
}

} // namespace

QString frameDirectionToString(FrameDirection direction)
{
    return direction == FrameDirection::Inbound ? QStringLiteral("in") : QStringLiteral("out");
}

QString FrameRecord::hexString() const
{
    if (bytes.isEmpty())
        return QString();

    QString hex = QString::fromLatin1(bytes.toHex(' ')).toUpper();
    return hex;
}

TelemetryLog::TelemetryLog(int capacity, QObject *parent)
    : QObject(parent)
    , m_capacity(capacity > 0 ? capacity : 1)
{
    qRegisterMetaType<protosim::FrameRecord>("protosim::FrameRecord");
    m_records.reserve(m_capacity);
}

void TelemetryLog::append(const FrameRecord &record)
{
    {
        QMutexLocker locker(&m_mutex);
        m_records.append(record);
        while (m_records.size() > m_capacity)
            m_records.removeFirst();
    }

    Q_EMIT recordAppended(record);
}

void TelemetryLog::clear()
{
    {
        QMutexLocker locker(&m_mutex);
        m_records.clear();
    }
    Q_EMIT cleared();
}

QVector<FrameRecord> TelemetryLog::records() const
{
    QMutexLocker locker(&m_mutex);
    return m_records;
}

QVector<FrameRecord> TelemetryLog::recordsOf(const QString &signalId) const
{
    QMutexLocker locker(&m_mutex);
    QVector<FrameRecord> out;
    for (const FrameRecord &record : m_records) {
        if (record.signalIds.contains(signalId))
            out.append(record);
    }
    return out;
}

QVector<FrameRecord> TelemetryLog::recent(int count) const
{
    QMutexLocker locker(&m_mutex);
    if (count <= 0 || count >= m_records.size())
        return m_records;

    return m_records.mid(m_records.size() - count);
}

int TelemetryLog::count() const
{
    QMutexLocker locker(&m_mutex);
    return m_records.size();
}

int TelemetryLog::capacity() const
{
    QMutexLocker locker(&m_mutex);
    return m_capacity;
}

void TelemetryLog::setCapacity(int capacity)
{
    if (capacity <= 0)
        return;

    QMutexLocker locker(&m_mutex);
    m_capacity = capacity;
    while (m_records.size() > m_capacity)
        m_records.removeFirst();
}

QString TelemetryLog::toText() const
{
    const QVector<FrameRecord> snapshot = records();

    QStringList lines;
    lines.reserve(snapshot.size());
    for (const FrameRecord &record : snapshot) {
        QString line = QStringLiteral("%1 | %2 | %3 | %4 | %5")
                           .arg(formatTimestamp(record.timestampMs),
                                frameDirectionToString(record.direction),
                                record.clientId,
                                record.hexString(),
                                record.summary);

        if (record.elapsedUs >= 0)
            line += QStringLiteral(" | %1us").arg(record.elapsedUs);
        if (!record.signalIds.isEmpty())
            line += QStringLiteral(" | {%1}").arg(record.signalIds.join(QLatin1Char(',')));

        lines.append(line);
    }
    return lines.join(QLatin1Char('\n'));
}

QString TelemetryLog::toTextOf(const QString &signalId) const
{
    const QVector<FrameRecord> filtered = recordsOf(signalId);

    QStringList lines;
    lines.reserve(filtered.size());
    for (const FrameRecord &record : filtered) {
        lines.append(QStringLiteral("%1 | %2 | %3 | %4 | %5")
                         .arg(formatTimestamp(record.timestampMs),
                              frameDirectionToString(record.direction),
                              record.clientId,
                              record.hexString(),
                              record.summary));
    }
    return lines.join(QLatin1Char('\n'));
}

} // namespace protosim
