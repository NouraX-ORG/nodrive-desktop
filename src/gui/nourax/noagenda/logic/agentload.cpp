/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "agentload.h"

#include "agendacontext.h"

#include <QLocale>
#include <QMap>
#include <QSet>

namespace OCC::Nourax::Agenda::AgentLoad {

namespace {

QString dayKey(const QDate &day)
{
    static const char *const names[] = {"mon", "tue", "wed", "thu", "fri", "sat", "sun"};
    return QString::fromLatin1(names[(day.dayOfWeek() - 1) % 7]);
}

QSet<QString> patternDays(const Agent &agent)
{
    QSet<QString> days;
    for (const auto &w : agent.weekly) {
        for (const auto &d : w.days) {
            days.insert(d.trimmed().toLower().left(3));
        }
    }
    if (days.isEmpty()) {
        for (int i = 1; i <= 7; ++i) {
            days.insert(dayKey(QDate(2026, 11, i))); // no pattern: every day
        }
    }
    return days;
}

QTimeZone zoneOf(const Agent &agent, const QTimeZone &fallback)
{
    if (!agent.timezone.isEmpty()) {
        const QTimeZone zone(agent.timezone.toUtf8());
        if (zone.isValid()) {
            return zone;
        }
    }
    return fallback.isValid() ? fallback : QTimeZone(QTimeZone::UTC);
}

QString robot()
{
    return QString::fromUcs4(U"\U0001F916");
}

} // namespace

bool isAvailable(const Agent &agent, const QDate &day)
{
    if (!day.isValid() || !patternDays(agent).contains(dayKey(day))) {
        return false;
    }
    const QString iso = day.toString(Qt::ISODate);
    for (const auto &off : agent.daysOff) {
        if (off.left(10) == iso) {
            return false;
        }
    }
    return true;
}

double capacityOn(const Agent &agent, const QDate &day)
{
    if (!isAvailable(agent, day)) {
        return 0;
    }
    const int days = static_cast<int>(patternDays(agent).size());
    return days > 0 ? agent.capacityHoursPerWeek / days : 0;
}

double loadOn(const Agent &agent, const QList<AgendaItem> &items, const QDate &day, const QTimeZone &zone)
{
    const QTimeZone agentZone = zoneOf(agent, zone);
    const QDateTime dayStart(day, QTime(0, 0), agentZone);
    const QDateTime dayEnd = QDateTime(day.addDays(1), QTime(0, 0), agentZone);
    double load = 0;
    for (const auto &item : items) {
        if (item.agent.isSelf || item.agent.id != agent.id || item.problemOnly || item.agentLoad || item.done) {
            continue;
        }
        if (item.kind == ItemKind::Event) {
            if (!item.timed || !item.start.isValid()) {
                continue;
            }
            const QDateTime end = item.end.isValid() && item.end > item.start ? item.end : item.start;
            const QDateTime a = qMax(item.start, dayStart);
            const QDateTime b = qMin(end, dayEnd);
            if (a < b) {
                load += a.secsTo(b) / 3600.0;
            }
        } else if (item.kind == ItemKind::GanttTask && item.agentWorkHours >= 0) {
            const QDate first = item.startDate.isValid() ? item.startDate : item.endDate;
            const QDate last = item.endDate.isValid() ? item.endDate : item.startDate;
            if (!first.isValid() || !last.isValid() || day < first || day > last || !isAvailable(agent, day)) {
                continue;
            }
            int available = 0;
            for (QDate d = first; d <= last && d <= first.addDays(800); d = d.addDays(1)) {
                if (isAvailable(agent, d)) {
                    ++available;
                }
            }
            if (available > 0) {
                load += item.agentWorkHours / available;
            }
        }
    }
    return load;
}

QList<AgentOverbooking> compute(const AgentRegistry &registry, const QList<AgendaItem> &items, const QDate &from, const QDate &to,
    const QTimeZone &zone, const QString &me)
{
    QList<AgentOverbooking> out;
    QMap<QString, QString> personOf; // agent id -> the display name of its person (team rows)
    QSet<QString> ids;
    for (const auto &item : items) {
        if (item.agent.isSelf || item.agent.unknown || item.agent.otherOrganisation || item.agent.id.isEmpty() || item.problemOnly || item.agentLoad) {
            continue;
        }
        if (registry.byId(item.agent.id)) {
            ids.insert(item.agent.id);
            if (!item.personName.isEmpty()) {
                personOf.insert(item.agent.id, item.personName);
            }
        }
    }
    QStringList sorted(ids.begin(), ids.end());
    sorted.sort();
    for (const auto &id : std::as_const(sorted)) {
        const auto *agent = registry.byId(id);
        if (!agent) {
            continue;
        }
        for (QDate d = from; d <= to; d = d.addDays(1)) {
            const double load = loadOn(*agent, items, d, zone);
            const double capacity = capacityOn(*agent, d);
            if (load > capacity + 0.01) {
                AgentOverbooking o;
                o.agentId = id;
                o.agentName = agent->name;
                o.day = d;
                o.load = load;
                o.capacity = capacity;
                o.mine = !me.isEmpty() && agent->person.compare(me, Qt::CaseInsensitive) == 0;
                o.personName = personOf.value(id);
                out.append(o);
            }
        }
    }
    return out;
}

QList<AgendaItem> asItems(const QList<AgentOverbooking> &overbookings, const AgentRegistry &registry)
{
    QList<AgendaItem> out;
    for (const auto &o : overbookings) {
        const auto *agent = registry.byId(o.agentId);
        if (!agent) {
            continue;
        }
        AgendaItem item;
        item.key = QStringLiteral("agentload:") + o.agentId + QLatin1Char(':') + o.day.toString(Qt::ISODate);
        item.kind = ItemKind::GanttTask;
        item.home = Home::Gantt;
        item.title = AgendaText::tr("%1 is overbooked on %2").arg(robot() + QLatin1Char(' ') + o.agentName, QLocale().dayName(o.day.dayOfWeek(), QLocale::LongFormat));
        item.sourceName = AgendaText::tr("Agent capacity");
        item.category = Category::Work;
        item.problemOnly = true;
        item.agentLoad = true;
        item.agentLoadDay = o.day;
        item.workKind = o.mine ? WorkKind::MyAgent : WorkKind::Team;
        item.agent = AgentRegistry::refFor(*agent);
        item.personName = o.mine ? QString() : o.personName;
        item.readOnly = true;
        item.readOnlyReason = AgendaText::tr("This row only reports a problem.");
        item.problems.append(makeFlag(ProblemFlag::Kind::Overbooked, o.mine ? ProblemFlag::Audience::Person : ProblemFlag::Audience::Manager, item.title));
        out.append(item);
    }
    return out;
}

} // namespace OCC::Nourax::Agenda::AgentLoad
