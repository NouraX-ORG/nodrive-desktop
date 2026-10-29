/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "agendacontext.h"

namespace OCC::Nourax::Agenda {

QDate dateOfText(const QJsonValue &value)
{
    if (!value.isString()) {
        return QDate();
    }
    return QDate::fromString(value.toString().left(10), Qt::ISODate);
}

QDate localDateOfIso(const QString &text, const QTimeZone &zone, QDateTime *utcOut)
{
    if (text.isEmpty()) {
        return QDate();
    }
    if (text.size() <= 10) {
        return QDate::fromString(text, Qt::ISODate);
    }
    const auto dt = QDateTime::fromString(text, Qt::ISODate);
    if (!dt.isValid()) {
        return QDate::fromString(text.left(10), Qt::ISODate);
    }
    if (utcOut) {
        *utcOut = dt.toUTC();
    }
    return dt.toTimeZone(zone).date();
}

ProblemFlag makeFlag(ProblemFlag::Kind kind, ProblemFlag::Audience audience, const QString &text)
{
    ProblemFlag f;
    f.kind = kind;
    f.audience = audience;
    f.text = text;
    return f;
}

namespace FlagText {

QString lateDays(int days)
{
    return days == 1 ? AgendaText::tr("1 day late") : AgendaText::tr("%1 days late").arg(days);
}

QString dueToday()
{
    return AgendaText::tr("Due today");
}

QString blockedBy(const QString &title)
{
    return AgendaText::tr("Blocked by %1").arg(title);
}

QString agentNoPerson()
{
    return AgendaText::tr("Agent with no person");
}

QString agentWrongPerson(const QString &agent, const QString &owner, const QString &other)
{
    if (owner.isEmpty()) {
        return AgendaText::tr("%1 belongs to someone else, not %2").arg(agent, other);
    }
    return AgendaText::tr("%1 belongs to %2, not %3").arg(agent, owner, other);
}

QString agentWithoutYou(const QString &agent, const QString &item)
{
    return AgendaText::tr("%1 is on \"%2\" without you").arg(agent, item);
}

QString twoAgents(const QString &person)
{
    return AgendaText::tr("Two agents for %1: choose one").arg(person);
}

QString unknownAgent(const QString &label)
{
    return AgendaText::tr("%1 (unknown agent)").arg(label);
}

QString retiredAgent(const QString &name)
{
    return AgendaText::tr("%1 (retired)").arg(name);
}

QString notLinked()
{
    return AgendaText::tr("Not linked to an account");
}

QString notAssigned()
{
    return AgendaText::tr("Not assigned to a person");
}

QString notShared(const QString &person, int tasks)
{
    if (tasks == 1) {
        return AgendaText::tr("%1 has 1 task in a project not shared with %1").arg(person);
    }
    return AgendaText::tr("%1 has %2 tasks in a project not shared with %1").arg(person).arg(tasks);
}

QString overbooked(const QString &person)
{
    return AgendaText::tr("%1 is overbooked").arg(person);
}

} // namespace FlagText

} // namespace OCC::Nourax::Agenda
