# 工业协议服务端模拟器 — 技术基线与依赖清单（v0.5 决策定稿）

> 日期：2026-11-11 ｜ 状态：**方案定稿，仍不含代码**
> 上游文档：`SERVER_SIMULATOR_PROPOSAL.md`（v0.1）／ 交付物：`../README.md`（含完整依赖版本清单）
> 本文只补齐三件事：**基调定稿、基础技术栈定稿、依赖/中间件清单**。v0.1 中未被本文覆盖的架构与分期结论继续有效。
>
> **v0.4 修订摘要（2026-11-11 孙总最终拍板）**
> 1. **数据库确定 SQLite**，不引入 MariaDB；§4.2 转为"评估存档"，删掉未验证的可选后端实现，仅保留 `IResultStore` 抽象。
> 2. **UI 确定为「Widgets 主体 + QML 局部」**（点表 / hex 报文 / 设备树用 Widgets；场景时间轴 / 曲线 / 演示动画用 QML）。
> 3. **Qt 6.6.3 起步**（本地已有环境），**编译器确定 MSVC 2022 x64，不考虑 MinGW**。新增 §3.5「Qt 6.6.3 版本边界与升级纪律」：该版本已 EOL、非 LTS，须规避 6.7+ API。
> 4. 依据 2026-11-11 版本复核修正三处：lib60870-C 锁 **2.4.1+**（2.4.0 存在高危 CVE）、mbedTLS 2.28 线已停止维护（TLS 默认不编）、Qt 6.6 无 QtHttpServer / StyleKit / QML 热重载。
>
> **v0.5 最终决定（2026-11-11）**：**Qt 确定使用 6.6.3 起步，不在 P0 前升级**；待 Qt 6.12 系列稳定后，按 §3.5 的判定标准于阶段边界切换。历次候选（6.8 / 6.10 / 6.11）均不作为升级目标。文中所有"暂定 / 建议现在升级"的表述已统一收敛为本决定。

---

## 0. 已拍板决策（全部定稿）

### 0.1 首轮（v0.2）

| # | 决策 | 结论 |
| --- | --- | --- |
| 1 | 语言与框架 | **Qt 6 + C++17，全项目单语言、单基线**；不做 Python 运行时 |
| 2 | 平台 | **Windows x64 优先**，Linux 后续；不为了"以后要跨平台"引入额外抽象框架 |
| 3 | 许可证 | 项目整体开源；lib60870-C / libiec61850 的 GPLv3 直接用，不采购商业授权 |

### 0.2 二轮（v0.3）

| # | 决策 | 结论 |
| --- | --- | --- |
| 4 | 许可证歧义 | ✅ **方案 A**：「本项目不拿去卖钱」，不禁止他人依法商用；方案 B 排除 |
| 5 | QML | ✅ **保留 QML / Qt Quick**，不排斥、不拉黑 |
| 7 | TDengine 复核 | ✅ **确认不需要**（§4.3） |

### 0.3 本轮（v0.4，最终）

| # | 议题 | 结论 |
| --- | --- | --- |
| 6 | 数据库 | ✅ **确定 SQLite**。MariaDB 评估后不采用（§4.2 存档）；`IResultStore` 抽象保留，可选后端**不实现** |
| 8 | UI 主体 | ✅ **Widgets 主体 + QML 局部**：Widgets 承担点表 / hex 报文 / 设备树 / 多页签；QML 承担场景时间轴、曲线预览、演示动画 |
| 9 | Qt 版本 | ✅ **确定使用 Qt 6.6.3 起步，不在 P0 前升级**；待 6.12 系列稳定后按 §3.5 判定标准于阶段边界切换。⚠️ 非 LTS、已 EOL，风险知情接受 |
| 10 | 工具链 | ✅ **MSVC 2022 x64**，不考虑 MinGW。Qt 6.6 官方预编译包为 `msvc2019_64`，MSVC 2022 与之 ABI 兼容可直接使用 |

**基调**：v0.1 的最大约束是"防 GPL 传染"，本轮之后该约束消失，**架构目标从"合规避让"降为"工程稳定性"**。同时接受一个事实：仓库许可证锁定为 GPLv3 系，基本不可逆（§2.5）。

> 关于"Qt 本就是跨平台的"：QML 与 Linux 后续版本无任何冲突。需区分的是**框架跨平台 ≠ 平台差异消失** —— Npcap 原始套接字、Defender 入站规则、命名管道权限、虚拟串口驱动签名、Win32 MiniDump 仍是 Windows 特有（§3.3），放进 `platform/` 隔离即可，不构成限制 UI 技术的理由。

---

## 1. 项目基调

**定位**：给上位机 / 主站 / SCADA 做联调、验收、自动化测试、演示的**协议对手方（test double）**。不是网关、不是采集平台、不是监控平台。

**技术路线一句话**：单进程内存实时库（DataBank）为唯一真相源 + 稳定适配器接口（IProtocolAdapter）+ 多协议端点同时对外 + 场景/故障注入可脚本化 + Windows 绿色包开箱即用。

**v0.1 保持不变的四条骨架**：

1. `sim-core` 不链接任何协议库；`Signal + ProtocolBinding` 统一信号模型。
2. 任一协议写入 → 统一写回管线 → 联动其他协议上送，杜绝"改了 A 读 B 是旧值"。
3. `IProtocolAdapter` 接口稳定、`capabilities()` 驱动 UI 动态渲染；61850 字段与 `schemaVersion` 现在就留。
4. P0 单协议闭环优先（Modbus TCP + FieldLink 互验），104 / 61850 后置。

**许可放开 ≠ 可取消进程边界**：建议保留 104 / 61850 独立 Worker，理由见 §2.6。

---

## 2. 开源路线（方案 A 已定稿）

### 2.1 结论

全仓库统一 **`GPL-3.0-or-later`**，`LICENSE` + 每个源文件 SPDX 头 + `THIRD_PARTY_NOTICES.md`。lib60870-C / libiec61850 可静态链接、可改源码、可分发二进制。

### 2.2 认知纠偏（保留作为依据）

GPLv3 的触发条件是**分发**，不是"商用"；它不禁止商用或收费，也不允许附加"禁止商用"条款。方案 A（我不拿去卖钱，但不禁止他人依法商用）✅ 已确认；方案 B（禁止任何人商用）❌ 与 GPLv3 冲突、已排除。

### 2.3 最小合规清单

- [ ] `LICENSE`（GPLv3 全文）、`COPYING`、每个文件头 `// SPDX-License-Identifier: GPL-3.0-or-later`
- [ ] `THIRD_PARTY_NOTICES.md`：组件名称、版本、许可证、来源 URL、原始版权声明
- [ ] 分发二进制时提供**完整对应源码**（含 CMake、生成脚本、补丁、第三方构建脚本），GPLv3 §5(a) 修改声明与日期
- [ ] 第三方版本固定（submodule tag / vcpkg baseline），保证可复现构建
- [ ] CycloneDX / SPDX SBOM（工具生成，不进运行时）
- [ ] README 写明"本仓库 GPLv3，下游二次分发须同样开源"，避免集成商误当 MIT/LGPL 组件嵌入商业产品

> 工程实践整理，非法律意见；对外商业分发 / SaaS 托管 / 法务审查时请法务复核。

### 2.4 许可证兼容性矩阵

| 组件 | 许可证 | 与 GPLv3 组合 | 使用约束 |
| --- | --- | --- | --- |
| lib60870-C / libiec61850 | GPLv3（MZ 双授权） | ✅ 同源 | 任意链接方式；保留版权与修改声明 |
| Qt 6.6.3 Essentials / Qml / Quick / Quick Controls / SerialPort / Svg / Linguist | LGPLv3 / GPL 系列 / 商业（逐模块核对） | ✅ 按 LGPLv3 档 | 本项目 GPLv3，动态/静态均可 |
| **Qt MQTT** | **GPLv3 / 商业** | ✅ 恰好匹配 | 开源用户拿不到预编译包，见 §5.3 ① |
| **Qt Charts** | **GPLv3 / 商业** | ✅ 恰好匹配 | 全仓 GPLv3 后无额外代价，QML 曲线直接用 |
| Qt SerialBus | LGPLv3 / GPLv2 / 商业 | ✅ 按 LGPLv3 档 | **不要用 GPLv2 档**（与 GPLv3 不兼容） |
| FieldLink 抽取代码 / 主题与 JSON 规范 | MIT | ✅ | 保留原版权声明 |
| libmodbus（不引入） | LGPL-2.1+ | ✅ | — |
| mbedTLS（默认不编） | Apache-2.0 | ✅ | 与 GPLv3 兼容 |
| OpenSSL 3.x（Qt TLS） | Apache-2.0 | ✅ | 用 3.x，别用 1.1 旧双许可 |
| SQLite（Qt QSQLITE 插件内置） | Public Domain | ✅ | 无需单独安装 |
| ~~MariaDB Server~~ | ~~GPLv2（无 or-later）~~ | ⚠️ 不兼容 | **已决定不引入**，见 §4.2 |
| GoogleTest / Catch2 / 小工具 | BSD-3 / BSL-1.0 / MIT | ✅ | — |
| NanoMQ（外部 Broker，可选） | MIT | ✅ | 独立进程，不链接；注意 2026 多个内存安全公告 |
| Eclipse Mosquitto（外部 Broker，可选） | EPL-2.0 或 EDL-1.0 二选一 | ⚠️ EPL-2.0 不兼容 | 取 **EDL-1.0** 档；只作独立进程 |
| EMQX 5.9+ | BSL 1.1 | ❌ | 不采用 |
| TDengine OSS | AGPL-3.0 | ⚠️ 网络 copyleft | 不引入，见 §4.3 |

**红利**：Qt 那些 GPLv3-only 附加模块（MQTT、Charts 等）在 LGPL 路线下要逐个回避，现在全部可用。

### 2.5 不可逆风险

一旦链接 lib60870 / libiec61850 / Qt MQTT / Qt Charts，将来闭源商用需同时：① 采购 MZ 商业授权；② 替换 Qt MQTT；③ 替换 Qt Charts；④ 清理所有 GPL-only 依赖。**成本远高于现在**。唯一缓解：`IProtocolAdapter` 保持纯净、协议实现可替换。

### 2.6 Worker 进程边界（建议保留）

| 原理由 | 新理由 |
| --- | --- |
| 规避 GPL 传染 | ① C 库崩溃不拖垮 UI ② 61850 后期新增 Worker 主程序零改动 ③ 工具链解耦 ④ Worker 可单独复用 ⑤ 单协议压测与故障注入隔离 |

IPC 用 `QLocalSocket` + 长度前缀帧，编码用 Qt6 自带 `QCborValue` / `QDataStream`，不引 protobuf。

---

## 3. 基础技术栈（定稿）

### 3.1 定稿表

| 项 | 定稿 | 说明 |
| --- | --- | --- |
| 语言标准 | **C++17** | `CMAKE_CXX_STANDARD 17` / MSVC `/std:c++17`；不上 C++20 |
| **Qt 版本** | **6.6.3（已确定）** | 本地已有环境；⚠️ 非 LTS、已 EOL，见 §3.5。**本阶段不升级**，未来按 §3.5 判定标准切至 6.12 稳定版 |
| **UI** | **Widgets 主体 + QML 局部** | Widgets：点表 / hex 报文监视 / 站点设备树 / 多页签；QML：场景时间轴 / 曲线预览 / 演示动画。QML 视图用 `QQuickWidget` 或独立 `QQuickView` 嵌入 |
| **编译器** | **MSVC 2022 x64（17.x）** | 不考虑 MinGW。Qt 6.6 官方包 `msvc2019_64` 与 MSVC 2022 ABI 兼容；所有 C 库必须同工具链、同 CRT（`/MD`） |
| 构建 | **CMake 3.25+ + Ninja** | `qt_standard_project_setup()` / `qt_add_executable` / `qt_add_qml_module`；不用 qmake |
| 依赖管理 | Qt：本地安装 / 官方归档；第三方 C 库：git submodule 固定 tag；小工具：vcpkg manifest（可选） | Qt 版本集中在一个 CMake 变量，见 §3.5 |
| IDE | Qt Creator（主，QML 调试最好）／ VS 2022 + Qt VS Tools | — |
| 目标系统 | Windows 10 1809+ / 11，x64 | 不做 ARM64 / 32 位 |
| 部署 | 绿色 zip + `windeployqt --qmldir` + VC++ Redistributable | 后期再做 Inno Setup / NSIS |
| 数据库 | **SQLite（Qt QSQLITE 插件内置）** | 只存冷数据，见 §4.1 |

> UI 路线已定，`sim-core` 与 `IProtocolAdapter` 仍不感知 UI 技术：Widgets 与 QML 都通过同一套模型/信号与内核交互，未来想改全 QML 不需要动内核。

### 3.2 工具链一致性铁律

Qt 预编译库、lib60870-C、libiec61850、mbedTLS、主程序**必须同一编译器、同一 CRT、同一架构**；不出现"MSVC Qt + MinGW C 库"或 `/MD` 与 `/MT` 混用；CMake 显式设 `CMAKE_MSVC_RUNTIME_LIBRARY`。

### 3.3 Windows-first 的具体约束

| # | 事项 | 结论 |
| --- | --- | --- |
| 1 | 低端口 | Windows 监听 502 / 2404 不需管理员权限；默认仍建议 1502 避开占用 |
| 2 | IPC | `QLocalSocket` 走命名管道；设 `QLocalServer::UserAccessOption`，注意会话隔离与杀软拦截 |
| 3 | 串口 RTU | `QSerialPort` 用 `COMn`；虚拟串口对用 com0com / 商业虚拟串口驱动（驱动签名是坑）。P1 的 RTU 优先级低于 TCP |
| 4 | 防火墙 | 首次监听会弹 Defender 入站提示；README / 安装包需说明"允许专用网络" |
| 5 | 编码与路径 | 配置统一 UTF-8 无 BOM；`.gitattributes` 统一 LF，避免 CRLF 污染 CSV/JSON 基线 |
| 6 | 崩溃捕获 | 系统 `MiniDumpWriteDump`（DbgHelp.lib / Windows SDK）+ `qInstallMessageHandler`，零第三方依赖 |
| 7 | 分发 | `windeployqt --qmldir` 收集 QML 模块与 Qt Quick 插件；带 `platforms/qwindows.dll`、`sqldrivers`、`tls`；随包附 VC_redist |
| 8 | 未签名 exe | SmartScreen / 杀软对监听端口的模拟器可能误报，README 提前说明 |
| 9 | QML 图形栈 | Qt Quick 默认 D3D11/OpenGL；老旧虚拟机 / 远程桌面需 `QSG_RHI_BACKEND=software` 兜底并写进 README |

### 3.4 Linux 后续的纪律

"先 Win 后 Linux" 的坑是 Win32 API 渗进业务代码。**除崩溃 dump、防火墙规则、服务集成外，一律用 Qt 封装**（`QProcess` / `QLocalSocket` / `QSerialPort` / `QSettings` / `QStandardPaths` / `QDir` / `QFile` / `QTcpSocket`），平台差异只在 `src/platform/`。CI 常驻 Linux 编译作业提前暴露平台耦合。

### 3.5 Qt 6.6.3 版本边界与升级纪律（v0.5 定稿，重要）

**事实（2026-11-11 核实）**：Qt 6.6 初始发布 2023-10-09，**开源支持 2024-04-02 结束**、商业标准支持 2024-10-09 结束，**最终补丁即 6.6.3（2024-03-26）**；Qt 官方版本页将 6.6 归入 Extended Support（商业扩展支持），**开源用户已约 2.5 年拿不到安全补丁**。6.6 不是 LTS（Qt 6 的 LTS 是 6.2 / 6.5 / 6.8 / 6.12）。

**最终决定与接受理由（v0.5）**：**维持 6.6.3 起步，不在 P0 前升级**；待 6.12 系列稳定后于阶段边界切换。理由是本地环境现成、代码尚未开始、项目为本地联调工具（非公网服务），EOL 风险在知情后接受 —— 这是取舍，不是遗漏。

**必须遵守的纪律**：

1. **版本集中配置**：Qt 版本号只在一处定义（`cmake/ProjectVersions.cmake` 或 `CMAKE_PREFIX_PATH` 变量），CI 与 README 引用同一处；升级时改一处。
2. **禁用 6.7+ API**（否则升级成本飙升）。已核实 6.6 **没有**：`QtHttpServer`（6.7+）、StyleKit（6.12 Labs）、QML 热重载（6.12）、Qt Canvas Painter（6.12）、QRangeModel（6.12）。因此：
   - HTTP 控制 API → **用 `QTcpServer` 手写最小 HTTP/1.1**（或 `cpp-httplib`，MIT），不再考虑 QtHttpServer；
   - QML 主题 → **Qt Quick Controls `palette` + 自定义配色令牌**，不再考虑 StyleKit；
   - QML 开发期热重载 → 无内置支持，靠重启或自研简易重载，写进 README 的已知限制。
3. **模块可用性**：6.6 具备 `qt_add_qml_module`、`QJSEngine`、Qt Quick Controls、Qt Charts（含 QML 类型）、Qt SerialBus（`QModbusTcpServer`）、Qt Test / Qt Quick Test —— 本方案所需全部齐备。
4. **升级目标**：**Qt 6.12 系列的稳定版本**（当前唯一仍在标准支持期、最后一个支持 Win10 的版本）。⚠️ **不要把 6.8 / 6.10 / 6.11 作为升级目标**：已核实 Qt 6.8 开源用户最后可获取的补丁是 **6.8.3（2025-03）**，6.8.4 及之后全为商业专属 Commercial LTS；官方宣传的"LTS 5 年维护 / 持续安全更新"仅对商业授权有效。**Qt 6 的 LTS 只有 6.2 / 6.5 / 6.8 / 6.12**，6.10 / 6.11 是普通 minor 且开源侧已分别于约 2026-04 / 2026-09 停更（6.11.3 为该系列终版，与 6.12.0 几乎同期发布）。同理 6.12 开源版标准支持约到 **2027-04** —— **开源版 Qt 每个 minor 只有约 1 年支持窗口，这是常态，不是某个版本的缺陷**。
   - 补充：Qt 官方把 6.12 定位为"稳定、成熟、两年努力的集大成"，并承诺同一 major 内**源码与二进制兼容**（Technology Preview / Labs 模块除外）。本项目只用成熟模块（Widgets / Qml / Quick Controls / Charts / SerialBus / SerialPort / Sql / Network），不把 StyleKit(Labs) / Canvas Painter / QML 热重载当硬依赖即可规避 `.0` 版本风险。
5. **最终升级策略**：**现在不升级**；两条纪律「版本集中配置 + 不碰高版本 API」因此更重要 —— 它们保证将来切换只是改一处配置 + 重新编译。
6. **"6.12 稳定"的判定标准**（满足全部才启动升级，避免凭感觉）：
   - ① **可得性**：Qt 在线安装器里开源用户**实际能勾选到**该版本（LTS 后续补丁可能商业专属，不按新闻稿判断）；若一直拿不到 6.12.1+，则 6.12.0 发布满约 3~6 个月、社区主要回归报告收敛后亦视为稳定；
   - ② **时间**：6.12 系列发布满约 3 个月，且已出现补丁版本或社区验证充分；
   - ③ **验证**：按 README 的升级验证清单跑通（Qt 版本查询、模块 Config 文件齐全、`cmake --fresh`、Widgets+QML 混合窗口、`windeployqt --qmldir`）；
   - ④ **时机**：只在**阶段边界**（P0 复盘 / P1 开始前）切换，不在迭代中途切；
   - ⑤ **回退**：6.6.3 保留不卸载，验证失败把 `CMAKE_PREFIX_PATH` 指回即可。
7. **风险登记**：已知未修补漏洞不会有人修；若将来对外分发或长期运行，升级为必做项（§9）。

---

## 4. 数据库 / 中间件清单

### 4.0 总判断与判据

**这是"实时状态机 + 报文收发器"，不是数据平台。数据 99% 在内存且由种子可复现，不需要任何独立数据库服务。**

**判据**：任何需要**单独安装、单独启动、单独运维、单独占端口**的组件默认否决，除非能换来当前阶段明确需要、且 SQLite/文件做不到的能力。

### 4.1 逐项结论（定稿）

| 组件 | 要不要 | 结论与理由 | 引入时机 |
| --- | --- | --- | --- |
| **SQLite（Qt QSQLITE）** | ✅ **确定** | 存报文日志、审计、故障剧本、自检结果、场景元数据。**绝不存 DataBank** —— 实时库必须内存结构（`QHash` + 读写锁），否则确定性与 100ms tick 全毁。随 Qt 6.6.3 插件内置，零安装 | P0 用内存环形缓冲 + JSON/CSV 文件；P1 报文量上来后开 WAL、批量事务、按时间滚动清理 |
| ~~MariaDB~~ | ❌ **不引入** | 已评估，见 §4.2 存档 | — |
| Redis | ❌ | 共享状态在单进程内存，IPC 用 QLocalSocket 足够；Windows 无官方 Redis | 触发条件：多实例跨机编排 / 外部脚本 pub/sub，届时也可用 MQTT Broker 替代 |
| **TDengine / 任意时序库** | ❌ **确认不需要** | 见 §4.3。**引入 MariaDB 也不改变此结论** —— 关系库与时序库解决不同问题 | — |
| MySQL / PostgreSQL | ❌ | 无多用户、无事务型业务、无关系查询需求 | — |
| InfluxDB / TimescaleDB / IoTDB / Prometheus | ❌ | 同上，且都要外部服务 | 运行指标看板用 `stats()` 输出 JSON 即可 |
| Kafka / RabbitMQ / ZeroMQ / NNG | ❌ | 无跨系统消息流，MQTT 已覆盖 | — |
| Elasticsearch / Loki / Grafana | ❌ | 报文监视用本地十六进制视图 + 导出文本 | — |
| Docker / K8s（运行期） | ❌ | Windows 桌面工具，不容器化 | CI 里可用容器跑 Linux 编译作业 |
| gRPC / Thrift / protobuf | ❌ | IPC 用 Qt6 CBOR / QDataStream；控制 API 用 JSON | — |
| **MQTT Broker（外部进程，可选）** | ✅ 唯一需要的外部服务 | 它是协议对手方的一部分，不是数据中间件 | P1：外挂 Mosquitto 2.1.2 / NanoMQ 0.25.7 + **内置 mini Broker（默认关闭）**，做到零外部依赖也能跑通 |

> 持久化收敛到 `IResultStore` 接口（报文 / 审计 / 剧本 / 自检结果），业务代码只依赖 `QSqlDatabase`，不出现 `QSQLITE` 字面量散落。**这是架构隔离，不是为 MariaDB 预留实现** —— 不写未验证的可选后端代码。

### 4.2 MariaDB 评估存档（已决定不采用）

**结论**：技术上能用，工程上"不方便"，**按孙总确认维持 SQLite，不再引入**。以下存档，避免以后重复调研。

| # | 不方便之处 |
| --- | --- |
| 1 | Qt 6 不提供 `qsqlmysql.dll` 预编译驱动，须用 `qt-cmake` 从 `qtbase/src/plugins/sqldrivers` 自编，且 Qt 版本 / 编译器 / 架构严格匹配，升一次 Qt 重编一次 |
| 2 | 绿色包要额外带 `qsqlmysql.dll`、`libmariadb.dll`、OpenSSL libcrypto/libssl |
| 3 | 必须跑独立服务：初始化数据目录、账号、3306 端口、防火墙、备份 |
| 4 | **MariaDB Community Server 是 GPLv2（无 or-later）**，与全仓 GPL-3.0-or-later 不兼容；Connector/C 是 LGPL-2.1+ 没问题。Server 只能作用户自装的独立可选进程，不链接、不随主发行包分发 |

**重新评估的触发条件**：多实例共享点表/场景、CI 集中收集结果、需与 FieldLink 或现场系统共用库。若真出现，优先考虑"让外部验收脚本 / CI 连库"，而不是把数据库塞进模拟器。

### 4.3 TDengine / 时序库复核（确认不需要）

1. **定位**：v0.1 §1 明确不做采集轮询、历史归档、报表；数据由种子可复现，回放靠场景脚本而非查历史库。
2. **技术匹配**：时序库解决海量测点长期存储 + 降采样 + 时间窗聚合；本项目 100ms tick、几十到几千点、只需实时曲线与短期报文日志，CSV/Parquet + 内存环形缓冲已覆盖。
3. **平台/许可**：TDengine OSS 为 **AGPL-3.0**，免费层 5000 tags、首次须联网注册激活且每年续期，开源树 **Windows 支持有限**（主要 Linux/macOS）—— 与 Windows-first、绿色包、可能离线现场三条全冲突。
4. **与关系库无关**：引入 MariaDB 不构成引入时序库的理由。

**重新评估触发条件**：定位升级为"演示/培训用虚拟变电站平台"且要求跨天历史趋势回溯，或需向第三方提供长期测点服务。届时顺序仍为 ① CSV/Parquet → ② SQLite 分区表 → ③ 才考虑外挂时序库，并先解决 Windows 部署与 AGPL 合规。架构上保留 `ITelemetrySink` 接口，P0 用内存环形缓冲画实时曲线。

---

## 5. 第三方库 / 组件全清单

### 5.1 A 组：必选核心（P0）

| 组件 | 版本 | 用途 | 许可 |
| --- | --- | --- | --- |
| Qt 6.6.3 Core / Widgets / Network / Sql / Test / Concurrent | 6.6.3 | 内核、Socket、日志、SQLite、单测 | LGPLv3 系 |
| Qt 6.6.3 Qml / Qt Quick / Qt Quick Controls | 6.6.3 | 场景时间轴、曲线、演示动画 | LGPLv3 系 |
| Qt 6.6.3 Qml（`QJSEngine`） | 6.6.3 | 仿真曲线脚本 | LGPLv3 系 |
| Qt 6.6.3 Charts | 6.6.3 | QML / Widgets 曲线图 | **GPLv3 / 商业** |
| Qt 6.6.3 SerialBus | 6.6.3 | `QModbusTcpServer` / `QModbusRtuSerialServer` | LGPLv3（按此档） |
| Qt 6.6.3 SerialPort / Svg / LinguistTools | 6.6.3 | RTU（P1）、图标、中英双语（Widgets `tr()` / QML `qsTr()` 共用 `.ts`） | LGPLv3 系 |
| MSVC 2022 / Windows SDK / CMake ≥3.25 / Ninja / Git | — | 构建 | 各自 |
| vcpkg（manifest，可选） | baseline 待锁 | 小依赖版本锁定 | MIT |
| `THIRD_PARTY_NOTICES.md` / SBOM | — | 合规 | — |

> 完整版本表见 `../README.md`，README 是本项目的**唯一版本真相源**。

### 5.2 B 组：协议栈

| 组件 | 版本 | 用途 | 许可 | 阶段 | 关键提醒 |
| --- | --- | --- | --- | --- | --- |
| — | — | Modbus TCP/RTU 服务端 | — | P0/P1 | **零第三方库**，Qt SerialBus 自带 |
| `libmodbus`（不引入） | 不锁 | 自定义功能码 / 畸形报文 | LGPL-2.1+ | 仅备选 | Qt 封不住时才考虑 |
| **MQTT 客户端** | 见 §5.3 ① | 设备侧发布 / 订阅下行命令 | — | P1 | 推荐自研 `core/mqttcodec` |
| **`lib60870-C`** | **2.4.1+** | IEC 104 服务端 `CS104_Slave` | GPLv3 | P2 | ⚠️ **不要用 2.4.0 及更早**：已核实 2.4.0 存在 CVE-2026-16002（越界读 / CISA 告警）与 CVE-2026-67873（服务端 FileSegment 编码堆溢出，CVSS 9.8）；**2.4.1（2026-07-16）为修复版**。62351-5 安全认证仍是商业 add-on，明确不做 |
| **`libiec61850`** | **1.6.2** | MMS / GOOSE / SV | GPLv3 | P3 | 支持 Windows/Linux/macOS；SCL/CID 运行时建模为商业版特性；库自带 SQLite 实现用于 Log service |
| **`mbedTLS`** | **默认不编** | 两个 C 库的 TLS | Apache-2.0 | 仅启用 TLS 时 | ⚠️ 上版"统一 2.28 LTS"需修正：**2.28.10 是 2.28 LTS 最终版，已停止 bugfix/安全修复**；lib60870 2.4.x 只认 2.28.x，libiec61850 1.6.2 另支持 3.6.x（TLS 1.2/1.3）。**结论：P2 默认不启用 TLS、完全不引 mbedTLS**；确需 TLS 时再单独评估版本与补丁策略，避免引入已 EOL 的加密库 |
| `Npcap` + SDK | 用户自行安装 | **仅 GOOSE / SV 的 Windows 原始套接字** | 专有（免费版限制分发） | P3 且仅做 GOOSE/SV | 现代 Windows 无可用 raw socket；WinPcap 已停维。**MMS（TCP）不需要**；不随绿色包分发驱动 |
| `Mosquitto` | **2.1.2** | 外部 Broker（可选） | EDL-1.0 档 | P1 | 单线程、稳定、社区成熟 |
| `NanoMQ` | **0.25.7** | 外部 Broker（可选） | MIT | P1 | 许可最干净，但已核实 2026 年有较多内存安全公告；仅作联调/故障注入用途、不暴露公网 |

> lib60870 与 libiec61850 共用 MZ 的 HAL/common：分进程天然规避符号冲突；万一合并编译记得 `WITHOUT_HAL` / `WITHOUT_COMMON`。
> **TLS / crypto 现状**：Qt 侧用 OpenSSL 3.x（或 Schannel），C 库侧用 mbedTLS；默认不启用 TLS 时包里只有一套 crypto。

### 5.3 两个技术要点

**① Qt MQTT 在 Windows 开源版下不"开箱可用"（仍待拍板）**

Qt MQTT 开源版是 **商业 / GPLv3**，且在线安装器对开源用户不提供预编译二进制。Qt 6.6.3 对应 `qt/qtmqtt` 的 `v6.6.3` 源码 tag。三个路线：

| 方案 | 做法 | 评价 |
| --- | --- | --- |
| A | 从 `qt/qtmqtt` v6.6.3 源码用 CMake 自编，产物纳入 `third_party/` | 能用，多一条构建链，升级 Qt 要重编 |
| B | 改用 MinGW + MSYS2 的 `qt6-mqtt` 包 | ❌ 已排除（不考虑 MinGW） |
| **C（推荐）** | **自研最小 MQTT 3.1.1 编解码 `core/mqttcodec`**，内置 mini Broker 与设备侧客户端共用 | 模拟器本就要故意拒绝连接、丢 PUBACK、伪造 CONNACK 错误码，商用客户端库反而束手束脚；一套 codec 两处复用、零依赖、许可最干净、Linux 零成本。代价约几百行 + 单测 |

建议主路线 C，Qt MQTT 留作可替换后端。

**② QML 保留后脚本引擎结论反转（已定案）**

QML 保留 → QtQml 已在依赖里 → **直接用 `QJSEngine`**，脚本能力最强、Qt 自带零新增依赖。`exprtk` / `muparser` 降为备选（仅在嫌 QtQml 体积大时）。P0 仍先把 `Curve` 做成接口，脚本引擎 P1 落地。

### 5.4 C 组：数据与工具

| 组件 | 要不要 | 说明 |
| --- | --- | --- |
| JSON | ✅ Qt `QJsonDocument` | 不引 nlohmann/json；配置包 `schemaVersion` + Python `jsonschema` 在 `tools/` 离线校验 |
| CSV | ⚠️ 可选 | 自研解析；或 `rapidcsv`（BSD-3）/ `csv-parser`（MIT） |
| IPC 序列化 | ✅ Qt6 `QCborValue` / `QDataStream` | 不引 protobuf / flatbuffers / msgpack |
| 日志 | ✅ `QLoggingCategory` + 环形缓冲 | 不引 spdlog / Boost.Log |
| **HTTP 控制 API** | ✅ `QTcpServer` 手写最小 HTTP/1.1 | v0.1 §7 的 6 个 JSON 接口。⚠️ **Qt 6.6 没有 QtHttpServer（6.7+）**，该选项已删除；备选 `cpp-httplib`（MIT，header-only）。不引 Boost.Beast / Pistache / Oat++ |
| Token 鉴权 | ✅ 自研 | `QCryptographicHash` HMAC-SHA256 + 常量时间比较 |
| 脚本 | ✅ `QJSEngine` | 见 §5.3 ② |
| 表达式 / 单位换算 | ✅ 自研 | 沿用 FieldLink `dataparser` 纯函数 + 单测 |
| 压缩 / 配置包 | ✅ Qt `QZipWriter/Reader` 或 zlib | 逐文件 SHA-256、路径穿越防护、覆盖前备份 |
| 持久化 | ✅ `IResultStore` + `QSqlDatabase` | 默认 QSQLITE；仅为架构隔离，不实现第二后端 |

### 5.5 D 组：质量、测试、交付（开发期，不进运行包）

| 组件 | 用途 |
| --- | --- |
| Qt Test / **Qt Quick Test** | 三层测试 L1；QML 视图单测 |
| ASan / UBSan / LSan | MSVC 的 ASan 不完整；全套 sanitizer 放 Linux CI（GCC/Clang），Windows 做 MSVC 静态分析 + Cppcheck |
| clang-format / clang-tidy / Cppcheck / **qmllint** | 格式与静态检查，QML 检查进 CI |
| Doxygen | `IProtocolAdapter` / `sim-core` 接口文档 |
| Wireshark + Npcap | 报文级人工验证、pcap 对照 |
| Python 3 + `jsonschema` + `pymodbus`（可选） | `tools/` 验收脚本、假主站、CI 驱动，**不进运行时** |
| GitHub Actions | `windows-2022` 构建 + Linux sanitizer / 跨平台耦合守卫作业 |
| Inno Setup / NSIS / CPack | 安装包（后期）；P0~P1 只出绿色 zip |

### 5.6 E 组：明确不引入

`MariaDB / Redis / TDengine / InfluxDB / TimescaleDB / IoTDB / MySQL / PostgreSQL` · `Kafka / RabbitMQ / ZeroMQ / NNG` · `gRPC / protobuf / Thrift` · `Boost`（整体） · `Qt WebEngine / Qt 3D` · `spdlog / nlohmann::json / fmt` · `OpenCV / 任何 AI 库` · `EMQX（BSL）` · `Qt 商业版` · `libmosquitto（EPL 档）` · `WinPcap（已停维）` · `MinGW 工具链` · `QtHttpServer / StyleKit（6.6 不存在）`。

> ✅ 黑名单已移出：`QML / Qt Quick / Qt Quick Controls / Qt Charts / QJSEngine`（本轮定为 UI 局部方案）。

---

## 6. 非代码但必须现在定下来的东西

| # | 事项 | 定稿 |
| --- | --- | --- |
| 1 | 仓库结构 | `sim-core/` `ui/widgets/` `ui/qml/` `adapters/` `workers/` `config/` `tests/` `tools/` `third_party/` `platform/` `cmake/` |
| 2 | 合规文件 | `LICENSE`、`THIRD_PARTY_NOTICES.md`、SPDX 头、SBOM、修改声明规范 |
| 3 | 版本真相源 | **README.md 的版本表是唯一真相源**；Qt 版本集中到 `cmake/ProjectVersions.cmake`，第三方用 submodule tag，vcpkg 用 baseline；改动需同步三处 |
| 4 | 配置包 | 一律带 `schemaVersion`；61850 的 `iedPath/fc/dataSet/RCB` 字段 P0 留空位 |
| 5 | 统一时标 | 单一 `QTimer` 100ms tick + 确定性种子；104 CP56Time2a、61850 时标、MQTT ISO8601 同源 |
| 6 | 日志与报文 | 每帧记录时间戳/方向/客户端/协议/hex/解析树/耗时；可按协议·客户端·信号过滤；导出 pcap-like 文本 |
| 7 | 远程控制 API | HTTP JSON + Token，6 接口 + `getStats`；默认只监听 127.0.0.1，远程写开关默认关 |
| 8 | 代码规范 | `.clang-format`、`.editorconfig`、`.gitattributes`（LF）、Qt 信号槽 / QML 绑定规范 |
| 9 | **主题机制** | Widgets 用 QSS（沿用 FieldLink 深浅主题）；QML 用 **Qt Quick Controls `palette` + 自定义配色令牌**（Qt 6.6 无 StyleKit）。**一套配色令牌 → 两边各自映射**，避免深浅主题不同步；双语共用 `.ts` |
| 10 | 文档 | ✅ 已产出：`../README.md`、`config/schema/sim-config.schema.json`、`config/examples/fieldlink_preset.json`（通过 Schema 校验）、`docs/INTERFACES.md`、`docs/P0_PLAN.md`。下一步按 `docs/P0_PLAN.md` 的 P0-1 落地（**仍不写实现代码**） |
| 11 | 安全 | 路径穿越防护、配置包覆盖前备份、Token 常量时间比较、远程写默认关闭、内置 Broker 默认关闭并标注"联调/故障注入专用" |

---

## 7. 依赖引入节奏

| 阶段 | 新增依赖 | 交付形态 / 验收 |
| --- | --- | --- |
| **P0** | Qt 6.6.3（Core / Widgets / Network / Test / SerialBus / Qml / Quick / Quick Controls / Charts / QJSEngine）+ MSVC 2022 + CMake；**零第三方 C 库、零数据库服务** | 绿色 zip；FieldLink 连 1502 读到正弦温度；FC16 写 → FC03 回读一致 |
| **P1** | + QSQLITE（`IResultStore`）、QtSerialPort、自研 MQTT codec + 内置 mini Broker、可选外挂 Mosquitto 2.1.2 / NanoMQ 0.25.7、CSV 解析、`QTcpServer` HTTP API | 故障注入可脚本化；FieldLink MQTT 主题兼容；无外部服务也能跑通 |
| **P2** | + `lib60870-C` **2.4.1**（submodule）、Worker 进程 + QLocalSocket/CBOR IPC、崩溃 MiniDump；**TLS 默认不编，不引 mbedTLS** | 104 总召唤 + 遥控闭环；Worker 崩溃不影响 UI |
| **P3** | + `libiec61850` **1.6.2**（submodule）、（仅 GOOSE/SV 时）用户自装 Npcap + SDK、SCL/ICD 解析 | 与 61850 客户端互认、报告触发 |

每阶段结束做一次**依赖盘点**：没用到的库立刻移除，不让"以后可能用"变成常驻依赖。

---

## 8. 决策状态

### 8.1 已确认（定稿）

✅ 许可证方案 A（全仓 GPL-3.0-or-later）· ✅ QML 保留 · ✅ UI = Widgets 主体 + QML 局部 · ✅ **Qt 6.6.3 起步、暂不升级（§3.5 判定标准）** · ✅ MSVC 2022 x64、不考虑 MinGW · ✅ SQLite · ✅ 不引入 MariaDB / Redis / TDengine / 任何时序库 · ✅ 脚本引擎用 `QJSEngine` · ✅ HTTP API 用 `QTcpServer` 手写 · ✅ 104/61850 保留 Worker（建议值）

### 8.2 仍待确认（按推荐默认值执行，不影响 README 与 P0 启动）

1. **MQTT 客户端路线**：自研 codec（推荐，暂按此）／ 源码编译 qtmqtt v6.6.3／ 两者都要。
2. **项目定位权重**：自动化测试替身优先（暂按此）／ 客户演示虚拟站点优先 —— 决定 QML 动画投入比例。
3. **104 范围**：P2 是否必须含时钟同步、SBO、冗余组？有无指定主站/规约测试仪做兼容性验收？
4. **仓库命名与共享核心**：新仓库名（`ProtoSim` / `StationSim` / 其它，README 暂用 ProtoSim）；`sim-core` 先内置（暂按此）还是直接抽 submodule。

---

## 9. 风险清单（v0.5 更新）

| 风险 | 影响 | 缓解 |
| --- | --- | --- |
| **Qt 6.6.3 已 EOL、非 LTS、无安全补丁** | 已知漏洞无人修；长期运行/对外分发有隐患 | **知情接受、暂不升级**；§3.5 两条纪律（版本集中配置、禁用 6.7+ API）+ 五条升级判定标准，满足后于阶段边界切 6.12；README 明示 |
| **lib60870 2.4.0 及更早有高危 CVE** | P2 引入带漏洞的协议栈 | 锁 **2.4.1+**；submodule 固定 tag；升级时核对 CHANGELOG / 安全公告 |
| **mbedTLS 2.28 线已停止维护** | 引入 EOL 加密库 | P2 默认不启用 TLS、不引 mbedTLS；确需 TLS 时单独评估 3.6.x 与补丁策略 |
| 全仓 GPLv3 路线不可逆 | 未来闭源商用成本高 | §2.5 知情确认；`IProtocolAdapter` 保持实现可替换 |
| Qt MQTT 开源版无预编译包 + GPLv3-only | P1 卡壳 | §5.3 ① 方案 C 自研 codec |
| Windows GOOSE/SV 依赖 Npcap（专有、免费版限制分发） | P3 不能随绿色包自由分发 | MMS 先行；GOOSE/SV 标注"需自行安装 Npcap" |
| **Widgets + QML 混合：两套主题 / 两套测试栈** | 深浅主题不同步、CI 变复杂 | §6.9 统一配色令牌；`qmllint` + Qt Quick Test 进 CI |
| NanoMQ 0.25.7 有较多 2026 内存安全公告 | 外挂 Broker 暴露时有风险 | 仅作本地联调/故障注入，默认用内置 mini Broker；不暴露公网，或改用 Mosquitto 2.1.2 |
| 工具链混用 | 链接/运行时诡异崩溃 | §3.2 铁律 + CMake 显式运行时库 |
| "先 Win 后 Linux" 变 Win32 绑死 | Linux 版等于重写 | §3.4 平台隔离（**P0 阶段已取消 CI 常驻 Linux 守卫**，理由见 `P0_PLAN.md` §7；平台隔离纪律本身不变） |
| 数据库被误当成实时库 | 确定性与性能崩塌 | §4.1 明确 DataBank 只在内存；`ITelemetrySink` / `IResultStore` 承接扩展 |
| 依赖膨胀 | 打包体积、合规清单、构建时间失控 | §7 分阶段引入 + 每阶段盘点 |

---

> 事实核对日期 2026-11-11：Qt 6.6 EOL（开源支持 2024-04-02 结束，最终 6.6.3）、Qt 6.12 LTS / Windows 支持配置、Qt MQTT 与 Qt Charts 许可、Qt SerialBus 许可、lib60870-C 2.4.1 与 2.4.0 的 CVE、libiec61850 1.6.2、mbedTLS 2.28.10 为 2.28 LTS 最终版且停止维护、TDengine AGPL-3.0 与 Windows 支持、Mosquitto 2.1.2 / NanoMQ 0.25.7、MariaDB Server GPLv2 与 Connector/C LGPL-2.1+、Qt 6 无 QMYSQL 预编译驱动。**版本与 CVE 状态会变化，立项与每次依赖升级前请复核官方公告。**
