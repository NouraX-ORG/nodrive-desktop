/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "driveregistrysource.h"

#include "agentregistry.h"

namespace OCC::Nourax::Agenda {

namespace {

void interpret(const HttpResult &r, const std::function<void(const RegistryFetch &)> &done)
{
    RegistryFetch f;
    if (r.status == 304) {
        f.outcome = RegistryFetch::Outcome::NotModified;
    } else if (r.status == 404) {
        f.outcome = RegistryFetch::Outcome::Missing;
    } else if (r.ok()) {
        f.bytes = r.body;
        f.etag = r.headers.value(QByteArrayLiteral("etag"));
        f.outcome = RegistryFetch::Outcome::Fetched;
    } else {
        f.outcome = RegistryFetch::Outcome::Failed;
    }
    done(f);
}

HttpRequest conditionalGet(const QUrl &url, const QByteArray &etag)
{
    auto request = makeRequest("GET", url);
    if (!etag.isEmpty()) {
        request = withHeader(request, "If-None-Match", etag);
    }
    return request;
}

} // namespace

DriveRegistrySource::DriveRegistrySource(HttpTransport *transport)
    : _transport(transport)
{
}

QString DriveRegistrySource::remotePath()
{
    return QStringLiteral("NoAgenda/agent-registry.json");
}

void DriveRegistrySource::fetch(const QByteArray &etag, const std::function<void(const RegistryFetch &)> &done)
{
    const auto url = _transport->url(QStringLiteral("remote.php/dav/files/") + QString::fromUtf8(QUrl::toPercentEncoding(_transport->davUser())) + QLatin1Char('/') + remotePath());
    _transport->send(conditionalGet(url, etag), [done](const HttpResult &r) { interpret(r, done); });
}

QString DriveRegistrySource::description() const
{
    return QStringLiteral("your NoDrive file /") + remotePath();
}

UrlRegistrySource::UrlRegistrySource(HttpTransport *transport, const QString &url)
    : _transport(transport)
    , _url(url)
{
}

void UrlRegistrySource::fetch(const QByteArray &etag, const std::function<void(const RegistryFetch &)> &done)
{
    _transport->send(conditionalGet(_transport->url(_url), etag), [done](const HttpResult &r) { interpret(r, done); });
}

QString UrlRegistrySource::description() const
{
    return QStringLiteral("the agents app");
}

} // namespace OCC::Nourax::Agenda
