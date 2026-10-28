/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: what the mappers (calendar, NoGantt, NoBoard) need to turn raw data into rows.

#pragma once

#include "../model/agendaitem.h"
#include "../registry/agentregistry.h"
#include "sourcesettings.h"

#include <QDate>
#include <QJsonValue>
#include <QSet>
#include <QTimeZone>

namespace OCC::Nourax::Agenda {

struct BuildContext
{
    QString me; ///< uid of the signed-in person
    QString myName;
    QStringList myAddresses; ///< lower case e-mail addresses (calendar-user-address-set, account e-mail)
    QStringList myGroups;
    bool serverAdmin = false;
    AgentRegistry registry;
    SourceSettings settings;
    QDate today;
    QTimeZone zone;
    QDateTime now;
    double hoursPerPt = 8.0;
    QString taskCopyCalendarUri; ///< NoGantt's copy calendar (hidden)

    [[nodiscard]] bool isMe(const QString &uid) const { return !uid.isEmpty() && uid.compare(me, Qt::CaseInsensitive) == 0; }
    [[nodiscard]] QSet<QString> myOrganisations() const { return registry.organisationsOf(me); }

    /// The reference shown for a registry agent. The name of an agent of another person is shown only
    /// when its organisation is one of mine (design 4.1 privacy); otherwise "Agent of <person>".
    [[nodiscard]] AgentRef refFor(const Agent &agent, const QString &ownerDisplayName) const
    {
        auto ref = AgentRegistry::refFor(agent);
        if (!isMe(agent.person) && !myOrganisations().contains(agent.organisation)) {
            ref.otherOrganisation = true;
            ref.owner = ownerDisplayName.isEmpty() ? agent.person : ownerDisplayName;
        }
        return ref;
    }
};

/// The date (first ten characters) of "Y-m-d" or "Y-m-d H:i:s" text.
QDate dateOfText(const QJsonValue &value);
/// A Deck due date: "2026-11-02T00:00:00+00:00" (shown in the local zone) or a plain date.
QDate localDateOfIso(const QString &text, const QTimeZone &zone, QDateTime *utcOut = nullptr);

ProblemFlag makeFlag(ProblemFlag::Kind kind, ProblemFlag::Audience audience, const QString &text);

/// The sentences of the problem flags (English; translations are a to-do).
namespace FlagText {
QString lateDays(int days);
QString dueToday();
QString blockedBy(const QString &title);
QString agentNoPerson();
QString agentWrongPerson(const QString &agent, const QString &owner, const QString &other);
QString twoAgents(const QString &person);
QString unknownAgent(const QString &label);
QString retiredAgent(const QString &name);
QString notLinked();
QString notAssigned();
QString notShared(const QString &person, int tasks);
QString overbooked(const QString &person);
}

} // namespace OCC::Nourax::Agenda
