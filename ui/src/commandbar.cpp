// SPDX-License-Identifier: GPL-3.0-or-later
#include "protosim/ui/commandbar.h"

#include "protosim/ui/theme.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QStyle>
#include <QPushButton>

namespace protosim::ui {
namespace {

/// 品牌标记：一个圆角方块 + 两条横线，纯手绘，不依赖图标资源。
class BrandMark : public QWidget
{
public:
    explicit BrandMark(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(16, 16);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(palette().color(QPalette::Highlight));
        painter.drawRoundedRect(rect(), 3, 3);

        QColor bar = palette().color(QPalette::Window);
        bar.setAlpha(216);
        painter.setBrush(bar);
        painter.drawRect(QRect(4, 4, 8, 2));
        bar.setAlpha(115);
        painter.setBrush(bar);
        painter.drawRect(QRect(4, 10, 8, 2));
    }
};

} // namespace

CommandBar::CommandBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("commandBar"));
    setFixedHeight(56);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 0, 16, 0);
    layout->setSpacing(14);

    // --- 品牌 ---
    auto *brand = new QWidget(this);
    auto *brandLayout = new QHBoxLayout(brand);
    brandLayout->setContentsMargins(0, 0, 0, 0);
    brandLayout->setSpacing(9);
    brandLayout->addWidget(new BrandMark(brand));

    auto *name = new QLabel(QStringLiteral("ProtoSim"), brand);
    name->setObjectName(QStringLiteral("brandName"));
    brandLayout->addWidget(name);

    m_brandSub = new QLabel(QStringLiteral("未加载场景"), brand);
    m_brandSub->setObjectName(QStringLiteral("brandSub"));
    brandLayout->addWidget(m_brandSub);

    brand->setMinimumWidth(230);
    layout->addWidget(brand);

    // --- 运行控制：只允许一个主状态 ---
    auto *segmented = new QWidget(this);
    segmented->setObjectName(QStringLiteral("segmented"));
    auto *segLayout = new QHBoxLayout(segmented);
    segLayout->setContentsMargins(2, 2, 2, 2);
    segLayout->setSpacing(2);

    m_runButton = new QPushButton(QStringLiteral("运行中"), segmented);
    m_runButton->setObjectName(QStringLiteral("runButton"));
    m_pauseButton = new QPushButton(QStringLiteral("暂停"), segmented);
    m_stepButton = new QPushButton(QStringLiteral("单步"), segmented);

    m_runButton->setCheckable(true);
    m_pauseButton->setCheckable(true);
    m_runButton->setCursor(Qt::PointingHandCursor);
    m_pauseButton->setCursor(Qt::PointingHandCursor);
    m_stepButton->setCursor(Qt::PointingHandCursor);

    auto *group = new QButtonGroup(this);
    group->setExclusive(true);
    group->addButton(m_runButton);
    group->addButton(m_pauseButton);
    m_runButton->setChecked(true);

    segLayout->addWidget(m_runButton);
    segLayout->addWidget(m_pauseButton);
    segLayout->addWidget(m_stepButton);
    layout->addWidget(segmented);

    // --- 倍速 ---
    auto *speedLabel = new QLabel(QStringLiteral("倍速"), this);
    speedLabel->setObjectName(QStringLiteral("fieldLabel"));
    layout->addWidget(speedLabel);

    m_speedBox = new QComboBox(this);
    m_speedBox->setObjectName(QStringLiteral("speedBox"));
    for (const QString &text : {QStringLiteral("0.5×"), QStringLiteral("1×"), QStringLiteral("2×"),
                                QStringLiteral("5×"), QStringLiteral("10×")}) {
        m_speedBox->addItem(text);
    }
    m_speedBox->setCurrentIndex(1);
    layout->addWidget(m_speedBox);

    layout->addStretch(1);

    // --- 端点胶囊 ---
    auto *chip = new QWidget(this);
    chip->setObjectName(QStringLiteral("endpointChip"));
    chip->setFixedHeight(26);
    auto *chipLayout = new QHBoxLayout(chip);
    chipLayout->setContentsMargins(10, 0, 10, 0);
    chipLayout->setSpacing(7);

    m_chipDot = new QLabel(chip);
    m_chipDot->setObjectName(QStringLiteral("chipDot"));
    m_chipDot->setFixedSize(6, 6);
    m_chipDot->setProperty("online", true);

    m_chipProto = new QLabel(QStringLiteral("modbus"), chip);
    m_chipProto->setObjectName(QStringLiteral("chipProto"));
    m_chipAddr = new QLabel(QStringLiteral("—"), chip);
    m_chipAddr->setObjectName(QStringLiteral("chipAddr"));

    chipLayout->addWidget(m_chipDot);
    chipLayout->addWidget(m_chipProto);
    chipLayout->addWidget(m_chipAddr);
    layout->addWidget(chip);

    auto *exportButton = new QPushButton(QStringLiteral("导出报文"), this);
    exportButton->setObjectName(QStringLiteral("ghostButton"));
    exportButton->setCursor(Qt::PointingHandCursor);
    layout->addWidget(exportButton);

    m_themeButton = new QPushButton(QStringLiteral("浅色"), this);
    m_themeButton->setObjectName(QStringLiteral("ghostButton"));
    m_themeButton->setCursor(Qt::PointingHandCursor);
    layout->addWidget(m_themeButton);

    connect(m_runButton, &QPushButton::clicked, this, &CommandBar::runRequested);
    connect(m_pauseButton, &QPushButton::clicked, this, &CommandBar::pauseRequested);
    connect(m_stepButton, &QPushButton::clicked, this, &CommandBar::stepRequested);
    connect(exportButton, &QPushButton::clicked, this, &CommandBar::exportRequested);
    connect(m_themeButton, &QPushButton::clicked, this, &CommandBar::themeToggleRequested);

    connect(m_speedBox, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (m_updatingSpeed)
            return;
        static const double speeds[] = {0.5, 1.0, 2.0, 5.0, 10.0};
        if (index >= 0 && index < 5)
            Q_EMIT speedChanged(speeds[index]);
    });
}

void CommandBar::setRunning(bool running)
{
    m_runButton->setChecked(running);
    m_pauseButton->setChecked(!running);
}

void CommandBar::setSpeed(double speed)
{
    static const double speeds[] = {0.5, 1.0, 2.0, 5.0, 10.0};
    for (int i = 0; i < 5; ++i) {
        if (qFuzzyCompare(speeds[i], speed)) {
            m_updatingSpeed = true;
            m_speedBox->setCurrentIndex(i);
            m_updatingSpeed = false;
            return;
        }
    }
}

void CommandBar::setEndpoint(const QString &protocol, const QString &address, int port, bool online)
{
    m_chipProto->setText(protocol);
    m_chipAddr->setText(port > 0 ? QStringLiteral("%1:%2").arg(address).arg(port)
                                 : QStringLiteral("未监听"));
    m_chipDot->setProperty("online", online);
    m_chipDot->style()->unpolish(m_chipDot);
    m_chipDot->style()->polish(m_chipDot);
}

void CommandBar::setDarkTheme(bool dark)
{
    m_themeButton->setText(dark ? QStringLiteral("浅色") : QStringLiteral("深色"));
}

void CommandBar::setScenarioName(const QString &name)
{
    m_brandSub->setText(name);
}

} // namespace protosim::ui
