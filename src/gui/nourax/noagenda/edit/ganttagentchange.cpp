/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ganttagentchange.h"

#include "../model/agendaitem.h"
#include "../ocs/ocsjson.h"

#include <QSet>
#include <QtMath>

namespace OCC::Nourax::Agenda {

GanttProjectInfo GanttProjectInfo::of(const QString &ownerId, const QJsonArray &tasks)
{
    GanttProjectInfo info;
    info.known = true;
    info.ownerId = ownerId;
    for (const auto &t : tasks) {
        for (const auto &row : t.toObject().value(QLatin1String("resources")).toArray()) {
            info.resourceIdsOnTasks.insert(intOf(row.toObject().value(QLatin1String("resource_id"))));
        }
    }
    return info;
}

} // namespace OCC::Nourax::Agenda

namespace OCC::Nourax::Agenda::GanttAgentChange {

namespace {

constexpr int kMaxRows = 4; // NoGantt: too_many_resources

QString robot()
{
    return QString::fromUcs4(U"\U0001F916");
}

QJsonObject resourceById(const QJsonArray &resources, int id)
{
    for (const auto &r : resources) {
        if (intOf(r.toObject().value(QLatin1String("id"))) == id) {
            return r.toObject();
        }
    }
    return QJsonObject();
}

QStringList myNamesOf(const QString &me, const QString &myName, const QJsonArray &resources)
{
    QStringList names;
    if (!myName.isEmpty()) {
        names.append(myName);
    }
    for (const auto &r : resources) {
        const auto o = r.toObject();
        if (o.value(QLatin1String("type")).toString() == QLatin1String("person") && o.value(QLatin1String("uid")).toString().compare(me, Qt::CaseInsensitive) == 0) {
            names.append(o.value(QLatin1String("uid_display_name")).toString());
            names.append(o.value(QLatin1String("name")).toString());
        }
    }
    names.removeAll(QString());
    names.removeDuplicates();
    return names;
}

bool isMyPlanningPerson(const QJsonObject &resource, const QString &me)
{
    return resource.value(QLatin1String("type")).toString() == QLatin1String("person")
        && resource.value(QLatin1String("uid")).toString().compare(me, Qt::CaseInsensitive) == 0 && !me.isEmpty();
}

double workInPt(const QJsonValue &work, const QJsonObject &resource, double hoursPerPt)
{
    const double w = numberOf(work, 0);
    return resource.value(QLatin1String("unit")).toString() == QLatin1String("h") ? w / (hoursPerPt > 0 ? hoursPerPt : 8.0) : w;
}

bool hasWork(const QJsonValue &work)
{
    return work.isDouble() || (work.isString() && !work.toString().isEmpty());
}

} // namespace

QList<int> resourcesFor(const Agent &agent, const QJsonArray &visibleResources, const QStringList &myNames)
{
    QList<int> out;
    for (const int id : agent.ganttResourceIds) {
        if (!resourceById(visibleResources, id).isEmpty() && !out.contains(id)) {
            out.append(id);
        }
    }
    for (const auto &r : visibleResources) {
        const auto o = r.toObject();
        if (o.value(QLatin1String("type")).toString() != QLatin1String("person") || !o.value(QLatin1String("uid")).toString().isEmpty()) {
            continue;
        }
        const QString name = AgentRegistry::normalize(o.value(QLatin1String("name")).toString());
        const int id = intOf(o.value(QLatin1String("id")));
        for (const auto &mine : myNames) {
            if (name == AgentRegistry::normalize(AgentRegistry::planningPersonName(agent.name, mine)) && !out.contains(id)) {
                out.append(id);
            }
        }
    }
    return out;
}

int resourceFor(const Agent &agent, const QJsonArray &visibleResources, const QStringList &myNames, const GanttProjectInfo &project)
{
    const auto ids = resourcesFor(agent, visibleResources, myNames);
    if (ids.isEmpty()) {
        return 0;
    }
    if (!project.known) {
        return ids.first();
    }
    for (const int id : ids) {
        if (project.resourceIdsOnTasks.contains(id)) {
            return id;
        }
    }
    for (const int id : ids) {
        if (!project.ownerId.isEmpty() && resourceById(visibleResources, id).value(QLatin1String("owner_id")).toString().compare(project.ownerId, Qt::CaseInsensitive) == 0) {
            return id;
        }
    }
    return 0;
}

int myResourceId(const QString &me, const QJsonArray &visibleResources)
{
    for (const auto &r : visibleResources) {
        const auto o = r.toObject();
        if (isMyPlanningPerson(o, me)) {
            return intOf(o.value(QLatin1String("id")));
        }
    }
    return 0;
}

QList<GanttAgentChoice> choices(const QString &me, const QString &myName, const AgentRegistry &registry, const QJsonArray &visibleResources,
    const GanttProjectInfo &project)
{
    QList<GanttAgentChoice> out;
    const auto names = myNamesOf(me, myName, visibleResources);
    for (const auto *agent : registry.ofPerson(me)) {
        GanttAgentChoice c;
        c.agentId = agent->id;
        c.name = agent->name;
        c.paused = agent->isPaused();
        c.retired = agent->isRetired();
        c.resourceIds = resourcesFor(*agent, visibleResources, names);
        c.resourceId = resourceFor(*agent, visibleResources, names, project);
        if (c.resourceId <= 0) {
            c.askManagerText = AgendaText::tr("Ask your project manager to add %1 to this project").arg(robot() + QLatin1Char(' ') + agent->name);
        }
        out.append(c);
    }
    return out;
}

GanttAgentPlan plan(const QJsonObject &freshTask, const QJsonArray &visibleResources, const AgentRegistry &registry, const QString &me,
    const QString &myName, const QString &chosenAgentId, double hoursPerPt, const GanttProjectInfo &project)
{
    GanttAgentPlan result;
    const auto rows = freshTask.value(QLatin1String("resources")).toArray();

    // Every current row must be visible to me: a row that is not visible is silently dropped by the server.
    bool mineOnTask = false;
    for (const auto &rowValue : rows) {
        const auto resource = resourceById(visibleResources, intOf(rowValue.toObject().value(QLatin1String("resource_id"))));
        if (resource.isEmpty()) {
            result.refusal = AgendaText::tr("Someone on this task is not visible to you, so changing the agent here could remove them. Open in NoGantt.");
            return result;
        }
        if (isMyPlanningPerson(resource, me)) {
            mineOnTask = true;
        }
    }
    if (!mineOnTask) {
        result.refusal = AgendaText::tr("You are not on this task, so you can't choose your agent for it.");
        return result;
    }

    const auto all = choices(me, myName, registry, visibleResources, project);
    // Every planning person that stands for one of my agents is mine (an agent can have one per manager pool, and
    // a placeholder with its exact name counts too), so no old row of an agent stays behind.
    QSet<int> myAgentResources;
    QSet<int> onTask;
    for (const auto &rowValue : rows) {
        onTask.insert(intOf(rowValue.toObject().value(QLatin1String("resource_id"))));
    }
    for (const auto &c : all) {
        for (const int id : c.resourceIds) {
            myAgentResources.insert(id);
        }
    }

    int chosenResource = 0;
    if (!chosenAgentId.isEmpty()) {
        const GanttAgentChoice *chosen = nullptr;
        for (const auto &c : all) {
            if (c.agentId == chosenAgentId) {
                chosen = &c;
            }
        }
        if (!chosen) {
            result.refusal = AgendaText::tr("This agent is not one of yours.");
            return result;
        }
        if (chosen->retired || chosen->paused) {
            result.refusal = AgendaText::tr("This agent can't be chosen now.");
            return result;
        }
        // A planning person of the agent that is already on the task stays; otherwise the one of this project.
        for (const int id : chosen->resourceIds) {
            if (onTask.contains(id)) {
                chosenResource = id;
                break;
            }
        }
        if (chosenResource <= 0) {
            chosenResource = chosen->resourceId;
        }
        if (chosenResource <= 0) {
            result.refusal = chosen->askManagerText;
            return result;
        }
    }

    // The new list: every other row exactly as it is, in its order; my other agents are removed.
    QJsonArray newRows;
    QSet<int> present;
    bool primaryRemoved = false;
    for (int i = 0; i < rows.size(); ++i) {
        const auto row = rows.at(i).toObject();
        const int rid = intOf(row.value(QLatin1String("resource_id")));
        if (myAgentResources.contains(rid) && rid != chosenResource) {
            if (i == 0) {
                primaryRemoved = true;
            }
            continue;
        }
        QJsonObject kept;
        kept.insert(QStringLiteral("resource_id"), rid);
        kept.insert(QStringLiteral("work"), hasWork(row.value(QLatin1String("work"))) ? QJsonValue(numberOf(row.value(QLatin1String("work")))) : QJsonValue(QJsonValue::Null));
        newRows.append(kept);
        present.insert(rid);
    }
    if (primaryRemoved) {
        result.refusal = AgendaText::tr("Changing the agent here would change the main person of this task. Open in NoGantt.");
        return result;
    }
    if (chosenResource > 0 && !present.contains(chosenResource)) {
        QJsonObject added;
        added.insert(QStringLiteral("resource_id"), chosenResource);
        added.insert(QStringLiteral("work"), QJsonValue(QJsonValue::Null));
        newRows.append(added);
    }
    if (newRows.size() > kMaxRows) {
        result.refusal = AgendaText::tr("This task already has 4 people or agents. Ask your project manager.");
        return result;
    }

    // Nothing to send when the list does not change.
    QJsonArray originalRows;
    for (const auto &rowValue : rows) {
        const auto row = rowValue.toObject();
        QJsonObject o;
        o.insert(QStringLiteral("resource_id"), intOf(row.value(QLatin1String("resource_id"))));
        o.insert(QStringLiteral("work"), hasWork(row.value(QLatin1String("work"))) ? QJsonValue(numberOf(row.value(QLatin1String("work")))) : QJsonValue(QJsonValue::Null));
        originalRows.append(o);
    }
    if (sameResources(originalRows, newRows)) {
        result.ok = true;
        result.noChange = true;
        result.resources = newRows;
        return result;
    }

    // The server recomputes the total work of the task from the rows. Refuse when that would change it.
    double total = 0;
    for (const auto &rowValue : std::as_const(newRows)) {
        const auto row = rowValue.toObject();
        if (hasWork(row.value(QLatin1String("work")))) {
            total += workInPt(row.value(QLatin1String("work")), resourceById(visibleResources, intOf(row.value(QLatin1String("resource_id")))), hoursPerPt);
        }
    }
    const double current = numberOf(freshTask.value(QLatin1String("work")), 0);
    if (qAbs(qRound(total * 100) / 100.0 - current) > 0.01) {
        result.refusal = AgendaText::tr("Changing the agent here would change the task's work estimate. Open in NoGantt.");
        return result;
    }

    result.ok = true;
    result.resources = newRows;
    return result;
}

bool sameResources(const QJsonArray &sent, const QJsonArray &returned)
{
    if (sent.size() != returned.size()) {
        return false;
    }
    for (int i = 0; i < sent.size(); ++i) {
        const auto a = sent.at(i).toObject();
        const auto b = returned.at(i).toObject();
        if (intOf(a.value(QLatin1String("resource_id"))) != intOf(b.value(QLatin1String("resource_id")))) {
            return false;
        }
        const bool aw = hasWork(a.value(QLatin1String("work")));
        const bool bw = hasWork(b.value(QLatin1String("work")));
        if (aw != bw) {
            return false;
        }
        if (aw && qAbs(numberOf(a.value(QLatin1String("work"))) - numberOf(b.value(QLatin1String("work")))) > 0.001) {
            return false;
        }
    }
    return true;
}

} // namespace OCC::Nourax::Agenda::GanttAgentChange
