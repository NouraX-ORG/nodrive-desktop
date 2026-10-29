/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: the load of an agent against its registry availability and capacity (design 4.3 and 7;
// plans/noagenda-desktop-plan.md, 2.7). Agent items never take the person's time, but they count for the
// agent: "Writer-Bot is overbooked on Thursday".
//   capacity per day = capacity_hours_per_week / (number of days of the weekly pattern), no capacity on a day off
//   load of a day    = the work of the agent's own planning person on the NoGantt tasks, spread over the task's days
//                      that are available days of the agent, plus its timed events of that day

#pragma once

#include "../model/agendaitem.h"
#include "../registry/agentregistry.h"

#include <QDate>
#include <QList>
#include <QTimeZone>

namespace OCC::Nourax::Agenda {

struct AgentOverbooking
{
    QString agentId;
    QString agentName;
    QDate day;
    double load = 0;
    double capacity = 0;
    bool mine = false; ///< one of my agents (else an agent of a person whose work I oversee)
    QString personName; ///< the person the agent belongs to (team rows)
};

namespace AgentLoad {

/// Whether the agent works on this day: the day is in its weekly pattern and not in its days off.
bool isAvailable(const Agent &agent, const QDate &day);
/// Hours of capacity on this day (0 on a day it does not work).
double capacityOn(const Agent &agent, const QDate &day);
/// Hours of work on this day, from the rows of the person's agenda.
double loadOn(const Agent &agent, const QList<AgendaItem> &items, const QDate &day, const QTimeZone &zone);

/// Every day from `from` to `to` on which an agent that appears in the rows is over its capacity.
QList<AgentOverbooking> compute(const AgentRegistry &registry, const QList<AgendaItem> &items, const QDate &from, const QDate &to,
    const QTimeZone &zone, const QString &me);

/// The rows that tell the person ("Writer-Bot is overbooked on Thursday"), one per agent and day.
QList<AgendaItem> asItems(const QList<AgentOverbooking> &overbookings, const AgentRegistry &registry);

} // namespace AgentLoad

} // namespace OCC::Nourax::Agenda
