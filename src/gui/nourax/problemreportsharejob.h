/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoDrive problem reports: the OCS Share API calls for the "Problem reports" folder.
//
// OcsShareJob::createShare() ignores its permissions argument (the server then gives all
// rights), so Support would get write and delete rights. This small job always sends
// permissions=1 (read only).

#pragma once

#include "ocsjob.h"

#include <QJsonDocument>

namespace OCC::Nourax {

class ProblemReportShareJob : public OcsJob
{
    Q_OBJECT

public:
    explicit ProblemReportShareJob(AccountPtr account);

    /// GET the shares of path (404, "no such file", is not an error).
    void findShares(const QString &path);

    /// POST a read-only (permissions=1) share of path with the user shareWith (shareType=0).
    void createReadOnlyUserShare(const QString &path, const QString &shareWith);

    /// True if the reply of findShares() has a user share (share_type 0) with userId.
    [[nodiscard]] static bool hasUserShare(const QJsonDocument &reply, const QString &userId);
};

} // namespace OCC::Nourax
