// SPDX-License-Identifier: GPL-3.0-or-later
#include "protosim/ui/mainwindow.h"

#include "protosim/ui/commandbar.h"
#include "protosim/ui/delegates.h"
#include "protosim/ui/framemodel.h"
#include "protosim/ui/proportionalheaderview.h"
#include "protosim/ui/signalrail.h"
#include "protosim/ui/signalmodel.h"
#include "protosim/ui/statusstrip.h"

#include <QApplication>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWidget>
#include <QSettings>
#include <QTimer>
#include <QSplitter>
#include <QTabWidget>
#include <QTableView>
#include <QTextStream>
#include <QVBoxLayout>

namespace protosim::ui {
namespace {

/// 面板头：标题 + 提示 + 右侧补充信息
QWidget *makePanelHead(const QString &title, const QString &hint, const QString &rightHint,
                       QWidget *parent)
{
    auto *head = new QWidget(parent);
    head->setObjectName(QStringLiteral("panelHead"));
    head->setFixedHeight(38);

    auto *layout = new QHBoxLayout(head);
    layout->setContentsMargins(14, 0, 14, 0);
    layout->setSpacing(10);

    auto *titleLabel = new QLabel(title, head);
    titleLabel->setObjectName(QStringLiteral("panelTitle"));
    layout->addWidget(titleLabel);

    auto *hintLabel = new QLabel(hint, head);
    hintLabel->setObjectName(QStringLiteral("panelHint"));
    layout->addWidget(hintLabel);

    layout->addStretch(1);

    if (!rightHint.isEmpty()) {
        auto *right = new QLabel(rightHint, head);
        right->setObjectName(QStringLiteral("panelHint"));
        layout->addWidget(right);
    }

    return head;
}

} // namespace

MainWindow::MainWindow(const Context &context, ThemeBridge *theme, QWidget *parent)
    : QMainWindow(parent)
    , m_context(context)
    , m_theme(theme)
{
    setWindowTitle(QStringLiteral("ProtoSim — 工业协议服务端模拟器"));
    resize(1280, 820);

    m_signalModel = new SignalTableModel(m_context.bank, this);
    m_frameModel = new FrameTableModel(m_context.log, this);

    buildUi();
    applyTokens();
    refreshStatus();

    if (m_context.engine != nullptr) {
        connect(m_context.engine, &SimulationEngine::ticked, this, &MainWindow::onTick);
        connect(m_context.engine, &SimulationEngine::stateChanged, this, [this](auto) {
            const bool running = m_context.engine->state() == SimulationEngine::State::Running;
            if (m_commandBar)
                m_commandBar->setRunning(running);
            if (m_status)
                m_status->setRunning(running);
        });
    }
    if (m_context.log != nullptr) {
        connect(m_context.log, &TelemetryLog::recordAppended, this,
                [this](const FrameRecord &) { refreshStatus(); });
    }

    restoreTableLayout();
    {
        // 无论是否恢复过，都要按当前窗口铺满一次：
        // 恢复的只是各列的相对比例，绝对宽度必须重新分配到可视宽度上。
        QTimer::singleShot(0, this, [this] {
            if (auto *header = qobject_cast<ProportionalHeaderView *>(
                    m_signalTable->horizontalHeader()))
                header->fitToWidth();
            if (auto *header = qobject_cast<ProportionalHeaderView *>(
                    m_frameTable->horizontalHeader()))
                header->fitToWidth();
        });
    }
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi()
{
    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---- 命令栏 ----
    m_commandBar = new CommandBar(central);
    root->addWidget(m_commandBar);

    connect(m_commandBar, &CommandBar::runRequested, this, [this] {
        if (m_context.engine)
            m_context.engine->start();
    });
    connect(m_commandBar, &CommandBar::pauseRequested, this, [this] {
        if (m_context.engine)
            m_context.engine->pause();
    });
    connect(m_commandBar, &CommandBar::stepRequested, this, [this] {
        if (!m_context.engine)
            return;
        m_context.engine->pause();
        m_context.engine->stepOnce();
    });
    connect(m_commandBar, &CommandBar::speedChanged, this, [this](double speed) {
        if (m_context.engine)
            m_context.engine->setSpeed(speed);
    });
    connect(m_commandBar, &CommandBar::exportRequested, this, &MainWindow::onExportFrames);
    connect(m_commandBar, &CommandBar::themeToggleRequested, this, &MainWindow::onThemeToggle);

    // ---- 主体：设备栏 + 右列 ----
    auto *splitter = new QSplitter(Qt::Horizontal, central);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(1);

    m_rail = new SignalRail(splitter);
    connect(m_rail, &SignalRail::signalSelected, this, &MainWindow::selectSignal);
    splitter->addWidget(m_rail);

    auto *right = new QWidget(splitter);
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(12, 12, 12, 0);
    rightLayout->setSpacing(12);
    rightLayout->addWidget(buildSignalPanel(), 2);
    rightLayout->addWidget(buildBottomPanel(), 3);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);

    root->addWidget(splitter, 1);

    // ---- 状态条 ----
    m_status = new StatusStrip(central);
    root->addWidget(m_status);

    setCentralWidget(central);
}

QWidget *MainWindow::buildSignalPanel()
{
    auto *card = new QWidget(this);
    card->setObjectName(QStringLiteral("panelCard"));
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    layout->addWidget(makePanelHead(QStringLiteral("点表"),
                                    QStringLiteral("按 (unit, 区) 绑定"),
                                    QStringLiteral("值下方细线 = 量程位置"), card));

    m_signalTable = new QTableView(card);
    m_signalTable->setObjectName(QStringLiteral("signalTable"));
    // 自定义表头：每一列都能拖（Qt 的 Stretch / ResizeToContents 会锁死拖动）
    m_signalTable->setHorizontalHeader(new ProportionalHeaderView(Qt::Horizontal, m_signalTable));
    m_signalTable->setModel(m_signalModel);
    m_signalTable->setItemDelegate(new SignalTableDelegate(m_signalTable));
    m_signalTable->setShowGrid(false);
    m_signalTable->setMouseTracking(true);
    m_signalTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_signalTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_signalTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_signalTable->setFrameShape(QFrame::NoFrame);
    m_signalTable->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

    m_signalTable->verticalHeader()->setVisible(false);
    m_signalTable->verticalHeader()->setDefaultSectionSize(50);

    auto *header = m_signalTable->horizontalHeader();
    header->setFixedHeight(34);
    header->setHighlightSections(false);
    header->setSectionsClickable(false);

    if (auto *sizedHeader = qobject_cast<ProportionalHeaderView *>(
            m_signalTable->horizontalHeader())) {
        sizedHeader->setColumnBounds(SignalTableModel::NameColumn, 90, 200);
        sizedHeader->setColumnBounds(SignalTableModel::IdColumn, 100, 240);
        sizedHeader->setColumnBounds(SignalTableModel::TypeColumn, 70, 120);
        sizedHeader->setColumnBounds(SignalTableModel::ValueColumn, 110, 420);
        sizedHeader->setColumnBounds(SignalTableModel::QualityColumn, 80, 130);
        sizedHeader->setColumnBounds(SignalTableModel::TimestampColumn, 90, 150);
        sizedHeader->setColumnBounds(SignalTableModel::ForceColumn, 60, 100);
    }
    m_signalTable->setColumnWidth(SignalTableModel::NameColumn, 120);
    m_signalTable->setColumnWidth(SignalTableModel::IdColumn, 140);
    m_signalTable->setColumnWidth(SignalTableModel::TypeColumn, 88);
    m_signalTable->setColumnWidth(SignalTableModel::QualityColumn, 96);
    m_signalTable->setColumnWidth(SignalTableModel::TimestampColumn, 104);
    m_signalTable->setColumnWidth(SignalTableModel::ForceColumn, 76);

    connect(m_signalTable->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this](const QItemSelection &, const QItemSelection &) {
                const auto rows = m_signalTable->selectionModel()->selectedRows();
                if (rows.isEmpty())
                    return;
                const QString signalId = m_signalModel->signalIdAt(rows.first().row());
                if (!signalId.isEmpty() && signalId != m_curveSignalId)
                    selectSignal(signalId);
            });

    if (auto *delegate = qobject_cast<SignalTableDelegate *>(m_signalTable->itemDelegate()))
        connect(delegate, &SignalTableDelegate::forceToggled, this, &MainWindow::toggleForce);

    layout->addWidget(m_signalTable, 1);
    return card;
}

QWidget *MainWindow::buildBottomPanel()
{
    auto *card = new QWidget(this);
    card->setObjectName(QStringLiteral("panelCard"));
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_tabs = new QTabWidget(card);
    m_tabs->setDocumentMode(true);

    // 报文页
    m_frameTable = new QTableView(m_tabs);
    m_frameTable->setObjectName(QStringLiteral("frameTable"));
    m_frameTable->setHorizontalHeader(new ProportionalHeaderView(Qt::Horizontal, m_frameTable));
    m_frameTable->setModel(m_frameModel);
    m_frameTable->setItemDelegate(new FrameTableDelegate(m_frameTable));
    m_frameTable->setShowGrid(false);
    m_frameTable->setMouseTracking(true);
    m_frameTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_frameTable->setFocusPolicy(Qt::NoFocus); // 只读观测窗，别画当前项高亮
    m_frameTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_frameTable->setFrameShape(QFrame::NoFrame);
    m_frameTable->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

    m_frameTable->verticalHeader()->setVisible(false);
    m_frameTable->verticalHeader()->setDefaultSectionSize(34);

    auto *frameHeader = m_frameTable->horizontalHeader();
    frameHeader->setFixedHeight(30);
    frameHeader->setHighlightSections(false);
    frameHeader->setSectionsClickable(false);

    if (auto *sizedHeader = qobject_cast<ProportionalHeaderView *>(
            m_frameTable->horizontalHeader())) {
        sizedHeader->setColumnBounds(FrameTableModel::TimeColumn, 76, 120);
        sizedHeader->setColumnBounds(FrameTableModel::DirectionColumn, 44, 80);
        sizedHeader->setColumnBounds(FrameTableModel::ClientColumn, 96, 200);
        sizedHeader->setColumnBounds(FrameTableModel::FunctionColumn, 28, 60);
        sizedHeader->setColumnBounds(FrameTableModel::HexColumn, 200, 900);
        sizedHeader->setColumnBounds(FrameTableModel::SummaryColumn, 90, 320);
        sizedHeader->setColumnBounds(FrameTableModel::ElapsedColumn, 56, 96);
    }
    m_frameTable->setColumnWidth(FrameTableModel::TimeColumn, 84);
    m_frameTable->setColumnWidth(FrameTableModel::DirectionColumn, 50);
    m_frameTable->setColumnWidth(FrameTableModel::ClientColumn, 110);
    m_frameTable->setColumnWidth(FrameTableModel::FunctionColumn, 36);
    m_frameTable->setColumnWidth(FrameTableModel::SummaryColumn, 112);
    m_frameTable->setColumnWidth(FrameTableModel::HexColumn, 320);
    m_frameTable->setColumnWidth(FrameTableModel::ElapsedColumn, 64);

    m_tabs->addTab(m_frameTable, QStringLiteral("报文"));

    // 曲线页
    m_curve = new QQuickWidget(m_tabs);
    m_curve->setResizeMode(QQuickWidget::SizeRootObjectToView);
    if (m_theme != nullptr)
        m_curve->rootContext()->setContextProperty(QStringLiteral("themeColors"), m_theme);

    // 必须"先连信号再 setSource"：qrc 里的 QML 是同步加载的，
    // 等 setSource 返回时 Ready 可能已经发过了，后连的槽永远收不到。
    connect(m_curve, &QQuickWidget::statusChanged, this, [this](QQuickWidget::Status status) {
        if (status != QQuickWidget::Ready) {
            if (status == QQuickWidget::Error) {
                for (const QQmlError &error : m_curve->errors())
                    ::fprintf(stderr, "QML 加载失败: %s\n",
                              error.toString().toUtf8().constData());
            }
            return;
        }
        m_curveReady = true;
        syncCurve();
    });

    m_curve->setSource(QUrl(QStringLiteral("qrc:/qml/CurveView.qml")));
    if (m_curve->status() == QQuickWidget::Ready) {
        m_curveReady = true;
        syncCurve();
    } else if (m_curve->status() == QQuickWidget::Error) {
        for (const QQmlError &error : m_curve->errors())
            ::fprintf(stderr, "QML 加载失败: %s\n", error.toString().toUtf8().constData());
    }

    m_tabs->addTab(m_curve, QStringLiteral("曲线"));

    auto *corner = new QLabel(QStringLiteral("最近 500 帧 · 完整记录可导出"), m_tabs);
    corner->setObjectName(QStringLiteral("panelHint"));
    corner->setContentsMargins(0, 0, 14, 0);
    m_tabs->setCornerWidget(corner, Qt::TopRightCorner);

    layout->addWidget(m_tabs, 1);
    return card;
}

void MainWindow::setBundle(const ConfigBundle &bundle)
{
    m_bundle = bundle;
    setWindowTitle(QStringLiteral("ProtoSim — %1").arg(bundle.meta.name));

    if (m_commandBar)
        m_commandBar->setScenarioName(bundle.meta.name);

    if (m_rail)
        m_rail->setBundle(bundle);

    if (m_signalModel)
        m_signalModel->refresh();
    if (m_frameModel)
        m_frameModel->setFilterSignal(QString());

    if (!bundle.signals.isEmpty()) {
        const QString first = bundle.signals.first().id;
        m_signalTable->selectRow(0);
        selectSignal(first);
    }

    if (m_context.adapter != nullptr) {
        const AdapterStats stats = m_context.adapter->stats();
        m_commandBar->setEndpoint(protocolKindToString(stats.kind),
                                  QStringLiteral("127.0.0.1"),
                                  stats.extra.value(QStringLiteral("port")).toInt(0),
                                  m_context.adapter->state() == AdapterState::Running);
    }

    refreshStatus();
}

void MainWindow::startSimulation()
{
    if (m_context.engine == nullptr)
        return;
    m_context.engine->start();
    if (m_commandBar)
        m_commandBar->setRunning(true);
}

void MainWindow::setDarkTheme(bool dark)
{
    m_dark = dark;
    if (m_commandBar)
        m_commandBar->setDarkTheme(dark);
    applyTokens();
}

void MainWindow::showCurveTab()
{
    if (m_tabs != nullptr)
        m_tabs->setCurrentIndex(1);
}

bool MainWindow::restoreTableLayout()
{
    bool restored = false;
    QSettings settings;
    settings.beginGroup(QStringLiteral("ui/layout-v2/columns"));
    if (settings.contains(QStringLiteral("signal")))
        m_signalTable->horizontalHeader()->restoreState(
            settings.value(QStringLiteral("signal")).toByteArray());
    if (settings.contains(QStringLiteral("frames")))
        m_frameTable->horizontalHeader()->restoreState(
            settings.value(QStringLiteral("frames")).toByteArray());
    settings.endGroup();

    settings.beginGroup(QStringLiteral("ui/layout-v2/window"));
    if (settings.contains(QStringLiteral("geometry"))) {
        restoreGeometry(settings.value(QStringLiteral("geometry")).toByteArray());
        restored = true;
    }
    settings.endGroup();

    return restored;
}

void MainWindow::saveTableLayout() const
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("ui/layout-v2/columns"));
    settings.setValue(QStringLiteral("signal"), m_signalTable->horizontalHeader()->saveState());
    settings.setValue(QStringLiteral("frames"), m_frameTable->horizontalHeader()->saveState());
    settings.endGroup();

    settings.beginGroup(QStringLiteral("ui/layout-v2/window"));
    settings.setValue(QStringLiteral("geometry"), saveGeometry());
    settings.endGroup();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveTableLayout();
    QMainWindow::closeEvent(event);
}

void MainWindow::applyTokens()
{
    const ThemeTokens tokens = m_dark ? darkTokens() : lightTokens();

    if (auto *delegate = qobject_cast<SignalTableDelegate *>(m_signalTable->itemDelegate()))
        delegate->setTokens(tokens);
    if (auto *delegate = qobject_cast<FrameTableDelegate *>(m_frameTable->itemDelegate()))
        delegate->setTokens(tokens);
    if (m_rail)
        m_rail->setTokens(tokens);

    if (m_signalTable)
        m_signalTable->viewport()->update();
    if (m_frameTable)
        m_frameTable->viewport()->update();
}

void MainWindow::toggleRunning()
{
    if (m_context.engine == nullptr)
        return;
    if (m_context.engine->state() == SimulationEngine::State::Running)
        m_context.engine->pause();
    else
        m_context.engine->start();
}

void MainWindow::toggleForce(const QString &signalId)
{
    if (m_context.bank == nullptr || m_context.pipeline == nullptr || signalId.isEmpty())
        return;

    const SignalSnapshot snapshot = m_context.bank->snapshot(signalId);
    const qint64 now = m_context.engine != nullptr ? m_context.engine->virtualTimeMs() : 0;

    if (snapshot.forced) {
        m_context.bank->clearForce(signalId, now);
        return;
    }

    WriteIntent intent;
    intent.signalId = signalId;
    intent.value = snapshot.value;
    intent.sourceProtocol = QStringLiteral("ui");
    intent.clientId = QStringLiteral("ui");
    intent.forced = true;
    m_context.pipeline->submit(intent, now);
}

void MainWindow::selectSignal(const QString &signalId)
{
    if (signalId.isEmpty())
        return;

    m_curveSignalId = signalId;

    if (m_rail)
        m_rail->setSelected(signalId);

    const int row = m_signalModel->rowOf(signalId);
    if (row >= 0 && m_signalTable->currentIndex().row() != row)
        m_signalTable->selectRow(row);

    syncCurve();
}

void MainWindow::syncCurve()
{
    if (!m_curveReady || m_curveSignalId.isEmpty() || m_context.bank == nullptr)
        return;

    QQuickItem *root = m_curve->rootObject();
    if (root == nullptr)
        return;

    const Signal definition = m_context.bank->definition(m_curveSignalId);

    // 纵轴用"工作带"而不是传感器量程：温度的行为是 15~35，量程却是 -40~120，
    // 用后者会把正弦压成一条平线，"离上下限还有多远"就看不出来了。
    double lo = definition.range.valid ? definition.range.min : 0.0;
    double hi = definition.range.valid ? definition.range.max : 100.0;
    const QJsonObject behavior = definition.behavior;
    if (behavior.contains(QStringLiteral("min")) && behavior.contains(QStringLiteral("max"))) {
        const double behaviorLo = behavior.value(QStringLiteral("min")).toDouble();
        const double behaviorHi = behavior.value(QStringLiteral("max")).toDouble();
        if (behaviorHi > behaviorLo) {
            lo = behaviorLo;
            hi = behaviorHi;
        }
    }

    QMetaObject::invokeMethod(root, "resetValues");
    root->setProperty("minValue", lo);
    root->setProperty("maxValue", hi);
    root->setProperty("labelName", definition.name);
    root->setProperty("labelUnit", definition.unit);
}

void MainWindow::onTick(qint64 virtualTimeMs, qint64 tickCount)
{
    if (m_status != nullptr)
        m_status->setClock(virtualTimeMs, tickCount);

    if (!m_curveReady || m_curveSignalId.isEmpty() || m_context.bank == nullptr)
        return;

    QQuickItem *root = m_curve->rootObject();
    if (root == nullptr)
        return;

    const SignalSnapshot snapshot = m_context.bank->snapshot(m_curveSignalId);
    QMetaObject::invokeMethod(root, "appendValue", Q_ARG(QVariant, snapshot.value));
}

void MainWindow::refreshStatus()
{
    if (m_status == nullptr)
        return;

    if (m_context.adapter != nullptr) {
        const AdapterStats stats = m_context.adapter->stats();
        m_status->setEndpoint(adapterStateToString(m_context.adapter->state()));
        m_status->setClients(stats.activeClients,
                             stats.extra.value(QStringLiteral("maxConnections")).toInt(0));
        m_status->setCounters(stats.framesRx, stats.framesTx, stats.errorFrames,
                              static_cast<int>(m_frameModel->rowCount()));
    }
}

void MainWindow::onExportFrames()
{
    if (m_context.log == nullptr)
        return;

    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出报文日志"), QStringLiteral("protosim-frames.txt"),
        QStringLiteral("文本文件 (*.txt);;所有文件 (*)"));
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), file.errorString());
        return;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << m_context.log->toText() << Qt::endl;
}

void MainWindow::onThemeToggle()
{
    m_dark = !m_dark;
    if (auto *app = qobject_cast<QApplication *>(QApplication::instance()))
        applyTheme(*app, m_theme, m_dark);
    if (m_commandBar)
        m_commandBar->setDarkTheme(m_dark);
    applyTokens();
}

} // namespace protosim::ui
