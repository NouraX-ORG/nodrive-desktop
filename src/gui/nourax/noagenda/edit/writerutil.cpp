/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "writerutil.h"

#include <QJsonDocument>

namespace OCC::Nourax::Agenda::WriterUtil {

bool commonFailure(const HttpResult &answer, EditResult *result)
{
    if (answer.networkError || answer.status == 0) {
        result->outcome = EditResult::Outcome::Offline;
        result->message = AgendaText::tr("There is no connection to the server.");
        return true;
    }
    if (answer.status == 401) {
        result->outcome = EditResult::Outcome::NeedsLogin;
        result->httpStatus = answer.status;
        result->message = AgendaText::tr("Sign in to NoDrive to send this change.");
        return true;
    }
    if (answer.status == 403) {
        result->outcome = EditResult::Outcome::Refused;
        result->httpStatus = answer.status;
        result->conflictKind = QStringLiteral("refused");
        result->message = AgendaText::tr("The server does not let you change this.");
        return true;
    }
    return false;
}

EditResult conflict(const QString &kind, const QString &message)
{
    EditResult r;
    r.outcome = EditResult::Outcome::Conflict;
    r.conflictKind = kind;
    r.message = message;
    return r;
}

EditResult refused(const QString &message, int status)
{
    EditResult r;
    r.outcome = EditResult::Outcome::Refused;
    r.conflictKind = QStringLiteral("refused");
    r.message = message;
    r.httpStatus = status;
    return r;
}

EditResult failed(const QString &message, int status)
{
    EditResult r;
    r.outcome = EditResult::Outcome::Failed;
    r.message = message;
    r.httpStatus = status;
    return r;
}

EditResult done(const QString &newVersion)
{
    EditResult r;
    r.outcome = EditResult::Outcome::Done;
    r.newVersion = newVersion;
    return r;
}

QString dayOf(const QString &text)
{
    return text.left(10);
}

QString timePartOf(const QString &text)
{
    if (text.size() > 10 && (text.at(10) == QLatin1Char(' ') || text.at(10) == QLatin1Char('T'))) {
        return text.mid(10);
    }
    return QString();
}

namespace {
HttpRequest prepared(const HttpTransport &transport, const QByteArray &verb, const QString &path, const QByteArray &body)
{
    auto r = makeRequest(verb, transport.url(path));
    r.headers.append({QByteArrayLiteral("OCS-APIRequest"), QByteArrayLiteral("true")});
    r.headers.append({QByteArrayLiteral("Accept"), QByteArrayLiteral("application/json")});
    r.headers.append({QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/json")});
    r.body = body;
    return r;
}
} // namespace

HttpRequest jsonRequest(const HttpTransport &transport, const QByteArray &verb, const QString &path, const QJsonObject &body)
{
    return prepared(transport, verb, path, QJsonDocument(body).toJson(QJsonDocument::Compact));
}

HttpRequest jsonRequest(const HttpTransport &transport, const QByteArray &verb, const QString &path, const QJsonArray &body)
{
    return prepared(transport, verb, path, QJsonDocument(body).toJson(QJsonDocument::Compact));
}

} // namespace OCC::Nourax::Agenda::WriterUtil
