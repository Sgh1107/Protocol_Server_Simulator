// SPDX-License-Identifier: GPL-3.0-or-later
#include "protosim/ui/delegates.h"

#include "protosim/ui/framemodel.h"
#include "protosim/ui/signalmodel.h"

#include <QAbstractItemModel>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>

namespace protosim::ui {
namespace {

constexpr int kRowHeight = 50;
constexpr int kCellPadX = 12;

QColor withAlpha(const QColor &color, int alpha)
{
    QColor out = color;
    out.setAlpha(alpha);
    return out;
}

QColor qualityColor(Quality quality, const ThemeTokens &tokens)
{
    switch (quality) {
    case Quality::Good:
        return tokens.good;
    case Quality::OutOfRange:
    case Quality::Timeout:
        return tokens.warning;
    case Quality::Bad:
        return tokens.bad;
    case Quality::Unknown:
        break;
    }
    return tokens.mutedText;
}

void drawLabel(QPainter *painter, const QRect &rect, const QFont &font, const QColor &color,
               int alignment, const QString &text)
{
    painter->setFont(font);
    painter->setPen(color);
    painter->drawText(rect, alignment, text);
}

QString elide(const QFont &font, const QString &text, int width)
{
    if (width <= 0)
        return QString();
    return QFontMetrics(font).elidedText(text, Qt::ElideRight, width);
}

QRect forceButtonRect(const QRect &cell)
{
    QRect button(0, 0, 60, 20);
    button.moveCenter(cell.center());
    button.moveLeft(cell.left() + kCellPadX);
    return button;
}

} // namespace

// ------------------------------------------------------------------ 点表

SignalTableDelegate::SignalTableDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

void SignalTableDelegate::setTokens(const ThemeTokens &tokens)
{
    m_tokens = tokens;
}

QSize SignalTableDelegate::sizeHint(const QStyleOptionViewItem &option,
                                    const QModelIndex &index) const
{
    Q_UNUSED(option)
    Q_UNUSED(index)
    return QSize(120, kRowHeight);
}

void SignalTableDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                                const QModelIndex &index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::TextAntialiasing, true);

    const QRect cell = option.rect;
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;

    if (selected)
        painter->fillRect(cell, m_tokens.accentSoft);
    else if (hovered)
        painter->fillRect(cell, m_tokens.hover);

    // 选中行左侧 2px 强调条（只由第一列画，避免每列都画一条）
    if (selected && index.column() == SignalTableModel::NameColumn)
        painter->fillRect(QRect(cell.left(), cell.top() + 4, 2, cell.height() - 8), m_tokens.accent);

    painter->setPen(QPen(m_tokens.borderSoft, 1));
    painter->drawLine(cell.left(), cell.bottom(), cell.right(), cell.bottom());

    const QString text = index.data(Qt::DisplayRole).toString();
    const int vCenter = Qt::AlignVCenter;

    switch (index.column()) {
    case SignalTableModel::NameColumn: {
        QFont font = uiFont(10, QFont::DemiBold);
        drawLabel(painter, cell.adjusted(kCellPadX, 0, -kCellPadX, 0), font, m_tokens.text,
                  Qt::AlignLeft | vCenter, elide(font, text, cell.width() - 2 * kCellPadX));
        break;
    }
    case SignalTableModel::IdColumn:
    case SignalTableModel::TypeColumn: {
        QFont font = monoFont(9);
        drawLabel(painter, cell.adjusted(kCellPadX, 0, -kCellPadX, 0), font,
                  m_tokens.subtleText, Qt::AlignLeft | vCenter,
                  elide(font, text, cell.width() - 2 * kCellPadX));
        break;
    }
    case SignalTableModel::ValueColumn: {
        const QString unit = index.data(SignalTableModel::UnitRole).toString();
        const bool hasRange = index.data(SignalTableModel::HasRangeRole).toBool();
        const double value = index.data(SignalTableModel::ValueRole).toDouble();
        const auto quality = static_cast<Quality>(index.data(SignalTableModel::QualityRole).toInt());

        QFont unitFont = uiFont(8);
        const int unitWidth = unit.isEmpty()
            ? 0
            : QFontMetrics(unitFont).horizontalAdvance(unit) + 5;

        QRect textRect = cell.adjusted(kCellPadX, 0, -kCellPadX - unitWidth,
                                       hasRange ? -14 : 0);
        QFont valueFont = monoFont(13);
        valueFont.setWeight(QFont::DemiBold);
        const QColor valueColor =
            quality == Quality::Good ? m_tokens.text : qualityColor(quality, m_tokens);
        drawLabel(painter, textRect, valueFont, valueColor, Qt::AlignRight | vCenter, text);

        if (!unit.isEmpty()) {
            drawLabel(painter, QRect(cell.right() - kCellPadX - unitWidth + 5, textRect.top(),
                                     unitWidth, textRect.height()),
                      unitFont, m_tokens.mutedText, Qt::AlignRight | vCenter, unit);
        }

        if (hasRange) {
            const double lo = index.data(SignalTableModel::RangeMinRole).toDouble();
            const double hi = index.data(SignalTableModel::RangeMaxRole).toDouble();
            const double span = hi - lo;
            const double ratio =
                qFuzzyIsNull(span) ? 0.0 : qBound(0.0, (value - lo) / span, 1.0);

            // 列被拖宽时量程条不能无限拉长，否则读起来像分割线；
            // 限宽 150 并右对齐到数值下方，保持"这是某个值的位置"这层语义。
            const int trackWidth = qMin(cell.width() - 2 * kCellPadX, 150);
            const QRect track(cell.right() - kCellPadX - trackWidth, cell.bottom() - 11,
                              trackWidth, 3);
            painter->setPen(Qt::NoPen);
            painter->setBrush(m_tokens.border);
            painter->drawRoundedRect(track, 1.5, 1.5);

            if (ratio > 0.0) {
                QRectF fill(track.left(), track.top(), track.width() * ratio, track.height());
                painter->setBrush(withAlpha(m_tokens.accent, 102));
                painter->drawRoundedRect(fill, 1.5, 1.5);
            }

            // 两端刻度：让这条线读起来是「量程」，而不是数值下划线
            painter->setPen(QPen(withAlpha(m_tokens.mutedText, 115), 1));
            painter->drawLine(track.left(), track.top() - 2, track.left(), track.bottom() + 2);
            painter->drawLine(track.right(), track.top() - 2, track.right(), track.bottom() + 2);

            const QPointF knob(track.left() + track.width() * ratio, track.center().y());
            painter->setPen(QPen(m_tokens.panelBackground, 2.5));
            painter->setBrush(m_tokens.accent);
            painter->drawEllipse(knob, 3.5, 3.5);
        }
        break;
    }
    case SignalTableModel::QualityColumn: {
        const auto quality = static_cast<Quality>(index.data(SignalTableModel::QualityRole).toInt());
        const QColor color = qualityColor(quality, m_tokens);

        QFont font = uiFont(8);
        const int textWidth = QFontMetrics(font).horizontalAdvance(text);
        QRect pill(0, 0, textWidth + 26, 18);
        pill.moveCenter(cell.center());
        pill.moveLeft(cell.left() + kCellPadX);

        painter->setPen(QPen(withAlpha(color, 87), 1));
        painter->setBrush(withAlpha(color, 30));
        painter->drawRoundedRect(pill, 9, 9);

        painter->setPen(Qt::NoPen);
        painter->setBrush(color);
        painter->drawEllipse(QPointF(pill.left() + 10, pill.center().y()), 2.5, 2.5);

        drawLabel(painter, pill.adjusted(18, 0, -4, 0), font, color, Qt::AlignLeft | vCenter, text);
        break;
    }
    case SignalTableModel::TimestampColumn: {
        QFont font = monoFont(9);
        drawLabel(painter, cell.adjusted(kCellPadX, 0, -kCellPadX, 0), font,
                  m_tokens.subtleText, Qt::AlignRight | vCenter, text);
        break;
    }
    case SignalTableModel::ForceColumn: {
        const bool forced = index.data(SignalTableModel::ForcedRole).toBool();
        const QRect button = forceButtonRect(cell);

        // 三目运算符在这里会让 QColor 与 Qt::NoBrush 撞成 QBrush(QColor)，
        // 结果非强制态被填成实心块，所以拆成显式分支。
        painter->save();
        if (forced) {
            QColor fill = m_tokens.warning;
            fill.setAlpha(22);
            painter->setBrush(fill);
            painter->setPen(QPen(withAlpha(m_tokens.warning, 115), 1, Qt::SolidLine));
        } else {
            painter->setBrush(Qt::NoBrush);
            painter->setPen(QPen(m_tokens.border, 1, Qt::DashLine));
        }
        painter->drawRoundedRect(button, 4, 4);
        painter->restore();

        drawLabel(painter, button, uiFont(8), forced ? m_tokens.warning : m_tokens.mutedText,
                  Qt::AlignCenter, text);
        break;
    }
    default:
        break;
    }

    painter->restore();
}

bool SignalTableDelegate::editorEvent(QEvent *event, QAbstractItemModel *model,
                                      const QStyleOptionViewItem &option, const QModelIndex &index)
{
    Q_UNUSED(model)

    if (index.column() != SignalTableModel::ForceColumn)
        return false;

    if (event->type() != QEvent::MouseButtonRelease)
        return false;

    auto *mouseEvent = static_cast<QMouseEvent *>(event);
    if (!forceButtonRect(option.rect).adjusted(-4, -4, 4, 4).contains(mouseEvent->pos()))
        return false;

    Q_EMIT forceToggled(index.data(SignalTableModel::SignalIdRole).toString());
    return true;
}

// ------------------------------------------------------------------ 报文

FrameTableDelegate::FrameTableDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

void FrameTableDelegate::setTokens(const ThemeTokens &tokens)
{
    m_tokens = tokens;
}

QSize FrameTableDelegate::sizeHint(const QStyleOptionViewItem &option,
                                   const QModelIndex &index) const
{
    Q_UNUSED(option)
    Q_UNUSED(index)
    return QSize(90, 34);
}

void FrameTableDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                               const QModelIndex &index) const
{
    painter->save();
    painter->setRenderHint(QPainter::TextAntialiasing, true);

    const QRect cell = option.rect;
    const bool error = index.data(FrameTableModel::ErrorRole).toBool();
    const bool inbound = index.data(FrameTableModel::InboundRole).toBool();
    const bool hovered = option.state & QStyle::State_MouseOver;

    if (error)
        painter->fillRect(cell, withAlpha(m_tokens.bad, 20));
    else if (hovered)
        painter->fillRect(cell, m_tokens.hover);

    if (error && index.column() == FrameTableModel::TimeColumn)
        painter->fillRect(QRect(cell.left(), cell.top(), 2, cell.height()), m_tokens.bad);

    painter->setPen(QPen(m_tokens.borderSoft, 1));
    painter->drawLine(cell.left(), cell.bottom(), cell.right(), cell.bottom());

    const int vCenter = Qt::AlignVCenter;

    // 窄列（方向/客户端/FC）用更小的内边距：统一 12px 会把 "03" 这种两字符挤成半个
    const auto column = index.column();
    const bool narrow = column == FrameTableModel::DirectionColumn
        || column == FrameTableModel::ClientColumn
        || column == FrameTableModel::FunctionColumn;
    const int padLeft = narrow ? 9 : kCellPadX;
    const int padRight = narrow ? 4 : 10;
    const QRect inner = cell.adjusted(padLeft, 0, -padRight, 0);

    switch (index.column()) {
    case FrameTableModel::TimeColumn: {
        QFont font = monoFont(9);
        drawLabel(painter, inner, font, m_tokens.subtleText, Qt::AlignRight | vCenter,
                  index.data(Qt::DisplayRole).toString());
        break;
    }
    case FrameTableModel::DirectionColumn: {
        const QString text = index.data(Qt::DisplayRole).toString();
        const bool isRecord = text.startsWith(QChar(0x00B7));
        QFont font = uiFont(9, QFont::DemiBold);
        const QColor color = isRecord ? m_tokens.mutedText
                                      : (inbound ? m_tokens.accent : m_tokens.mutedText);
        drawLabel(painter, inner, font, color, Qt::AlignLeft | vCenter, text);
        break;
    }
    case FrameTableModel::ClientColumn: {
        QFont font = monoFont(8);
        drawLabel(painter, inner, font, m_tokens.subtleText, Qt::AlignLeft | vCenter,
                  elide(font, index.data(Qt::DisplayRole).toString(), inner.width()));
        break;
    }
    case FrameTableModel::FunctionColumn: {
        QFont font = monoFont(9);
        painter->setFont(font);
        painter->setPen(m_tokens.text);
        painter->drawText(inner, Qt::AlignLeft | vCenter, index.data(Qt::DisplayRole).toString());
        break;
    }
    case FrameTableModel::HexColumn: {
        const QVariantList segments = index.data(FrameTableModel::SegmentsRole).toList();
        QFont font = monoFont(9);
        painter->setFont(font);
        const QFontMetrics metrics(font);

        int x = inner.left();
        const int limit = inner.right();
        for (const QVariant &segment : segments) {
            const QStringList pair = segment.toStringList();
            if (pair.size() != 2)
                continue;

            const QString &text = pair.at(0);
            const QString &kind = pair.at(1);

            QColor color = m_tokens.text;
            if (kind == QLatin1String("mbap"))
                color = m_tokens.mutedText;
            else if (kind == QLatin1String("unit"))
                color = m_tokens.subtleText;
            else if (kind == QLatin1String("fc"))
                color = m_tokens.accent;
            else if (kind == QLatin1String("err"))
                color = m_tokens.bad;

            const int width = metrics.horizontalAdvance(text);
            if (x + width <= limit) {
                painter->setPen(color);
                painter->drawText(QPoint(x, cell.center().y() + metrics.ascent() / 2 - 1), text);
                x += width + 7;
            } else {
                // 放不下就截断并画省略号，避免整列糊在一起
                const QString tail = metrics.elidedText(text, Qt::ElideRight, qMax(0, limit - x));
                painter->setPen(color);
                painter->drawText(QPoint(x, cell.center().y() + metrics.ascent() / 2 - 1), tail);
                break;
            }
        }
        break;
    }
    case FrameTableModel::SummaryColumn: {
        QFont font = uiFont(9);
        drawLabel(painter, inner, font, m_tokens.subtleText, Qt::AlignLeft | vCenter,
                  elide(font, index.data(Qt::DisplayRole).toString(), inner.width()));
        break;
    }
    case FrameTableModel::ElapsedColumn: {
        QFont font = monoFont(8);
        drawLabel(painter, inner, font, m_tokens.mutedText, Qt::AlignRight | vCenter,
                  index.data(Qt::DisplayRole).toString());
        break;
    }
    default:
        break;
    }

    painter->restore();
}

} // namespace protosim::ui
