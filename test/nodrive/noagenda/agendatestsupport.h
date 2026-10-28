/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// Helpers of the NoAgenda tests: the fixtures (made-up data in the shape of NoGantt 2.4.1, Deck 1.19.1 and
// the server's CalDAV answers), a fake transport that answers from them, and the scenario "Ali's day".
// Today in every scenario is Wednesday 2026-10-28.

#pragma once

#include "gui/nourax/noagenda/accountagenda.h"
#include "gui/nourax/noagenda/caldav/calendarreader.h"
#include "gui/nourax/noagenda/deck/deckclient.h"
#include "gui/nourax/noagenda/gantt/ganttclient.h"
#include "gui/nourax/noagenda/logic/agendabuilder.h"
#include "gui/nourax/noagenda/ocs/serverinfo.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

namespace AgendaTest {

using namespace OCC::Nourax::Agenda;

inline QString fixtureDir()
{
    return qEnvironmentVariable("NODRIVE_AGENDA_FIXTURES", QStringLiteral(NODRIVE_AGENDA_FIXTURES_DIR));
}

inline QByteArray fixture(const QString &relative)
{
    QFile file(fixtureDir() + QLatin1Char('/') + relative);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "missing fixture" << relative;
        return QByteArray();
    }
    return file.readAll();
}

inline QJsonValue fixtureJson(const QString &relative)
{
    const auto doc = QJsonDocument::fromJson(fixture(relative));
    return doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(doc.array());
}

/// Wraps a JSON payload in the OCS envelope.
inline QByteArray ocsEnvelope(const QByteArray &data)
{
    return QByteArrayLiteral("{\"ocs\":{\"meta\":{\"status\":\"ok\",\"statuscode\":200,\"message\":\"OK\"},\"data\":") + data + QByteArrayLiteral("}}");
}

inline QDateTime fixedNow()
{
    return QDateTime(QDate(2026, 10, 28), QTime(12, 0), QTimeZone::UTC);
}

/// Answers from canned bodies. Callbacks run at once, so the readers finish without an event loop.
class FakeTransport : public HttpTransport
{
public:
    struct Answer
    {
        QByteArray verb;
        QString path;
        QByteArray bodyContains;
        int status = 200;
        QByteArray body;
        QMap<QByteArray, QByteArray> headers;
    };

    void route(const QByteArray &verb, const QString &path, int status, const QByteArray &body, const QByteArray &bodyContains = QByteArray())
    {
        answers.append({verb, path, bodyContains, status, body, {}});
    }
    void routeFile(const QByteArray &verb, const QString &path, int status, const QString &fixtureFile, const QByteArray &bodyContains = QByteArray())
    {
        route(verb, path, status, fixture(fixtureFile), bodyContains);
    }
    void routeOcs(const QString &path, const QString &fixtureFile)
    {
        route("GET", QStringLiteral("/ocs/v2.php/apps/nc_gantt/api/v1/") + path, 200, ocsEnvelope(fixture(fixtureFile)));
    }
    void clear() { answers.clear(); }
    int count(const QByteArray &verb, const QString &path) const
    {
        int n = 0;
        for (const auto &r : requests) {
            if (r.verb == verb && r.url.path() == path) {
                ++n;
            }
        }
        return n;
    }

    void send(const HttpRequest &request, const HttpCallback &done) override
    {
        requests.append(request);
        HttpResult result;
        if (offline) {
            result.networkError = true;
            done(result);
            return;
        }
        const auto path = request.url.path();
        for (const auto &a : std::as_const(answers)) {
            if (a.verb == request.verb && a.path == path && (a.bodyContains.isEmpty() || request.body.contains(a.bodyContains))) {
                result.status = a.status;
                result.body = a.body;
                result.headers = a.headers;
                done(result);
                return;
            }
        }
        result.status = 404;
        done(result);
    }
    [[nodiscard]] QUrl serverUrl() const override { return QUrl(QStringLiteral("https://drive.example.test")); }
    [[nodiscard]] QString davUser() const override { return user; }

    QString user = QStringLiteral("ali");
    bool offline = false;
    QList<Answer> answers;
    QList<HttpRequest> requests;
};

/// Routes of the whole scenario for one person: "ali" (staff) or "mara" (manager).
inline void routeScenario(FakeTransport *t, const QString &user)
{
    t->user = user;
    t->routeFile("GET", QStringLiteral("/remote.php/dav/files/") + user + QStringLiteral("/NoAgenda/agent-registry.json"), 200, QStringLiteral("agent-registry.json"));
    t->route("GET", QStringLiteral("/ocs/v2.php/cloud/user"), 200, ocsEnvelope(fixture(QStringLiteral("server/user-%1.json").arg(user))));
    t->route("GET", QStringLiteral("/ocs/v1.php/cloud/capabilities"), 200, ocsEnvelope(fixture(QStringLiteral("server/capabilities.json"))));

    // NoGantt
    t->routeOcs(QStringLiteral("my-tasks"), QStringLiteral("gantt/my-tasks-%1.json").arg(user));
    t->routeOcs(QStringLiteral("projects"), QStringLiteral("gantt/projects-%1.json").arg(user));
    t->routeOcs(QStringLiteral("resources"), QStringLiteral("gantt/resources.json"));
    t->routeOcs(QStringLiteral("work-schedules"), QStringLiteral("gantt/work-schedules.json"));
    t->routeOcs(QStringLiteral("holidays"), QStringLiteral("gantt/holidays.json"));
    t->routeOcs(QStringLiteral("work-calendars"), QStringLiteral("gantt/work-calendars.json"));
    t->routeOcs(QStringLiteral("settings"), QStringLiteral("gantt/settings.json"));
    t->routeOcs(QStringLiteral("task-sync"), QStringLiteral("gantt/task-sync-off.json"));
    t->routeOcs(QStringLiteral("capacity-check"), user == QLatin1String("mara") ? QStringLiteral("gantt/capacity-mara.json") : QStringLiteral("gantt/capacity-empty.json"));
    for (const auto *id : {"1", "2"}) {
        const auto n = QString::fromLatin1(id);
        t->routeOcs(QStringLiteral("projects/") + n + QStringLiteral("/tasks"), QStringLiteral("gantt/tasks-") + n + QStringLiteral(".json"));
        t->routeOcs(QStringLiteral("projects/") + n + QStringLiteral("/milestones"), QStringLiteral("gantt/milestones-") + n + QStringLiteral(".json"));
        t->routeOcs(QStringLiteral("projects/") + n + QStringLiteral("/subprojects"), QStringLiteral("gantt/subprojects-") + n + QStringLiteral(".json"));
        t->routeOcs(QStringLiteral("projects/") + n + QStringLiteral("/shares"), QStringLiteral("gantt/shares-") + n + QStringLiteral(".json"));
    }

    // Deck
    t->routeFile("GET", QStringLiteral("/index.php/apps/deck/api/v1.0/boards"), 200, QStringLiteral("deck/boards-%1.json").arg(user));
    t->routeFile("GET", QStringLiteral("/index.php/apps/deck/api/v1.0/boards/10/stacks"), 200, QStringLiteral("deck/stacks-10.json"));

    // CalDAV (Ali has the calendars of the scenario, Mara one empty calendar)
    if (user == QLatin1String("ali")) {
        t->routeFile("PROPFIND", QStringLiteral("/remote.php/dav/principals/users/ali/"), 207, QStringLiteral("caldav/principal-ali.xml"));
        t->routeFile("PROPFIND", QStringLiteral("/remote.php/dav/calendars/ali/"), 207, QStringLiteral("caldav/home-ali.xml"));
        const auto base = QStringLiteral("/remote.php/dav/calendars/ali/");
        t->routeFile("REPORT", base + QStringLiteral("personal/"), 207, QStringLiteral("caldav/events-personal.xml"), "VEVENT");
        t->routeFile("REPORT", base + QStringLiteral("personal/"), 207, QStringLiteral("caldav/todos-personal.xml"), "VTODO");
        t->routeFile("REPORT", base + QStringLiteral("work/"), 207, QStringLiteral("caldav/events-work.xml"), "VEVENT");
        t->routeFile("REPORT", base + QStringLiteral("mara-team_shared_by_mara/"), 207, QStringLiteral("caldav/events-team.xml"), "VEVENT");
        t->routeFile("REPORT", base + QStringLiteral("holidays/"), 207, QStringLiteral("caldav/events-holidays.xml"), "VEVENT");
        t->routeFile("REPORT", base + QStringLiteral("contact_birthdays/"), 207, QStringLiteral("caldav/events-birthdays.xml"), "VEVENT");
        t->routeFile("REPORT", base + QStringLiteral("ali-task-copy/"), 207, QStringLiteral("caldav/empty.xml"), "VEVENT");
    } else if (user == QLatin1String("mara")) {
        // Mara has one calendar and no events in the fixtures.
        t->routeFile("PROPFIND", QStringLiteral("/remote.php/dav/principals/users/mara/"), 207, QStringLiteral("caldav/principal-mara.xml"));
        t->routeFile("PROPFIND", QStringLiteral("/remote.php/dav/calendars/mara/"), 207, QStringLiteral("caldav/home-mara.xml"));
        const auto base = QStringLiteral("/remote.php/dav/calendars/mara/personal/");
        t->routeFile("REPORT", base, 207, QStringLiteral("caldav/empty.xml"), "VEVENT");
        t->routeFile("REPORT", base, 207, QStringLiteral("caldav/empty.xml"), "VTODO");
    }
}

/// Reads the scenario of one person through the real readers (with the fake transport) into a build input.
inline BuildInput loadScenario(const QString &user, FakeTransport *transport = nullptr, const QTimeZone &zone = QTimeZone(QTimeZone::UTC))
{
    FakeTransport local;
    FakeTransport *t = transport ? transport : &local;
    routeScenario(t, user);

    BuildInput input;
    input.davUser = user;
    input.zone = zone;
    input.now = fixedNow();
    input.today = QDate(2026, 10, 28);
    input.registry = AgentRegistry::parse(fixture(QStringLiteral("agent-registry.json")));

    ServerInfoClient::fetch(t, ServerInfo(), [&input](const ServerInfoResult &r) { input.server = r.info; });

    CalendarReadOptions calendar;
    calendar.davUser = user;
    calendar.zone = zone;
    CalendarReader::syncWindow(input.today, &calendar.from, &calendar.to);
    calendar.now = input.now;
    CalendarReader::read(t, calendar, CalendarSnapshot(), [&input](const CalendarReadResult &r) { input.calendar = r.snapshot; });

    GanttFetchOptions gantt;
    gantt.davUser = user;
    gantt.serverAdmin = input.server.isServerAdmin;
    gantt.groups = input.server.groups;
    gantt.now = input.now;
    gantt.wantProject = [](const ProjectRelation &) { return true; };
    GanttClient::fetch(t, gantt, GanttSnapshot(), [&input](const GanttFetchResult &r) { input.gantt = r.snapshot; });

    DeckFetchOptions deck;
    deck.davUser = user;
    deck.version = input.server.deckVersion;
    deck.now = input.now;
    deck.wantBoard = [](const BoardRelation &) { return true; };
    DeckClient::fetch(t, deck, DeckSnapshot(), [&input](const DeckFetchResult &r) { input.deck = r.snapshot; });
    return input;
}

/// Ali after he answered "which calendars are personal?" with just "Personal" (the others count as Work).
inline BuildInput loadAliAnswered()
{
    auto input = loadScenario(QStringLiteral("ali"));
    input.settings.answerPersonalQuestion({QStringLiteral("personal")}, {QStringLiteral("personal"), QStringLiteral("work"), QStringLiteral("ali-task-copy")}, false);
    return input;
}

inline const AgendaItem *find(const QList<AgendaItem> &items, const QString &key)
{
    for (const auto &i : items) {
        if (i.key == key) {
            return &i;
        }
    }
    return nullptr;
}

inline const AgendaItem *findTitle(const QList<AgendaItem> &items, const QString &title)
{
    for (const auto &i : items) {
        if (i.title == title) {
            return &i;
        }
    }
    return nullptr;
}

inline bool hasProblem(const AgendaItem &item, ProblemFlag::Kind kind)
{
    for (const auto &p : item.problems) {
        if (p.kind == kind) {
            return true;
        }
    }
    return false;
}

inline QString robot()
{
    return QString::fromUcs4(U"\U0001F916");
}

} // namespace AgendaTest
