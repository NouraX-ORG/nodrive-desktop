/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: the version guard for NoGantt. NoGantt 2.4.1 shows no version to a normal user, so every
// response is checked for the keys and types of 2.4.1 (plans/noagenda-desktop-plan.md, 1.6). Any
// mismatch turns NoGantt into read only with "Open in NoGantt".

#pragma once

#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>

namespace OCC::Nourax::Agenda {

namespace GanttContract {

enum class Endpoint { MyTasks, Projects, ProjectTasks, Milestones, Subprojects, Resources, CapacityCheck, TaskSync, Shares };

QString name(Endpoint endpoint);
/// The keys (with the JSON types) that NoGantt 2.4.1 answers with. A key may also be null where the
/// source allows it; "any" means the type is not checked.
struct Key
{
    QString name;
    QString type; ///< string | number | bool | array | object | any
};
QList<Key> requiredKeys(Endpoint endpoint);
/// Empty if `data` (the OCS data of the answer) has the expected shape; otherwise a sentence that names
/// the endpoint and the first missing or wrong key (never a value).
QString check(Endpoint endpoint, const QJsonValue &data);

} // namespace GanttContract

} // namespace OCC::Nourax::Agenda
