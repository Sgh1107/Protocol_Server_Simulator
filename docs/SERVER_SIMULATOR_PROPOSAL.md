# 工业协议服务端模拟器 — 立项方案（v0.1 讨论稿）

> 日期：2026-11-11 ｜ 状态：**仅方案，不含代码**
> 调研基线：FieldLink v2.1.0（Qt 5.15.2 / C++17 / MIT）
> 短期范围：Modbus、IEC 60870-5-104、MQTT；后期预留 IEC 61850 服务端
> 本文件放在 FieldLink 工作区根目录仅作为提案载体，未改动现有工程任何代码或配置；立项后整体移入新仓库即可。

---

## 0. 一页结论

1. **独立立项是对的**，但复用方式要从"拷代码"改成"抽规范"：把点表模型、数据类型/字节序换算、质量码、主题与 JSON 载荷规范抽成一个无协议库依赖的共享核心，两个仓库各自引用，避免双份维护。
2. **Qt C++ 是对的**，但有两处需要纠偏：
   - **Modbus 服务端不必引入 libmodbus**。Qt SerialBus 自带 `QModbusTcpServer` / `QModbusRtuSerialServer`（Qt 5 中名为 `...Slave`），与 FieldLink 完全同栈，零额外依赖。libmodbus 降级为"需要自定义功能码 / 原始报文 / 异常注入 / 多 unit-id 路由"时的可选后端。
   - **MQTT 的"服务端"语义必须先澄清**。Qt MQTT 模块只有客户端（`QMqttClient`），没有 Broker。建议模拟器扮演**设备侧**（发布遥测 + 订阅下行命令），Broker 用外部 Mosquitto / NanoMQ 进程；内置极简 Broker 作为可选联调插件。
3. **最大的风险不是技术，是许可证**：`lib60870-C`（104）与 `libiec61850`（61850）均为 **GPLv3 / 商业双授权**。若本项目未来闭源商用，直接静态链接会把开源义务带到整个产品。架构上必须为它们预留**独立协议 Worker 进程**边界，或直接采购商业授权。这是本方案最需要孙总先拍板的一件事。
4. **架构核心是"一个实时库，多协议同时对外"**：同一个信号点可同时被 Modbus 读取、被 104 突发上送、被 MQTT 发布、被 61850 报告；任一协议写入都回写实时库并联动其他协议。这是模拟器相对 FieldLink 内置 Python 从站的本质升级，也是它真正值钱的地方。
5. **分期建议**：P0 先做 Modbus TCP + 统一实时库 + 配置包 + 报文日志，立刻能用 FieldLink 当"现成主站"做闭环互验；P1 加 MQTT 与故障注入；P2 上 104 Worker；P3 才接 61850。不要一上来三协议齐头并进。

---

## 1. 定位与边界（假设，待确认）

**假设定位**：这是一台给上位机 / 主站 / SCADA 做**联调、验收、自动化测试、演示**用的"协议对手方"（test double），不是生产级网关、不是采集平台。

由此确定的边界：

| 做 | 不做 |
| --- | --- |
| 多协议服务端同时在线、共享数据源 | 采集轮询、历史归档、报警规则、报表、用户权限体系（这些是 FieldLink 的职责） |
| 点表 / 场景可编排、可导入导出、可复现 | 真实设备驱动、实际 I/O |
| 故障注入与异常报文构造 | 生产环境长期无人值守的高可用集群 |
| 报文级监视、协议一致性自检 | 做成一个"全能监控平台" |
| 远程控制 API，便于 CI / 自动化脚本驱动 | Python 作为正式协议实现（Python 只留在 `tools/` 做验收脚本） |

> 若定位偏向"客户演示 / 虚拟变电站"，则 UI 可视化与场景动画权重上升；若偏向"自动化测试"，则 HTTP 控制 API、脚本化、可复现种子、确定性时钟权重上升。建议以测试替身为先，两者兼顾。

---

## 2. 从 FieldLink 借鉴什么，不借鉴什么

FieldLink 目前是**纯 Modbus 主站 + MQTT 发布端**，仓库内没有任何 104 / 61850 痕迹（已全文检索确认），所以这两块是完全新增。

### 2.1 直接可借鉴的资产

| FieldLink 资产 | 借鉴方式 |
| --- | --- |
| `header/devicetemplate.h`、`header/pointmodel.h` 的点表定义（数据类型、ABCD/DCBA/BADC/CDAB 字节序、scale/offset/单位、质量码、归档死区） | 抽象为与协议无关的 `Signal` + `ProtocolBinding`，剥掉 Modbus 专属字段 |
| `header/dataparser.h` / `src/dataparser.cpp` 的寄存器 ↔ 工程值换算 | 抽成 `core` 纯函数，连同测试用例一起搬，作为第一个单测目标 |
| `slave/modbus_tcp_simulator.py` | **协议行为与自检用例的最小参照**（FC01/02/03/04/05/06/0F/10、异常码 0x01/0x02/0x03、地址越界、位打包、MBAP 收发）；C++ 重写，保留 `--selftest` 思路 |
| `slave/mqtt_test_broker.py` | 内置 Broker 的协议子集边界，以及两个踩坑经验：**连接级半包缓冲**、**socket 超时不能当 EOF**（否则 KeepAlive 60s 的客户端会被误踢） |
| `header/mqttclient.h` + `doc/MQTT_GUIDE.md` 的主题与 JSON 规范 | **直接沿用** `fieldlink/data/{从站}/{寄存器类型}/{起始地址}` 等主题格式与载荷结构 —— 这样 FieldLink 不用改一行代码就能连上新模拟器，形成闭环互验 |
| `header/siteconfigbundle.h`（单 JSON 配置包、逐文件 SHA-256、路径穿越防护、覆盖前备份） | 场景 / 点表配置包的导入导出与安全校验 |
| `header/scriptengine.h`（QJSEngine 脚本控制台） | 仿真曲线脚本化（正弦之外可写自定义工况） |
| `header/devicesimulator.h`（QProcess 管理独立进程、优雅停止、日志回显） | **协议 Worker 进程生命周期管理的现成范式**，其"独立实现互验、避免同进程自连的自洽假象"理念直接继承 |
| `header/remoteserver.h`（HTTP JSON API + Token 鉴权 + 远程写开关） | 给模拟器加远程控制 API：启动 / 停止 / 加载场景 / 注入故障 / 强制置数，供 CI 驱动 |
| `header/reliabilitymanager.h`、`crashlogger.h`、`logviewer.h` | 长稳运行、崩溃捕获、报文级日志查看 |
| `style/dark.qss`、`light.qss`、`translations/`、`thememanager.h` | 工业风深浅主题、中英双语，同构即可，省一轮 UI 基建 |
| `tests/` + `tools/run_tests.sh` + GitHub Actions（Qt6 + ASan/UBSan/LSan） | 三层测试骨架（纯逻辑单测 → 假主站 E2E → 真实主站互验）与 CI 直接搬 |

### 2.2 明确不借鉴

- **不搬 `mainwindow.cpp` / `mainwindow_advanced.cpp` 的单体主窗口结构**：那是采集主站视角（连接、轮询、报警、历史），模拟器是服务端视角（端点、点表、场景、报文、故障），信息架构完全不同，硬搬会得到一个四不像。
- **不引入主站侧业务功能**（报警、历史库、报表、备份、安全三级模型），保持内核精简；远程 API 的鉴权可以极简。
- **不用 Python 子进程承载正式协议实现**：FieldLink 里 Python 只是"交叉验证参照物"，新项目要单语言自洽、便于打包交付。

---

## 3. 总体架构

### 3.1 分层

```
┌──────────────────────────────────────────────────────────────┐
│ sim-ui  (Qt Widgets)                                         │
│  站点/设备树 · 点表(实时值/质量/强制) · 场景时间轴 · 端点状态      │
│  报文监视(hex+解析树) · 故障注入 · 日志 · 配置包                  │
├──────────────────────────────────────────────────────────────┤
│ sim-core  (纯 Qt/C++，**不链接任何协议库**)                     │
│  SignalModel · DataBank(实时库/质量码/时标) · SimulationEngine  │
│  Scenario/Curve/Script(QJSEngine) · FaultInjector · EventBus   │
│  ConfigBundle(JSON/CSV) · TelemetryLog · ControlApi            │
├──────────────────────────────────────────────────────────────┤
│ IProtocolAdapter  (稳定接口，是 61850 能否平滑接入的关键)        │
│  start/stop · bind(Signal↔协议地址) · read/write · onTelemetry │
│  injectFault · stats · capabilities(声明支持的协议能力)          │
├───────────────────────────┬──────────────────────────────────┤
│ 进程内适配器                │ 独立 Worker 进程 (QLocalSocket IPC)│
│  Modbus / MQTT             │  IEC 104 / (后期) IEC 61850       │
│  Qt SerialBus / Qt MQTT    │  lib60870-C / libiec61850         │
└───────────────────────────┴──────────────────────────────────┘
```

**进程边界不是性能设计，是许可证与稳定性设计**：GPL 库单独成进程，C 库崩溃不拖垮 UI；后期 61850 新增一个 Worker 即可，主程序零改动。IPC 建议 `QLocalSocket` + 长度前缀 JSON（无额外依赖），接口稳定后可换 protobuf。

### 3.2 一个实时库，多协议同时对外

```
        ┌──────────── DataBank (唯一真相源) ────────────┐
        │  Signal{ value, quality, timestamp, forced }  │
        └──┬──────────┬──────────┬──────────┬──────────┘
           │          │          │          │
   Modbus FC03  104 ASDU 9   MQTT publish  61850 Report
   读 HR[0]     突发上送     fieldlink/...  (后期)
           │          │          │          │
   Modbus FC16  104 C_SC   MQTT cmd   61850 Control
   写 HR[0]  ───► 统一写回 + 审计 + 联动触发其他协议上送
```

任何一路写入都走同一条写回管线（含写保护、联锁、审计），避免"从 104 改了值，Modbus 读出来还是旧的"这类不一致。这点要写进 MVP 验收口径。

### 3.3 统一信号模型（`Signal` + `ProtocolBinding`）

在 FieldLink `PointDefinition` 基础上扩展，一个 `Signal` 可挂 N 个 `ProtocolBinding`：

| 公共字段 | 说明 |
| --- | --- |
| `id / name / deviceId / description` | 设备维度必须有（FieldLink 的教训：同从站地址多设备会串数据） |
| `category` | AI / DI / AO / DO / 遥测 / 遥信 / 遥控 / 遥调 / 定值 / 计量 |
| `dataType / byteOrder` | uint16/int16/uint32/int32/float32/float64/ascii；ABCD/DCBA/BADC/CDAB |
| `scale / offset / unit / range` | 工程值换算与上下限 |
| `value / quality / timestamp` | 质量码沿用 Good/Bad/Timeout/OutOfRange/Unknown |
| `behavior` | 常量 / 正弦 / 随机游走 / 斜坡 / 阶跃序列 / CSV 回放 / 脚本 / 手工强制 |
| `bindings[]` | 见下表 |

| 协议 | Binding 关键字段 |
| --- | --- |
| Modbus | `unitId`、`registerType`(Coils/Discrete/Holding/Input)、`address`、`count`、`fc` 覆写 |
| IEC 104 | `asduType`、`commonAddress`(CA)、`infoObjectAddress`(IOA)、`COT`、时标类型(CP56Time2a) |
| MQTT | `topic 模板`、`qos`、`retain`、`payload 模板`(JSON)、`cmdTopic`(下行写回) |
| IEC 61850（预留字段，现在就留） | `iedPath`(LD/LN/DO/DA)、`fc`、`dataSet`、`reportControlBlock`、`mmsType` |

> 61850 的字段**现在就要留**，后期不必改配置包 schema 就能接入；同时配置包必须带 `schemaVersion`，为迁移留后路。

### 3.4 适配器接口要点

```
IProtocolAdapter {
  capabilities()            // 声明支持的读写/上报/故障类型，UI 据此动态渲染
  start(config) / stop()
  bind(bindings)            // 从配置包装载 Signal↔协议地址映射
  onRead(request)  -> DataBank 读快照（带质量码）
  onWrite(request) -> 写回管线（联动其他协议）
  pushTelemetry(changedSignals)  // 实时库变化 → 主动上送/突发/发布
  injectFault(faultSpec)    // 延迟/丢包/异常码/断链/质量置坏
  stats()                   // 连接数、帧计数、错误计数
}
```

Worker 版适配器 = 同一接口的 IPC 代理，UI 与 core 感知不到差异。

---

## 4. 协议与第三方库选型

### 4.1 选型总表（许可证已核对，2026-11）

| 组件 | 用途 | 许可证 | 集成方式 | 备注 |
| --- | --- | --- | --- | --- |
| Qt 5.15 / 6.x（Widgets / SerialBus / SerialPort / Network / Sql / Qml / Mqtt） | UI + Modbus 服务端 + MQTT 客户端 | LGPLv3 / GPLv3 / 商业 | 动态链接 | 闭源分发遵守 LGPL 动态链接义务 |
| **libmodbus**（可选） | Modbus 底层报文 / 异常注入 | **LGPL-2.1+** | 动态链接或 Worker | 商用友好，但 MVP 可完全不用 |
| **lib60870-C** | IEC 104 服务端 | **GPLv3 / 商业双授权**（MZ Automation） | **独立 Worker 进程** | 支持 CS104 client/server、101、冗余组、文件服务、TLS(需 mbedtls)；62351-5 安全认证为商业 add-on |
| **libiec61850**（后期） | MMS 服务端 / GOOSE / SV | **GPLv3 / 商业双授权** | **独立 Worker 进程** | 1.6.x；SCL/CID 运行时建模为商业版特性 |
| Eclipse Mosquitto | 外部 MQTT Broker | EPL-2.0 / EDL-1.0 | 外部进程 | 3.1.1 / 5.0，事实标准 |
| NanoMQ（备选） | 外部 MQTT Broker | MIT | 外部进程 | 更宽松，多线程 |
| mbedtls（可选） | TLS | Apache-2.0 | 静态 | lib60870 / libiec61850 的 TLS 依赖 |
| SQLite / QtSql | 场景、报文日志 | Public Domain | QtSql | 沿用 FieldLink 三库分离思路 |

> lib60870 与 libiec61850 共享 MZ Automation 的 common/HAL 代码，两个 Worker 同时编译注意 `WITHOUT_HAL` / `WITHOUT_COMMON` 选项，避免符号冲突；分成两个独立进程后天然规避。

### 4.2 Modbus 服务端

- **首选 Qt SerialBus**：`QModbusTcpServer` / `QModbusRtuSerialServer`（Qt 5 里叫 `QModbusRtuSerialSlave`），`setMap()` 直接把 `QModbusDataUnit` 映射表喂进去，读写回调里回写 DataBank。与 FieldLink 同栈，团队零学习成本，异常码/边界由 Qt 处理。
- **libmodbus 作为可选后端**：当 Qt 封不住的场景出现时启用 —— 自定义/未封装功能码（FieldLink 的 `rawrequestdialog` 已覆盖 FC07/08/0B/0C/11/16/17/18/2B·0E 等）、刻意构造畸形/异常响应、单端口多 unit-id 路由、更细的监听控制（`modbus_tcp_listen` / `modbus_receive` / `modbus_reply`）。
- **能力清单**：TCP（默认 1502，可配 502）+ RTU（串口）；FC01/02/03/04/05/06/0F/10 起步，逐步加 07/08/11/17/2B；多从站地址、多台虚拟设备；地址越界与非法功能异常码；连接数上限 / 白名单。

### 4.3 IEC 60870-5-104 服务端（P2）

- **实现**：`lib60870-C` 的 `CS104_Slave`，包在 Worker 进程里。
- **链路层**：TCP 2404；STARTDT/CON、STOPDT、TESTFR；U/I/S 帧；`k / w / t0 / t1 / t2 / t3` 参数全部可配并纳入故障注入。
- **ASDU 子集（MVP）**：
  - 监视方向：Type 1（单点）、3（双点）、9（归一化）、11（标度化）、13（浮点）、30/31/…带 CP56Time2a 时标
  - 控制方向：45/46（单点/双点命令，含 SBO 选择-执行）、48/49/50（设点命令）
  - 系统：100（总召唤 C_IC_NA_1）、103（时钟同步）、读命令、初始化结束、突发/周期上送队列
  - `CA / IOA / COT` 与点表绑定，总召唤响应可按组分批
- **连接管理**：单主站起步 → 多主站；冗余组放后期。
- **明确不做**：文件服务、101 串口链路、IEC 62351-3/-5 安全认证（商业 add-on）、完整 ASDU 全集。
- **替代路径**（若拒绝 GPL 且不买授权）：自研 104 最小子集（APCI + 上述 10 余种 ASDU），工作量约 3~5 人周，且一致性风险高，**不推荐**，仅作为兜底。

### 4.4 MQTT

角色必须区分清楚，三者能力不同：

| 角色 | 是否需要 | 方案 |
| --- | --- | --- |
| **设备侧 Publisher / Subscriber** | **必须有** | Qt MQTT `QMqttClient`（3.1 / 3.1.1 / 5.0，QoS 0/1/2、retain、Will、TLS、WebSocket）。上行按主题模板发布遥测，下行订阅命令主题并写回 DataBank |
| **Broker** | 视需求 | **默认外挂** Mosquitto / NanoMQ（EPL / MIT，进程隔离无传染）；**可选内置**极简 Broker（QTcpServer 实现 3.1.1 子集：CONNECT/CONNACK/PUBLISH/SUBSCRIBE/PINGREQ/DISCONNECT、QoS0/1、`+`/`#` 通配符、retain、认证），照搬 FieldLink Python mini broker 的行为边界，专供离线联调与可控故障注入 |
| **与 FieldLink 互验** | 强烈建议 | 主题与 JSON 直接兼容 `doc/MQTT_GUIDE.md`，FieldLink 的 MQTT 发布端 / 迷你 broker / `tools/mqtt_test.ps1` 立刻变成现成的验收工具 |

> 关键取舍：自研 Broker 的价值不在"功能全"，而在**能故意拒绝连接、丢 QoS1 的 PUBACK、伪造 CONNACK 错误码** —— 这些是商用 Broker 做不到的测试能力。因此建议内置 Broker 定位为"故障注入专用"，不追求生产级。

### 4.5 IEC 61850 服务端预留（P3，不实现）

- 不自研 MMS 全栈，用 `libiec61850`（MMS server / GOOSE / SV）。
- 现在要做的四件预留：① `Signal` 里留 `iedPath / fc / dataSet / RCB` 字段；② `IProtocolAdapter` 接口稳定；③ Worker 进程边界与 IPC 协议定型；④ 配置包带 `schemaVersion`。
- 后期工作集中在：SCL/ICD/CID 解析 → DataBank 映射、报告控制块（URCB/BRCB）、控制模型、数据集、GOOSE/SV 是否纳入。
- 与 104 同属 MZ Automation 生态，Worker 化后两套 HAL 互不干扰。

### 4.6 许可证红线（最需要拍板）

**GPLv3 的两条现实路径：**

| 路径 | 做法 | 代价 | 适用 |
| --- | --- | --- | --- |
| **A. 商业授权** | 向 MZ Automation 采购 lib60870 / libiec61850 商业许可，静态链接进单一进程 | 采购成本 | 产品化、闭源分发、架构最简单 |
| **B. GPL + 进程隔离** | 104 / 61850 各自独立 Worker 进程，GPL 部分单独开源，主程序仅通过 IPC 与之通信 | Worker 二进制需按 GPL 开源；有法务不确定性 | 内部工具 / 研究 / 可开源组件 |

> **必须如实说明**：进程隔离能显著简化许可边界，但**不是法律豁免** —— 若 Worker 与主程序被认定为同一产品的组成部分，仍可能被视作组合作品。这不是法律意见，闭源商用前请法务确认，或直接走路径 A。
> 相对地，libmodbus（LGPL-2.1+）、Mosquitto（EPL/EDL）、NanoMQ（MIT）、mbedtls（Apache-2.0）都是商用友好的，可以放心动态链接或外挂进程。FieldLink 本身是 MIT，从中抽取的代码在新仓库中可自由使用（保留版权声明即可）。

---

## 5. 仿真引擎与故障注入

**统一时钟**：单一 `QTimer` 基准（默认 100 ms tick），支持播放 / 暂停 / 单步 / 倍速 / 重置 / 确定性随机种子（保证自动化用例可复现）。所有协议共享同一时标源，104 的 CP56Time2a、61850 时标、MQTT ISO8601 必须一致。

**数据源**：常量、正弦、随机游走、斜坡、阶跃序列、CSV 曲线回放、`QJSEngine` 脚本、手工强制值（带"强制/解除"标记与审计）。

**故障注入清单**（模拟器的核心卖点，建议按协议分组做成面板 + 可脚本化 API）：

| 层面 | 可注入项 |
| --- | --- |
| 链路 | 拒绝连接、连接数上限、空闲断链、半开连接、响应延迟/抖动、丢包、端口占用报错 |
| 协议 | Modbus 异常码 0x01~0x04、非法功能码、畸形 MBAP；104 的 U 帧超时、k/w 拥塞、总召唤丢弃、COT 异常、SBO 超时；MQTT CONNACK 拒绝码、丢 PUBACK、retain 覆盖、Will 触发 |
| 数据 | 质量码置 Bad/Timeout、数值冻结、越限、抖动、时标回退、写保护/联锁拒绝 |
| 场景 | 一键"断网 30s 后恢复"、"遥信抖动 10 次"、"遥测越限 5s"等预设剧本，可导出复用 |

**报文监视**：每条帧记录时间戳、方向、客户端、协议类型、原始 hex、解析树、耗时；可按协议/客户端/信号过滤，支持导出 pcap-like 文本供问题定位。

---

## 6. UI 信息架构（草案）

```
┌ 工具栏：场景播放/暂停/倍速/单步/重置 │ 载入预设 │ 全部端点启/停 ┐
├──────────┬────────────────────────────────┬───────────────────┤
│ 站点/设备树│ 点表：名称/实时值/质量/时标/强制值 │ 协议端点状态       │
│ (多设备、  │ -------------------------------- │ Modbus :1502 ✅   │
│  多端点)  │ 场景时间轴 + 曲线预览             │ 104   :2404 ⏸    │
│          │                                │ MQTT  →broker ✅  │
│          │                                │ 在线客户端列表/计数│
├──────────┴────────────────────────────────┴───────────────────┤
│ 底部多页签：报文监视(hex+解析) │ 故障注入 │ 日志 │ 协议自检结果      │
└────────────────────────────────────────────────────────────────┘
```

沿用 FieldLink 的深浅主题 QSS、中英双语、Fusion 风格。建议内置一个 **"FieldLink 联调预设"**：一键起一个 Modbus TCP 从站（reg0 正弦温度、reg1 随机游走、reg2=42）+ MQTT 主题模板，启动后直接拿 FieldLink 连 `127.0.0.1:1502` 就能验收，把两个项目的协同做成开箱体验。

---

## 7. 仓库 / 构建 / 测试 / 交付

- **仓库**：`FieldLink-Simulator`（或 `ProtoSim` / `StationSim`，待定），独立 git 仓库，与 FieldLink 同 org 但不同生命周期、不同版本号、不同 Release 节奏。
- **共享核心**：`sim-core` 独立成库（可先作为新仓库内的 `core/` 子目录， stabilize 后再抽成 submodule 或独立包），FieldLink 侧**不反向依赖模拟器**。
- **构建**：CMake 为主（要同时编 C 库、Qt 模块、多个 Worker 与 CI），qmake 仅作过渡；C++17 起步。
- **Qt 基线建议**：新项目无历史包袱，建议**单基线 Qt 6.2+（推荐 6.5 / 6.8 LTS）**，避免维护 FieldLink 那套 `QModbusRtuSerialMaster/Client` 条件编译；复用其代码片段时注意 Qt5→Qt6 的 `Slave→Server` 改名。若要求与现场 5.15.2 MinGW 环境完全一致，再退回 5.15.2 双基线。
- **第三方库**：git submodule + 预编译产物 / vcpkg，Windows（MinGW 或 MSVC）与 Linux 双平台；GPL 库不进主程序链接线。
- **测试三层**（沿用 FieldLink 的验证哲学）：
  1. 纯逻辑单测（无 GUI）：字节序换算、点表/绑定、配置包往返与原子性、异常码构造、104 ASDU 编解码、MQTT 报文编解码、写回联动；
  2. 进程内假主站 E2E：假 Modbus master / 假 104 主站 / 假 MQTT client 走真实 socket；
  3. **真实主站互验**：FieldLink 直接连模拟器跑全链路；内置协议自检 `--selftest`（照搬 Python 从站的 9 项自检思路）。
- **CI**：GitHub Actions（Qt6 + ASan/UBSan/LSan，Linux 作业）；交付脚本沿用 `deliverymanager` 的思路（环境检查、日志打包、发布说明、Windows 打包）。
- **远程控制 API**：HTTP JSON + Token，提供 `loadScenario / start / stop / forceValue / injectFault / getStats`，让自动化用例与 CI 能无人值守驱动。

---

## 8. 分期路线（单人投入粗估，不作为交付承诺）

| 阶段 | 内容 | 产出 / 验收口径 | 粗估 |
| --- | --- | --- | --- |
| **P0** | `sim-core`（SignalModel / DataBank / 配置包 / 字节序换算）、Qt UI 骨架、Modbus TCP 服务端（Qt SerialBus）、常量+正弦仿真、报文日志、单测 | 启动模拟器 → FieldLink 连 1502 读到正弦温度；写 FC16 后 FC03 回读一致 | 2~3 周 |
| **P1** | Modbus RTU + 多 unit/多设备、点表 CSV/JSON 导入导出、异常注入、MQTT 设备侧（外挂 Mosquitto）、内置 mini Broker、与 FieldLink 主题兼容 | FieldLink 的 MQTT 测试脚本能跑通；故障注入可脚本化 | 3~4 周 |
| **P2** | 104 Worker（lib60870-C）：总召唤、突发/周期上送、SBO 命令、时钟同步、k/w 参数；场景回放；远程 API | 用第三方 104 主站（或自写假主站）完成总召唤 + 遥控闭环；Worker 崩溃不影响 UI | 4~6 周 |
| **P3** | 61850 Worker：SCL 导入、MMS 报告/控制、数据集；GOOSE/SV 视需求 | 与 61850 客户端互认、报告触发 | 另行评估 |

> 建议 P0 结束时先做一次方案复盘，再决定是否调整 P2 的 104 实现路径（自研 vs 授权）。

---

## 9. 风险清单

| 风险 | 影响 | 缓解 |
| --- | --- | --- |
| GPLv3 库与闭源商用冲突 | 可能无法产品化 | 立项阶段就定许可证路径；GPL 库一律 Worker 化 / 采购授权 |
| 三协议同时开发导致战线过长 | 迟迟拿不到可用版本 | P0 单协议闭环优先，104/61850 后置 |
| Qt 5.15 / Qt 6 API 差异 | 复用代码时编译踩坑 | 单基线 Qt6；复用片段逐处核对 `Slave→Server` 改名 |
| lib60870 与 libiec61850 共用 HAL 冲突 | 链接符号冲突 | 分进程隔离；编译选项 `WITHOUT_HAL/WITHOUT_COMMON` |
| 多协议写回一致性 | 出现"改了 A 协议、B 协议读旧值" | 单一 DataBank + 统一写回管线，写入必触发联动，纳入验收口径 |
| Worker IPC 成为瓶颈 | 高频突发上送丢帧 | 批量变更推送、只推 diff、IPC 用二进制帧；压测指标写进 P2 验收 |
| 内置 mini Broker 被误当生产组件 | 客户现场误用 | UI 明确标注"联调/故障注入专用"，默认关闭 |

---

## 10. 待孙总拍板的决策点

1. **许可证路径**：走 A（采购 MZ Automation 商业授权，架构最简单）还是 B（GPL + Worker 进程隔离）？或本项目仅内部使用、暂不考虑分发？—— 这一条决定 104 / 61850 的整个工程结构，建议优先定。
2. **产品定位**：自动化测试替身 / 客户演示虚拟站点 / 两者兼顾？权重如何分？
3. **Qt 基线**：单基线 Qt 6.2+（推荐），还是与 FieldLink 保持一致用 5.15.2 MinGW？
4. **MQTT 角色**：外挂 Broker 为主 + 内置 mini Broker 仅做故障注入（推荐），还是必须"开箱自带完整 Broker"？
5. **Modbus 后端**：MVP 只用 Qt SerialBus（推荐），还是一开始就要 libmodbus 做底层报文控制？
6. **仓库命名与共享核心**：新仓库叫什么？`sim-core` 先内置还是直接抽独立仓库 / submodule？
7. **104 范围**：P2 是否必须支持总召唤之外的时钟同步、SBO、冗余组？有无指定的主站/规约测试仪需要做兼容性验收？

---

> 下一步（确认本方案后）：产出 `README.md`、配置包 JSON Schema、`IProtocolAdapter` 接口定义、P0 任务拆解与验收用例清单 —— 仍然不写实现代码。
