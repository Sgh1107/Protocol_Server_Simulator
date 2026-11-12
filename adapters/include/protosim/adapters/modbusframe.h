// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace protosim::modbus {

/// MBAP 头 7 字节：事务号(2) 协议号(2) 长度(2) 单元号(1)
constexpr int kMbapSize = 7;
constexpr int kMaxPduSize = 253;
constexpr int kMaxAduSize = kMbapSize + kMaxPduSize;

// 功能码
constexpr quint8 kReadCoils = 0x01;
constexpr quint8 kReadDiscreteInputs = 0x02;
constexpr quint8 kReadHoldingRegisters = 0x03;
constexpr quint8 kReadInputRegisters = 0x04;
constexpr quint8 kWriteSingleCoil = 0x05;
constexpr quint8 kWriteSingleRegister = 0x06;
constexpr quint8 kWriteMultipleCoils = 0x0F;
constexpr quint8 kWriteMultipleRegisters = 0x10;

// 异常码
constexpr quint8 kIllegalFunction = 0x01;
constexpr quint8 kIllegalDataAddress = 0x02;
constexpr quint8 kIllegalDataValue = 0x03;
constexpr quint8 kServerDeviceFailure = 0x04;

struct Adu
{
    bool valid = false;
    QString error;

    quint16 transactionId = 0;
    quint16 protocolId = 0;
    quint16 length = 0; // 其后字节数 = unitId + pdu
    quint8 unitId = 0;
    quint8 functionCode = 0;
    QByteArray pdu;     // 含功能码

    bool isException() const { return (functionCode & 0x80) != 0; }
};

/// 解析一条完整 ADU（含 MBAP 头）。不完整/协议号非 0 都会被判为无效。
Adu parseAdu(const QByteArray &bytes);

/// 从字节流里切出完整 ADU。用于 TCP 粘包/半包，一次 readyRead 可能有多帧。
class AduReassembler
{
public:
    QVector<QByteArray> consume(const QByteArray &chunk);
    void clear() { m_buffer.clear(); }
    QByteArray pendingBytes() const { return m_buffer; }

private:
    QByteArray m_buffer;
};

/// 读写请求涉及的地址范围。写多寄存器时还会带上数据长度，供 0x03 判定。
struct AddressRange
{
    bool valid = false;
    quint8 functionCode = 0;
    quint16 start = 0;
    quint16 count = 0;
    int dataBytes = 0;
};

AddressRange addressRangeOf(const Adu &adu);

QString functionName(quint8 functionCode);
QString exceptionName(quint8 code);

/// 寄存器区名称（PLC 惯例：0x 线圈 / 1x 离散输入 / 3x 输入寄存器 / 4x 保持寄存器）。
QString registerAreaName(quint8 functionCode);

/// 人类可读摘要，报文日志的「解析」列。
///
/// `isRequest` 必须显式给：FC03 的请求是「起始地址+数量」，响应是「字节数+数据」，
/// 只看 PDU 前几个字节无法区分，按请求解析会把响应读成乱码范围。
///
/// 这里只产出「协议层人话」，不带信号名——列宽有限，多一个名字就把范围挤没了；
/// 按信号过滤走 FrameRecord::signalIds，导出文本里也单独带一段。
QString describe(const Adu &adu, bool isRequest = true);

/// 构造异常响应 ADU（事务号原样回，功能码 OR 0x80）。
QByteArray buildExceptionResponse(quint16 transactionId, quint8 unitId, quint8 functionCode,
                                 quint8 exceptionCode);

/// 各功能码的数量合法区间（Modbus 规范），越界应回 0x03。
bool isQuantityValid(quint8 functionCode, quint16 count);

QString toHex(const QByteArray &bytes);

} // namespace protosim::modbus
