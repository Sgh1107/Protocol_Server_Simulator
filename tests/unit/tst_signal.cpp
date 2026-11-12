// SPDX-License-Identifier: GPL-3.0-or-later
//
// AC-P0-03：scale=0.1 / offset=-40，原始 500 -> 10.0；越界被钳位并置 OutOfRange。

#include "protosim/signal.h"

#include <QtTest>

using namespace protosim;

class TestSignal : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void scaling()
    {
        Signal s;
        s.id = QStringLiteral("temperature");
        s.name = QStringLiteral("温度");
        s.dataType = DataType::Uint16;
        s.scale = 0.1;
        s.offset = -40.0;

        QCOMPARE(s.rawToEngineering(500), 10.0);
        QCOMPARE(s.rawToEngineering(0), -40.0);
        QCOMPARE(s.engineeringToRaw(10.0), 500.0);
        QVERIFY(s.isValid());
    }

    void clampMarksOutOfRange()
    {
        Signal s;
        s.id = QStringLiteral("p");
        s.name = QStringLiteral("压力");
        s.scale = 0.1;
        s.offset = 0.0;
        s.range.min = 0.0;
        s.range.max = 100.0;
        s.range.valid = true;

        bool out = false;
        QCOMPARE(s.clampToRange(10.0, &out), 10.0);
        QVERIFY(!out);

        QCOMPARE(s.clampToRange(150.0, &out), 100.0);
        QVERIFY(out);

        QCOMPARE(s.clampToRange(-5.0, &out), 0.0);
        QVERIFY(out);
    }

    void noRangeMeansNoClamp()
    {
        Signal s;
        bool out = true;
        QCOMPARE(s.clampToRange(1e9, &out), 1e9);
        QVERIFY(!out);
    }

    void reversedRangeIsTolerated()
    {
        Signal s;
        s.range.min = 100.0;
        s.range.max = 0.0;
        s.range.valid = true;

        bool out = false;
        QCOMPARE(s.clampToRange(150.0, &out), 100.0);
        QVERIFY(out);
    }

    void registerCountFollowsDataType()
    {
        Signal s;
        s.dataType = DataType::Uint16;
        QCOMPARE(s.registerCount(), 1);

        s.dataType = DataType::Float32;
        QCOMPARE(s.registerCount(), 2);

        s.dataType = DataType::Float64;
        QCOMPARE(s.registerCount(), 4);
    }

    void stringConversionsRoundTrip()
    {
        for (auto type : {DataType::Bool,
                          DataType::Uint16,
                          DataType::Int16,
                          DataType::Uint32,
                          DataType::Int32,
                          DataType::Float32,
                          DataType::Float64,
                          DataType::Ascii}) {
            DataType parsed = DataType::Bool;
            QVERIFY(dataTypeFromString(dataTypeToString(type), &parsed));
            QCOMPARE(parsed, type);
        }

        for (auto order : {ByteOrder::ABCD, ByteOrder::BADC, ByteOrder::CDAB, ByteOrder::DCBA}) {
            ByteOrder parsed = ByteOrder::ABCD;
            QVERIFY(byteOrderFromString(byteOrderToString(order), &parsed));
            QCOMPARE(parsed, order);
        }

        DataType unknown = DataType::Bool;
        QVERIFY(!dataTypeFromString(QStringLiteral("float16"), &unknown));
        ByteOrder unknownOrder = ByteOrder::ABCD;
        QVERIFY(!byteOrderFromString(QStringLiteral("ACBD"), &unknownOrder));
    }

    void byteCountMatchesDataType()
    {
        QCOMPARE(dataTypeByteCount(DataType::Bool), 1);
        QCOMPARE(dataTypeByteCount(DataType::Uint16), 2);
        QCOMPARE(dataTypeByteCount(DataType::Int32), 4);
        QCOMPARE(dataTypeByteCount(DataType::Float64), 8);
        QVERIFY(dataTypeIsFloat(DataType::Float32));
        QVERIFY(!dataTypeIsFloat(DataType::Uint32));
        QVERIFY(dataTypeIsSigned(DataType::Int16));
        QVERIFY(!dataTypeIsSigned(DataType::Uint16));
    }
};

QTEST_GUILESS_MAIN(TestSignal)
#include "tst_signal.moc"
