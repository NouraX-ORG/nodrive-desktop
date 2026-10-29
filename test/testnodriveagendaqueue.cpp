/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda (edit step): the offline queue and the edit conflicts. Commands keep their order, survive a restart, are
// never sent in parallel, and a conflict stays in the "Not sent" list until the person clicks.

#include "nodrive/noagenda/edittestsupport.h"

#include <QSignalSpy>
#include <QTemporaryDir>

#include <memory>

using namespace AgendaTest;

namespace {

QByteArray ics(const QString &uid)
{
    return (QStringLiteral("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VEVENT\r\nUID:") + uid + QStringLiteral("\r\nSUMMARY:Old\r\nDTSTART:20261028T100000Z\r\nDTEND:20261028T110000Z\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n")).toUtf8();
}

AgendaItem eventNamed(const QString &object)
{
    auto item = eventItem();
    item.key = QStringLiteral("cal:work/") + object;
    item.objectUri = object;
    item.title = object;
    item.version = QStringLiteral("\"e1\"");
    return item;
}

QJsonObject titleChange(const QString &title)
{
    return QJsonObject{{QStringLiteral("title"), title}};
}

} // namespace

class TestNoDriveAgendaQueue : public QObject
{
    Q_OBJECT

    QTemporaryDir _dir;

    std::unique_ptr<EditService> service(ScriptedTransport *t, const QString &sub)
    {
        return std::make_unique<EditService>(t, _dir.path() + QLatin1Char('/') + sub, [] { return aliContext(); });
    }

    /// Answers the calendar calls of every object: the ETag is "e1" and goes up by one with every PUT.
    static ScriptedTransport::Handler calendarHandler()
    {
        auto versions = std::make_shared<QMap<QString, int>>();
        return [versions](const HttpRequest &r) {
            const QString path = r.url.path();
            int &version = (*versions)[path];
            if (version == 0) {
                version = 1;
            }
            if (r.verb == "GET") {
                return ScriptedTransport::answer(200, ics(path.section(QLatin1Char('/'), -1)), {{QByteArrayLiteral("etag"), QByteArray("\"e") + QByteArray::number(version) + QByteArray("\"")}});
            }
            ++version;
            return ScriptedTransport::answer(204, QByteArray(), {{QByteArrayLiteral("etag"), QByteArray("\"e") + QByteArray::number(version) + QByteArray("\"")}});
        };
    }

private Q_SLOTS:
    void initTestCase() { QVERIFY(_dir.isValid()); }

    void commandsKeepTheirOrderAndSurviveARestart()
    {
        const QString dir = _dir.path() + QStringLiteral("/order");
        EditQueue first(dir);
        first.enqueue(EditCommands::calendarPatch(eventNamed(QStringLiteral("a.ics")), titleChange(QStringLiteral("A"))));
        first.enqueue(EditCommands::calendarPatch(eventNamed(QStringLiteral("b.ics")), titleChange(QStringLiteral("B"))));
        first.enqueue(EditCommands::ganttUpdate(ganttItem(), QJsonObject{{QStringLiteral("end_date"), QStringLiteral("2026-11-04")}}));
        QVERIFY(first.save());

        EditQueue second(dir); // after a restart
        second.load();
        QCOMPARE(second.pendingCount(), 3);
        QCOMPARE(second.commands().at(0).title, QStringLiteral("a.ics"));
        QCOMPARE(second.commands().at(1).title, QStringLiteral("b.ics"));
        QCOMPARE(second.commands().at(2).kind, EditKind::GanttUpdate);
        QCOMPARE(second.commands().at(2).change.value(QStringLiteral("end_date")).toString(), QStringLiteral("2026-11-04"));
        QCOMPARE(second.commands().at(0).baseVersion, QStringLiteral("\"e1\""));
        QCOMPARE(second.commands().at(0).state, CommandState::Waiting);
        // The file holds no credentials, only the commands.
        QFile file(dir + QStringLiteral("/queue.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(file.readAll());
        QVERIFY(text.contains(QStringLiteral("\"cache_schema\": 1")));
        QVERIFY(!text.contains(QStringLiteral("password"), Qt::CaseInsensitive));
    }

    void aFileThatCannotBeReadIsIgnored()
    {
        const QString dir = _dir.path() + QStringLiteral("/broken");
        QDir().mkpath(dir);
        QFile file(dir + QStringLiteral("/queue.json"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{ not json");
        file.close();
        EditQueue queue(dir);
        queue.load();
        QCOMPARE(queue.pendingCount(), 0);
        QVERIFY(queue.conflicts().isEmpty());
    }

    void twoEditsOfTheSameItemThatAreStillWaitingAreFoldedIntoOne()
    {
        EditQueue queue(_dir.path() + QStringLiteral("/merge"));
        const auto item = eventNamed(QStringLiteral("a.ics"));
        const QString first = queue.enqueue(EditCommands::calendarPatch(item, titleChange(QStringLiteral("One"))));
        const QString second = queue.enqueue(EditCommands::calendarPatch(item, QJsonObject{{QStringLiteral("description"), QStringLiteral("Notes")}, {QStringLiteral("title"), QStringLiteral("Two")}}));
        QCOMPARE(first, second);
        QCOMPARE(queue.pendingCount(), 1);
        const auto &c = queue.commands().first();
        QCOMPARE(c.change.value(QStringLiteral("title")).toString(), QStringLiteral("Two"));
        QCOMPARE(c.change.value(QStringLiteral("description")).toString(), QStringLiteral("Notes"));
        // Another item is never folded in, and a command that is being sent is never changed.
        queue.enqueue(EditCommands::calendarPatch(eventNamed(QStringLiteral("b.ics")), titleChange(QStringLiteral("B"))));
        QCOMPARE(queue.pendingCount(), 2);
        queue.setSending(first);
        queue.enqueue(EditCommands::calendarPatch(item, titleChange(QStringLiteral("Three"))));
        QCOMPARE(queue.pendingCount(), 3);
        QCOMPARE(queue.commands().first().change.value(QStringLiteral("title")).toString(), QStringLiteral("Two"));
    }

    void offlineEditsWaitAndAreSentInOrderWhenTheConnectionIsBack()
    {
        ScriptedTransport t;
        t.offline = true;
        auto svc = service(&t, QStringLiteral("offline"));
        QSignalSpy finished(svc.get(), &EditService::finished);
        svc->submit(EditCommands::calendarPatch(eventNamed(QStringLiteral("a.ics")), titleChange(QStringLiteral("A"))));
        svc->submit(EditCommands::calendarPatch(eventNamed(QStringLiteral("b.ics")), titleChange(QStringLiteral("B"))));
        svc->submit(EditCommands::calendarPatch(eventNamed(QStringLiteral("c.ics")), titleChange(QStringLiteral("C"))));
        QCOMPARE(svc->queue().pendingCount(), 3);
        QVERIFY(svc->queue().conflicts().isEmpty()); // nothing is lost and nothing is a conflict
        QCOMPARE(qvariant_cast<EditResult>(finished.last().at(1)).outcome, EditResult::Outcome::Offline);

        // They are drawn on the rows at once, with the "waiting to send" mark.
        QList<AgendaItem> rows{eventNamed(QStringLiteral("a.ics")), eventNamed(QStringLiteral("b.ics"))};
        svc->overlay(&rows, QDate(2026, 10, 28));
        QCOMPARE(rows.at(0).title, QStringLiteral("A"));
        QCOMPARE(rows.at(0).pending, PendingState::Waiting);

        // Back online: all three go out, oldest first, one at a time.
        t.offline = false;
        t.handler = calendarHandler();
        t.requests.clear();
        svc->retry();
        QCOMPARE(svc->queue().pendingCount(), 0);
        QVERIFY(svc->queue().conflicts().isEmpty());
        QStringList order;
        for (const auto &r : t.of("PUT")) {
            order.append(r.url.path().section(QLatin1Char('/'), -1));
        }
        QCOMPARE(order, (QStringList{QStringLiteral("a.ics"), QStringLiteral("b.ics"), QStringLiteral("c.ics")}));
        // GET, PUT, GET, PUT, GET, PUT: the next one starts only after the answer of the one before.
        QStringList verbs;
        for (const auto &r : t.requests) {
            verbs.append(QString::fromLatin1(r.verb));
        }
        QCOMPARE(verbs, (QStringList{QStringLiteral("GET"), QStringLiteral("PUT"), QStringLiteral("GET"), QStringLiteral("PUT"), QStringLiteral("GET"), QStringLiteral("PUT")}));
    }

    void thereIsNeverMoreThanOneRequestInFlight()
    {
        ScriptedTransport t;
        t.deferred = true;
        t.handler = calendarHandler();
        auto svc = service(&t, QStringLiteral("parallel"));
        svc->submit(EditCommands::calendarPatch(eventNamed(QStringLiteral("a.ics")), titleChange(QStringLiteral("A1"))));
        svc->submit(EditCommands::calendarPatch(eventNamed(QStringLiteral("a.ics")), QJsonObject{{QStringLiteral("description"), QStringLiteral("more")}}));
        svc->submit(EditCommands::calendarPatch(eventNamed(QStringLiteral("b.ics")), titleChange(QStringLiteral("B"))));
        QCOMPARE(t.held.size(), 1); // only the first GET is out
        // The second edit of the item that is being sent was NOT folded into the command in flight.
        QCOMPARE(svc->queue().pendingCount(), 3);
        int guard = 0;
        while (t.releaseOne() && guard++ < 20) {
            QVERIFY(t.held.size() <= 1);
        }
        QCOMPARE(svc->queue().pendingCount(), 0);
        QCOMPARE(t.count("PUT"), 3);
    }

    void aConflictStaysUntilTheClickAndSurvivesARestart()
    {
        ScriptedTransport t;
        t.handler = [](const HttpRequest &r) {
            if (r.verb == "GET") {
                return ScriptedTransport::answer(200, ics(QStringLiteral("a")), {{QByteArrayLiteral("etag"), QByteArrayLiteral("\"e9\"")}}); // changed elsewhere
            }
            return ScriptedTransport::answer(500);
        };
        const QString sub = QStringLiteral("conflict");
        {
            auto svc = service(&t, sub);
            svc->submit(EditCommands::calendarPatch(eventNamed(QStringLiteral("a.ics")), titleChange(QStringLiteral("Mine"))));
            QCOMPARE(svc->queue().pendingCount(), 0);
            QCOMPARE(svc->queue().conflicts().size(), 1);
            const auto &c = svc->queue().conflicts().first();
            QCOMPARE(c.conflictKind, QStringLiteral("changed"));
            QVERIFY(c.message.contains(QStringLiteral("NoAgenda kept the other change")));
            QCOMPARE(t.count("PUT"), 0); // the other change wins: nothing was written
            // The row shows the other change and a "not sent" mark.
            QList<AgendaItem> rows{eventNamed(QStringLiteral("a.ics"))};
            svc->overlay(&rows, QDate(2026, 10, 28));
            QCOMPARE(rows.first().title, QStringLiteral("a.ics"));
            QCOMPARE(rows.first().pending, PendingState::Conflict);
            // Sending again does not drop it.
            svc->retry();
            QCOMPARE(svc->queue().conflicts().size(), 1);
        }
        // After a restart it is still there, until a click.
        auto again = service(&t, sub);
        again->load();
        QCOMPARE(again->queue().conflicts().size(), 1);
        const QString id = again->queue().conflicts().first().id;
        again->keepOther(id);
        QCOMPARE(again->queue().conflicts().size(), 0);
        auto third = service(&t, sub);
        third->load();
        QCOMPARE(third->queue().conflicts().size(), 0); // the click is saved too
    }

    void applyMyChangeAgainRereadsAndAppliesTheSameChangeOnTopOfTheNewVersion()
    {
        ScriptedTransport t;
        t.handler = [](const HttpRequest &r) {
            if (r.verb == "GET") {
                return ScriptedTransport::answer(200, ics(QStringLiteral("a")), {{QByteArrayLiteral("etag"), QByteArrayLiteral("\"e9\"")}}); // changed elsewhere
            }
            return ScriptedTransport::answer(204, QByteArray(), {{QByteArrayLiteral("etag"), QByteArrayLiteral("\"e10\"")}});
        };
        auto svc = service(&t, QStringLiteral("again"));
        svc->submit(EditCommands::calendarPatch(eventNamed(QStringLiteral("a.ics")), titleChange(QStringLiteral("Mine"))));
        QCOMPARE(svc->queue().conflicts().size(), 1); // the item is e9, the person saw e1
        QCOMPARE(t.count("PUT"), 0);
        t.requests.clear();
        const QString id = svc->queue().conflicts().first().id;
        svc->applyAgain(id);
        QCOMPARE(svc->queue().conflicts().size(), 0);
        QCOMPARE(svc->queue().pendingCount(), 0);
        QCOMPARE(t.count("GET"), 1); // one fresh read
        QCOMPARE(t.count("PUT"), 1); // one write, with the new ETag
        QCOMPARE(ScriptedTransport::header(t.of("PUT").first(), "If-Match"), QByteArrayLiteral("\"e9\""));
        QVERIFY(QString::fromUtf8(t.of("PUT").first().body).contains(QStringLiteral("SUMMARY:Mine")));
    }

    void aRefusedChangeIsKeptUntilTheClickToo()
    {
        ScriptedTransport t;
        t.handler = [](const HttpRequest &) { return ScriptedTransport::answer(403); };
        auto svc = service(&t, QStringLiteral("refused"));
        svc->submit(EditCommands::calendarPatch(eventNamed(QStringLiteral("a.ics")), titleChange(QStringLiteral("Mine"))));
        QCOMPARE(svc->queue().conflicts().size(), 1);
        QCOMPARE(svc->queue().conflicts().first().conflictKind, QStringLiteral("refused"));
    }

    void aSecondCommandOfTheSameItemUsesTheNewVersionOfTheFirst()
    {
        ScriptedTransport t;
        t.deferred = true;
        t.handler = calendarHandler();
        auto svc = service(&t, QStringLiteral("rebase"));
        const auto item = eventNamed(QStringLiteral("a.ics"));
        svc->submit(EditCommands::calendarPatch(item, titleChange(QStringLiteral("One"))));
        svc->submit(EditCommands::calendarDelete(item)); // another kind of command for the same item: not folded in
        int guard = 0;
        while (t.releaseOne() && guard++ < 20) {
        }
        QVERIFY(svc->queue().conflicts().isEmpty());
        QCOMPARE(svc->queue().pendingCount(), 0);
        // The delete used the ETag that the first command's PUT returned.
        QCOMPARE(ScriptedTransport::header(t.of("DELETE").first(), "If-Match"), QByteArrayLiteral("\"e2\""));
    }

    void theOverlayDrawsEveryKindOfPendingChange()
    {
        ScriptedTransport t;
        t.offline = true;
        auto svc = service(&t, QStringLiteral("overlay"));
        auto event = eventNamed(QStringLiteral("a.ics"));
        auto gantt = ganttItem();
        auto card = cardItem();
        auto todo = eventNamed(QStringLiteral("t.ics"));
        todo.kind = ItemKind::Todo;
        todo.home = Home::Tasks;
        todo.timed = false;
        todo.due = QDate(2026, 10, 28);
        svc->submit(EditCommands::calendarPatch(event, QJsonObject{{QStringLiteral("start"), QStringLiteral("2026-10-28T13:00:00Z")}, {QStringLiteral("end"), QStringLiteral("2026-10-28T14:30:00Z")},
            {QStringLiteral("agent"), QJsonObject{{QStringLiteral("id"), QStringLiteral("AG-0001")}, {QStringLiteral("tag"), QStringLiteral("agent:AG-0001")}, {QStringLiteral("removeTags"), QJsonArray()}}}}));
        svc->submit(EditCommands::ganttUpdate(gantt, QJsonObject{{QStringLiteral("end_date"), QStringLiteral("2026-11-04")}, {QStringLiteral("manual_progress"), 60}}));
        svc->submit(EditCommands::deckCard(card, QJsonObject{{QStringLiteral("due"), QStringLiteral("2026-11-05")}}));
        svc->submit(EditCommands::calendarPatch(todo, QJsonObject{{QStringLiteral("done"), true}}));
        svc->submit(EditCommands::calendarCreate(QStringLiteral("work"), false, QStringLiteral("new-1"), QJsonObject{{QStringLiteral("title"), QStringLiteral("Planned")}, {QStringLiteral("start"), QStringLiteral("2026-10-29T09:00:00Z")}}, QStringLiteral("Planned")));
        QList<AgendaItem> rows{event, gantt, card, todo};
        svc->overlay(&rows, QDate(2026, 10, 28));
        QCOMPARE(rows.size(), 5); // the new event is a synthetic row
        QCOMPARE(rows.at(0).start, QDateTime(QDate(2026, 10, 28), QTime(13, 0), QTimeZone::UTC));
        QCOMPARE(rows.at(0).agent.id, QStringLiteral("AG-0001"));
        QCOMPARE(rows.at(0).workKind, WorkKind::MyAgent);
        QCOMPARE(rows.at(1).endDate, QDate(2026, 11, 4));
        QCOMPARE(rows.at(1).percentDone, 60);
        QCOMPARE(rows.at(2).due, QDate(2026, 11, 5));
        QVERIFY(rows.at(3).done);
        QCOMPARE(rows.at(4).title, QStringLiteral("Planned"));
        QCOMPARE(rows.at(4).pending, PendingState::Waiting);
        for (const auto &r : rows) {
            QCOMPARE(r.pending, PendingState::Waiting);
        }
    }

    void aDeleteTakesTheRowOutAndAgentSelfPutsBackSelf()
    {
        ScriptedTransport t;
        t.offline = true;
        auto svc = service(&t, QStringLiteral("overlay2"));
        auto event = eventNamed(QStringLiteral("a.ics"));
        auto other = eventNamed(QStringLiteral("b.ics"));
        other.agent.isSelf = false;
        other.agent.id = QStringLiteral("AG-0001");
        other.workKind = WorkKind::MyAgent;
        svc->submit(EditCommands::calendarDelete(event));
        svc->submit(EditCommands::calendarPatch(other, QJsonObject{{QStringLiteral("agent"), QJsonObject{{QStringLiteral("id"), QString()}, {QStringLiteral("tag"), QString()}, {QStringLiteral("removeTags"), QJsonArray()}}}}));
        QList<AgendaItem> rows{event, other};
        svc->overlay(&rows, QDate(2026, 10, 28));
        QCOMPARE(rows.size(), 1);
        QVERIFY(rows.first().agent.isSelf);
        QCOMPARE(rows.first().workKind, WorkKind::Self);
    }
};

QTEST_GUILESS_MAIN(TestNoDriveAgendaQueue)
#include "testnodriveagendaqueue.moc"
