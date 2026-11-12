# ProtoSim — 工业协议服务端模拟器（暂定名）

> **当前状态：P0 已落地可运行。** `sim-core` / Modbus TCP 服务端 / 统一写回管线 / 报文日志 / Widgets+QML 界面全部就位，`ctest` 9 项全绿、`--selftest` 9 项通过、绿色 zip 已能产出。P1 起的内容（RTU、多 unit、MQTT、SQLite、故障注入）尚未开始。

> 决策依据：`docs/SERVER_SIMULATOR_PROPOSAL.md`（v0.1 立项方案）、`docs/TECH_STACK_DECISIONS.md`（v0.5 技术基线与依赖决策）、`docs/P0_PLAN.md`（P0 任务与验收）。

---

## 1. 这是什么

给上位机 / 主站 / SCADA 做**联调、验收、自动化测试、演示**用的「协议对手方」（test double）：一台机器上同时起多个协议服务端，共享同一份实时数据，还能故意制造异常报文与链路故障，用来验证主站行为。

| 做 | 不做 |
| --- | --- |
| 多协议服务端同时在线、共享数据源 | 采集轮询、历史归档、报警规则、报表、用户权限体系 |
| 点表 / 场景可编排、可导入导出、可复现 | 真实设备驱动、实际 I/O |
| 故障注入与异常报文构造 | 生产环境长期无人值守的高可用集群 |
| 报文级监视（hex + 解析树）、协议一致性自检 | 做成"全能监控平台" |
| 远程控制 API，便于 CI / 自动化脚本驱动 | Python 作为正式协议实现（Python 只留在 `tools/`） |

**核心卖点：一个实时库，多协议同时对外。** 同一个信号点可同时被 Modbus 读取、被 104 突发上送、被 MQTT 发布、被 61850 报告；任一协议写入都回写实时库并联动其他协议上送 —— 杜绝"从 104 改了值，Modbus 读出来还是旧的"。

**协议范围**：短期 Modbus TCP/RTU、IEC 60870-5-104、MQTT；后期预留 IEC 61850（MMS / GOOSE / SV）。

---

## 2. 技术基线（已定稿）

| 项 | 定稿 |
| --- | --- |
| 语言 | **C++17**（`CMAKE_CXX_STANDARD 17`，MSVC `/std:c++17`） |
| 框架 | **Qt 6.6.3（已确定，本地已有环境）** |
| UI | **Widgets 主体 + QML 局部**：点表 / hex 报文 / 设备树 / 多页签用 Widgets；场景时间轴 / 曲线预览 / 演示动画用 QML |
| 编译器 | **MSVC 2022 x64**（Qt 6.6 官方包 `msvc2019_64` 与之 ABI 兼容）；**不考虑 MinGW** |
| 构建 | **CMake ≥ 3.23 + Ninja**（本机无 Ninja 时可用 VS 生成器），不用 qmake |
| 目标平台 | **Windows 10 1809+ / 11，x64**；Linux 后续 |
| 数据库 | **SQLite**（Qt QSQLITE 插件内置），只存冷数据；**实时库 DataBank 只在内存** |
| 交付形态 | 绿色 zip（`windeployqt --qmldir` + VC++ Redistributable） |
| 许可证 | **GPL-3.0-or-later**（全仓统一） |

> ⚠️ **Qt 6.6.3 已 EOL**：Qt 6.6 非 LTS，开源支持 2024-04-02 结束、最终补丁即 6.6.3（2024-03-26），开源用户已拿不到安全补丁。**本轮已决定：维持 6.6.3 起步，不在 P0 前升级；待 Qt 6.12 系列稳定后，按 `docs/TECH_STACK_DECISIONS.md` §3.5 的判定标准于阶段边界切换。** 这是知情后的取舍 —— 本地环境现成、代码尚未开始、项目为本地联调工具（非公网服务），暴露面有限；风险已登记。升级纪律与禁用 API 清单见 §3.5；升级目标为 **6.12 系列的稳定版本**，**不要选 6.8 / 6.10 / 6.11**（开源侧均已停更）。

---

## 3. 依赖与版本清单

> **本表是项目唯一的版本真相源。** 任何依赖升级必须同步更新：本表 → `cmake/ProjectVersions.cmake` → git submodule tag / vcpkg baseline → `THIRD_PARTY_NOTICES.md`。
> 版本核实日期：**2026-11-11**。标「随 Qt」的组件不单独安装；标「不锁」的是开发期工具，不进运行包。

### 3.1 核心运行时（P0 必需）

| 组件 | 版本 | 许可证 | 获取方式 | 说明 |
| --- | --- | --- | --- | --- |
| Qt 6 | **6.6.3** | LGPLv3 / GPL 系列 / 商业（逐模块） | 本地已安装；或 Qt 在线安装器归档 | 全项目单基线，所有 Qt 模块必须同版本 |
| MSVC 2022 | 17.x（x64） | 专有（工具） | Visual Studio 2022「使用 C++ 的桌面开发」 | Windows SDK 一并安装（DbgHelp.lib 用于崩溃 dump） |
| CMake | **≥ 3.23** | BSD-3 | 独立安装 / Qt Creator 自带 | Qt 6.6 最低要求 3.16，本项目统一 3.23+ |
| Ninja | 随 Qt / 独立 | Apache-2.0 | Qt 安装器 / 独立 | 生成器 |
| C++ 标准库 | MSVC STL（`/MD`） | 专有（工具） | 随 MSVC | 所有 C 库必须同 CRT |
| SQLite | **随 Qt 6.6.3 的 QSQLITE 插件内置**，不单独装 | Public Domain | Qt 自带 | P1 起用于报文日志 / 审计 / 剧本 / 自检结果 |
| OpenSSL | **3.x**（Qt TLS 需要时） | Apache-2.0 | 随 Qt / 系统 | P0/P1 不启用 TLS 时不需要 |

**Qt 6.6.3 模块清单**（`find_package(Qt6 REQUIRED COMPONENTS ...)`）：

| 模块 | 用途 | 许可证档 |
| --- | --- | --- |
| `Core` `Widgets` `Network` `Concurrent` `Test` | 内核、UI 骨架、Socket、单测 | LGPLv3 系 |
| `Sql` | SQLite / `IResultStore` | LGPLv3 系 |
| `Qml` `Quick` `Quick Controls` | QML 局部视图、场景时间轴、动画 | LGPLv3 系 |
| `QJSEngine`（同属 `Qml` 模块） | 仿真曲线脚本，沿用 FieldLink `scriptengine` 思路 | LGPLv3 系 |
| `Charts` | QML / Widgets 曲线预览 | **GPLv3 / 商业**（全仓 GPLv3，可用）— **P0 未使用**：曲线用 Qt Quick Canvas 自绘，不留未链接的依赖 |
| `SerialBus` | `QModbusTcpServer` / `QModbusRtuSerialServer` | **按 LGPLv3 档使用**（该模块另有 GPLv2 档，勿选） |
| `SerialPort` | Modbus RTU（P1） | LGPLv3 系 — **P0 起即为构建期硬依赖**：`Qt6SerialBusConfig.cmake` 里 `find_dependency(Qt6SerialPort)`，CI 装 Qt 时必须同时给 `qtserialbus qtserialport` |
| `Svg` `LinguistTools` | 图标、中英双语（Widgets `tr()` / QML `qsTr()` 共用 `.ts`） | LGPLv3 系 |

> Qt 6.6 **没有** `QtHttpServer`（6.7+）、StyleKit（6.12 Labs）、QML 热重载（6.12）、Qt Canvas Painter（6.12）。因此 HTTP 控制 API 用 `QTcpServer` 手写，QML 主题用 Qt Quick Controls `palette` + 自定义配色令牌。**代码中禁止使用 6.7+ API**，否则将来升级 LTS 成本会很高。

### 3.2 协议与第三方库（按阶段引入）

| 组件 | 版本 | 许可证 | 引入阶段 | 集成方式 | 备注 |
| --- | --- | --- | --- | --- | --- |
| Modbus 服务端 | — | — | **P0** | Qt `SerialBus` 自带 | **零第三方库** |
| `lib60870-C` | **2.4.1+** | GPLv3（MZ 双授权） | **P2** | git submodule，固定 tag（按上游实际 tag 核对，建议 `v2.4.1`） | ⚠️ **不要用 2.4.0 及更早**：存在 CVE-2026-16002（越界读，CISA 告警）与 CVE-2026-67873（服务端 FileSegment 编码堆溢出，CVSS 9.8）；2.4.1（2026-07-16）为修复版。独立 Worker 进程 |
| `libiec61850` | **1.6.2** | GPLv3（MZ 双授权） | **P3** | git submodule，固定 tag | 支持 Windows/Linux/macOS；SCL/CID 运行时建模为商业版特性；库自带 SQLite 实现用于 Log service。独立 Worker 进程 |
| `mbedTLS` | **默认不编**；启用时 2.28.10 / 3.6.x | Apache-2.0 | 仅启用 TLS 时 | 源码放入 `third_party/` | ⚠️ **2.28.10 是 2.28 LTS 最终版，已停止 bugfix/安全修复**；lib60870 2.4.x 只认 2.28.x，libiec61850 1.6.2 另支持 3.6.x。**P2 默认不启用 TLS、完全不引 mbedTLS**，确需 TLS 时再单独评估 |
| `Npcap` + SDK | 用户自行安装，不锁版本 | 专有（免费版限制再分发） | **P3，且仅做 GOOSE/SV 时** | 不随包分发 | 现代 Windows 无可用 raw socket；WinPcap 已停维。**MMS（TCP）不需要** |
| `qtmqtt`（Qt MQTT 源码） | `v6.6.3`（对应 Qt tag） | GPLv3 / 商业 | 仅当选自编方案 | CMake 从源码编译 | 开源用户拿不到预编译包；本项目**默认走自研 `core/mqttcodec`**，此项为可选后端 |
| `libmodbus` | 不锁（不引入） | LGPL-2.1+ | 仅备选 | — | 只在 Qt 封不住自定义功能码 / 畸形报文时考虑 |
| `cpp-httplib` | 不锁（可选） | MIT | P1 备选 | header-only | 仅当不想手写 HTTP/1.1 时 |

### 3.3 可选外部服务（不进运行包、不随包分发）

| 组件 | 版本 | 许可证 | 用途 | 备注 |
| --- | --- | --- | --- | --- |
| Eclipse Mosquitto | **2.1.2** | 取 **EDL-1.0** 档（勿用 EPL-2.0 档，与 GPLv3 不兼容） | 外挂 MQTT Broker（P1） | 稳定、社区成熟；只作独立进程 |
| NanoMQ | **0.25.7** | MIT | 外挂 MQTT Broker（P1） | 许可最干净，但 2026 年有较多内存安全公告；仅本地联调、不暴露公网 |
| 内置 mini Broker | 自研 | 本项目 GPLv3 | 离线联调 / 故障注入（P1） | **默认关闭**，UI 标注"联调/故障注入专用"；零外部依赖即可跑通 MQTT |

> 项目**不依赖**任何数据库服务：Redis、MariaDB、TDengine、MySQL/PostgreSQL、Kafka、gRPC 等一律不引入。判据见 `docs/TECH_STACK_DECISIONS.md` §4.0。

### 3.4 开发 / 构建 / 测试工具（不进运行包）

| 组件 | 版本 | 许可证 | 用途 |
| --- | --- | --- | --- |
| Qt Test / Qt Quick Test | 随 Qt 6.6.3 | LGPLv3 系 | 纯逻辑单测、QML 视图单测 |
| vcpkg（manifest 模式，可选） | baseline 待锁 | MIT | 小依赖版本锁定 |
| clang-format / clang-tidy / Cppcheck / qmllint | 不锁 | 各自 | 代码规范与静态检查，QML 检查进 CI |
| ASan / UBSan / LSan | 随编译器 | — | MSVC 的 ASan 不完整；全套放 Linux CI 作业（GCC/Clang） |
| Doxygen | 不锁 | GPLv2 | `IProtocolAdapter` / `sim-core` 接口文档 |
| Wireshark + Npcap | 不锁 | GPLv2 / 专有 | 报文级人工验证、pcap 对照 |
| Python 3 + `jsonschema` + `pymodbus` | 不锁 | 各自 | `tools/` 验收脚本、假主站、CI 驱动；**不进运行时** |
| GitHub Actions `windows-2022` | — | — | Windows 构建 + Linux sanitizer / 跨平台耦合守卫作业 |
| Inno Setup / NSIS / CPack | 不锁 | 各自 | 后期安装包；P0~P1 只出绿色 zip |

---

## 4. 环境准备与构建

**前置条件**：Windows 10 1809+ / 11 x64、Qt 6.6.3（`msvc2019_64` kit）、Visual Studio 2022（C++ 桌面开发 + Windows SDK）、CMake ≥3.23、Git。`CMAKE_PREFIX_PATH` 按本机 Qt 实际安装路径调整。

```bat
:: 有 Ninja（CI 走这条）
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_PREFIX_PATH="D:/programming/Qt/6.6.3/msvc2019_64"
cmake --build build --parallel

:: 没装 Ninja（本机现状，用 VS 生成器）
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_PREFIX_PATH="D:/programming/Qt/6.6.3/msvc2019_64"
cmake --build build --config Release --parallel

ctest --test-dir build -C Release --output-on-failure
build\bin\ProtoSim.exe --selftest
```

> ⚠️ **MSBuild 的坑**：如果环境里同时存在 `HTTPS_PROXY` 和 `https_proxy`（不少代理工具会这么设），
> Windows 上这两个变量名不区分大小写，MSBuild 会直接崩在
> `System.ArgumentException: 已添加项。字典中的关键字:"HTTPS_PROXY"`。
> 构建前把重复项去掉即可（cmd 里 `set https_proxy=`，bash 里 `env -u https_proxy -u http_proxy cmake --build ...`）。

**测试 / 自检 / 校验**：

| 命令 | 内容 |
| --- | --- |
| `bash tools/check.sh` | **提交前敲这个**：配置包校验 → 编译（WERROR，与 CI 同款参数）→ ctest → `--selftest`，一条命令全绿才算可提交 |
| `bash tools/check.sh --fast` | 跳过 CMake 配置（没动 `CMakeLists.txt` 时用） |
| `ctest --test-dir build -C Release` | L1 纯逻辑 + L2 假主站 E2E，共 9 个测试程序 |
| `build\bin\ProtoSim.exe --selftest` | 9 项内置自检，退出码 0 表示全通过 |
| `python tools/validate_config.py` | 配置包 JSON Schema 校验 |

**分支与提交流程**（单人开发 + AI 协作，`main` 不允许直推）：

```bat
git switch main && git pull
git switch -c feat/p1-rtu          :: 每件事一个短分支
bash tools/check.sh                :: 本地先过一遍，别让 CI 当第一道关
git add -A && git commit -m "..."
git push -u origin feat/p1-rtu     :: 然后开 PR，CI 跑在 PR 上
:: PR 页面：等 Windows 作业绿 -> Squash and merge -> 删分支
git switch main && git pull
```

只想快速看 CI、不想开 PR（例如调 workflow 本身）：开 `ci/xxx` 分支 push 即可，`main` 与 `ci/**` 都会触发 CI，
其余分支只在开 PR 时跑。合并后建议在 GitHub 仓库设置里打开 *Automatically delete head branches*。

**打绿色包**（QML 必须带 `--qmldir`，否则运行时缺 QML 模块）：

```bat
python tools/package.py --qt D:/programming/Qt/6.6.3/msvc2019_64
```

产物 `dist/ProtoSim-<版本>-win64.zip` 解压即跑，内含 `config/`、许可声明与「快速开始.txt」。

**工具链铁律**：Qt 预编译库、`lib60870-C`、`libiec61850`、主程序必须**同一编译器、同一 CRT（`/MD`）、同一架构**；不混用 MSVC 与 MinGW 产物。

**第三方库获取**：`git submodule update --init --recursive`（P2/P3 才需要）。Qt 本体、C 库、小工具三者来源分离，不混着管。

---

## 5. 运行与联调（规划）

| 项 | 默认值 |
| --- | --- |
| Modbus TCP | `127.0.0.1:1502`（可配 502；Windows 上低端口无需管理员权限） |
| IEC 104 | `:2404`（P2 起） |
| MQTT | 外挂 Broker `:1883`；内置 mini Broker **默认关闭** |
| 远程控制 API | HTTP JSON + Token，仅监听 `127.0.0.1`，端口待定；远程写开关默认关 |
| 接口 | `loadScenario` / `start` / `stop` / `forceValue` / `injectFault` / `getStats` |

**FieldLink 联调预设**（P0 内置）：一键起一个 Modbus TCP 从站（reg0 正弦温度、reg1 随机游走、reg2 = 42）+ MQTT 主题模板，启动后直接拿 FieldLink 连 `127.0.0.1:1502` 即可验收。主题与 JSON 沿用 `doc/MQTT_GUIDE.md` 的 `fieldlink/data/{从站}/{寄存器类型}/{起始地址}` 规范，FieldLink 不用改一行代码。

**首次运行注意**：Windows Defender 会弹入站规则提示，需允许专用网络；未签名 exe 可能触发 SmartScreen / 杀软误报；远程桌面或老旧虚拟机若 QML 渲染异常，设 `QSG_RHI_BACKEND=software`。

---

## 6. 目录结构

```
ProtoSim/
├── cmake/                 # ProjectVersions.cmake（Qt 版本集中配置）
├── sim-core/              # 纯 Qt/C++，不链接任何协议库
│                          # SignalModel · DataBank · SimulationEngine
│                          # Scenario/Curve/Script(QJSEngine) · FaultInjector
│                          # ConfigBundle · TelemetryLog · IResultStore
├── adapters/              # IProtocolAdapter 及进程内适配器（Modbus / MQTT）
├── workers/               # 104 / 61850 独立 Worker 进程（P2/P3）
├── ui/
│   ├── widgets/           # 点表 / hex 报文 / 设备树 / 多页签（QSS 主题）
│   └── qml/               # 场景时间轴 / 曲线 / 演示动画（Qt Quick palette 主题）
├── config/                # 配置包 JSON Schema（带 schemaVersion）、预设场景
├── tests/                 # 三层测试：纯逻辑 / 假主站 E2E / FieldLink 真实互验
├── tools/                 # Python 验收脚本（不进运行时）
├── third_party/           # git submodule：lib60870-C、libiec61850 …
├── platform/              # 平台差异隔离（Win32 MiniDump、防火墙规则等）
├── LICENSE                # GPL-3.0-or-later
└── THIRD_PARTY_NOTICES.md # 第三方组件许可证与版权声明（待补）
```

---

## 7. 分期路线

| 阶段 | 内容 | 验收口径 |
| --- | --- | --- |
| **P0** | `sim-core`、Qt UI 骨架（Widgets + QML 局部）、Modbus TCP 服务端、常量/正弦仿真、报文日志、单测 | 启动模拟器 → FieldLink 连 1502 读到正弦温度；FC16 写 → FC03 回读一致 |
| **P1** | Modbus RTU + 多 unit/多设备、点表 CSV/JSON 导入导出、异常注入、SQLite 冷数据、MQTT 设备侧 + 内置 mini Broker、HTTP 控制 API | FieldLink MQTT 测试脚本跑通；故障注入可脚本化 |
| **P2** | 104 Worker（lib60870-C 2.4.1）：总召唤、突发上送、SBO、时钟同步、k/w 参数；场景回放 | 第三方 104 主站完成总召唤 + 遥控闭环；Worker 崩溃不影响 UI |
| **P3** | 61850 Worker：SCL 导入、MMS 报告/控制、数据集；GOOSE/SV 视需求 | 与 61850 客户端互认、报告触发 |

Linux 版本在 P0 之后单独立项。**P0 的 CI 只跑 Windows x64 + MSVC**（配置 / 编译 / 测试 / 自检四项），
不设 Linux 编译作业、也不设格式门禁——理由记在 `docs/P0_PLAN.md` §7 变更记录。

---

## 8. 许可证与合规

- **本仓库：`GPL-3.0-or-later`**。使用 lib60870-C / libiec61850 的 GPLv3 版本，未采购商业授权；项目不禁止他人依法商用，但下游二次分发须同样开源。
- 每个源文件带 `// SPDX-License-Identifier: GPL-3.0-or-later`；分发二进制时提供完整对应源码（含构建脚本与补丁）。
- `THIRD_PARTY_NOTICES.md` 记录每个第三方组件的名称、版本、许可证、来源与原始版权声明；建议生成 CycloneDX / SPDX SBOM。
- Qt 模块按 **LGPLv3 档**使用（Qt MQTT / Qt Charts 为 GPLv3-only，与全仓许可证一致）；Qt SerialBus 勿选 GPLv2 档。
- 从 FieldLink（MIT）抽取的规范与代码保留原版权声明。
- **不是法律意见**：对外商业分发、SaaS 托管或法务审查前请法务复核。

---

## 9. 已知限制与风险

| 项 | 说明 |
| --- | --- |
| P0 已落地、P1 未开始 | 当前只有 Modbus TCP 单协议闭环；RTU、多 unit、MQTT、SQLite、故障注入均为 P1+ |
| **Qt 6.6.3 已 EOL** | 非 LTS、开源支持 2024-04-02 结束、无安全补丁；**已决定知情接受、暂不升级**，待 6.12 稳定后按 §3.5 五条判定标准于阶段边界切换；开源版 LTS 后续补丁为商业专属，以在线安装器实际可见版本为准 |
| lib60870 版本红线 | 必须 2.4.1+；2.4.0 及更早存在高危 CVE |
| TLS 默认关闭 | mbedTLS 2.28 线已停止维护，P2 默认不编 TLS、不引入该库 |
| QML 无热重载 | 6.6 没有内置热重载，开发期靠重启；主题需 Widgets QSS 与 QML palette 共享一套配色令牌 |
| GOOSE/SV 依赖 Npcap | 仅 P3，且驱动需用户自行安装，不随绿色包分发 |
| 不做生产级能力 | 无高可用集群、无历史归档 / 报表 / 权限体系、无真实 I/O |
| 未签名 exe | SmartScreen / 杀软可能误报 |
| Linux | 后续版本，尚未排期 |

---

## 10. 相关文档与下一步

- `docs/SERVER_SIMULATOR_PROPOSAL.md` — v0.1 立项方案（定位、架构、协议范围、UI 草案、分期、风险）
- `docs/TECH_STACK_DECISIONS.md` — v0.5 技术基线定稿（许可证、Qt 版本边界、数据库/中间件判据、完整依赖决策与风险清单）
- `config/schema/sim-config.schema.json` — 配置包 JSON Schema（Draft 2020-12）
- `config/examples/fieldlink_preset.json` — FieldLink 联调预设（3 信号 / 1 端点），已通过 Schema 校验
- `docs/INTERFACES.md` — `IProtocolAdapter` 接口契约（统一写回管线、Worker IPC、capabilities 驱动 UI）
- `docs/P0_PLAN.md` — P0 任务拆解与 19 条验收用例
- `docs/P0_OPEN_QUESTIONS.md` — P0 落地中的冲突、偏离与待拍板事项
- `docs/design/` — 界面改版设计稿（静态稿 + 可交互原型）与落地记录

**P0 已落地**：`sim-core` 数据模型 / 确定性仿真 / 配置包校验 / Modbus TCP（8 功能码 + 异常码）/ 统一写回管线 / 报文日志 / UI 骨架 / 三层测试 / `--selftest` / 绿色包全部完成。
落地过程中遇到的规格冲突与偏离，逐条记录在 `docs/P0_OPEN_QUESTIONS.md`（含 5 条待拍板项）。

**尚待确认（按推荐默认值执行，不阻塞 P0）**：MQTT 客户端是否走自研 codec、项目定位权重（测试替身 / 演示）、P2 的 104 功能范围与验收主站、仓库正式命名、`sim-core` 是否抽成 submodule。
