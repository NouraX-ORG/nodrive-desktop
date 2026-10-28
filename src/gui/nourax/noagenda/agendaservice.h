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
    void start();

    /// The NoAgenda data of an account (created on first use). Never null for a valid state.
    AccountAgenda *agendaFor(AccountState *state);

    static constexpr int refreshIntervalMs = 5 * 60 * 1000;

private:
    void watch(AccountState *state);
    void remove(AccountState *state);
    void onTimer();

    bool _started = false;
    QTimer _timer;
    std::map<QString, std::unique_ptr<AccountAgenda>> _agendas;
    std::map<QString, QPointer<AccountState>> _states;
};

} // namespace OCC::Nourax::Agenda
