// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "protosim/telemetrylog.h"
#include "protosim/ui/theme.h"

#include <QAbstractTableModel>
#include <QVector>

namespace protosim::ui {

/// 报文页签：逐帧落地到表格。默认只保留最近 500 帧，
/// 全量在 TelemetryLog 里（可导出文本），UI 只负责"看着不卡"。
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

    explicit FrameTableModel(TelemetryLog *log, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    void setTokens(const ThemeTokens &tokens);
    void setFilterSignal(const QString &signalId);
    void clearView();

public Q_SLOTS:
    void onRecordAppended(const protosim::FrameRecord &record);

private:
    TelemetryLog *m_log;
    QVector<FrameRecord> m_rows;
    QString m_filterSignalId;
    ThemeTokens m_tokens = darkTokens();
    int m_maxRows = 500;
};

} // namespace protosim::ui
