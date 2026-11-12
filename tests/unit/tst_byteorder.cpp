// SPDX-License-Identifier: GPL-3.0-or-later
//
// AC-P0-02：对 0x12345678 做 ABCD/DCBA/BADC/CDAB 互相换算并回读，
// 四次往返完全还原，且寄存器摆放符合 Modbus 常规约定。

#include "protosim/byteorder.h"

#include <QtTest>

using namespace protosim;
using namespace protosim::byteorder;

namespace {

QString regsToHex(const QVector<quint16> &regs)
{
    QStringList parts;
    for (quint16 r : regs) {
        parts << QStringLiteral("0x")
                     + QString::number(r, 16).rightJustified(4, QLatin1Char('0')).toUpper();
    }
    return parts.join(QLatin1Char(' '));
}

} // namespace

class TestByteOrder : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void registerLayout_data();
    void registerLayout();

    void roundTrip_data();
    void roundTrip();

    void permuteIsSelfInverse();

    void decodesFloat32();
    void decodesSignedIntegers();
    void registerBytesAreBigEndian();
};

void TestByteOrder::registerLayout_data()
{
    QTest::addColumn<int>("order");
    QTest::addColumn<QString>("expected");

    // 0x12345678 在四种字节序下，寄存器里实际摆放的值
    QTest::newRow("ABCD 规范大端") << int(ByteOrder::ABCD) << QStringLiteral("0x1234 0x5678");
    QTest::newRow("BADC 字节内互换") << int(ByteOrder::BADC) << QStringLiteral("0x3412 0x7856");
    QTest::newRow("CDAB 字内字间互换") << int(ByteOrder::CDAB) << QStringLiteral("0x5678 0x1234");
    QTest::newRow("DCBA 完整小端") << int(ByteOrder::DCBA) << QStringLiteral("0x7856 0x3412");
}

void TestByteOrder::registerLayout()
{
    QFETCH(int, order);
    QFETCH(QString, expected);

    const QVector<quint16> actual = encodeBits(0x12345678u, 4, static_cast<ByteOrder>(order));

    QCOMPARE(regsToHex(actual), expected);
}

void TestByteOrder::roundTrip_data()
{
    QTest::addColumn<int>("order");
    QTest::addColumn<uint>("value");
    QTest::addColumn<int>("byteCount");

    for (int order :
         {int(ByteOrder::ABCD), int(ByteOrder::BADC), int(ByteOrder::CDAB), int(ByteOrder::DCBA)}) {
        QTest::newRow(qPrintable(QStringLiteral("0x12345678 order=%1").arg(order)))
            << order << 0x12345678u << 4;
        QTest::newRow(qPrintable(QStringLiteral("0xABCD order=%1").arg(order)))
            << order << 0xABCDu << 2;
        QTest::newRow(qPrintable(QStringLiteral("0xDEADBEEFCAFEBABE order=%1").arg(order)))
            << order << 0xDEADBEEFu << 4;
    }
}

void TestByteOrder::roundTrip()
{
    QFETCH(int, order);
    QFETCH(uint, value);
    QFETCH(int, byteCount);

    const auto byteOrder = static_cast<ByteOrder>(order);
    const QVector<quint16> regs = encodeBits(value, byteCount, byteOrder);
    QCOMPARE(regs.size(), byteCount / 2);
    QCOMPARE(decodeBits(regs, byteCount, byteOrder), quint64(value));
}

void TestByteOrder::permuteIsSelfInverse()
{
    const QByteArray be = QByteArray::fromHex("12345678");
    for (auto order : {ByteOrder::ABCD, ByteOrder::BADC, ByteOrder::CDAB, ByteOrder::DCBA}) {
        const QByteArray wired = permute(be, order);
        QCOMPARE(permute(wired, order), be);
    }
}

void TestByteOrder::decodesFloat32()
{
    // 23.5f 的 IEEE754 位型是 0x41BC0000
    const QVector<quint16> regs = encodeNumber(23.5, DataType::Float32, ByteOrder::ABCD);
    QCOMPARE(regsToHex(regs), QStringLiteral("0x41BC 0x0000"));

    bool ok = false;
    QCOMPARE(decodeNumber(regs, DataType::Float32, ByteOrder::ABCD, &ok), 23.5);
    QVERIFY(ok);

    // 同一数值换成 CDAB，寄存器顺序应互换，解码结果不变
    const QVector<quint16> swapped = encodeNumber(23.5, DataType::Float32, ByteOrder::CDAB);
    QCOMPARE(regsToHex(swapped), QStringLiteral("0x0000 0x41BC"));
    QCOMPARE(decodeNumber(swapped, DataType::Float32, ByteOrder::CDAB, &ok), 23.5);
    QVERIFY(ok);
}

void TestByteOrder::decodesSignedIntegers()
{
    bool ok = false;

    const QVector<quint16> negativeOne = encodeNumber(-1, DataType::Int16, ByteOrder::ABCD);
    QCOMPARE(negativeOne, QVector<quint16>{0xFFFF});
    QCOMPARE(decodeNumber(negativeOne, DataType::Int16, ByteOrder::ABCD, &ok), -1.0);
    QVERIFY(ok);

    const QVector<quint16> minus40 = encodeNumber(-40, DataType::Int32, ByteOrder::DCBA);
    QCOMPARE(decodeNumber(minus40, DataType::Int32, ByteOrder::DCBA, &ok), -40.0);
    QVERIFY(ok);

    // uint16 全 1 是 65535，不是 -1
    const QVector<quint16> maxUint = {0xFFFF};
    QCOMPARE(decodeNumber(maxUint, DataType::Uint16, ByteOrder::ABCD, &ok), 65535.0);
    QVERIFY(ok);
}

void TestByteOrder::registerBytesAreBigEndian()
{
    QCOMPARE(registersToBytes({0x1234, 0x5678}), QByteArray::fromHex("12345678"));
}

QTEST_GUILESS_MAIN(TestByteOrder)
#include "tst_byteorder.moc"
