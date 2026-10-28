/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "driveregistrysource.h"

namespace OCC::Nourax::Agenda {

namespace {

// The address of the Nourax agents app, relative to the account's own server (not a secret).
// Empty while the agents app does not exist: the placeholder file in the user's NoDrive is read.
// When the agents app is there, putting its address here is the only change needed.
constexpr const char *kAgentsAppRegistryUrl = "";

} // namespace

std::unique_ptr<RegistrySource> makeRegistrySource(HttpTransport *transport)
{
    const QString url = QString::fromLatin1(kAgentsAppRegistryUrl);
    if (url.isEmpty()) {
        return std::make_unique<DriveRegistrySource>(transport);
    }
    return std::make_unique<UrlRegistrySource>(transport, url);
}

} // namespace OCC::Nourax::Agenda
