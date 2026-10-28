/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "icaltime.h"

namespace OCC::Nourax::Agenda {

QTimeZone ICalTime::zoneFor(const QString &tzid)
{
    if (tzid.isEmpty()) {
        return QTimeZone();
    }
    QString name = tzid;
    // Some clients write "/mozilla.org/20070129_1/Europe/London".
    const auto marker = name.indexOf(QLatin1String("/20"));
    if (name.startsWith(QLatin1Char('/')) && marker > 0) {
        const auto parts = name.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        if (parts.size() >= 2) {
            name = parts.mid(parts.size() - 2).join(QLatin1Char('/'));
        }
    }
    QTimeZone zone(name.toUtf8());
    if (zone.isValid()) {
        return zone;
    }
    const auto iana = QTimeZone::windowsIdToDefaultIanaId(name.toUtf8());
    if (!iana.isEmpty()) {
        QTimeZone mapped(iana);
        if (mapped.isValid()) {
            return mapped;
        }
    }
    return QTimeZone();
}

ICalTime ICalTime::parseValue(const QString &valueIn, const QString &tzid, bool dateOnly, const QTimeZone &floatingZone)
{
    ICalTime t;
    const QString value = valueIn.trimmed();
    if (value.size() >= 8) {
        const QDate date = QDate::fromString(value.left(8), QStringLiteral("yyyyMMdd"));
        if (!date.isValid()) {
            return t;
        }
        if (dateOnly || value.size() == 8) {
            t.valid = true;
            t.isDate = true;
            t.date = date;
            return t;
        }
        if (value.size() >= 15 && value.at(8) == QLatin1Char('T')) {
            const QTime time = QTime::fromString(value.mid(9, 6), QStringLiteral("HHmmss"));
            if (!time.isValid()) {
                return t;
            }
            t.valid = true;
            t.date = date;
            if (value.endsWith(QLatin1Char('Z')) || value.endsWith(QLatin1Char('z'))) {
                t.utc = QDateTime(date, time, QTimeZone::UTC);
                return t;
            }
            QTimeZone zone = zoneFor(tzid);
            if (!zone.isValid()) {
                t.floating = tzid.isEmpty();
                t.approximated = !tzid.isEmpty();
                zone = floatingZone.isValid() ? floatingZone : QTimeZone::systemTimeZone();
            }
            t.utc = QDateTime(date, time, zone).toUTC();
            return t;
        }
    }
    return t;
}

ICalTime ICalTime::parse(const ICalProperty &property, const QTimeZone &floatingZone)
{
    const bool dateOnly = property.param(QStringLiteral("VALUE")).compare(QLatin1String("DATE"), Qt::CaseInsensitive) == 0;
    return parseValue(property.raw, property.param(QStringLiteral("TZID")), dateOnly, floatingZone);
}

qint64 ICalTime::parseDurationSeconds(const QString &valueIn)
{
    QString value = valueIn.trimmed();
    int sign = 1;
    if (value.startsWith(QLatin1Char('-'))) {
        sign = -1;
        value.remove(0, 1);
    } else if (value.startsWith(QLatin1Char('+'))) {
        value.remove(0, 1);
    }
    if (!value.startsWith(QLatin1Char('P'))) {
        return -1;
    }
    qint64 total = 0;
    bool inTime = false;
    QString number;
    for (int i = 1; i < value.size(); ++i) {
        const QChar c = value.at(i);
        if (c == QLatin1Char('T')) {
            inTime = true;
        } else if (c.isDigit()) {
            number.append(c);
        } else {
            if (number.isEmpty()) {
                return -1;
            }
            const qint64 n = number.toLongLong();
            number.clear();
            switch (c.toLatin1()) {
            case 'W':
                total += n * 7 * 86400;
                break;
            case 'D':
                total += n * 86400;
                break;
            case 'H':
                total += n * 3600;
                break;
            case 'M':
                total += inTime ? n * 60 : 0;
                break;
            case 'S':
                total += n;
                break;
            default:
                return -1;
            }
        }
    }
    return sign * total;
}

} // namespace OCC::Nourax::Agenda
