/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: the offline cache - round trip, schema, partial refresh, no secrets, removal of its own folder only.

#include "nodrive/noagenda/agendatestsupport.h"

#include "configfile.h"

#include <QDirIterator>
#include <QStandardPaths>
#include <QTemporaryDir>

using namespace AgendaTest;

class TestNoDriveAgendaCache : public QObject
{
    Q_OBJECT

    static QByteArray read(const QString &path)
    {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    void everyPartRoundTrips()
    {
        QTemporaryDir root;
        const AgendaCache cache(root.path() + QStringLiteral("/noagenda/acc"));
        auto input = loadScenario(QStringLiteral("ali"));
        input.settings.setProjectMode(2, WorkMode::Do);
        input.settings.lastView = QStringLiteral("month");
        RegistryCache reg;
        reg.present = true;
        reg.bytes = fixture(QStringLiteral("agent-registry.json"));
        reg.etag = "\"etag-1\"";
        reg.fetchedAt = fixedNow();

        QVERIFY(cache.saveSettings(input.settings));
        QVERIFY(cache.saveRegistry(reg));
        QVERIFY(cache.saveCalendar(input.calendar));
        QVERIFY(cache.saveGantt(input.gantt));
        QVERIFY(cache.saveDeck(input.deck));
        QVERIFY(cache.saveServerInfo(input.server));

        const auto settings = cache.loadSettings();
        QCOMPARE(settings.lastView, QStringLiteral("month"));
        ProjectRelation relation;
        relation.id = 2;
        QCOMPARE(settings.projectMode(relation), WorkMode::Do);
        const auto registry = cache.loadRegistry();
        QVERIFY(registry.present);
        QCOMPARE(registry.bytes, reg.bytes);
        QCOMPARE(registry.etag, reg.etag);
        QCOMPARE(registry.fetchedAt, reg.fetchedAt);
        QCOMPARE(cache.loadCalendar().records.size(), input.calendar.records.size());
        QCOMPARE(cache.loadGantt().projectData.value(1).tasks.size(), 13);
        QCOMPARE(cache.loadDeck().boards.size(), 1);
        QCOMPARE(cache.loadServerInfo().uid, QStringLiteral("ali"));

        // Rows built from the cache are the rows built from the live data.
        BuildInput again = input;
        again.calendar = cache.loadCalendar();
        again.gantt = cache.loadGantt();
        again.deck = cache.loadDeck();
        QCOMPARE(AgendaBuilder::build(again).items.size(), AgendaBuilder::build(input).items.size());
    }

    void nothingIsReadFromAnEmptyFolder()
    {
        QTemporaryDir root;
        const AgendaCache cache(root.path() + QStringLiteral("/noagenda/none"));
        QVERIFY(!cache.loadCalendar().loaded);
        QVERIFY(!cache.loadGantt().loaded);
        QVERIFY(!cache.loadDeck().loaded);
        QVERIFY(!cache.loadRegistry().present);
        QVERIFY(!cache.loadServerInfo().loaded);
        QVERIFY(!cache.loadSettings().everOpened);
    }

    void anOlderSchemaOrBrokenFileIsIgnored()
    {
        QTemporaryDir root;
        const QString dir = root.path() + QStringLiteral("/noagenda/acc");
        const AgendaCache cache(dir);
        auto input = loadScenario(QStringLiteral("ali"));
        QVERIFY(cache.saveCalendar(input.calendar));
        QVERIFY(cache.loadCalendar().loaded);

        // an older schema
        {
            QFile f(dir + QStringLiteral("/calendars.json"));
            QVERIFY(f.open(QIODevice::ReadOnly));
            auto text = QString::fromUtf8(f.readAll());
            f.close();
            text.replace(QStringLiteral("\"cache_schema\":1"), QStringLiteral("\"cache_schema\":0"));
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(text.toUtf8());
        }
        QVERIFY(!cache.loadCalendar().loaded);

        // a file that does not parse
        {
            QFile f(dir + QStringLiteral("/calendars.json"));
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write("{ not json");
        }
        QVERIFY(!cache.loadCalendar().loaded);
    }

    void aFailedRefreshDoesNotTouchTheFileOfThatSource()
    {
        QTemporaryDir root;
        const QString dir = root.path() + QStringLiteral("/noagenda/acc");
        auto fake = std::make_unique<FakeTransport>();
        auto *transport = fake.get();
        routeScenario(transport, QStringLiteral("ali"));
        AccountAgenda agenda(std::move(fake), dir);
        agenda.setClock([] { return fixedNow(); });
        agenda.setZone(QTimeZone(QTimeZone::UTC));
        agenda.refresh(true);
        const auto ganttBefore = read(dir + QStringLiteral("/gantt.json"));
        const auto calendarBefore = read(dir + QStringLiteral("/calendars.json"));
        QVERIFY(!ganttBefore.isEmpty());

        // NoGantt fails while everything else works.
        for (auto &a : transport->answers) {
            if (a.path.contains(QStringLiteral("/nc_gantt/"))) {
                a.status = 503;
            }
        }
        agenda.refresh(true);
        QCOMPARE(read(dir + QStringLiteral("/gantt.json")), ganttBefore);
        QCOMPARE(agenda.state().status(QStringLiteral("gantt"))->state, SourceStatus::State::Cached);
        Q_UNUSED(calendarBefore)
    }

    void noSecretIsEverWritten()
    {
        QTemporaryDir root;
        const QString dir = root.path() + QStringLiteral("/noagenda/acc");
        auto fake = std::make_unique<FakeTransport>();
        routeScenario(fake.get(), QStringLiteral("ali"));
        // The transport of the real client sends the credentials of the account; the fake one shows what
        // the cache would hold if it ever took them: a planted fake password in the server's answers.
        const QByteArray planted = "Planted-Fake-Password-7Hq2";
        for (auto &a : fake->answers) {
            if (a.path.endsWith(QLatin1String("/user"))) {
                // the planted value is in a header of the answer, not in the data
                a.headers.insert("set-cookie", "oc_sessionPassphrase=" + planted);
            }
        }
        AccountAgenda agenda(std::move(fake), dir);
        agenda.setClock([] { return fixedNow(); });
        agenda.setZone(QTimeZone(QTimeZone::UTC));
        agenda.refresh(true);
        QDirIterator it(dir, QDir::Files);
        int files = 0;
        while (it.hasNext()) {
            const auto bytes = read(it.next());
            ++files;
            QVERIFY(!bytes.contains(planted));
            QVERIFY(!bytes.contains("Authorization"));
            QVERIFY(!bytes.contains("app_password"));
            QVERIFY(!bytes.contains("Bearer "));
            QVERIFY(!bytes.contains("Basic "));
            QVERIFY(!bytes.contains("oc_sessionPassphrase"));
        }
        QCOMPARE(files, 5); // registry, calendars, gantt, deck and the user (settings are written on the first change)
    }

    void removingTheCacheOnlyRemovesNoAgendasOwnFolder()
    {
        QTemporaryDir root;
        const QString own = root.path() + QStringLiteral("/noagenda/acc");
        const AgendaCache cache(own);
        QVERIFY(cache.saveSettings(SourceSettings()));
        QVERIFY(QFile::exists(own + QStringLiteral("/settings.json")));
        QVERIFY(cache.removeAll());
        QVERIFY(!QDir(own).exists());

        // Any other folder is never touched, even when it is given by mistake.
        const QString other = root.path() + QStringLiteral("/documents");
        QVERIFY(QDir().mkpath(other));
        QFile precious(other + QStringLiteral("/precious.txt"));
        QVERIFY(precious.open(QIODevice::WriteOnly));
        precious.write("keep me");
        precious.close();
        const AgendaCache mistaken(other);
        QVERIFY(!mistaken.removeAll());
        QVERIFY(QFile::exists(other + QStringLiteral("/precious.txt")));
        QVERIFY(!AgendaCache(QString()).removeAll());
    }

    void theFolderIsPerAccountAndSafe()
    {
        QTemporaryDir config;
        QVERIFY(config.isValid());
        OCC::ConfigFile::setConfDir(config.path());
        const auto a = AgendaCache::directoryForAccount(QStringLiteral("acc/../evil"));
        QVERIFY(a.contains(QStringLiteral("/noagenda/")));
        QVERIFY(!a.contains(QStringLiteral("..")));
        QVERIFY(AgendaCache::directoryForAccount(QStringLiteral("1")) != AgendaCache::directoryForAccount(QStringLiteral("2")));
    }
};

QTEST_GUILESS_MAIN(TestNoDriveAgendaCache)
#include "testnodriveagendacache.moc"
