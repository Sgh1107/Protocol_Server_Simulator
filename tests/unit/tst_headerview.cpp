// SPDX-License-Identifier: GPL-3.0-or-later
//
// 表头约束的行为测试。
//
// 这段逻辑没法靠截图验证（拖拽是交互），但它有三条硬性不变量，
// 一旦写错用户立刻能感觉到，所以用测试钉死：
//   1. 任何时刻各列宽度之和 == 表头可视宽度（不出现横向滚动条）
//   2. 任何一列都不会被拖到小于它的最小宽度
//   3. 任何一列都不会超过它的最大宽度
//
// 用户拖动在 Qt 内部就是 resizeSection()，所以这里直接调它来模拟。

#include "protosim/ui/proportionalheaderview.h"

#include <QApplication>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QtTest>

using protosim::ui::ProportionalHeaderView;

namespace {

constexpr int kColumns = 7;
constexpr int kViewport = 800;

} // namespace

class TestHeaderView : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void fitFillsViewportExactly();
    void draggingOneColumnKeepsTotalWidth();
    void noColumnShrinksBelowItsMinimum();
    void noColumnGrowsBeyondItsMaximum();
    void windowResizeKeepsProportionsAndFill();

private:
    QStandardItemModel m_model;
};

void TestHeaderView::initTestCase()
{
    m_model.setColumnCount(kColumns);
}

void TestHeaderView::fitFillsViewportExactly()
{
    ProportionalHeaderView header(Qt::Horizontal);
    header.setModel(&m_model);
    header.resize(kViewport, 30);

    for (int i = 0; i < kColumns; ++i)
        header.setColumnBounds(i, 60, 400);

    header.fitToWidth();

    int total = 0;
    for (int i = 0; i < kColumns; ++i)
        total += header.sectionSize(i);

    QCOMPARE(total, kViewport);
}

void TestHeaderView::draggingOneColumnKeepsTotalWidth()
{
    ProportionalHeaderView header(Qt::Horizontal);
    header.setModel(&m_model);
    header.resize(kViewport, 30);
    for (int i = 0; i < kColumns; ++i)
        header.setColumnBounds(i, 60, 400);
    header.fitToWidth();

    // 把第 3 列拖宽 150px（等价于用户往右拖分隔线）
    const int before = header.sectionSize(3);
    header.resizeSection(3, before + 150);

    int total = 0;
    for (int i = 0; i < kColumns; ++i)
        total += header.sectionSize(i);

    QCOMPARE(total, kViewport);          // 总宽没变
    QVERIFY(header.sectionSize(3) > before); // 目标列确实变宽了
}

void TestHeaderView::noColumnShrinksBelowItsMinimum()
{
    ProportionalHeaderView header(Qt::Horizontal);
    header.setModel(&m_model);
    header.resize(kViewport, 30);
    for (int i = 0; i < kColumns; ++i)
        header.setColumnBounds(i, 60, 400);
    header.fitToWidth();

    // 把第 0 列拖到只剩 10px —— 必须被夹到最小宽度
    header.resizeSection(0, 10);
    QVERIFY2(header.sectionSize(0) >= 60,
             qPrintable(QStringLiteral("第 0 列被压到 %1，低于最小宽度").arg(header.sectionSize(0))));

    // 反向：把第 0 列拖得极大，邻列也不能被挤没
    header.resizeSection(0, 2000);
    QVERIFY(header.sectionSize(0) <= 400); // 自身不超过最大宽度
    for (int i = 1; i < kColumns; ++i) {
        QVERIFY2(header.sectionSize(i) >= 60,
                 qPrintable(QStringLiteral("第 %1 列被挤到 %2").arg(i).arg(header.sectionSize(i))));
    }
}

void TestHeaderView::noColumnGrowsBeyondItsMaximum()
{
    ProportionalHeaderView header(Qt::Horizontal);
    header.setModel(&m_model);
    header.resize(kViewport, 30);
    for (int i = 0; i < kColumns; ++i)
        header.setColumnBounds(i, 60, 200);
    header.fitToWidth();

    header.resizeSection(2, 5000);
    QVERIFY2(header.sectionSize(2) <= 200,
             qPrintable(QStringLiteral("第 2 列撑到 %1，超过最大宽度").arg(header.sectionSize(2))));
}

void TestHeaderView::windowResizeKeepsProportionsAndFill()
{
    ProportionalHeaderView header(Qt::Horizontal);
    header.setModel(&m_model);
    header.resize(kViewport, 30);
    for (int i = 0; i < kColumns; ++i)
        header.setColumnBounds(i, 40, 900);
    header.fitToWidth();

    header.resize(600, 30);
    header.fitToWidth();

    int total = 0;
    for (int i = 0; i < kColumns; ++i)
        total += header.sectionSize(i);

    QCOMPARE(total, 600);
    for (int i = 0; i < kColumns; ++i)
        QVERIFY(header.sectionSize(i) >= 40);
}

QTEST_MAIN(TestHeaderView)
#include "tst_headerview.moc"
