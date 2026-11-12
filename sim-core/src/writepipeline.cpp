// SPDX-License-Identifier: GPL-3.0-or-later
#include "protosim/writepipeline.h"

#include <QMutexLocker>
#include <QStringList>

namespace protosim {
namespace {

QString describeValue(const QVariant &value)
{
    if (!value.isValid())
        return QStringLiteral("<invalid>");
    return value.toString();
}

} // namespace

QString AuditRecord::toString() const
{
    return QStringLiteral("%1 %2 -> %3 %4 [%5/%6] %7")
        .arg(signalId,
             describeValue(oldValue),
             describeValue(newValue),
             forced ? QStringLiteral("(forced)") : QString(),
             sourceProtocol,
             clientId.isEmpty() ? QStringLiteral("-") : clientId,
             writeOutcomeToString(outcome));
}

WritePipeline::WritePipeline(DataBank *bank, QObject *parent)
    : QObject(parent)
    , m_bank(bank)
{
    qRegisterMetaType<protosim::AuditRecord>("protosim::AuditRecord");
    m_audit.reserve(m_auditCapacity);
}

void WritePipeline::setWriteProtected(bool enabled)
{
    QMutexLocker locker(&m_mutex);
    m_writeProtected = enabled;
}

bool WritePipeline::writeProtected() const
{
    QMutexLocker locker(&m_mutex);
    return m_writeProtected;
}

WriteOutcome WritePipeline::submit(const WriteIntent &intent, qint64 timestampMs)
{
    AuditRecord audit;
    const WriteOutcome outcome = apply(intent, timestampMs, &audit);
    record(audit);

    if (audit.accepted)
        Q_EMIT written(audit);
    else
        Q_EMIT rejected(audit);

    return outcome;
}

QVector<WriteOutcome> WritePipeline::submitBatch(const QVector<WriteIntent> &intents,
                                                 qint64 timestampMs)
{
    QVector<WriteOutcome> outcomes;
    outcomes.reserve(intents.size());

    QVector<AuditRecord> audits;
    audits.reserve(intents.size());

    for (const WriteIntent &intent : intents) {
        AuditRecord audit;
        outcomes.append(apply(intent, timestampMs, &audit));
        audits.append(audit);
    }

    for (const AuditRecord &audit : audits) {
        record(audit);
        if (audit.accepted)
            Q_EMIT written(audit);
        else
            Q_EMIT rejected(audit);
    }

    return outcomes;
}

QVector<AuditRecord> WritePipeline::auditTrail() const
{
    QMutexLocker locker(&m_mutex);
    return m_audit;
}

int WritePipeline::auditCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_audit.size();
}

void WritePipeline::clearAudit()
{
    QMutexLocker locker(&m_mutex);
    m_audit.clear();
}

WriteOutcome WritePipeline::apply(const WriteIntent &intent, qint64 timestampMs, AuditRecord *record)
{
    *record = AuditRecord{};
    record->timestampMs = timestampMs;
    record->signalId = intent.signalId;
    record->sourceProtocol = intent.sourceProtocol;
    record->clientId = intent.clientId;
    record->newValue = intent.value;
    record->forced = intent.forced;

    if (m_bank == nullptr || !m_bank->contains(intent.signalId)) {
        record->outcome = WriteOutcome::RejectedUnknownSignal;
        record->accepted = false;
        return record->outcome;
    }

    const SignalSnapshot before = m_bank->snapshot(intent.signalId);
    record->oldValue = before.value;

    {
        QMutexLocker locker(&m_mutex);
        if (m_writeProtected) {
            record->outcome = WriteOutcome::RejectedWriteProtected;
            record->accepted = false;
            return record->outcome;
        }
    }

    // 写进来的值先按信号量程钳位（越界置 OutOfRange，不静默当正常值）。
    // 强制的值不钳位 —— 手工强制就是要造越界场景，钳掉就没有意义了。
    const Signal definition = m_bank->definition(intent.signalId);
    double engineering = intent.value.toDouble();

    Quality quality = before.quality;
    if (quality == Quality::Unknown)
        quality = Quality::Good;

    bool clamped = false;
    if (!intent.forced) {
        engineering = definition.clampToRange(engineering, &clamped);
        quality = clamped ? Quality::OutOfRange : Quality::Good;
    } else {
        quality = Quality::Good;
    }

    const QVariant stored = intent.value.canConvert<double>() && intent.value.typeId() != QMetaType::QString
        ? QVariant(engineering)
        : intent.value;

    const bool ok = m_bank->write(intent.signalId,
                                  stored,
                                  quality,
                                  timestampMs,
                                  intent.forced,
                                  intent.sourceProtocol,
                                  intent.clientId);

    record->newValue = stored;
    if (!ok) {
        record->outcome = WriteOutcome::RejectedUnknownSignal;
        record->accepted = false;
        return record->outcome;
    }

    record->outcome = WriteOutcome::Accepted;
    record->accepted = true;
    return record->outcome;
}

void WritePipeline::record(const AuditRecord &audit)
{
    QMutexLocker locker(&m_mutex);
    m_audit.append(audit);
    while (m_audit.size() > m_auditCapacity)
        m_audit.removeFirst();
}

} // namespace protosim
