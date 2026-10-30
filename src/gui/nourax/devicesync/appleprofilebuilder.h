/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoDrive device calendar and contacts sync (design section 10): builds the Apple configuration profile
// (.mobileconfig) with a CalDAV and a CardDAV account. Pure: no network, no files, no logging.
// The keys are the ones of Apple's documentation (the "CalDAV" and "CardDAV" payloads of a configuration profile).

#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QUrl>

namespace OCC::Nourax::DeviceSync {

struct AppleProfileInput
{
    /// The account's server, for example https://drive.nourax.org. Host, port and an optional path prefix are used.
    QUrl server;
    /// The user id in the DAV paths (Account::davUser()).
    QString davUser;
    /// The name that signs in (the loginName of Login Flow v2).
    QString loginName;
    /// The separate app password for this device. It is written into the profile and nowhere else.
    QByteArray appPassword;
    /// The account's id (Account::id()), only hashed into the profile identifier.
    QString accountId;
    /// The bundle id of the app; the profile identifier starts with it.
    QString bundleId;
};

class AppleProfileBuilder
{
public:
    /// The profile as XML property list. Empty when the input is incomplete (no host, user, password or bundle id).
    /// A new PayloadUUID is made on every call; the PayloadIdentifier stays the same for the same account.
    [[nodiscard]] static QByteArray build(const AppleProfileInput &input);

    /// <bundle id>.davprofile.<hash of the account id>. Stable per account, so that installing again replaces the old profile.
    [[nodiscard]] static QString payloadIdentifier(const QString &bundleId, const QString &accountId);

    /// https://<host>/remote.php/dav/principals/users/<davUser>/ (a path prefix of the server is kept).
    [[nodiscard]] static QString principalUrl(const QUrl &server, const QString &davUser);

    /// The addresses for Thunderbird and the like (no credentials in them).
    [[nodiscard]] static QString calendarHomeUrl(const QUrl &server, const QString &davUser);
    [[nodiscard]] static QString addressBookHomeUrl(const QUrl &server, const QString &davUser);

    /// The keys of one account payload, exactly the Apple keys: prefix is "CalDAV" or "CardDAV".
    [[nodiscard]] static QStringList accountKeys(const QString &prefix);
};

} // namespace OCC::Nourax::DeviceSync
