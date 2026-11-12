// SPDX-License-Identifier: GPL-3.0-or-later
//
// L1：MBAP/PDU 解析、粘包重组、异常响应构造、数量合法性、报文摘要。
// 这些都是纯函数，先把它们测死，L2 的 socket 用例才有意义。

#include "protosim/adapters/modbusframe.h"

#include <QtTest>

using namespace protosim;
using namespace protosim::modbus;

namespace {

QByteArray adu(quint16 txId, quint8 unitId, const QByteArray &pdu)
{
    QByteArray out;
    out.append(static_cast<char>((txId >> 8) & 0xFF));
    out.append(static_cast<char>(txId & 0xFF));
    out.append(char(0));
    out.append(char(0));
    const quint16 length = static_cast<quint16>(pdu.size() + 1);
    out.append(static_cast<char>((length >> 8) & 0xFF));
    out.append(static_cast<char>(length & 0xFF));
    out.append(static_cast<char>(unitId));
    out.append(pdu);
    return out;
}

QByteArray pduReadHolding(quint16 start, quint16 count)
{
    QByteArray pdu;
    pdu.append(char(kReadHoldingRegisters));
    pdu.append(static_cast<char>((start >> 8) & 0xFF));
    pdu.append(static_cast<char>(start & 0xFF));
    pdu.append(static_cast<char>((count >> 8) & 0xFF));
    pdu.append(static_cast<char>(count & 0xFF));
    return pdu;
}

} // namespace

class TestModbusFrame : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void parsesReadRequest()
    {
        const QByteArray bytes = adu(0x0001, 1, pduReadHolding(0, 4));
        const Adu parsed = parseAdu(bytes);

        QVERIFY2(parsed.valid, qPrintable(parsed.error));
        QCOMPARE(parsed.transactionId, quint16(1));
        QCOMPARE(parsed.protocolId, quint16(0));
        QCOMPARE(parsed.unitId, quint8(1));
        QCOMPARE(parsed.functionCode, quint8(kReadHoldingRegisters));
        QVERIFY(!parsed.isException());

        const AddressRange range = addressRangeOf(parsed);
        QVERIFY(range.valid);
        QCOMPARE(range.start, quint16(0));
        QCOMPARE(range.count, quint16(4));
    }

    void rejectsBadProtocolId()
    {
        QByteArray bytes = adu(1, 1, pduReadHolding(0, 1));
        bytes[3] = char(1); // 协议号改成 1
        const Adu parsed = parseAdu(bytes);
        QVERIFY(!parsed.valid);
        QVERIFY(!parsed.error.isEmpty());
    }

    void rejectsTruncatedFrame()
    {
        const QByteArray bytes = adu(1, 1, pduReadHolding(0, 4)).left(9);
        QVERIFY(!parseAdu(bytes).valid);
    }

    void detectsExceptionResponse()
    {
        QByteArray pdu;
        pdu.append(char(kReadHoldingRegisters | 0x80));
        pdu.append(char(kIllegalDataAddress));
        const Adu parsed = parseAdu(adu(7, 1, pdu));

        QVERIFY(parsed.valid);
        QVERIFY(parsed.isException());
        QCOMPARE(parsed.transactionId, quint16(7));
    }

    void reassemblerHandlesStickyAndPartialFrames()
    {
        const QByteArray first = adu(1, 1, pduReadHolding(0, 2));
        const QByteArray second = adu(2, 1, pduReadHolding(4, 2));

        AduReassembler reassembler;

        // 半包
        QVERIFY(reassembler.consume(first.left(3)).isEmpty());
        // 补全 + 第二帧粘在一起
        const QVector<QByteArray> frames = reassembler.consume(first.mid(3) + second);
        QCOMPARE(frames.size(), 2);
        QCOMPARE(frames.at(0), first);
        QCOMPARE(frames.at(1), second);
        QVERIFY(reassembler.pendingBytes().isEmpty());
    }

    void reassemblerResyncsOnGarbage()
    {
        AduReassembler reassembler;
        const QByteArray frame = adu(9, 1, pduReadHolding(0, 1));
        const QVector<QByteArray> frames = reassembler.consume(QByteArray::fromHex("FF") + frame);
        QCOMPARE(frames.size(), 1);
        QCOMPARE(frames.at(0), frame);
    }

    void buildsExceptionResponse()
    {
        const QByteArray response =
            buildExceptionResponse(0x1234, 1, kReadHoldingRegisters, kIllegalDataAddress);
        QCOMPARE(response.toHex(), QByteArray("123400000003018302"));

        const Adu parsed = parseAdu(response);
        QVERIFY(parsed.valid);
        QVERIFY(parsed.isException());
        QCOMPARE(parsed.transactionId, quint16(0x1234));
    }

    void quantityBounds()
    {
        QVERIFY(isQuantityValid(kReadHoldingRegisters, 1));
        QVERIFY(isQuantityValid(kReadHoldingRegisters, 125));
        QVERIFY(!isQuantityValid(kReadHoldingRegisters, 0));
        QVERIFY(!isQuantityValid(kReadHoldingRegisters, 126));

        QVERIFY(isQuantityValid(kReadCoils, 2000));
        QVERIFY(!isQuantityValid(kReadCoils, 2001));

        QVERIFY(isQuantityValid(kWriteMultipleRegisters, 123));
        QVERIFY(!isQuantityValid(kWriteMultipleRegisters, 124));

        // 不支持的功能码一律非法
        QVERIFY(!isQuantityValid(0x2B, 1));
    }

    void describesRequests()
    {
        const Adu read = parseAdu(adu(1, 1, pduReadHolding(0, 2)));
        const QString readText = describe(read, {QStringLiteral("temperature")});
        QVERIFY2(readText.contains(QStringLiteral("FC03")), qPrintable(readText));
        QVERIFY2(readText.contains(QStringLiteral("读")), qPrintable(readText));
        QVERIFY2(readText.contains(QStringLiteral("temperature")), qPrintable(readText));

        QByteArray coilPdu;
        coilPdu.append(char(kWriteSingleCoil));
        coilPdu.append(char(0));
        coilPdu.append(char(5));
        coilPdu.append(char(0xFF));
        coilPdu.append(char(0));
        const QString coilText = describe(parseAdu(adu(2, 1, coilPdu)));
        QVERIFY2(coilText.contains(QStringLiteral("FC05")), qPrintable(coilText));
        QVERIFY2(coilText.contains(QStringLiteral("ON")), qPrintable(coilText));
    }

    void describesException()
    {
        QByteArray pdu;
        pdu.append(char(kReadHoldingRegisters | 0x80));
        pdu.append(char(kIllegalFunction));
        const QString text = describe(parseAdu(adu(1, 1, pdu)));
        QVERIFY2(text.contains(QStringLiteral("0x01")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("Illegal Function")), qPrintable(text));
    }

    void areaNamesFollowPlcConvention()
    {
        QCOMPARE(registerAreaName(kReadHoldingRegisters), QStringLiteral("HoldingRegisters"));
        QCOMPARE(registerAreaName(kReadInputRegisters), QStringLiteral("InputRegisters"));
        QCOMPARE(registerAreaName(kReadCoils), QStringLiteral("Coils"));
        QCOMPARE(registerAreaName(kReadDiscreteInputs), QStringLiteral("DiscreteInputs"));
    }
};

QTEST_GUILESS_MAIN(TestModbusFrame)
#include "tst_modbusframe.moc"
