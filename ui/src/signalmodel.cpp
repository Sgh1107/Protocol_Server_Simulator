// SPDX-License-Identifier: GPL-3.0-or-later
#include "protosim/ui/signalmodel.h"

#include <QDateTime>

namespace protosim::ui {
namespace {

/// 小数位由「量程」决定而不是原始类型决定：
/// 压力是 uint16 但 scale=0.1，工程值 483.7 如果按整型四舍五入就丢精度了。
QString formatValue(const Signal &signal, double value)
{
    if (signal.dataType == DataType::Bool)
        return value != 0.0 ? QStringLiteral("1") : QStringLiteral("0");

    if (signal.dataType == DataType::Float32 || signal.dataType == DataType::Float64)
        return QString::number(value, 'f', 3);

    const double scale = qAbs(signal.scale);
    if (scale >= 1.0)
        return QString::number(qRound64(value));

    int decimals = 3;
    if (scale >= 0.1)
        decimals = 1;
    else if (scale >= 0.01)
        decimals = 2;
    return QString::number(value, 'f', decimals);
}

/// 时标可能是仿真时钟（从 0 起），也可能被接到墙上时间。
/// 小于 1973 年一律按「仿真相对时间」显示，避免出现 08:00:01.700 这种假时间。
QString formatTimestamp(qint64 timestampMs)
{
    if (timestampMs <= 0)
        return QStringLiteral("—");
    if (timestampMs < 100000000000LL)
        return QStringLiteral("T+%1s").arg(timestampMs / 1000.0, 0, 'f', 3);
    return QDateTime::fromMSecsSinceEpoch(timestampMs).toString(QStringLiteral("HH:mm:ss.zzz"));
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

int SignalTableModel::rowOf(const QString &signalId) const
{
    return m_rowOf.value(signalId, -1);
}

int SignalTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_ids.size();
}

int SignalTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant SignalTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_snapshots.size())
        return {};

    const SignalSnapshot &snapshot = m_snapshots.at(index.row());
    const Signal definition = m_bank != nullptr ? m_bank->definition(snapshot.signalId) : Signal();

    bool ok = false;
    const double numeric = snapshot.value.toDouble(&ok);

    switch (role) {
    case SignalIdRole:
        return snapshot.signalId;
    case ValueRole:
        return ok ? numeric : 0.0;
    case UnitRole:
        return definition.unit;
    case QualityRole:
        return static_cast<int>(snapshot.quality);
    case ForcedRole:
        return snapshot.forced;
    case RangeMinRole:
        return definition.range.min;
    case RangeMaxRole:
        return definition.range.max;
    case HasRangeRole:
        return definition.range.valid;
    case TimestampMsRole:
        return snapshot.timestampMs;
    case CategoryRole:
        return definition.category;
    default:
        break;
    }

    if (role == Qt::TextAlignmentRole)
        return static_cast<int>(Qt::AlignVCenter | (index.column() == TimestampColumn
                                                        ? Qt::AlignRight
                                                        : Qt::AlignLeft));

    if (role != Qt::DisplayRole)
        return {};

    switch (index.column()) {
    case NameColumn:
        return definition.name;
    case IdColumn:
        return snapshot.signalId;
    case TypeColumn:
        return dataTypeToString(definition.dataType);
    case ValueColumn:
        return ok ? formatValue(definition, numeric) : QStringLiteral("—");
    case QualityColumn:
        return qualityToString(snapshot.quality);
    case TimestampColumn:
        return formatTimestamp(snapshot.timestampMs);
    case ForceColumn:
        return snapshot.forced ? QStringLiteral("已强制") : QStringLiteral("强制");
    default:
        break;
    }
    return {};
}

QVariant SignalTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal)
        return {};

    if (role == Qt::TextAlignmentRole) {
        const bool numeric = section == ValueColumn || section == TimestampColumn;
        return static_cast<int>(numeric ? (Qt::AlignRight | Qt::AlignVCenter)
                                        : (Qt::AlignLeft | Qt::AlignVCenter));
    }

    if (role != Qt::DisplayRole)
        return {};

    switch (section) {
    case NameColumn:
        return QStringLiteral("名称");
    case IdColumn:
        return QStringLiteral("ID");
    case TypeColumn:
        return QStringLiteral("类型");
    case ValueColumn:
        return QStringLiteral("工程值");
    case QualityColumn:
        return QStringLiteral("质量");
    case TimestampColumn:
        return QStringLiteral("时标");
    case ForceColumn:
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
        refresh();
        return;
    }
    if (m_bank == nullptr)
        return;

    m_snapshots[row] = m_bank->snapshot(change.signalId);
    Q_EMIT dataChanged(index(row, 0), index(row, ColumnCount - 1));
}

} // namespace protosim::ui
