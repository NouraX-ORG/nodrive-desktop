/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: iCalendar dates and times. DATE, DATE-TIME with Z, with TZID (IANA or Windows id) and floating.

#pragma once

#include "icaltext.h"

#include <QDate>
#include <QDateTime>
#include <QTimeZone>

namespace OCC::Nourax::Agenda {

struct ICalTime
{
    bool valid = false;
    bool isDate = false; ///< a DATE (all-day)
    QDate date; ///< for a DATE: that day; for a DATE-TIME: the local date in its own zone
    QDateTime utc; ///< for a DATE-TIME
    bool floating = false;
    bool approximated = false; ///< the time zone was not known; local time was used

    /// Reads DTSTART, DTEND, DUE, ... `floatingZone` is used for floating times (the computer's zone).
    static ICalTime parse(const ICalProperty &property, const QTimeZone &floatingZone);
    /// Reads "YYYYMMDD" or "YYYYMMDDTHHMMSS[Z]" with an optional zone name.
    static ICalTime parseValue(const QString &value, const QString &tzid, bool dateOnly, const QTimeZone &floatingZone);
    /// RFC 5545 DURATION (P1D, PT1H30M, P1W) in seconds; -1 if invalid.
    static qint64 parseDurationSeconds(const QString &value);
    /// Maps a TZID (IANA name, or a Windows zone name) to a zone; invalid if unknown.
    static QTimeZone zoneFor(const QString &tzid);
};

} // namespace OCC::Nourax::Agenda
