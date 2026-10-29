/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "agendaservice.h"

#include "net/accounttransport.h"

#include "account.h"
#include "accountmanager.h"
#include "accountstate.h"

#include <QCoreApplication>
#include <QLoggingCategory>

namespace OCC::Nourax::Agenda {

Q_LOGGING_CATEGORY(lcAgendaService, "nodrive.agenda.service", QtInfoMsg)

AgendaService *AgendaService::instance()
{
    static AgendaService *service = new AgendaService(qApp);
    return service;
}

AgendaService::AgendaService(QObject *parent)
    : QObject(parent)
{
    _timer.setInterval(refreshIntervalMs);
    connect(&_timer, &QTimer::timeout, this, &AgendaService::onTimer);
}

void AgendaService::start()
{
    if (_started) {
        return;
    }
    _started = true;
    const auto accounts = AccountManager::instance()->accounts();
    for (const auto &state : accounts) {
        watch(state.data());
    }
    connect(AccountManager::instance(), &AccountManager::accountAdded, this, &AgendaService::watch);
    // Order of removeAccountState(): accountSyncConnectionRemoved, then accountRemoved.
    connect(AccountManager::instance(), &AccountManager::accountSyncConnectionRemoved, this, &AgendaService::accountDeleted);
    connect(AccountManager::instance(), &AccountManager::accountRemoved, this, &AgendaService::accountClosed);
    _timer.start();
}

void AgendaService::watch(AccountState *state)
{
    if (!state || !state->account()) {
        return;
    }
    const auto id = state->account()->id();
    if (_states.count(id) && _states[id] == state) {
        return; // already watched
    }
    _states[id] = state;
    connect(state, &AccountState::stateChanged, this, [this, id] {
        const auto it = _states.find(id);
        if (it == _states.end() || !it->second) {
            return;
        }
        const auto agendaIt = _agendas.find(id);
        if (agendaIt == _agendas.end()) {
            return;
        }
        auto *agenda = agendaIt->second.get();
        const auto state = it->second->state();
        agenda->setOnline(state == AccountState::Connected);
        agenda->setNeedsLogin(state == AccountState::SignedOut);
        // Back online: read again, but only for people who use NoAgenda.
        if (state == AccountState::Connected && agenda->settings().everOpened) {
            agenda->refresh(false);
        }
    });

    // NoAgenda was opened for this account before: load its saved data now (no request), so that the
    // 5 minute refresh keeps running after a restart even if the window is not opened.
    if (!_agendas.count(id) && AgendaCache(AgendaCache::directoryForAccount(id)).loadSettings().everOpened) {
        agendaFor(state);
    }
}

void AgendaService::accountDeleted(AccountState *state)
{
    if (!state || !state->account()) {
        return;
    }
    forget(state->account()->id(), true);
}

void AgendaService::accountClosed(AccountState *state)
{
    if (!state || !state->account()) {
        return;
    }
    forget(state->account()->id(), false);
}

void AgendaService::forget(const QString &id, bool deleteData)
{
    Q_EMIT agendaRemoved(id); // windows close first
    const auto it = _agendas.find(id);
    if (deleteData) {
        // NoAgenda's own copy of the server data goes away with a removed account (it holds personal events).
        if (it != _agendas.end()) {
            it->second->removeCache();
        } else {
            AgendaCache(AgendaCache::directoryForAccount(id)).removeAll();
        }
    }
    if (it != _agendas.end()) {
        _agendas.erase(it);
    }
    _states.erase(id);
}

AccountAgenda *AgendaService::agendaFor(AccountState *state)
{
    if (!state || !state->account()) {
        return nullptr;
    }
    const auto account = state->account();
    const auto id = account->id();
    const auto it = _agendas.find(id);
    if (it != _agendas.end()) {
        return it->second.get();
    }
    auto agenda = std::make_unique<AccountAgenda>(std::make_unique<AccountTransport>(account), AgendaCache::directoryForAccount(id), this);
    agenda->loadFromCache();
    agenda->setOnline(state->isConnected());
    agenda->setNeedsLogin(state->state() == AccountState::SignedOut);
    auto *raw = agenda.get();
    _agendas[id] = std::move(agenda);
    if (!_states.count(id)) {
        watch(state);
    }
    return raw;
}

void AgendaService::onTimer()
{
    for (auto &entry : _agendas) {
        auto *agenda = entry.second.get();
        if (agenda->settings().everOpened && agenda->isOnline()) {
            agenda->refresh(false);
        }
    }
}

} // namespace OCC::Nourax::Agenda
