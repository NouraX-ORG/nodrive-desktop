/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: raw data of the four homes + the registry + the settings -> the rows and the Sources screen.
// Pure functions: the same input always gives the same rows (this is what the unit tests check).

#pragma once

#include "../caldav/calendardata.h"
#include "../deck/deckclient.h"
#include "../gantt/ganttdata.h"
#include "../model/agendaitem.h"
#include "../ocs/serverinfo.h"
#include "../registry/agentregistry.h"
#include "buildcontext.h"
#include "sourcesettings.h"

namespace OCC::Nourax::Agenda {

struct BuildInput
{
    ServerInfo server;
    QString davUser;
    QString accountEmail;
    AgentRegistry registry;
    SourceSettings settings;
    CalendarSnapshot calendar;
    GanttSnapshot gantt;
    DeckSnapshot deck;
    QDate today;
    QTimeZone zone;
    QDateTime now;
};

struct BuildOutput
{
    QList<AgendaItem> items;
    QList<SourceEntry> sources;
    bool needsPersonalQuestion = false;
    QList<QPair<QString, QString>> ownCalendars; ///< {uri, name} for the one-time question
    QString taskCopyCalendarUri;
    bool ganttNotLinked = false;
    bool ganttUnavailable = false;
    bool boardUnavailable = false;
    bool ganttReadOnlyByGuard = false;
    bool boardReadOnlyByGuard = false;
    QString ganttContractProblem;
    bool anyOversee = false;
    int registryAgentsOfMine = 0;
    QList<Agent> myAgents; ///< the person's own agents (Sources screen)
};

class AgendaBuilder
{
public:
    static BuildOutput build(const BuildInput &input);

    /// The person's working pattern (design 7): the NoGantt schedule of the linked planning person, else
    /// Monday to Friday 09:00-17:00, unless the person has set their own hours.
    static WorkingHours workingHours(const BuildInput &input);
};

} // namespace OCC::Nourax::Agenda
