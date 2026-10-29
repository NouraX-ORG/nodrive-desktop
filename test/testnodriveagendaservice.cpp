/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: what happens to the settings and the offline cache when NoDrive quits (AccountManager::shutdown()
// emits accountRemoved for every account) and when the user really removes an account (removeAccountState()
// emits accountSyncConnectionRemoved first). And: an account where NoAgenda was opened gets its data back at start.

#include "nodrive/noagenda/agendatestsupport.h"

#include "gui/nourax/noagenda/agendaservice.h"

#include "account.h"
#include "accountmanager.h"
#include "configfile.h"
#include "testhelper.h"

#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>

using namespace AgendaTest;

class TestNoDriveAgendaService : public QObject
{
    Q_OBJECT

    QTemporaryDir _config;

    static OCC::AccountPtr makeAccount(const QString &id)
    {
        auto account = OCC::Account::create();
        account->setUrl(QUrl(QStringLiteral("https://drive.example.test")));
        account->setDavUser(QStringLiteral("ali"));
        account->setProperty("id", id);
        return account;
    }

    static QString settingsFile(const QString &id)
    {
        return AgendaCache::directoryForAccount(id) + QLatin1Char('/') + AgendaCache::fileName("settings");
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(_config.isValid());
        QStandardPaths::setTestModeEnabled(true);
        OCC::ConfigFile::setConfDir(_config.path());
    }

    void quittingKeepsTheSettingsAndTheCache()
    {
        const QString id = QStringLiteral("acc-quit");
        FakeAccountState state(makeAccount(id));
        {
            AgendaService service;
            service.start();
            service.watch(&state);
            auto *agenda = service.agendaFor(&state);
            QVERIFY(agenda);
            agenda->markOpened();
            WorkingHours hours;
            hours.from = QTime(8, 30);
            agenda->setWorkingHours(hours);
            agenda->setCalendarMode(QStringLiteral("work"), CalendarMode::Hidden);
            QVERIFY(QFile::exists(settingsFile(id)));

            QSignalSpy removed(&service, &AgendaService::agendaRemoved);
            // NoDrive quits: AccountManager::shutdown() emits accountRemoved for every account, nothing else.
            Q_EMIT OCC::AccountManager::instance()->accountRemoved(&state);
            QCOMPARE(removed.count(), 1); // the window closes
            QVERIFY(!service.hasAgenda(id));
            QVERIFY2(QFile::exists(settingsFile(id)), "the settings must survive a normal quit");
        }

        // The next start: the choices are there, and (because NoAgenda was opened) the agenda is loaded at once.
        {
            AgendaService service;
            service.start();
            service.watch(&state);
            QVERIFY(service.hasAgenda(id));
            auto *agenda = service.agendaFor(&state);
            QVERIFY(agenda->settings().everOpened);
            QVERIFY(agenda->settings().workingHoursOverridden());
            QCOMPARE(agenda->settings().workingHours().from, QTime(8, 30));
            QVERIFY(agenda->settings().hasCalendarOverride(QStringLiteral("work")));
        }
        QDir(AgendaCache::directoryForAccount(id)).removeRecursively();
    }

    void removingTheAccountDeletesTheDataOfThatAccountOnly()
    {
        const QString id = QStringLiteral("acc-removed");
        const QString other = QStringLiteral("acc-other");
        FakeAccountState state(makeAccount(id));
        FakeAccountState otherState(makeAccount(other));
        AgendaService service;
        service.start();
        service.watch(&state);
        service.watch(&otherState);
        service.agendaFor(&state)->markOpened();
        service.agendaFor(&otherState)->markOpened();
        QVERIFY(QFile::exists(settingsFile(id)));
        QVERIFY(QFile::exists(settingsFile(other)));

        // removeAccountState(): accountSyncConnectionRemoved first, then accountRemoved.
        Q_EMIT OCC::AccountManager::instance()->accountSyncConnectionRemoved(&state);
        Q_EMIT OCC::AccountManager::instance()->accountRemoved(&state);
        QVERIFY(!service.hasAgenda(id));
        QVERIFY2(!QDir(AgendaCache::directoryForAccount(id)).exists(), "the cache of a removed account is deleted");
        QVERIFY(service.hasAgenda(other));
        QVERIFY(QFile::exists(settingsFile(other)));
        QDir(AgendaCache::directoryForAccount(other)).removeRecursively();
    }

    void anAccountWhereNoAgendaWasNeverOpenedCostsNothing()
    {
        const QString id = QStringLiteral("acc-never");
        FakeAccountState state(makeAccount(id));
        AgendaService service;
        service.start();
        service.watch(&state);
        QVERIFY(!service.hasAgenda(id));
        QVERIFY(!QDir(AgendaCache::directoryForAccount(id)).exists());
    }
};

QTEST_GUILESS_MAIN(TestNoDriveAgendaService)
#include "testnodriveagendaservice.moc"
