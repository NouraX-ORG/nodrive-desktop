/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "accounttransport.h"

#include "account.h"
#include "networkjobs.h"

#include <QBuffer>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace OCC::Nourax::Agenda {

AccountTransport::AccountTransport(AccountPtr account, QObject *parent)
    : QObject(parent)
    , _account(std::move(account))
{
}

QUrl AccountTransport::serverUrl() const
{
    return _account->url();
}

QString AccountTransport::davUser() const
{
    return _account->davUser();
}

void AccountTransport::send(const HttpRequest &request, const HttpCallback &done)
{
    // The account's own server only: refuse anything that points elsewhere.
    // Same scheme, host and port: never a downgrade to plain http and never another server.
    if (request.url.scheme() != _account->url().scheme() || request.url.host() != _account->url().host() || request.url.port() != _account->url().port()) {
        HttpResult refused;
        refused.networkError = true;
        QMetaObject::invokeMethod(this, [done, refused] { done(refused); }, Qt::QueuedConnection);
        return;
    }

    QNetworkRequest req;
    for (const auto &header : request.headers) {
        req.setRawHeader(header.first, header.second);
    }
    QBuffer *body = nullptr;
    if (!request.body.isEmpty()) {
        body = new QBuffer;
        body->setData(request.body);
    }
    const auto job = _account->sendRequest(request.verb, request.url, req, body);
    QObject::connect(job, &SimpleNetworkJob::finishedSignal, this, [done](QNetworkReply *reply) {
        HttpResult result;
        result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        result.networkError = result.status == 0;
        result.body = reply->readAll();
        const auto headers = reply->rawHeaderPairs();
        for (const auto &pair : headers) {
            result.headers.insert(pair.first.toLower(), pair.second);
        }
        done(result);
    });
}

} // namespace OCC::Nourax::Agenda
