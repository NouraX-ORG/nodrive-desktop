/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: helpers that all three writers (CalDAV, NoGantt, NoBoard) share.

#pragma once

#include "../net/httptransport.h"
#include "editcommand.h"

namespace OCC::Nourax::Agenda::WriterUtil {

/// Fills `result` and returns true when the answer is a network error, a sign-in problem or a refusal that
/// every writer treats the same way. Other answers return false.
/// `app` is "NoGantt" or "NoBoard" for a request to one of those apps: their 401 or 403 is a refusal by that app
/// ("Open in ..."), never a sign-in problem of the account (design 5). Only CalDAV (`app` empty) gives NeedsLogin.
bool commonFailure(const HttpResult &answer, EditResult *result, const QString &app = QString());

EditResult conflict(const QString &kind, const QString &message);
EditResult refused(const QString &message, int status = 0);
EditResult failed(const QString &message, int status = 0);
EditResult done(const QString &newVersion = QString());

/// "Y-m-d" of a NoGantt date text ("2026-11-04" or "2026-11-04 00:00:00").
QString dayOf(const QString &text);
/// The time part (" 00:00:00") of a NoGantt date text, to keep it when the day changes. Empty if there is none.
QString timePartOf(const QString &text);

/// JSON request (body, Content-Type, Accept, OCS-APIRequest) to the account's own server.
HttpRequest jsonRequest(const HttpTransport &transport, const QByteArray &verb, const QString &relativePathAndQuery, const QJsonObject &body);
HttpRequest jsonRequest(const HttpTransport &transport, const QByteArray &verb, const QString &relativePathAndQuery, const QJsonArray &body);

} // namespace OCC::Nourax::Agenda::WriterUtil
