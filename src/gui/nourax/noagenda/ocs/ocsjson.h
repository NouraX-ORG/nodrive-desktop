/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: unwraps the OCS envelope {"ocs":{"meta":{...},"data":...}} and a few JSON helpers.

#pragma once

#include "../net/httptransport.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

namespace OCC::Nourax::Agenda {

struct OcsReply
{
    int httpStatus = 0;
    bool networkError = false;
    bool envelopeOk = false; ///< the body had the {"ocs":{"meta","data"}} shape
    int ocsStatus = 0; ///< meta.statuscode
    QJsonValue data;

    [[nodiscard]] bool ok() const { return httpStatus >= 200 && httpStatus < 300 && envelopeOk; }
    [[nodiscard]] bool notFound() const { return httpStatus == 404 || ocsStatus == 404; }
};

OcsReply parseOcs(const HttpResult &result);

/// The key set of an object as "key:type" strings, sorted (type: string, number, bool, null, array, object).
QStringList keySignature(const QJsonObject &object);
QString jsonTypeName(const QJsonValue &value);

/// Number helper: accepts numbers and numeric strings.
double numberOf(const QJsonValue &value, double fallback = 0);
int intOf(const QJsonValue &value, int fallback = 0);

} // namespace OCC::Nourax::Agenda
