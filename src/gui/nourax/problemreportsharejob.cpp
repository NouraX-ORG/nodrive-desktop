/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "problemreportsharejob.h"

#include <QJsonArray>
#include <QJsonObject>

namespace OCC::Nourax {

namespace {
// Share::TypeUser, written out so this file needs no sharing headers.
constexpr int shareTypeUser = 0;
constexpr int permissionRead = 1;
}

ProblemReportShareJob::ProblemReportShareJob(AccountPtr account)
    : OcsJob(account)
{
    setPath(QStringLiteral("ocs/v2.php/apps/files_sharing/api/v1/shares"));
}

void ProblemReportShareJob::findShares(const QString &path)
{
    setVerb("GET");
    addParam(QStringLiteral("path"), path);
    addParam(QStringLiteral("reshares"), QStringLiteral("false"));
    addParam(QStringLiteral("subfiles"), QStringLiteral("false"));
    addPassStatusCode(404);
    start();
}

void ProblemReportShareJob::createReadOnlyUserShare(const QString &path, const QString &shareWith)
{
    setVerb("POST");
    addParam(QStringLiteral("path"), path);
    addParam(QStringLiteral("shareType"), QString::number(shareTypeUser));
    addParam(QStringLiteral("shareWith"), shareWith);
    addParam(QStringLiteral("permissions"), QString::number(permissionRead));
    start();
}

bool ProblemReportShareJob::hasUserShare(const QJsonDocument &reply, const QString &userId)
{
    const auto data = reply.object().value(QStringLiteral("ocs")).toObject().value(QStringLiteral("data"));
    const auto shares = data.isArray() ? data.toArray() : QJsonArray{};
    for (const auto &entry : shares) {
        const auto share = entry.toObject();
        if (share.value(QStringLiteral("share_type")).toInt(-1) == shareTypeUser
            && share.value(QStringLiteral("share_with")).toString() == userId) {
            return true;
        }
    }
    return false;
}

} // namespace OCC::Nourax
