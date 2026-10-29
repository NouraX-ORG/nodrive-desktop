/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda (edit step): "my agent" on a NoGantt task (design 4.5 and 9). The new list of people keeps every other
// row exactly, and the change is refused whenever the server could drop or change something else.

#include "nodrive/noagenda/edittestsupport.h"

#include "gui/nourax/noagenda/edit/ganttagentchange.h"
#include "gui/nourax/noagenda/edit/ganttwriter.h"

using namespace AgendaTest;
namespace GAC = OCC::Nourax::Agenda::GanttAgentChange;

namespace {

QJsonObject row(int resourceId, const QJsonValue &work)
{
    return QJsonObject{{QStringLiteral("id"), resourceId * 10}, {QStringLiteral("task_id"), 111}, {QStringLiteral("resource_id"), resourceId}, {QStringLiteral("work"), work}, {QStringLiteral("sort_order"), 0}};
}

QJsonObject task(const QJsonArray &rows, const QJsonValue &work)
{
    return QJsonObject{{QStringLiteral("id"), 111}, {QStringLiteral("title"), QStringLiteral("Landing page build")}, {QStringLiteral("work"), work}, {QStringLiteral("resources"), rows}};
}

QJsonValue none()
{
    return QJsonValue(QJsonValue::Null);
}

QList<int> ids(const QJsonArray &rows)
{
    QList<int> out;
    for (const auto &r : rows) {
        out.append(r.toObject().value(QStringLiteral("resource_id")).toInt());
    }
    return out;
}

// Resources of the fixtures: 1 Ali, 2 Sara, 11 Writer-Bot (Ali), 12 Design-Bot (Sara), 13 Research-Bot (Ali), 14 Temp worker, 20 Designer (role, PT)

} // namespace

class TestNoDriveAgendaGanttAgent : public QObject
{
    Q_OBJECT

    static GanttAgentPlan plan(const QJsonObject &fresh, const QString &agent, const QJsonArray &resources = QJsonArray(), const AgentRegistry *registry = nullptr)
    {
        const auto ctx = aliContext();
        return GAC::plan(fresh, resources.isEmpty() ? ctx.ganttResources : resources, registry ? *registry : ctx.registry, QStringLiteral("ali"), QStringLiteral("Ali"), agent, 8.0);
    }

private Q_SLOTS:
    void fromSelfToMyAgentAddsOneRowAndKeepsMine()
    {
        const auto fresh = task({row(1, 6)}, 0.75);
        const auto p = plan(fresh, QStringLiteral("AG-0001"));
        QVERIFY2(p.ok, qPrintable(p.refusal));
        QCOMPARE(ids(p.resources), (QList<int>{1, 11}));
        QCOMPARE(p.resources.at(0).toObject().value(QStringLiteral("work")).toDouble(), 6.0); // my own row is untouched
        QVERIFY(p.resources.at(1).toObject().value(QStringLiteral("work")).isNull());
    }

    void fromMyAgentBackToSelfRemovesOnlyThatRow()
    {
        const auto fresh = task({row(1, 6), row(11, none())}, 0.75);
        const auto p = plan(fresh, QString());
        QVERIFY2(p.ok, qPrintable(p.refusal));
        QCOMPARE(ids(p.resources), (QList<int>{1}));
    }

    void otherPeoplesRowsKeepTheirOrderAndTheirWork()
    {
        // Ali (6 h), a temp worker (5 h), Sara (2 h): 13 h = 1.625 person days
        const auto fresh = task({row(1, 6), row(14, 5), row(2, 2)}, 1.625);
        const auto p = plan(fresh, QStringLiteral("AG-0001"));
        QVERIFY2(p.ok, qPrintable(p.refusal));
        QCOMPARE(ids(p.resources), (QList<int>{1, 14, 2, 11}));
        QCOMPARE(p.resources.at(1).toObject().value(QStringLiteral("work")).toDouble(), 5.0);
        QCOMPARE(p.resources.at(2).toObject().value(QStringLiteral("work")).toDouble(), 2.0);
    }

    void myAgentIsReplacedByAnotherOfMineInPlaceOfNothingElse()
    {
        // The paused Research-Bot may not be chosen; but a task that has it can go back to Self.
        const auto fresh = task({row(1, 6), row(13, none())}, 0.75);
        QVERIFY(plan(fresh, QString()).ok);
        const auto toPaused = plan(task({row(1, 6)}, 0.75), QStringLiteral("AG-0003"));
        QVERIFY(!toPaused.ok);
        const auto toRetired = plan(task({row(1, 6)}, 0.75), QStringLiteral("AG-0004"));
        QVERIFY(!toRetired.ok);
    }

    void anAgentOfSomeoneElseIsNeverChosen()
    {
        const auto p = plan(task({row(1, 6)}, 0.75), QStringLiteral("AG-0002")); // Design-Bot belongs to Sara
        QVERIFY(!p.ok);
        QVERIFY(p.refusal.contains(QStringLiteral("not one of yours")));
        QVERIFY(!plan(task({row(1, 6)}, 0.75), QStringLiteral("AG-9999")).ok);
    }

    void aTaskWithFourRowsIsRefused()
    {
        const auto fresh = task({row(1, 2), row(14, 2), row(2, 2), row(20, 1)}, 0.875);
        const auto p = plan(fresh, QStringLiteral("AG-0001"));
        QVERIFY(!p.ok);
        QCOMPARE(p.refusal, QStringLiteral("This task already has 4 people or agents. Ask your project manager."));
    }

    void aChangeThatWouldChangeTheWorkEstimateIsRefused()
    {
        // The agent's own row holds 6 h: removing it would remove 0.75 person days from the task.
        const auto withWork = task({row(1, 2), row(11, 6)}, 1.0);
        const auto p = plan(withWork, QString());
        QVERIFY(!p.ok);
        QCOMPARE(p.refusal, QStringLiteral("Changing the agent here would change the task's work estimate. Open in NoGantt."));
        // The task has a work estimate but no row has any: sending the rows would erase it.
        const auto erased = task({row(1, none())}, 1.0);
        QVERIFY(!plan(erased, QStringLiteral("AG-0001")).ok);
        // A role row counts in person days (unit PT), not in hours: 2 PT + 2 h of Ali = 2.25 PT
        const auto pt = task({row(1, 2), row(20, 2)}, 2.25);
        QVERIFY2(plan(pt, QStringLiteral("AG-0001")).ok, "person days are converted with the hours per person day");
        const auto ptWrong = task({row(1, 2), row(20, 2)}, 0.5);
        QVERIFY(!plan(ptWrong, QStringLiteral("AG-0001")).ok);
    }

    void aRowThatIsNotVisibleToMeStopsTheChange()
    {
        const auto fresh = task({row(1, 2), row(999, 1)}, 0.375);
        const auto p = plan(fresh, QStringLiteral("AG-0001"));
        QVERIFY(!p.ok);
        QVERIFY(p.refusal.contains(QStringLiteral("not visible to you")));
    }

    void iMustBeOnTheTask()
    {
        const auto p = plan(task({row(2, 2)}, 0.25), QStringLiteral("AG-0001"));
        QVERIFY(!p.ok);
        QVERIFY(p.refusal.contains(QStringLiteral("not on this task")));
    }

    void thePrimaryRowNeverChanges()
    {
        const auto fresh = task({row(11, none()), row(1, 6)}, 0.75);
        const auto p = plan(fresh, QString());
        QVERIFY(!p.ok);
        QVERIFY(p.refusal.contains(QStringLiteral("main person")));
    }

    void nothingIsSentWhenThereIsNothingToChange()
    {
        const auto fresh = task({row(1, 6)}, 0.75);
        const auto p = plan(fresh, QString());
        QVERIFY(p.ok);
        QVERIFY(p.noChange);
        const auto already = task({row(1, 6), row(11, none())}, 0.75);
        const auto same = plan(already, QStringLiteral("AG-0001"));
        QVERIFY(same.ok);
        QVERIFY(same.noChange);
    }

    void anAgentWhosePlanningPersonIsNotVisibleAsksTheProjectManager()
    {
        QJsonArray visible;
        for (const auto &r : aliContext().ganttResources) {
            if (r.toObject().value(QStringLiteral("id")).toInt() != 11) {
                visible.append(r);
            }
        }
        const auto p = plan(task({row(1, 6)}, 0.75), QStringLiteral("AG-0001"), visible);
        QVERIFY(!p.ok);
        const QString robot = QString::fromUcs4(U"\U0001F916");
        QCOMPARE(p.refusal, QStringLiteral("Ask your project manager to add %1 Writer-Bot to this project").arg(robot));
        const auto choices = GAC::choices(QStringLiteral("ali"), QStringLiteral("Ali"), aliContext().registry, visible);
        for (const auto &c : choices) {
            if (c.agentId == QLatin1String("AG-0001")) {
                QVERIFY(!c.choosable());
                QVERIFY(!c.askManagerText.isEmpty());
            }
        }
    }

    void anAgentIsFoundByItsExactNameWhenTheRegistryHasNoId()
    {
        auto doc = QJsonDocument::fromJson(fixture(QStringLiteral("agent-registry.json")));
        auto root = doc.object();
        auto agents = root.value(QStringLiteral("agents")).toArray();
        auto first = agents.at(0).toObject();
        auto links = first.value(QStringLiteral("links")).toObject();
        links.insert(QStringLiteral("nogantt_resource_ids"), QJsonArray());
        first.insert(QStringLiteral("links"), links);
        agents.replace(0, first);
        root.insert(QStringLiteral("agents"), agents);
        const auto registry = AgentRegistry::parse(QJsonDocument(root).toJson());
        const auto p = plan(task({row(1, 6)}, 0.75), QStringLiteral("AG-0001"), QJsonArray(), &registry);
        QVERIFY2(p.ok, qPrintable(p.refusal));
        QCOMPARE(ids(p.resources), (QList<int>{1, 11}));
    }

    void theReturnedListMustBeTheListThatWasSent()
    {
        const QJsonArray sent{row(1, 6), row(11, none())};
        QVERIFY(GAC::sameResources(sent, sent));
        QVERIFY(!GAC::sameResources(sent, QJsonArray{row(1, 6)}));
        QVERIFY(!GAC::sameResources(sent, QJsonArray{row(1, 6), row(13, none())}));
        QVERIFY(!GAC::sameResources(sent, QJsonArray{row(1, 5), row(11, none())}));
        QVERIFY(!GAC::sameResources(sent, QJsonArray{row(1, 6), row(11, 3)}));
        // the server adds fields of its own (id, task_id, sort_order): they do not count
        QVERIFY(GAC::sameResources(sent, QJsonArray{row(1, 6.0), row(11, none())}));
    }

    void theWriterSendsTheNewListAndChecksTheAnswer()
    {
        const auto context = aliContext();
        const QJsonObject fresh = QJsonObject{{QStringLiteral("id"), 111}, {QStringLiteral("title"), QStringLiteral("Landing page build")}, {QStringLiteral("updated_at"), QStringLiteral("2026-10-27 09:00:00")},
            {QStringLiteral("work"), 0.75}, {QStringLiteral("resources"), QJsonArray{row(1, 6)}}};
        for (const bool honest : {true, false}) {
            ScriptedTransport t;
            t.handler = [&](const HttpRequest &r) {
                const QString path = r.url.path();
                if (r.verb == "GET" && path.endsWith(QStringLiteral("/projects/1/tasks"))) {
                    return ScriptedTransport::answer(200, ocsBody(QJsonArray{fresh}));
                }
                if (r.verb == "GET" && path.endsWith(QStringLiteral("/resources"))) {
                    return ScriptedTransport::answer(200, ocsBody(context.ganttResources));
                }
                if (r.verb == "PUT") {
                    QJsonObject saved = fresh;
                    saved.insert(QStringLiteral("updated_at"), QStringLiteral("2026-10-28 12:00:00"));
                    // a dishonest server answers with the old list
                    saved.insert(QStringLiteral("resources"), honest ? parseObject(r.body).value(QStringLiteral("resources")) : QJsonValue(QJsonArray{row(1, 6)}));
                    return ScriptedTransport::answer(200, ocsBody(QJsonObject{{QStringLiteral("task"), saved}, {QStringLiteral("propagated"), QJsonArray()}, {QStringLiteral("op_id"), none()}, {QStringLiteral("deck"), QJsonObject()}}));
                }
                return ScriptedTransport::answer(404);
            };
            const auto command = EditCommands::ganttAgent(ganttItem(), QStringLiteral("AG-0001"), QStringLiteral("ali"));
            EditResult result;
            GanttWriter::run(&t, command, context, [&](const EditResult &r) { result = r; });
            QVERIFY2(result.done(), qPrintable(result.message));
            const auto body = parseObject(t.of("PUT").first().body);
            const auto sent = body.value(QStringLiteral("resources")).toArray();
            QCOMPARE(ids(sent), (QList<int>{1, 11}));
            QVERIFY(body.contains(QStringLiteral("deck_confirmed")));
            QVERIFY(!body.contains(QStringLiteral("start_date"))); // only the people are sent
            QCOMPARE(result.checkInNoGantt, !honest);
            if (!honest) {
                QCOMPARE(result.note, QStringLiteral("Check this task in NoGantt"));
            }
        }
    }

    void theWriterSendsNothingWhenThePlanIsRefused()
    {
        const auto context = aliContext();
        const QJsonObject fresh = QJsonObject{{QStringLiteral("id"), 111}, {QStringLiteral("updated_at"), QStringLiteral("2026-10-27 09:00:00")}, {QStringLiteral("work"), 1.0},
            {QStringLiteral("resources"), QJsonArray{row(1, 2), row(11, 6)}}};
        ScriptedTransport t;
        t.handler = [&](const HttpRequest &r) {
            if (r.verb == "GET" && r.url.path().endsWith(QStringLiteral("/tasks"))) {
                return ScriptedTransport::answer(200, ocsBody(QJsonArray{fresh}));
            }
            if (r.verb == "GET") {
                return ScriptedTransport::answer(200, ocsBody(context.ganttResources));
            }
            return ScriptedTransport::answer(500);
        };
        const auto command = EditCommands::ganttAgent(ganttItem(), QString(), QStringLiteral("ali"));
        EditResult result;
        GanttWriter::run(&t, command, context, [&](const EditResult &r) { result = r; });
        QCOMPARE(result.outcome, EditResult::Outcome::Refused);
        QCOMPARE(t.count("PUT"), 0);
    }
};

QTEST_GUILESS_MAIN(TestNoDriveAgendaGanttAgent)
#include "testnodriveagendaganttagent.moc"
