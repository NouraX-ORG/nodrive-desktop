/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "devicesyncsettings.h"

#include "configfile.h"

#include <QCryptographicHash>
#include <QSettings>

namespace OCC::Nourax::DeviceSync {

QString DeviceSyncSettings::profileOfferedKey(const QString &accountId)
{
    const auto hash = QCryptographicHash::hash(accountId.toUtf8(), QCryptographicHash::Sha256).toHex().left(16);
    return QStringLiteral("NoDrive/deviceSync/%1/profileOffered").arg(QString::fromLatin1(hash));
}

bool DeviceSyncSettings::profileOffered(const QString &accountId)
{
    QSettings settings(ConfigFile().configFile(), QSettings::IniFormat);
    return settings.value(profileOfferedKey(accountId), false).toBool();
}

void DeviceSyncSettings::setProfileOffered(const QString &accountId, bool offered)
{
    QSettings settings(ConfigFile().configFile(), QSettings::IniFormat);
    if (offered) {
        settings.setValue(profileOfferedKey(accountId), true);
    } else {
        settings.remove(profileOfferedKey(accountId));
    }
    settings.sync();
}

} // namespace OCC::Nourax::DeviceSync
