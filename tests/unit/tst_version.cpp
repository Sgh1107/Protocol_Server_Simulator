// SPDX-License-Identifier: GPL-3.0-or-later
#include "protosim/version.h"

#include <QtTest>

class TestVersion : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void versionIsInjectedFromCMake()
    {
        // 版本号必须由 cmake/ProjectVersions.cmake 一路注入到这里，
        // 不允许源码里另写一份（那是"版本真相源分裂"的开始）。
        QVERIFY(!protosim::versionString().isEmpty());
        QVERIFY(protosim::versionString() != QStringLiteral("0.0.0-unknown"));
        QVERIFY(protosim::versionString().contains(QLatin1Char('.')));
    }

    void buildInfoHasCompilerAndQt()
    {
        const QString info = protosim::buildInfoString();
        QVERIFY2(info.contains(QStringLiteral("ProtoSim")), qPrintable(info));
        QVERIFY2(info.contains(QStringLiteral("Qt ")), qPrintable(info));
    }
};

QTEST_GUILESS_MAIN(TestVersion)
#include "tst_version.moc"
