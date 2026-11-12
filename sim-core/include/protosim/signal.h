// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "protosim/protocolkind.h"
#include "protosim/quality.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace protosim {

/// 信号数据类型，与 config/schema 的 signal.dataType 一一对应。
enum class DataType {
    Bool,
    Uint16,
    Int16,
    Uint32,
    Int32,
    Float32,
    Float64,
    Ascii,
};

/// 四字节序。语义以「寄存器里实际摆放的字节顺序」为准：
///   ABCD = 规范大端           0x12345678 -> 12 34 56 78
///   BADC = 字节内互换         0x12345678 -> 34 12 78 56
///   CDAB = 字内字间互换       0x12345678 -> 56 78 12 34
///   DCBA = 完整小端           0x12345678 -> 78 56 34 12
enum class ByteOrder {
    ABCD,
    BADC,
    CDAB,
    DCBA,
};

int dataTypeByteCount(DataType type);
int dataTypeRegisterCount(DataType type);
bool dataTypeIsFloat(DataType type);
bool dataTypeIsSigned(DataType type);

QString dataTypeToString(DataType type);
bool dataTypeFromString(const QString &text, DataType *out);

QString byteOrderToString(ByteOrder order);
bool byteOrderFromString(const QString &text, ByteOrder *out);

/// 工程值上下限。valid=false 表示配置里没给 range，此时不做钳位。
struct SignalRange
{
    double min = 0.0;
    double max = 0.0;
    bool valid = false;

    bool contains(double value) const { return !valid || (value >= min && value <= max); }
};

/// 一个信号可以同时挂多个协议绑定 —— 「一个实时库，多协议同时对外」的落点。
struct ProtocolBinding
{
    ProtocolKind protocol = ProtocolKind::Modbus;
    int unitId = 1;
    QString registerType; // Coils / DiscreteInputs / HoldingRegisters / InputRegisters
    int address = 0;
    int count = 1;
    QJsonObject raw;      // 原始 JSON，协议专属字段原样透传（61850 走这里）
};

/// 点表里的一个信号。工程值 = 原始值 * scale + offset。
struct Signal
{
    QString id;
    QString name;
    QString deviceId;
    QString description;
    QString category; // AI / DI / AO / DO / ... 自由文本，用于 UI 分组
    QString unit;     // °C / kPa / ""，仅用于展示
    DataType dataType = DataType::Uint16;
    ByteOrder byteOrder = ByteOrder::ABCD;
    double scale = 1.0;
    double offset = 0.0;
    QVector<ProtocolBinding> bindings;
    SignalRange range;
    Quality initialQuality = Quality::Good;
    QJsonObject behavior;       // 数据源行为，由仿真引擎解释（P0-4）
    QJsonObject reserved61850;  // P0 不解释，仅为 P3 预留

    bool isValid() const { return !id.isEmpty() && !name.isEmpty(); }

    /// 该信号在寄存器表里占几个 16 位寄存器。
    int registerCount() const;

    double rawToEngineering(double raw) const { return raw * scale + offset; }
    double engineeringToRaw(double engineering) const;

    /// 钳位到 range。越界时 outOfRange=true，调用方据此把质量置 OutOfRange（AC-P0-03）。
    double clampToRange(double engineering, bool *outOfRange) const;
};

} // namespace protosim
