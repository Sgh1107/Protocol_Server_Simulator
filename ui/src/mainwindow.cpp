// SPDX-License-Identifier: GPL-3.0-or-later
#include "protosim/ui/mainwindow.h"

#include "protosim/ui/framemodel.h"
#include "protosim/ui/signalmodel.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QQuickItem>

#include <cstdio>
#include <QQuickWidget>
#include <QQmlContext>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableView>
#include <QTextStream>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace protosim::ui {

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
    buildToolBar();
    buildStatusBar();
    applyTokens();

    if (m_context.engine != nullptr) {
        connect(m_context.engine, &SimulationEngine::ticked, this, &MainWindow::onTick);
        connect(m_context.engine, &SimulationEngine::stateChanged, this, [this](auto) {
            const bool running = m_context.engine->state() == SimulationEngine::State::Running;
            if (m_runAction)
                m_runAction->setText(running ? QStringLiteral("暂停") : QStringLiteral("启动"));
        });
    }
    if (m_context.log != nullptr) {
        connect(m_context.log, &TelemetryLog::recordAppended, this,
                [this](const FrameRecord &) { refreshEndpointStatus(); });
    }

    refreshEndpointStatus();
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi()
{
    // --- 左侧设备树 ---
    auto *dock = new QDockWidget(QStringLiteral("设备树"), this);
    dock->setObjectName(QStringLiteral("deviceDock"));
    dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);

    m_deviceTree = new QTreeWidget(dock);
    m_deviceTree->setHeaderLabels({QStringLiteral("设备 / 信号"), QStringLiteral("地址")});
    m_deviceTree->header()->setStretchLastSection(false);
    m_deviceTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_deviceTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_deviceTree->setRootIsDecorated(true);

    dock->setWidget(m_deviceTree);
    dock->setMinimumWidth(280);
    addDockWidget(Qt::LeftDockWidgetArea, dock);

    // --- 点表 ---
    auto *signalGroup = new QGroupBox(QStringLiteral("点表"), this);
    auto *signalLayout = new QVBoxLayout(signalGroup);
    signalLayout->setContentsMargins(10, 16, 10, 10);

    m_signalTable = new QTableView(signalGroup);
    m_signalTable->setModel(m_signalModel);
    m_signalTable->setAlternatingRowColors(true);
    m_signalTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_signalTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_signalTable->verticalHeader()->setVisible(false);
    m_signalTable->verticalHeader()->setDefaultSectionSize(26);
    m_signalTable->horizontalHeader()->setStretchLastSection(false);
    m_signalTable->horizontalHeader()->setSectionResizeMode(SignalTableModel::ValueColumn,
                                                            QHeaderView::Stretch);
    m_signalTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    signalLayout->addWidget(m_signalTable);

    connect(m_signalTable->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this](const QItemSelection &, const QItemSelection &) { onSelectionChanged(); });

    // --- 底部页签：报文 / 曲线 ---
    m_bottomTabs = new QTabWidget(this);

    auto *framePage = new QWidget(m_bottomTabs);
    auto *frameLayout = new QVBoxLayout(framePage);
    frameLayout->setContentsMargins(10, 10, 10, 10);
    frameLayout->setSpacing(8);

    auto *filterRow = new QHBoxLayout;
    filterRow->addWidget(new QLabel(QStringLiteral("按信号过滤："), framePage));
    m_filterCombo = new QComboBox(framePage);
    m_filterCombo->addItem(QStringLiteral("全部"), QString());
    m_filterCombo->setMinimumWidth(180);
    filterRow->addWidget(m_filterCombo);
    filterRow->addStretch(1);
    auto *hint = new QLabel(QStringLiteral("最近 500 帧 · 完整记录可导出"), framePage);
    hint->setObjectName(QStringLiteral("hintLabel"));
    filterRow->addWidget(hint);
    frameLayout->addLayout(filterRow);

    m_frameTable = new QTableView(framePage);
    m_frameTable->setModel(m_frameModel);
    m_frameTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_frameTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_frameTable->verticalHeader()->setVisible(false);
    m_frameTable->verticalHeader()->setDefaultSectionSize(24);
    m_frameTable->horizontalHeader()->setSectionResizeMode(FrameTableModel::HexColumn,
                                                           QHeaderView::Stretch);
    m_frameTable->horizontalHeader()->setSectionResizeMode(FrameTableModel::SummaryColumn,
                                                           QHeaderView::Stretch);
    frameLayout->addWidget(m_frameTable);

    connect(m_filterCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        const QString signalId = m_filterCombo->itemData(index).toString();
        m_frameModel->setFilterSignal(signalId);
    });

    m_bottomTabs->addTab(framePage, QStringLiteral("报文"));

    m_curveWidget = new QQuickWidget(m_bottomTabs);
    m_curveWidget->setResizeMode(QQuickWidget::SizeRootObjectToView);
    if (m_theme != nullptr)
        m_curveWidget->rootContext()->setContextProperty(QStringLiteral("themeColors"), m_theme);
    connect(m_curveWidget, &QQuickWidget::statusChanged, this, [this](QQuickWidget::Status status) {
        if (status == QQuickWidget::Ready) {
            syncCurveLabel();
            return;
        }
        if (status != QQuickWidget::Error)
            return;
        for (const QQmlError &error : m_curveWidget->errors()) {
            ::fprintf(stderr, "QML 加载失败: %s\n", error.toString().toUtf8().constData());
        }
    });
    m_curveWidget->setSource(QUrl(QStringLiteral("qrc:/qml/CurveView.qml")));
    if (m_curveWidget->status() == QQuickWidget::Error) {
        ::fprintf(stderr, "曲线视图加载失败，改用占位提示\n");
    }
    m_bottomTabs->addTab(m_curveWidget, QStringLiteral("曲线"));

    // --- 主分割 ---
    auto *splitter = new QSplitter(Qt::Vertical, this);
    splitter->addWidget(signalGroup);
    splitter->addWidget(m_bottomTabs);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);

    auto *central = new QWidget(this);
    auto *centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(10, 6, 10, 10);
    centralLayout->addWidget(splitter);
    setCentralWidget(central);
}

void MainWindow::buildToolBar()
{
    auto *toolbar = addToolBar(QStringLiteral("主工具栏"));
    toolbar->setObjectName(QStringLiteral("mainToolBar"));
    toolbar->setMovable(false);

    m_runAction = toolbar->addAction(QStringLiteral("启动"));
    connect(m_runAction, &QAction::triggered, this, &MainWindow::onToggleRun);

    m_stepAction = toolbar->addAction(QStringLiteral("单步"));
    connect(m_stepAction, &QAction::triggered, this, &MainWindow::onStep);

    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(QStringLiteral(" 倍速 "), toolbar));
    m_speedCombo = new QComboBox(toolbar);
    for (const QString &text : {QStringLiteral("0.5×"), QStringLiteral("1×"), QStringLiteral("2×"),
                                QStringLiteral("5×"), QStringLiteral("10×")}) {
        m_speedCombo->addItem(text);
    }
    m_speedCombo->setCurrentIndex(1);
    connect(m_speedCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onSpeedChanged);
    toolbar->addWidget(m_speedCombo);

    toolbar->addSeparator();
    auto *exportAction = toolbar->addAction(QStringLiteral("导出报文"));
    connect(exportAction, &QAction::triggered, this, &MainWindow::onExportFrames);

    toolbar->addSeparator();
    m_themeAction = toolbar->addAction(QStringLiteral("浅色主题"));
    connect(m_themeAction, &QAction::triggered, this, &MainWindow::onThemeToggled);
}

void MainWindow::buildStatusBar()
{
    m_endpointLabel = new QLabel(QStringLiteral("端点：未启动"), this);
    m_clockLabel = new QLabel(QStringLiteral("仿真时钟 0.000 s"), this);
    m_frameCountLabel = new QLabel(QStringLiteral("报文 0"), this);

    statusBar()->addWidget(m_endpointLabel, 1);
    statusBar()->addPermanentWidget(m_frameCountLabel);
    statusBar()->addPermanentWidget(m_clockLabel);
}

void MainWindow::applyTokens()
{
    const ThemeTokens tokens = m_dark ? darkTokens() : lightTokens();
    if (m_signalModel)
        m_signalModel->setTokens(tokens);
    if (m_frameModel)
        m_frameModel->setTokens(tokens);
}

void MainWindow::setBundle(const ConfigBundle &bundle)
{
    m_bundle = bundle;
    setWindowTitle(QStringLiteral("ProtoSim — %1").arg(bundle.meta.name));

    m_deviceTree->clear();
    m_filterCombo->blockSignals(true);
    m_filterCombo->clear();
    m_filterCombo->addItem(QStringLiteral("全部"), QString());

    for (const Device &device : bundle.devices) {
        auto *deviceItem = new QTreeWidgetItem(m_deviceTree);
        deviceItem->setText(0, device.name.isEmpty() ? device.id : device.name);
        deviceItem->setText(1, device.type.isEmpty() ? device.id : device.type);
        deviceItem->setExpanded(true);

        for (const Signal &signal : bundle.signalsOfDevice(device.id)) {
            auto *signalItem = new QTreeWidgetItem(deviceItem);
            signalItem->setText(0, QStringLiteral("%1（%2）").arg(signal.name, signal.id));
            const ProtocolBinding *modbusBinding = nullptr;
            for (const ProtocolBinding &binding : signal.bindings) {
                if (binding.protocol == ProtocolKind::Modbus) {
                    modbusBinding = &binding;
                    break;
                }
            }
            if (modbusBinding != nullptr) {
                // 用 PLC 惯用简称，避免 "HoldingRegisters[0]" 把列撑爆被截断
                static const QHash<QString, QString> abbreviations = {
                    {QStringLiteral("Coils"), QStringLiteral("CO")},
                    {QStringLiteral("DiscreteInputs"), QStringLiteral("DI")},
                    {QStringLiteral("HoldingRegisters"), QStringLiteral("HR")},
                    {QStringLiteral("InputRegisters"), QStringLiteral("IR")},
                };
                const QString area = abbreviations.value(modbusBinding->registerType,
                                                         modbusBinding->registerType);
                signalItem->setText(1, QStringLiteral("%1[%2]").arg(area).arg(modbusBinding->address));
                signalItem->setToolTip(0, QStringLiteral("%1 → %2[%3]，占 %4 个寄存器")
                                              .arg(signal.id, modbusBinding->registerType,
                                                   QString::number(modbusBinding->address),
                                                   QString::number(modbusBinding->count)));
            }
            signalItem->setData(0, Qt::UserRole, signal.id);
        }
    }

    for (const Signal &signal : bundle.signals)
        m_filterCombo->addItem(QStringLiteral("%1（%2）").arg(signal.name, signal.id), signal.id);
    m_filterCombo->blockSignals(false);

    if (m_signalModel)
        m_signalModel->refresh();
    if (m_frameModel)
        m_frameModel->setFilterSignal(QString());

    // 默认把第一个信号挂到曲线
    if (!bundle.signals.isEmpty() && m_curveSignalId.isEmpty()) {
        m_curveSignalId = bundle.signals.first().id;
        syncCurveLabel();
    }
}

void MainWindow::setDarkTheme(bool dark)
{
    m_dark = dark;
    if (m_themeAction)
        m_themeAction->setText(m_dark ? QStringLiteral("浅色主题") : QStringLiteral("深色主题"));
    applyTokens();
}

void MainWindow::startSimulation()
{
    if (m_context.engine == nullptr)
        return;

    m_context.engine->start();
    if (m_runAction)
        m_runAction->setText(QStringLiteral("暂停"));
}

void MainWindow::onToggleRun()
{
    if (m_context.engine == nullptr)
        return;

    switch (m_context.engine->state()) {
    case SimulationEngine::State::Running:
        m_context.engine->pause();
        m_runAction->setText(QStringLiteral("启动"));
        break;
    case SimulationEngine::State::Paused:
        m_context.engine->resume();
        m_runAction->setText(QStringLiteral("暂停"));
        break;
    case SimulationEngine::State::Idle:
        m_context.engine->start();
        m_runAction->setText(QStringLiteral("暂停"));
        break;
    }
}

void MainWindow::onStep()
{
    if (m_context.engine != nullptr)
        m_context.engine->stepOnce();
}

void MainWindow::onSpeedChanged(int index)
{
    static const double speeds[] = {0.5, 1.0, 2.0, 5.0, 10.0};
    if (m_context.engine != nullptr && index >= 0 && index < 5)
        m_context.engine->setSpeed(speeds[index]);
}

void MainWindow::onThemeToggled()
{
    m_dark = !m_dark;
    if (m_themeAction)
        m_themeAction->setText(m_dark ? QStringLiteral("浅色主题") : QStringLiteral("深色主题"));

    if (auto *app = qobject_cast<QApplication *>(QApplication::instance()))
        applyTheme(*app, m_theme, m_dark);
    applyTokens();
}

void MainWindow::onSelectionChanged()
{
    if (m_signalTable == nullptr || m_context.bank == nullptr)
        return;

    const QModelIndexList rows = m_signalTable->selectionModel()->selectedRows();
    if (rows.isEmpty())
        return;

    const QString signalId = m_signalModel->signalIdAt(rows.first().row());
    if (signalId.isEmpty() || signalId == m_curveSignalId)
        return;

    m_curveSignalId = signalId;

    if (m_curveWidget != nullptr && m_curveWidget->rootObject() != nullptr) {
        QMetaObject::invokeMethod(m_curveWidget->rootObject(), "resetValues");
        syncCurveLabel();
    }
}

void MainWindow::syncCurveLabel()
{
    if (m_curveWidget == nullptr || m_context.bank == nullptr)
        return;

    QObject *root = m_curveWidget->rootObject();
    if (root == nullptr || m_curveSignalId.isEmpty())
        return;

    const Signal definition = m_context.bank->definition(m_curveSignalId);
    const QString label = definition.unit.isEmpty()
        ? definition.name
        : QStringLiteral("%1（%2）").arg(definition.name, definition.unit);
    root->setProperty("label", label);
}

void MainWindow::onTick(qint64 virtualTimeMs, qint64 tickCount)
{
    if (m_clockLabel != nullptr) {
        m_clockLabel->setText(QStringLiteral("仿真时钟 %1 s · tick %2")
                                  .arg(virtualTimeMs / 1000.0, 0, 'f', 3)
                                  .arg(tickCount));
    }

    if (m_curveSignalId.isEmpty() || m_context.bank == nullptr || m_curveWidget == nullptr)
        return;

    QObject *root = m_curveWidget->rootObject();
    if (root == nullptr)
        return;

    const SignalSnapshot snapshot = m_context.bank->snapshot(m_curveSignalId);
    QMetaObject::invokeMethod(root, "appendValue", Q_ARG(QVariant, snapshot.value));
}

void MainWindow::onExportFrames()
{
    if (m_context.log == nullptr)
        return;

    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出报文日志"),
        QStringLiteral("protosim-frames.txt"),
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
    statusBar()->showMessage(QStringLiteral("已导出 %1 帧到 %2")
                                 .arg(m_context.log->count())
                                 .arg(path),
                             4000);
}

void MainWindow::refreshEndpointStatus()
{
    if (m_endpointLabel == nullptr)
        return;

    if (m_context.adapter == nullptr) {
        m_endpointLabel->setText(QStringLiteral("端点：未配置"));
        return;
    }

    const AdapterStats stats = m_context.adapter->stats();
    const QString protocol = protocolKindToString(stats.kind);
    const int port = stats.extra.value(QStringLiteral("port")).toInt(0);
    const int maxConnections = stats.extra.value(QStringLiteral("maxConnections")).toInt(0);
    const QString state = adapterStateToString(m_context.adapter->state());

    m_endpointLabel->setText(QStringLiteral("%1 %2:%3 · %4 · 在线 %5/%6 · 收 %7 发 %8 异常 %9")
                                 .arg(protocol)
                                 .arg(QStringLiteral("127.0.0.1"))
                                 .arg(port)
                                 .arg(state)
                                 .arg(stats.activeClients)
                                 .arg(maxConnections)
                                 .arg(stats.framesRx)
                                 .arg(stats.framesTx)
                                 .arg(stats.errorFrames));

    if (m_frameCountLabel != nullptr && m_context.log != nullptr)
        m_frameCountLabel->setText(QStringLiteral("报文 %1").arg(m_context.log->count()));
}

} // namespace protosim::ui
