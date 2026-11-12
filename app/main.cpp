// SPDX-License-Identifier: GPL-3.0-or-later
//
// ProtoSim 入口。
//
// 启动顺序（与 P0 目标对应）：
//   1. 解析参数 -> 2. 载入配置包 -> 3. 装配实时库/管线/总线/引擎 -> 4. 起 Modbus 端点
//   -> 5. 显示主窗口 -> 6. 开跑。
// 这样启动完 FieldLink 立刻能连 127.0.0.1:1502（AC-P0-17）。

#include "selftest.h"

#include "protosim/adapters/modbusadapter.h"
#include "protosim/configbundle.h"
#include "protosim/databank.h"
#include "protosim/eventbus.h"
#include "protosim/simulationengine.h"
#include "protosim/telemetrylog.h"
#include "protosim/ui/mainwindow.h"
#include "protosim/ui/theme.h"
#include "protosim/version.h"
#include "protosim/writepipeline.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QFile>
#include <QPixmap>
#include <QTimer>
#include <QMessageBox>

#include <cstdio>

namespace {

/// 配置包查找顺序：先看绿色包内的相对位置，再看开发期源码目录。
QString resolveConfigPath(const QString &requested)
{
    if (!requested.isEmpty())
        return requested;

    const QStringList candidates = {
        QCoreApplication::applicationDirPath()
            + QStringLiteral("/config/examples/fieldlink_preset.json"),
        QStringLiteral(PROTOSIM_SOURCE_DIR "/config/examples/fieldlink_preset.json"),
    };

    for (const QString &candidate : candidates) {
        if (QFile::exists(candidate))
            return candidate;
    }
    return candidates.first();
}

} // namespace

int main(int argc, char *argv[])
{
    // protosim_ui 是静态库，qrc 生成的资源初始化和链接器会"看不见引用"而裁掉，
    // 必须显式 Q_INIT_RESOURCE 一次，否则 QML 会报 No such file。
    Q_INIT_RESOURCE(qml);

    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("ProtoSim"));
    QCoreApplication::setApplicationVersion(protosim::versionString());
    QCoreApplication::setOrganizationName(QStringLiteral("ProtoSim"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("工业协议服务端模拟器 —— 一个实时库，多协议同时对外"));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption selftestOption(
        QStringLiteral("selftest"), QStringLiteral("跑 9 项内置自检后退出，退出码 0 表示全通过"));
    parser.addOption(selftestOption);

    const QCommandLineOption configOption(
        QStringLiteral("config"), QStringLiteral("指定场景配置包路径"), QStringLiteral("path"));
    parser.addOption(configOption);

    // 交付与回归留证用：启动后自截一张窗口图再退出，不依赖任何外部截图工具。
    const QCommandLineOption screenshotOption(
        QStringLiteral("screenshot"),
        QStringLiteral("启动后把主窗口截图到指定路径并退出"),
        QStringLiteral("path"));
    parser.addOption(screenshotOption);

    const QCommandLineOption screenshotDelayOption(
        QStringLiteral("screenshot-delay"),
        QStringLiteral("截图前等待的毫秒数（默认 1800，用于先跑出一些流量）"),
        QStringLiteral("ms"), QStringLiteral("1800"));
    parser.addOption(screenshotDelayOption);

    const QCommandLineOption tabOption(
        QStringLiteral("tab"),
        QStringLiteral("启动后停在哪个页签：frames（默认）或 curve"),
        QStringLiteral("name"), QStringLiteral("frames"));
    parser.addOption(tabOption);

    const QCommandLineOption themeOption(
        QStringLiteral("theme"),
        QStringLiteral("启动主题：dark（默认）或 light"),
        QStringLiteral("name"),
        QStringLiteral("dark"));
    parser.addOption(themeOption);

    parser.process(app);

    if (parser.isSet(selftestOption))
        return protosim::runSelfTest();

    // ---- 运行时装配（顺序不能乱：后面几个都依赖前面的存活）----
    const QString configPath = resolveConfigPath(parser.value(configOption));
    const auto loaded = protosim::ConfigBundle::loadFromFile(configPath);
    if (!loaded.ok()) {
        const QString message = QStringLiteral("场景加载失败：\n%1\n\n路径：%2")
                                    .arg(loaded.error().toString(), configPath);
        ::fputs(message.toUtf8().constData(), stderr);
        QMessageBox::critical(nullptr, QStringLiteral("ProtoSim 启动失败"), message);
        return 1;
    }
    const protosim::ConfigBundle bundle = loaded.value();

    protosim::DataBank bank;
    protosim::WritePipeline pipeline(&bank);
    protosim::TelemetryLog telemetryLog(5000);
    protosim::EventBus bus;
    protosim::SimulationEngine engine(&bank);
    protosim::ModbusTcpAdapter adapter(&bank, &pipeline, &telemetryLog);

    const auto populated = bundle.populate(&bank);
    if (!populated.ok()) {
        QMessageBox::critical(nullptr, QStringLiteral("ProtoSim 启动失败"),
                              populated.error().toString());
        return 1;
    }

    engine.load(bundle);

    // 协议写入的时标与仿真时钟同源（INTERFACES §8）
    adapter.setClockSource([&engine] { return engine.virtualTimeMs(); });

    bus.attachDataBank(&bank);
    bus.registerAdapter(&adapter);

    adapter.bind(protosim::ModbusTcpAdapter::bindingsFromBundle(bundle));

    const protosim::EndpointInfo *modbusEndpoint =
        bundle.endpointFor(protosim::ProtocolKind::Modbus);
    if (modbusEndpoint != nullptr) {
        protosim::EndpointConfig endpointConfig;
        endpointConfig.kind = protosim::ProtocolKind::Modbus;
        endpointConfig.raw = modbusEndpoint->raw;

        const auto started = adapter.start(endpointConfig);
        if (!started.ok()) {
            const QString message = QStringLiteral("Modbus 端点启动失败：\n%1")
                                        .arg(started.error().toString());
            ::fputs(message.toUtf8().constData(), stderr);
            QMessageBox::critical(nullptr, QStringLiteral("ProtoSim 启动失败"), message);
            return 1;
        }
    }

    const bool darkTheme = parser.value(themeOption) != QLatin1String("light");
    protosim::ui::ThemeBridge themeBridge;
    protosim::ui::applyTheme(app, &themeBridge, darkTheme);

    protosim::ui::MainWindow::Context context;
    context.bank = &bank;
    context.pipeline = &pipeline;
    context.log = &telemetryLog;
    context.bus = &bus;
    context.engine = &engine;
    context.adapter = &adapter;

    protosim::ui::MainWindow window(context, &themeBridge);
    window.setBundle(bundle);
    window.setDarkTheme(darkTheme);
    if (parser.value(tabOption) == QLatin1String("curve"))
        window.showCurveTab();
    window.show();

    window.startSimulation();

    if (parser.isSet(screenshotOption)) {
        const QString target = parser.value(screenshotOption);
        const int delay = qMax(200, parser.value(screenshotDelayOption).toInt());
        QTimer::singleShot(delay, &window, [&window, target] {
            const bool saved = window.grab().save(target);
            ::fprintf(saved ? stdout : stderr, "screenshot -> %s\n", target.toUtf8().constData());
            QCoreApplication::quit();
        });
    }

    return app.exec();
}
