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
#include <QString>

class QComboBox;
class QLabel;
class QTableView;
class QTabWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QQuickWidget;

namespace protosim::ui {

class SignalTableModel;
class FrameTableModel;

/// 主窗口：设备树 + 点表 + 报文 hex + 曲线。
///
/// 纪律：UI 只读 DataBank / 写走 WritePipeline，绝不直接改实时库。
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
        IProtocolAdapter *adapter = nullptr; // 只看接口，不认具体协议（INTERFACES §6）
    };

    MainWindow(const Context &context, ThemeBridge *theme, QWidget *parent = nullptr);
    ~MainWindow() override;

    void setBundle(const ConfigBundle &bundle);
    void setDarkTheme(bool dark);
    void startSimulation();

private:
    void buildUi();
    void buildToolBar();
    void buildDeviceTree();
    void buildStatusBar();
    void applyTokens();

    void onToggleRun();
    void onStep();
    void onSpeedChanged(int index);
    void onThemeToggled();
    void onSelectionChanged();
    void onTick(qint64 virtualTimeMs, qint64 tickCount);
    void onExportFrames();
    void refreshEndpointStatus();
    void syncCurveLabel();

    Context m_context;
    ThemeBridge *m_theme = nullptr;
    ConfigBundle m_bundle;
    bool m_dark = true;

    QTreeWidget *m_deviceTree = nullptr;
    QTableView *m_signalTable = nullptr;
    QTableView *m_frameTable = nullptr;
    QTabWidget *m_bottomTabs = nullptr;
    QQuickWidget *m_curveWidget = nullptr;
    QComboBox *m_speedCombo = nullptr;
    QComboBox *m_filterCombo = nullptr;

    QLabel *m_endpointLabel = nullptr;
    QLabel *m_clockLabel = nullptr;
    QLabel *m_frameCountLabel = nullptr;

    QAction *m_runAction = nullptr;
    QAction *m_stepAction = nullptr;
    QAction *m_themeAction = nullptr;

    SignalTableModel *m_signalModel = nullptr;
    FrameTableModel *m_frameModel = nullptr;

    QString m_curveSignalId;
};

} // namespace protosim::ui
