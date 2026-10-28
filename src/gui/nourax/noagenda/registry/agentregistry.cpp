/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "agentregistry.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace OCC::Nourax::Agenda {

namespace {

QString stringOf(const QJsonObject &o, const char *key)
{
    const auto v = o.value(QLatin1String(key));
    return v.isString() ? v.toString().trimmed() : QString();
}

QStringList stringList(const QJsonValue &v)
{
    QStringList out;
    if (v.isArray()) {
        const auto array = v.toArray();
        for (const auto &e : array) {
            if (e.isString()) {
                out.append(e.toString());
            }
        }
    }
    return out;
}

} // namespace

QString AgentRegistry::normalize(const QString &text)
{
    return text.normalized(QString::NormalizationForm_C);
}

QString AgentRegistry::planningPersonName(const QString &agentName, const QString &personDisplayName)
{
    // U+1F916 ROBOT FACE, then a space.
    const QString robot = QString::fromUcs4(U"\U0001F916");
    return robot + QLatin1Char(' ') + agentName + QStringLiteral(" (") + personDisplayName + QLatin1Char(')');
}

AgentRegistry AgentRegistry::missing()
{
    AgentRegistry r;
    r._status = Status::Missing;
    return r;
}

AgentRegistry AgentRegistry::parse(const QByteArray &json)
{
    AgentRegistry r;
    r._status = Status::Invalid;
    if (json.size() > maxBytes) {
        return r;
    }
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return r;
    }
    const auto root = doc.object();
    if (root.value(QLatin1String("schema")).toString() != QLatin1String("nourax-agent-registry")
        || root.value(QLatin1String("version")).toInt(-1) != 1 || !root.value(QLatin1String("agents")).isArray()) {
        return r;
    }
    r._updated = QDateTime::fromString(stringOf(root, "updated_utc"), Qt::ISODate);

    QSet<QString> seen;
    const auto agents = root.value(QLatin1String("agents")).toArray();
    for (const auto &entry : agents) {
        if (!entry.isObject()) {
            ++r._skipped;
            continue;
        }
        const auto o = entry.toObject();
        Agent a;
        a.id = stringOf(o, "id");
        a.name = stringOf(o, "name");
        a.person = stringOf(o, "person");
        a.status = stringOf(o, "status");
        a.organisation = stringOf(o, "organisation");
        const bool statusOk = a.status == QLatin1String("active") || a.status == QLatin1String("paused") || a.status == QLatin1String("retired");
        if (a.id.isEmpty() || a.name.isEmpty() || a.person.isEmpty() || !statusOk || a.organisation.isEmpty() || seen.contains(a.id)) {
            ++r._skipped;
            continue;
        }
        seen.insert(a.id);
        a.skills = stringList(o.value(QLatin1String("skills")));
        const auto availability = o.value(QLatin1String("availability")).toObject();
        a.timezone = stringOf(availability, "timezone");
        a.capacityHoursPerWeek = availability.value(QLatin1String("capacity_hours_per_week")).toDouble(0);
        a.daysOff = stringList(availability.value(QLatin1String("days_off")));
        const auto weekly = availability.value(QLatin1String("weekly")).toArray();
        for (const auto &w : weekly) {
            const auto wo = w.toObject();
            AgentWeekly entryWeekly;
            entryWeekly.days = stringList(wo.value(QLatin1String("days")));
            entryWeekly.from = stringOf(wo, "from");
            entryWeekly.to = stringOf(wo, "to");
            a.weekly.append(entryWeekly);
        }
        const auto links = o.value(QLatin1String("links")).toObject();
        const auto ids = links.value(QLatin1String("nogantt_resource_ids")).toArray();
        for (const auto &id : ids) {
            if (id.isDouble()) {
                a.ganttResourceIds.append(id.toInt());
            }
        }
        a.boardLabel = stringOf(links, "noboard_label");
        a.calendarTag = stringOf(links, "calendar_tag");
        r._agents.append(a);
    }
    r._status = Status::Ok;
    return r;
}

const Agent *AgentRegistry::byId(const QString &id) const
{
    for (const auto &a : _agents) {
        if (a.id == id) {
            return &a;
        }
    }
    return nullptr;
}

const Agent *AgentRegistry::byLabel(const QString &label) const
{
    const auto wanted = normalize(label);
    for (const auto &a : _agents) {
        if (!a.boardLabel.isEmpty() && normalize(a.boardLabel) == wanted) {
            return &a;
        }
    }
    return nullptr;
}

const Agent *AgentRegistry::byTag(const QString &tag) const
{
    for (const auto &a : _agents) {
        if (!a.calendarTag.isEmpty() && a.calendarTag == tag) {
            return &a;
        }
    }
    return nullptr;
}

const Agent *AgentRegistry::byResourceId(int resourceId) const
{
    for (const auto &a : _agents) {
        if (a.ganttResourceIds.contains(resourceId)) {
            return &a;
        }
    }
    return nullptr;
}

QList<const Agent *> AgentRegistry::ofPerson(const QString &uid) const
{
    QList<const Agent *> out;
    for (const auto &a : _agents) {
        if (a.person.compare(uid, Qt::CaseInsensitive) == 0) {
            out.append(&a);
        }
    }
    return out;
}

QSet<QString> AgentRegistry::organisationsOf(const QString &uid) const
{
    QSet<QString> out;
    for (const auto *a : ofPerson(uid)) {
        out.insert(a->organisation);
    }
    return out;
}

AgentRef AgentRegistry::refFor(const Agent &agent)
{
    AgentRef ref;
    ref.isSelf = false;
    ref.id = agent.id;
    ref.name = agent.name;
    ref.status = agent.status;
    ref.paused = agent.isPaused();
    ref.retired = agent.isRetired();
    return ref;
}

AgentRef AgentRegistry::unknownRef(const QString &idOrLabel)
{
    AgentRef ref;
    ref.isSelf = false;
    ref.id = idOrLabel;
    ref.unknown = true;
    return ref;
}

AgentRef AgentRegistry::selfRef()
{
    return AgentRef();
}

} // namespace OCC::Nourax::Agenda
