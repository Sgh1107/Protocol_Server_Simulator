# 第三方组件与许可证声明

> 版本：P0 初版。本项目整体以 **GPL-3.0-or-later** 分发（见 `README.md` §8）。
> 每次依赖变动必须同步更新本文件与 `cmake/ProjectVersions.cmake`。

## 1. 运行时分发的组件

| 组件 | 版本 | 许可证档 | 引入阶段 | 说明 |
| --- | --- | --- | --- | --- |
| Qt 6 Core | 6.6.3 | LGPL-3.0（按 LGPLv3 档使用） | P0 | 基础类型、JSON、文件、线程 |
| Qt 6 Gui | 6.6.3 | LGPL-3.0 | P0 | 字体、绘图、调色板 |
| Qt 6 Widgets | 6.6.3 | LGPL-3.0 | P0 | 点表 / 设备树 / 报文页签 |
| Qt 6 Network | 6.6.3 | LGPL-3.0 | P0 | Modbus TCP 传输、帧观察层 |
| Qt 6 Qml / Quick / QuickWidgets | 6.6.3 | LGPL-3.0 | P0 | 曲线局部视图 |
| Qt 6 SerialBus | 6.6.3 | LGPL-3.0（**仅此档**） | P0 | `QModbusTcpServer`。该模块另有 GPLv2 档，本项目不使用 |
| Qt 6 Sql | 6.6.3 | LGPL-3.0 | P1 | SQLite 冷数据、报文归档 |
| Qt 6 Test | 6.6.3 | LGPL-3.0 | P0（构建期） | 单元测试，不随运行包分发 |
| Qt 6 Svg / LinguistTools | 6.6.3 | LGPL-3.0 | P1 | 图标、中英双语 `.ts` |
| Qt 6 Charts | 6.6.3 | **GPL-3.0 / 商业** | 未链接（P1 视需要） | 若启用，与全仓 GPLv3 一致；当前曲线用 Qt Quick Canvas 自绘，未引入 |

**Qt 许可结论**：本项目以 LGPLv3 档使用 Qt，全仓为 GPL-3.0-or-later，二者兼容。分发绿色包时须提供：
1. 本声明与 Qt 的 LGPLv3 许可文本（`windeployqt` 会随包附带 `LICENSE.LGPL3` 等文件）；
2. 本项目完整对应源码（含构建脚本）；
3. 不修改 Qt 源码；如修改，须以同样方式开源修改部分并允许替换 Qt 库。

## 2. 编译器与系统组件（不随包分发，但用户需要）

| 组件 | 版本 | 说明 |
| --- | --- | --- |
| MSVC 2022 (v143) / 14.44 | 17.x | 编译期工具链；运行期需要 **Microsoft Visual C++ Redistributable (x64)** |
| Windows SDK | 10.0.26100 | 编译期 |

## 3. 开发 / 测试 / 打包工具（不进运行包）

| 组件 | 版本 | 许可证 | 用途 |
| --- | --- | --- | --- |
| CMake | ≥ 3.23 | BSD-3-Clause | 构建 |
| Ninja | 不锁 | Apache-2.0 | 生成器（CI 使用；本机可用 VS 生成器） |
| clang-format | 不锁 | Apache-2.0 with LLVM exceptions | 代码风格 |
| Python 3 + `jsonschema` | 不锁 | PSF / MIT | `tools/validate_config.py`、`tools/package.py` |
| GitHub Actions `windows-2022` / `ubuntu-22.04` | — | — | CI |

## 4. P0 明确**未**引入的依赖

以下按设计决策保留在外，任何一项被引入都需要在本文件登记并说明理由：

- SQLite（P1 起，随 Qt `QSQLITE` 插件，Public Domain）
- `lib60870-C`（P2，GPLv3，必须 ≥ 2.4.1，见 README §3.2 的 CVE 红线）
- `libiec61850`（P3，GPLv3）
- `mbedTLS`（P2 默认不启用）
- `qtmqtt` / `libmodbus` / `cpp-httplib` / 任何 MQTT broker
- 任何数据库服务（Redis / MariaDB / TDengine / Kafka …）

## 5. 版权声明

- Qt 版权归 The Qt Company Ltd. 及贡献者所有，按 LGPLv3 使用。
- `QModbusTcpServer` / `QModbusPdu` 等 Qt SerialBus 头文件中的版权声明随 Qt 分发保留。
- 本项目自身代码版权归各贡献者，以 GPL-3.0-or-later 授权。

> **本文件不是法律意见。** 对外商业分发、SaaS 托管或法务审查前请由法务复核。
