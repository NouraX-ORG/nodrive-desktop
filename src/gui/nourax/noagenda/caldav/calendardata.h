/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: what is read from CalDAV (calendars, events and to-dos), and its offline-cache form.

#pragma once

#include <QDate>
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace OCC::Nourax::Agenda {

struct CalendarInfo
{
    QString href; ///< server path of the calendar collection
    QString uri; ///< last path segment
    QString name;
    QString color;
    QString ownerPrincipal;
    QString ownerName;
    bool mine = false;
    bool readOnly = false;
    bool subscribed = false;
    bool birthdays = false;
    bool deckBoard = false; ///< calendar made by the board app
    bool ignored = false; ///< inbox, outbox, trash bin
    bool hasEvents = false;
    bool hasTodos = false;
    bool transparent = false; ///< the whole calendar does not block time
    QString syncToken;
    QString ctag;

    [[nodiscard]] QJsonObject toJson() const;
    static CalendarInfo fromJson(const QJsonObject &o);
};

struct CalAttendee
{
    QString address; ///< lower case, "mailto:" removed
    QString partstat; ///< ACCEPTED, DECLINED, TENTATIVE, NEEDS-ACTION, ...
};

struct CalRecord
{
    QString calendarUri;
    QString href;
    QString etag;
    bool isTodo = false;
    QString uid;
    QString summary;
    QString description; ///< first 2,000 characters
    bool descriptionCut = false; ///< the description was longer than that: it can't be changed here
    QString location;
    bool allDay = false;
    bool timeApproximated = false;
    QDateTime start; ///< UTC
    QDateTime end; ///< UTC
    QDate startDate; ///< all-day: first day
    QDate endDate; ///< all-day: last day (inclusive)
    QDateTime dueTime; ///< to-do with a due time (UTC)
    QDate dueDate; ///< to-do: due day
    bool dueIsDate = false;
    QString transp; ///< OPAQUE | TRANSPARENT
    QString status; ///< CONFIRMED | TENTATIVE | CANCELLED | NEEDS-ACTION | COMPLETED | IN-PROCESS
    int percent = -1;
    bool completed = false;
    QString recurrenceId;
    bool repeating = false;
    QString organizer; ///< lower case address
    QList<CalAttendee> attendees;
    QString agentId; ///< X-NOURAX-AGENT-ID
    QStringList categories;

    [[nodiscard]] QJsonObject toJson() const;
    static CalRecord fromJson(const QJsonObject &o);
};

struct CalendarSnapshot
{
    QDateTime fetchedAt;
    QString principalHref;
    QString displayName;
    QStringList addresses; ///< calendar-user-address-set, lower case, "mailto:" removed
    QString defaultCalendarUri;
    QList<CalendarInfo> calendars;
    QList<CalRecord> records;
    QStringList failedCalendars; ///< calendars whose read failed in this refresh (their old records are kept)
    QDate windowFrom;
    QDate windowTo;
    bool loaded = false;

    [[nodiscard]] QJsonObject toJson() const;
    static CalendarSnapshot fromJson(const QJsonObject &o);
    [[nodiscard]] const CalendarInfo *calendar(const QString &uri) const;
};

} // namespace OCC::Nourax::Agenda
