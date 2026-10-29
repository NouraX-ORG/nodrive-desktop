/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda (edit step): every edit of design section 9 sends exactly the expected verb, path, headers and body,
// with the precondition of its home (ETag, updated_at, lastModified); a changed item is an edit conflict and
// nothing is written; repeating items, invitations and viewers are refused without a request.

#include "nodrive/noagenda/edittestsupport.h"

#include "gui/nourax/noagenda/edit/caldavwriter.h"
#include "gui/nourax/noagenda/edit/deckwriter.h"
#include "gui/nourax/noagenda/edit/editability.h"
#include "gui/nourax/noagenda/edit/ganttwriter.h"

using namespace AgendaTest;

namespace {

const QString kEventPath = QStringLiteral("/remote.php/dav/calendars/ali/work/call.ics");

QByteArray eventIcs(const QString &extra = QString())
{
    return (QStringLiteral("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//x//EN\r\nBEGIN:VEVENT\r\nUID:ci-call\r\nDTSTAMP:20261001T080000Z\r\nSUMMARY:Client call\r\n")
        + QStringLiteral("DTSTART:20261028T100000Z\r\nDTEND:20261028T110000Z\r\n") + extra
        + QStringLiteral("BEGIN:VALARM\r\nACTION:DISPLAY\r\nTRIGGER:-PT15M\r\nEND:VALARM\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n")).toUtf8();
}

EditResult run(const std::function<void(const EditDone &)> &start)
{
    EditResult result;
    bool called = false;
    start([&](const EditResult &r) {
        result = r;
        called = true;
    });
    if (!called) {
        qWarning() << "the writer did not answer";
    }
    return result;
}

QMap<QByteArray, QByteArray> etag(const char *value)
{
    return {{QByteArrayLiteral("etag"), QByteArray(value)}};
}

QJsonObject ganttTask(const QString &updatedAt = QStringLiteral("2026-10-27 09:00:00"), const QString &mode = QStringLiteral("manual"), int cardId = 0)
{
    QJsonObject row{{QStringLiteral("id"), 1}, {QStringLiteral("task_id"), 111}, {QStringLiteral("resource_id"), 1}, {QStringLiteral("work"), 6}, {QStringLiteral("sort_order"), 0}};
    QJsonObject t{
        {QStringLiteral("id"), 111},
        {QStringLiteral("title"), QStringLiteral("Landing page build")},
        {QStringLiteral("start_date"), QStringLiteral("2026-10-28 00:00:00")},
        {QStringLiteral("end_date"), QStringLiteral("2026-10-30 00:00:00")},
        {QStringLiteral("updated_at"), updatedAt},
        {QStringLiteral("progress_mode"), mode},
        {QStringLiteral("status"), QStringLiteral("in_progress")},
        {QStringLiteral("work"), 0.75},
        {QStringLiteral("resources"), QJsonArray{row}},
    };
    if (cardId > 0) {
        t.insert(QStringLiteral("deck_card_id"), cardId);
    }
    return t;
}

QJsonObject deckCardJson(const QString &etagValue = QStringLiteral("etag501"), int lastModified = 1790000000)
{
    return QJsonObject{
        {QStringLiteral("id"), 501},
        {QStringLiteral("title"), QStringLiteral("Landing page card")},
        {QStringLiteral("description"), QStringLiteral("Keep me")},
        {QStringLiteral("type"), QStringLiteral("plain")},
        {QStringLiteral("stackId"), 20},
        {QStringLiteral("order"), 7},
        {QStringLiteral("archived"), false},
        {QStringLiteral("duedate"), QStringLiteral("2026-10-30T00:00:00+00:00")},
        {QStringLiteral("startdate"), QStringLiteral("2026-10-27T00:00:00+00:00")},
        {QStringLiteral("done"), QJsonValue(QJsonValue::Null)},
        {QStringLiteral("lastModified"), lastModified},
        {QStringLiteral("ETag"), etagValue},
        {QStringLiteral("owner"), QJsonObject{{QStringLiteral("uid"), QStringLiteral("mara")}, {QStringLiteral("displayname"), QStringLiteral("Mara")}}},
        {QStringLiteral("labels"), QJsonArray()},
    };
}

} // namespace

class TestNoDriveAgendaEdit : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // ---- CalDAV --------------------------------------------------------------------------------------------------

    void anEventEditIsAGetThenAPutWithIfMatch()
    {
        ScriptedTransport t;
        t.handler = [](const HttpRequest &r) {
            if (r.verb == "GET" && r.url.path() == kEventPath) {
                return ScriptedTransport::answer(200, eventIcs(), etag("\"e1\""));
            }
            if (r.verb == "PUT" && r.url.path() == kEventPath) {
                return ScriptedTransport::answer(204, QByteArray(), etag("\"e2\""));
            }
            return ScriptedTransport::answer(404);
        };
        QJsonObject fields{{QStringLiteral("title"), QStringLiteral("Moved call")},
            {QStringLiteral("start"), QStringLiteral("2026-10-28T13:00:00Z")}, {QStringLiteral("end"), QStringLiteral("2026-10-28T14:00:00Z")}};
        const auto command = EditCommands::calendarPatch(eventItem(), fields);
        const auto context = aliContext();
        const auto result = run([&](const EditDone &done) { CalDavWriter::run(&t, command, context, done); });
        QVERIFY2(result.done(), qPrintable(result.message));
        QCOMPARE(result.newVersion, QStringLiteral("\"e2\""));
        QCOMPARE(t.requests.size(), 2);
        QCOMPARE(t.requests.at(0).verb, QByteArrayLiteral("GET"));
        const auto put = t.requests.at(1);
        QCOMPARE(put.verb, QByteArrayLiteral("PUT"));
        QCOMPARE(put.url.path(), kEventPath);
        QCOMPARE(ScriptedTransport::header(put, "If-Match"), QByteArrayLiteral("\"e1\""));
        QVERIFY(ScriptedTransport::header(put, "Content-Type").startsWith("text/calendar"));
        const QString body = QString::fromUtf8(put.body);
        QVERIFY(body.contains(QStringLiteral("SUMMARY:Moved call\r\n")));
        QVERIFY(body.contains(QStringLiteral("DTSTART:20261028T130000Z\r\n")));
        QVERIFY(body.contains(QStringLiteral("DTEND:20261028T140000Z\r\n")));
        QVERIFY(body.contains(QStringLiteral("BEGIN:VALARM\r\nACTION:DISPLAY\r\nTRIGGER:-PT15M\r\nEND:VALARM\r\n")));
    }

    void aChangedEtagIsAConflictAndNothingIsWritten()
    {
        ScriptedTransport t;
        t.handler = [](const HttpRequest &r) {
            if (r.verb == "GET") {
                return ScriptedTransport::answer(200, eventIcs(), etag("\"e9\""));
            }
            return ScriptedTransport::answer(500);
        };
        const auto command = EditCommands::calendarPatch(eventItem(), QJsonObject{{QStringLiteral("title"), QStringLiteral("Mine")}});
        const auto result = run([&](const EditDone &done) { CalDavWriter::run(&t, command, aliContext(), done); });
        QCOMPARE(result.outcome, EditResult::Outcome::Conflict);
        QCOMPARE(result.conflictKind, QStringLiteral("changed"));
        QVERIFY(result.message.contains(QStringLiteral("kept the other change")));
        QCOMPARE(t.count("PUT"), 0);
    }

    void a412OnThePutIsAConflictToo()
    {
        ScriptedTransport t;
        t.handler = [](const HttpRequest &r) {
            if (r.verb == "GET") {
                return ScriptedTransport::answer(200, eventIcs(), etag("\"e1\""));
            }
            return ScriptedTransport::answer(412);
        };
        const auto command = EditCommands::calendarPatch(eventItem(), QJsonObject{{QStringLiteral("title"), QStringLiteral("Mine")}});
        const auto result = run([&](const EditDone &done) { CalDavWriter::run(&t, command, aliContext(), done); });
        QCOMPARE(result.outcome, EditResult::Outcome::Conflict);
        QCOMPARE(t.count("PUT"), 1);
    }

    void aRemovedObjectIsAConflict()
    {
        ScriptedTransport t;
        t.handler = [](const HttpRequest &) { return ScriptedTransport::answer(404); };
        const auto command = EditCommands::calendarPatch(eventItem(), QJsonObject{{QStringLiteral("title"), QStringLiteral("Mine")}});
        const auto result = run([&](const EditDone &done) { CalDavWriter::run(&t, command, aliContext(), done); });
        QCOMPARE(result.outcome, EditResult::Outcome::Conflict);
        QCOMPARE(result.conflictKind, QStringLiteral("gone"));
    }

    void aRepeatingObjectIsRefusedBeforeAnyPut()
    {
        ScriptedTransport t;
        t.handler = [](const HttpRequest &r) {
            if (r.verb == "GET") {
                return ScriptedTransport::answer(200, eventIcs(QStringLiteral("RRULE:FREQ=DAILY\r\n")), etag("\"e1\""));
            }
            return ScriptedTransport::answer(500);
        };
        const auto command = EditCommands::calendarPatch(eventItem(), QJsonObject{{QStringLiteral("title"), QStringLiteral("Mine")}});
        const auto result = run([&](const EditDone &done) { CalDavWriter::run(&t, command, aliContext(), done); });
        QCOMPARE(result.outcome, EditResult::Outcome::Refused);
        QCOMPARE(t.count("PUT"), 0);
    }

    void aPutWithoutTheNewEtagAsksForIt()
    {
        ScriptedTransport t;
        int gets = 0;
        t.handler = [&gets](const HttpRequest &r) {
            if (r.verb == "GET") {
                ++gets;
                return ScriptedTransport::answer(200, eventIcs(), etag(gets == 1 ? "\"e1\"" : "\"e2\""));
            }
            return ScriptedTransport::answer(204);
        };
        const auto command = EditCommands::calendarPatch(eventItem(), QJsonObject{{QStringLiteral("title"), QStringLiteral("Mine")}});
        const auto result = run([&](const EditDone &done) { CalDavWriter::run(&t, command, aliContext(), done); });
        QVERIFY(result.done());
        QCOMPARE(result.newVersion, QStringLiteral("\"e2\""));
    }

    void aNewEventIsAPutWithIfNoneMatch()
    {
        ScriptedTransport t;
        t.handler = [](const HttpRequest &r) { return r.verb == "PUT" ? ScriptedTransport::answer(201, QByteArray(), etag("\"n1\"")) : ScriptedTransport::answer(404); };
        QJsonObject fields{{QStringLiteral("title"), QStringLiteral("Lunch")}, {QStringLiteral("start"), QStringLiteral("2026-11-06T11:00:00Z")},
            {QStringLiteral("end"), QStringLiteral("2026-11-06T12:00:00Z")},
            {QStringLiteral("agent"), QJsonObject{{QStringLiteral("id"), QStringLiteral("AG-0001")}, {QStringLiteral("tag"), QStringLiteral("agent:AG-0001")}, {QStringLiteral("removeTags"), QJsonArray()}}}};
        const auto command = EditCommands::calendarCreate(QStringLiteral("work"), false, QStringLiteral("uid-77"), fields, QStringLiteral("Lunch"));
        const auto result = run([&](const EditDone &done) { CalDavWriter::run(&t, command, aliContext(), done); });
        QVERIFY2(result.done(), qPrintable(result.message));
        QCOMPARE(t.requests.size(), 1);
        const auto put = t.requests.first();
        QCOMPARE(put.url.path(), QStringLiteral("/remote.php/dav/calendars/ali/work/uid-77.ics"));
        QCOMPARE(ScriptedTransport::header(put, "If-None-Match"), QByteArrayLiteral("*"));
        const QString body = QString::fromUtf8(put.body);
        QVERIFY(body.contains(QStringLiteral("UID:uid-77")));
        QVERIFY(body.contains(QStringLiteral("X-NOURAX-AGENT-ID:AG-0001")));
        QVERIFY(body.contains(QStringLiteral("CATEGORIES:agent:AG-0001")));
        QVERIFY(body.contains(QStringLiteral("PRODID:-//Nourax//NoAgenda//EN")));
    }

    void aDeleteIsADeleteWithIfMatch()
    {
        ScriptedTransport t;
        t.handler = [](const HttpRequest &r) { return r.verb == "DELETE" ? ScriptedTransport::answer(204) : ScriptedTransport::answer(404); };
        const auto command = EditCommands::calendarDelete(eventItem());
        const auto result = run([&](const EditDone &done) { CalDavWriter::run(&t, command, aliContext(), done); });
        QVERIFY(result.done());
        QCOMPARE(t.requests.first().verb, QByteArrayLiteral("DELETE"));
        QCOMPARE(t.requests.first().url.path(), kEventPath);
        QCOMPARE(ScriptedTransport::header(t.requests.first(), "If-Match"), QByteArrayLiteral("\"e1\""));
    }

    void aDeleteOfAChangedObjectIsAConflict()
    {
        ScriptedTransport t;
        t.handler = [](const HttpRequest &) { return ScriptedTransport::answer(412); };
        const auto result = run([&](const EditDone &done) { CalDavWriter::run(&t, EditCommands::calendarDelete(eventItem()), aliContext(), done); });
        QCOMPARE(result.outcome, EditResult::Outcome::Conflict);
    }

    void aTodoIsTickedDoneThroughAPatch()
    {
        AgendaItem todo = eventItem();
        todo.kind = ItemKind::Todo;
        todo.home = Home::Tasks;
        todo.key = QStringLiteral("cal:personal/renew.ics");
        todo.calendarUri = QStringLiteral("personal");
        todo.objectUri = QStringLiteral("renew.ics");
        ScriptedTransport t;
        t.handler = [](const HttpRequest &r) {
            if (r.verb == "GET") {
                return ScriptedTransport::answer(200, QByteArray("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VTODO\r\nUID:t1\r\nSUMMARY:Renew\r\nDUE;VALUE=DATE:20261030\r\nEND:VTODO\r\nEND:VCALENDAR\r\n"), etag("\"e1\""));
            }
            return ScriptedTransport::answer(204, QByteArray(), etag("\"e2\""));
        };
        const auto command = EditCommands::calendarPatch(todo, QJsonObject{{QStringLiteral("done"), true}});
        const auto result = run([&](const EditDone &done) { CalDavWriter::run(&t, command, aliContext(), done); });
        QVERIFY(result.done());
        const QString body = QString::fromUtf8(t.of("PUT").first().body);
        QVERIFY(body.contains(QStringLiteral("STATUS:COMPLETED")));
        QVERIFY(body.contains(QStringLiteral("COMPLETED:20261028T120000Z")));
        QVERIFY(body.contains(QStringLiteral("PERCENT-COMPLETE:100")));
    }

    void noConnectionASignInProblemAndARefusalAreTold()
    {
        const auto command = EditCommands::calendarPatch(eventItem(), QJsonObject{{QStringLiteral("title"), QStringLiteral("Mine")}});
        {
            ScriptedTransport t;
            t.offline = true;
            const auto r = run([&](const EditDone &done) { CalDavWriter::run(&t, command, aliContext(), done); });
            QCOMPARE(r.outcome, EditResult::Outcome::Offline);
        }
        {
            ScriptedTransport t;
            t.handler = [](const HttpRequest &) { return ScriptedTransport::answer(401); };
            const auto r = run([&](const EditDone &done) { CalDavWriter::run(&t, command, aliContext(), done); });
            QCOMPARE(r.outcome, EditResult::Outcome::NeedsLogin);
        }
        {
            ScriptedTransport t;
            t.handler = [](const HttpRequest &) { return ScriptedTransport::answer(403); };
            const auto r = run([&](const EditDone &done) { CalDavWriter::run(&t, command, aliContext(), done); });
            QCOMPARE(r.outcome, EditResult::Outcome::Refused);
        }
    }

    // ---- NoGantt -------------------------------------------------------------------------------------------------------

    void aGanttMoveSendsTheDatesAndShowsWhatMovedWithIt()
    {
        ScriptedTransport t;
        t.handler = [](const HttpRequest &r) {
            if (r.verb == "GET" && r.url.path() == QStringLiteral("/ocs/v2.php/apps/nc_gantt/api/v1/projects/1/tasks")) {
                return ScriptedTransport::answer(200, ocsBody(QJsonArray{ganttTask()}));
            }
            if (r.verb == "PUT" && r.url.path() == QStringLiteral("/ocs/v2.php/apps/nc_gantt/api/v1/tasks/111")) {
                QJsonObject saved = ganttTask(QStringLiteral("2026-10-28 12:00:00"));
                QJsonObject moved{{QStringLiteral("id"), 112}, {QStringLiteral("title"), QStringLiteral("Send invoice")}};
                return ScriptedTransport::answer(200, ocsBody(QJsonObject{{QStringLiteral("task"), saved}, {QStringLiteral("propagated"), QJsonArray{moved}},
                    {QStringLiteral("op_id"), QStringLiteral("op-1")}, {QStringLiteral("deck"), QJsonObject()}}));
            }
            return ScriptedTransport::answer(404);
        };
        const auto command = EditCommands::ganttUpdate(ganttItem(), QJsonObject{{QStringLiteral("end_date"), QStringLiteral("2026-11-04")}});
        const auto result = run([&](const EditDone &done) { GanttWriter::run(&t, command, aliContext(), done); });
        QVERIFY2(result.done(), qPrintable(result.message));
        QCOMPARE(result.newVersion, QStringLiteral("2026-10-28 12:00:00"));
        QCOMPARE(result.alsoMoved, QStringList{QStringLiteral("Send invoice")});
        QCOMPARE(result.undoOpId, QStringLiteral("op-1"));
        const auto put = t.of("PUT").first();
        QCOMPARE(ScriptedTransport::header(put, "OCS-APIRequest"), QByteArrayLiteral("true"));
        QVERIFY(ScriptedTransport::header(put, "Content-Type").startsWith("application/json"));
        const auto body = parseObject(put.body);
        QCOMPARE(body.value(QStringLiteral("end_date")).toString(), QStringLiteral("2026-11-04 00:00:00")); // the time of day is kept
        QVERIFY(!body.contains(QStringLiteral("start_date")));
        QCOMPARE(body.value(QStringLiteral("adjust_lag")).toBool(), true);
        QVERIFY(body.contains(QStringLiteral("deck_confirmed")));
        QVERIFY(!body.value(QStringLiteral("deck_confirmed")).toBool()); // a linked card is never moved without the person's say
    }

    void ticksDoneAndPercentOfAManualTask()
    {
        ScriptedTransport t;
        t.handler = [](const HttpRequest &r) {
            if (r.verb == "GET") {
                return ScriptedTransport::answer(200, ocsBody(QJsonArray{ganttTask()}));
            }
            return ScriptedTransport::answer(200, ocsBody(QJsonObject{{QStringLiteral("task"), ganttTask(QStringLiteral("n"))}, {QStringLiteral("propagated"), QJsonArray()},
                {QStringLiteral("op_id"), QJsonValue(QJsonValue::Null)}, {QStringLiteral("deck"), QJsonObject()}}));
        };
        auto command = EditCommands::ganttUpdate(ganttItem(), QJsonObject{{QStringLiteral("done"), true}});
        auto result = run([&](const EditDone &done) { GanttWriter::run(&t, command, aliContext(), done); });
        QVERIFY(result.done());
        auto body = parseObject(t.of("PUT").first().body);
        QCOMPARE(body.value(QStringLiteral("status")).toString(), QStringLiteral("done"));
        QCOMPARE(body.value(QStringLiteral("manual_progress")).toInt(), 100);
        QVERIFY(result.undoOpId.isEmpty());

        t.requests.clear();
        command = EditCommands::ganttUpdate(ganttItem(), QJsonObject{{QStringLiteral("manual_progress"), 55}, {QStringLiteral("status"), QStringLiteral("blocked")}});
        result = run([&](const EditDone &done) { GanttWriter::run(&t, command, aliContext(), done); });
        QVERIFY(result.done());
        body = parseObject(t.of("PUT").first().body);
        QCOMPARE(body.value(QStringLiteral("manual_progress")).toInt(), 55);
        QCOMPARE(body.value(QStringLiteral("status")).toString(), QStringLiteral("blocked"));
        QVERIFY(!body.contains(QStringLiteral("adjust_lag")));
    }

    void percentDoneOfATaskThatFollowsTheBoardIsRefused()
    {
        ScriptedTransport t;
        t.handler = [](const HttpRequest &r) {
            return r.verb == "GET" ? ScriptedTransport::answer(200, ocsBody(QJsonArray{ganttTask(QStringLiteral("2026-10-27 09:00:00"), QStringLiteral("deck"))})) : ScriptedTransport::answer(500);
        };
        const auto command = EditCommands::ganttUpdate(ganttItem(), QJsonObject{{QStringLiteral("manual_progress"), 55}});
        const auto result = run([&](const EditDone &done) { GanttWriter::run(&t, command, aliContext(), done); });
        QCOMPARE(result.outcome, EditResult::Outcome::Refused);
        QCOMPARE(result.message, QStringLiteral("% done comes from NoBoard"));
        QCOMPARE(t.count("PUT"), 0);
    }

    void aGanttTaskThatChangedMeanwhileIsAConflict()
    {
        ScriptedTransport t;
        t.handler = [](const HttpRequest &r) {
            return r.verb == "GET" ? ScriptedTransport::answer(200, ocsBody(QJsonArray{ganttTask(QStringLiteral("2026-10-28 11:59:59"))})) : ScriptedTransport::answer(500);
        };
        const auto command = EditCommands::ganttUpdate(ganttItem(), QJsonObject{{QStringLiteral("end_date"), QStringLiteral("2026-11-04")}});
        const auto result = run([&](const EditDone &done) { GanttWriter::run(&t, command, aliContext(), done); });
        QCOMPARE(result.outcome, EditResult::Outcome::Conflict);
        QCOMPARE(t.count("PUT"), 0);
    }

    void aViewerGetsThePlainSentence()
    {
        ScriptedTransport t;
        t.handler = [](const HttpRequest &r) {
            return r.verb == "GET" ? ScriptedTransport::answer(200, ocsBody(QJsonArray{ganttTask()})) : ScriptedTransport::answer(403);
        };
        const auto command = EditCommands::ganttUpdate(ganttItem(), QJsonObject{{QStringLiteral("end_date"), QStringLiteral("2026-11-04")}});
        const auto result = run([&](const EditDone &done) { GanttWriter::run(&t, command, aliContext(), done); });
        QCOMPARE(result.outcome, EditResult::Outcome::Refused);
        QCOMPARE(result.message, QStringLiteral("You can view this project only. Ask the project manager for editor rights."));
    }

    void theServersRefusalsAreToldInPlainWords()
    {
        for (const auto &code : {QStringLiteral("too_many_resources"), QStringLiteral("roles_only_assignment"), QStringLiteral("invalid_date")}) {
            ScriptedTransport t;
            t.handler = [&code](const HttpRequest &r) {
                return r.verb == "GET" ? ScriptedTransport::answer(200, ocsBody(QJsonArray{ganttTask()})) : ScriptedTransport::answer(400, ("{\"ocs\":{\"meta\":{\"statuscode\":400,\"message\":\"" + code + "\"},\"data\":[]}}").toUtf8());
            };
            const auto command = EditCommands::ganttUpdate(ganttItem(), QJsonObject{{QStringLiteral("end_date"), QStringLiteral("2026-11-04")}});
            const auto result = run([&](const EditDone &done) { GanttWriter::run(&t, command, aliContext(), done); });
            QCOMPARE(result.outcome, EditResult::Outcome::Refused);
            QVERIFY2(!result.message.contains(code), qPrintable(result.message)); // never the code itself
            QVERIFY(!result.message.isEmpty());
        }
    }

    void aLinkedCardThatDidNotMoveIsSaid()
    {
        ScriptedTransport t;
        t.handler = [](const HttpRequest &r) {
            if (r.verb == "GET") {
                return ScriptedTransport::answer(200, ocsBody(QJsonArray{ganttTask(QStringLiteral("2026-10-27 09:00:00"), QStringLiteral("manual"), 501)}));
            }
            return ScriptedTransport::answer(200, ocsBody(QJsonObject{{QStringLiteral("task"), ganttTask(QStringLiteral("n"))}, {QStringLiteral("propagated"), QJsonArray()},
                {QStringLiteral("op_id"), QStringLiteral("op-2")}, {QStringLiteral("deck"), QJsonObject{{QStringLiteral("pending"), QJsonArray{QJsonObject{{QStringLiteral("card_id"), 501}}}}, {QStringLiteral("unconfirmed"), 1}}}}));
        };
        const auto command = EditCommands::ganttUpdate(ganttItem(), QJsonObject{{QStringLiteral("end_date"), QStringLiteral("2026-11-04")}});
        const auto result = run([&](const EditDone &done) { GanttWriter::run(&t, command, aliContext(), done); });
        QVERIFY(result.done());
        QCOMPARE(result.note, QStringLiteral("The linked card wasn't moved. Open in NoGantt to confirm."));
    }

    void undoRevertsTheMove()
    {
        ScriptedTransport t;
        t.handler = [](const HttpRequest &) { return ScriptedTransport::answer(200, ocsBody(QJsonObject())); };
        const auto result = run([&](const EditDone &done) { GanttWriter::undo(&t, QStringLiteral("op-1"), done); });
        QVERIFY(result.done());
        QCOMPARE(t.requests.first().verb, QByteArrayLiteral("POST"));
        QCOMPARE(t.requests.first().url.path(), QStringLiteral("/ocs/v2.php/apps/nc_gantt/api/v1/changes/op-1/revert"));
    }

    // ---- NoBoard ---------------------------------------------------------------------------------------------------------

    void aCardUpdateIsBuiltFromTheFreshCardAndAlwaysSendsEveryField()
    {
        ScriptedTransport t;
        const QString cardPath = QStringLiteral("/index.php/apps/deck/api/v1.0/boards/10/stacks/20/cards/501");
        t.handler = [&](const HttpRequest &r) {
            if (r.verb == "GET" && r.url.path() == cardPath) {
                return ScriptedTransport::answer(200, json(deckCardJson()));
            }
            if (r.verb == "PUT" && r.url.path() == cardPath) {
                return ScriptedTransport::answer(200, json(deckCardJson(QStringLiteral("etag502"), 1790000100)));
            }
            return ScriptedTransport::answer(404);
        };
        const auto command = EditCommands::deckCard(cardItem(), QJsonObject{{QStringLiteral("due"), QStringLiteral("2026-11-05")}});
        const auto result = run([&](const EditDone &done) { DeckWriter::run(&t, command, aliContext(), done); });
        QVERIFY2(result.done(), qPrintable(result.message));
        QCOMPARE(result.newVersion, QStringLiteral("etag502"));
        const auto put = t.of("PUT").first();
        QCOMPARE(ScriptedTransport::header(put, "OCS-APIRequest"), QByteArrayLiteral("true"));
        const auto body = parseObject(put.body);
        // Deck clears every field that is not sent: all of them are there, from the fresh card.
        for (const auto &key : {"title", "type", "owner", "description", "order", "archived", "startdate", "duedate", "done"}) {
            QVERIFY2(body.contains(QString::fromLatin1(key)), key);
        }
        QCOMPARE(body.value(QStringLiteral("title")).toString(), QStringLiteral("Landing page card"));
        QCOMPARE(body.value(QStringLiteral("owner")).toString(), QStringLiteral("mara"));
        QCOMPARE(body.value(QStringLiteral("description")).toString(), QStringLiteral("Keep me"));
        QCOMPARE(body.value(QStringLiteral("order")).toInt(), 7);
        QCOMPARE(body.value(QStringLiteral("startdate")).toString(), QStringLiteral("2026-10-27T00:00:00+00:00"));
        QCOMPARE(body.value(QStringLiteral("duedate")).toString(), QStringLiteral("2026-11-05T00:00:00Z"));
        QVERIFY(body.value(QStringLiteral("done")).isNull());
        QVERIFY(!body.contains(QStringLiteral("color")));
    }

    void tickingACardDoneKeepsTheDueDate()
    {
        ScriptedTransport t;
        t.handler = [](const HttpRequest &r) {
            return r.verb == "GET" ? ScriptedTransport::answer(200, json(deckCardJson())) : ScriptedTransport::answer(200, json(deckCardJson(QStringLiteral("etag503"))));
        };
        const auto command = EditCommands::deckCard(cardItem(), QJsonObject{{QStringLiteral("done"), true}});
        const auto result = run([&](const EditDone &done) { DeckWriter::run(&t, command, aliContext(), done); });
        QVERIFY(result.done());
        const auto body = parseObject(t.of("PUT").first().body);
        QVERIFY(body.value(QStringLiteral("done")).isString());
        QVERIFY(!body.value(QStringLiteral("done")).toString().isEmpty());
        QCOMPARE(body.value(QStringLiteral("duedate")).toString(), QStringLiteral("2026-10-30T00:00:00+00:00"));
    }

    void aCardThatChangedMeanwhileIsAConflict()
    {
        ScriptedTransport t;
        t.handler = [](const HttpRequest &r) {
            return r.verb == "GET" ? ScriptedTransport::answer(200, json(deckCardJson(QStringLiteral("other"), 1790000999))) : ScriptedTransport::answer(500);
        };
        const auto command = EditCommands::deckCard(cardItem(), QJsonObject{{QStringLiteral("due"), QStringLiteral("2026-11-05")}});
        const auto result = run([&](const EditDone &done) { DeckWriter::run(&t, command, aliContext(), done); });
        QCOMPARE(result.outcome, EditResult::Outcome::Conflict);
        QCOMPARE(t.count("PUT"), 0);
    }

    void aCardAgentSwitchRemovesTheOldLabelThenAssignsTheNewOne()
    {
        ScriptedTransport t;
        QJsonObject card = deckCardJson();
        card.insert(QStringLiteral("labels"), QJsonArray{QJsonObject{{QStringLiteral("id"), 3}, {QStringLiteral("title"), QString::fromUtf8("\xF0\x9F\xA4\x96 Research-Bot")}}});
        QJsonObject board{{QStringLiteral("id"), 10}, {QStringLiteral("permissions"), QJsonObject{{QStringLiteral("PERMISSION_MANAGE"), false}}},
            {QStringLiteral("labels"), QJsonArray{QJsonObject{{QStringLiteral("id"), 1}, {QStringLiteral("title"), QString::fromUtf8("\xF0\x9F\xA4\x96 Writer-Bot")}}}}};
        t.handler = [&](const HttpRequest &r) {
            const QString path = r.url.path();
            if (r.verb == "GET" && path.endsWith(QStringLiteral("/cards/501"))) {
                return ScriptedTransport::answer(200, json(card));
            }
            if (r.verb == "GET" && path.endsWith(QStringLiteral("/boards/10"))) {
                return ScriptedTransport::answer(200, json(board));
            }
            return ScriptedTransport::answer(200, QByteArray("{}"));
        };
        const auto command = EditCommands::deckAgent(cardItem(), QStringLiteral("AG-0001"), QStringLiteral("ali"));
        const auto result = run([&](const EditDone &done) { DeckWriter::run(&t, command, aliContext(), done); });
        QVERIFY2(result.done(), qPrintable(result.message));
        const auto puts = t.of("PUT");
        QCOMPARE(puts.size(), 2);
        QVERIFY(puts.at(0).url.path().endsWith(QStringLiteral("/cards/501/removeLabel")));
        QCOMPARE(parseObject(puts.at(0).body).value(QStringLiteral("labelId")).toInt(), 3);
        QVERIFY(puts.at(1).url.path().endsWith(QStringLiteral("/cards/501/assignLabel")));
        QCOMPARE(parseObject(puts.at(1).body).value(QStringLiteral("labelId")).toInt(), 1);
        QCOMPARE(t.count("POST"), 0); // the label exists: none is created
    }

    void aMissingLabelIsCreatedOnlyWhereThePersonMayManageTheBoard()
    {
        for (const bool manage : {false, true}) {
            ScriptedTransport t;
            QJsonObject board{{QStringLiteral("id"), 10}, {QStringLiteral("permissions"), QJsonObject{{QStringLiteral("PERMISSION_MANAGE"), manage}}}, {QStringLiteral("labels"), QJsonArray()}};
            t.handler = [&](const HttpRequest &r) {
                const QString path = r.url.path();
                if (r.verb == "GET" && path.endsWith(QStringLiteral("/cards/501"))) {
                    return ScriptedTransport::answer(200, json(deckCardJson()));
                }
                if (r.verb == "GET" && path.endsWith(QStringLiteral("/boards/10"))) {
                    return ScriptedTransport::answer(200, json(board));
                }
                if (r.verb == "POST" && path.endsWith(QStringLiteral("/boards/10/labels"))) {
                    return ScriptedTransport::answer(200, json(QJsonObject{{QStringLiteral("id"), 42}, {QStringLiteral("title"), QJsonValue(parseObject(r.body).value(QStringLiteral("title")))}}));
                }
                return ScriptedTransport::answer(200, QByteArray("{}"));
            };
            const auto command = EditCommands::deckAgent(cardItem(), QStringLiteral("AG-0001"), QStringLiteral("ali"));
            const auto result = run([&](const EditDone &done) { DeckWriter::run(&t, command, aliContext(), done); });
            if (manage) {
                QVERIFY2(result.done(), qPrintable(result.message));
                QCOMPARE(t.count("POST"), 1);
                const auto assign = t.of("PUT").first();
                QCOMPARE(parseObject(assign.body).value(QStringLiteral("labelId")).toInt(), 42);
            } else {
                QCOMPARE(result.outcome, EditResult::Outcome::Refused);
                QCOMPARE(t.count("POST"), 0);
                QCOMPARE(t.count("PUT"), 0);
            }
        }
    }

    void whenTheNewLabelCannotBeAssignedTheOldOneIsPutBack()
    {
        ScriptedTransport t;
        QJsonObject card = deckCardJson();
        card.insert(QStringLiteral("labels"), QJsonArray{QJsonObject{{QStringLiteral("id"), 3}, {QStringLiteral("title"), QString::fromUtf8("\xF0\x9F\xA4\x96 Research-Bot")}}});
        QJsonObject board{{QStringLiteral("permissions"), QJsonObject{{QStringLiteral("PERMISSION_MANAGE"), false}}},
            {QStringLiteral("labels"), QJsonArray{QJsonObject{{QStringLiteral("id"), 1}, {QStringLiteral("title"), QString::fromUtf8("\xF0\x9F\xA4\x96 Writer-Bot")}}}}};
        t.handler = [&](const HttpRequest &r) {
            const QString path = r.url.path();
            if (r.verb == "GET" && path.endsWith(QStringLiteral("/cards/501"))) {
                return ScriptedTransport::answer(200, json(card));
            }
            if (r.verb == "GET" && path.endsWith(QStringLiteral("/boards/10"))) {
                return ScriptedTransport::answer(200, json(board));
            }
            if (path.endsWith(QStringLiteral("/assignLabel")) && parseObject(r.body).value(QStringLiteral("labelId")).toInt() == 1) {
                return ScriptedTransport::answer(500);
            }
            return ScriptedTransport::answer(200, QByteArray("{}"));
        };
        const auto command = EditCommands::deckAgent(cardItem(), QStringLiteral("AG-0001"), QStringLiteral("ali"));
        const auto result = run([&](const EditDone &done) { DeckWriter::run(&t, command, aliContext(), done); });
        QCOMPARE(result.outcome, EditResult::Outcome::Refused);
        QVERIFY(result.message.contains(QStringLiteral("old agent was put back")));
        const auto puts = t.of("PUT");
        QCOMPARE(puts.size(), 3); // remove old, assign new (failed), assign old again
        QCOMPARE(parseObject(puts.last().body).value(QStringLiteral("labelId")).toInt(), 3);
        QVERIFY(puts.last().url.path().endsWith(QStringLiteral("/assignLabel")));
    }

    // ---- what may be changed ----------------------------------------------------------------------------------------------------

    void repeatingInvitedAndLockedRowsAreRefusedWithPlainReasons()
    {
        auto repeating = eventItem();
        repeating.repeating = true;
        auto e = EditRules::of(repeating);
        QVERIFY(!e.canEdit && !e.canDelete && !e.canChangeAgent && e.repeats);
        QCOMPARE(e.reason, QStringLiteral("This repeats. Change it in NoCalendar."));
        auto repeatingTodo = repeating;
        repeatingTodo.kind = ItemKind::Todo;
        repeatingTodo.home = Home::Tasks;
        QCOMPARE(EditRules::of(repeatingTodo).reason, QStringLiteral("This repeats. Change it in NoTasks."));

        auto invited = eventItem();
        invited.invitation = true;
        e = EditRules::of(invited);
        QVERIFY(!e.canEdit && !e.canChangeAgent);
        QCOMPARE(e.agentReason, QStringLiteral("You were invited, so this is always Self."));

        auto readOnly = eventItem();
        readOnly.readOnly = true;
        readOnly.readOnlyReason = QStringLiteral("This calendar is read only for you.");
        e = EditRules::of(readOnly);
        QVERIFY(!e.canEdit && e.locked);
        QCOMPARE(e.reason, readOnly.readOnlyReason);

        auto viewer = ganttItem();
        viewer.readOnly = true;
        e = EditRules::of(viewer);
        QVERIFY(!e.canEdit);
        QCOMPARE(e.reason, QStringLiteral("You can view this project only. Ask the project manager for editor rights."));

        auto guard = ganttItem();
        guard.versionReadOnly = true;
        QVERIFY(!EditRules::of(guard).canEdit);

        auto deckMode = ganttItem();
        deckMode.progressMode = QStringLiteral("deck");
        e = EditRules::of(deckMode);
        QVERIFY(e.canEdit && !e.canProgress);
        QCOMPARE(e.progressReason, QStringLiteral("% done comes from NoBoard"));

        auto team = ganttItem();
        team.workKind = WorkKind::Team;
        QVERIFY(!EditRules::of(team).canEdit);

        auto problem = ganttItem();
        problem.problemOnly = true;
        QVERIFY(!EditRules::of(problem).canEdit);
    }

    void aGanttTaskOrACardCanNeverBeDeleted()
    {
        QVERIFY(!EditRules::of(ganttItem()).canDelete);
        QVERIFY(!EditRules::of(cardItem()).canDelete);
        QVERIFY(EditRules::of(eventItem()).canDelete);
    }
};

QTEST_GUILESS_MAIN(TestNoDriveAgendaEdit)
#include "testnodriveagendaedit.moc"
