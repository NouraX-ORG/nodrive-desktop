/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoDrive device calendar and contacts sync: the one thing that is remembered per account, whether the Apple profile
// was offered on this Mac (for the removal note). No secret is ever saved.

#pragma once

#include <QString>

namespace OCC::Nourax::DeviceSync {

class DeviceSyncSettings
{
public:
    [[nodiscard]] static bool profileOffered(const QString &accountId);
    static void setProfileOffered(const QString &accountId, bool offered);

    /// The settings key (the account id is hashed, so that no character of it needs escaping).
    [[nodiscard]] static QString profileOfferedKey(const QString &accountId);
};

} // namespace OCC::Nourax::DeviceSync
