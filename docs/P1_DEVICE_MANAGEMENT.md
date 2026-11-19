# P1 第一阶段: 虚拟设备 / 端点管理

> 状态: 实施中  
> 基线: `main` 分支 P0 归档状态  
> 分支: `feat/p1-device-management`

## 1. 目标

把 P0 里“写死在 `fieldlink_preset.json` 里的一个 Modbus 设备 + 一个 1502 端口”，
升级为可在界面里管理的虚拟设备: 

- 新增一个虚拟设备(含监听地址、端口、unitId)；
- 删除一个虚拟设备；
- 启用 / 停用虚拟设备；
- 修改虚拟设备的监听地址和端口；
- 改动写回配置 JSON，并尽量在当前进程内热生效；
- 不破坏 P0 的 FieldLink 默认场景。

## 2. 非目标

本阶段**不做**: 

- Modbus RTU；
- IEC 104 / MQTT / 61850 端点；
- 点位编辑器(新增 / 删除 / 改寄存器地址)；
- 写语义(原始寄存器 vs 工程值)的最终拍板；
- 地址重叠策略调整(P0 仍按 unitId + 区 + 地址硬拒)；
- 故障注入执行。

这些仍然在 `docs/P0_OPEN_QUESTIONS.md` 中排队。

## 3. 配置模型扩展

### 3.1 `devices[]`

新增两个可选字段，保持向后兼容: 

```json
{
  "id": "dev-02",
  "name": "备用 PLC",
  "type": "PLC",
  "description": "P1 新增",
  "endpointId": "ep-dev-02",
  "enabled": true
}
```

- `endpointId`: 指向 `endpoints[].id`；缺省时按顺序回落到第一个同协议端点。
- `enabled`: 缺省 `true`。

### 3.2 `endpoints[]`

Modbus TCP 端点新增两个可选字段: 

```json
{
  "id": "ep-dev-02",
  "protocol": "modbus",
  "transport": "tcp",
  "bindAddress": "127.0.0.1",
  "port": 1503,
  "unitIds": [2],
  "maxConnections": 8,
  "enabled": true
}
```

- `id`: 缺省时由构建层生成 `modbus-tcp-1`、`modbus-tcp-2` …
- `enabled`: 缺省 `true`。

## 4. 操作语义

| 操作 | 行为 |
|---|---|
| 新增设备 | 同时创建一个 Modbus TCP 端点(独立端口)和一个默认保持寄存器点位 |
| 删除设备 | 删除设备、该设备所有点位、该设备专属端点；禁止删除最后一个设备 |
| 启用 / 停用 | 同步切换 `device.enabled` 与对应 `endpoint.enabled` |
| 改端口 / 地址 | 更新端点原始 JSON；应用层校验端口冲突 |
| 端口冲突 | 同一 `bindAddress:port` 且两端点均启用时拒绝 |
| unitId | 新增设备默认挑一个未占用的 unitId(1~247) |
| 保存 | 复用 `ConfigBundle::saveToFile()`，保留 `.bak` 备份纪律 |

## 5. 架构落点

- **sim-core**: 只加配置模型、校验、纯逻辑 `DeviceManager`，不碰协议库、不碰 GUI。
- **adapters**: 给 `ModbusTcpAdapter::bindingsFromBundle()` 增加按端点过滤的重载。
- **app**: 把单个 adapter 改成按启用端点装配多个 adapter；提供 `applyBundle()` 回调给 UI。
- **ui**: 新增 `DeviceManagerDialog`，从命令栏“设备”按钮进入。
- **测试**: 新增 `tst_devicemanager.cpp`，覆盖新增、删除、启停、端口冲突、JSON 往返。

## 6. 验收标准

1. 默认配置加载后，`dev-01` 自动获得端点 id，现有 FieldLink 1502 链路不变。
2. 新增设备 `dev-02` 后，配置里出现端口 1503、unitId 2、一个默认点位。
3. 端口冲突时新增 / 改端口失败，且不修改原配置。
4. 删除设备后，其点位和端点一并消失；最后一个设备不可删除。
5. 停用设备后，对应端点不再监听。
6. 配置写盘后重新加载，设备状态和端口保持一致。
7. `ctest` 全绿，`--selftest` 9 项全绿，MSVC `/WX` 构建通过。

## 7. 实施顺序

1. 先写 `tst_devicemanager.cpp`，确认 RED；
2. 实现 `DeviceManager` 与 schema / validator / serializer 扩展，转 GREEN；
3. 实现适配器按端点过滤绑定；
4. 实现 app 多端点装配与 `applyBundle()` 回调；
5. 实现 `DeviceManagerDialog`；
6. 跑 `tools\check.bat`，同步文档。
