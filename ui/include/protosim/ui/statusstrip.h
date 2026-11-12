// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QWidget>

class QLabel;

namespace protosim::ui {

/// 底部状态条：把原来的一句话拆成离散读数，数字全部等宽对齐。
class StatusStrip : public QWidget
{
    Q_OBJECT

public:
    explicit StatusStrip(QWidget *parent = nullptr);

    void setRunning(bool running);
    void setEndpoint(const QString &state);
    void setClients(int active, int max);
    void setCounters(quint64 rx, quint64 tx, quint64 errors, int frames);
    void setClock(qint64 virtualMs, qint64 ticks);

private:
    QLabel *m_dot = nullptr;
    QLabel *m_state = nullptr;
    QLabel *m_clients = nullptr;
    QLabel *m_rx = nullptr;
    QLabel *m_tx = nullptr;
    QLabel *m_errors = nullptr;
    QLabel *m_frames = nullptr;
    QLabel *m_clock = nullptr;
    QLabel *m_ticks = nullptr;
};

} // namespace protosim::ui
