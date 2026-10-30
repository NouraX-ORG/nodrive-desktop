/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "agendaitem.h"

#include <QTimeZone>

namespace OCC::Nourax::Agenda {

namespace {

/// The agent label draws its own robot icon, so the robot emoji that a planning person's name starts with is left out (it is
/// also not in every font and shows as an empty box).
QString withoutRobot(const QString &text)
{
    const QString robot = QString::fromUcs4(U"\U0001F916");
    if (!text.startsWith(robot)) {
        return text;
    }
    return text.mid(robot.size()).trimmed();
}

} // namespace

QString AgentRef::displayText() const
{
    if (isSelf) {
        return AgendaText::tr("Self");
    }
    if (otherOrganisation) {
        return owner.isEmpty() ? AgendaText::tr("Agent of someone else") : AgendaText::tr("Agent of %1").arg(owner);
    }
    if (unknown) {
        return AgendaText::tr("%1 (unknown agent)").arg(withoutRobot(id));
    }
    if (retired) {
        return AgendaText::tr("%1 (retired)").arg(withoutRobot(name));
    }
    return withoutRobot(name);
}

bool AgendaItem::hasDate() const
{
    return timed || startDate.isValid() || endDate.isValid() || due.isValid();
}

QDate AgendaItem::firstDay(const QTimeZone &zone) const
{
    if (timed && start.isValid()) {
        return start.toTimeZone(zone).date();
    }
    if (startDate.isValid()) {
        return startDate;
    }
    if (endDate.isValid()) {
        return endDate;
    }
    return due;
}

QDate AgendaItem::lastDay(const QTimeZone &zone) const
{
    if (timed && start.isValid()) {
        // An event that ends exactly at midnight does not belong to the next day.
        auto last = (end.isValid() && end > start) ? end.addSecs(-1) : start;
        return last.toTimeZone(zone).date();
    }
    if (endDate.isValid()) {
        return endDate;
    }
    if (due.isValid()) {
        return due;
    }
    return startDate;
}

} // namespace OCC::Nourax::Agenda
