// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;

namespace protosim::ui {

/// 顶部命令栏。设计上只允许一个「主状态」（运行/暂停），其余控件降级为次级操作。
class CommandBar : public QWidget
{
    Q_OBJECT

public:
    explicit CommandBar(QWidget *parent = nullptr);

    void setRunning(bool running);
    void setSpeed(double speed);
    void setEndpoint(const QString &protocol, const QString &address, int port, bool online);
    void setDarkTheme(bool dark);
    void setScenarioName(const QString &name);

Q_SIGNALS:
    void runRequested();
    void pauseRequested();
    void stepRequested();
    void speedChanged(double multiplier);
    void exportRequested();
    void themeToggleRequested();

private:
    QPushButton *m_runButton = nullptr;
    QPushButton *m_pauseButton = nullptr;
    QPushButton *m_stepButton = nullptr;
    QComboBox *m_speedBox = nullptr;
    QLabel *m_chipDot = nullptr;
    QLabel *m_chipProto = nullptr;
    QLabel *m_chipAddr = nullptr;
    QPushButton *m_themeButton = nullptr;
    QLabel *m_brandSub = nullptr;
    bool m_updatingSpeed = false;
};

} // namespace protosim::ui
