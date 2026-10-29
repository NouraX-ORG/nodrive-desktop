/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "workload.h"

#include <algorithm>

namespace OCC::Nourax::Agenda {

QString DayLoad::text() const
{
    return AgendaText::tr("%1 h planned / %2 h free").arg(Workload::hoursText(planned), Workload::hoursText(qMax(0.0, free)));
}

namespace Workload {

QString hoursText(double hours)
{
    const double rounded = qRound(hours * 10) / 10.0;
    if (qAbs(rounded - qRound(rounded)) < 0.05) {
        return QString::number(static_cast<int>(qRound(rounded)));
    }
    return QString::number(rounded, 'f', 1);
}

DayLoad forDay(const QList<AgendaItem> &items, const DayModel &model, const QDate &day)
{
    DayLoad load;
    load.day = day;
    load.workingDay = model.isWorkingDay(day);
    if (!load.workingDay) {
        return load; // a day off: no working hours, nothing planned on it
    }
    load.working = model.hours().hoursPerDay();

    // Busy: the busy events of mine inside the working hours of this day.
    const auto zone = model.zone();
    const QDateTime from(day, model.hours().from, zone);
    const QDateTime to(day, model.hours().to, zone);
    QList<QPair<QDateTime, QDateTime>> spans;
    for (const auto &item : items) {
        if (!DayModel::blocksMyTime(item) || item.kind == ItemKind::GanttTask) {
            continue; // a task with real times is already counted by its hours below
        }
        QDateTime start;
        QDateTime end;
        if (item.kind == ItemKind::Event) {
            if (!item.timed || !item.start.isValid()) {
                continue;
            }
            start = item.start;
            end = item.end.isValid() && item.end > item.start ? item.end : item.start;
        } else if (!DayModel::timeSlot(item, &start, &end)) {
            continue;
        }
        const QDateTime a = qMax(start, from);
        const QDateTime b = qMin(end, to);
        if (a < b) {
            spans.append({a, b});
        }
    }
    std::sort(spans.begin(), spans.end(), [](const auto &x, const auto &y) { return x.first < y.first; });
    qint64 busySeconds = 0;
    QDateTime cursor;
    for (const auto &s : std::as_const(spans)) {
        const QDateTime a = cursor.isValid() ? qMax(s.first, cursor) : s.first;
        if (a < s.second) {
            busySeconds += a.secsTo(s.second);
            cursor = s.second;
        }
    }
    load.busy = busySeconds / 3600.0;
    load.free = qMax(0.0, load.working - load.busy);

    // Planned: my own share of work on the NoGantt tasks that are active today. On a task my agent does, my own share
    // (myWorkHours) is my review or supervision time and counts; the agent's hours (agentWorkHours) never do.
    for (const auto &item : items) {
        if (item.kind != ItemKind::GanttTask || (item.workKind != WorkKind::Self && item.workKind != WorkKind::MyAgent) || item.problemOnly || item.agentLoad || item.done) {
            continue;
        }
        const QDate first = item.startDate.isValid() ? item.startDate : item.endDate;
        const QDate last = item.endDate.isValid() ? item.endDate : item.startDate;
        if (!first.isValid() || !last.isValid() || day < first || day > last) {
            continue;
        }
        if (item.myWorkHours < 0) {
            if (item.workKind == WorkKind::Self) {
                ++load.noEstimate; // an agent's task without time for my review is normal: nothing to warn about
            }
            continue;
        }
        const int days = model.workingDaysBetween(first, last);
        if (days > 0) {
            load.planned += item.myWorkHours / days;
        }
    }
    load.overbooked = load.planned > load.free + 1e-6;
    return load;
}

} // namespace Workload

} // namespace OCC::Nourax::Agenda
