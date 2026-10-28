/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ocsjson.h"

#include <QJsonDocument>

namespace OCC::Nourax::Agenda {

OcsReply parseOcs(const HttpResult &result)
{
    OcsReply r;
    r.httpStatus = result.status;
    r.networkError = result.networkError;
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(result.body, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return r;
    }
    const auto ocs = doc.object().value(QLatin1String("ocs"));
    if (!ocs.isObject()) {
        return r;
    }
    const auto meta = ocs.toObject().value(QLatin1String("meta"));
    if (!meta.isObject() || !ocs.toObject().contains(QLatin1String("data"))) {
        return r;
    }
    r.envelopeOk = true;
    r.ocsStatus = meta.toObject().value(QLatin1String("statuscode")).toInt();
    r.data = ocs.toObject().value(QLatin1String("data"));
    return r;
}

QString jsonTypeName(const QJsonValue &value)
{
    switch (value.type()) {
    case QJsonValue::String:
        return QStringLiteral("string");
    case QJsonValue::Double:
        return QStringLiteral("number");
    case QJsonValue::Bool:
        return QStringLiteral("bool");
    case QJsonValue::Null:
        return QStringLiteral("null");
    case QJsonValue::Array:
        return QStringLiteral("array");
    case QJsonValue::Object:
        return QStringLiteral("object");
    case QJsonValue::Undefined:
        break;
    }
    return QStringLiteral("undefined");
}

QStringList keySignature(const QJsonObject &object)
{
    QStringList out;
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        out.append(it.key() + QLatin1Char(':') + jsonTypeName(it.value()));
    }
    out.sort();
    return out;
}

double numberOf(const QJsonValue &value, double fallback)
{
    if (value.isDouble()) {
        return value.toDouble();
    }
    if (value.isString()) {
        bool ok = false;
        const double d = value.toString().toDouble(&ok);
        return ok ? d : fallback;
    }
    return fallback;
}

int intOf(const QJsonValue &value, int fallback)
{
    return static_cast<int>(numberOf(value, fallback));
}

} // namespace OCC::Nourax::Agenda
