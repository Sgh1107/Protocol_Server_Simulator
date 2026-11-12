// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "protosim/ui/theme.h"

#include <QStyledItemDelegate>

namespace protosim::ui {

/// 点表委托：值 + 量程条 + 质量胶囊 + 强制按钮，全部手绘。
///
/// 为什么不用 QSS 拼：量程条要按数值定位、胶囊要带色点、强制按钮要能在单元格里响应点击，
/// 这三件事 QSS 都做不到，只能落到 delegate。
class SignalTableDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit SignalTableDelegate(QObject *parent = nullptr);

    void setTokens(const ThemeTokens &tokens);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override;
    bool editorEvent(QEvent *event, QAbstractItemModel *model,
                     const QStyleOptionViewItem &option, const QModelIndex &index) override;

Q_SIGNALS:
    void forceToggled(const QString &signalId);

private:
    ThemeTokens m_tokens = darkTokens();
};

/// 报文委托：hex 按 MBAP / 单元号 / 功能码 / 数据 分色，异常帧整行淡红 + 左红条。
class FrameTableDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit FrameTableDelegate(QObject *parent = nullptr);

    void setTokens(const ThemeTokens &tokens);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override;

private:
    ThemeTokens m_tokens = darkTokens();
};

} // namespace protosim::ui
