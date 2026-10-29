/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: owns one AccountAgenda per account and the 5 minute refresh while NoDrive runs.
// Users who never open NoAgenda cause no server load: only accounts where it was opened once refresh.

#pragma once

#include "accountagenda.h"

#include <QObject>
#include <QPointer>
#include <QTimer>

#include <map>
#include <memory>

namespace OCC {
class AccountState;
}

namespace OCC::Nourax::Agenda {

class AgendaService : public QObject
{
    Q_OBJECT

public:
    static AgendaService *instance();

    explicit AgendaService(QObject *parent = nullptr);

    /// Called once at startup (from our problem report manager). Connects to the accounts; no request.
    /// An account where NoAgenda was opened before gets its agenda back from the cache at once, so the
    /// 5 minute refresh runs for it without the window being opened.
    void start();

    /// Starts to watch an account (at start and when it is added). Reads nothing from the server.
    void watch(AccountState *state);

    /// Whether this account has its NoAgenda data loaded in memory.
    [[nodiscard]] bool hasAgenda(const QString &accountId) const { return _agendas.count(accountId) > 0; }

    /// The NoAgenda data of an account (created on first use). Never null for a valid state.
    AccountAgenda *agendaFor(AccountState *state);

    static constexpr int refreshIntervalMs = 5 * 60 * 1000;

Q_SIGNALS:
    /// The account is gone from memory (removed by the user, or NoDrive is quitting): windows must close.
    void agendaRemoved(const QString &accountId);

private:
    /// The user removed the account (AccountManager::accountSyncConnectionRemoved, which only
    /// removeAccountState() emits): windows close and NoAgenda's own copy of its data is deleted.
    void accountDeleted(AccountState *state);
    /// AccountManager::accountRemoved. NoDrive also emits it for every account when it quits
    /// (AccountManager::shutdown()), so this must not delete anything: the settings and the offline
    /// cache have to be there at the next start.
    void accountClosed(AccountState *state);
    void forget(const QString &accountId, bool deleteData);
    void onTimer();

    bool _started = false;
    QTimer _timer;
    std::map<QString, std::unique_ptr<AccountAgenda>> _agendas;
    std::map<QString, QPointer<AccountState>> _states;
};

} // namespace OCC::Nourax::Agenda
