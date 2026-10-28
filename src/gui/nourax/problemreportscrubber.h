/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoDrive problem reports: removes secrets from everything that goes into a report.
// File and folder names are kept on purpose, they are needed to find the problem.

#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace OCC::Nourax {

class ProblemReportScrubber
{
public:
    /// Scrubs free text (logs, crash text, notes, build info). Idempotent.
    static QString scrubText(const QString &text);

    /// Scrubs an INI settings file: keys that look secret lose their value, then scrubText().
    static QByteArray scrubConfig(const QByteArray &ini);

    /// Scrubs the command line (for example "--apppassword <value>") and joins it with spaces.
    static QString scrubArguments(const QStringList &arguments);

private:
    static QString scrubLine(const QString &line);
};

} // namespace OCC::Nourax
