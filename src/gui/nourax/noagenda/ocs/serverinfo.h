/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: who the signed-in person is (ocs cloud/user) and what the server offers (capabilities).

#pragma once

#include "../net/httptransport.h"

#include <QDateTime>
#include <QJsonObject>
#include <QStringList>

#include <functional>

namespace OCC::Nourax::Agenda {

struct ServerInfo
{
    bool loaded = false;
    QString uid;
    QString displayName;
    QString email;
    QStringList groups;
    bool isServerAdmin = false;
    QString deckVersion; ///< empty when the server has no "deck" capability
    QDateTime fetchedAt;

    [[nodiscard]] QJsonObject toJson() const;
    static ServerInfo fromJson(const QJsonObject &o);
    /// Reads the answer of ocs/v2.php/cloud/user (the OCS "data" object).
    static void applyUser(const QJsonObject &data, ServerInfo *info);
    /// Reads the answer of ocs/v1.php/cloud/capabilities (the OCS "data" object).
    static void applyCapabilities(const QJsonObject &data, ServerInfo *info);
};

struct ServerInfoResult
{
    bool ok = false;
    int status = 0;
    bool networkError = false;
    ServerInfo info;
};

class ServerInfoClient
{
public:
    static void fetch(HttpTransport *transport, const ServerInfo &previous, const std::function<void(const ServerInfoResult &)> &done);
};

} // namespace OCC::Nourax::Agenda
