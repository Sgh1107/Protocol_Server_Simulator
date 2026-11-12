// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QMetaType>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace protosim {

enum class FrameDirection {
    Inbound,  // 主站 -> 模拟器
    Outbound, // 模拟器 -> 主站
};

QString frameDirectionToString(FrameDirection direction);

/// 报文日志的一条。字段对齐 AC-P0-14：
/// 时间戳 / 方向 / 客户端 / 协议 / hex / 解析 / 耗时，外加用于过滤的信号列表。
struct FrameRecord
{
    qint64 timestampMs = 0;    // 墙上时间（报文日志用真实时间，不用仿真时钟）
    qint64 elapsedUs = -1;     // 请求到响应的耗时；inbound 记录没有
    QString clientId;          // 形如 127.0.0.1:52344
    QString protocol = QStringLiteral("modbus");
    FrameDirection direction = FrameDirection::Inbound;
    QByteArray bytes;          // 完整 ADU
    QString summary;           // 人类可读解析，如 "FC03 读 HR[0..1]"
    QStringList signalIds;     // 该帧涉及的信号，供按信号过滤
    bool error = false;        // 异常响应
    quint16 transactionId = 0;
    quint8 functionCode = 0;

    QString hexString() const;
};

/// 内存环形缓冲。P0 不落盘（SQLite 在 P1），只保证「够用 + 不拖慢 100ms tick」。
class TelemetryLog : public QObject
{
    Q_OBJECT

public:
    explicit TelemetryLog(int capacity = 5000, QObject *parent = nullptr);

    void append(const FrameRecord &record);
    void clear();

    QVector<FrameRecord> records() const;
    QVector<FrameRecord> recordsOf(const QString &signalId) const;
    QVector<FrameRecord> recent(int count) const;

    int count() const;
    int capacity() const;
    void setCapacity(int capacity);

    /// 导出为纯文本（UI「导出」按钮与自检都走这里）。
    QString toText() const;
    QString toTextOf(const QString &signalId) const;

Q_SIGNALS:
    void recordAppended(const protosim::FrameRecord &record);
    void cleared();

private:
    mutable QMutex m_mutex;
    QVector<FrameRecord> m_records;
    int m_capacity;
};

} // namespace protosim

Q_DECLARE_METATYPE(protosim::FrameRecord)
