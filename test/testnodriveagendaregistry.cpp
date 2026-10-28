/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: the agent registry (schema nourax-agent-registry, version 1) and its swappable source.

#include "nodrive/noagenda/agendatestsupport.h"

#include "gui/nourax/noagenda/registry/driveregistrysource.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using namespace AgendaTest;

class TestNoDriveAgendaRegistry : public QObject
{
    Q_OBJECT

    static QByteArray withAgents(const QJsonArray &agents)
    {
        QJsonObject root{{"schema", "nourax-agent-registry"}, {"version", 1}, {"agents", agents}};
        return QJsonDocument(root).toJson();
    }
    static QJsonObject agent(const QString &id, const QString &name = QStringLiteral("Bot"), const QString &person = QStringLiteral("ali"),
        const QString &status = QStringLiteral("active"), const QString &org = QStringLiteral("demo-org"))
    {
        return QJsonObject{{"id", id}, {"name", name}, {"person", person}, {"status", status}, {"organisation", org}};
    }

private Q_SLOTS:
    void theFixtureParsesWithSixAgents()
    {
        const auto registry = AgentRegistry::parse(fixture(QStringLiteral("agent-registry.json")));
        QCOMPARE(registry.status(), AgentRegistry::Status::Ok);
        QCOMPARE(registry.agents().size(), 6);
        QCOMPARE(registry.skippedCount(), 0);
        QCOMPARE(registry.byId(QStringLiteral("AG-0001"))->name, QStringLiteral("Writer-Bot"));
        QVERIFY(registry.byId(QStringLiteral("AG-0003"))->isPaused());
        QVERIFY(registry.byId(QStringLiteral("AG-0004"))->isRetired());
        QVERIFY(!registry.byId(QStringLiteral("AG-9999")));
    }

    void theBundledPlaceholderFileParsesToo()
    {
        // The placeholder of plans/agent-registry.placeholder.json has four agents of two made-up persons.
        const auto json = QByteArray(R"({"schema":"nourax-agent-registry","version":1,"updated_utc":"2026-10-28T18:00:00Z","agents":[
            {"id":"AG-0001","name":"Writer-Bot","person":"00000000-0000-4000-8000-00000000000a","status":"active","organisation":"demo-org",
             "availability":{"timezone":"Europe/London","weekly":[{"days":["mon"],"from":"00:00","to":"24:00"}],"capacity_hours_per_week":40,"days_off":[]},
             "links":{"nogantt_resource_ids":[],"noboard_label":"x Writer-Bot","calendar_tag":"agent:AG-0001"}},
            {"id":"AG-0002","name":"Design-Bot","person":"00000000-0000-4000-8000-00000000000a","status":"active","organisation":"demo-org"},
            {"id":"AG-0003","name":"Research-Bot","person":"00000000-0000-4000-8000-00000000000b","status":"active","organisation":"demo-org"},
            {"id":"AG-0004","name":"Invoice-Bot","person":"00000000-0000-4000-8000-00000000000b","status":"paused","organisation":"demo-org"}]})");
        const auto registry = AgentRegistry::parse(json);
        QCOMPARE(registry.status(), AgentRegistry::Status::Ok);
        QCOMPARE(registry.agents().size(), 4);
        QCOMPARE(registry.ofPerson(QStringLiteral("00000000-0000-4000-8000-00000000000A")).size(), 2); // case does not matter
        QCOMPARE(registry.agents().first().capacityHoursPerWeek, 40.0);
    }

    void lookupsByLabelTagAndResourceId()
    {
        const auto registry = AgentRegistry::parse(fixture(QStringLiteral("agent-registry.json")));
        QCOMPARE(registry.byLabel(robot() + QStringLiteral(" Writer-Bot"))->id, QStringLiteral("AG-0001"));
        QVERIFY(!registry.byLabel(robot() + QStringLiteral(" Writer-Bot "))); // a different title
        QCOMPARE(registry.byTag(QStringLiteral("agent:AG-0002"))->name, QStringLiteral("Design-Bot"));
        QCOMPARE(registry.byResourceId(11)->id, QStringLiteral("AG-0001"));
        QVERIFY(!registry.byResourceId(12));
        QCOMPARE(registry.ofPerson(QStringLiteral("ali")).size(), 3);
        QCOMPARE(registry.organisationsOf(QStringLiteral("olaf")), QSet<QString>{QStringLiteral("other-org")});
    }

    void aLabelWithADifferentUnicodeFormStillMatches()
    {
        // "e" + combining accent against the single character: both are the same title after normalisation.
        const auto registry = AgentRegistry::parse(withAgents(QJsonArray{QJsonObject{{"id", "AG-1"}, {"name", "Café-Bot"}, {"person", "ali"}, {"status", "active"},
            {"organisation", "o"}, {"links", QJsonObject{{"noboard_label", "Café-Bot"}}}}}));
        QVERIFY(registry.byLabel(QStringLiteral("Café-Bot")));
    }

    void badSchemaOrVersionIsInvalid()
    {
        QCOMPARE(AgentRegistry::parse("not json").status(), AgentRegistry::Status::Invalid);
        QCOMPARE(AgentRegistry::parse("[]").status(), AgentRegistry::Status::Invalid);
        QCOMPARE(AgentRegistry::parse(R"({"schema":"other","version":1,"agents":[]})").status(), AgentRegistry::Status::Invalid);
        QCOMPARE(AgentRegistry::parse(R"({"schema":"nourax-agent-registry","version":2,"agents":[]})").status(), AgentRegistry::Status::Invalid);
        QCOMPARE(AgentRegistry::parse(R"({"schema":"nourax-agent-registry","version":1})").status(), AgentRegistry::Status::Invalid);
        QCOMPARE(AgentRegistry::missing().status(), AgentRegistry::Status::Missing);
    }

    void incompleteAgentsAreSkippedAndCounted()
    {
        const auto registry = AgentRegistry::parse(withAgents(QJsonArray{
            agent(QStringLiteral("AG-1")),
            agent(QString()), // no id
            agent(QStringLiteral("AG-3"), QString()), // no name
            agent(QStringLiteral("AG-4"), QStringLiteral("Bot"), QString()), // no person
            agent(QStringLiteral("AG-5"), QStringLiteral("Bot"), QStringLiteral("ali"), QStringLiteral("sleeping")), // bad status
            agent(QStringLiteral("AG-6"), QStringLiteral("Bot"), QStringLiteral("ali"), QStringLiteral("active"), QString()), // no organisation
            QJsonValue(1).toObject(), // not an agent
        }));
        QCOMPARE(registry.status(), AgentRegistry::Status::Ok);
        QCOMPARE(registry.agents().size(), 1);
        QCOMPARE(registry.skippedCount(), 6);
    }

    void aRepeatedIdIsSkipped()
    {
        const auto registry = AgentRegistry::parse(withAgents(QJsonArray{agent(QStringLiteral("AG-1"), QStringLiteral("First")), agent(QStringLiteral("AG-1"), QStringLiteral("Second"))}));
        QCOMPARE(registry.agents().size(), 1);
        QCOMPARE(registry.agents().first().name, QStringLiteral("First"));
        QCOMPARE(registry.skippedCount(), 1);
    }

    void aFileOverOneMegabyteIsInvalid()
    {
        QByteArray big = withAgents(QJsonArray{agent(QStringLiteral("AG-1"))});
        big.append(QByteArray(AgentRegistry::maxBytes, ' '));
        QCOMPARE(AgentRegistry::parse(big).status(), AgentRegistry::Status::Invalid);
    }

    void referencesCarryTheStatus()
    {
        const auto registry = AgentRegistry::parse(fixture(QStringLiteral("agent-registry.json")));
        const auto active = AgentRegistry::refFor(*registry.byId(QStringLiteral("AG-0001")));
        QVERIFY(!active.isSelf && !active.unknown && !active.retired && !active.paused);
        QCOMPARE(active.displayText(), QStringLiteral("Writer-Bot"));
        const auto retired = AgentRegistry::refFor(*registry.byId(QStringLiteral("AG-0004")));
        QVERIFY(retired.retired);
        QCOMPARE(retired.displayText(), QStringLiteral("Invoice-Bot (retired)"));
        QVERIFY(AgentRegistry::refFor(*registry.byId(QStringLiteral("AG-0003"))).paused);
        const auto unknown = AgentRegistry::unknownRef(QStringLiteral("AG-0001"));
        QCOMPARE(unknown.displayText(), QStringLiteral("AG-0001 (unknown agent)"));
        QCOMPARE(AgentRegistry::selfRef().displayText(), QStringLiteral("Self"));
    }

    void thePlanningPersonNameIsExact()
    {
        QCOMPARE(AgentRegistry::planningPersonName(QStringLiteral("Writer-Bot"), QStringLiteral("Ali")), robot() + QStringLiteral(" Writer-Bot (Ali)"));
    }

    void theDriveSourceAsksForTheUsersOwnFile()
    {
        FakeTransport transport;
        transport.routeFile("GET", QStringLiteral("/remote.php/dav/files/ali/NoAgenda/agent-registry.json"), 200, QStringLiteral("agent-registry.json"));
        DriveRegistrySource source(&transport);
        RegistryFetch fetched;
        source.fetch(QByteArray(), [&fetched](const RegistryFetch &f) { fetched = f; });
        QCOMPARE(fetched.outcome, RegistryFetch::Outcome::Fetched);
        QCOMPARE(AgentRegistry::parse(fetched.bytes).agents().size(), 6);
        QCOMPARE(transport.requests.size(), 1);
        QCOMPARE(transport.requests.first().url.host(), QStringLiteral("drive.example.test"));
    }

    void theDriveSourceAnswers304And404()
    {
        FakeTransport transport;
        transport.route("GET", QStringLiteral("/remote.php/dav/files/ali/NoAgenda/agent-registry.json"), 304, QByteArray());
        DriveRegistrySource source(&transport);
        RegistryFetch fetched;
        source.fetch(QByteArrayLiteral("\"abc\""), [&fetched](const RegistryFetch &f) { fetched = f; });
        QCOMPARE(fetched.outcome, RegistryFetch::Outcome::NotModified);
        // The cached ETag was sent.
        bool sent = false;
        for (const auto &h : transport.requests.first().headers) {
            sent = sent || (h.first == "If-None-Match" && h.second == "\"abc\"");
        }
        QVERIFY(sent);

        FakeTransport empty; // everything is 404
        DriveRegistrySource missing(&empty);
        missing.fetch(QByteArray(), [&fetched](const RegistryFetch &f) { fetched = f; });
        QCOMPARE(fetched.outcome, RegistryFetch::Outcome::Missing);

        FakeTransport broken;
        broken.offline = true;
        DriveRegistrySource failing(&broken);
        failing.fetch(QByteArray(), [&fetched](const RegistryFetch &f) { fetched = f; });
        QCOMPARE(fetched.outcome, RegistryFetch::Outcome::Failed);
    }

    void theFactoryGivesTheDriveSourceNow()
    {
        FakeTransport transport;
        const auto source = makeRegistrySource(&transport);
        QVERIFY(source);
        QVERIFY(source->description().contains(QStringLiteral("NoAgenda/agent-registry.json")));
    }
};

QTEST_GUILESS_MAIN(TestNoDriveAgendaRegistry)
#include "testnodriveagendaregistry.moc"
