// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "protosim/signal.h"
#include "protosim/snapshot.h"

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QStringList>
#include <QVector>

namespace protosim {

/// 批量写的一条记录。一次 tick 的所有信号凑成一个批次，
/// 保证读者要么看到旧的一整帧、要么看到新的一整帧，不会读到半帧。
struct SignalUpdate
{
    QString signalId;
    QVariant value;
    Quality quality = Quality::Good;
    bool forced = false;
};

/// 实时库：内存里唯一的真相源。
///
/// 写入口只开放 write / writeBatch / setQuality / force，
/// 协议适配器一律走 WritePipeline 提交，不允许直接摸 DataBank（INTERFACES §4）。
class DataBank : public QObject
{
    Q_OBJECT

public:
    explicit DataBank(QObject *parent = nullptr);
    ~DataBank() override;

    // ---- 点表定义 ----

    /// 注册信号。id 重复返回 false（不允许静默覆盖）。
    bool registerSignal(const Signal &signal);
    bool contains(const QString &signalId) const;
    Signal definition(const QString &signalId) const;
    QVector<Signal> definitions() const;
    QStringList signalIds() const;
    int count() const;

    // ---- 读 ----

    SignalSnapshot snapshot(const QString &signalId) const;
    QVector<SignalSnapshot> snapshots() const;
    QVector<SignalSnapshot> snapshotsOf(const QStringList &signalIds) const;

    // ---- 写 ----

    /// 单点写。信号未注册返回 false，不做隐式建点。
    bool write(const QString &signalId,
               const QVariant &value,
               Quality quality,
               qint64 timestampMs,
               bool forced = false,
               const QString &sourceProtocol = QString(),
               const QString &clientId = QString());

    /// 批量写：全部命中才落地；任一 signalId 未注册则整体不生效。
    bool writeBatch(const QVector<SignalUpdate> &updates,
                    qint64 timestampMs,
                    const QString &sourceProtocol = QString(),
                    const QString &clientId = QString());

    bool setQuality(const QString &signalId, Quality quality, qint64 timestampMs);

    /// 手工强制：写入并打 forced 标记（AC-P0-10）。
    bool force(const QString &signalId,
               const QVariant &value,
               qint64 timestampMs,
               const QString &sourceProtocol,
               const QString &clientId);

    /// 取消强制，保留当前值。
    bool clearForce(const QString &signalId, qint64 timestampMs);

    /// 清空全部（重载场景用）。
    void reset();

Q_SIGNALS:
    void signalChanged(const protosim::SignalChange &change);
    void signalRegistered(const QString &signalId);

private:
    bool applyUpdatesLocked(const QVector<SignalUpdate> &updates,
                            qint64 timestampMs,
                            const QString &sourceProtocol,
                            const QString &clientId,
                            QVector<SignalChange> *changes);

    mutable QMutex m_mutex;
    QHash<QString, Signal> m_definitions;
    QHash<QString, SignalSnapshot> m_values;
    QStringList m_order; // 点表原始顺序，UI 列顺序要稳定
};

} // namespace protosim
