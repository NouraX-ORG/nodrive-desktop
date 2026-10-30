/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "devicesyncservice.h"

#include "devicesyncsettings.h"

#include "account.h"
#include "accountmanager.h"
#include "accountstate.h"

#include <QCoreApplication>
#include <QMessageBox>
#include <QOperatingSystemVersion>

namespace OCC::Nourax::DeviceSync {

namespace {
int &macForced()
{
    static int forced = -1;
    return forced;
}

int &majorForced()
{
    static int forced = -1;
    return forced;
}

std::function<void(const QString &)> &messageHook()
{
    static std::function<void(const QString &)> hook;
    return hook;
}
} // namespace

DeviceSyncService::DeviceSyncService()
    : QObject(QCoreApplication::instance())
{
    if (const auto manager = AccountManager::instance()) {
        connect(manager, &AccountManager::accountSyncConnectionRemoved, this, &DeviceSyncService::onAccountSyncConnectionRemoved);
    }
}

DeviceSyncService *DeviceSyncService::instance()
{
    static DeviceSyncService *service = new DeviceSyncService;
    return service;
}

bool DeviceSyncService::isMac()
{
    if (macForced() >= 0) {
        return macForced() == 1;
    }
#ifdef Q_OS_MACOS
    return true;
#else
    return false;
#endif
}

void DeviceSyncService::setMacForTests(int forced)
{
    macForced() = forced;
}

void DeviceSyncService::setMacMajorVersionForTests(int forced)
{
    majorForced() = forced;
}

int DeviceSyncService::currentMacMajorVersion()
{
    if (majorForced() >= 0) {
        return majorForced();
    }
    return QOperatingSystemVersion::current().majorVersion();
}

QString DeviceSyncService::profilesPath(int macMajorVersion)
{
    // Checked against Apple's documentation: macOS 15 moved the profiles into General > Device Management.
    if (macMajorVersion >= 15) {
        return tr("System Settings › General › Device Management");
    }
    return tr("System Settings › Privacy & Security › Profiles");
}

QString DeviceSyncService::removalText(int macMajorVersion)
{
    return tr("To remove NoDrive calendars and contacts from this Mac, open %1, select NoDrive Calendar & Contacts and click Remove. "
              "You can also remove its password in NoDrive's Security settings on the web.")
        .arg(profilesPath(macMajorVersion));
}

QMessageBox *DeviceSyncService::createRemovalBox(int macMajorVersion)
{
    return new QMessageBox(QMessageBox::Information, QString(), removalText(macMajorVersion), QMessageBox::Ok);
}

void DeviceSyncService::setMessageHookForTests(std::function<void(const QString &)> hook)
{
    messageHook() = std::move(hook);
}

void DeviceSyncService::onAccountSyncConnectionRemoved(OCC::AccountState *state)
{
    if (!state || !state->account()) {
        return;
    }
    accountDeleted(state->account()->id());
}

void DeviceSyncService::accountDeleted(const QString &accountId)
{
    if (!DeviceSyncSettings::profileOffered(accountId)) {
        return;
    }
    DeviceSyncSettings::setProfileOffered(accountId, false);
    if (!isMac()) {
        return;
    }
    const auto text = removalText(currentMacMajorVersion());
    if (messageHook()) {
        messageHook()(text);
        return;
    }
    auto *box = new QMessageBox(QMessageBox::Information, QString(), text, QMessageBox::Ok);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->open();
}

} // namespace OCC::Nourax::DeviceSync
