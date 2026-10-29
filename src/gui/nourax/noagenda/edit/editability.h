/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: what may be changed on a row, and the plain reason when it may not (design 9;
// plans/noagenda-desktop-plan.md, 2.4). Pure: the same row always gives the same answer.

#pragma once

#include "../model/agendaitem.h"

namespace OCC::Nourax::Agenda {

struct Editability
{
    bool canEdit = false; ///< title, time, dates, description (events, to-dos), dates (NoGantt), due date (cards)
    bool canDelete = false; ///< only events and to-dos
    bool canDone = false;
    bool canProgress = false; ///< NoGantt: % done (read only when it comes from the board)
    bool canStatus = false; ///< NoGantt: the status (design 9: also when % done comes from the board)
    bool canChangeAgent = false;
    bool repeats = false;
    bool locked = false; ///< show the lock: the person may see this row but not change it
    QString reason; ///< why nothing can be changed (empty when canEdit)
    QString agentReason; ///< why the agent can't be changed (empty when canChangeAgent)
    QString progressReason; ///< why % done can't be changed ("% done comes from NoBoard")
    QString openLabel; ///< "Open in NoCalendar" and so on, shown with a reason
};

namespace EditRules {

Editability of(const AgendaItem &item);

/// The status values of NoGantt, in the order of the editor.
QStringList ganttStatuses();
QString statusText(const QString &status);

} // namespace EditRules

} // namespace OCC::Nourax::Agenda
