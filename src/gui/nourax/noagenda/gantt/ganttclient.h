/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: the NoGantt calls of plans/noagenda-desktop-plan.md, 1.5. Every route of NoGantt 2.4.1 is an
// OCS route: ocs/v2.php/apps/nc_gantt/api/v1/... with the OCS-APIRequest header.

#pragma once

#include "../logic/sourcesettings.h"
#include "../net/httptransport.h"
#include "ganttdata.h"

#include <functional>

namespace OCC::Nourax::Agenda {

struct GanttFetchOptions
{
    QString davUser;
    bool serverAdmin = false;
    QStringList groups;
    QDateTime now;
    bool forceSlow = false;
    /// Whether the data of this project is needed (its mode is not Hidden).
    std::function<bool(const ProjectRelation &)> wantProject;
};

struct GanttFetchResult
{
    bool ok = false;
    int status = 0;
    bool networkError = false;
    GanttSnapshot snapshot; ///< on failure: the previous snapshot
};

class GanttClient
{
public:
    static QString apiBase();
    static void fetch(HttpTransport *transport, const GanttFetchOptions &options, const GanttSnapshot &previous,
        const std::function<void(const GanttFetchResult &)> &done);
};

} // namespace OCC::Nourax::Agenda
