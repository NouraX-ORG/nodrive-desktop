/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "agendabuilder.h"

#include "../model/agendacolors.h"
#include "../ocs/ocsjson.h"
#include "calendarmapper.h"
#include "deckmapper.h"
#include "ganttmapper.h"

#include <QJsonArray>
#include <QJsonObject>

namespace OCC::Nourax::Agenda {

WorkingHours AgendaBuilder::workingHours(const BuildInput &input)
{
    if (input.settings.workingHoursOverridden()) {
        return input.settings.workingHours();
    }
    WorkingHours hours; // Monday to Friday, 09:00 - 17:00
    const QString me = input.server.uid.isEmpty() ? input.davUser : input.server.uid;
    // The schedule of my linked planning person gives the working days; NoGantt has instance-wide hours.
    int scheduleId = 0;
    for (const auto &r : input.gantt.resources) {
        const auto resource = r.toObject();
        if (resource.value(QLatin1String("type")).toString() == QLatin1String("person")
            && resource.value(QLatin1String("uid")).toString().compare(me, Qt::CaseInsensitive) == 0) {
            scheduleId = intOf(resource.value(QLatin1String("schedule_id")));
            break;
        }
    }
    if (scheduleId > 0) {
        for (const auto &s : input.gantt.workSchedules) {
            const auto schedule = s.toObject();
            if (intOf(schedule.value(QLatin1String("id"))) == scheduleId) {
                hours.daysMask = intOf(schedule.value(QLatin1String("work_days")), 31);
            }
        }
    }
    const auto from = QTime::fromString(input.gantt.settings.value(QLatin1String("work_start")).toString().left(5), QStringLiteral("HH:mm"));
    const auto to = QTime::fromString(input.gantt.settings.value(QLatin1String("work_end")).toString().left(5), QStringLiteral("HH:mm"));
    if (from.isValid() && to.isValid() && from < to) {
        hours.from = from;
        hours.to = to;
    }
    return hours;
}

namespace {

QString modeTextForCalendar(CalendarMode mode)
{
    return toString(mode);
}

} // namespace

BuildOutput AgendaBuilder::build(const BuildInput &input)
{
    BuildOutput out;

    BuildContext ctx;
    ctx.me = input.server.uid.isEmpty() ? input.davUser : input.server.uid;
    ctx.myName = input.server.displayName.isEmpty() ? ctx.me : input.server.displayName;
    ctx.myAddresses = input.calendar.addresses;
    if (!input.accountEmail.isEmpty()) {
        ctx.myAddresses.append(input.accountEmail.toLower());
    }
    if (!input.server.email.isEmpty()) {
        ctx.myAddresses.append(input.server.email.toLower());
    }
    ctx.myGroups = input.server.groups;
    ctx.serverAdmin = input.server.isServerAdmin;
    ctx.registry = input.registry;
    ctx.settings = input.settings;
    ctx.today = input.today;
    ctx.zone = input.zone;
    ctx.now = input.now;
    const double hoursPerPt = input.gantt.settings.value(QLatin1String("hours_per_pt")).toDouble(8.0);
    ctx.hoursPerPt = hoursPerPt > 0 ? hoursPerPt : 8.0;
    ctx.taskCopyCalendarUri = GanttMapper::taskCopyCalendarUri(input.gantt);
    out.taskCopyCalendarUri = ctx.taskCopyCalendarUri;

    // ---- rows ----------------------------------------------------------------------------------------------------
    CalendarMapper::map(ctx, input.calendar, &out.items);
    GanttMapper::Result gantt;
    GanttMapper::map(ctx, input.gantt, &gantt);
    out.items.append(gantt.items);
    DeckMapper::map(ctx, input.deck, gantt.linkedCardIds, &out.items);

    out.ganttNotLinked = input.gantt.loaded && !input.gantt.unavailable && !input.gantt.hasLinkedResource;
    out.ganttUnavailable = input.gantt.loaded && input.gantt.unavailable;
    out.boardUnavailable = input.deck.loaded && input.deck.unavailable;
    out.ganttReadOnlyByGuard = input.gantt.loaded && !input.gantt.unavailable && !input.gantt.contractOk;
    out.ganttContractProblem = input.gantt.contractProblem;
    out.boardReadOnlyByGuard = input.deck.loaded && !input.deck.unavailable && !DeckClient::versionIsTested(input.deck.version);
    for (const auto *agent : ctx.registry.ofPerson(ctx.me)) {
        out.myAgents.append(*agent);
    }
    out.registryAgentsOfMine = out.myAgents.size();

    // ---- Sources screen --------------------------------------------------------------------------------------------
    for (const auto &cal : input.calendar.calendars) {
        if (cal.ignored) {
            continue;
        }
        SourceEntry e;
        e.type = SourceEntry::Type::Calendar;
        e.key = cal.uri;
        e.name = cal.name;
        e.color = cal.color.isEmpty() ? cobaltColor() : cal.color;
        e.autoHiddenReason = SourceSettings::autoHiddenReason(cal, ctx.taskCopyCalendarUri);
        e.autoHidden = !e.autoHiddenReason.isEmpty();
        e.defaultMode = modeTextForCalendar(SourceSettings::defaultCalendarMode(cal));
        e.mode = modeTextForCalendar(ctx.settings.calendarMode(cal, ctx.taskCopyCalendarUri));
        e.personalQuestion = cal.mine && !cal.subscribed && !cal.birthdays && !cal.deckBoard && !e.autoHidden;
        if (cal.subscribed) {
            e.note = AgendaText::tr("Subscription");
        } else if (cal.birthdays) {
            e.note = AgendaText::tr("Birthdays");
        } else if (!cal.mine) {
            e.note = cal.ownerName.isEmpty() ? AgendaText::tr("Shared with you") : AgendaText::tr("Shared by %1").arg(cal.ownerName);
        } else if (cal.readOnly) {
            e.note = AgendaText::tr("Read only");
        }
        out.sources.append(e);
        if (e.personalQuestion) {
            out.ownCalendars.append({cal.uri, cal.name});
        }
    }
    out.needsPersonalQuestion = input.calendar.loaded && !ctx.settings.personalAnswered() && !out.ownCalendars.isEmpty();

    if (input.gantt.loaded && !input.gantt.unavailable) {
        for (const auto &p : input.gantt.projects) {
            const auto project = p.toObject();
            const auto relation = GanttMapper::relationOf(ctx, input.gantt, project);
            SourceEntry e;
            e.type = SourceEntry::Type::Project;
            e.key = QString::number(relation.id);
            e.name = relation.title;
            e.color = sourceColor(relation.color);
            e.defaultMode = toString(relation.defaultMode());
            e.mode = toString(ctx.settings.projectMode(relation));
            if (relation.owned) {
                e.note = AgendaText::tr("You run this project");
            } else if (relation.seenOnlyAsServerAdmin) {
                e.note = AgendaText::tr("Seen only as a server admin");
            } else if (relation.runByRealShare) {
                e.note = AgendaText::tr("You run this project");
            } else if (!relation.ownerName.isEmpty()) {
                e.note = AgendaText::tr("Shared by %1").arg(relation.ownerName);
            }
            if (e.mode == toString(WorkMode::DoOversee)) {
                out.anyOversee = true;
            }
            out.sources.append(e);
        }
    }
    if (input.deck.loaded && !input.deck.unavailable) {
        for (const auto &b : input.deck.boards) {
            const auto board = b.toObject();
            const auto relation = DeckMapper::relationOf(ctx, board);
            SourceEntry e;
            e.type = SourceEntry::Type::Board;
            e.key = QString::number(relation.id);
            e.name = relation.title;
            e.color = sourceColor(QLatin1Char('#') + relation.color);
            e.defaultMode = toString(relation.defaultMode());
            e.mode = toString(ctx.settings.boardMode(relation));
            if (relation.owned || relation.canManage) {
                e.note = AgendaText::tr("You run this board");
            }
            if (e.mode == toString(WorkMode::DoOversee)) {
                out.anyOversee = true;
            }
            out.sources.append(e);
        }
    }
    return out;
}

} // namespace OCC::Nourax::Agenda
