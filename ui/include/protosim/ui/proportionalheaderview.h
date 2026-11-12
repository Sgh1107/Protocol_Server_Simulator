// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHeaderView>
#include <QVector>

namespace protosim::ui {

/// 表头：**每列可拖 + 总宽恒定 + 每列有上下限**。
///
/// 目标是表格的"填充式拖拽"：
///   - 拖宽某列时，其它列按各自可让出的余量分摊，总宽始终等于可视宽度（不出现横向滚动条）
///   - 每列都有最小/最大宽度，任何一列都不会被挤没，也不会无限变宽
///   - 窗口尺寸变化时按当前比例重新铺满
///
/// 为什么不用 Qt 原生模式：
///   - `Stretch` / `ResizeToContents` 会让该列**拖不动**（用户直接能感受到）
///   - 裸 `Interactive` 拖完会溢出可视宽度，出现横向滚动条
/// 所以这里统一 Interactive，再在 `sectionResized` 里做一次"邻列补偿"。
class ProportionalHeaderView : public QHeaderView
{
    Q_OBJECT

public:
    explicit ProportionalHeaderView(Qt::Orientation orientation, QWidget *parent = nullptr);

    /// 为某一列设定可拖范围（像素）。不设的列用默认上下限。
    void setColumnBounds(int index, int minWidth, int maxWidth);

    /// 按当前比例铺满可用宽度
    void fitToWidth();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void applyWeights(int targetWidth);
    void rebalance(int index, int oldSize, int newSize);
    void snapshotWeights();

    int minOf(int index) const;
    int maxOf(int index) const; // 0 表示不限

    QVector<int> m_weights;    // 各列相对宽度（窗口缩放时按它分配）
    QVector<int> m_minWidths;
    QVector<int> m_maxWidths;
    bool m_adjusting = false;  // 防止自己的 resizeSection 再次触发补偿
};

} // namespace protosim::ui
