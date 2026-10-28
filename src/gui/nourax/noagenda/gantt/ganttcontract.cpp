/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ganttcontract.h"

#include "../ocs/ocsjson.h"

#include <QJsonArray>
#include <QJsonObject>

namespace OCC::Nourax::Agenda::GanttContract {

QString name(Endpoint endpoint)
{
    switch (endpoint) {
    case Endpoint::MyTasks:
        return QStringLiteral("my-tasks");
    case Endpoint::Projects:
        return QStringLiteral("projects");
    case Endpoint::ProjectTasks:
        return QStringLiteral("projects/{id}/tasks");
    case Endpoint::Milestones:
        return QStringLiteral("projects/{id}/milestones");
    case Endpoint::Subprojects:
        return QStringLiteral("projects/{id}/subprojects");
    case Endpoint::Resources:
        return QStringLiteral("resources");
    case Endpoint::CapacityCheck:
        return QStringLiteral("capacity-check");
    case Endpoint::TaskSync:
        return QStringLiteral("task-sync");
    case Endpoint::Shares:
        return QStringLiteral("projects/{id}/shares");
    }
    return QString();
}

QList<Key> requiredKeys(Endpoint endpoint)
{
    switch (endpoint) {
    case Endpoint::MyTasks:
        // The answer is an object {has_linked_resource, tasks}; the keys of one task are in Projects' sibling list below.
        return {{QStringLiteral("has_linked_resource"), QStringLiteral("bool")}, {QStringLiteral("tasks"), QStringLiteral("array")}};
    case Endpoint::Projects:
        return {{QStringLiteral("id"), QStringLiteral("number")}, {QStringLiteral("title"), QStringLiteral("string")},
            {QStringLiteral("color"), QStringLiteral("string")}, {QStringLiteral("owner_id"), QStringLiteral("string")},
            {QStringLiteral("permission"), QStringLiteral("string")}};
    case Endpoint::ProjectTasks:
        return {{QStringLiteral("id"), QStringLiteral("number")}, {QStringLiteral("title"), QStringLiteral("string")},
            {QStringLiteral("project_id"), QStringLiteral("number")}, {QStringLiteral("start_date"), QStringLiteral("any")},
            {QStringLiteral("end_date"), QStringLiteral("any")}, {QStringLiteral("work"), QStringLiteral("any")},
            {QStringLiteral("status"), QStringLiteral("string")}, {QStringLiteral("manual_progress"), QStringLiteral("number")},
            {QStringLiteral("resources"), QStringLiteral("array")}, {QStringLiteral("dependencies"), QStringLiteral("array")},
            {QStringLiteral("updated_at"), QStringLiteral("any")}};
    case Endpoint::Milestones:
        return {{QStringLiteral("id"), QStringLiteral("number")}, {QStringLiteral("title"), QStringLiteral("string")},
            {QStringLiteral("milestone_date"), QStringLiteral("string")}};
    case Endpoint::Subprojects:
        return {{QStringLiteral("id"), QStringLiteral("number")}, {QStringLiteral("title"), QStringLiteral("string")}};
    case Endpoint::Resources:
        return {{QStringLiteral("id"), QStringLiteral("number")}, {QStringLiteral("name"), QStringLiteral("string")},
            {QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("uid"), QStringLiteral("any")},
            {QStringLiteral("unit"), QStringLiteral("string")}};
    case Endpoint::CapacityCheck:
        return {{QStringLiteral("findings"), QStringLiteral("array")}};
    case Endpoint::TaskSync:
        return {{QStringLiteral("enabled"), QStringLiteral("bool")}};
    case Endpoint::Shares:
        return {{QStringLiteral("principal_id"), QStringLiteral("string")}, {QStringLiteral("principal_type"), QStringLiteral("string")},
            {QStringLiteral("permission"), QStringLiteral("string")}};
    }
    return {};
}

namespace {

bool typeMatches(const QJsonValue &value, const QString &type)
{
    if (type == QLatin1String("any")) {
        return !value.isUndefined();
    }
    if (type == QLatin1String("string")) {
        return value.isString();
    }
    if (type == QLatin1String("number")) {
        return value.isDouble();
    }
    if (type == QLatin1String("bool")) {
        return value.isBool();
    }
    if (type == QLatin1String("array")) {
        return value.isArray();
    }
    if (type == QLatin1String("object")) {
        return value.isObject();
    }
    return false;
}

QString missingKey(Endpoint endpoint, const QString &key)
{
    return QStringLiteral("%1: key %2 is missing").arg(name(endpoint), key);
}

QString checkObject(Endpoint endpoint, const QJsonObject &object)
{
    for (const auto &key : requiredKeys(endpoint)) {
        if (!object.contains(key.name)) {
            return missingKey(endpoint, key.name);
        }
        if (!typeMatches(object.value(key.name), key.type)) {
            // Some keys may be null where the source allows it (for example a task without a start).
            if (!object.value(key.name).isNull() || key.type == QLatin1String("array") || key.type == QLatin1String("bool")) {
                return QStringLiteral("%1: key %2 has another type").arg(name(endpoint), key.name);
            }
        }
    }
    return QString();
}

} // namespace

QString check(Endpoint endpoint, const QJsonValue &data)
{
    if (endpoint == Endpoint::MyTasks) {
        if (!data.isObject()) {
            return QStringLiteral("%1: the answer is not an object").arg(name(endpoint));
        }
        const auto problem = checkObject(endpoint, data.toObject());
        if (!problem.isEmpty()) {
            return problem;
        }
        // One task of the list: the keys the day list needs.
        const auto tasks = data.toObject().value(QLatin1String("tasks")).toArray();
        if (!tasks.isEmpty()) {
            const auto task = tasks.first().toObject();
            for (const auto &key : {QStringLiteral("id"), QStringLiteral("title"), QStringLiteral("project_id"), QStringLiteral("start_date"), QStringLiteral("end_date"),
                     QStringLiteral("status")}) {
                if (!task.contains(key)) {
                    return QStringLiteral("%1: task key %2 is missing").arg(name(endpoint), key);
                }
            }
        }
        return QString();
    }
    if (endpoint == Endpoint::CapacityCheck || endpoint == Endpoint::TaskSync) {
        if (!data.isObject()) {
            return QStringLiteral("%1: the answer is not an object").arg(name(endpoint));
        }
        return checkObject(endpoint, data.toObject());
    }
    if (!data.isArray()) {
        return QStringLiteral("%1: the answer is not a list").arg(name(endpoint));
    }
    const auto list = data.toArray();
    for (const auto &entry : list) {
        if (!entry.isObject()) {
            return QStringLiteral("%1: an entry is not an object").arg(name(endpoint));
        }
        const auto problem = checkObject(endpoint, entry.toObject());
        if (!problem.isEmpty()) {
            return problem;
        }
    }
    return QString();
}

} // namespace OCC::Nourax::Agenda::GanttContract
