// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QFont>
#include <QObject>
#include <QString>

class QApplication;

namespace protosim::ui {

/// 一套配色令牌。Widgets 走 QSS、QML 走 ThemeBridge，两边都从这里取色，
/// 保证深浅主题切换时不串色。命名与 docs/design/ui-prototype-live.html 一一对应。
struct ThemeTokens
{
    QColor windowBackground; // --bg
    QColor panelBackground;  // --panel
    QColor panelAlt;         // --panel-2
    QColor elevated;         // --elev
    QColor well;             // --well（报文井）
    QColor hover;

    QColor text;
    QColor subtleText; // --dim
    QColor mutedText;  // --mute

    QColor border;     // --line
    QColor borderSoft; // --line-soft

    QColor accent;
    QColor accentSoft;
    QColor selection;

    QColor good;
    QColor warning;
    QColor bad;

    QColor gridLine;
};

ThemeTokens darkTokens();
ThemeTokens lightTokens();

/// 等宽字体：数值、hex、地址一律用它，保证列对齐（tabular figures）。
QFont monoFont(int pointSize = 10);
QFont uiFont(int pointSize = 9, QFont::Weight weight = QFont::Normal);

QString styleSheetFor(const ThemeTokens &tokens);

/// 供 QML 使用的桥：把同一套令牌暴露成 String 属性。
class ThemeBridge : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString background READ background NOTIFY changed)
    Q_PROPERTY(QString text READ text NOTIFY changed)
    Q_PROPERTY(QString subtleText READ subtleText NOTIFY changed)
    Q_PROPERTY(QString accent READ accent NOTIFY changed)
    Q_PROPERTY(QString grid READ grid NOTIFY changed)
    Q_PROPERTY(QString warning READ warning NOTIFY changed)
    Q_PROPERTY(bool dark READ dark NOTIFY changed)

public:
    explicit ThemeBridge(QObject *parent = nullptr);

    void setTokens(const ThemeTokens &tokens, bool dark);

    QString background() const { return m_tokens.well.name(); }
    QString text() const { return m_tokens.text.name(); }
    QString subtleText() const { return m_tokens.mutedText.name(); }
    QString accent() const { return m_tokens.accent.name(); }
    QString grid() const { return m_tokens.gridLine.name(); }
    QString warning() const { return m_tokens.warning.name(); }
    bool dark() const { return m_dark; }

Q_SIGNALS:
    void changed();

private:
    ThemeTokens m_tokens = darkTokens();
    bool m_dark = true;
};

/// 应用主题：QSS + QPalette + QML 桥，三处一起改，避免只在半边生效。
void applyTheme(QApplication &app, ThemeBridge *bridge, bool dark);

} // namespace protosim::ui
