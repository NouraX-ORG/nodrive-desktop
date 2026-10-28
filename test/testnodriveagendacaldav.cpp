/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: CalDAV request bodies, multistatus parsing, calendar classification and the reader.

#include "nodrive/noagenda/agendatestsupport.h"

#include "gui/nourax/noagenda/caldav/caldavxml.h"

#include <QXmlStreamReader>

using namespace AgendaTest;

class TestNoDriveAgendaCalDav : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void propfindBodyNamesEveryProperty()
    {
        const auto body = QString::fromUtf8(CalDavXml::propfindBody(CalendarReader::calendarProps()));
        QVERIFY(body.contains(QStringLiteral("resourcetype")));
        QVERIFY(body.contains(QStringLiteral("supported-calendar-component-set")));
        QVERIFY(body.contains(QStringLiteral("sync-token")));
        QVERIFY(body.contains(QStringLiteral("getctag")));
        QVERIFY(body.contains(QStringLiteral("owner-principal")));
        QVERIFY(body.contains(QStringLiteral("schedule-calendar-transp")));
        // The answer is well formed XML.
        QXmlStreamReader reader(body);
        while (!reader.atEnd()) {
            reader.readNext();
        }
        QVERIFY2(!reader.hasError(), qPrintable(reader.errorString()));
    }

    void eventQueryIsExpandedAndTimeRanged()
    {
        const auto from = QDateTime(QDate(2026, 9, 1), QTime(0, 0), QTimeZone::UTC);
        const auto to = QDateTime(QDate(2027, 10, 1), QTime(0, 0), QTimeZone::UTC);
        const auto body = QString::fromUtf8(CalDavXml::eventQueryBody(from, to));
        QVERIFY(body.contains(QStringLiteral("expand")));
        QVERIFY(body.contains(QStringLiteral("start=\"20260901T000000Z\"")));
        QVERIFY(body.contains(QStringLiteral("end=\"20271001T000000Z\"")));
        QVERIFY(body.contains(QStringLiteral("VEVENT")));
        QVERIFY(!body.contains(QStringLiteral("VTODO")));
    }

    void todoQueryIsNeverExpanded()
    {
        const auto body = QString::fromUtf8(CalDavXml::todoQueryBody());
        QVERIFY(body.contains(QStringLiteral("VTODO")));
        QVERIFY(body.contains(QStringLiteral("COMPLETED")));
        QVERIFY(body.contains(QStringLiteral("is-not-defined")));
        QVERIFY2(!body.contains(QStringLiteral("expand")), "the server drops every VTODO on expand");
    }

    void parsesAMultistatus()
    {
        const auto ms = CalDavXml::parseMultistatus(fixture(QStringLiteral("caldav/home-ali.xml")));
        QVERIFY(ms.valid);
        QCOMPARE(ms.responses.size(), 10);
        const auto &personal = ms.responses.at(1);
        QCOMPARE(personal.href, QStringLiteral("/remote.php/dav/calendars/ali/personal/"));
        QCOMPARE(personal.props.value(Dav::key(Dav::nsDav(), QStringLiteral("displayname"))).text, QStringLiteral("Personal"));
        const auto comps = personal.props.value(Dav::key(Dav::nsCalDav(), QStringLiteral("supported-calendar-component-set"))).compNames;
        QCOMPARE(comps, (QStringList{QStringLiteral("VEVENT"), QStringLiteral("VTODO")}));
        QVERIFY(personal.props.value(Dav::key(Dav::nsDav(), QStringLiteral("resourcetype"))).has(QStringLiteral("calendar")));
        QCOMPARE(personal.props.value(Dav::key(Dav::nsDav(), QStringLiteral("sync-token"))).text, QStringLiteral("http://sabre.io/ns/sync/t-personal-1"));
    }

    void aRemovedHrefHasAStatusOfItsOwn()
    {
        const auto ms = CalDavXml::parseMultistatus(QByteArrayLiteral(
            "<d:multistatus xmlns:d=\"DAV:\"><d:response><d:href>/a/b.ics</d:href><d:status>HTTP/1.1 404 Not Found</d:status></d:response>"
            "<d:sync-token>http://sabre.io/ns/sync/9</d:sync-token></d:multistatus>"));
        QVERIFY(ms.valid);
        QCOMPARE(ms.responses.size(), 1);
        QCOMPARE(ms.responses.first().status, 404);
        QCOMPARE(ms.syncToken, QStringLiteral("http://sabre.io/ns/sync/9"));
    }

    void anErrorBodyIsNotAMultistatus()
    {
        const auto ms = CalDavXml::parseMultistatus(QByteArrayLiteral("<d:error xmlns:d=\"DAV:\"><d:valid-sync-token/></d:error>"));
        QVERIFY(!ms.valid);
        QCOMPARE(ms.errorCondition, QStringLiteral("valid-sync-token"));
        QVERIFY(!CalDavXml::parseMultistatus("not xml at all").valid);
    }

    void propertiesAnsweredWith404AreMissing()
    {
        const auto ms = CalDavXml::parseMultistatus(QByteArrayLiteral(
            "<d:multistatus xmlns:d=\"DAV:\" xmlns:oc=\"http://owncloud.org/ns\"><d:response><d:href>/x/</d:href>"
            "<d:propstat><d:prop><d:displayname>X</d:displayname></d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat>"
            "<d:propstat><d:prop><oc:read-only/></d:prop><d:status>HTTP/1.1 404 Not Found</d:status></d:propstat></d:response></d:multistatus>"));
        QCOMPARE(ms.responses.first().props.size(), 1);
        QCOMPARE(ms.responses.first().missing, QStringList{Dav::key(Dav::nsOc(), QStringLiteral("read-only"))});
    }

    void classifiesTheCalendarsOfTheScenario()
    {
        const auto ms = CalDavXml::parseMultistatus(fixture(QStringLiteral("caldav/home-ali.xml")));
        QMap<QString, CalendarInfo> byUri;
        for (const auto &r : ms.responses) {
            const auto info = CalendarReader::classify(r, QStringLiteral("ali"));
            byUri.insert(info.uri, info);
        }
        QVERIFY(byUri.value(QStringLiteral("personal")).mine);
        QVERIFY(byUri.value(QStringLiteral("personal")).hasEvents && byUri.value(QStringLiteral("personal")).hasTodos);
        QVERIFY(!byUri.value(QStringLiteral("personal")).readOnly);
        QCOMPARE(byUri.value(QStringLiteral("personal")).color, QStringLiteral("#795AAF")); // the alpha part is dropped
        QCOMPARE(byUri.value(QStringLiteral("work")).color, QStringLiteral("#2563EB")); // the server's default blue is mapped to cobalt
        QVERIFY(!byUri.value(QStringLiteral("mara-team_shared_by_mara")).mine);
        QCOMPARE(byUri.value(QStringLiteral("mara-team_shared_by_mara")).ownerName, QStringLiteral("Mara"));
        QVERIFY(byUri.value(QStringLiteral("holidays")).subscribed);
        QVERIFY(byUri.value(QStringLiteral("holidays")).readOnly);
        QVERIFY(byUri.value(QStringLiteral("contact_birthdays")).birthdays);
        QVERIFY(byUri.value(QStringLiteral("app-generated--deck--board-10")).deckBoard);
        QVERIFY(byUri.value(QStringLiteral("inbox")).ignored);
        QVERIFY(byUri.value(QStringLiteral("trashbin")).ignored);
        QVERIFY(byUri.value(QStringLiteral("ali")).ignored); // the calendar home itself
    }

    void theWindowIsPreviousMonthToTwelveMonthsAhead()
    {
        QDate from;
        QDate to;
        CalendarReader::syncWindow(QDate(2026, 10, 28), &from, &to);
        QCOMPARE(from, QDate(2026, 9, 1));
        QCOMPARE(to, QDate(2027, 10, 31));
    }

    void readsTheWholeScenarioAndAsksForTodosOnlyWhereTheyBelong()
    {
        FakeTransport transport;
        routeScenario(&transport, QStringLiteral("ali"));
        CalendarReadOptions options;
        options.davUser = QStringLiteral("ali");
        options.zone = QTimeZone(QTimeZone::UTC);
        CalendarReader::syncWindow(QDate(2026, 10, 28), &options.from, &options.to);
        options.now = fixedNow();
        CalendarReadResult result;
        CalendarReader::read(&transport, options, CalendarSnapshot(), [&result](const CalendarReadResult &r) { result = r; });
        QVERIFY(result.ok);
        const auto &snapshot = result.snapshot;
        QVERIFY(snapshot.loaded);
        QCOMPARE(snapshot.displayName, QStringLiteral("Ali"));
        QVERIFY(snapshot.addresses.contains(QStringLiteral("ali@example.test")));
        QCOMPARE(snapshot.defaultCalendarUri, QStringLiteral("personal"));
        QCOMPARE(snapshot.calendars.size(), 7); // home, inbox and trashbin are not calendars
        // The board's calendar is never read (NoAgenda reads the board directly).
        QCOMPARE(transport.count("REPORT", QStringLiteral("/remote.php/dav/calendars/ali/app-generated--deck--board-10/")), 0);
        // Events are asked with expand, to-dos never with expand.
        for (const auto &request : transport.requests) {
            if (request.verb == "REPORT" && request.body.contains("VTODO")) {
                QVERIFY(!request.body.contains("expand"));
            }
            if (request.verb == "REPORT" && request.body.contains("VEVENT")) {
                QVERIFY(request.body.contains("expand"));
            }
        }
        // To-dos are asked only from the calendar that has VTODO.
        QCOMPARE(transport.count("REPORT", QStringLiteral("/remote.php/dav/calendars/ali/personal/")), 2);
        QCOMPARE(transport.count("REPORT", QStringLiteral("/remote.php/dav/calendars/ali/work/")), 1);
        int todos = 0;
        for (const auto &r : snapshot.records) {
            todos += r.isTodo ? 1 : 0;
        }
        QCOMPARE(todos, 6);
        QVERIFY(snapshot.failedCalendars.isEmpty());
    }

    void anUnchangedSyncTokenReusesTheCachedObjects()
    {
        FakeTransport transport;
        routeScenario(&transport, QStringLiteral("ali"));
        CalendarReadOptions options;
        options.davUser = QStringLiteral("ali");
        options.zone = QTimeZone(QTimeZone::UTC);
        CalendarReader::syncWindow(QDate(2026, 10, 28), &options.from, &options.to);
        options.now = fixedNow();
        CalendarReadResult first;
        CalendarReader::read(&transport, options, CalendarSnapshot(), [&first](const CalendarReadResult &r) { first = r; });
        const int reportsAfterFirst = static_cast<int>(transport.requests.size());
        CalendarReadResult second;
        CalendarReader::read(&transport, options, first.snapshot, [&second](const CalendarReadResult &r) { second = r; });
        QVERIFY(second.ok);
        // Two PROPFIND requests only: no REPORT, because no sync token changed.
        QCOMPARE(static_cast<int>(transport.requests.size()) - reportsAfterFirst, 2);
        QCOMPARE(second.snapshot.records.size(), first.snapshot.records.size());
        // A forced full read asks again.
        options.forceFull = true;
        CalendarReader::read(&transport, options, first.snapshot, [&second](const CalendarReadResult &r) { second = r; });
        QVERIFY(static_cast<int>(transport.requests.size()) - reportsAfterFirst > 2);
    }

    void aFailingCalendarKeepsItsOldObjects()
    {
        FakeTransport transport;
        routeScenario(&transport, QStringLiteral("ali"));
        CalendarReadOptions options;
        options.davUser = QStringLiteral("ali");
        options.zone = QTimeZone(QTimeZone::UTC);
        CalendarReader::syncWindow(QDate(2026, 10, 28), &options.from, &options.to);
        options.now = fixedNow();
        options.forceFull = true;
        CalendarReadResult first;
        CalendarReader::read(&transport, options, CalendarSnapshot(), [&first](const CalendarReadResult &r) { first = r; });
        int workBefore = 0;
        for (const auto &r : first.snapshot.records) {
            workBefore += r.calendarUri == QLatin1String("work") ? 1 : 0;
        }
        QVERIFY(workBefore > 0);
        // The work calendar now answers 500.
        for (auto &a : transport.answers) {
            if (a.path == QLatin1String("/remote.php/dav/calendars/ali/work/")) {
                a.status = 500;
            }
        }
        CalendarReadResult second;
        CalendarReader::read(&transport, options, first.snapshot, [&second](const CalendarReadResult &r) { second = r; });
        QVERIFY(second.ok);
        QVERIFY(second.snapshot.failedCalendars.contains(QStringLiteral("work")));
        int workAfter = 0;
        for (const auto &r : second.snapshot.records) {
            workAfter += r.calendarUri == QLatin1String("work") ? 1 : 0;
        }
        QCOMPARE(workAfter, workBefore);
    }

    void aFailingPrincipalFailsTheWholeReadAndKeepsThePrevious()
    {
        FakeTransport transport;
        transport.offline = true;
        CalendarReadOptions options;
        options.davUser = QStringLiteral("ali");
        options.zone = QTimeZone(QTimeZone::UTC);
        CalendarSnapshot previous;
        previous.loaded = true;
        previous.displayName = QStringLiteral("Before");
        CalendarReadResult result;
        CalendarReader::read(&transport, options, previous, [&result](const CalendarReadResult &r) { result = r; });
        QVERIFY(!result.ok);
        QVERIFY(result.networkError);
        QCOMPARE(result.snapshot.displayName, QStringLiteral("Before"));
    }

    void theSnapshotSurvivesItsCacheForm()
    {
        auto input = loadScenario(QStringLiteral("ali"));
        const auto copy = CalendarSnapshot::fromJson(input.calendar.toJson());
        QCOMPARE(copy.records.size(), input.calendar.records.size());
        QCOMPARE(copy.calendars.size(), input.calendar.calendars.size());
        QCOMPARE(copy.addresses, input.calendar.addresses);
        QCOMPARE(copy.records.first().summary, input.calendar.records.first().summary);
        QCOMPARE(copy.records.first().start, input.calendar.records.first().start);
    }
};

QTEST_GUILESS_MAIN(TestNoDriveAgendaCalDav)
#include "testnodriveagendacaldav.moc"
