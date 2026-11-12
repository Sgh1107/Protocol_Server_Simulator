// SPDX-License-Identifier: GPL-3.0-or-later
#include "protosim/quality.h"

namespace protosim {

QString qualityToString(Quality quality)
{
    switch (quality) {
    case Quality::Good:
        return QStringLiteral("Good");
    case Quality::Bad:
        return QStringLiteral("Bad");
    case Quality::Timeout:
        return QStringLiteral("Timeout");
    case Quality::OutOfRange:
        return QStringLiteral("OutOfRange");
    case Quality::Unknown:
        break;
    }
    return QStringLiteral("Unknown");
}

Quality qualityFromString(const QString &text, Quality fallback)
{
    if (text == QLatin1String("Good"))
        return Quality::Good;
    if (text == QLatin1String("Bad"))
        return Quality::Bad;
    if (text == QLatin1String("Timeout"))
        return Quality::Timeout;
    if (text == QLatin1String("OutOfRange"))
        return Quality::OutOfRange;
    if (text == QLatin1String("Unknown"))
        return Quality::Unknown;
    return fallback;
}

} // namespace protosim
