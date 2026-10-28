/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: NoGantt tasks and milestones to rows, with the agent of every assignment (design 4.2, 4.4,
// 4.5, 6.2, 6.3).

#pragma once

#include "../gantt/ganttdata.h"
#include "buildcontext.h"

namespace OCC::Nourax::Agenda {

namespace GanttMapper {

struct Result
{
    QList<AgendaItem> items;
    QSet<int> linkedCardIds; ///< board cards that are one row with their Gantt task (dedupe)
};

/// NoGantt's own "copy my tasks into a calendar": the calendar uri when it is switched on, else empty.
QString taskCopyCalendarUri(const GanttSnapshot &snapshot);

/// The relation of a project, built from the snapshot.
ProjectRelation relationOf(const BuildContext &ctx, const GanttSnapshot &snapshot, const QJsonObject &project);

void map(const BuildContext &ctx, const GanttSnapshot &snapshot, Result *out);

/// Hours of work of a resource row: hours, or person days x hours per person day.
double workHours(const QJsonObject &row, const QJsonObject &resource, double hoursPerPt);

} // namespace GanttMapper

} // namespace OCC::Nourax::Agenda
