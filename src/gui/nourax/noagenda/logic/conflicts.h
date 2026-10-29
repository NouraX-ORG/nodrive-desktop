/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: the conflict kinds of design 7, computed on the device from data the person can already see
// (plans/noagenda-desktop-plan.md, 2.7): clash and soft clash, deadline on a day off. ("Blocked" is already
// decided by the NoGantt mapper; "overbooked" is workload.h.)
// Items whose agent is not Self never take the person's time: they never clash with the person's own items
// and never make a day off.

#pragma once

#include "../gantt/ganttdata.h"
#include "../model/agendaitem.h"
#include "sourcesettings.h"

#include <QDate>
#include <QHash>
#include <QList>
#include <QSet>
#include <QTimeZone>

namespace OCC::Nourax::Agenda {

/// The days of the person: working pattern, holidays and all-day busy events.
class DayModel
{
public:
    DayModel() = default;
    DayModel(const QList<AgendaItem> &items, const WorkingHours &hours, const QSet<QDate> &holidays, const QTimeZone &zone);

    [[nodiscard]] const WorkingHours &hours() const { return _hours; }
    [[nodiscard]] const QTimeZone &zone() const { return _zone; }
    /// A day of the working pattern that is not a holiday and has no all-day busy event of mine.
    [[nodiscard]] bool isWorkingDay(const QDate &day) const;
    [[nodiscard]] bool isDayOff(const QDate &day) const { return !isWorkingDay(day); }
    /// The number of working days from `first` to `last`, both included (capped at two years).
    [[nodiscard]] int workingDaysBetween(const QDate &first, const QDate &last) const;

    /// An event, a timed card or a task with real times that blocks my time: mine (agent Self), not free, not
    /// information, not a problem row, not done.
    static bool blocksMyTime(const AgendaItem &item);
    /// The time an item takes: the start and end of a timed event, or the slot of a card or task with real times.
    /// An event without an end takes one minute. False when the item has no time.
    static bool timeSlot(const AgendaItem &item, QDateTime *start, QDateTime *end);

private:
    WorkingHours _hours;
    QTimeZone _zone = QTimeZone(QTimeZone::UTC);
    QSet<QDate> _holidays;
    QSet<QDate> _busyAllDay;
    mutable QHash<QPair<QDate, QDate>, int> _cache;
};

namespace Conflicts {

/// The NoGantt holidays that apply to the person: those of the calendar of their linked planning person.
QSet<QDate> holidaysFor(const GanttSnapshot &gantt, const QString &me);

/// Sets `clash`, `softClash`, `clashWith` and `deadlineDayOff` on the items.
void annotate(QList<AgendaItem> *items, const DayModel &model);

} // namespace Conflicts

} // namespace OCC::Nourax::Agenda
