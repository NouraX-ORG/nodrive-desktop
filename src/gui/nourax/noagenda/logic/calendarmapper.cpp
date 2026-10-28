/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "calendarmapper.h"

#include "../model/agendacolors.h"

#include <QMap>
#include <QUrl>

namespace OCC::Nourax::Agenda::CalendarMapper {

namespace {

QString objectName(const QString &href)
{
    const auto parts = href.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    return parts.isEmpty() ? QString() : QUrl::fromPercentEncoding(parts.last().toUtf8());
}

} // namespace

AgentRef agentOf(const BuildContext &ctx, const CalRecord &record, bool invitation, QList<ProblemFlag> *problems)
{
    if (invitation) {
        return AgentRegistry::selfRef();
    }
    if (!record.agentId.isEmpty()) {
        if (const auto *agent = ctx.registry.byId(record.agentId)) {
            if (!ctx.isMe(agent->person)) {
                // An agent of someone else on my own item is never guessed to be mine.
                auto ref = AgentRegistry::unknownRef(record.agentId);
                problems->append(makeFlag(ProblemFlag::Kind::UnknownAgent, ProblemFlag::Audience::Person, FlagText::unknownAgent(record.agentId)));
                return ref;
            }
            if (agent->isRetired()) {
                problems->append(makeFlag(ProblemFlag::Kind::RetiredAgent, ProblemFlag::Audience::Person, FlagText::retiredAgent(agent->name)));
            }
            return AgentRegistry::refFor(*agent);
        }
        problems->append(makeFlag(ProblemFlag::Kind::UnknownAgent, ProblemFlag::Audience::Person, FlagText::unknownAgent(record.agentId)));
        return AgentRegistry::unknownRef(record.agentId);
    }
    for (const auto &category : record.categories) {
        if (const auto *agent = ctx.registry.byTag(category)) {
            if (ctx.isMe(agent->person)) {
                if (agent->isRetired()) {
                    problems->append(makeFlag(ProblemFlag::Kind::RetiredAgent, ProblemFlag::Audience::Person, FlagText::retiredAgent(agent->name)));
                }
                return AgentRegistry::refFor(*agent);
            }
        }
    }
    return AgentRegistry::selfRef();
}

void map(const BuildContext &ctx, const CalendarSnapshot &snapshot, QList<AgendaItem> *out)
{
    // The same UID + recurrence in several visible calendars: remember which one wins.
    QMap<QString, int> chosen; // uid|rid -> index in `rows`
    QList<AgendaItem> rows;
    QList<bool> fromOwnCalendar;

    for (const auto &rec : snapshot.records) {
        const auto *cal = snapshot.calendar(rec.calendarUri);
        if (!cal) {
            continue;
        }
        const auto mode = ctx.settings.calendarMode(*cal, ctx.taskCopyCalendarUri);
        if (mode == CalendarMode::Hidden) {
            continue;
        }
        if (rec.status == QLatin1String("CANCELLED")) {
            continue;
        }

        AgendaItem item;
        item.home = Home::Calendar;
        item.kind = rec.isTodo ? ItemKind::Todo : ItemKind::Event;
        if (rec.isTodo) {
            item.home = Home::Tasks;
        }
        item.title = rec.summary.isEmpty() ? AgendaText::tr("(no title)") : rec.summary;
        item.description = rec.description;
        item.location = rec.location;
        item.sourceKey = QStringLiteral("cal:") + cal->uri;
        item.sourceName = cal->name;
        item.sourceColor = cal->color.isEmpty() ? cobaltColor() : cal->color;
        item.category = mode == CalendarMode::Personal ? Category::Personal : (mode == CalendarMode::Info ? Category::Info : Category::Work);
        item.info = mode == CalendarMode::Info;
        item.calendarUri = cal->uri;
        item.objectUri = objectName(rec.href);
        item.uid = rec.uid;
        item.recurrenceId = rec.recurrenceId;
        item.version = rec.etag;
        item.repeating = rec.repeating;
        item.timeApproximated = rec.timeApproximated;
        item.readOnly = cal->readOnly;
        if (cal->readOnly) {
            item.readOnlyReason = cal->subscribed ? AgendaText::tr("This is a subscription and can't be changed.") : AgendaText::tr("This calendar is read only for you.");
        }
        item.key = QStringLiteral("cal:") + cal->uri + QLatin1Char('/') + item.objectUri;
        if (!rec.recurrenceId.isEmpty()) {
            item.key += QLatin1Char('#') + rec.recurrenceId;
        }

        // People
        bool amAttendee = false;
        for (const auto &a : rec.attendees) {
            if (ctx.myAddresses.contains(a.address)) {
                amAttendee = true;
                item.myPartstat = a.partstat;
            }
        }
        item.organizerIsMe = !rec.organizer.isEmpty() && ctx.myAddresses.contains(rec.organizer);
        item.invitation = !rec.isTodo && !rec.organizer.isEmpty() && !item.organizerIsMe && amAttendee;

        if (!rec.isTodo) {
            if (item.myPartstat == QLatin1String("DECLINED")) {
                continue; // declined invitations are hidden
            }
            if (mode == CalendarMode::Team && !(item.organizerIsMe || amAttendee)) {
                continue; // a shared team calendar: only what I organise or attend
            }
            item.transparent = rec.transp == QLatin1String("TRANSPARENT") || cal->transparent || item.info;
            item.tentative = rec.status == QLatin1String("TENTATIVE") || item.myPartstat == QLatin1String("TENTATIVE");
            if (rec.allDay) {
                item.allDay = true;
                item.startDate = rec.startDate;
                item.endDate = rec.endDate.isValid() ? rec.endDate : rec.startDate;
            } else {
                item.timed = true;
                item.start = rec.start;
                item.end = rec.end.isValid() ? rec.end : rec.start;
            }
        } else {
            if (rec.completed || rec.status == QLatin1String("COMPLETED")) {
                continue;
            }
            if (mode == CalendarMode::Team && !(amAttendee || item.organizerIsMe) && !rec.organizer.isEmpty()) {
                continue;
            }
            item.percentDone = rec.percent;
            item.status = rec.status;
            if (rec.dueIsDate) {
                item.due = rec.dueDate;
            } else if (rec.dueTime.isValid()) {
                item.due = rec.dueTime.toTimeZone(ctx.zone).date();
                item.end = rec.dueTime;
            }
            if (rec.start.isValid()) {
                item.startDate = rec.start.toTimeZone(ctx.zone).date();
            } else if (rec.startDate.isValid()) {
                item.startDate = rec.startDate;
            }
            item.endDate = item.due;
            if (item.due.isValid()) {
                item.late = item.due < ctx.today;
                item.dueToday = item.due == ctx.today;
                if (item.late) {
                    item.lateDays = static_cast<int>(item.due.daysTo(ctx.today));
                }
            }
        }

        item.agent = agentOf(ctx, rec, item.invitation, &item.problems);
        item.workKind = item.agent.isSelf ? WorkKind::Self : WorkKind::MyAgent;

        // Dedupe
        const QString dedupeKey = (rec.uid.isEmpty() ? item.key : rec.uid) + QLatin1Char('|') + rec.recurrenceId + QLatin1Char('|') + (rec.isTodo ? QLatin1String("t") : QLatin1String("e"));
        const bool own = cal->mine;
        const auto found = chosen.constFind(dedupeKey);
        if (found != chosen.constEnd() && !rec.uid.isEmpty()) {
            const int index = found.value();
            if (own && !fromOwnCalendar.at(index)) {
                rows[index] = item;
                fromOwnCalendar[index] = true;
            }
            continue;
        }
        chosen.insert(dedupeKey, rows.size());
        rows.append(item);
        fromOwnCalendar.append(own);
    }
    out->append(rows);
}

} // namespace OCC::Nourax::Agenda::CalendarMapper
