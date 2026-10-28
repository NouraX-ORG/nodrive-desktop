/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: reads the calendars, events and to-dos of the signed-in user (CalDAV).
//
// Request plan (plans/noagenda-desktop-plan.md, 1.5): PROPFIND of the principal and of the calendar
// home, then, per calendar whose sync token or ctag changed, one calendar-query for the events of
// the sync window (expanded by the server, answers in UTC) and one for the to-dos that are not
// completed (never expanded: the server drops every VTODO on expand).

#pragma once

#include "../net/httptransport.h"
#include "caldavxml.h"
#include "calendardata.h"

#include <QDate>
#include <QTimeZone>

#include <functional>

namespace OCC::Nourax::Agenda {

struct CalendarReadOptions
{
    QString davUser;
    QTimeZone zone; ///< for floating times
    QDate from; ///< sync window
    QDate to;
    /// Whether the contents of this calendar are needed (not for hidden calendars).
    std::function<bool(const CalendarInfo &)> wanted;
    bool forceFull = false;
    QDateTime now; ///< for fetchedAt
};

struct CalendarReadResult
{
    bool ok = false; ///< the principal and the calendar list were read
    int status = 0; ///< HTTP status of the first failure (0 = none)
    bool networkError = false;
    CalendarSnapshot snapshot;
};

class CalendarReader
{
public:
    static void read(HttpTransport *transport, const CalendarReadOptions &options, const CalendarSnapshot &previous,
        const std::function<void(const CalendarReadResult &)> &done);

    /// Reads one calendar object (an .ics body) into records. `forTodos` selects VTODO, otherwise VEVENT.
    static QList<CalRecord> parseObject(const QString &calendarUri, const QString &href, const QString &etag, const QString &ics, bool forTodos, const QTimeZone &floatingZone);

    /// Classifies one answer of the calendar-home PROPFIND. `davUser` is the signed-in user.
    static CalendarInfo classify(const DavResponse &response, const QString &davUser);

    /// Window of the sync: first day of the previous month to 12 months ahead.
    static void syncWindow(const QDate &today, QDate *from, QDate *to);

    static QList<QPair<QString, QString>> principalProps();
    static QList<QPair<QString, QString>> calendarProps();
};

} // namespace OCC::Nourax::Agenda
