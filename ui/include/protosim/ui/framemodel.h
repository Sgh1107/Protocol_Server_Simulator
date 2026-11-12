// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "protosim/telemetrylog.h"
#include "protosim/ui/theme.h"

#include <QAbstractTableModel>
#include <QVector>

namespace protosim::ui {

/// 报文模型。窗口只保留最近 500 行（全量在 TelemetryLog 里、可导出），
/// 同时把 hex 预先切成「MBAP / 单元号 / 功能码 / 数据」四段交给委托上色。
class FrameTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column {
        TimeColumn,
        DirectionColumn,
        ClientColumn,
        FunctionColumn,
        HexColumn,
        SummaryColumn,
        ElapsedColumn,
        ColumnCount,
    };

    enum Role {
        SegmentsRole = Qt::UserRole + 1, // QVariantList<QStringList{text, kind}>
        ErrorRole,
        InboundRole,
        ElapsedUsRole,
        SummaryRole,
        ClientRole,
        TimestampMsRole,
    };

    explicit FrameTableModel(TelemetryLog *log, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    void setFilterSignal(const QString &signalId);
    void clearView();

public Q_SLOTS:
    void onRecordAppended(const protosim::FrameRecord &record);

private:
    TelemetryLog *m_log;
    QVector<FrameRecord> m_rows;
    QString m_filterSignalId;
    int m_maxRows = 500;
};

} // namespace protosim::ui
