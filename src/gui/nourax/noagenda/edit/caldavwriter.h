/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: writes events and to-dos through CalDAV with ETags (plans/noagenda-desktop-plan.md, 2.2, 2.4).
//   change:  GET (with the ETag) -> compare with the version the person saw -> patch the text -> PUT If-Match
//   create:  PUT <calendar>/<uuid>.ics with If-None-Match: *
//   delete:  DELETE with If-Match (the server keeps the object in the calendar's trash bin)
// A 412, or a different ETag, is an edit conflict: nothing is written.

#pragma once

#include "../ical/icalpatch.h"
#include "../net/httptransport.h"
#include "editcommand.h"

namespace OCC::Nourax::Agenda {

class CalDavWriter
{
public:
    static void run(HttpTransport *transport, const EditCommand &command, const EditContext &context, const EditDone &done);

    /// The semantic change of a command as an edit of the iCalendar text.
    static ICalEdit editFrom(const QJsonObject &change, const QDateTime &now);
    static QString objectPath(const QString &davUser, const QString &calendarUri, const QString &objectUri);

    /// Value of a date field of a command: "2026-11-04" (a date), "2026-11-04T09:00:00Z" (a UTC date-time), "" (remove).
    static ICalDateValue dateValue(const QJsonValue &value);

private:
    static void patch(HttpTransport *transport, const EditCommand &command, const EditContext &context, const EditDone &done);
    static void create(HttpTransport *transport, const EditCommand &command, const EditContext &context, const EditDone &done);
    static void remove(HttpTransport *transport, const EditCommand &command, const EditContext &context, const EditDone &done);
};

} // namespace OCC::Nourax::Agenda
