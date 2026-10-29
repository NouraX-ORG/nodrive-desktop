/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "editability.h"

namespace OCC::Nourax::Agenda::EditRules {

QStringList ganttStatuses()
{
    return {QStringLiteral("not_started"), QStringLiteral("in_progress"), QStringLiteral("blocked"), QStringLiteral("done")};
}

QString statusText(const QString &status)
{
    if (status == QLatin1String("not_started")) {
        return AgendaText::tr("Not started");
    }
    if (status == QLatin1String("in_progress")) {
        return AgendaText::tr("In progress");
    }
    if (status == QLatin1String("blocked")) {
        return AgendaText::tr("Blocked");
    }
    if (status == QLatin1String("done")) {
        return AgendaText::tr("Done");
    }
    return status;
}

Editability of(const AgendaItem &item)
{
    Editability e;
    switch (item.home) {
    case Home::Calendar:
        e.openLabel = AgendaText::tr("Open in NoCalendar");
        break;
    case Home::Tasks:
        e.openLabel = AgendaText::tr("Open in NoTasks");
        break;
    case Home::Gantt:
        e.openLabel = AgendaText::tr("Open in NoGantt");
        break;
    case Home::Board:
        e.openLabel = AgendaText::tr("Open in NoBoard");
        break;
    }

    auto lock = [&e](const QString &reason) {
        e.locked = true;
        e.reason = reason;
        e.agentReason = reason;
        e.progressReason = reason;
    };

    if (item.problemOnly || item.agentLoad) {
        lock(AgendaText::tr("This row only reports a problem."));
        return e;
    }
    if (item.versionReadOnly) {
        lock(AgendaText::tr("Read only: this version of the app on the server has not been checked yet."));
        return e;
    }
    if (item.workKind == WorkKind::Team) {
        lock(AgendaText::tr("This is work of your team. Change it in %1.").arg(item.home == Home::Board ? AgendaText::tr("NoBoard") : AgendaText::tr("NoGantt")));
        return e;
    }

    switch (item.home) {
    case Home::Calendar:
    case Home::Tasks: {
        const bool todo = item.kind == ItemKind::Todo;
        if (item.repeating) {
            e.repeats = true;
            e.locked = true;
            e.reason = todo ? AgendaText::tr("This repeats. Change it in NoTasks.") : AgendaText::tr("This repeats. Change it in NoCalendar.");
            e.agentReason = e.reason;
            return e;
        }
        if (item.readOnly) {
            lock(item.readOnlyReason.isEmpty() ? AgendaText::tr("This calendar is read only for you.") : item.readOnlyReason);
            return e;
        }
        if (item.info) {
            lock(AgendaText::tr("This is for information and can't be changed."));
            return e;
        }
        if (item.invitation) {
            e.locked = true;
            e.reason = AgendaText::tr("You were invited. Only the organiser can change this event.");
            e.agentReason = AgendaText::tr("You were invited, so this is always Self.");
            return e;
        }
        if (item.teamCalendar && item.kind == ItemKind::Event && !item.organizerIsMe) {
            // Plan 2.4: an event of a shared team calendar can be changed only by its organiser.
            e.locked = true;
            e.reason = AgendaText::tr("Only the organiser can change this event.");
            e.agentReason = e.reason;
            return e;
        }
        e.canEdit = true;
        e.canDelete = true;
        e.canDone = todo;
        e.canChangeAgent = true;
        return e;
    }
    case Home::Gantt: {
        if (item.kind != ItemKind::GanttTask) {
            lock(AgendaText::tr("Milestones are changed in NoGantt."));
            return e;
        }
        if (item.readOnly) {
            lock(AgendaText::tr("You can view this project only. Ask the project manager for editor rights."));
            return e;
        }
        e.canEdit = true;
        e.canDone = true;
        e.canStatus = true;
        e.canChangeAgent = true;
        if (item.progressMode == QLatin1String("deck")) {
            e.canProgress = false;
            e.progressReason = AgendaText::tr("% done comes from NoBoard");
        } else {
            e.canProgress = true;
        }
        return e;
    }
    case Home::Board: {
        if (item.readOnly) {
            lock(AgendaText::tr("You can see this board but not change it."));
            return e;
        }
        e.canEdit = true; // the due date only
        e.canDone = true;
        e.canChangeAgent = true;
        return e;
    }
    }
    return e;
}

} // namespace OCC::Nourax::Agenda::EditRules
