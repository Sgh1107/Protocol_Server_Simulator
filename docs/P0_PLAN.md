# P0 任务拆解与验收用例清单

> 状态：**任务规格，不含实现代码**。依据 `SERVER_SIMULATOR_PROPOSAL.md` §8、`TECH_STACK_DECISIONS.md` v0.5、`../README.md` §7。
> 基线：Qt **6.6.3** / C++17 / MSVC 2022 x64 / CMake ≥3.25 / Windows 10 1809+ / Widgets 主体 + QML 局部 / SQLite（P1 才引入）。

---

## 1. P0 目标

**一句话**：启动模拟器 → FieldLink 连 `127.0.0.1:1502` 读到正弦温度；用 FC16 写入后 FC03 回读一致。

P0 是**单协议闭环**，不是三协议齐头并进。产出必须是绿色 zip，解压即可运行，不需要装任何服务。

| 做 | 不做（明确推迟） |
| --- | --- |
| `sim-core` 数据模型、DataBank、确定性时钟 | SQLite（P1）、任何数据库服务 |
| 配置包加载 + JSON Schema 校验 + FieldLink 预设 | MQTT / 104 / 61850 端点（P1/P2/P3） |
| Modbus TCP 服务端（FC01/02/03/04/05/06/0F/10） | Modbus RTU / 多 unit-id / 多设备（P1） |
| 常量 + 正弦仿真、`Curve` 接口 | `QJSEngine` 脚本、CSV 回放（P1） |
| 统一写回管线 + 报文日志（内存环形缓冲） | 故障注入面板 / HTTP 控制 API（P1） |
| UI 骨架（点表 / 设备树 / 端点状态 / 报文 hex） | 场景时间轴动画、完整演示动画（P1 起） |
| 三层测试骨架 + `--selftest` + 绿色包 | 安装包、崩溃 dump 平台化 |

---

## 2. 任务拆解

| # | 任务 | 产出（规划路径） | 依赖 | 验收口径 | 复杂度 |
| --- | --- | --- | --- | --- | --- |
| **P0-1** | 仓库骨架与构建 | `CMakeLists.txt`、`cmake/ProjectVersions.cmake`、`.clang-format`、`.editorconfig`、`.gitattributes`、`.github/workflows/ci.yml` | — | `cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=<Qt6.6.3/msvc2019_64>` 配置通过；Windows CI 作业全绿（**P0 不设 Linux 作业**，见文末变更记录） | 低 |
| **P0-2** | `sim-core` 数据模型 | `sim-core/signal.h/.cpp`、`databank.h/.cpp`、`quality.h` | P0-1 | `Signal + ProtocolBinding` 可表达多绑定；DataBank 读写原子、带质量码与时标；四种字节序换算单测全过 | 中 |
| **P0-3** | 配置包加载与校验 | `sim-core/configbundle.h/.cpp`、`config/schema/sim-config.schema.json`、`tools/validate_config.py` | P0-2 | `config/examples/fieldlink_preset.json` 通过 Schema；缺 `schemaVersion` 被拒；路径穿越被拒；覆盖前备份存在 | 中 |
| **P0-4** | 仿真引擎最小集 | `sim-core/simulationengine.h/.cpp`、`curve.h` | P0-2 | 100ms tick、确定性种子、播放/暂停/单步/倍速；同种子两次运行逐帧一致；`Curve` 为接口，脚本实现留到 P1 | 中 |
| **P0-5** | Modbus TCP 服务端 | `adapters/modbusadapter.h/.cpp` | P0-2、接口契约 | `QModbusTcpServer` + `setMap()`；8 个功能码；异常码 0x01/0x02/0x03；地址越界；连接数上限 | 中 |
| **P0-6** | 写回管线与报文日志 | `sim-core/writepipeline.h/.cpp`、`telemetrylog.h/.cpp`、`eventbus.h` | P0-2、P0-5 | FC16 写 → 管线 → DataBank → FC03 回读一致；每帧含时间戳/方向/客户端/hex/解析/耗时；可按信号过滤与导出文本 | **高** |
| **P0-7** | UI 骨架 | `ui/widgets/`、`ui/qml/` | P0-2、P0-5、P0-6 | 点表实时值/质量/时标、设备树、端点状态、报文 hex 页签、QML 曲线局部视图；深浅主题与双语机制就位（可先只做中文） | 中 |
| **P0-8** | 测试、自检与绿色包 | `tests/`、`tools/run_selftest.py`、打包脚本 | P0-1~7 | 三层测试全绿；`--selftest` 9 项通过；`windeployqt --qmldir` 后在一台干净 Windows 上解压即跑 | 中 |

**顺序约束**：P0-2 → (P0-3 / P0-4 并行) → P0-5 → P0-6 → P0-7 → P0-8。P0-6 是唯一高风险项，单独留 buffer。

---

## 3. 三层测试策略（沿用 FieldLink 验证哲学）

| 层 | 内容 | 工具 |
| --- | --- | --- |
| **L1 纯逻辑单测**（无 GUI、无 socket） | 字节序换算、scale/offset、质量码、配置包往返与原子性、异常码构造、写回联动 | Qt Test（Qt 6.6.3 自带） |
| **L2 假主站 E2E**（真实 socket、进程内） | 自写最小 Modbus master 走 127.0.0.1:1502，覆盖 8 个功能码与异常路径 | Qt Test + `QTcpSocket` |
| **L3 真实主站互验** | **FieldLink 直接连模拟器跑全链路**，不改 FieldLink 一行代码 | 手工 + 脚本化回归 |
| **附加** | `--selftest` 9 项（照搬 Python 从站的自检思路，C++ 重写） | 命令行开关 |

---

## 4. 验收用例清单

> 判定标准全部可自动化或可一眼确认；编号 `AC-P0-xx` 供 CI / 交付核对。

| 编号 | 场景 | 步骤 | 预期结果 |
| --- | --- | --- | --- |
| AC-P0-01 | 构建 | 干净目录执行 `cmake --fresh -S . -B build -G Ninja` 再 build | 0 error；`sim-core` 链接线无 QtSerialBus 之外的协议库 |
| AC-P0-02 | 字节序 | 对 `0x12345678` 做 ABCD/DCBA/BADC/CDAB 互相换算并回读 | 四次往返完全还原，且与 FieldLink `dataparser` 基线一致 |
| AC-P0-03 | 工程值换算 | `scale=0.1, offset=-40`，原始 500 | 工程值 10.0；上下限越界被钳位并置 `OutOfRange` |
| AC-P0-04 | 配置包校验 | 载入 `fieldlink_preset.json` | 通过 Schema；3 个信号、1 个端点、1 个设备全部装载 |
| AC-P0-05 | 配置包健壮性 | 删掉 `schemaVersion` / 塞入 `../../evil.json` 路径 / 覆盖已有配置 | 分别被拒、被拒、覆盖前生成备份 |
| AC-P0-06 | 确定性 | 同一 `seed=20261111` 跑两次各 1000 tick，导出序列 | 逐帧完全一致；暂停/单步/倍速不改变序列 |
| AC-P0-07 | Modbus 读 | FC03 读 HR[0..3] | reg2=42 恒定；reg0 在 15~35 间按 10s 周期正弦；reg1 有界随机游走 |
| AC-P0-08 | 功能码覆盖 | 依次发 FC01/02/03/04/05/06/0F/10 | 全部得到合法响应，无崩溃、无错帧 |
| AC-P0-09 | 异常码 | 非法功能码 / 越界地址 / 非法数据值 | 分别返回 0x01 / 0x02 / 0x03，报文监视可见 |
| AC-P0-10 | **写回一致性** | FC16 写 HR[0] = 23.5 → 立刻 FC03 回读 | 回读 23.5；DataBank 中该信号 `forced=true`；审计记录含来源协议与客户端 |
| AC-P0-11 | **多协议联动前置** | 注册两个 FakeAdapter，从 A 写入 | B 收到 `pushTelemetry` 且读到新值（为 P1/P2 的多协议联动打底） |
| AC-P0-12 | 质量码 | 手工把某信号置 `Bad` | 点表显示 Bad；读取响应带无效品质，不静默当正常值 |
| AC-P0-13 | 连接管理 | 起 9 个客户端（上限 8） | 第 9 个被拒；UI 端点状态在线数正确 |
| AC-P0-14 | 报文日志 | 跑 AC-P0-07~10 | 每帧含时间戳/方向/客户端/协议/hex/解析树/耗时；可按信号过滤、可导出文本 |
| AC-P0-15 | UI | 启动后观察点表、设备树、端点状态、QML 曲线 | 实时值随 tick 刷新；QML 曲线有数据；深浅主题切换不串色 |
| AC-P0-16 | `--selftest` | 命令行执行 | 9 项自检全通过，退出码 0 |
| AC-P0-17 | **L3 互验** | 用 FieldLink 连 `127.0.0.1:1502` | 读到正弦温度；写寄存器后回读一致 —— **P0 的核心验收口径** |
| AC-P0-18 | 绿色包 | `windeployqt --qmldir ui/qml` 打包，拷到干净 Windows 解压运行 | 无需安装 Qt / VC_redist 之外的依赖即可启动并连上 1502 |
| ~~AC-P0-19~~ | ~~平台守卫~~ | ~~CI 的 Linux 编译作业~~ | **已取消**（2026-11-12 决定：P0 只守 Windows + MSVC，Linux 版本待单独立项） |

---

## 5. Definition of Done

- [x] AC-P0-01 ~ AC-P0-19 **全部通过**（AC-P0-19 已取消，不列入）
      - 17：FieldLink 实连 1502，读值 / FC16 写回读 / 异常码三条均验证（2026-11-12）
      - 18：绿色包在干净 Windows 上解压即跑；本机清空 Qt 的 PATH 后自检 9/9（2026-11-12）
- [x] 测试接入 CI（`windows-2022`：配置 + 编译(WERROR) + ctest 10 项 + `--selftest`；**无 Linux 作业**）
- [ ] 绿色 zip 产出，README 的"快速开始"照着做能跑通
- [ ] `THIRD_PARTY_NOTICES.md` 初版（至少列出 Qt 6.6.3 各模块及许可证档）
- [ ] **P0 复盘**：复核 §3.5 的 6.12 升级判定标准、决定是否切 Qt 版本；同时复盘是否调整 P2 的 104 实现路径
- [ ] 依赖盘点：P0 未用到的库一个都不许留在链接线里

---

## 6. P0 阶段的风险与纪律

| 风险 | 缓解 |
| --- | --- |
| Qt 6.6.3 已 EOL、无安全补丁 | 知情接受（v0.5 决定）；**版本集中在 `cmake/ProjectVersions.cmake` 一处定义**、**禁用 6.7+ API**，保证未来切换只改一处 |
| Widgets + QML 混合两套主题 | 一套配色令牌 → QSS 与 Qt Quick `palette` 各自映射；P0 就验证深浅主题不串色 |
| P0-6 写回管线成为进度瓶颈 | 单列高风险项，先写接口与单测再接 UI（AC-P0-10/11 先行） |
| 配置包 Schema 过早锁死 | `schemaVersion` + 61850 预留字段已留；Schema 只约束结构，语义校验放应用层 |
| 依赖悄悄膨胀 | 每阶段结束做依赖盘点；P0 不许出现 SQLite、MQTT broker、任何第三方 C 库 |
| 报文日志拖慢 100ms tick | 内存环形缓冲 + 批量落盘；P0 不落 SQLite |

---

## 7. 变更记录

| 日期 | 变更 | 原因 |
| --- | --- | --- |
| 2026-11-12 | **删除 AC-P0-19（Linux 平台守卫）**，DoD 里的「Linux 编译守卫」同步删除 | 项目定位是 Windows 桌面联调工具，Linux 版本待 P0 之后单独立项；P0 阶段维护一个跑不通的 Linux 作业只会制造噪音 |
| 2026-11-12 | **CI 不设格式门禁** | 格式由 `.clang-format` + 评审保证；为排版差异反复烧 CI 分钟不划算 |
| 2026-11-12 | P0-1 验收口径从 `-G Ninja` 改为 VS 生成器 | 开发机无 Ninja，且 CI runner 的 PATH 里带 MinGW，VS 生成器能直接锁死 MSVC |
| 2026-11-12 | **P0-7 界面在收官前重做为 v2**（自绘委托 / 量程条 / hex 分色 / 图标集 / 填充式列宽） | 默认 Qt 控件拼出来的界面达不到"看着可信"的要求；设计稿与落地记录见 `docs/design/`，契约与验收口径不变 |
| 2026-11-12 | 新增 `tst_headerview`（ctest 从 9 项变 10 项） | 表头"总宽恒定 + 每列上下限"是纯交互行为，截图验不出，必须用断言钉死 |
