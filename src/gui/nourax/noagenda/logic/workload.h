/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: the workload of a day (design 7, "Overbooked"; plans/noagenda-desktop-plan.md, 2.7).
// planned = the person's own share of work on NoGantt tasks that are active that day, spread evenly over the
//           task's working days (holidays and days off excluded);
// free    = the working hours of the day minus the busy events inside them.
// Planned > free means overbooked. A task without `work` is "no estimate" and adds no hours. Only items whose
// agent is Self count: an agent's hours never count for the person.

#pragma once

#include "conflicts.h"

namespace OCC::Nourax::Agenda {

struct DayLoad
{
    QDate day;
    bool workingDay = false;
    double working = 0; ///< working hours of the day (0 on a day off)
    double busy = 0; ///< hours taken by busy events inside the working hours
    double free = 0; ///< working - busy
    double planned = 0; ///< hours of work planned on the day
    int noEstimate = 0; ///< tasks active on the day that have no estimate
    bool overbooked = false;

    /// "6 h planned / 4 h free"
    [[nodiscard]] QString text() const;
};

namespace Workload {

DayLoad forDay(const QList<AgendaItem> &items, const DayModel &model, const QDate &day);

/// "6", "4.5" (one decimal, no trailing zero).
QString hoursText(double hours);

} // namespace Workload

} // namespace OCC::Nourax::Agenda
