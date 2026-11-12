// SPDX-License-Identifier: GPL-3.0-or-later
#include "protosim/ui/signalrail.h"

#include <QEnterEvent>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>

#include <functional>

namespace protosim::ui {

/// 一行信号：名称在左、寄存器地址胶囊在右。手绘是为了 hover / 选中态可控。
class SignalRow : public QWidget
{
public:
    SignalRow(const QString &signalId, const QString &name, const QString &address,
              QWidget *parent)
        : QWidget(parent)
        , m_signalId(signalId)
        , m_name(name)
        , m_address(address)
    {
        setFixedHeight(30);
        setCursor(Qt::PointingHandCursor);
        setMouseTracking(true);
        setAttribute(Qt::WA_Hover, true);
    }

    void setSelected(bool selected)
    {
        if (m_selected == selected)
            return;
        m_selected = selected;
        update();
    }

    void setTokens(const ThemeTokens &tokens)
    {
        m_tokens = tokens;
        update();
    }

    std::function<void(const QString &)> activated;

    QString signalId() const { return m_signalId; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);

        if (m_selected)
            painter.fillRect(rect(), m_tokens.accentSoft);
        else if (m_hover)
            painter.fillRect(rect(), m_tokens.hover);

        if (m_selected) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(m_tokens.accent);
            painter.drawRoundedRect(QRectF(0, 6, 2, height() - 12), 1, 1);
        }

        QFont nameFont = uiFont(9, m_selected ? QFont::DemiBold : QFont::Normal);
        painter.setFont(nameFont);
        painter.setPen(m_selected ? m_tokens.text : m_tokens.subtleText);

        QFont addressFont = monoFont(8);
        const int addressWidth = QFontMetrics(addressFont).horizontalAdvance(m_address) + 10;
        const QRect nameRect(10, 0, width() - addressWidth - 20, height());
        painter.drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter, m_name);

        const QRect chip(width() - 10 - addressWidth, (height() - 17) / 2, addressWidth, 17);
        painter.setPen(QPen(m_tokens.border, 1));
        painter.setBrush(m_tokens.panelBackground);
        painter.drawRoundedRect(chip, 3, 3);
        painter.setFont(addressFont);
        painter.setPen(m_tokens.subtleText);
        painter.drawText(chip, Qt::AlignCenter, m_address);
    }

    void enterEvent(QEnterEvent *event) override
    {
        m_hover = true;
        update();
        QWidget::enterEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        m_hover = false;
        update();
        QWidget::leaveEvent(event);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && activated)
            activated(m_signalId);
        QWidget::mousePressEvent(event);
    }

private:
    QString m_signalId;
    QString m_name;
    QString m_address;
    bool m_selected = false;
    bool m_hover = false;
    ThemeTokens m_tokens = darkTokens();
};

namespace {

QFrame *makeDivider(QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setObjectName(QStringLiteral("railDivider"));
    line->setFixedHeight(1);
    return line;
}

} // namespace

SignalRail::SignalRail(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("railPanel"));
    setFixedWidth(240);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 头部：设备树 / 计数
    auto *head = new QWidget(this);
    auto *headLayout = new QHBoxLayout(head);
    headLayout->setContentsMargins(14, 14, 14, 10);
    auto *title = new QLabel(QStringLiteral("设备树"), head);
    title->setObjectName(QStringLiteral("railMicro"));
    m_countLabel = new QLabel(QStringLiteral("—"), head);
    m_countLabel->setObjectName(QStringLiteral("railCount"));
    headLayout->addWidget(title);
    headLayout->addStretch(1);
    headLayout->addWidget(m_countLabel);
    layout->addWidget(head);

    m_cardLayout = new QVBoxLayout;
    m_cardLayout->setContentsMargins(10, 0, 10, 10);
    m_cardLayout->setSpacing(10);
    layout->addLayout(m_cardLayout);

    layout->addStretch(1);

    m_footLabel = new QLabel(QStringLiteral("—"), this);
    m_footLabel->setObjectName(QStringLiteral("railFoot"));
    m_footLabel->setContentsMargins(14, 10, 14, 10);
    layout->addWidget(m_footLabel);
}

void SignalRail::clearCards()
{
    m_rows.clear();
    while (QLayoutItem *item = m_cardLayout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
}

void SignalRail::setBundle(const ConfigBundle &bundle)
{
    clearCards();

    const int signalCount = bundle.signals.size();
    m_countLabel->setText(QStringLiteral("%1 台 · %2 点")
                              .arg(bundle.devices.size())
                              .arg(signalCount));
    m_footLabel->setText(QStringLiteral("%1 · seed %2")
                             .arg(bundle.meta.name, QString::number(bundle.meta.seed)));

    for (const Device &device : bundle.devices) {
        auto *card = new QFrame(this);
        card->setObjectName(QStringLiteral("railCard"));
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(0, 0, 0, 0);
        cardLayout->setSpacing(0);

        // 设备行
        auto *deviceRow = new QWidget(card);
        auto *deviceLayout = new QHBoxLayout(deviceRow);
        deviceLayout->setContentsMargins(10, 9, 10, 9);
        deviceLayout->setSpacing(8);

        auto *caret = new QLabel(QStringLiteral("▼"), deviceRow);
        caret->setObjectName(QStringLiteral("railCaret"));
        deviceLayout->addWidget(caret);

        auto *name = new QLabel(device.name.isEmpty() ? device.id : device.name, deviceRow);
        name->setObjectName(QStringLiteral("railDeviceName"));
        deviceLayout->addWidget(name, 1);

        auto *tag = new QLabel(device.type.isEmpty() ? QStringLiteral("DEV") : device.type,
                               deviceRow);
        tag->setObjectName(QStringLiteral("railTag"));
        deviceLayout->addWidget(tag);

        cardLayout->addWidget(deviceRow);
        cardLayout->addWidget(makeDivider(card));

        auto *meta = new QLabel(QStringLiteral("%1 · unit %2")
                                    .arg(device.id)
                                    .arg(bundle.signalsOfDevice(device.id).isEmpty()
                                             ? 1
                                             : bundle.signalsOfDevice(device.id).first().bindings
                                                   .value(0)
                                                   .unitId),
                                card);
        meta->setObjectName(QStringLiteral("railMeta"));
        meta->setContentsMargins(10, 6, 10, 8);
        cardLayout->addWidget(meta);

        auto *rowsHost = new QWidget(card);
        auto *rowsLayout = new QVBoxLayout(rowsHost);
        rowsLayout->setContentsMargins(4, 0, 4, 4);
        rowsLayout->setSpacing(0);

        for (const Signal &signal : bundle.signalsOfDevice(device.id)) {
            QString address;
            for (const ProtocolBinding &binding : signal.bindings) {
                if (binding.protocol != ProtocolKind::Modbus)
                    continue;
                static const QHash<QString, QString> abbreviations = {
                    {QStringLiteral("Coils"), QStringLiteral("CO")},
                    {QStringLiteral("DiscreteInputs"), QStringLiteral("DI")},
                    {QStringLiteral("HoldingRegisters"), QStringLiteral("HR")},
                    {QStringLiteral("InputRegisters"), QStringLiteral("IR")},
                };
                address = QStringLiteral("%1[%2]")
                              .arg(abbreviations.value(binding.registerType,
                                                       binding.registerType))
                              .arg(binding.address);
                break;
            }

            auto *row = new SignalRow(signal.id, signal.name, address, rowsHost);
            row->setTokens(m_tokens);
            row->setSelected(signal.id == m_selectedId);
            row->activated = [this](const QString &signalId) {
                setSelected(signalId);
                Q_EMIT signalSelected(signalId);
            };
            rowsLayout->addWidget(row);
            m_rows.append(row);
        }

        cardLayout->addWidget(rowsHost);
        m_cardLayout->addWidget(card);
    }
}

void SignalRail::setSelected(const QString &signalId)
{
    m_selectedId = signalId;
    for (SignalRow *row : m_rows)
        row->setSelected(row->signalId() == signalId);
    update();
}

void SignalRail::setTokens(const ThemeTokens &tokens)
{
    m_tokens = tokens;
    for (SignalRow *row : m_rows)
        row->setTokens(tokens);
}

} // namespace protosim::ui
