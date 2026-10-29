/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "calendardata.h"

#include <QJsonArray>

namespace OCC::Nourax::Agenda {

namespace {

QJsonArray toArray(const QStringList &list)
{
    QJsonArray a;
    for (const auto &s : list) {
        a.append(s);
    }
    return a;
}

QStringList toList(const QJsonValue &v)
{
    QStringList out;
    const auto array = v.toArray();
    for (const auto &e : array) {
        out.append(e.toString());
    }
    return out;
}

QString dateTimeToString(const QDateTime &dt)
{
    return dt.isValid() ? dt.toUTC().toString(Qt::ISODate) : QString();
}

QDateTime dateTimeFromString(const QString &s)
{
    return s.isEmpty() ? QDateTime() : QDateTime::fromString(s, Qt::ISODate).toUTC();
}

QString dateToString(const QDate &d)
{
    return d.isValid() ? d.toString(Qt::ISODate) : QString();
}

QDate dateFromString(const QString &s)
{
    return s.isEmpty() ? QDate() : QDate::fromString(s, Qt::ISODate);
}

} // namespace

QJsonObject CalendarInfo::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("href"), href);
    o.insert(QStringLiteral("uri"), uri);
    o.insert(QStringLiteral("name"), name);
    o.insert(QStringLiteral("color"), color);
    o.insert(QStringLiteral("owner"), ownerPrincipal);
    o.insert(QStringLiteral("ownerName"), ownerName);
    o.insert(QStringLiteral("mine"), mine);
    o.insert(QStringLiteral("readOnly"), readOnly);
    o.insert(QStringLiteral("subscribed"), subscribed);
    o.insert(QStringLiteral("birthdays"), birthdays);
    o.insert(QStringLiteral("deck"), deckBoard);
    o.insert(QStringLiteral("ignored"), ignored);
    o.insert(QStringLiteral("events"), hasEvents);
    o.insert(QStringLiteral("todos"), hasTodos);
    o.insert(QStringLiteral("transparent"), transparent);
    o.insert(QStringLiteral("syncToken"), syncToken);
    o.insert(QStringLiteral("ctag"), ctag);
    return o;
}

CalendarInfo CalendarInfo::fromJson(const QJsonObject &o)
{
    CalendarInfo c;
    c.href = o.value(QLatin1String("href")).toString();
    c.uri = o.value(QLatin1String("uri")).toString();
    c.name = o.value(QLatin1String("name")).toString();
    c.color = o.value(QLatin1String("color")).toString();
    c.ownerPrincipal = o.value(QLatin1String("owner")).toString();
    c.ownerName = o.value(QLatin1String("ownerName")).toString();
    c.mine = o.value(QLatin1String("mine")).toBool();
    c.readOnly = o.value(QLatin1String("readOnly")).toBool();
    c.subscribed = o.value(QLatin1String("subscribed")).toBool();
    c.birthdays = o.value(QLatin1String("birthdays")).toBool();
    c.deckBoard = o.value(QLatin1String("deck")).toBool();
    c.ignored = o.value(QLatin1String("ignored")).toBool();
    c.hasEvents = o.value(QLatin1String("events")).toBool();
    c.hasTodos = o.value(QLatin1String("todos")).toBool();
    c.transparent = o.value(QLatin1String("transparent")).toBool();
    c.syncToken = o.value(QLatin1String("syncToken")).toString();
    c.ctag = o.value(QLatin1String("ctag")).toString();
    return c;
}

QJsonObject CalRecord::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("cal"), calendarUri);
    o.insert(QStringLiteral("href"), href);
    o.insert(QStringLiteral("etag"), etag);
    o.insert(QStringLiteral("todo"), isTodo);
    o.insert(QStringLiteral("uid"), uid);
    o.insert(QStringLiteral("summary"), summary);
    o.insert(QStringLiteral("description"), description);
    o.insert(QStringLiteral("descriptionCut"), descriptionCut);
    o.insert(QStringLiteral("location"), location);
    o.insert(QStringLiteral("allDay"), allDay);
    o.insert(QStringLiteral("approx"), timeApproximated);
    o.insert(QStringLiteral("start"), dateTimeToString(start));
    o.insert(QStringLiteral("end"), dateTimeToString(end));
    o.insert(QStringLiteral("startDate"), dateToString(startDate));
    o.insert(QStringLiteral("endDate"), dateToString(endDate));
    o.insert(QStringLiteral("dueTime"), dateTimeToString(dueTime));
    o.insert(QStringLiteral("dueDate"), dateToString(dueDate));
    o.insert(QStringLiteral("dueIsDate"), dueIsDate);
    o.insert(QStringLiteral("transp"), transp);
    o.insert(QStringLiteral("status"), status);
    o.insert(QStringLiteral("percent"), percent);
    o.insert(QStringLiteral("completed"), completed);
    o.insert(QStringLiteral("rid"), recurrenceId);
    o.insert(QStringLiteral("repeating"), repeating);
    o.insert(QStringLiteral("organizer"), organizer);
    QJsonArray people;
    for (const auto &a : attendees) {
        QJsonObject p;
        p.insert(QStringLiteral("a"), a.address);
        p.insert(QStringLiteral("p"), a.partstat);
        people.append(p);
    }
    o.insert(QStringLiteral("attendees"), people);
    o.insert(QStringLiteral("agent"), agentId);
    o.insert(QStringLiteral("categories"), toArray(categories));
    return o;
}

CalRecord CalRecord::fromJson(const QJsonObject &o)
{
    CalRecord r;
    r.calendarUri = o.value(QLatin1String("cal")).toString();
    r.href = o.value(QLatin1String("href")).toString();
    r.etag = o.value(QLatin1String("etag")).toString();
    r.isTodo = o.value(QLatin1String("todo")).toBool();
    r.uid = o.value(QLatin1String("uid")).toString();
    r.summary = o.value(QLatin1String("summary")).toString();
    r.description = o.value(QLatin1String("description")).toString();
    r.descriptionCut = o.value(QLatin1String("descriptionCut")).toBool();
    r.location = o.value(QLatin1String("location")).toString();
    r.allDay = o.value(QLatin1String("allDay")).toBool();
    r.timeApproximated = o.value(QLatin1String("approx")).toBool();
    r.start = dateTimeFromString(o.value(QLatin1String("start")).toString());
    r.end = dateTimeFromString(o.value(QLatin1String("end")).toString());
    r.startDate = dateFromString(o.value(QLatin1String("startDate")).toString());
    r.endDate = dateFromString(o.value(QLatin1String("endDate")).toString());
    r.dueTime = dateTimeFromString(o.value(QLatin1String("dueTime")).toString());
    r.dueDate = dateFromString(o.value(QLatin1String("dueDate")).toString());
    r.dueIsDate = o.value(QLatin1String("dueIsDate")).toBool();
    r.transp = o.value(QLatin1String("transp")).toString();
    r.status = o.value(QLatin1String("status")).toString();
    r.percent = o.value(QLatin1String("percent")).toInt(-1);
    r.completed = o.value(QLatin1String("completed")).toBool();
    r.recurrenceId = o.value(QLatin1String("rid")).toString();
    r.repeating = o.value(QLatin1String("repeating")).toBool();
    r.organizer = o.value(QLatin1String("organizer")).toString();
    const auto people = o.value(QLatin1String("attendees")).toArray();
    for (const auto &p : people) {
        const auto po = p.toObject();
        r.attendees.append({po.value(QLatin1String("a")).toString(), po.value(QLatin1String("p")).toString()});
    }
    r.agentId = o.value(QLatin1String("agent")).toString();
    r.categories = toList(o.value(QLatin1String("categories")));
    return r;
}

QJsonObject CalendarSnapshot::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("fetchedAt"), dateTimeToString(fetchedAt));
    o.insert(QStringLiteral("principal"), principalHref);
    o.insert(QStringLiteral("displayName"), displayName);
    o.insert(QStringLiteral("addresses"), toArray(addresses));
    o.insert(QStringLiteral("defaultCalendar"), defaultCalendarUri);
    QJsonArray cals;
    for (const auto &c : calendars) {
        cals.append(c.toJson());
    }
    o.insert(QStringLiteral("calendars"), cals);
    QJsonArray recs;
    for (const auto &r : records) {
        recs.append(r.toJson());
    }
    o.insert(QStringLiteral("records"), recs);
    o.insert(QStringLiteral("failed"), toArray(failedCalendars));
    o.insert(QStringLiteral("windowFrom"), dateToString(windowFrom));
    o.insert(QStringLiteral("windowTo"), dateToString(windowTo));
    return o;
}

CalendarSnapshot CalendarSnapshot::fromJson(const QJsonObject &o)
{
    CalendarSnapshot s;
    s.fetchedAt = dateTimeFromString(o.value(QLatin1String("fetchedAt")).toString());
    s.principalHref = o.value(QLatin1String("principal")).toString();
    s.displayName = o.value(QLatin1String("displayName")).toString();
    s.addresses = toList(o.value(QLatin1String("addresses")));
    s.defaultCalendarUri = o.value(QLatin1String("defaultCalendar")).toString();
    const auto cals = o.value(QLatin1String("calendars")).toArray();
    for (const auto &c : cals) {
        s.calendars.append(CalendarInfo::fromJson(c.toObject()));
    }
    const auto recs = o.value(QLatin1String("records")).toArray();
    for (const auto &r : recs) {
        s.records.append(CalRecord::fromJson(r.toObject()));
    }
    s.failedCalendars = toList(o.value(QLatin1String("failed")));
    s.windowFrom = dateFromString(o.value(QLatin1String("windowFrom")).toString());
    s.windowTo = dateFromString(o.value(QLatin1String("windowTo")).toString());
    s.loaded = s.fetchedAt.isValid();
    return s;
}

const CalendarInfo *CalendarSnapshot::calendar(const QString &uri) const
{
    for (const auto &c : calendars) {
        if (c.uri == uri) {
            return &c;
        }
    }
    return nullptr;
}

} // namespace OCC::Nourax::Agenda
