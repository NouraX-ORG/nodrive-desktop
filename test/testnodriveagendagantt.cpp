/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: NoGantt 2.4.1 - the OCS envelope, the version guard (contract), the reader and the agents
// of an assignment (design 4.2, 4.4, 4.5).

#include "nodrive/noagenda/agendatestsupport.h"

#include "gui/nourax/noagenda/gantt/ganttcontract.h"
#include "gui/nourax/noagenda/logic/ganttmapper.h"
#include "gui/nourax/noagenda/ocs/ocsjson.h"

#include <QJsonArray>

using namespace AgendaTest;

class TestNoDriveAgendaGantt : public QObject
{
    Q_OBJECT

    static void setResourceField(BuildInput *input, int id, const QString &key, const QJsonValue &value)
    {
        QJsonArray out;
        for (const auto &r : std::as_const(input->gantt.resources)) {
            auto o = r.toObject();
            if (o.value(QLatin1String("id")).toInt() == id) {
                o.insert(key, value);
            }
            out.append(o);
        }
        input->gantt.resources = out;
    }

private Q_SLOTS:
    void unwrapsTheOcsEnvelope()
    {
        HttpResult result;
        result.status = 200;
        result.body = ocsEnvelope(QByteArrayLiteral("{\"has_linked_resource\":true,\"tasks\":[]}"));
        const auto reply = parseOcs(result);
        QVERIFY(reply.ok());
        QCOMPARE(reply.ocsStatus, 200);
        QVERIFY(reply.data.isObject());

        result.body = QByteArrayLiteral("<html>not found</html>");
        result.status = 404;
        QVERIFY(!parseOcs(result).ok());
        QVERIFY(parseOcs(result).notFound());
        result.status = 200;
        result.body = QByteArrayLiteral("{\"other\":1}");
        QVERIFY(!parseOcs(result).envelopeOk);
    }

    void everyFixtureEndpointPassesTheContract()
    {
        using GanttContract::Endpoint;
        QCOMPARE(GanttContract::check(Endpoint::MyTasks, fixtureJson(QStringLiteral("gantt/my-tasks-ali.json"))), QString());
        QCOMPARE(GanttContract::check(Endpoint::Projects, fixtureJson(QStringLiteral("gantt/projects-ali.json"))), QString());
        QCOMPARE(GanttContract::check(Endpoint::ProjectTasks, fixtureJson(QStringLiteral("gantt/tasks-1.json"))), QString());
        QCOMPARE(GanttContract::check(Endpoint::Milestones, fixtureJson(QStringLiteral("gantt/milestones-1.json"))), QString());
        QCOMPARE(GanttContract::check(Endpoint::Subprojects, fixtureJson(QStringLiteral("gantt/subprojects-1.json"))), QString());
        QCOMPARE(GanttContract::check(Endpoint::Resources, fixtureJson(QStringLiteral("gantt/resources.json"))), QString());
        QCOMPARE(GanttContract::check(Endpoint::CapacityCheck, fixtureJson(QStringLiteral("gantt/capacity-mara.json"))), QString());
        QCOMPARE(GanttContract::check(Endpoint::TaskSync, fixtureJson(QStringLiteral("gantt/task-sync-off.json"))), QString());
        QCOMPARE(GanttContract::check(Endpoint::Shares, fixtureJson(QStringLiteral("gantt/shares-1.json"))), QString());
    }

    void aMissingOrRenamedKeyFailsLoudly()
    {
        using GanttContract::Endpoint;
        auto tasks = fixtureJson(QStringLiteral("gantt/tasks-1.json")).toArray();
        auto first = tasks.at(0).toObject();
        first.remove(QStringLiteral("resources"));
        first.insert(QStringLiteral("assignments"), QJsonArray());
        tasks.replace(0, first);
        const auto problem = GanttContract::check(Endpoint::ProjectTasks, tasks);
        QVERIFY(problem.contains(QStringLiteral("projects/{id}/tasks")));
        QVERIFY(problem.contains(QStringLiteral("resources")));
        // The sentence never holds a value of the data.
        QVERIFY(!problem.contains(QStringLiteral("Write homepage")));

        auto mine = fixtureJson(QStringLiteral("gantt/my-tasks-ali.json")).toObject();
        mine.remove(QStringLiteral("has_linked_resource"));
        QVERIFY(GanttContract::check(Endpoint::MyTasks, mine).contains(QStringLiteral("has_linked_resource")));

        auto projects = fixtureJson(QStringLiteral("gantt/projects-ali.json")).toArray();
        auto p = projects.at(0).toObject();
        p.insert(QStringLiteral("id"), QStringLiteral("one")); // wrong type
        projects.replace(0, p);
        QVERIFY(!GanttContract::check(Endpoint::Projects, projects).isEmpty());
        QVERIFY(!GanttContract::check(Endpoint::Projects, QJsonObject()).isEmpty()); // not a list
    }

    void readsTheScenarioThroughTheClient()
    {
        FakeTransport transport;
        const auto input = loadScenario(QStringLiteral("ali"), &transport);
        const auto &g = input.gantt;
        QVERIFY(g.loaded);
        QVERIFY(g.contractOk);
        QVERIFY(g.hasLinkedResource);
        QCOMPARE(g.myTasks.size(), 8);
        QCOMPARE(g.projects.size(), 2);
        QCOMPARE(g.resources.size(), 9);
        QCOMPARE(g.projectData.value(1).tasks.size(), 13);
        QCOMPARE(g.projectData.value(1).milestones.size(), 1);
        QVERIFY(g.projectData.value(1).loaded);
        // Ali is an editor, not an admin: no share list is asked.
        QCOMPARE(transport.count("GET", QStringLiteral("/ocs/v2.php/apps/nc_gantt/api/v1/projects/1/shares")), 0);
        // Every NoGantt request carries the OCS header and asks for JSON.
        for (const auto &request : transport.requests) {
            if (request.url.path().contains(QStringLiteral("/nc_gantt/"))) {
                bool ocs = false;
                for (const auto &h : request.headers) {
                    ocs = ocs || (h.first == "OCS-APIRequest" && h.second == "true");
                }
                QVERIFY2(ocs, qPrintable(request.url.path()));
                QVERIFY(request.url.query().contains(QStringLiteral("format=json")));
            }
        }
    }

    void aManagerGetsTheSharesAndTheCapacityCheck()
    {
        FakeTransport transport;
        const auto input = loadScenario(QStringLiteral("mara"), &transport);
        QCOMPARE(transport.count("GET", QStringLiteral("/ocs/v2.php/apps/nc_gantt/api/v1/projects/1/shares")), 1);
        QCOMPARE(transport.count("GET", QStringLiteral("/ocs/v2.php/apps/nc_gantt/api/v1/capacity-check")), 1);
        QVERIFY(input.gantt.projectData.value(1).sharesKnown);
        QCOMPARE(input.gantt.capacity.value(QLatin1String("findings")).toArray().size(), 1);
    }

    void aHiddenProjectIsNotRead()
    {
        FakeTransport transport;
        routeScenario(&transport, QStringLiteral("ali"));
        GanttFetchOptions options;
        options.davUser = QStringLiteral("ali");
        options.now = fixedNow();
        options.wantProject = [](const ProjectRelation &r) { return r.id == 1; };
        GanttFetchResult result;
        GanttClient::fetch(&transport, options, GanttSnapshot(), [&result](const GanttFetchResult &r) { result = r; });
        QVERIFY(result.ok);
        QVERIFY(result.snapshot.projectData.contains(1));
        QVERIFY(!result.snapshot.projectData.contains(2));
        QCOMPARE(transport.count("GET", QStringLiteral("/ocs/v2.php/apps/nc_gantt/api/v1/projects/2/tasks")), 0);
    }

    void theSharesOfAHiddenProjectAreKeptAndNotReadAgain()
    {
        FakeTransport transport;
        routeScenario(&transport, QStringLiteral("mara"));
        const auto sharesPath = QStringLiteral("/ocs/v2.php/apps/nc_gantt/api/v1/projects/1/shares");
        GanttFetchOptions options;
        options.davUser = QStringLiteral("mara");
        options.now = fixedNow();
        options.wantProject = [](const ProjectRelation &) { return true; };
        GanttFetchResult first;
        GanttClient::fetch(&transport, options, GanttSnapshot(), [&first](const GanttFetchResult &r) { first = r; });
        QVERIFY(first.ok);
        QCOMPARE(transport.count("GET", sharesPath), 1);

        // The project is switched to Hidden: no tasks are read, but its shares stay in the snapshot.
        options.now = fixedNow().addSecs(300);
        options.wantProject = [](const ProjectRelation &r) { return r.id != 1; };
        GanttFetchResult hidden;
        GanttClient::fetch(&transport, options, first.snapshot, [&hidden](const GanttFetchResult &r) { hidden = r; });
        QVERIFY(hidden.ok);
        QVERIFY(hidden.snapshot.projectData.contains(1));
        QVERIFY(hidden.snapshot.projectData.value(1).sharesKnown);
        QVERIFY(!hidden.snapshot.projectData.value(1).loaded);
        QVERIFY(hidden.snapshot.projectData.value(1).tasks.isEmpty());
        QCOMPARE(transport.count("GET", sharesPath), 1); // not read again

        // Every later refresh within the day does not read them either, hidden or shown.
        options.now = fixedNow().addSecs(600);
        GanttFetchResult again;
        GanttClient::fetch(&transport, options, hidden.snapshot, [&again](const GanttFetchResult &r) { again = r; });
        QCOMPARE(transport.count("GET", sharesPath), 1);
        options.wantProject = [](const ProjectRelation &) { return true; };
        GanttFetchResult shown;
        GanttClient::fetch(&transport, options, again.snapshot, [&shown](const GanttFetchResult &r) { shown = r; });
        QCOMPARE(transport.count("GET", sharesPath), 1);
        QVERIFY(shown.snapshot.projectData.value(1).loaded);
        QVERIFY(shown.snapshot.projectData.value(1).sharesKnown);
    }

    void noGanttOnTheServerIsUnavailableNotAnError()
    {
        FakeTransport transport; // every route answers 404
        GanttFetchOptions options;
        options.davUser = QStringLiteral("ali");
        options.now = fixedNow();
        GanttFetchResult result;
        GanttClient::fetch(&transport, options, GanttSnapshot(), [&result](const GanttFetchResult &r) { result = r; });
        QVERIFY(result.ok);
        QVERIFY(result.snapshot.unavailable);
        QVERIFY(result.snapshot.loaded);
        BuildInput input;
        input.gantt = result.snapshot;
        input.server.uid = QStringLiteral("ali");
        input.today = QDate(2026, 10, 28);
        input.zone = QTimeZone(QTimeZone::UTC);
        input.now = fixedNow();
        const auto out = AgendaBuilder::build(input);
        QVERIFY(out.ganttUnavailable);
        QVERIFY(out.items.isEmpty());
    }

    void notLinkedGivesTheNotLinkedFlag()
    {
        FakeTransport transport;
        routeScenario(&transport, QStringLiteral("ali"));
        transport.answers.removeIf([](const FakeTransport::Answer &a) { return a.path.endsWith(QLatin1String("/my-tasks")); });
        transport.routeOcs(QStringLiteral("my-tasks"), QStringLiteral("gantt/my-tasks-ali-unlinked.json"));
        GanttFetchOptions options;
        options.davUser = QStringLiteral("ali");
        options.now = fixedNow();
        GanttFetchResult result;
        GanttClient::fetch(&transport, options, GanttSnapshot(), [&result](const GanttFetchResult &r) { result = r; });
        QVERIFY(result.ok);
        QVERIFY(!result.snapshot.hasLinkedResource);
        BuildInput input;
        input.gantt = result.snapshot;
        input.server.uid = QStringLiteral("ali");
        input.today = QDate(2026, 10, 28);
        input.zone = QTimeZone(QTimeZone::UTC);
        input.now = fixedNow();
        QVERIFY(AgendaBuilder::build(input).ganttNotLinked);
    }

    void aFailureKeepsThePreviousSnapshot()
    {
        FakeTransport transport;
        auto input = loadScenario(QStringLiteral("ali"), &transport);
        transport.offline = true;
        GanttFetchOptions options;
        options.davUser = QStringLiteral("ali");
        options.now = fixedNow().addSecs(300);
        GanttFetchResult result;
        GanttClient::fetch(&transport, options, input.gantt, [&result](const GanttFetchResult &r) { result = r; });
        QVERIFY(!result.ok);
        QVERIFY(result.networkError);
        QCOMPARE(result.snapshot.myTasks.size(), input.gantt.myTasks.size());
        QCOMPARE(result.snapshot.fetchedAt, input.gantt.fetchedAt);
    }

    void aChangedShapeTurnsNoGanttReadOnly()
    {
        FakeTransport transport;
        routeScenario(&transport, QStringLiteral("ali"));
        // The tasks answer loses the "updated_at" key (as a newer NoGantt might do).
        auto tasks = fixtureJson(QStringLiteral("gantt/tasks-1.json")).toArray();
        QJsonArray changed;
        for (const auto &t : tasks) {
            auto o = t.toObject();
            o.remove(QStringLiteral("updated_at"));
            changed.append(o);
        }
        transport.answers.removeIf([](const FakeTransport::Answer &a) { return a.path.endsWith(QLatin1String("/projects/1/tasks")); });
        transport.route("GET", QStringLiteral("/ocs/v2.php/apps/nc_gantt/api/v1/projects/1/tasks"), 200, ocsEnvelope(QJsonDocument(changed).toJson()));
        GanttFetchOptions options;
        options.davUser = QStringLiteral("ali");
        options.now = fixedNow();
        GanttFetchResult result;
        GanttClient::fetch(&transport, options, GanttSnapshot(), [&result](const GanttFetchResult &r) { result = r; });
        QVERIFY(result.ok);
        QVERIFY(!result.snapshot.contractOk);
        QVERIFY(result.snapshot.contractProblem.contains(QStringLiteral("updated_at")));
        BuildInput input = loadScenario(QStringLiteral("ali"));
        input.gantt = result.snapshot;
        const auto out = AgendaBuilder::build(input);
        QVERIFY(out.ganttReadOnlyByGuard);
        int ganttRows = 0;
        for (const auto &item : out.items) {
            if (item.home == Home::Gantt) {
                QVERIFY(item.versionReadOnly);
                ++ganttRows;
            }
        }
        QVERIFY(ganttRows > 0);
    }

    void agentsOfAliAreRecognisedByIdAndByName()
    {
        const auto out = AgendaBuilder::build(loadScenario(QStringLiteral("ali")));
        // (a) the planning person is listed in the registry's links.nogantt_resource_ids
        const auto *copy = find(out.items, QStringLiteral("gantt:task:101"));
        QVERIFY(copy);
        QCOMPARE(copy->agent.displayText(), QStringLiteral("Writer-Bot"));
        QCOMPARE(copy->workKind, WorkKind::MyAgent);
        // the hours of the agent never count for Ali: only his own row (2 h)
        QCOMPARE(copy->myWorkHours, 2.0);
        // by itself
        const auto *self = find(out.items, QStringLiteral("gantt:task:102"));
        QVERIFY(self);
        QVERIFY(self->agent.isSelf);
        QCOMPARE(self->workKind, WorkKind::Self);
        QCOMPARE(self->myWorkHours, 10.0);
    }

    void aNameOnlyAgentNextToItsPersonIsRecognisedForTheManagerView()
    {
        const auto out = AgendaBuilder::build(loadScenario(QStringLiteral("mara")));
        // (b) "<robot> Design-Bot (Sara)" next to Sara (no id in the registry)
        const auto *banner = find(out.items, QStringLiteral("gantt:task:103:sara"));
        QVERIFY(banner);
        QCOMPARE(banner->agent.displayText(), QStringLiteral("Design-Bot"));
        QCOMPARE(banner->personName, QStringLiteral("Sara"));
        QVERIFY(banner->late);
        QCOMPARE(banner->lateDays, 2);
    }

    void nearMissesAreNeverGuessed()
    {
        // A trailing space in the name: not the exact name, so it is an unknown agent and a flag.
        {
            auto input = loadScenario(QStringLiteral("ali"));
            setResourceField(&input, 13, QStringLiteral("name"), QString(robot() + QStringLiteral(" Research-Bot (Ali) ")));
            const auto out = AgendaBuilder::build(input);
            const auto *item = find(out.items, QStringLiteral("gantt:task:108"));
            QVERIFY(item);
            QVERIFY(!hasProblem(*item, ProblemFlag::Kind::TwoAgents));
            QVERIFY(hasProblem(*item, ProblemFlag::Kind::UnknownAgent));
        }
        // The person's name differs: not recognised.
        {
            auto input = loadScenario(QStringLiteral("ali"));
            setResourceField(&input, 13, QStringLiteral("name"), QString(robot() + QStringLiteral(" Research-Bot (Alice)")));
            const auto built_item = AgendaBuilder::build(input);
            const auto *item = find(built_item.items, QStringLiteral("gantt:task:108"));
            QVERIFY(hasProblem(*item, ProblemFlag::Kind::UnknownAgent));
        }
        // A planning person that has an account is a person, never an agent.
        {
            auto input = loadScenario(QStringLiteral("ali"));
            setResourceField(&input, 13, QStringLiteral("uid"), QStringLiteral("someone"));
            const auto built_item = AgendaBuilder::build(input);
            const auto *item = find(built_item.items, QStringLiteral("gantt:task:108"));
            QVERIFY(!hasProblem(*item, ProblemFlag::Kind::TwoAgents));
            QVERIFY(!hasProblem(*item, ProblemFlag::Kind::UnknownAgent));
        }
        // Without the registry nothing is guessed: the agent planning people are unknown agents.
        {
            auto input = loadScenario(QStringLiteral("ali"));
            input.registry = AgentRegistry::missing();
            const auto built_item = AgendaBuilder::build(input);
            const auto *item = find(built_item.items, QStringLiteral("gantt:task:101"));
            QVERIFY(item);
            QVERIFY(item->agent.unknown); // without the registry an agent planning person is not guessed
        }
    }

    void twoAgentsOfOnePersonAreFlagged()
    {
        const auto out = AgendaBuilder::build(loadScenario(QStringLiteral("ali")));
        const auto *item = find(out.items, QStringLiteral("gantt:task:108"));
        QVERIFY(item);
        QVERIFY(hasProblem(*item, ProblemFlag::Kind::TwoAgents));
        QCOMPARE(item->agent.displayText(), QStringLiteral("Writer-Bot")); // the first is shown
    }

    void anAgentLookingLikeOneButNotInTheRegistryIsUnknown()
    {
        const auto out = AgendaBuilder::build(loadScenario(QStringLiteral("ali")));
        const auto *item = find(out.items, QStringLiteral("gantt:task:113"));
        QVERIFY(item);
        QVERIFY(item->agent.unknown);
        QVERIFY(hasProblem(*item, ProblemFlag::Kind::UnknownAgent));
        QVERIFY(item->agent.displayText().contains(QStringLiteral("Ghost-Bot")));
        QVERIFY(item->agent.displayText().contains(QStringLiteral("(unknown agent)")));
    }

    void workInPersonDaysIsConvertedToHours()
    {
        auto input = loadScenario(QStringLiteral("ali"));
        setResourceField(&input, 1, QStringLiteral("unit"), QStringLiteral("PT"));
        const auto built_item = AgendaBuilder::build(input);
        const auto *item = find(built_item.items, QStringLiteral("gantt:task:102"));
        QCOMPARE(item->myWorkHours, 80.0); // 10 person days x 8 hours (the instance's hours_per_pt)
    }

    void projectColoursNeverShowTheServerBlue()
    {
        const auto out = AgendaBuilder::build(loadScenario(QStringLiteral("ali")));
        const auto *task = find(out.items, QStringLiteral("gantt:task:102"));
        QCOMPARE(task->sourceColor, QStringLiteral("#2563EB"));
    }

    void milestonesComeWithProjectsWhereIHaveTasks()
    {
        const auto out = AgendaBuilder::build(loadScenario(QStringLiteral("ali")));
        const auto *ms = find(out.items, QStringLiteral("gantt:ms:301"));
        QVERIFY(ms);
        QCOMPARE(ms->kind, ItemKind::Milestone);
        QCOMPARE(ms->startDate, QDate(2026, 11, 10));
    }

    void theSnapshotSurvivesItsCacheForm()
    {
        const auto input = loadScenario(QStringLiteral("mara"));
        const auto copy = GanttSnapshot::fromJson(input.gantt.toJson());
        QVERIFY(copy.loaded);
        QCOMPARE(copy.projects.size(), input.gantt.projects.size());
        QCOMPARE(copy.projectData.value(1).tasks.size(), input.gantt.projectData.value(1).tasks.size());
        QCOMPARE(copy.projectData.value(1).shares.size(), input.gantt.projectData.value(1).shares.size());
        QCOMPARE(copy.capacity, input.gantt.capacity);
    }
};

QTEST_GUILESS_MAIN(TestNoDriveAgendaGantt)
#include "testnodriveagendagantt.moc"
