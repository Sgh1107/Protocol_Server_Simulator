// SPDX-License-Identifier: GPL-3.0-or-later
#include "protosim/ui/theme.h"

#include <QApplication>
#include <QPalette>

namespace protosim::ui {
namespace {

QString rgba(const QColor &color, int alpha)
{
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(alpha);
}

} // namespace

ThemeTokens darkTokens()
{
    ThemeTokens t;
    t.windowBackground = QColor(QStringLiteral("#0f1114"));
    t.panelBackground = QColor(QStringLiteral("#15181c"));
    t.panelAlt = QColor(QStringLiteral("#191d22"));
    t.elevated = QColor(QStringLiteral("#1e2329"));
    t.well = QColor(QStringLiteral("#0b0d10"));
    t.hover = QColor(QStringLiteral("#1b2027"));

    t.text = QColor(QStringLiteral("#e9ecf1"));
    t.subtleText = QColor(QStringLiteral("#8a94a3"));
    t.mutedText = QColor(QStringLiteral("#5c6675"));

    t.border = QColor(QStringLiteral("#262b32"));
    t.borderSoft = QColor(QStringLiteral("#1d2127"));

    t.accent = QColor(QStringLiteral("#4d8ff0"));
    t.accentSoft = QColor::fromRgb(77, 143, 240, 41);
    t.selection = t.accentSoft;

    t.good = QColor(QStringLiteral("#57b98a"));
    t.warning = QColor(QStringLiteral("#d8a548"));
    t.bad = QColor(QStringLiteral("#e2595c"));

    t.gridLine = QColor(QStringLiteral("#232830"));
    return t;
}

ThemeTokens lightTokens()
{
    ThemeTokens t;
    t.windowBackground = QColor(QStringLiteral("#eef1f5"));
    t.panelBackground = QColor(QStringLiteral("#ffffff"));
    t.panelAlt = QColor(QStringLiteral("#f7f9fc"));
    t.elevated = QColor(QStringLiteral("#ffffff"));
    t.well = QColor(QStringLiteral("#f7f9fc"));
    t.hover = QColor(QStringLiteral("#f2f6fb"));

    t.text = QColor(QStringLiteral("#0f1620"));
    t.subtleText = QColor(QStringLiteral("#66738a"));
    t.mutedText = QColor(QStringLiteral("#9aa5b6"));

    t.border = QColor(QStringLiteral("#dde3ec"));
    t.borderSoft = QColor(QStringLiteral("#eaeff6"));

    t.accent = QColor(QStringLiteral("#1668e3"));
    t.accentSoft = QColor::fromRgb(22, 104, 227, 23);
    t.selection = t.accentSoft;

    t.good = QColor(QStringLiteral("#0f9c6c"));
    t.warning = QColor(QStringLiteral("#a9701a"));
    t.bad = QColor(QStringLiteral("#d33f3f"));

    t.gridLine = QColor(QStringLiteral("#e6ebf2"));
    return t;
}

QFont monoFont(int pointSize)
{
    QFont font;
    // Cascadia Mono 随 VS / Win11 提供；退到 Consolas 也能保证等宽
    font.setFamilies({QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas"),
                      QStringLiteral("Courier New")});
    font.setStyleHint(QFont::Monospace);
    font.setPointSize(pointSize);
    return font;
}

QFont uiFont(int pointSize, QFont::Weight weight)
{
    QFont font;
    font.setFamilies({QStringLiteral("Microsoft YaHei UI"), QStringLiteral("Segoe UI")});
    font.setStyleHint(QFont::SansSerif);
    font.setPointSize(pointSize);
    font.setWeight(weight);
    return font;
}

QString styleSheetFor(const ThemeTokens &t)
{
    QString qss = QStringLiteral(R"QSS(
QWidget {
    background-color: @window;
    color: @text;
    font-family: "Microsoft YaHei UI", "Segoe UI";
    font-size: 12px;
}
QMainWindow::separator { background: @border; width: 1px; height: 1px; }
QSplitter::handle { background: @border; }
QToolTip {
    background: @elev; color: @text; border: 1px solid @border;
    padding: 4px 7px; border-radius: 4px;
}

/* ---------------- 命令栏 ---------------- */
#commandBar { background: @panel; border-bottom: 1px solid @border; }
#brandName { font-size: 14px; font-weight: 600; }
#brandSub { color: @dim; font-size: 11px; padding-left: 9px; border-left: 1px solid @border; }

#segmented { background: @panelAlt; border: 1px solid @border; border-radius: 6px; }
#segmented QPushButton {
    background: transparent; border: 0; color: @dim;
    padding: 5px 14px; border-radius: 4px; font-size: 12px;
}
#segmented QPushButton:hover { color: @text; }
#segmented QPushButton:checked { background: @elev; color: @text; font-weight: 600; }
#segmented QPushButton#runButton:checked { color: @good; }

#fieldLabel { color: @dim; font-size: 11px; }
QComboBox#speedBox {
    background: @panelAlt; border: 1px solid @border; border-radius: 5px;
    padding: 3px 8px; color: @text; font-size: 11px; min-width: 58px;
}
QComboBox#speedBox::drop-down { border: 0; width: 16px; }
QComboBox#speedBox QAbstractItemView {
    background: @elev; border: 1px solid @border; selection-background-color: @selection;
    outline: none; padding: 2px;
}

#endpointChip { background: @panelAlt; border: 1px solid @border; border-radius: 5px; }
#chipProto { font-size: 11px; font-weight: 600; color: @text; }
#chipAddr { font-family: "Cascadia Mono", Consolas; font-size: 11px; color: @dim; }

QPushButton#ghostButton {
    background: @panelAlt; border: 1px solid @border; border-radius: 5px;
    color: @dim; padding: 4px 11px; font-size: 11px;
}
QPushButton#ghostButton:hover { color: @text; border-color: @dim; }
QPushButton#ghostButton:pressed { background: @selection; }

/* ---------------- 面板 ---------------- */
#panelCard { background: @panel; border: 1px solid @border; border-radius: 6px; }
#panelHead { border-bottom: 1px solid @border; }
#panelTitle { font-size: 12px; font-weight: 600; }
#panelHint { color: @mute; font-size: 11px; }

/* ---------------- 表格 ---------------- */
QTableView#signalTable, QTableView#frameTable {
    background: @panel; border: 0; outline: none;
    selection-background-color: transparent;
    gridline-color: @borderSoft;
}
QTableView#frameTable { background: @well; }
QHeaderView::section {
    background: @panel; color: @mute;
    border: 0; border-bottom: 1px solid @border;
    padding: 7px 12px; font-size: 11px; font-weight: 600;
}
QTableView#frameTable QHeaderView::section { background: @well; }
QHeaderView { background: @panel; border: 0; }

/* ---------------- 左栏 ---------------- */
#railPanel { background: @panel; border-right: 1px solid @border; }
#railCard { background: @panelAlt; border: 1px solid @border; border-radius: 6px; }
#railDeviceName { font-size: 12px; font-weight: 600; }
#railTag {
    color: @dim; font-size: 10px; border: 1px solid @border; border-radius: 3px;
    padding: 1px 5px; background: @panel;
}
#railMeta { color: @mute; font-family: "Cascadia Mono", Consolas; font-size: 10px; }
#railMicro { color: @mute; font-size: 10px; font-weight: 600; letter-spacing: 1px; }
#railCount { color: @dim; font-family: "Cascadia Mono", Consolas; font-size: 10px; }
#railCaret { color: @mute; font-size: 9px; }
#railDivider { background: @borderSoft; }
#chipDot { border-radius: 3px; background: @mute; }
#chipDot[online="true"] { background: @good; }
#statusDot { border-radius: 3px; background: @mute; }
#statusDot[online="true"] { background: @good; }
#railFoot {
    color: @mute; font-size: 10px; border-top: 1px solid @border;
    font-family: "Cascadia Mono", Consolas;
}
#railSignal {
    background: transparent; border: 0; border-radius: 4px;
    color: @dim; text-align: left; padding: 7px 8px; font-size: 12px;
}
#railSignal:hover { background: @hover; color: @text; }
#railSignal:checked { background: @selection; color: @text; font-weight: 600; }
#railAddr {
    color: @dim; font-family: "Cascadia Mono", Consolas; font-size: 10px;
    border: 1px solid @border; border-radius: 3px; padding: 1px 5px; background: @panel;
}

/* ---------------- 页签 ---------------- */
QTabWidget::pane { border: 0; }
QTabBar { background: transparent; }
QTabBar::tab {
    background: transparent; color: @dim; border: 0;
    padding: 9px 10px; margin-right: 2px; font-size: 12px;
}
QTabBar::tab:hover { color: @text; }
QTabBar::tab:selected { color: @text; font-weight: 600; border-bottom: 2px solid @accent; }

/* ---------------- 状态栏 ---------------- */
#statusStrip { background: @panel; border-top: 1px solid @border; }
#statusText { color: @dim; font-size: 11px; }
#statusValue { color: @text; font-family: "Cascadia Mono", Consolas; font-size: 11px; font-weight: 600; }
#statusSep { background: @border; }

QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }
QScrollBar::handle:vertical { background: @border; border-radius: 5px; min-height: 24px; }
QScrollBar::handle:vertical:hover { background: @dim; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 0; }
QScrollBar::handle:horizontal { background: @border; border-radius: 5px; min-width: 24px; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
)QSS");

    qss.replace(QStringLiteral("@window"), t.windowBackground.name());
    qss.replace(QStringLiteral("@panelAlt"), t.panelAlt.name());
    qss.replace(QStringLiteral("@panel"), t.panelBackground.name());
    qss.replace(QStringLiteral("@elev"), t.elevated.name());
    qss.replace(QStringLiteral("@well"), t.well.name());
    qss.replace(QStringLiteral("@hover"), t.hover.name());
    qss.replace(QStringLiteral("@text"), t.text.name());
    qss.replace(QStringLiteral("@dim"), t.subtleText.name());
    qss.replace(QStringLiteral("@mute"), t.mutedText.name());
    qss.replace(QStringLiteral("@borderSoft"), t.borderSoft.name());
    qss.replace(QStringLiteral("@border"), t.border.name());
    qss.replace(QStringLiteral("@accent"), t.accent.name());
    qss.replace(QStringLiteral("@selection"), rgba(t.accent, t.accentSoft.alpha()));
    qss.replace(QStringLiteral("@good"), t.good.name());
    return qss;
}

ThemeBridge::ThemeBridge(QObject *parent)
    : QObject(parent)
{
}

void ThemeBridge::setTokens(const ThemeTokens &tokens, bool dark)
{
    m_tokens = tokens;
    m_dark = dark;
    Q_EMIT changed();
}

void applyTheme(QApplication &app, ThemeBridge *bridge, bool dark)
{
    const ThemeTokens tokens = dark ? darkTokens() : lightTokens();

    app.setStyleSheet(styleSheetFor(tokens));

    QPalette palette;
    palette.setColor(QPalette::Window, tokens.windowBackground);
    palette.setColor(QPalette::WindowText, tokens.text);
    palette.setColor(QPalette::Base, tokens.panelBackground);
    palette.setColor(QPalette::AlternateBase, tokens.panelAlt);
    palette.setColor(QPalette::Text, tokens.text);
    palette.setColor(QPalette::Button, tokens.panelAlt);
    palette.setColor(QPalette::ButtonText, tokens.text);
    palette.setColor(QPalette::Highlight, tokens.accent);
    palette.setColor(QPalette::HighlightedText, tokens.text);
    palette.setColor(QPalette::ToolTipBase, tokens.elevated);
    palette.setColor(QPalette::ToolTipText, tokens.text);
    app.setPalette(palette);

    if (bridge)
        bridge->setTokens(tokens, dark);
}

} // namespace protosim::ui
