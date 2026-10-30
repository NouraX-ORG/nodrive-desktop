/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoDrive device calendar and contacts sync: when the user removes an account on a Mac where the Apple profile was
// offered, shows how to remove the profile again. Shared helpers: which system we are on, and the texts.
//
// It listens to AccountManager::accountSyncConnectionRemoved (the user removed the account), not to accountRemoved,
// which AccountManager::shutdown() also emits for every account when the app quits.

#pragma once

#include <QObject>
#include <QString>

#include <functional>

class QMessageBox;

namespace OCC {
class AccountState;
}

namespace OCC::Nourax::DeviceSync {

class DeviceSyncService : public QObject
{
    Q_OBJECT

public:
    static DeviceSyncService *instance();

    /// True on macOS. Tests can force either answer (-1 = the real system).
    [[nodiscard]] static bool isMac();
    static void setMacForTests(int forced);
    /// Tests can force the macOS major version (-1 = the real one).
    static void setMacMajorVersionForTests(int forced);

    /// "System Settings › General › Device Management" (macOS 15 and newer) or "System Settings › Privacy & Security › Profiles".
    [[nodiscard]] static QString profilesPath(int macMajorVersion);
    [[nodiscard]] static int currentMacMajorVersion();

    [[nodiscard]] static QString removalText(int macMajorVersion);

    /// The message box with the removal note (not shown yet; the caller owns it).
    [[nodiscard]] static QMessageBox *createRemovalBox(int macMajorVersion);

    /// Tests catch the note instead of showing a message box.
    static void setMessageHookForTests(std::function<void(const QString &)> hook);

    /// What happens when the account is removed (also called by the signal).
    void accountDeleted(const QString &accountId);

private Q_SLOTS:
    void onAccountSyncConnectionRemoved(OCC::AccountState *state);

private:
    DeviceSyncService();
};

} // namespace OCC::Nourax::DeviceSync
