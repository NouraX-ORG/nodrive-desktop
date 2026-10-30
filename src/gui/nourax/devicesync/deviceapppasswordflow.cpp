/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "deviceapppasswordflow.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

namespace OCC::Nourax::DeviceSync {

Q_LOGGING_CATEGORY(lcDeviceSyncFlow, "nextcloud.gui.nourax.devicesync.flow", QtInfoMsg)

namespace {

int portOf(const QUrl &url)
{
    return url.port(url.scheme() == QLatin1String("http") ? 80 : 443);
}

bool sameOrigin(const QUrl &a, const QUrl &b)
{
    return !a.host().isEmpty() && a.scheme().toLower() == b.scheme().toLower() && a.host().toLower() == b.host().toLower()
        && portOf(a) == portOf(b);
}

QString trimmedPath(const QUrl &url)
{
    auto path = url.path();
    while (path.endsWith(QLatin1Char('/'))) {
        path.chop(1);
    }
    return path;
}

// Writes zeros into a buffer that may be shared with other copies (the receivers of the signal, the reply),
// so that all of them are cleared at once.
void zero(const QByteArray &data)
{
    auto *p = const_cast<volatile char *>(data.constData());
    for (qsizetype i = 0; p && i < data.size(); ++i) {
        p[i] = 0;
    }
}

void zero(const QString &text)
{
    auto *p = reinterpret_cast<volatile char16_t *>(const_cast<QChar *>(text.constData()));
    for (qsizetype i = 0; p && i < text.size(); ++i) {
        p[i] = 0;
    }
}

QNetworkRequest baseRequest(const QUrl &url, const QString &computerName)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, DeviceAppPasswordFlow::userAgent(computerName));
    request.setRawHeader("OCS-APIRequest", "true");
    // No cookies in or out: a revoked password must really be refused, and the browser's session is not ours.
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    return request;
}

} // namespace

DeviceAppPasswordFlow::DeviceAppPasswordFlow(const QUrl &server, const QString &computerName, QNetworkAccessManager *nam, QObject *parent)
    : QObject(parent)
    , _server(server)
    , _computerName(computerName)
    , _nam(nam)
{
    if (!_nam) {
        _nam = new QNetworkAccessManager(this);
        _ownsNam = true;
    }
    _pollTimer.setSingleShot(true);
    _timeoutTimer.setSingleShot(true);
    connect(&_pollTimer, &QTimer::timeout, this, &DeviceAppPasswordFlow::poll);
    connect(&_timeoutTimer, &QTimer::timeout, this, [this] { fail(Failure::TimedOut); });
}

DeviceAppPasswordFlow::~DeviceAppPasswordFlow()
{
    cancel();
}

QByteArray DeviceAppPasswordFlow::userAgent(const QString &computerName)
{
    return QStringLiteral("NoDrive Calendar & Contacts (%1)").arg(computerName).toUtf8();
}

bool DeviceAppPasswordFlow::sameServer(const QUrl &a, const QUrl &b)
{
    return sameOrigin(a, b) && trimmedPath(a) == trimmedPath(b);
}

void DeviceAppPasswordFlow::wipe(QByteArray &data)
{
    zero(data);
    data.clear();
}

QUrl DeviceAppPasswordFlow::serverPath(const QString &path) const
{
    auto url = _server;
    url.setPath(trimmedPath(_server) + path);
    url.setQuery(QString());
    url.setFragment(QString());
    return url;
}

void DeviceAppPasswordFlow::start()
{
    if (_running) {
        return;
    }
    _running = true;
    _timeoutTimer.start(_timeout);

    auto request = baseRequest(serverPath(QStringLiteral("/index.php/login/v2")), _computerName);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/x-www-form-urlencoded"));
    qCInfo(lcDeviceSyncFlow) << "starting the sign-in for a separate password";
    const auto reply = _nam->post(request, QByteArray());
    _pending = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] { handleInit(reply); });
}

void DeviceAppPasswordFlow::cancel()
{
    _running = false;
    _pollTimer.stop();
    _timeoutTimer.stop();
    if (_pending) {
        _pending->disconnect(this);
        _pending->abort();
        _pending->deleteLater();
    }
    _pending = nullptr;
}

void DeviceAppPasswordFlow::fail(Failure failure)
{
    if (!_running) {
        return;
    }
    cancel();
    QString text;
    switch (failure) {
    case Failure::TimedOut:
        text = tr("The sign-in took too long. Try again.");
        break;
    case Failure::ServerMismatch:
        text = tr("The answer came from a different server than your account's, so NoDrive did not use it.");
        break;
    case Failure::BadAnswer:
    case Failure::NotStarted:
        text = tr("The server didn't create a password. Ask your administrator.");
        break;
    }
    Q_EMIT failed(failure, text);
}

void DeviceAppPasswordFlow::handleInit(QNetworkReply *reply)
{
    reply->deleteLater();
    if (!_running || reply->error() == QNetworkReply::OperationCanceledError) {
        return;
    }
    const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    qCInfo(lcDeviceSyncFlow) << "the server answered the start of the sign-in: HTTP" << status;
    if (status != 200) {
        fail(Failure::NotStarted);
        return;
    }
    QByteArray body = reply->readAll();
    const auto json = QJsonDocument::fromJson(body).object();
    const auto poll = json.value(QStringLiteral("poll")).toObject();
    const auto token = poll.value(QStringLiteral("token")).toString();
    const auto endpoint = QUrl(poll.value(QStringLiteral("endpoint")).toString());
    const auto login = QUrl(json.value(QStringLiteral("login")).toString());
    if (token.isEmpty() || !endpoint.isValid() || !login.isValid() || login.isRelative() || endpoint.isRelative()) {
        fail(Failure::BadAnswer);
        return;
    }
    // Both addresses must lie on the account's own server: the person is sent there, and the token goes there.
    if (!sameOrigin(endpoint, _server) || !sameOrigin(login, _server)) {
        fail(Failure::ServerMismatch);
        return;
    }
    _pollToken = token.toUtf8();
    _pollEndpoint = endpoint;
    _loginUrl = login;
    Q_EMIT loginUrlReady(_loginUrl);
    _pollTimer.start(_pollInterval);
}

void DeviceAppPasswordFlow::poll()
{
    if (!_running) {
        return;
    }
    auto request = baseRequest(_pollEndpoint, _computerName);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/x-www-form-urlencoded"));
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("token"), QString::fromUtf8(_pollToken));
    const auto reply = _nam->post(request, form.toString(QUrl::FullyEncoded).toUtf8());
    _pending = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] { handlePoll(reply); });
}

void DeviceAppPasswordFlow::handlePoll(QNetworkReply *reply)
{
    reply->deleteLater();
    if (!_running || reply->error() == QNetworkReply::OperationCanceledError) {
        return;
    }
    const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status != 200) {
        // 404 means "not allowed yet". Anything else (a hiccup) is tried again until the time is up.
        qCInfo(lcDeviceSyncFlow) << "waiting for the person to allow access: HTTP" << status;
        _pollTimer.start(_pollInterval);
        return;
    }

    QByteArray body = reply->readAll();
    QJsonObject json = QJsonDocument::fromJson(body).object();
    zero(body);
    body.clear();
    const auto server = QUrl(json.value(QStringLiteral("server")).toString());
    QString loginName = json.value(QStringLiteral("loginName")).toString();
    QString passwordText = json.value(QStringLiteral("appPassword")).toString();
    json = QJsonObject();
    if (loginName.isEmpty() || passwordText.isEmpty()) {
        zero(passwordText);
        fail(Failure::BadAnswer);
        return;
    }
    if (!sameServer(server, _server)) {
        zero(passwordText);
        fail(Failure::ServerMismatch);
        return;
    }
    QByteArray password = passwordText.toUtf8();
    zero(passwordText);
    passwordText.clear();

    qCInfo(lcDeviceSyncFlow) << "access was allowed; a separate password was made (not shown, not kept)";
    cancel();
    wipe(_pollToken);
    Q_EMIT granted(loginName, password);
    wipe(password);
}

void DeviceAppPasswordFlow::revoke(const QString &loginName, QByteArray appPassword)
{
    auto request = baseRequest(serverPath(QStringLiteral("/ocs/v2.php/core/apppassword")), _computerName);
    QByteArray credentials = loginName.toUtf8() + ':' + appPassword;
    request.setRawHeader("Authorization", "Basic " + credentials.toBase64());
    wipe(credentials);
    qCInfo(lcDeviceSyncFlow) << "revoking the separate password (it was not installed)";
    const auto reply = _nam->sendCustomRequest(request, "DELETE");
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qCInfo(lcDeviceSyncFlow) << "revoke answered: HTTP" << status;
        Q_EMIT revoked(status == 200);
    });
}

} // namespace OCC::Nourax::DeviceSync
