// SPDX-License-Identifier: GPL-3.0-or-later
#include "protosim/ui/proportionalheaderview.h"

#include <QResizeEvent>

namespace protosim::ui {
namespace {

constexpr int kDefaultMin = 40;
constexpr int kDefaultMax = 640;
/// 表头还没成型时（几十像素）不做分配，否则会先压到最小再弹开，肉眼看到一帧闪烁
constexpr int kMinUsableWidth = 240;

} // namespace

ProportionalHeaderView::ProportionalHeaderView(Qt::Orientation orientation, QWidget *parent)
    : QHeaderView(orientation, parent)
{
    setSectionResizeMode(QHeaderView::Interactive);
    setMinimumSectionSize(24);
    setSectionsMovable(false);
    setStretchLastSection(false);

    connect(this, &QHeaderView::sectionResized, this,
            [this](int index, int oldSize, int newSize) {
                if (m_adjusting)
                    return;
                rebalance(index, oldSize, newSize);
            });
}

void ProportionalHeaderView::setColumnBounds(int index, int minWidth, int maxWidth)
{
    if (index < 0)
        return;
    while (m_minWidths.size() <= index)
        m_minWidths.append(kDefaultMin);
    while (m_maxWidths.size() <= index)
        m_maxWidths.append(kDefaultMax);
    m_minWidths[index] = qMax(16, minWidth);
    m_maxWidths[index] = maxWidth;
}

int ProportionalHeaderView::minOf(int index) const
{
    return index < m_minWidths.size() ? m_minWidths.at(index) : kDefaultMin;
}

int ProportionalHeaderView::maxOf(int index) const
{
    return index < m_maxWidths.size() ? m_maxWidths.at(index) : kDefaultMax;
}

void ProportionalHeaderView::resizeEvent(QResizeEvent *event)
{
    if (!m_adjusting)
        applyWeights(event->size().width());

    QHeaderView::resizeEvent(event);
}

void ProportionalHeaderView::fitToWidth()
{
    applyWeights(width());
}

void ProportionalHeaderView::snapshotWeights()
{
    m_weights.clear();
    for (int i = 0; i < count(); ++i)
        m_weights.append(qMax(1, sectionSize(i)));
}

void ProportionalHeaderView::applyWeights(int targetWidth)
{
    if (targetWidth < kMinUsableWidth || count() == 0)
        return;

    if (m_weights.size() != count())
        snapshotWeights();

    int totalWeight = 0;
    for (int weight : m_weights)
        totalWeight += weight;
    if (totalWeight <= 0)
        return;

    m_adjusting = true;

    int used = 0;
    for (int i = 0; i < count(); ++i) {
        int target = static_cast<int>(qint64(targetWidth) * m_weights.at(i) / totalWeight);
        target = qBound(minOf(i), target, maxOf(i) > 0 ? maxOf(i) : targetWidth);
        if (i == count() - 1)
            target = qMax(minOf(i), targetWidth - used); // 最后一列吃掉取整误差
        used += target;
        if (sectionSize(i) != target)
            resizeSection(i, target);
    }

    m_adjusting = false;
    snapshotWeights();
}

void ProportionalHeaderView::rebalance(int index, int oldSize, int newSize)
{
    if (oldSize == newSize || index < 0 || index >= count())
        return;

    int othersMin = 0;
    for (int i = 0; i < count(); ++i) {
        if (i != index)
            othersMin += minOf(i);
    }

    // 用户拖动后 Qt 已把 newSize 写进去，总宽可能超出可视宽度。
    // 这里把"本列最多能占多宽"限制成 可视宽度 - 其它列的最小宽度之和，
    // 于是总宽天然等于可视宽度，且没人会被挤没。
    const int maxAllowed = qMax(minOf(index), width() - othersMin);
    int target = qBound(minOf(index), newSize, maxAllowed);
    if (maxOf(index) > 0)
        target = qMin(target, maxOf(index));

    const int delta = target - oldSize;

    m_adjusting = true;
    if (target != newSize)
        resizeSection(index, target);

    // 把 -delta 分摊给其它列：本列变宽→邻列按"可让出的余量"缩；本列变窄→邻列按"剩余空间"长
    if (delta != 0) {
        QVector<int> capacity(count(), 0);
        int capacityTotal = 0;
        for (int i = 0; i < count(); ++i) {
            if (i == index)
                continue;
            const int room = delta > 0
                ? qMax(0, sectionSize(i) - minOf(i))
                : qMax(0, (maxOf(i) > 0 ? maxOf(i) : width()) - sectionSize(i));
            capacity[i] = room;
            capacityTotal += room;
        }

        if (capacityTotal > 0) {
            int distributed = 0;
            int lastFlexible = -1;
            for (int i = 0; i < count(); ++i) {
                if (i != index && capacity.at(i) > 0)
                    lastFlexible = i;
            }

            for (int i = 0; i < count(); ++i) {
                if (i == index || capacity.at(i) <= 0)
                    continue;

                int amount = static_cast<int>(qint64(-delta) * capacity.at(i) / capacityTotal);
                if (i == lastFlexible)
                    amount = -delta - distributed; // 最后一列吃掉取整误差
                distributed += amount;

                const int wanted = sectionSize(i) + amount;
                const int upper = maxOf(i) > 0 ? maxOf(i) : width();
                resizeSection(i, qBound(minOf(i), wanted, upper));
            }
        }
    }

    m_adjusting = false;
    snapshotWeights();
}

} // namespace protosim::ui
