# `IProtocolAdapter` 接口契约（P0 前置规格）

> 状态：**接口规格，不是实现代码**。本文定义的是"契约"，P0 落地时才拆成 `include/protosim/iprotocoladapter.h`。
> 依据：`SERVER_SIMULATOR_PROPOSAL.md` §3.4、`TECH_STACK_DECISIONS.md` v0.5 §2.6 / §6。
> 技术基线：Qt 6.6.3 / C++17 / MSVC 2022 x64 / 全仓 GPL-3.0-or-later。

---

## 1. 这个接口要解决什么

它是**整个项目唯一能平滑接入 IEC 61850 的关键**。四层架构里，`sim-core`（纯 Qt/C++，不链接任何协议库）只认这个接口；进程内适配器（Modbus / MQTT）与独立 Worker 版适配器（104 / 61850）实现同一接口，UI 与 core 感知不到差异。

设计约束（不可让步）：

1. **`sim-core` 不链接任何协议库** —— 接口只依赖 `QtCore`（QString / QVector / QJsonObject / QCborValue / QDateTime）。
2. **一个实时库，多协议同时对外** —— 任一适配器写入都必须走统一写回管线，禁止直接改 DataBank。
3. **能力声明驱动 UI** —— UI 不硬编码"104 支持什么"，通过 `capabilities()` 动态渲染故障注入面板与绑定编辑器。
4. **进程边界对调用方透明** —— Worker 版只是 IPC 代理，接口签名一致。
5. **不抛异常跨线程/跨进程** —— 所有可能失败的操作返回 `Result<T>`；C++ 异常禁止越过 Worker 边界。
6. **为 61850 预留但不塌缩** —— 61850 专属字段走 `Binding::extension`（QJsonObject / CBOR），不污染公共结构、不改接口签名。

---

## 2. 公共数据类型（契约示意）

```cpp
namespace protosim {

enum class ProtocolKind { Modbus, Iec104, Mqtt, Iec61850 };
enum class Quality      { Good, Bad, Timeout, OutOfRange, Unknown };
enum class AdapterState { Created, Bound, Running, Stopped, Fault };
enum class WriteOutcome { Accepted, RejectedWriteProtected, RejectedInterlock,
                          RejectedOutOfRange, RejectedUnknownSignal };

// 统一错误模型：不抛异常，携带可展示/可记录的错误码与文本
struct Error { int code; QString message; QString detail; };
template <typename T> class Result {           // 简化示意
public:
    bool ok() const;
    const T& value() const;
    const Error& error() const;
};

// 能力声明：UI 据此动态渲染，新增协议不需要改 UI 代码
struct Capabilities {
    ProtocolKind kind;
    bool supportsRead;
    bool supportsWrite;
    bool supportsPush;            // 主动上送 / 突发 / publish
    bool supportsFaultInjection;
    bool supportsMultiClient;
    QVector<int> readFunctionCodes;   // Modbus: 1/2/3/4；104: 监视方向 ASDU 类型
    QVector<int> writeFunctionCodes;  // Modbus: 5/6/15/16；104: 控制方向 ASDU
    QVector<QString> faultTypes;      // 与 sim-config.schema.json 的 fault.type 对齐
    int maxBindings;
};

// Signal 快照：DataBank 对外暴露的唯一读视图，带质量码与时标
struct SignalSnapshot {
    QString signalId;
    QVariant value;
    Quality quality;
    qint64 timestampMs;        // 统一时标源，来自 100ms tick / 确定性种子时钟
    bool forced;               // 手工强制标记（必须带审计）
};

struct AddressRef {            // 协议地址，具体含义由 adapter 解释
    QString kind;              // "holdingRegister" / "ioa" / "topic" / "iedPath"
    QJsonObject fields;        // 与配置包 bindings[] 对应
};

struct Binding {
    QString signalId;
    ProtocolKind protocol;
    AddressRef address;
    QJsonObject extension;     // 61850 等后期协议的专属字段，接口不解释
};

struct ReadRequest  { QString clientId; QVector<AddressRef> addresses; QJsonObject context; };
struct WriteRequest { QString clientId; QVector<QPair<AddressRef, QVariant>> values;
                      QJsonObject context; };
struct ReadResult   { Result<QVector<SignalSnapshot>> data; int protocolErrorCode; };
struct WriteResult  { QVector<QPair<QString, WriteOutcome>> outcomes; int protocolErrorCode; };

struct SignalChange { QString signalId; QVariant oldValue, newValue;
                      Quality quality; qint64 timestampMs; QString sourceProtocol; };
struct FaultSpec {
    QString id;
    QString target;            // 协议名 / 端点名 / signal.id
    QString type;              // 与 schema 的 fault.type 枚举一致
    QJsonObject params;
    bool enabled;
    int durationMs;
};
struct AdapterStats {
    ProtocolKind kind;
    int activeClients;
    quint64 framesRx, framesTx, errorFrames;
    qint64 uptimeMs;
    QJsonObject extra;
};
struct EndpointConfig { ProtocolKind kind; QJsonObject raw; };  // 来自配置包 endpoints[]

} // namespace protosim
```

> `protocolErrorCode` 是**协议层**错误码（如 Modbus 异常码 0x01~0x04、104 的 COT），与 `Error::code`（工程层错误）分开，报文监视要能原样展示。

---

## 3. 接口本体

```cpp
class IProtocolAdapter {
public:
    virtual ~IProtocolAdapter() = default;

    virtual Capabilities capabilities() const = 0;

    // 生命周期：Created -> Bound -> Running -> Stopped / Fault
    virtual Result<void> start(const EndpointConfig& cfg) = 0;
    virtual Result<void> stop() = 0;                       // 幂等，优雅断链
    virtual Result<void> bind(const QVector<Binding>& bindings) = 0;  // 可热更新
    virtual AdapterState state() const = 0;

    // 读：必须从 DataBank 取快照（含质量码），不得自行缓存旧值
    virtual ReadResult onRead(const ReadRequest& req) = 0;

    // 写：禁止直接改 DataBank —— 提交 WriteIntent 给统一写回管线
    virtual WriteResult onWrite(const WriteRequest& req) = 0;

    // 实时库变化 -> 主动上送 / 突发 / publish；只推 diff，支持批量
    virtual void pushTelemetry(const QVector<SignalChange>& changes) = 0;

    virtual Result<void> injectFault(const FaultSpec& spec) = 0;
    virtual AdapterStats stats() const = 0;
};
```

**实现者义务**：

- `start()` 失败必须回到 `Created`/`Fault`，不得半开端口；`stop()` 必须释放监听并通知所有客户端。
- `bind()` 的绑定与配置包 `bindings[]` 一一对应；`extension` 原样透传，接口层不解释。
- `onRead()` 只做"协议地址 → signalId → DataBank 快照 → 协议编码"，不做业务计算。
- `pushTelemetry()` 允许丢帧降级（背压策略由适配器声明在 `Capabilities`），但不得阻塞 100ms tick。

---

## 4. 统一写回管线（最容易出错的一条）

```
任意协议 onWrite()
   └─> WritePipeline::submit(WriteIntent{ signalId, value, sourceProtocol, clientId, ts })
         ├─ 写保护 / 联锁检查  -> 拒绝则原路返回协议错误码
         ├─ 审计记录（谁、何时、从哪个协议、旧值/新值）
         ├─ DataBank 原子更新（唯一真相源）
         └─ EventBus 广播 SignalChange
               └─> 所有已注册适配器 pushTelemetry()（含来源协议自身之外的其他协议）
```

**验收口径**：从 104 写 HR[0] 之后，Modbus FC03 立刻读到新值；从 Modbus FC16 写之后，MQTT 立即 publish 新值。这条要写进 P0/P2 的自动化用例，防止"改了 A 读 B 是旧值"。

---

## 5. Worker 版适配器（104 / 61850）

- **接口不变**：`WorkerAdapterProxy : IProtocolAdapter` 把每个调用序列化成 IPC 帧发给独立进程，主程序零感知。
- **传输**：`QLocalSocket`（Windows 上命名管道，设 `QLocalServer::UserAccessOption`）+ **4 字节小端长度前缀** + Qt6 `QCborValue` 帧（零第三方依赖，不引 protobuf）。
- **帧字段**：`methodId`（capabilities/start/stop/bind/onRead/onWrite/push/injectFault/stats）、`requestId`、`payload`、`error`。
- **崩溃隔离**：Worker 进程退出时 proxy 置 `Fault` 并上报 EventBus，UI 可单独重启该 Worker；主进程不崩。
- **背压**：批量变更、只推 diff；压测指标写进 P2 验收。

---

## 6. `capabilities()` 如何驱动 UI

| UI 元素 | 数据来源 |
| --- | --- |
| 绑定编辑器显示的协议专属字段 | `Capabilities::kind` + 配置包 Schema 的对应 binding 分支 |
| 故障注入面板的可勾选项 | `Capabilities::faultTypes`（与 schema 枚举同源） |
| "支持主动上送 / 支持多主站" 标记 | `supportsPush` / `supportsMultiClient` |
| 协议自检页展示的功能码列表 | `readFunctionCodes` / `writeFunctionCodes` |

新增协议 = 新增一个实现类 + 一份 binding Schema 分支，**UI 不改代码**。

---

## 7. 故障注入契约

- `injectFault()` 只描述"注入什么"，不负责计时；`durationMs` 由 `FaultInjector`（core）统一调度，保证与确定性时钟一致，可复现。
- 故障可按协议、端点、信号三层叠加；`params` 是自由 JSON，由适配器自行解释并做白名单校验（防注入非法参数导致崩溃）。
- 链路层故障（拒绝连接、半开连接、延迟抖动）在适配器内实现；数据层故障（质量置坏、值冻结、时标回退）在 core 的 `FaultInjector` 实现，两者不得重复。

---

## 8. 时标与质量码

- 所有协议共享**同一时标源**：`meta.tickMs`（默认 100ms）+ `meta.timeSource=simulation` 的确定性虚拟时钟。
- 输出时统一映射：104 → CP56Time2a / CP24Time2a，61850 → 标准时标，MQTT → ISO8601；**映射只发生在适配器出口**，core 内部一律 `qint64 ms`。
- 质量码 `Good/Bad/Timeout/OutOfRange/Unknown` 随快照一起传递；质量非 Good 时的协议行为由各适配器声明（例如 104 上送带"无效"品质描述词），不得静默当正常值。

---

## 9. 61850 预留为什么不会破坏接口

配置包 Schema 里已存在 `bindingIec61850` 与 `signal.reserved61850`（`iedPath / fc / dataSet / reportControlBlock / mmsType`）。它们通过 `Binding::extension` 进入适配器，公共结构、`IProtocolAdapter` 签名、IPC 帧格式三者都不需要改动。P3 的工作集中在：SCL/ICD 解析 → 映射、报告控制块、数据集与控制模型。

---

## 10. 接口层验收口径（P0 就要能验）

1. `sim-core` 的链接线里**没有任何协议库**（用 `cmake --graphviz` / 链接命令自检）。
2. 写一个 `FakeAdapter : IProtocolAdapter`，不改 core 任何代码即可注册、收发、注入故障。
3. `capabilities()` 变化时 UI 面板随之变化（至少用 Qt Test 验证模型层）。
4. 写回管线联动：任一来源写入后，其余适配器都收到 `pushTelemetry` 且读到新值。
5. Worker proxy 与进程内适配器跑同一套接口测试（同一份用例、两种实现）。
