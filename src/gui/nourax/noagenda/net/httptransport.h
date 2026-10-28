/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: how the readers talk to the server. The real transport (AccountTransport) goes through
// the account's own connection and credentials, only to the account's own server. Tests use a fake
// transport with canned answers.

#pragma once

#include "../model/agendaitem.h"

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QPair>
#include <QString>
#include <QUrl>

#include <functional>

namespace OCC::Nourax::Agenda {

struct HttpRequest
{
    QByteArray verb;
    QUrl url;
    QList<QPair<QByteArray, QByteArray>> headers;
    QByteArray body;
};

struct HttpResult
{
    int status = 0; ///< 0 = no HTTP answer (network error)
    bool networkError = false;
    QByteArray body;
    QMap<QByteArray, QByteArray> headers; ///< names in lower case
    [[nodiscard]] bool ok() const { return status >= 200 && status < 300; }
    [[nodiscard]] bool multistatus() const { return status == 207; }
};

using HttpCallback = std::function<void(const HttpResult &)>;

class HttpTransport
{
public:
    virtual ~HttpTransport() = default;
    virtual void send(const HttpRequest &request, const HttpCallback &done) = 0;
    /// The account's server address (the only host that is ever contacted).
    [[nodiscard]] virtual QUrl serverUrl() const = 0;
    /// The user name of the WebDAV paths (remote.php/dav/files/<user>/).
    [[nodiscard]] virtual QString davUser() const = 0;
    /// Address for a path relative to the server (no leading slash needed).
    [[nodiscard]] QUrl url(const QString &relativePath) const;
    /// Address for a server absolute path such as an href from a multistatus answer.
    [[nodiscard]] QUrl urlForHref(const QString &href) const;
};

/// Convenience builders.
HttpRequest makeRequest(const QByteArray &verb, const QUrl &url);
HttpRequest withHeader(HttpRequest request, const QByteArray &name, const QByteArray &value);
HttpRequest withBody(HttpRequest request, const QByteArray &body, const QByteArray &contentType);

/// OCS-style GET: JSON answer, with the CSRF header the server wants.
HttpRequest ocsGet(const HttpTransport &transport, const QString &relativePathAndQuery);

} // namespace OCC::Nourax::Agenda
