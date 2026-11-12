// SPDX-License-Identifier: GPL-3.0-or-later
#include "protosim/ui/framemodel.h"

#include <QDateTime>
#include <QFont>

namespace protosim::ui {
namespace {

QString formatTime(qint64 timestampMs)
{
    if (timestampMs <= 0)
        return QStringLiteral("—");
    return QDateTime::fromMSecsSinceEpoch(timestampMs).toString(QStringLiteral("HH:mm:ss.zzz"));
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

void FrameTableModel::setTokens(const ThemeTokens &tokens)
{
    m_tokens = tokens;
    if (!m_rows.isEmpty()) {
        Q_EMIT dataChanged(index(0, 0), index(m_rows.size() - 1, ColumnCount - 1),
                          {Qt::ForegroundRole});
    }
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
    if (parent.isValid())
        return 0;
    return m_rows.size();
}

int FrameTableModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return ColumnCount;
}

QVariant FrameTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};

    const FrameRecord &record = m_rows.at(index.row());

    if (role == Qt::FontRole && index.column() == HexColumn) {
        QFont font(QStringLiteral("Consolas"));
        font.setStyleHint(QFont::Monospace);
        font.setPointSize(9);
        return font;
    }

    if (role == Qt::ForegroundRole) {
        if (record.error)
            return m_tokens.bad;
        if (record.direction == FrameDirection::Outbound)
            return m_tokens.subtleText;
        return {};
    }

    if (role == Qt::TextAlignmentRole && index.column() == TimeColumn)
        return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);

    if (role != Qt::DisplayRole)
        return {};

    switch (index.column()) {
    case TimeColumn:
        return formatTime(record.timestampMs);
    case DirectionColumn:
        if (record.bytes.isEmpty())
            return QStringLiteral("· 记录"); // 端点启停这类事件，不是协议帧
        return record.direction == FrameDirection::Inbound ? QStringLiteral("→ 收")
                                                            : QStringLiteral("← 发");
    case ClientColumn:
        return record.clientId;
    case FunctionColumn:
        return record.functionCode == 0
            ? QString()
            : QStringLiteral("0x%1")
                  .arg(QString::number(record.functionCode, 16).toUpper().rightJustified(
                      2, QLatin1Char('0')));
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
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
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
