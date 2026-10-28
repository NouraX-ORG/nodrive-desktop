/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoDrive problem reports: the switch, the fixed names and the folders.

#pragma once

#include <QString>

namespace OCC::Nourax {

class ProblemReportSettings
{
public:
    /// The Nourax Support account the "Problem reports" folder is shared with (read-only).
    static QString supportUserId();
    /// Folder in the user's own files. A server path, so it is English in every language.
    static QString remoteFolder();

    static constexpr qint64 maxZipBytes = 5'000'000;
    static constexpr int maxPending = 5;

    /// Off by default. A missing key means off.
    static bool enabled();
    static void setEnabled(bool enabled);

    /// <config folder>/problem-reports
    static QString reportsDir();
    /// <config folder>/problem-reports/pending
    static QString pendingDir();
};

} // namespace OCC::Nourax
