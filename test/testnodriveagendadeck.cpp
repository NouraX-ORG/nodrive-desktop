/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: NoBoard (Deck 1.19.1) - boards, cards, the agent label and the version guard.

#include "nodrive/noagenda/agendatestsupport.h"

#include "gui/nourax/noagenda/logic/deckmapper.h"

#include <QJsonArray>

using namespace AgendaTest;

class TestNoDriveAgendaDeck : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void theVersionGuardKnowsOnlyTheTestedMinor()
    {
        QVERIFY(DeckClient::versionIsTested(QStringLiteral("1.19.1")));
        QVERIFY(DeckClient::versionIsTested(QStringLiteral("1.19.7")));
        QVERIFY(!DeckClient::versionIsTested(QStringLiteral("1.20.0")));
        QVERIFY(!DeckClient::versionIsTested(QStringLiteral("2.0.0")));
        QVERIFY(!DeckClient::versionIsTested(QStringLiteral("1.18.6")));
        QVERIFY(!DeckClient::versionIsTested(QString()));
    }

    void readsTheBoardAndItsStacks()
    {
        FakeTransport transport;
        const auto input = loadScenario(QStringLiteral("ali"), &transport);
        QVERIFY(input.deck.loaded);
        QCOMPARE(input.deck.version, QStringLiteral("1.19.1"));
        QCOMPARE(input.deck.boards.size(), 1);
        const auto board = input.deck.boards.first().toObject();
        QCOMPARE(board.value(QLatin1String("_stacks")).toArray().size(), 1);
        QCOMPARE(board.value(QLatin1String("_stacks")).toArray().first().toObject().value(QLatin1String("cards")).toArray().size(), 8);
        // Every request has the CSRF header, so the server's CORS check does not log the session out.
        for (const auto &request : transport.requests) {
            if (request.url.path().contains(QStringLiteral("/apps/deck/"))) {
                bool ocs = false;
                for (const auto &h : request.headers) {
                    ocs = ocs || (h.first == "OCS-APIRequest" && h.second == "true");
                }
                QVERIFY(ocs);
            }
        }
    }

    void aServerWithoutTheBoardAppIsUnavailable()
    {
        FakeTransport transport;
        DeckFetchOptions options;
        options.davUser = QStringLiteral("ali");
        options.version = QString(); // no "deck" capability
        options.now = fixedNow();
        DeckFetchResult result;
        DeckClient::fetch(&transport, options, DeckSnapshot(), [&result](const DeckFetchResult &r) { result = r; });
        QVERIFY(result.ok);
        QVERIFY(result.snapshot.unavailable);
        QCOMPARE(transport.requests.size(), 0); // not even asked
    }

    void aFailureKeepsThePreviousBoards()
    {
        FakeTransport transport;
        const auto input = loadScenario(QStringLiteral("ali"), &transport);
        transport.offline = true;
        DeckFetchOptions options;
        options.davUser = QStringLiteral("ali");
        options.version = QStringLiteral("1.19.1");
        options.now = fixedNow().addSecs(600);
        DeckFetchResult result;
        DeckClient::fetch(&transport, options, input.deck, [&result](const DeckFetchResult &r) { result = r; });
        QVERIFY(!result.ok);
        QCOMPARE(result.snapshot.boards.size(), 1);
    }

    void aHiddenBoardStaysInTheSourcesAndCanBeShownAgain()
    {
        FakeTransport transport;
        auto input = loadScenario(QStringLiteral("ali"), &transport);
        const auto stacksPath = QStringLiteral("/index.php/apps/deck/api/v1.0/boards/10/stacks");
        const int stacksBefore = transport.count("GET", stacksPath);

        // The person switches the board to Hidden: the next refresh does not read its cards...
        input.settings.setBoardMode(10, WorkMode::Hidden);
        DeckFetchOptions options;
        options.davUser = QStringLiteral("ali");
        options.version = QStringLiteral("1.19.1");
        options.now = fixedNow().addSecs(300);
        options.wantBoard = [&input](const BoardRelation &relation) { return input.settings.boardMode(relation) != WorkMode::Hidden; };
        DeckFetchResult hidden;
        DeckClient::fetch(&transport, options, input.deck, [&hidden](const DeckFetchResult &r) { hidden = r; });
        QVERIFY(hidden.ok);
        QCOMPARE(transport.count("GET", stacksPath), stacksBefore);
        // ...but the board itself is still in the snapshot, so the Sources screen still lists it.
        QCOMPARE(hidden.snapshot.boards.size(), 1);
        QVERIFY(!hidden.snapshot.boards.first().toObject().contains(QLatin1String("_stacks")));
        input.deck = hidden.snapshot;
        const auto out = AgendaBuilder::build(input);
        const SourceEntry *entry = nullptr;
        for (const auto &e : out.sources) {
            if (e.type == SourceEntry::Type::Board && e.key == QLatin1String("10")) {
                entry = &e;
            }
        }
        QVERIFY2(entry, "the hidden board must stay on the Sources screen");
        QCOMPARE(entry->mode, QStringLiteral("hidden"));
        for (const auto &i : out.items) {
            QVERIFY(i.boardId != 10);
        }

        // Switched back: the cards are read again.
        input.settings.setBoardMode(10, WorkMode::Do);
        options.now = fixedNow().addSecs(600);
        DeckFetchResult shown;
        DeckClient::fetch(&transport, options, input.deck, [&shown](const DeckFetchResult &r) { shown = r; });
        QVERIFY(shown.ok);
        QCOMPARE(transport.count("GET", stacksPath), stacksBefore + 1);
        QVERIFY(shown.snapshot.boards.first().toObject().contains(QLatin1String("_stacks")));
    }

    void myCardsHaveTheirAgentFromTheLabel()
    {
        const auto out = AgendaBuilder::build(loadAliAnswered());
        const auto *call = find(out.items, QStringLiteral("deck:card:502"));
        QVERIFY(call);
        QCOMPARE(call->workKind, WorkKind::MyAgent);
        QCOMPARE(call->agent.displayText(), QStringLiteral("Writer-Bot"));
        const auto *pay = find(out.items, QStringLiteral("deck:card:503"));
        QVERIFY(pay);
        QVERIFY(pay->agent.isSelf);
        QVERIFY(pay->dueToday);
        QCOMPARE(pay->due, QDate(2026, 10, 28));
    }

    void aCardWithoutADueDateHasNoDate()
    {
        const auto out = AgendaBuilder::build(loadAliAnswered());
        const auto *idea = find(out.items, QStringLiteral("deck:card:505"));
        QVERIFY(idea);
        QVERIFY(!idea->hasDate());
    }

    void anUnknownRobotLabelIsFlagged()
    {
        const auto out = AgendaBuilder::build(loadAliAnswered());
        const auto *stranger = find(out.items, QStringLiteral("deck:card:507"));
        QVERIFY(stranger);
        QVERIFY(stranger->agent.unknown);
        QVERIFY(hasProblem(*stranger, ProblemFlag::Kind::UnknownAgent));
    }

    void doneUnassignedAndOtherPeoplesCardsAreNotInMyList()
    {
        const auto out = AgendaBuilder::build(loadAliAnswered());
        QVERIFY(!find(out.items, QStringLiteral("deck:card:506"))); // done
        QVERIFY(!find(out.items, QStringLiteral("deck:card:508"))); // nobody
        QVERIFY(!find(out.items, QStringLiteral("deck:card:504"))); // Sara's: Ali does not run this board
    }

    void aLinkedGanttTaskAndCardAreOneRow()
    {
        const auto out = AgendaBuilder::build(loadAliAnswered());
        QVERIFY(find(out.items, QStringLiteral("gantt:task:111")));
        QVERIFY(!find(out.items, QStringLiteral("deck:card:501")));
        QVERIFY(find(out.items, QStringLiteral("gantt:task:111"))->boardBadge);
    }

    void aBoardIAmOnlyAMemberOfIsDoAndOneIRunIsDoOversee()
    {
        {
            const auto ali = loadScenario(QStringLiteral("ali"));
            BuildContext ctx;
            ctx.me = QStringLiteral("ali");
            const auto relation = DeckMapper::relationOf(ctx, ali.deck.boards.first().toObject());
            QVERIFY(!relation.owned);
            QVERIFY(!relation.canManage);
            QCOMPARE(relation.defaultMode(), WorkMode::Do);
        }
        {
            const auto mara = loadScenario(QStringLiteral("mara"));
            BuildContext ctx;
            ctx.me = QStringLiteral("mara");
            const auto relation = DeckMapper::relationOf(ctx, mara.deck.boards.first().toObject());
            QVERIFY(relation.owned);
            QVERIFY(relation.canManage);
            QCOMPARE(relation.defaultMode(), WorkMode::DoOversee);
        }
    }

    void theManagerSeesTheProblemsOfTheBoard()
    {
        const auto out = AgendaBuilder::build(loadScenario(QStringLiteral("mara")));
        const auto *late = find(out.items, QStringLiteral("deck:card:504"));
        QVERIFY(late);
        QCOMPARE(late->workKind, WorkKind::Team);
        QCOMPARE(late->personName, QStringLiteral("Sara"));
        QCOMPARE(late->agent.displayText(), QStringLiteral("Design-Bot"));
        QVERIFY(hasProblem(*late, ProblemFlag::Kind::Late));
        QVERIFY(hasProblem(*find(out.items, QStringLiteral("deck:card:503")), ProblemFlag::Kind::DueToday));
        const auto *backlog = find(out.items, QStringLiteral("deck:card:508"));
        QVERIFY(backlog);
        QVERIFY(hasProblem(*backlog, ProblemFlag::Kind::NotAssigned));
        // Nothing wrong with this one: it is a team row without a problem.
        QVERIFY(find(out.items, QStringLiteral("deck:card:502"))->problems.isEmpty());
    }

    void aNewerDeckMakesTheCardsReadOnly()
    {
        auto input = loadAliAnswered();
        input.deck.version = QStringLiteral("1.20.0");
        const auto out = AgendaBuilder::build(input);
        QVERIFY(out.boardReadOnlyByGuard);
        const auto *call = find(out.items, QStringLiteral("deck:card:502"));
        QVERIFY(call->versionReadOnly);
        // The tested version is not read only.
        QVERIFY(!AgendaBuilder::build(loadAliAnswered()).boardReadOnlyByGuard);
    }

    void theSnapshotSurvivesItsCacheForm()
    {
        const auto input = loadScenario(QStringLiteral("ali"));
        const auto copy = DeckSnapshot::fromJson(input.deck.toJson());
        QVERIFY(copy.loaded);
        QCOMPARE(copy.version, QStringLiteral("1.19.1"));
        QCOMPARE(copy.boards.size(), 1);
    }
};

QTEST_GUILESS_MAIN(TestNoDriveAgendaDeck)
#include "testnodriveagendadeck.moc"
