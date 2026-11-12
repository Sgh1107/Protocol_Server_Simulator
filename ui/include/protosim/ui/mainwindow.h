// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "protosim/configbundle.h"
#include "protosim/databank.h"
#include "protosim/eventbus.h"
#include "protosim/iprotocoladapter.h"
#include "protosim/simulationengine.h"
#include "protosim/telemetrylog.h"
#include "protosim/ui/theme.h"
#include "protosim/writepipeline.h"

#include <QMainWindow>

class QQuickWidget;
class QTableView;
class QTabWidget;

namespace protosim::ui {

class CommandBar;
class FrameTableModel;
class SignalRail;
class SignalTableModel;
class StatusStrip;

/// 主窗口：命令栏 + 设备栏 + 点表 + 报文/曲线 + 状态条。
///
/// 纪律：UI 只读 DataBank、写走 WritePipeline；颜色只从 theme.h 取，
/// 任何硬编码色都会在深浅切换时露馅。
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    struct Context
    {
        DataBank *bank = nullptr;
        WritePipeline *pipeline = nullptr;
        TelemetryLog *log = nullptr;
        EventBus *bus = nullptr;
        SimulationEngine *engine = nullptr;
        IProtocolAdapter *adapter = nullptr;
    };

    MainWindow(const Context &context, ThemeBridge *theme, QWidget *parent = nullptr);
    ~MainWindow() override;

    void setBundle(const ConfigBundle &bundle);
    void startSimulation();
    void setDarkTheme(bool dark);
    void showCurveTab();

private:
    void buildUi();
    QWidget *buildSignalPanel();
    QWidget *buildBottomPanel();
    void applyTokens();
    void syncCurve();
    void refreshStatus();

    void toggleRunning();
    void toggleForce(const QString &signalId);
    void selectSignal(const QString &signalId);
    void onTick(qint64 virtualTimeMs, qint64 tickCount);
    void onExportFrames();
    void onThemeToggle();

    Context m_context;
    ThemeBridge *m_theme = nullptr;
    ConfigBundle m_bundle;
    bool m_dark = true;

    CommandBar *m_commandBar = nullptr;
    SignalRail *m_rail = nullptr;
    QTableView *m_signalTable = nullptr;
    QTableView *m_frameTable = nullptr;
    QTabWidget *m_tabs = nullptr;
    QQuickWidget *m_curve = nullptr;
    StatusStrip *m_status = nullptr;

    SignalTableModel *m_signalModel = nullptr;
    FrameTableModel *m_frameModel = nullptr;

    QString m_curveSignalId;
    bool m_curveReady = false;
};

} // namespace protosim::ui
