/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: events and to-dos to rows (plans/noagenda-decisions.md, 6.1 event rules, 6.2, 4.2 agents).

#pragma once

#include "../caldav/calendardata.h"
#include "buildcontext.h"

namespace OCC::Nourax::Agenda {

namespace CalendarMapper {

/// Adds the rows of all calendars that are not hidden. The same invitation (UID + recurrence) in two
/// visible calendars gives one row, preferring the person's own calendar (it carries their PARTSTAT).
void map(const BuildContext &ctx, const CalendarSnapshot &snapshot, QList<AgendaItem> *out);

/// The agent of an event or to-do: X-NOURAX-AGENT-ID, else a CATEGORIES value equal to a tag of one of
/// the person's agents. An invitation (someone else organises) is always Self (design 4.2).
AgentRef agentOf(const BuildContext &ctx, const CalRecord &record, bool invitation, QList<ProblemFlag> *problems);

} // namespace CalendarMapper

} // namespace OCC::Nourax::Agenda
