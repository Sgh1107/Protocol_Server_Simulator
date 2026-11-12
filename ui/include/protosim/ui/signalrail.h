// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "protosim/configbundle.h"
#include "protosim/ui/theme.h"

#include <QVector>
#include <QWidget>

class QLabel;
class QVBoxLayout;

namespace protosim::ui {

class SignalRow; // 定义在 signalrail.cpp

/// 左侧设备栏：设备卡片 + 可点选的信号行（名称 + 寄存器地址胶囊）。
/// 用自绘行而不是 QTreeWidget —— 树控件做不出卡片和地址胶囊这两个细节。
class SignalRail : public QWidget
{
    Q_OBJECT

public:
    explicit SignalRail(QWidget *parent = nullptr);

    void setBundle(const ConfigBundle &bundle);
    void setSelected(const QString &signalId);
    void setTokens(const ThemeTokens &tokens);

Q_SIGNALS:
    void signalSelected(const QString &signalId);

private:
    void clearCards();

    QVBoxLayout *m_cardLayout = nullptr;
    QLabel *m_countLabel = nullptr;
    QLabel *m_footLabel = nullptr;
    QVector<SignalRow *> m_rows;
    ThemeTokens m_tokens = darkTokens();
    QString m_selectedId;
};

} // namespace protosim::ui
