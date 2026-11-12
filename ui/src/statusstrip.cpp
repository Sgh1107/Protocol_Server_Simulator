// SPDX-License-Identifier: GPL-3.0-or-later
#include "protosim/ui/statusstrip.h"

#include "protosim/ui/theme.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QStyle>

namespace protosim::ui {
namespace {

QFrame *makeSeparator(QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setObjectName(QStringLiteral("statusSep"));
    line->setFrameShape(QFrame::VLine);
    line->setFixedSize(1, 12);
    return line;
}

QLabel *makeText(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("statusText"));
    return label;
}

QLabel *makeValue(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("statusValue"));
    return label;
}

} // namespace

StatusStrip::StatusStrip(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("statusStrip"));
    setFixedHeight(30);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(14, 0, 14, 0);
    layout->setSpacing(8);

    m_dot = new QLabel(this);
    m_dot->setObjectName(QStringLiteral("statusDot"));
    m_dot->setFixedSize(6, 6);
    m_dot->setProperty("online", true);

    m_state = makeText(QStringLiteral("端点 Running"), this);

    auto *clientGroup = new QWidget(this);
    auto *clientLayout = new QHBoxLayout(clientGroup);
    clientLayout->setContentsMargins(0, 0, 0, 0);
    clientLayout->setSpacing(5);
    clientLayout->addWidget(makeText(QStringLiteral("在线"), this));
    m_clients = makeValue(QStringLiteral("0/8"), this);
    clientLayout->addWidget(m_clients);

    auto *rxGroup = new QWidget(this);
    auto *rxLayout = new QHBoxLayout(rxGroup);
    rxLayout->setContentsMargins(0, 0, 0, 0);
    rxLayout->setSpacing(5);
    rxLayout->addWidget(makeText(QStringLiteral("收"), this));
    m_rx = makeValue(QStringLiteral("0"), this);
    rxLayout->addWidget(m_rx);

    auto *txGroup = new QWidget(this);
    auto *txLayout = new QHBoxLayout(txGroup);
    txLayout->setContentsMargins(0, 0, 0, 0);
    txLayout->setSpacing(5);
    txLayout->addWidget(makeText(QStringLiteral("发"), this));
    m_tx = makeValue(QStringLiteral("0"), this);
    txLayout->addWidget(m_tx);

    auto *errGroup = new QWidget(this);
    auto *errLayout = new QHBoxLayout(errGroup);
    errLayout->setContentsMargins(0, 0, 0, 0);
    errLayout->setSpacing(5);
    errLayout->addWidget(makeText(QStringLiteral("异常"), this));
    m_errors = makeValue(QStringLiteral("0"), this);
    errLayout->addWidget(m_errors);

    auto *frameGroup = new QWidget(this);
    auto *frameLayout = new QHBoxLayout(frameGroup);
    frameLayout->setContentsMargins(0, 0, 0, 0);
    frameLayout->setSpacing(5);
    frameLayout->addWidget(makeText(QStringLiteral("报文"), this));
    m_frames = makeValue(QStringLiteral("0"), this);
    frameLayout->addWidget(m_frames);

    auto *clockGroup = new QWidget(this);
    auto *clockLayout = new QHBoxLayout(clockGroup);
    clockLayout->setContentsMargins(0, 0, 0, 0);
    clockLayout->setSpacing(5);
    clockLayout->addWidget(makeText(QStringLiteral("仿真时钟"), this));
    m_clock = makeValue(QStringLiteral("T+0.000s"), this);
    clockLayout->addWidget(m_clock);
    clockLayout->addWidget(makeText(QStringLiteral("· tick"), this));
    m_ticks = makeValue(QStringLiteral("0"), this);
    clockLayout->addWidget(m_ticks);

    layout->addWidget(m_dot);
    layout->addWidget(m_state);
    layout->addWidget(makeSeparator(this));
    layout->addWidget(clientGroup);
    layout->addSpacing(10);
    layout->addWidget(rxGroup);
    layout->addSpacing(10);
    layout->addWidget(txGroup);
    layout->addSpacing(10);
    layout->addWidget(errGroup);
    layout->addStretch(1);
    layout->addWidget(frameGroup);
    layout->addWidget(makeSeparator(this));
    layout->addWidget(clockGroup);
}

void StatusStrip::setRunning(bool running)
{
    setEndpoint(running ? QStringLiteral("Running") : QStringLiteral("Paused"));
}

void StatusStrip::setEndpoint(const QString &state)
{
    const bool running = state == QLatin1String("Running");
    m_dot->setProperty("online", running);
    m_dot->style()->unpolish(m_dot);
    m_dot->style()->polish(m_dot);
    m_state->setText(QStringLiteral("端点 %1").arg(state));
}

void StatusStrip::setClients(int active, int max)
{
    m_clients->setText(QStringLiteral("%1/%2").arg(active).arg(max));
}

void StatusStrip::setCounters(quint64 rx, quint64 tx, quint64 errors, int frames)
{
    m_rx->setText(QString::number(rx));
    m_tx->setText(QString::number(tx));
    m_errors->setText(QString::number(errors));
    m_frames->setText(QString::number(frames));
}

void StatusStrip::setClock(qint64 virtualMs, qint64 ticks)
{
    m_clock->setText(QStringLiteral("T+%1s").arg(virtualMs / 1000.0, 0, 'f', 3));
    m_ticks->setText(QString::number(ticks));
}

} // namespace protosim::ui
