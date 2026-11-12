// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "protosim/databank.h"
#include "protosim/ui/theme.h"

#include <QAbstractTableModel>
#include <QHash>
#include <QStringList>
#include <QVector>

namespace protosim::ui {

/// 点表：直接读 DataBank 快照，不另做缓存层。
/// 每个 tick 只刷新变化的行，避免整表重绘拖慢 UI。
class SignalTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column {
        NameColumn,
        IdColumn,
        DeviceColumn,
        TypeColumn,
        ValueColumn,
        UnitColumn,
        QualityColumn,
        TimestampColumn,
        ForcedColumn,
        ColumnCount,
    };

    explicit SignalTableModel(DataBank *bank, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    /// 装载点表后整体重建。
    void refresh();

    QString signalIdAt(int row) const;

    void setTokens(const ThemeTokens &tokens);

public Q_SLOTS:
    void onSignalChanged(const protosim::SignalChange &change);

private:
    DataBank *m_bank;
    QStringList m_ids;
    QVector<SignalSnapshot> m_snapshots;
    QHash<QString, int> m_rowOf;
    ThemeTokens m_tokens = darkTokens();
};

} // namespace protosim::ui
