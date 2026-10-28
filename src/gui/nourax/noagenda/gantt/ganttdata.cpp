/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ganttdata.h"

namespace OCC::Nourax::Agenda {

namespace {

QString stamp(const QDateTime &dt)
{
    return dt.isValid() ? dt.toUTC().toString(Qt::ISODate) : QString();
}

QDateTime parseStamp(const QString &s)
{
    return s.isEmpty() ? QDateTime() : QDateTime::fromString(s, Qt::ISODate).toUTC();
}

} // namespace

QJsonObject GanttSnapshot::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("unavailable"), unavailable);
    o.insert(QStringLiteral("contractOk"), contractOk);
    o.insert(QStringLiteral("contractProblem"), contractProblem);
    o.insert(QStringLiteral("fetchedAt"), stamp(fetchedAt));
    o.insert(QStringLiteral("slowFetchedAt"), stamp(slowFetchedAt));
    o.insert(QStringLiteral("hasLinked"), hasLinkedResource);
    o.insert(QStringLiteral("myTasks"), myTasks);
    o.insert(QStringLiteral("projects"), projects);
    o.insert(QStringLiteral("resources"), resources);
    o.insert(QStringLiteral("workSchedules"), workSchedules);
    o.insert(QStringLiteral("holidays"), holidays);
    o.insert(QStringLiteral("workCalendars"), workCalendars);
    o.insert(QStringLiteral("settings"), settings);
    o.insert(QStringLiteral("taskSync"), taskSync);
    o.insert(QStringLiteral("capacity"), capacity);
    QJsonObject data;
    for (auto it = projectData.constBegin(); it != projectData.constEnd(); ++it) {
        QJsonObject p;
        p.insert(QStringLiteral("tasks"), it->tasks);
        p.insert(QStringLiteral("milestones"), it->milestones);
        p.insert(QStringLiteral("subprojects"), it->subprojects);
        p.insert(QStringLiteral("shares"), it->shares);
        p.insert(QStringLiteral("sharesKnown"), it->sharesKnown);
        p.insert(QStringLiteral("sharesFetchedAt"), stamp(it->sharesFetchedAt));
        p.insert(QStringLiteral("loaded"), it->loaded);
        data.insert(QString::number(it.key()), p);
    }
    o.insert(QStringLiteral("projectData"), data);
    return o;
}

GanttSnapshot GanttSnapshot::fromJson(const QJsonObject &o)
{
    GanttSnapshot s;
    s.unavailable = o.value(QLatin1String("unavailable")).toBool();
    s.contractOk = o.value(QLatin1String("contractOk")).toBool(true);
    s.contractProblem = o.value(QLatin1String("contractProblem")).toString();
    s.fetchedAt = parseStamp(o.value(QLatin1String("fetchedAt")).toString());
    s.slowFetchedAt = parseStamp(o.value(QLatin1String("slowFetchedAt")).toString());
    s.hasLinkedResource = o.value(QLatin1String("hasLinked")).toBool(true);
    s.myTasks = o.value(QLatin1String("myTasks")).toArray();
    s.projects = o.value(QLatin1String("projects")).toArray();
    s.resources = o.value(QLatin1String("resources")).toArray();
    s.workSchedules = o.value(QLatin1String("workSchedules")).toArray();
    s.holidays = o.value(QLatin1String("holidays")).toArray();
    s.workCalendars = o.value(QLatin1String("workCalendars")).toArray();
    s.settings = o.value(QLatin1String("settings")).toObject();
    s.taskSync = o.value(QLatin1String("taskSync")).toObject();
    s.capacity = o.value(QLatin1String("capacity")).toObject();
    const auto data = o.value(QLatin1String("projectData")).toObject();
    for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
        const auto p = it.value().toObject();
        GanttProjectData d;
        d.tasks = p.value(QLatin1String("tasks")).toArray();
        d.milestones = p.value(QLatin1String("milestones")).toArray();
        d.subprojects = p.value(QLatin1String("subprojects")).toArray();
        d.shares = p.value(QLatin1String("shares")).toArray();
        d.sharesKnown = p.value(QLatin1String("sharesKnown")).toBool();
        d.sharesFetchedAt = parseStamp(p.value(QLatin1String("sharesFetchedAt")).toString());
        d.loaded = p.value(QLatin1String("loaded")).toBool();
        s.projectData.insert(it.key().toInt(), d);
    }
    s.loaded = s.fetchedAt.isValid();
    return s;
}

} // namespace OCC::Nourax::Agenda
