// SPDX-License-Identifier: GPL-3.0-or-later
#include "protosim/byteorder.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace protosim::byteorder {
namespace {

QByteArray bigEndianBytes(quint64 value, int byteCount)
{
    QByteArray out(byteCount, Qt::Uninitialized);
    for (int i = 0; i < byteCount; ++i) {
        out[i] = static_cast<char>((value >> (8 * (byteCount - 1 - i))) & 0xFF);
    }
    return out;
}

quint64 valueOfBigEndian(const QByteArray &bytes)
{
    quint64 value = 0;
    for (char byte : bytes) {
        value = (value << 8) | static_cast<quint8>(byte);
    }
    return value;
}

int signExtendShift(DataType type)
{
    return dataTypeByteCount(type) * 8;
}

} // namespace

QByteArray permute(const QByteArray &bigEndian, ByteOrder order)
{
    QByteArray out = bigEndian;
    const int n = out.size();

    switch (order) {
    case ByteOrder::ABCD:
        break;
    case ByteOrder::DCBA:
        std::reverse(out.begin(), out.end());
        break;
    case ByteOrder::BADC:
        for (int i = 0; i + 1 < n; i += 2)
            std::swap(out[i], out[i + 1]);
        break;
    case ByteOrder::CDAB:
        for (int i = 0; i + 3 < n; i += 4) {
            std::swap(out[i], out[i + 2]);
            std::swap(out[i + 1], out[i + 3]);
        }
        break;
    }
    return out;
}

QVector<quint16> encodeBits(quint64 value, int byteCount, ByteOrder order)
{
    QVector<quint16> registers;
    if (byteCount <= 0 || byteCount % 2 != 0)
        return registers;

    const QByteArray wire = permute(bigEndianBytes(value, byteCount), order);
    registers.reserve(byteCount / 2);
    for (int i = 0; i < wire.size(); i += 2) {
        const quint16 word = static_cast<quint16>((static_cast<quint8>(wire[i]) << 8)
                                                  | static_cast<quint8>(wire[i + 1]));
        registers.append(word);
    }
    return registers;
}

quint64 decodeBits(const QVector<quint16> &registers, int byteCount, ByteOrder order)
{
    if (byteCount <= 0 || byteCount % 2 != 0 || registers.size() * 2 < byteCount)
        return 0;

    QByteArray wire;
    wire.reserve(byteCount);
    for (int i = 0; i < byteCount / 2; ++i) {
        wire.append(static_cast<char>((registers[i] >> 8) & 0xFF));
        wire.append(static_cast<char>(registers[i] & 0xFF));
    }

    // permute 是对合变换，解码等于再排一次
    return valueOfBigEndian(permute(wire, order));
}

double decodeNumber(const QVector<quint16> &registers, DataType type, ByteOrder order, bool *ok)
{
    if (ok)
        *ok = true;

    if (type == DataType::Ascii) {
        if (ok)
            *ok = false;
        return 0.0;
    }

    if (type == DataType::Bool) {
        if (registers.isEmpty()) {
            if (ok)
                *ok = false;
            return 0.0;
        }
        return registers.first() != 0 ? 1.0 : 0.0;
    }

    const int bytes = dataTypeByteCount(type);
    if (registers.size() * 2 < bytes) {
        if (ok)
            *ok = false;
        return 0.0;
    }

    const quint64 bits = decodeBits(registers, bytes, order);

    switch (type) {
    case DataType::Uint16:
    case DataType::Uint32:
        return static_cast<double>(bits);

    case DataType::Int16:
    case DataType::Int32: {
        const int shift = 64 - signExtendShift(type);
        const qint64 signedValue = static_cast<qint64>(bits << shift) >> shift;
        return static_cast<double>(signedValue);
    }

    case DataType::Float32: {
        const quint32 pattern = static_cast<quint32>(bits);
        float f = 0.0f;
        std::memcpy(&f, &pattern, sizeof(f));
        return static_cast<double>(f);
    }

    case DataType::Float64: {
        double d = 0.0;
        std::memcpy(&d, &bits, sizeof(d));
        return d;
    }

    default:
        break;
    }

    if (ok)
        *ok = false;
    return 0.0;
}

QVector<quint16> encodeNumber(double value, DataType type, ByteOrder order)
{
    if (type == DataType::Ascii)
        return {};

    if (type == DataType::Bool)
        return {static_cast<quint16>(value != 0.0 ? 1 : 0)};

    const int bytes = dataTypeByteCount(type);

    switch (type) {
    case DataType::Float32: {
        const float f = static_cast<float>(value);
        quint32 pattern = 0;
        std::memcpy(&pattern, &f, sizeof(pattern));
        return encodeBits(pattern, bytes, order);
    }
    case DataType::Float64: {
        quint64 pattern = 0;
        std::memcpy(&pattern, &value, sizeof(pattern));
        return encodeBits(pattern, bytes, order);
    }
    case DataType::Int16:
    case DataType::Int32: {
        const qint64 rounded = static_cast<qint64>(std::llround(value));
        const int shift = 64 - signExtendShift(type);
        const quint64 pattern = static_cast<quint64>((rounded << shift) >> shift);
        return encodeBits(pattern, bytes, order);
    }
    default:
        break;
    }

    // 无符号整型：四舍五入后取低位
    const quint64 rounded = static_cast<quint64>(std::llround(value));
    return encodeBits(rounded, bytes, order);
}

QByteArray registersToBytes(const QVector<quint16> &registers)
{
    QByteArray out;
    out.reserve(registers.size() * 2);
    for (quint16 word : registers) {
        out.append(static_cast<char>((word >> 8) & 0xFF));
        out.append(static_cast<char>(word & 0xFF));
    }
    return out;
}

} // namespace protosim::byteorder
