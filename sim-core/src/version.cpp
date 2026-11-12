// SPDX-License-Identifier: GPL-3.0-or-later
#include "protosim/version.h"

#include <QStringList>

namespace protosim {

QString versionString()
{
#ifdef PROTOSIM_VERSION_STRING
    return QStringLiteral(PROTOSIM_VERSION_STRING);
#else
    return QStringLiteral("0.0.0-unknown");
#endif
}

QString buildInfoString()
{
    QStringList parts;
    parts << QStringLiteral("ProtoSim %1").arg(versionString());

#if defined(_MSC_VER)
    parts << QStringLiteral("MSVC %1.%2")
                 .arg(_MSC_VER / 100)
                 .arg(_MSC_VER % 100);
#elif defined(__clang__)
    parts << QStringLiteral("Clang %1.%2").arg(__clang_major__).arg(__clang_minor__);
#elif defined(__GNUC__)
    parts << QStringLiteral("GCC %1.%2").arg(__GNUC__).arg(__GNUC_MINOR__);
#else
    parts << QStringLiteral("unknown compiler");
#endif

    parts << QStringLiteral("Qt %1").arg(QString::fromLatin1(qVersion()));

    return parts.join(QStringLiteral(" | "));
}

} // namespace protosim
