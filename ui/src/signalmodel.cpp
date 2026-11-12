// SPDX-License-Identifier: GPL-3.0-or-later
#include "protosim/ui/signalmodel.h"

#include <QDateTime>

namespace protosim::ui {
namespace {

QString formatValue(const SignalSnapshot &snapshot)
{
    if (!snapshot.value.isValid())
        return QStringLiteral("—");

    bool ok = false;
    const double numeric = snapshot.value.toDouble(&ok);
    if (!ok)
        return snapshot.value.toString();

    if (qFuzzyCompare(numeric, qRound64(numeric) * 1.0))
        return QString::number(static_cast<qlonglong>(qRound64(numeric)));

    return QString::number(numeric, 'f', 3);
}

/// 时标可能是仿真时钟（从 0 起），也可能被接到墙上时间。
/// 小于 1973 年一律按"仿真相对时间"显示，避免出现 08:00:01.700 这种假时间。
QString formatTimestamp(qint64 timestampMs)
{
    if (timestampMs <= 0)
        return QStringLiteral("—");

    if (timestampMs < 100000000000LL)
        return QStringLiteral("T+%1s").arg(timestampMs / 1000.0, 0, 'f', 3);

    return QDateTime::fromMSecsSinceEpoch(timestampMs).toString(QStringLiteral("HH:mm:ss.zzz"));
}

QColor qualityColor(Quality quality, const ThemeTokens &tokens)
{
    switch (quality) {
    case Quality::Good:
        return tokens.good;
    case Quality::OutOfRange:
    case Quality::Timeout:
        return tokens.warning;
    case Quality::Bad:
        return tokens.bad;
    case Quality::Unknown:
        break;
    }
    return tokens.subtleText;
}

} // namespace

SignalTableModel::SignalTableModel(DataBank *bank, QObject *parent)
    : QAbstractTableModel(parent)
    , m_bank(bank)
{
    if (m_bank != nullptr) {
        connect(m_bank, &DataBank::signalChanged, this, &SignalTableModel::onSignalChanged);
        connect(m_bank, &DataBank::signalRegistered, this, [this](const QString &) { refresh(); });
    }
    refresh();
}

void SignalTableModel::setTokens(const ThemeTokens &tokens)
{
    m_tokens = tokens;
    if (!m_ids.isEmpty()) {
        Q_EMIT dataChanged(index(0, 0), index(m_ids.size() - 1, ColumnCount - 1),
                          {Qt::ForegroundRole});
    }
}

void SignalTableModel::refresh()
{
    beginResetModel();

    m_ids = m_bank != nullptr ? m_bank->signalIds() : QStringList();
    m_snapshots = m_bank != nullptr ? m_bank->snapshots() : QVector<SignalSnapshot>();
    m_rowOf.clear();
    for (int i = 0; i < m_ids.size(); ++i)
        m_rowOf.insert(m_ids.at(i), i);

    endResetModel();
}

QString SignalTableModel::signalIdAt(int row) const
{
    return row >= 0 && row < m_ids.size() ? m_ids.at(row) : QString();
}

int SignalTableModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_ids.size();
}

int SignalTableModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return ColumnCount;
}

QVariant SignalTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_snapshots.size())
        return {};

    const SignalSnapshot &snapshot = m_snapshots.at(index.row());
    const Signal definition =
        m_bank != nullptr ? m_bank->definition(snapshot.signalId) : Signal();

    if (role == Qt::TextAlignmentRole) {
        if (index.column() == ValueColumn || index.column() == TimestampColumn)
            return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
        return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
    }

    if (role == Qt::ForegroundRole) {
        if (index.column() == QualityColumn)
            return qualityColor(snapshot.quality, m_tokens);
        if (index.column() == ValueColumn && snapshot.quality != Quality::Good)
            return qualityColor(snapshot.quality, m_tokens);
        if (index.column() == IdColumn || index.column() == TimestampColumn)
            return m_tokens.subtleText;
        if (index.column() == ForcedColumn && snapshot.forced)
            return m_tokens.warning;
        return {};
    }

    if (role != Qt::DisplayRole)
        return {};

    switch (index.column()) {
    case NameColumn:
        return definition.name;
    case IdColumn:
        return snapshot.signalId;
    case DeviceColumn:
        return definition.deviceId;
    case TypeColumn:
        return dataTypeToString(definition.dataType);
    case ValueColumn:
        return formatValue(snapshot);
    case UnitColumn:
        return definition.unit;
    case QualityColumn:
        return qualityToString(snapshot.quality);
    case TimestampColumn:
        return formatTimestamp(snapshot.timestampMs);
    case ForcedColumn:
        return snapshot.forced ? QStringLiteral("强制") : QString();
    default:
        break;
    }

    return {};
}

QVariant SignalTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};

    switch (section) {
    case NameColumn:
        return QStringLiteral("名称");
    case IdColumn:
        return QStringLiteral("ID");
    case DeviceColumn:
        return QStringLiteral("设备");
    case TypeColumn:
        return QStringLiteral("类型");
    case ValueColumn:
        return QStringLiteral("工程值");
    case UnitColumn:
        return QStringLiteral("单位");
    case QualityColumn:
        return QStringLiteral("质量");
    case TimestampColumn:
        return QStringLiteral("时标");
    case ForcedColumn:
        return QStringLiteral("强制");
    default:
        break;
    }
    return {};
}

void SignalTableModel::onSignalChanged(const protosim::SignalChange &change)
{
    const int row = m_rowOf.value(change.signalId, -1);
    if (row < 0) {
        // 新信号（热加载）后补一行
        refresh();
        return;
    }

    if (m_bank == nullptr)
        return;

    m_snapshots[row] = m_bank->snapshot(change.signalId);
    Q_EMIT dataChanged(index(row, 0), index(row, ColumnCount - 1));
}

} // namespace protosim::ui
