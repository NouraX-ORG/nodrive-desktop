/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "calendarreader.h"

#include "../ical/icaltext.h"
#include "../ical/icaltime.h"
#include "../model/agendacolors.h"

#include <QLoggingCategory>
#include <QUrl>

#include <memory>

namespace OCC::Nourax::Agenda {

Q_LOGGING_CATEGORY(lcAgendaCal, "nodrive.agenda.calendar", QtInfoMsg)

namespace {

QString mailAddress(const QString &value)
{
    QString v = value.trimmed();
    if (v.startsWith(QLatin1String("mailto:"), Qt::CaseInsensitive)) {
        v.remove(0, 7);
    }
    return v.toLower();
}

QString lastSegment(const QString &href)
{
    auto parts = href.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        return QString();
    }
    return QUrl::fromPercentEncoding(parts.last().toUtf8());
}

CalRecord fromComponent(const QString &calendarUri, const QString &href, const QString &etag, const ICalComponent &c, bool isTodo, const QTimeZone &zone, bool *valid)
{
    CalRecord r;
    *valid = false;
    r.calendarUri = calendarUri;
    r.href = href;
    r.etag = etag;
    r.isTodo = isTodo;
    r.uid = c.text(QStringLiteral("UID"));
    r.summary = c.text(QStringLiteral("SUMMARY"));
    r.description = c.text(QStringLiteral("DESCRIPTION")).left(2000);
    r.location = c.text(QStringLiteral("LOCATION"));
    r.status = c.text(QStringLiteral("STATUS")).toUpper();
    r.transp = c.text(QStringLiteral("TRANSP")).toUpper();
    r.agentId = c.text(QStringLiteral("X-NOURAX-AGENT-ID")).trimmed();
    const auto categories = c.all(QStringLiteral("CATEGORIES"));
    for (const auto *p : categories) {
        r.categories.append(p->textList());
    }
    if (const auto *rid = c.first(QStringLiteral("RECURRENCE-ID"))) {
        r.recurrenceId = rid->raw;
    }
    r.repeating = !r.recurrenceId.isEmpty() || c.has(QStringLiteral("RRULE")) || c.has(QStringLiteral("RDATE"));
    if (const auto *org = c.first(QStringLiteral("ORGANIZER"))) {
        r.organizer = mailAddress(org->raw);
    }
    const auto attendees = c.all(QStringLiteral("ATTENDEE"));
    for (const auto *a : attendees) {
        r.attendees.append({mailAddress(a->raw), a->param(QStringLiteral("PARTSTAT")).toUpper()});
    }

    const auto *dtstartProp = c.first(QStringLiteral("DTSTART"));
    const ICalTime dtstart = dtstartProp ? ICalTime::parse(*dtstartProp, zone) : ICalTime();

    if (!isTodo) {
        if (!dtstart.valid) {
            return r;
        }
        const auto *dtendProp = c.first(QStringLiteral("DTEND"));
        const ICalTime dtend = dtendProp ? ICalTime::parse(*dtendProp, zone) : ICalTime();
        if (dtstart.isDate) {
            r.allDay = true;
            r.startDate = dtstart.date;
            r.endDate = dtstart.date;
            if (dtend.valid && dtend.isDate && dtend.date > dtstart.date) {
                r.endDate = dtend.date.addDays(-1);
            } else if (const auto *dur = c.first(QStringLiteral("DURATION"))) {
                const auto secs = ICalTime::parseDurationSeconds(dur->raw);
                if (secs > 86400) {
                    r.endDate = dtstart.date.addDays(static_cast<int>((secs - 1) / 86400));
                }
            }
        } else {
            r.start = dtstart.utc;
            r.timeApproximated = dtstart.approximated;
            r.end = r.start;
            if (dtend.valid && !dtend.isDate) {
                r.end = dtend.utc;
            } else if (const auto *dur = c.first(QStringLiteral("DURATION"))) {
                const auto secs = ICalTime::parseDurationSeconds(dur->raw);
                if (secs > 0) {
                    r.end = r.start.addSecs(secs);
                }
            }
        }
        *valid = true;
        return r;
    }

    // To-do
    if (dtstart.valid) {
        if (dtstart.isDate) {
            r.allDay = true;
            r.startDate = dtstart.date;
        } else {
            r.start = dtstart.utc;
            r.timeApproximated = dtstart.approximated;
        }
    }
    if (const auto *dueProp = c.first(QStringLiteral("DUE"))) {
        const ICalTime due = ICalTime::parse(*dueProp, zone);
        if (due.valid) {
            if (due.isDate) {
                r.dueIsDate = true;
                r.dueDate = due.date;
            } else {
                r.dueTime = due.utc;
                r.timeApproximated = r.timeApproximated || due.approximated;
            }
        }
    }
    const auto percent = c.text(QStringLiteral("PERCENT-COMPLETE"));
    if (!percent.isEmpty()) {
        r.percent = percent.toInt();
    }
    r.completed = c.has(QStringLiteral("COMPLETED")) || r.status == QLatin1String("COMPLETED") || r.percent == 100;
    *valid = true;
    return r;
}

struct ReadState
{
    HttpTransport *transport = nullptr;
    CalendarReadOptions options;
    CalendarSnapshot previous;
    CalendarSnapshot result;
    std::function<void(const CalendarReadResult &)> done;
    QList<CalendarInfo> toRead;
    int index = 0;
    bool isFinished = false;
};

HttpRequest davRequest(HttpTransport *t, const QByteArray &verb, const QString &href, int depth, const QByteArray &body)
{
    auto r = makeRequest(verb, t->urlForHref(href));
    r.headers.append({QByteArrayLiteral("Depth"), QByteArray::number(depth)});
    r = withBody(r, body, QByteArrayLiteral("application/xml; charset=utf-8"));
    return r;
}

void finish(const std::shared_ptr<ReadState> &s, bool ok, int status, bool networkError)
{
    if (s->isFinished) {
        return;
    }
    s->isFinished = true;
    CalendarReadResult result;
    result.ok = ok;
    result.status = status;
    result.networkError = networkError;
    result.snapshot = ok ? s->result : s->previous;
    s->done(result);
}

void readNext(const std::shared_ptr<ReadState> &s);

void readTodos(const std::shared_ptr<ReadState> &s, const CalendarInfo &cal)
{
    auto *t = s->transport;
    t->send(davRequest(t, "REPORT", cal.href, 1, CalDavXml::todoQueryBody()), [s, cal](const HttpResult &r) {
        if (r.multistatus()) {
            const auto ms = CalDavXml::parseMultistatus(r.body);
            for (const auto &resp : ms.responses) {
                const auto data = resp.props.value(Dav::key(Dav::nsCalDav(), QStringLiteral("calendar-data")));
                if (data.text.isEmpty()) {
                    continue;
                }
                const auto etag = resp.props.value(Dav::key(Dav::nsDav(), QStringLiteral("getetag"))).text;
                s->result.records.append(CalendarReader::parseObject(cal.uri, resp.href, etag, data.text, true, s->options.zone));
            }
        } else {
            qCInfo(lcAgendaCal) << "to-do query answered" << r.status;
            s->result.failedCalendars.append(cal.uri);
        }
        ++s->index;
        readNext(s);
    });
}

void readEvents(const std::shared_ptr<ReadState> &s, const CalendarInfo &cal)
{
    auto *t = s->transport;
    const QDateTime from(s->options.from, QTime(0, 0), QTimeZone::UTC);
    const QDateTime to(s->options.to.addDays(1), QTime(0, 0), QTimeZone::UTC);
    t->send(davRequest(t, "REPORT", cal.href, 1, CalDavXml::eventQueryBody(from, to)), [s, cal](const HttpResult &r) {
        if (r.multistatus()) {
            const auto ms = CalDavXml::parseMultistatus(r.body);
            for (const auto &resp : ms.responses) {
                const auto data = resp.props.value(Dav::key(Dav::nsCalDav(), QStringLiteral("calendar-data")));
                if (data.text.isEmpty()) {
                    continue;
                }
                const auto etag = resp.props.value(Dav::key(Dav::nsDav(), QStringLiteral("getetag"))).text;
                s->result.records.append(CalendarReader::parseObject(cal.uri, resp.href, etag, data.text, false, s->options.zone));
            }
        } else {
            qCInfo(lcAgendaCal) << "event query answered" << r.status;
            s->result.failedCalendars.append(cal.uri);
        }
        if (cal.hasTodos) {
            readTodos(s, cal);
        } else {
            ++s->index;
            readNext(s);
        }
    });
}

void readNext(const std::shared_ptr<ReadState> &s)
{
    while (s->index < s->toRead.size()) {
        const auto cal = s->toRead.at(s->index);
        const auto *old = s->previous.calendar(cal.uri);
        const bool unchanged = !s->options.forceFull && old && !cal.syncToken.isEmpty() && old->syncToken == cal.syncToken
            && s->previous.windowFrom == s->options.from && s->previous.windowTo == s->options.to
            && !s->previous.failedCalendars.contains(cal.uri);
        const bool unchangedCtag = !s->options.forceFull && old && cal.syncToken.isEmpty() && !cal.ctag.isEmpty() && old->ctag == cal.ctag
            && s->previous.windowFrom == s->options.from && s->previous.windowTo == s->options.to
            && !s->previous.failedCalendars.contains(cal.uri);
        if (unchanged || unchangedCtag) {
            for (const auto &rec : s->previous.records) {
                if (rec.calendarUri == cal.uri) {
                    s->result.records.append(rec);
                }
            }
            ++s->index;
            continue;
        }
        if (cal.hasEvents) {
            readEvents(s, cal);
        } else if (cal.hasTodos) {
            readTodos(s, cal);
        } else {
            ++s->index;
            continue;
        }
        return;
    }
    // A calendar that could not be read keeps its old records.
    for (const auto &failed : std::as_const(s->result.failedCalendars)) {
        for (const auto &rec : s->previous.records) {
            if (rec.calendarUri == failed) {
                s->result.records.append(rec);
            }
        }
    }
    s->result.loaded = true;
    finish(s, true, 0, false);
}

} // namespace

QList<QPair<QString, QString>> CalendarReader::principalProps()
{
    return {
        {Dav::nsDav(), QStringLiteral("displayname")},
        {Dav::nsCalDav(), QStringLiteral("calendar-home-set")},
        {Dav::nsCalDav(), QStringLiteral("calendar-user-address-set")},
        {Dav::nsCalDav(), QStringLiteral("schedule-default-calendar-URL")},
    };
}

QList<QPair<QString, QString>> CalendarReader::calendarProps()
{
    return {
        {Dav::nsDav(), QStringLiteral("resourcetype")},
        {Dav::nsDav(), QStringLiteral("displayname")},
        {Dav::nsApple(), QStringLiteral("calendar-color")},
        {Dav::nsCalDav(), QStringLiteral("supported-calendar-component-set")},
        {Dav::nsDav(), QStringLiteral("sync-token")},
        {Dav::nsCs(), QStringLiteral("getctag")},
        {Dav::nsOc(), QStringLiteral("owner-principal")},
        {Dav::nsOc(), QStringLiteral("read-only")},
        {Dav::nsNc(), QStringLiteral("owner-displayname")},
        {Dav::nsDav(), QStringLiteral("current-user-privilege-set")},
        {Dav::nsCalDav(), QStringLiteral("schedule-calendar-transp")},
    };
}

void CalendarReader::syncWindow(const QDate &today, QDate *from, QDate *to)
{
    const QDate firstOfMonth(today.year(), today.month(), 1);
    *from = firstOfMonth.addMonths(-1);
    *to = firstOfMonth.addMonths(13).addDays(-1);
}

CalendarInfo CalendarReader::classify(const DavResponse &response, const QString &davUser)
{
    CalendarInfo c;
    c.href = response.href;
    c.uri = lastSegment(response.href);
    const auto type = response.props.value(Dav::key(Dav::nsDav(), QStringLiteral("resourcetype")));
    c.subscribed = type.has(QStringLiteral("subscribed"));
    const bool isCalendar = type.has(QStringLiteral("calendar")) || c.subscribed;
    if (!isCalendar || type.has(QStringLiteral("schedule-inbox")) || type.has(QStringLiteral("schedule-outbox"))
        || c.uri == QLatin1String("trashbin") || c.uri == QLatin1String("inbox") || c.uri == QLatin1String("outbox")) {
        c.ignored = true;
    }
    c.name = response.props.value(Dav::key(Dav::nsDav(), QStringLiteral("displayname"))).text;
    if (c.name.isEmpty()) {
        c.name = c.uri;
    }
    c.color = sourceColor(response.props.value(Dav::key(Dav::nsApple(), QStringLiteral("calendar-color"))).text);

    const auto comps = response.props.value(Dav::key(Dav::nsCalDav(), QStringLiteral("supported-calendar-component-set"))).compNames;
    if (comps.isEmpty()) {
        c.hasEvents = true; // subscriptions and old servers do not list components
    }
    for (const auto &name : comps) {
        if (name.compare(QLatin1String("VEVENT"), Qt::CaseInsensitive) == 0) {
            c.hasEvents = true;
        } else if (name.compare(QLatin1String("VTODO"), Qt::CaseInsensitive) == 0) {
            c.hasTodos = true;
        }
    }
    c.syncToken = response.props.value(Dav::key(Dav::nsDav(), QStringLiteral("sync-token"))).text;
    c.ctag = response.props.value(Dav::key(Dav::nsCs(), QStringLiteral("getctag"))).text;

    const auto owner = response.props.value(Dav::key(Dav::nsOc(), QStringLiteral("owner-principal")));
    c.ownerPrincipal = !owner.hrefs.isEmpty() ? owner.hrefs.first() : owner.text;
    c.ownerName = response.props.value(Dav::key(Dav::nsNc(), QStringLiteral("owner-displayname"))).text;
    QString ownerPath = c.ownerPrincipal;
    while (ownerPath.endsWith(QLatin1Char('/'))) {
        ownerPath.chop(1);
    }
    if (ownerPath.isEmpty()) {
        c.mine = !c.uri.contains(QLatin1String("_shared_by_")) && !c.uri.startsWith(QLatin1String("app-generated--"));
    } else {
        c.mine = ownerPath.endsWith(QString(QLatin1String("/users/") + davUser));
    }

    const auto readOnly = response.props.value(Dav::key(Dav::nsOc(), QStringLiteral("read-only"))).text.trimmed();
    c.readOnly = readOnly == QLatin1String("1") || readOnly.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
    const auto privileges = response.props.value(Dav::key(Dav::nsDav(), QStringLiteral("current-user-privilege-set")));
    if (!privileges.descendants.isEmpty() && !privileges.has(QStringLiteral("write")) && !privileges.has(QStringLiteral("write-content"))
        && !privileges.has(QStringLiteral("all"))) {
        c.readOnly = true;
    }
    if (c.subscribed) {
        c.readOnly = true;
    }
    c.birthdays = c.uri == QLatin1String("contact_birthdays");
    c.deckBoard = c.uri.startsWith(QLatin1String("app-generated--deck--"));
    c.transparent = response.props.value(Dav::key(Dav::nsCalDav(), QStringLiteral("schedule-calendar-transp"))).has(QStringLiteral("transparent"));
    return c;
}

QList<CalRecord> CalendarReader::parseObject(const QString &calendarUri, const QString &href, const QString &etag, const QString &ics, bool forTodos, const QTimeZone &floatingZone)
{
    QList<CalRecord> out;
    const auto components = ICalText::parse(ics);
    const QString wanted = forTodos ? QStringLiteral("VTODO") : QStringLiteral("VEVENT");
    for (const auto &c : components) {
        if (c.type != wanted) {
            continue;
        }
        bool valid = false;
        auto record = fromComponent(calendarUri, href, etag, c, forTodos, floatingZone, &valid);
        if (valid) {
            out.append(record);
        }
    }
    return out;
}

void CalendarReader::read(HttpTransport *transport, const CalendarReadOptions &options, const CalendarSnapshot &previous,
    const std::function<void(const CalendarReadResult &)> &done)
{
    auto state = std::make_shared<ReadState>();
    state->transport = transport;
    state->options = options;
    state->previous = previous;
    state->done = done;
    state->result.fetchedAt = options.now.isValid() ? options.now : QDateTime::currentDateTimeUtc();
    state->result.windowFrom = options.from;
    state->result.windowTo = options.to;

    const QString principalPath = QStringLiteral("remote.php/dav/principals/users/") + QString::fromUtf8(QUrl::toPercentEncoding(options.davUser)) + QLatin1Char('/');
    auto principalRequest = makeRequest("PROPFIND", transport->url(principalPath));
    principalRequest.headers.append({QByteArrayLiteral("Depth"), QByteArrayLiteral("0")});
    principalRequest = withBody(principalRequest, CalDavXml::propfindBody(principalProps()), QByteArrayLiteral("application/xml; charset=utf-8"));

    transport->send(principalRequest, [state, transport, options](const HttpResult &r) {
        if (!r.multistatus()) {
            finish(state, false, r.status, r.networkError);
            return;
        }
        const auto ms = CalDavXml::parseMultistatus(r.body);
        if (ms.responses.isEmpty()) {
            finish(state, false, r.status, false);
            return;
        }
        const auto &principal = ms.responses.first();
        state->result.principalHref = principal.href;
        state->result.displayName = principal.props.value(Dav::key(Dav::nsDav(), QStringLiteral("displayname"))).text;
        const auto addresses = principal.props.value(Dav::key(Dav::nsCalDav(), QStringLiteral("calendar-user-address-set"))).hrefs;
        for (const auto &a : addresses) {
            state->result.addresses.append(mailAddress(a));
        }
        const auto def = principal.props.value(Dav::key(Dav::nsCalDav(), QStringLiteral("schedule-default-calendar-URL"))).hrefs;
        if (!def.isEmpty()) {
            state->result.defaultCalendarUri = lastSegment(def.first());
        }
        auto homes = principal.props.value(Dav::key(Dav::nsCalDav(), QStringLiteral("calendar-home-set"))).hrefs;
        QString home = homes.isEmpty() ? QString() : homes.first();
        if (home.isEmpty()) {
            home = transport->url(QStringLiteral("remote.php/dav/calendars/") + QString::fromUtf8(QUrl::toPercentEncoding(options.davUser)) + QLatin1Char('/')).path(QUrl::FullyEncoded);
        }
        transport->send(davRequest(transport, "PROPFIND", home, 1, CalDavXml::propfindBody(calendarProps())), [state](const HttpResult &homeReply) {
            if (!homeReply.multistatus()) {
                finish(state, false, homeReply.status, homeReply.networkError);
                return;
            }
            const auto homeMs = CalDavXml::parseMultistatus(homeReply.body);
            for (const auto &resp : homeMs.responses) {
                auto info = classify(resp, state->options.davUser);
                if (info.uri.isEmpty() || info.ignored) {
                    continue;
                }
                state->result.calendars.append(info);
                const bool wanted = !info.deckBoard && (!state->options.wanted || state->options.wanted(info));
                if (wanted && (info.hasEvents || info.hasTodos)) {
                    state->toRead.append(info);
                }
            }
            readNext(state);
        });
    });
}

} // namespace OCC::Nourax::Agenda
