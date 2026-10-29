/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: "my agent" on a NoGantt task (design 4.5 and 9; plans/noagenda-desktop-plan.md, 2.5).
// PUT tasks/{id} with `resources` REPLACES the whole list of people on the task, so the new list must keep every
// other row exactly, and the change is refused (nothing is sent) whenever the server could drop or change
// something else. NoAgenda never creates planning people.

#pragma once

#include "../registry/agentregistry.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QString>

namespace OCC::Nourax::Agenda {

/// One of my agents, and whether it can be chosen on a task of this project.
struct GanttAgentChoice
{
    QString agentId;
    QString name;
    bool paused = false;
    bool retired = false;
    int resourceId = 0; ///< its planning person that I can see and that belongs to this project (0 = none)
    QList<int> resourceIds; ///< every planning person that stands for this agent and that I can see (it can have one per manager pool)
    /// "Ask your project manager to add <robot> Writer-Bot to this project" when the planning person is not visible to me.
    QString askManagerText;

    [[nodiscard]] bool choosable() const { return resourceId > 0 && !paused && !retired; }
};

/// What is known of the project a task is in: design 4.5 allows only a planning person that belongs to the project, that
/// is, one that already sits on a task of it or that is in the pool of the project owner.
struct GanttProjectInfo
{
    bool known = false; ///< false = nothing is known: every visible planning person counts
    QString ownerId;
    QSet<int> resourceIdsOnTasks;

    /// `tasks` are the tasks of the project (each with its `resources` rows).
    static GanttProjectInfo of(const QString &ownerId, const QJsonArray &tasks);
};

struct GanttAgentPlan
{
    bool ok = false;
    bool noChange = false; ///< the task already has this agent: nothing to send
    QString refusal; ///< plain words (the reason nothing is sent)
    QJsonArray resources; ///< the new list: [{resource_id, work}]
};

namespace GanttAgentChange {

/// Every planning person of `agent` among the planning people visible to me, by the registry (rule 4.5 a: the ids of
/// the agent, one per manager pool) or by the exact name (rule 4.5 b, the placeholder phase). Empty if there is none.
/// `myNames` are the names of my planning people.
QList<int> resourcesFor(const Agent &agent, const QJsonArray &visibleResources, const QStringList &myNames);

/// The one of those that belongs to this project, preferring one that is on a task of it, then one in the pool of the
/// project owner. 0 if there is none. Without project information, or without a known owner, the first one.
int resourceFor(const Agent &agent, const QJsonArray &visibleResources, const QStringList &myNames, const GanttProjectInfo &project = GanttProjectInfo());

/// All of my agents with what can be done with them here.
QList<GanttAgentChoice> choices(const QString &me, const QString &myName, const AgentRegistry &registry, const QJsonArray &visibleResources,
    const GanttProjectInfo &project = GanttProjectInfo());

/// The user id comparison of planning people and the person: case-insensitive.
int myResourceId(const QString &me, const QJsonArray &visibleResources);

/// The new list for `chosenAgentId` (empty = Self) on this fresh task, or the reason it is refused.
GanttAgentPlan plan(const QJsonObject &freshTask, const QJsonArray &visibleResources, const AgentRegistry &registry, const QString &me,
    const QString &myName, const QString &chosenAgentId, double hoursPerPt, const GanttProjectInfo &project = GanttProjectInfo());

/// Whether the list that came back (`task.resources`) is the list that was sent (ids, order and work).
bool sameResources(const QJsonArray &sent, const QJsonArray &returned);

} // namespace GanttAgentChange

} // namespace OCC::Nourax::Agenda
