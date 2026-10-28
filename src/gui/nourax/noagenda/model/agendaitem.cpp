/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "agendaitem.h"

#include <QTimeZone>

namespace OCC::Nourax::Agenda {

QString AgentRef::displayText() const
{
    if (isSelf) {
        return AgendaText::tr("Self");
    }
    if (otherOrganisation) {
        return AgendaText::tr("Agent of %1").arg(owner);
    }
    if (unknown) {
        return AgendaText::tr("%1 (unknown agent)").arg(id);
    }
    if (retired) {
        return AgendaText::tr("%1 (retired)").arg(name);
    }
    return name;
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
