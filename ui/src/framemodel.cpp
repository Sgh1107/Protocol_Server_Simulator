// SPDX-License-Identifier: GPL-3.0-or-later
#include "protosim/ui/framemodel.h"

#include <QDateTime>

namespace protosim::ui {
namespace {

QString formatTime(qint64 timestampMs)
{
    if (timestampMs <= 0)
        return QStringLiteral("—");
    return QDateTime::fromMSecsSinceEpoch(timestampMs).toString(QStringLiteral("HH:mm:ss.zzz"));
}

/// 把 ADU 切成四段并两字节一组：MBAP(灰) / 单元号(暗) / 功能码(蓝) / 数据(白)。
/// 异常响应的功能码段染红。
QVariantList buildSegments(const FrameRecord &record)
{
    QVariantList segments;
    const QByteArray &bytes = record.bytes;
    if (bytes.size() < 8)
        return segments;

    auto groupHex = [](const QByteArray &chunk) {
        QString text;
        for (int i = 0; i < chunk.size(); i += 2) {
            if (!text.isEmpty())
                text += QLatin1Char(' ');
            text += QString::fromLatin1(chunk.mid(i, 2).toHex()).toUpper();
        }
        return text;
    };

    segments.append(QStringList{groupHex(bytes.left(6)), QStringLiteral("mbap")});
    segments.append(QStringList{groupHex(bytes.mid(6, 1)), QStringLiteral("unit")});

    if (record.error) {
        segments.append(QStringList{groupHex(bytes.mid(7, 2)), QStringLiteral("err")});
        if (bytes.size() > 9)
            segments.append(QStringList{groupHex(bytes.mid(9)), QStringLiteral("data")});
        return segments;
    }

    segments.append(QStringList{groupHex(bytes.mid(7, 1)), QStringLiteral("fc")});

    QByteArray payload = bytes.mid(8);
    if (payload.isEmpty())
        return segments;

    // 读类响应（发出方向、FC01~04）的第一个数据字节是"字节数"，
    // 把它单独成段，后面的寄存器才是两两一组——否则 08 会和第一个数据字节拼在一起。
    const quint8 fc = record.functionCode;
    const bool readResponse = record.direction == FrameDirection::Outbound
        && (fc == 0x01 || fc == 0x02 || fc == 0x03 || fc == 0x04);
    if (readResponse && payload.size() > 1) {
        segments.append(QStringList{groupHex(payload.left(1)), QStringLiteral("unit")});
        payload = payload.mid(1);
    }

    if (!payload.isEmpty())
        segments.append(QStringList{groupHex(payload), QStringLiteral("data")});

    return segments;
}

QString formatElapsed(qint64 elapsedUs)
{
    if (elapsedUs < 0)
        return QString();
    if (elapsedUs < 1000)
        return QStringLiteral("%1 µs").arg(elapsedUs);
    return QStringLiteral("%1 ms").arg(elapsedUs / 1000.0, 0, 'f', 2);
}

} // namespace

FrameTableModel::FrameTableModel(TelemetryLog *log, QObject *parent)
    : QAbstractTableModel(parent)
    , m_log(log)
{
    if (m_log != nullptr)
        connect(m_log, &TelemetryLog::recordAppended, this, &FrameTableModel::onRecordAppended);
}

void FrameTableModel::setFilterSignal(const QString &signalId)
{
    m_filterSignalId = signalId;
    clearView();
    if (m_log == nullptr)
        return;

    const QVector<FrameRecord> records =
        signalId.isEmpty() ? m_log->recent(m_maxRows) : m_log->recordsOf(signalId);
    beginResetModel();
    m_rows = records.size() > m_maxRows ? records.mid(records.size() - m_maxRows) : records;
    endResetModel();
}

void FrameTableModel::clearView()
{
    if (m_rows.isEmpty())
        return;
    beginResetModel();
    m_rows.clear();
    endResetModel();
}

void FrameTableModel::onRecordAppended(const protosim::FrameRecord &record)
{
    if (!m_filterSignalId.isEmpty() && !record.signalIds.contains(m_filterSignalId))
        return;

    if (m_rows.size() >= m_maxRows) {
        beginRemoveRows(QModelIndex(), 0, 0);
        m_rows.removeFirst();
        endRemoveRows();
    }

    beginInsertRows(QModelIndex(), m_rows.size(), m_rows.size());
    m_rows.append(record);
    endInsertRows();
}

int FrameTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

int FrameTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant FrameTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};

    const FrameRecord &record = m_rows.at(index.row());

    switch (role) {
    case SegmentsRole:
        return buildSegments(record);
    case ErrorRole:
        return record.error;
    case InboundRole:
        return record.direction == FrameDirection::Inbound;
    case ElapsedUsRole:
        return record.elapsedUs;
    case SummaryRole:
        return record.summary;
    case ClientRole:
        return record.clientId;
    case TimestampMsRole:
        return record.timestampMs;
    default:
        break;
    }

    if (role != Qt::DisplayRole)
        return {};

    switch (index.column()) {
    case TimeColumn:
        return formatTime(record.timestampMs);
    case DirectionColumn:
        if (record.bytes.isEmpty())
            return QStringLiteral("· 记录");
        return record.direction == FrameDirection::Inbound ? QStringLiteral("→ 收")
                                                            : QStringLiteral("← 发");
    case ClientColumn:
        return record.clientId;
    case FunctionColumn:
        return record.functionCode == 0
            ? QString()
            : QString::number(record.functionCode, 16).toUpper().rightJustified(2,
                                                                              QLatin1Char('0'));
    case HexColumn:
        return record.hexString();
    case SummaryColumn:
        return record.summary;
    case ElapsedColumn:
        return formatElapsed(record.elapsedUs);
    default:
        break;
    }
    return {};
}

QVariant FrameTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal)
        return {};

    if (role == Qt::TextAlignmentRole) {
        const bool numeric = section == TimeColumn || section == ElapsedColumn;
        return static_cast<int>(numeric ? (Qt::AlignRight | Qt::AlignVCenter)
                                        : (Qt::AlignLeft | Qt::AlignVCenter));
    }

    if (role != Qt::DisplayRole)
        return {};

    switch (section) {
    case TimeColumn:
        return QStringLiteral("时间");
    case DirectionColumn:
        return QStringLiteral("方向");
    case ClientColumn:
        return QStringLiteral("客户端");
    case FunctionColumn:
        return QStringLiteral("FC");
    case HexColumn:
        return QStringLiteral("报文（hex）");
    case SummaryColumn:
        return QStringLiteral("解析");
    case ElapsedColumn:
        return QStringLiteral("耗时");
    default:
        break;
    }
    return {};
}

} // namespace protosim::ui
