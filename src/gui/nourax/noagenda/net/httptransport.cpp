/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "httptransport.h"

namespace OCC::Nourax::Agenda {

QUrl HttpTransport::url(const QString &relativePath) const
{
    auto base = serverUrl();
    auto path = base.path();
    if (!path.endsWith(QLatin1Char('/'))) {
        path += QLatin1Char('/');
    }
    QString rel = relativePath;
    QString query;
    const auto q = rel.indexOf(QLatin1Char('?'));
    if (q >= 0) {
        query = rel.mid(q + 1);
        rel = rel.left(q);
    }
    while (rel.startsWith(QLatin1Char('/'))) {
        rel.remove(0, 1);
    }
    base.setPath(path + rel, QUrl::TolerantMode);
    if (!query.isEmpty()) {
        base.setQuery(query, QUrl::TolerantMode);
    }
    return base;
}

QUrl HttpTransport::urlForHref(const QString &href) const
{
    auto base = serverUrl();
    // A full URL is only accepted when it points at the same host.
    if (href.startsWith(QLatin1String("http://")) || href.startsWith(QLatin1String("https://"))) {
        const QUrl full(href, QUrl::TolerantMode);
        base.setPath(full.path(QUrl::FullyEncoded), QUrl::TolerantMode);
        return base;
    }
    base.setPath(href, QUrl::TolerantMode);
    return base;
}

HttpRequest makeRequest(const QByteArray &verb, const QUrl &url)
{
    HttpRequest r;
    r.verb = verb;
    r.url = url;
    return r;
}

HttpRequest withHeader(HttpRequest request, const QByteArray &name, const QByteArray &value)
{
    request.headers.append({name, value});
    return request;
}

HttpRequest withBody(HttpRequest request, const QByteArray &body, const QByteArray &contentType)
{
    request.body = body;
    request.headers.append({QByteArrayLiteral("Content-Type"), contentType});
    return request;
}

HttpRequest ocsGet(const HttpTransport &transport, const QString &relativePathAndQuery)
{
    auto r = makeRequest("GET", transport.url(relativePathAndQuery));
    r.headers.append({QByteArrayLiteral("OCS-APIRequest"), QByteArrayLiteral("true")});
    r.headers.append({QByteArrayLiteral("Accept"), QByteArrayLiteral("application/json")});
    return r;
}

} // namespace OCC::Nourax::Agenda
