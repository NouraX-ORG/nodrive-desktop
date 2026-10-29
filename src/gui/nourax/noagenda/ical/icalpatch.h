/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: changes only the named properties of one VEVENT or VTODO in the raw iCalendar text and leaves
// every other line byte for byte (alarms, attendees, unknown X- properties, time zones). Also makes the text of
// a new event or to-do (plans/noagenda-desktop-plan.md, 2.2 and 2.4).

#pragma once

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QStringList>

#include <optional>

namespace OCC::Nourax::Agenda {

/// One date or date-time property of an edit. Keep leaves the property as it is.
struct ICalDateValue
{
    enum class Mode { Keep, Set, Remove };

    Mode mode = Mode::Keep;
    bool dateOnly = false;
    QDate date; ///< for a DATE value (all-day): the value as written in the file (an all-day DTEND is the day after the last day)
    QDateTime utc; ///< for a DATE-TIME value; written with the Z suffix

    static ICalDateValue keep() { return {}; }
    static ICalDateValue remove()
    {
        ICalDateValue v;
        v.mode = Mode::Remove;
        return v;
    }
    static ICalDateValue ofDate(const QDate &day)
    {
        ICalDateValue v;
        v.mode = Mode::Set;
        v.dateOnly = true;
        v.date = day;
        return v;
    }
    static ICalDateValue ofUtc(const QDateTime &time)
    {
        ICalDateValue v;
        v.mode = Mode::Set;
        v.utc = time.toUTC();
        return v;
    }
};

/// The change itself. Only what is set is touched.
struct ICalEdit
{
    bool isTodo = false;
    std::optional<QString> summary;
    std::optional<QString> description;
    ICalDateValue start; ///< DTSTART
    ICalDateValue end; ///< DTEND (events)
    ICalDateValue due; ///< DUE (to-dos)
    std::optional<bool> done; ///< to-dos: done or not done
    bool changeAgent = false;
    QString agentId; ///< the agent id for X-NOURAX-AGENT-ID; empty = Self (the property and the tags are removed)
    QString addCategory; ///< the agent's calendar tag for CATEGORIES (empty = none)
    QStringList removeCategories; ///< calendar tags of every agent of the person: removed from CATEGORIES
    QDateTime now; ///< UTC: DTSTAMP, LAST-MODIFIED and COMPLETED
};

struct ICalPatchResult
{
    bool ok = false;
    QString error; ///< "repeats", "no-component", "several-components" or a short reason
    QString text;
};

class ICalPatch
{
public:
    /// The raw text of one object with the edit applied. Refuses (ok = false) a repeating item.
    static ICalPatchResult apply(const QString &text, const ICalEdit &edit);
    /// The text of a new event or to-do (PRODID -//Nourax//NoAgenda//EN, all times in UTC).
    static ICalPatchResult create(const QString &uid, const ICalEdit &edit);
    /// Whether the object has a repeat rule or a recurrence id.
    static bool isRepeating(const QString &text);

    static QString escapeText(const QString &value);
    /// Folds a logical line at 75 octets (never inside a UTF-8 character).
    static QString foldLine(const QString &logical, const QString &eol);
};

} // namespace OCC::Nourax::Agenda
