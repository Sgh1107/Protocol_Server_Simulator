// SPDX-License-Identifier: GPL-3.0-or-later
#include "protosim/signal.h"

#include <QtGlobal>

namespace protosim {
namespace {

struct DataTypeInfo
{
    const char *name;
    int byteCount;
    bool isFloat;
    bool isSigned;
};

// 下标与 DataType 的枚举顺序一一对应，改枚举顺序会在这里炸编译期断言之外的运行期错误，
// 因此额外用 static_assert 之外的单测兜住（tests/unit/tst_signal.cpp）。
constexpr DataTypeInfo kDataTypeInfo[] = {
    {"bool", 1, false, false},     // Bool
    {"uint16", 2, false, false},   // Uint16
    {"int16", 2, false, true},     // Int16
    {"uint32", 4, false, false},   // Uint32
    {"int32", 4, false, true},     // Int32
    {"float32", 4, true, true},    // Float32
    {"float64", 8, true, true},    // Float64
    {"ascii", 0, false, false},    // Ascii：长度由 count 决定，数值编解码不支持
};

static_assert(sizeof(kDataTypeInfo) / sizeof(kDataTypeInfo[0]) == 8,
              "DataType 新增枚举项时必须同步 kDataTypeInfo");

struct ByteOrderInfo
{
    const char *name;
    ByteOrder order;
};

constexpr ByteOrderInfo kByteOrderInfo[] = {
    {"ABCD", ByteOrder::ABCD},
    {"BADC", ByteOrder::BADC},
    {"CDAB", ByteOrder::CDAB},
    {"DCBA", ByteOrder::DCBA},
};

const DataTypeInfo &infoOf(DataType type)
{
    return kDataTypeInfo[static_cast<int>(type)];
}

} // namespace

int dataTypeByteCount(DataType type)
{
    return infoOf(type).byteCount;
}

int dataTypeRegisterCount(DataType type)
{
    const int bytes = infoOf(type).byteCount;
    return bytes <= 0 ? 0 : (bytes + 1) / 2;
}

bool dataTypeIsFloat(DataType type)
{
    return infoOf(type).isFloat;
}

bool dataTypeIsSigned(DataType type)
{
    return infoOf(type).isSigned;
}

QString dataTypeToString(DataType type)
{
    return QString::fromLatin1(infoOf(type).name);
}

bool dataTypeFromString(const QString &text, DataType *out)
{
    const int count = static_cast<int>(sizeof(kDataTypeInfo) / sizeof(kDataTypeInfo[0]));
    for (int i = 0; i < count; ++i) {
        if (text == QLatin1String(kDataTypeInfo[i].name)) {
            if (out)
                *out = static_cast<DataType>(i);
            return true;
        }
    }
    return false;
}

QString byteOrderToString(ByteOrder order)
{
    for (const ByteOrderInfo &info : kByteOrderInfo) {
        if (info.order == order)
            return QString::fromLatin1(info.name);
    }
    return QStringLiteral("ABCD");
}

bool byteOrderFromString(const QString &text, ByteOrder *out)
{
    for (const ByteOrderInfo &info : kByteOrderInfo) {
        if (text == QLatin1String(info.name)) {
            if (out)
                *out = info.order;
            return true;
        }
    }
    return false;
}

int Signal::registerCount() const
{
    if (dataType == DataType::Ascii) {
        // ascii 的寄存器数量由绑定上的 count 决定，信号本身不主张长度
        return 0;
    }
    const int declared = dataTypeRegisterCount(dataType);
    for (const ProtocolBinding &binding : bindings) {
        if (binding.count > declared)
            return binding.count;
    }
    return declared;
}

double Signal::engineeringToRaw(double engineering) const
{
    // scale 为 0 是配置错误，但这里不能崩；退化为不透缩，由应用层校验报错
    if (qFuzzyIsNull(scale))
        return engineering;
    return (engineering - offset) / scale;
}

double Signal::clampToRange(double engineering, bool *outOfRange) const
{
    bool out = false;
    double value = engineering;

    if (range.valid) {
        // 配置写反了（min > max）也按钳位处理，不静默放过
        const double lo = qMin(range.min, range.max);
        const double hi = qMax(range.min, range.max);
        if (value < lo) {
            value = lo;
            out = true;
        } else if (value > hi) {
            value = hi;
            out = true;
        }
    }

    if (outOfRange)
        *outOfRange = out;
    return value;
}

} // namespace protosim
