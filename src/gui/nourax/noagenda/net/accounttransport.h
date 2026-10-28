/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: the real transport. Every request goes through Account::sendRequest(), which uses the
// account's own connection, credentials, cookies and proxy. No other host is ever contacted.

#pragma once

#include "httptransport.h"

#include "accountfwd.h"

#include <QObject>
#include <QPointer>

namespace OCC::Nourax::Agenda {

class AccountTransport : public QObject, public HttpTransport
{
    Q_OBJECT

public:
    explicit AccountTransport(AccountPtr account, QObject *parent = nullptr);

    void send(const HttpRequest &request, const HttpCallback &done) override;
    [[nodiscard]] QUrl serverUrl() const override;
    [[nodiscard]] QString davUser() const override;

private:
    AccountPtr _account;
};

} // namespace OCC::Nourax::Agenda
