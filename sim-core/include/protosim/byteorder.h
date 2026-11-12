// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "protosim/signal.h"

#include <QByteArray>
#include <QVector>

namespace protosim::byteorder {

/// 把规范大端字节重排成指定字节序的线上字节（AC-P0-02）。
/// 四种排列都是对合变换（自己就是自己的逆），因此解码复用同一函数。
QByteArray permute(const QByteArray &bigEndian, ByteOrder order);

/// 数值 <-> 寄存器。寄存器按 Modbus 线序大端（高字节在前）。
QVector<quint16> encodeBits(quint64 value, int byteCount, ByteOrder order);
quint64 decodeBits(const QVector<quint16> &registers, int byteCount, ByteOrder order);

/// 按信号类型解码成 double（未做 scale/offset）。失败时 *ok=false。
/// ascii 不走数值编解码，返回 false。
double decodeNumber(const QVector<quint16> &registers, DataType type, ByteOrder order, bool *ok);

/// 按信号类型把 double 编码成寄存器。越界/非数由调用方先钳位。
QVector<quint16> encodeNumber(double value, DataType type, ByteOrder order);

/// 寄存器 -> 大端 hex 字节串，报文日志用。
QByteArray registersToBytes(const QVector<quint16> &registers);

} // namespace protosim::byteorder
