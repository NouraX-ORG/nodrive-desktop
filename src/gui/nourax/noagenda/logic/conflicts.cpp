/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "conflicts.h"

#include "../ocs/ocsjson.h"

#include <algorithm>

namespace OCC::Nourax::Agenda {

namespace {

constexpr int kMaxSpanDays = 800;

} // namespace

DayModel::DayModel(const QList<AgendaItem> &items, const WorkingHours &hours, const QSet<QDate> &holidays, const QTimeZone &zone)
    : _hours(hours)
    , _zone(zone.isValid() ? zone : QTimeZone(QTimeZone::UTC))
    , _holidays(holidays)
{
    for (const auto &item : items) {
        if (item.kind != ItemKind::Event || !item.allDay || !blocksMyTime(item) || !item.startDate.isValid()) {
            continue;
        }
        const QDate last = item.endDate.isValid() ? item.endDate : item.startDate;
        for (QDate d = item.startDate; d <= last && d <= item.startDate.addDays(kMaxSpanDays); d = d.addDays(1)) {
            _busyAllDay.insert(d);
        }
    }
}

bool DayModel::blocksMyTime(const AgendaItem &item)
{
    return item.kind == ItemKind::Event && item.workKind == WorkKind::Self && !item.transparent && !item.info && !item.problemOnly && !item.declined;
}

bool DayModel::isWorkingDay(const QDate &day) const
{
    return _hours.isWorkingDay(day) && !_holidays.contains(day) && !_busyAllDay.contains(day);
}

int DayModel::workingDaysBetween(const QDate &first, const QDate &last) const
{
    if (!first.isValid() || !last.isValid() || last < first) {
        return 0;
    }
    const auto key = qMakePair(first, last);
    const auto found = _cache.constFind(key);
    if (found != _cache.constEnd()) {
        return found.value();
    }
    int n = 0;
    const QDate end = qMin(last, first.addDays(kMaxSpanDays));
    for (QDate d = first; d <= end; d = d.addDays(1)) {
        if (isWorkingDay(d)) {
            ++n;
        }
    }
    _cache.insert(key, n);
    return n;
}

namespace Conflicts {

QSet<QDate> holidaysFor(const GanttSnapshot &gantt, const QString &me)
{
    int calendarId = 0;
    for (const auto &r : gantt.resources) {
        const auto resource = r.toObject();
        if (resource.value(QLatin1String("type")).toString() == QLatin1String("person")
            && resource.value(QLatin1String("uid")).toString().compare(me, Qt::CaseInsensitive) == 0 && !me.isEmpty()) {
            calendarId = intOf(resource.value(QLatin1String("calendar_id")));
            break;
        }
    }
    QSet<QDate> out;
    for (const auto &h : gantt.holidays) {
        const auto holiday = h.toObject();
        if (intOf(holiday.value(QLatin1String("calendar_id"))) != calendarId) {
            continue;
        }
        const auto day = QDate::fromString(holiday.value(QLatin1String("holiday_date")).toString().left(10), Qt::ISODate);
        if (day.isValid()) {
            out.insert(day);
        }
    }
    return out;
}

void annotate(QList<AgendaItem> *items, const DayModel &model)
{
    // Reset (the rows are rebuilt from the data on every refresh, but an item can be annotated twice in tests).
    for (auto &item : *items) {
        item.clash = false;
        item.softClash = false;
        item.clashWith.clear();
        item.deadlineDayOff = false;
    }

    // ---- clash: two timed items of mine that overlap ----------------------------------------------------------
    struct Slot
    {
        int index = 0;
        QDateTime start;
        QDateTime end;
        bool tentative = false;
    };
    QList<Slot> slots;
    for (int i = 0; i < items->size(); ++i) {
        const auto &item = items->at(i);
        if (!DayModel::blocksMyTime(item) || !item.timed || !item.start.isValid()) {
            continue;
        }
        Slot s;
        s.index = i;
        s.start = item.start;
        s.end = item.end.isValid() && item.end > item.start ? item.end : item.start.addSecs(60);
        s.tentative = item.tentative;
        slots.append(s);
    }
    std::sort(slots.begin(), slots.end(), [](const Slot &a, const Slot &b) { return a.start < b.start; });
    QHash<int, bool> firm;
    QHash<int, bool> soft;
    for (int i = 0; i < slots.size(); ++i) {
        for (int j = i + 1; j < slots.size() && slots.at(j).start < slots.at(i).end; ++j) {
            const bool isSoft = slots.at(i).tentative || slots.at(j).tentative;
            for (const int k : {i, j}) {
                const int index = slots.at(k).index;
                (isSoft ? soft : firm).insert(index, true);
            }
            auto &a = (*items)[slots.at(i).index];
            auto &b = (*items)[slots.at(j).index];
            a.clashWith.append(b.title);
            b.clashWith.append(a.title);
        }
    }
    for (auto it = firm.constBegin(); it != firm.constEnd(); ++it) {
        (*items)[it.key()].clash = true;
    }
    for (auto it = soft.constBegin(); it != soft.constEnd(); ++it) {
        if (!firm.contains(it.key())) {
            (*items)[it.key()].softClash = true;
        }
    }

    // ---- a due or end date on a day off ----------------------------------------------------------------------------
    for (auto &item : *items) {
        if (item.workKind != WorkKind::Self || item.problemOnly || item.done || item.info || !item.due.isValid()) {
            continue;
        }
        if (item.kind != ItemKind::Todo && item.kind != ItemKind::GanttTask && item.kind != ItemKind::Card) {
            continue;
        }
        if (model.isDayOff(item.due)) {
            item.deadlineDayOff = true;
        }
    }
}

} // namespace Conflicts

} // namespace OCC::Nourax::Agenda
