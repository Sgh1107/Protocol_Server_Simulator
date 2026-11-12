// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "protosim/databank.h"
#include "protosim/iprotocoladapter.h"

#include <QMetaType>
#include <QMutex>
#include <QObject>
#include <QVariant>
#include <QVector>

namespace protosim {

/// 一次写请求。适配器把协议报文翻成这个结构后提交，不允许自己动 DataBank。
struct WriteIntent
{
    QString signalId;
    QVariant value;
    QString sourceProtocol; // "modbus" / "iec104" / "mqtt" / "ui"
    QString clientId;       // 主站地址或 UI 操作者，审计要用
    bool forced = false;    // 手工强制写
};

/// 审计记录：谁、何时、从哪个协议、旧值/新值、成没成。
struct AuditRecord
{
    qint64 timestampMs = 0;
    QString signalId;
    QVariant oldValue;
    QVariant newValue;
    QString sourceProtocol;
    QString clientId;
    WriteOutcome outcome = WriteOutcome::Accepted;
    bool forced = false;
    bool accepted = false;

    QString toString() const;
};

/// 统一写回管线（INTERFACES §4）。
///
///   任意协议 onWrite()
///     -> WritePipeline::submit()
///          写保护检查 -> 审计 -> DataBank 原子更新 -> EventBus 广播
///
/// 它是「改了 A 读 B 还是旧值」这类 bug 的唯一防线，
/// 所以适配器里任何绕过它的 DataBank 写都算破功。
class WritePipeline : public QObject
{
    Q_OBJECT

public:
    explicit WritePipeline(DataBank *bank, QObject *parent = nullptr);

    WriteOutcome submit(const WriteIntent &intent, qint64 timestampMs);
    QVector<WriteOutcome> submitBatch(const QVector<WriteIntent> &intents, qint64 timestampMs);

    /// 全局写保护（P0 只做总闸，P1 再按信号/联锁细分）。
    void setWriteProtected(bool enabled);
    bool writeProtected() const;

    QVector<AuditRecord> auditTrail() const;
    int auditCount() const;
    void clearAudit();

Q_SIGNALS:
    void written(const protosim::AuditRecord &record);
    void rejected(const protosim::AuditRecord &record);

private:
    WriteOutcome apply(const WriteIntent &intent, qint64 timestampMs, AuditRecord *record);
    void record(const AuditRecord &audit);

    mutable QMutex m_mutex;
    DataBank *m_bank = nullptr;
    bool m_writeProtected = false;
    QVector<AuditRecord> m_audit;
    int m_auditCapacity = 2000;
};

} // namespace protosim

Q_DECLARE_METATYPE(protosim::AuditRecord)
