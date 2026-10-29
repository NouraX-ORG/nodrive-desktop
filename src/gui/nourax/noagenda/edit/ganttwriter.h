/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: writes NoGantt tasks (plans/noagenda-desktop-plan.md, 2.4 and 2.5). NoGantt has no ETag, so the
// task is read fresh right before the write and its `updated_at` is compared with the one the person saw.
// Every route is an OCS route: ocs/v2.php/apps/nc_gantt/api/v1/...

#pragma once

#include "../net/httptransport.h"
#include "editcommand.h"

namespace OCC::Nourax::Agenda {

class GanttWriter
{
public:
    static void run(HttpTransport *transport, const EditCommand &command, const EditContext &context, const EditDone &done);
    /// "Undo" of a move: POST changes/{opId}/revert.
    static void undo(HttpTransport *transport, const QString &opId, const EditDone &done);

    /// The body of the PUT for a GanttUpdate, built from the fresh task (public for the tests).
    /// An empty object (with `refusal` set) means nothing may be sent.
    static QJsonObject updateBody(const EditCommand &command, const QJsonObject &freshTask, QString *refusal);

private:
    static void withFreshTask(HttpTransport *transport, const EditCommand &command,
        const std::function<void(const QJsonObject &task, const QJsonArray &projectTasks, const EditResult &)> &next);
    static void put(HttpTransport *transport, const EditCommand &command, const QJsonObject &freshTask, const QJsonObject &body, const QJsonArray &sentResources, const EditDone &done);
};

} // namespace OCC::Nourax::Agenda
