// SPDX-License-Identifier: GPL-3.0-or-later
#include "protosim/ui/theme.h"

#include <QApplication>
#include <QPalette>

namespace protosim::ui {

ThemeTokens darkTokens()
{
    ThemeTokens t;
    t.windowBackground = QColor(QStringLiteral("#15181D"));
    t.panelBackground = QColor(QStringLiteral("#1B1F26"));
    t.headerBackground = QColor(QStringLiteral("#20252E"));
    t.text = QColor(QStringLiteral("#E6E9EF"));
    t.subtleText = QColor(QStringLiteral("#8A93A3"));
    t.border = QColor(QStringLiteral("#2B313B"));
    t.accent = QColor(QStringLiteral("#4FA3FF"));
    t.selection = QColor(QStringLiteral("#26405E"));
    t.good = QColor(QStringLiteral("#4CC38A"));
    t.warning = QColor(QStringLiteral("#E2B33C"));
    t.bad = QColor(QStringLiteral("#E5674F"));
    t.hexBackground = QColor(QStringLiteral("#12161B"));
    t.gridLine = QColor(QStringLiteral("#2A3140"));
    return t;
}

ThemeTokens lightTokens()
{
    ThemeTokens t;
    t.windowBackground = QColor(QStringLiteral("#F4F5F7"));
    t.panelBackground = QColor(QStringLiteral("#FFFFFF"));
    t.headerBackground = QColor(QStringLiteral("#EDEFF3"));
    t.text = QColor(QStringLiteral("#1D2330"));
    t.subtleText = QColor(QStringLiteral("#6A7383"));
    t.border = QColor(QStringLiteral("#D8DCE3"));
    t.accent = QColor(QStringLiteral("#1F6FEB"));
    t.selection = QColor(QStringLiteral("#DCE8FB"));
    t.good = QColor(QStringLiteral("#1C8C5A"));
    t.warning = QColor(QStringLiteral("#A67612"));
    t.bad = QColor(QStringLiteral("#C0392B"));
    t.hexBackground = QColor(QStringLiteral("#FAFBFC"));
    t.gridLine = QColor(QStringLiteral("#E2E6EC"));
    return t;
}

QString styleSheetFor(const ThemeTokens &t)
{
    QString qss = QStringLiteral(R"QSS(
QWidget {
    background-color: @window;
    color: @text;
    font-family: "Microsoft YaHei UI", "Segoe UI", sans-serif;
    font-size: 12px;
}
QMainWindow::separator { background: @border; width: 1px; height: 1px; }

QToolBar {
    background: @panel;
    border: none;
    border-bottom: 1px solid @border;
    padding: 6px 8px;
    spacing: 8px;
}
QToolBar QToolButton {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 4px;
    padding: 4px 10px;
    color: @text;
}
QToolBar QToolButton:hover { background: @header; border-color: @border; }
QToolBar QToolButton:pressed { background: @selection; }
QToolBar QToolButton:disabled { color: @subtle; }

QPushButton {
    background: @header;
    border: 1px solid @border;
    border-radius: 4px;
    padding: 4px 12px;
}
QPushButton:hover { border-color: @accent; }
QPushButton:pressed { background: @selection; }

QComboBox, QLineEdit {
    background: @panel;
    border: 1px solid @border;
    border-radius: 4px;
    padding: 3px 6px;
    selection-background-color: @selection;
}
QComboBox::drop-down { border: none; width: 16px; }

QTableView {
    background: @panel;
    alternate-background-color: @header;
    gridline-color: @border;
    border: 1px solid @border;
    border-radius: 4px;
    selection-background-color: @selection;
    selection-color: @text;
}
QHeaderView::section {
    background: @header;
    color: @subtle;
    border: none;
    border-right: 1px solid @border;
    border-bottom: 1px solid @border;
    padding: 5px 8px;
    font-weight: 600;
}

QTabWidget::pane { border: 1px solid @border; border-radius: 4px; top: -1px; }
QTabBar::tab {
    background: transparent;
    color: @subtle;
    padding: 6px 14px;
    border-bottom: 2px solid transparent;
}
QTabBar::tab:selected { color: @text; border-bottom-color: @accent; }
QTabBar::tab:hover { color: @text; }

QTreeWidget, QTreeView {
    background: @panel;
    border: 1px solid @border;
    border-radius: 4px;
    outline: none;
}
QTreeWidget::item { padding: 3px 4px; }
QTreeWidget::item:selected { background: @selection; color: @text; }

QStatusBar {
    background: @panel;
    border-top: 1px solid @border;
    color: @subtle;
}
QStatusBar QLabel { color: @subtle; padding: 0 8px; }

QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }
QScrollBar::handle:vertical { background: @border; border-radius: 5px; min-height: 24px; }
QScrollBar::handle:vertical:hover { background: @subtle; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 0; }
QScrollBar::handle:horizontal { background: @border; border-radius: 5px; min-width: 24px; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }

QLabel#titleLabel { font-size: 13px; font-weight: 600; color: @text; }
QLabel#hintLabel { color: @subtle; }
QSplitter::handle { background: @border; }
)QSS");

    qss.replace(QStringLiteral("@window"), t.windowBackground.name());
    qss.replace(QStringLiteral("@panel"), t.panelBackground.name());
    qss.replace(QStringLiteral("@header"), t.headerBackground.name());
    qss.replace(QStringLiteral("@text"), t.text.name());
    qss.replace(QStringLiteral("@subtle"), t.subtleText.name());
    qss.replace(QStringLiteral("@border"), t.border.name());
    qss.replace(QStringLiteral("@accent"), t.accent.name());
    qss.replace(QStringLiteral("@selection"), t.selection.name());
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
    palette.setColor(QPalette::AlternateBase, tokens.headerBackground);
    palette.setColor(QPalette::Text, tokens.text);
    palette.setColor(QPalette::Button, tokens.headerBackground);
    palette.setColor(QPalette::ButtonText, tokens.text);
    palette.setColor(QPalette::Highlight, tokens.selection);
    palette.setColor(QPalette::HighlightedText, tokens.text);
    palette.setColor(QPalette::ToolTipBase, tokens.panelBackground);
    palette.setColor(QPalette::ToolTipText, tokens.text);
    app.setPalette(palette);

    if (bridge)
        bridge->setTokens(tokens, dark);
}

} // namespace protosim::ui
