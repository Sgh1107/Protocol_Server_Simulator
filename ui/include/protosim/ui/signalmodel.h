// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "protosim/databank.h"
#include "protosim/ui/theme.h"

#include <QAbstractTableModel>
#include <QHash>
#include <QStringList>
#include <QVector>

namespace protosim::ui {

/// 点表模型。除了显示文本，还把「数值 / 量程 / 质量 / 强制」拆成独立角色，
/// 让委托能画量程条和胶囊，而不用去解析字符串。
class SignalTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column {
        NameColumn,
        IdColumn,
        TypeColumn,
        ValueColumn,
        QualityColumn,
        TimestampColumn,
        ForceColumn,
        ColumnCount,
    };

    enum Role {
        SignalIdRole = Qt::UserRole + 1,
        ValueRole,
        UnitRole,
        QualityRole,
        ForcedRole,
        RangeMinRole,
        RangeMaxRole,
        HasRangeRole,
        TimestampMsRole,
        CategoryRole,
    };

    explicit SignalTableModel(DataBank *bank, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    void refresh();
    QString signalIdAt(int row) const;
    int rowOf(const QString &signalId) const;

public Q_SLOTS:
    void onSignalChanged(const protosim::SignalChange &change);

private:
    DataBank *m_bank;
    QStringList m_ids;
    QVector<SignalSnapshot> m_snapshots;
    QHash<QString, int> m_rowOf;
};

} // namespace protosim::ui
