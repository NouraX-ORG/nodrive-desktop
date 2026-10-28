/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: the agent registry (plans/noagenda-decisions.md, 4.1; format in
// plans/agent-registry.placeholder.json, schema "nourax-agent-registry", version 1).

#pragma once

#include "../model/agendaitem.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

namespace OCC::Nourax::Agenda {

struct AgentWeekly
{
    QStringList days; ///< mon, tue, ...
    QString from;
    QString to;
};

struct Agent
{
    QString id;
    QString name;
    QString person; ///< owner uid
    QString status; ///< active | paused | retired
    QString organisation;
    QStringList skills;
    QString timezone;
    QList<AgentWeekly> weekly;
    double capacityHoursPerWeek = 0;
    QStringList daysOff;
    QList<int> ganttResourceIds; ///< links.nogantt_resource_ids
    QString boardLabel; ///< links.noboard_label
    QString calendarTag; ///< links.calendar_tag

    [[nodiscard]] bool isActive() const { return status == QLatin1String("active"); }
    [[nodiscard]] bool isPaused() const { return status == QLatin1String("paused"); }
    [[nodiscard]] bool isRetired() const { return status == QLatin1String("retired"); }
};

class AgentRegistry
{
public:
    enum class Status { Missing, Invalid, Ok };

    static constexpr int maxBytes = 1024 * 1024;

    /// Parses the file. Anything that is not schema "nourax-agent-registry" version 1 is Invalid.
    /// Agents with an empty or invalid id, name, person, status or organisation are skipped (and counted);
    /// a repeated id skips the later agent.
    static AgentRegistry parse(const QByteArray &json);
    static AgentRegistry missing();

    [[nodiscard]] Status status() const { return _status; }
    [[nodiscard]] int skippedCount() const { return _skipped; }
    [[nodiscard]] const QList<Agent> &agents() const { return _agents; }
    [[nodiscard]] QDateTime updated() const { return _updated; }

    [[nodiscard]] const Agent *byId(const QString &id) const;
    /// Board label title, compared after Unicode normalisation (NFC).
    [[nodiscard]] const Agent *byLabel(const QString &label) const;
    [[nodiscard]] const Agent *byTag(const QString &tag) const;
    [[nodiscard]] const Agent *byResourceId(int resourceId) const;
    [[nodiscard]] QList<const Agent *> ofPerson(const QString &uid) const;
    /// The "organisation" values of the agents of this person.
    [[nodiscard]] QSet<QString> organisationsOf(const QString &uid) const;

    /// The reference shown on a row (the registry name, status and flags).
    [[nodiscard]] static AgentRef refFor(const Agent &agent);
    [[nodiscard]] static AgentRef unknownRef(const QString &idOrLabel);
    [[nodiscard]] static AgentRef selfRef();

    /// The exact planning-person name of the placeholder phase: "<robot> <agent name> (<person's display name>)".
    [[nodiscard]] static QString planningPersonName(const QString &agentName, const QString &personDisplayName);
    [[nodiscard]] static QString normalize(const QString &text);

private:
    Status _status = Status::Missing;
    int _skipped = 0;
    QDateTime _updated;
    QList<Agent> _agents;
};

} // namespace OCC::Nourax::Agenda
