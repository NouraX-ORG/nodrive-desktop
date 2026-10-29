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
#include <QString>

namespace OCC::Nourax::Agenda {

/// One of my agents, and whether it can be chosen on a task of this project.
struct GanttAgentChoice
{
    QString agentId;
    QString name;
    bool paused = false;
    bool retired = false;
    int resourceId = 0; ///< its planning person that I can see (0 = none)
    /// "Ask your project manager to add <robot> Writer-Bot to this project" when the planning person is not visible to me.
    QString askManagerText;

    [[nodiscard]] bool choosable() const { return resourceId > 0 && !paused && !retired; }
};

struct GanttAgentPlan
{
    bool ok = false;
    bool noChange = false; ///< the task already has this agent: nothing to send
    QString refusal; ///< plain words (the reason nothing is sent)
    QJsonArray resources; ///< the new list: [{resource_id, work}]
};

namespace GanttAgentChange {

/// The planning person of `agent` among the planning people visible to me, by the registry (rule 4.5 a) or by the
/// exact name (rule 4.5 b, the placeholder phase). 0 if there is none. `myNames` are the names of my planning people.
int resourceFor(const Agent &agent, const AgentRegistry &registry, const QJsonArray &visibleResources, const QStringList &myNames);

/// All of my agents with what can be done with them here.
QList<GanttAgentChoice> choices(const QString &me, const QString &myName, const AgentRegistry &registry, const QJsonArray &visibleResources);

/// The user id comparison of planning people and the person: case-insensitive.
int myResourceId(const QString &me, const QJsonArray &visibleResources);

/// The new list for `chosenAgentId` (empty = Self) on this fresh task, or the reason it is refused.
GanttAgentPlan plan(const QJsonObject &freshTask, const QJsonArray &visibleResources, const AgentRegistry &registry, const QString &me,
    const QString &myName, const QString &chosenAgentId, double hoursPerPt);

/// Whether the list that came back (`task.resources`) is the list that was sent (ids, order and work).
bool sameResources(const QJsonArray &sent, const QJsonArray &returned);

} // namespace GanttAgentChange

} // namespace OCC::Nourax::Agenda
