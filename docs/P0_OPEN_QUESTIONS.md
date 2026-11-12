# P0 落地中的冲突、偏离与待拍板事项

> 状态：P0-1 ~ P0-8 代码已落地，`ctest` 9 项全绿、`--selftest` 9 项全通过、绿色 zip 已产出。
> 本文记录**实现过程中遇到的规格冲突/歧义**，以及我按什么假设先做了。
> 标注 🔴 的需要你拍板；🟡 的是我已按最合理方式处理、想改随时说；⚪ 的是纯环境事实。

---

## 1. 需要你拍板的（会影响验收口径）

### 🔴 1.1 AC-P0-10「FC16 写 HR[0] = 23.5」怎么解

预设里 HR[0..1] 是 **float32 温度**（占 2 个寄存器），但验收写的是"写 HR[0] = 23.5"。

两种互斥的语义：

| 语义 | 主站发什么 | 回读看到 | 我的实现 |
| --- | --- | --- | --- |
| **原始寄存器**（协议真实） | 2 个寄存器放 23.5f 的 IEEE 位型 `41BC 0000` | FC03 读到 `41BC 0000`，工程值 23.5 | ✅ 已实现 |
| **工程值**（调试友好） | 1 个寄存器放 235（scale=0.1） | 同上 | ❌ 未实现 |

我选了**原始寄存器**，理由：FieldLink 自己有点表 + scale/offset，写入时会把工程值换成原始值，模拟器做协议层就该按原始值走。
**代价**：L2 用例里必须写 2 个寄存器（测试已这么写），"写 HR[0]"这句话严格讲是"写 HR[0..1]"。

如果你的本意是"主站能直接写 23.5 这种工程值"，告诉我，我在适配器里加一层工程值入口（只影响 UI/调试写入，不影响协议路径）。

### 🔴 1.2 AC-P0-08「8 个功能码全部得到合法响应」在预设上不成立

预设只有 `HoldingRegisters` 三个绑定，所以对预设发 **FC01/FC02/FC04** 一定返回异常码 **0x02**（地址未映射）——这是**合法响应**，但不是"成功响应"。

我的处理：E2E 用一份更全的测试夹具（`tests/e2e/tst_modbus_e2e.cpp` 内联 JSON，四个寄存器区都有点），8 个功能码全部拿到正常响应；对预设只验 FC03/FC06/FC16。

**要你选**：
- A（现状）：预设保持最小，测试夹具另算；
- B：把 `fieldlink_preset.json` 扩成四区都有点的"演示场景"（FieldLink 那边看到的点会变多）。

### 🔴 1.3 地址重叠校验比你的计划更严

`P0_PLAN` 只写了"地址不重叠"是应用层语义约束，我实现成**硬拒**：同一 `(unitId, registerType)` 下两个信号压同一寄存器 → 载入失败。

副作用：**"故意让两个信号指同一寄存器"这种测试场景现在造不出来**。对模拟器来说这可能是刚需（比如验证主站读到别名点是否一致）。

- A（现状）：硬拒，保证点表干净；
- B：降级为警告 + UI 标黄，允许别名。

### 🔴 1.4 写入越界：钳位还是拒绝

`WriteOutcome` 里有 `RejectedOutOfRange`，说明你预留了"拒绝"的意图。我实现的是**钳位 + 质量置 `OutOfRange`**（与仿真侧 AC-P0-03 一致），`RejectedOutOfRange` 目前没被用到。

Modbus 侧没有专门的"值越界"异常码，最接近的是 0x03。要不要改成拒绝并回 0x03？

### 🔴 1.5 两套时间基准并存

- **DataBank 时标** = 仿真虚拟时钟（从 0 起），点表显示 `T+1.400s`；
- **报文日志时标** = 墙上时间（`17:20:07.560`），因为要跟 FieldLink / Wireshark 对时间。

好处是各自都合理，坏处是同一屏里两种时间。要不要统一成一套（例如报文日志也显示 `T+`，或点表额外显示墙上时间）？

---

## 2. 环境相关偏离（⚪ 事实 + 我的绕法）

| # | 计划要求 | 本机实际 | 我怎么办 |
| --- | --- | --- | --- |
| 2.1 | `-G Ninja` | 全盘没有 `ninja.exe` | 本机用 `-G "Visual Studio 17 2022" -A x64`；CI 里仍用 Ninja。`CMakeLists.txt` 本身与生成器无关 |
| 2.2 | `cmake --fresh` | CMake 3.23.5 没有该参数（3.24 才有） | 用「删 build 目录」代替 |
| 2.3 | MSVC 2022 x64 | VS2022 装在 `D:\programming\Microsoft Visual Studio\2022\Community`（不在默认路径） | `vswhere` 能发现，CMake 自动识别；工具集 14.44 |
| 2.4 | — | **MSBuild 会崩**：环境里 `HTTPS_PROXY` 与 `https_proxy` 同时存在，Windows 上不区分大小写 → `System.ArgumentException: 已添加项` | 构建时清掉小写变量：`env -u https_proxy -u http_proxy -u all_proxy cmake --build ...`。**这条已写进 README，你自己命令行构建也会踩** |

---

## 3. 我已做的实现决策（🟡 想改随时说）

### 3.1 Modbus 服务端 = `QModbusTcpServer` + 一层薄帧观察层

你当时说"在 QModbusTcpServer 基础上补充，自研太麻烦"，我按这个做的：

```
主站 → 对外端口 1502（ModbusFrameTap，管连接上限 + 抓原始字节）
        → 内部回环端口 2502（QModbusTcpServer，协议编解码全交给 Qt）
```

**为什么非要这一层**：Qt 不暴露原始 ADU，也不告诉调用方"这条请求来自哪个客户端"。而 AC-P0-10 要求审计里有客户端、AC-P0-14 要求每帧有真实 hex（含事务号）。这一层只做透传 + 记账，不做协议解析。

**副作用 / 已知限制**：
- 内部端口固定占 `对外端口 + 1000`（1502 → 2502），被占用时自动往后找 20 个；`netstat` 会看到两条监听；
- **多客户端并发写时，客户端归属按帧到达顺序匹配**（FIFO）。单客户端（FieldLink 场景）完全准确；8 个主站同时写极端场景可能张冠李戴。要绝对精确只有两条路：自研 Modbus codec，或改 IPC 传连接上下文——都留到 P1 评估。

### 3.2 全仓启用 `QT_NO_KEYWORDS`

本项目的核心名词就是「信号」，而 `signals` 是 Qt 宏，导致 `QVector<Signal> signals;` 直接编译失败。我没有把成员改名（`signalDefs` 之类很别扭），而是关掉 Qt 关键字宏，改写成 `Q_SIGNALS` / `Q_SLOTS` / `Q_EMIT`。语义更明确，代价是与常见 Qt 示例代码写法不同。

### 3.3 正弦中心值从 `behavior.min/max` 推导

`schema` 的 `behavior` 里**没有 center 字段**（只有 amplitude / periodMs / min / max / phaseDeg）。预设是 `amplitude=10, min=15, max=35`，要求输出落在 15~35，所以中心 = (15+35)/2 = 25。

⚠️ 注意 `signal.range` 是**工程量程**（-40~120），不是行为范围。第一版我错用了 range 中点，正弦跑到 30~50，被测试抓出来了。建议 P1 给 schema 补 `center` 字段，避免继续推导。

### 3.4 随机游走语义

- 起点 = `behavior.value` → `behavior.min/max` 中点 → `signal.range` 中点；
- 每 `intervalMs` 走一步，步长均匀分布 `[-step, +step]`；
- 越界直接钳到 `[min, max]`（不是反弹）；
- 随机数用 `std::mt19937_64`，种子 = `场景种子 XOR FNV1a(signal.id)` —— 保证**增删一个信号不会打乱其他信号的序列**。

### 3.5 `Signal` 比计划多了一个 `unit` 字段

计划里 P0-2 列的是 `signal.h/.cpp`、`databank.h/.cpp`、`quality.h`，没提 `unit`；但 schema 和预设里都有 `"unit": "°C"`。我加了这个字段（纯展示用）。

### 3.6 `--selftest` 的 9 项内容是我定义的

计划只说"9 项，照搬 Python 从站思路"，没列清单。我定的是：构建信息 / 配置加载 / 坏配置拒绝 / 点表装载 / 四种字节序 / 工程值换算与钳位 / 仿真确定性 / FC16→FC03 回读 / 跨协议联动。

### 3.7 额外加的两个命令行参数

- `--theme dark|light`：切主题（AC-P0-15 要验深浅不串色，手动切太慢）；
- `--screenshot <path>`：启动后自截一张图再退出。加它是因为本机安全策略禁止 `Add-Type`，外部截图工具用不了；顺带给交付留证用。

### 3.8 Modbus 只支持单 unitId

`endpoint.unitIds` 目前只取第一个（`serverAddress = unitIds[0]`）。多 unit / 多设备是 P1 明确推迟项，与计划一致。

### 3.9 故障注入未实现

`injectFault()` 返回 `Unsupported`。计划里故障注入是 P1；schema 里的 `faults[]` 已能原样加载与校验，但 P0 不执行。

---

## 4. 计划里没写、但 P0 必须有的东西

| 项 | 我的处理 | 备注 |
| --- | --- | --- |
| `LICENSE` 全文 | 已补 | 从 gnu.org 取 GPL-3.0 正文（674 行），打包时一并带入 |
| `tools/requirements.txt` | 新增 | `validate_config.py` 需要 `jsonschema`（README §3.4 已列为开发期依赖） |
| `tools/package.py` | 新增 | 用 Python 而非 PowerShell：避免中文注释在 `.ps1` 里的编码坑 |
| `.editorconfig` | 新增 | 计划 P0-1 要求 |

---

## 5. 验收项当前状态

| 编号 | 状态 | 证据 |
| --- | --- | --- |
| AC-P0-01 构建 | ✅ | 0 error；`protosim_core` 只链 `Qt6::Core` |
| AC-P0-02 字节序 | ✅ | `tst_byteorder`（含四种序寄存器摆放 + 往返） |
| AC-P0-03 工程值换算 | ✅ | `tst_signal`（500→10.0、越界钳位 + OutOfRange） |
| AC-P0-04 配置包校验 | ✅ | `tst_configbundle` + `tools/validate_config.py` |
| AC-P0-05 配置包健壮性 | ✅ | 缺 schemaVersion 拒 / 路径穿越拒 / 覆盖生成 `.bak` |
| AC-P0-06 确定性 | ✅ | `tst_simulation`（1000 tick 逐帧一致、变速/暂停/单步不改序列） |
| AC-P0-07 Modbus 读 | ✅ | `tst_simulation` + `tst_modbus_e2e` |
| AC-P0-08 功能码覆盖 | ✅（口径见 1.2） | `tst_modbus_e2e::allEightFunctionCodesRespond` |
| AC-P0-09 异常码 | ✅ | 0x01 / 0x02 / 0x03 三条用例 |
| AC-P0-10 写回一致性 | ✅（口径见 1.1） | `writeFloatViaFc16AndReadBack` + 审计断言 |
| AC-P0-11 多协议联动 | ✅ | `modbusWriteNotifiesOtherProtocol` |
| AC-P0-12 质量码 | ✅ | `tst_databank`（置 Bad / OutOfRange 不静默） |
| AC-P0-13 连接管理 | ✅ | 9 客户端用例，第 9 个被拒且前 8 个仍可用 |
| AC-P0-14 报文日志 | ✅ | 时间/方向/客户端/hex/解析/耗时 + 按信号过滤 + 文本导出 |
| AC-P0-15 UI | ✅ | 点表实时刷新、设备树、端点状态、hex 页签、QML 曲线；深浅主题各截一张图 |
| AC-P0-16 `--selftest` | ✅ | 9/9 通过，退出码 0（绿色包内同样通过） |
| AC-P0-17 **L3 FieldLink 互验** | ⏳ **需要你手工** | 我这边没有 FieldLink，只能保证 1502 起来且协议符合规范 |
| AC-P0-18 绿色包 | 🟡 部分 | zip 已产出（29 MB），在**本机**清空 Qt 的 PATH 后自检通过；"干净 Windows"只能在另一台机器验 |
| AC-P0-19 Linux 守卫 | ⏳ 未跑 | CI 写了，但没推仓库没跑过（你也说了不提交代码） |

---

## 6. 建议的下一步（按优先级）

1. **先跑 AC-P0-17**：启动模拟器，FieldLink 连 1502，读 HR[0..1] 看温度是否 15~35 周期变化，再 FC16 写、FC03 回读。这是 P0 的真正验收线。
2. 拍板 §1 的 5 条（尤其 1.1 和 1.2），我按你的口径调实现/用例。
3. 补 `LICENSE` 正文。
4. 决定要不要现在提交代码（你说先不提交，所以我一个 commit 都没做，全部改动都在工作区）。
