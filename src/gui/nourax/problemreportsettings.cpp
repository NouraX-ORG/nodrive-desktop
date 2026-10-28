/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "problemreportsettings.h"

#include "configfile.h"

#include <QSettings>

namespace OCC::Nourax {

namespace {
const auto enabledKey = QStringLiteral("NoDrive/problemReportsEnabled");
}

QString ProblemReportSettings::supportUserId()
{
    return QStringLiteral("nourax-support");
}

QString ProblemReportSettings::remoteFolder()
{
    return QStringLiteral("Problem reports");
}

bool ProblemReportSettings::enabled()
{
    QSettings settings(ConfigFile().configFile(), QSettings::IniFormat);
    return settings.value(enabledKey, false).toBool();
}

void ProblemReportSettings::setEnabled(bool enabled)
{
    QSettings settings(ConfigFile().configFile(), QSettings::IniFormat);
    settings.setValue(enabledKey, enabled);
    settings.sync();
}

QString ProblemReportSettings::reportsDir()
{
    return ConfigFile().configPath() + QStringLiteral("problem-reports");
}

QString ProblemReportSettings::pendingDir()
{
    return reportsDir() + QStringLiteral("/pending");
}

} // namespace OCC::Nourax
