// SPDX-License-Identifier: GPL-3.0-or-later
#include "protosim/adapters/modbusframe.h"


namespace protosim::modbus {
namespace {

quint16 readUInt16(const QByteArray &bytes, int offset)
{
    return static_cast<quint16>((static_cast<quint8>(bytes.at(offset)) << 8)
                                | static_cast<quint8>(bytes.at(offset + 1)));
}

} // namespace

Adu parseAdu(const QByteArray &bytes)
{
    Adu adu;

    if (bytes.size() < kMbapSize + 1) {
        adu.error = QStringLiteral("ADU 长度不足（%1 字节）").arg(bytes.size());
        return adu;
    }

    adu.transactionId = readUInt16(bytes, 0);
    adu.protocolId = readUInt16(bytes, 2);
    adu.length = readUInt16(bytes, 4);
    adu.unitId = static_cast<quint8>(bytes.at(6));

    if (adu.protocolId != 0) {
        adu.error = QStringLiteral("协议号必须为 0，实际 %1").arg(adu.protocolId);
        return adu;
    }

    // length 覆盖 unitId + PDU，因此总长 = 6 + length
    const int expected = 6 + static_cast<int>(adu.length);
    if (adu.length < 2 || bytes.size() < expected) {
        adu.error = QStringLiteral("MBAP 长度字段与实际不符（length=%1, 实际 %2 字节）")
                        .arg(adu.length)
                        .arg(bytes.size());
        return adu;
    }

    adu.functionCode = static_cast<quint8>(bytes.at(7));
    adu.pdu = bytes.mid(kMbapSize, expected - kMbapSize);
    adu.valid = true;
    return adu;
}

QVector<QByteArray> AduReassembler::consume(const QByteArray &chunk)
{
    m_buffer.append(chunk);

    QVector<QByteArray> frames;
    while (m_buffer.size() >= kMbapSize) {
        const int length = (static_cast<quint8>(m_buffer.at(4)) << 8)
            | static_cast<quint8>(m_buffer.at(5));
        if (length < 2 || length > kMaxPduSize + 1) {
            // 不是合法 MBAP，丢掉首字节重新同步，避免整条连接卡死
            m_buffer.remove(0, 1);
            continue;
        }
        const int total = 6 + length;
        if (m_buffer.size() < total)
            break;

        frames.append(m_buffer.left(total));
        m_buffer.remove(0, total);
    }

    return frames;
}

AddressRange addressRangeOf(const Adu &adu)
{
    AddressRange range;
    if (!adu.valid)
        return range;

    range.functionCode = adu.functionCode;

    switch (adu.functionCode) {
    case kReadCoils:
    case kReadDiscreteInputs:
    case kReadHoldingRegisters:
    case kReadInputRegisters:
        if (adu.pdu.size() < 5)
            return range;
        range.start = readUInt16(adu.pdu, 1);
        range.count = readUInt16(adu.pdu, 3);
        range.valid = true;
        return range;

    case kWriteSingleCoil:
    case kWriteSingleRegister:
        if (adu.pdu.size() < 5)
            return range;
        range.start = readUInt16(adu.pdu, 1);
        range.count = 1;
        range.dataBytes = 2;
        range.valid = true;
        return range;

    case kWriteMultipleCoils:
    case kWriteMultipleRegisters:
        if (adu.pdu.size() < 6)
            return range;
        range.start = readUInt16(adu.pdu, 1);
        range.count = readUInt16(adu.pdu, 3);
        range.dataBytes = static_cast<quint8>(adu.pdu.at(5));
        range.valid = true;
        return range;

    default:
        return range;
    }
}

QString functionName(quint8 functionCode)
{
    switch (functionCode) {
    case kReadCoils:
        return QStringLiteral("读线圈");
    case kReadDiscreteInputs:
        return QStringLiteral("读离散输入");
    case kReadHoldingRegisters:
        return QStringLiteral("读保持寄存器");
    case kReadInputRegisters:
        return QStringLiteral("读输入寄存器");
    case kWriteSingleCoil:
        return QStringLiteral("写单线圈");
    case kWriteSingleRegister:
        return QStringLiteral("写单寄存器");
    case kWriteMultipleCoils:
        return QStringLiteral("写多线圈");
    case kWriteMultipleRegisters:
        return QStringLiteral("写多寄存器");
    default:
        break;
    }

    if ((functionCode & 0x80) != 0)
        return QStringLiteral("异常响应");

    return QStringLiteral("未知功能码");
}

QString exceptionName(quint8 code)
{
    switch (code) {
    case kIllegalFunction:
        return QStringLiteral("Illegal Function");
    case kIllegalDataAddress:
        return QStringLiteral("Illegal Data Address");
    case kIllegalDataValue:
        return QStringLiteral("Illegal Data Value");
    case kServerDeviceFailure:
        return QStringLiteral("Server Device Failure");
    default:
        break;
    }
    return QStringLiteral("Unknown Exception");
}

QString registerAreaName(quint8 functionCode)
{
    switch (functionCode) {
    case kReadCoils:
    case kWriteSingleCoil:
    case kWriteMultipleCoils:
        return QStringLiteral("Coils");
    case kReadDiscreteInputs:
        return QStringLiteral("DiscreteInputs");
    case kReadHoldingRegisters:
    case kWriteSingleRegister:
    case kWriteMultipleRegisters:
        return QStringLiteral("HoldingRegisters");
    case kReadInputRegisters:
        return QStringLiteral("InputRegisters");
    default:
        break;
    }
    return QStringLiteral("Unknown");
}

namespace {

/// 解析列用的短区名。FC 已经在自己的列里了，这里不再重复"FC03 读保持寄存器"。
QString shortAreaName(quint8 functionCode)
{
    switch (functionCode) {
    case kReadCoils:
    case kWriteSingleCoil:
    case kWriteMultipleCoils:
        return QStringLiteral("CO");
    case kReadDiscreteInputs:
        return QStringLiteral("DI");
    case kReadInputRegisters:
        return QStringLiteral("IR");
    default:
        break;
    }
    return QStringLiteral("HR");
}

QString exceptionText(quint8 code)
{
    switch (code) {
    case kIllegalFunction:
        return QStringLiteral("非法功能码");
    case kIllegalDataAddress:
        return QStringLiteral("地址非法");
    case kIllegalDataValue:
        return QStringLiteral("数据值非法");
    case kServerDeviceFailure:
        return QStringLiteral("设备故障");
    default:
        break;
    }
    return QStringLiteral("未知异常");
}

} // namespace

QString describe(const Adu &adu, bool isRequest)
{
    if (!adu.valid)
        return adu.error;

    QString text;

    if (adu.isException()) {
        if (adu.pdu.size() < 2)
            return QStringLiteral("异常响应（格式错误）");
        const quint8 code = static_cast<quint8>(adu.pdu.at(1));
        return QStringLiteral("0x%1 %2")
            .arg(QString::number(code, 16).toUpper().rightJustified(2, QLatin1Char('0')),
                 exceptionText(code));
    }

    // 响应帧：只有当它是异常时才有语义，正常响应只报数据长度。
    // 读类响应的第 2 个字节就是字节数，按它报才是"4 个字节的数据"，
    // 而不是把计数位也算进去（那样 2 个寄存器会显示成 5 字节）。
    if (!isRequest) {
        int payloadBytes = qMax(0, adu.pdu.size() - 1);
        const quint8 fc = adu.functionCode;
        const bool isReadResponse = fc == kReadCoils || fc == kReadDiscreteInputs
            || fc == kReadHoldingRegisters || fc == kReadInputRegisters;
        if (isReadResponse && adu.pdu.size() >= 2)
            payloadBytes = static_cast<quint8>(adu.pdu.at(1));

        return QStringLiteral("响应 %1 字节").arg(payloadBytes);
    }

    const AddressRange range = addressRangeOf(adu);

    switch (adu.functionCode) {
    case kReadCoils:
    case kReadDiscreteInputs:
    case kReadHoldingRegisters:
    case kReadInputRegisters: {
        if (!range.valid)
            return QStringLiteral("FC%1 报文长度不足").arg(adu.functionCode, 2, 16, QLatin1Char('0'));

        const quint16 last = range.count > 0 ? range.start + range.count - 1 : range.start;
        text = QStringLiteral("读 %1[%2..%3] · %4 个")
                   .arg(shortAreaName(adu.functionCode))
                   .arg(range.start)
                   .arg(last)
                   .arg(range.count);
        break;
    }

    case kWriteSingleCoil: {
        // 线圈值在 pdu[3..4]，0xFF00 = ON，0x0000 = OFF
        const bool on = adu.pdu.size() >= 5 && readUInt16(adu.pdu, 3) == 0xFF00;
        text = QStringLiteral("写 CO[%1] = %2")
                   .arg(range.start)
                   .arg(on ? QStringLiteral("ON") : QStringLiteral("OFF"));
        break;
    }

    case kWriteSingleRegister: {
        const quint16 value = adu.pdu.size() >= 5 ? readUInt16(adu.pdu, 3) : 0;
        text = QStringLiteral("写 %1[%2] = %3")
                   .arg(shortAreaName(adu.functionCode))
                   .arg(range.start)
                   .arg(value);
        break;
    }

    case kWriteMultipleCoils:
    case kWriteMultipleRegisters: {
        const quint16 last = range.count > 0 ? range.start + range.count - 1 : range.start;
        text = QStringLiteral("写 %1[%2..%3] · %4 个")
                   .arg(shortAreaName(adu.functionCode))
                   .arg(range.start)
                   .arg(last)
                   .arg(range.count);
        break;
    }

    default:
        text = QStringLiteral("功能码 0x%1")
                   .arg(QString::number(adu.functionCode, 16).toUpper().rightJustified(
                       2, QLatin1Char('0')));
        break;
    }

    return text;
}

QByteArray buildExceptionResponse(quint16 transactionId,
                                  quint8 unitId,
                                  quint8 functionCode,
                                  quint8 exceptionCode)
{
    QByteArray out;
    out.reserve(kMbapSize + 2);

    out.append(static_cast<char>((transactionId >> 8) & 0xFF));
    out.append(static_cast<char>(transactionId & 0xFF));
    out.append(char(0));
    out.append(char(0));
    out.append(char(0));
    out.append(char(3)); // unitId(1) + fc(1) + code(1)
    out.append(static_cast<char>(unitId));
    out.append(static_cast<char>(functionCode | 0x80));
    out.append(static_cast<char>(exceptionCode));

    return out;
}

bool isQuantityValid(quint8 functionCode, quint16 count)
{
    switch (functionCode) {
    case kReadCoils:
    case kReadDiscreteInputs:
        return count >= 1 && count <= 2000;
    case kReadHoldingRegisters:
    case kReadInputRegisters:
        return count >= 1 && count <= 125;
    case kWriteMultipleCoils:
        return count >= 1 && count <= 1968;
    case kWriteMultipleRegisters:
        return count >= 1 && count <= 123;
    case kWriteSingleCoil:
    case kWriteSingleRegister:
        return count == 1;
    default:
        return false;
    }
}

QString toHex(const QByteArray &bytes)
{
    if (bytes.isEmpty())
        return QString();
    return QString::fromLatin1(bytes.toHex(' ')).toUpper();
}

} // namespace protosim::modbus
