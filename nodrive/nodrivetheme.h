/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * NoDrive theme class. Selected through upstream's THEME_CLASS / THEME_INCLUDE
 * hook (set in nodrive/NODRIVE.cmake) and included by src/libsync/theme.cpp.
 * Most of the branding comes from the config.h values that NODRIVE.cmake sets;
 * only the virtual Theme methods are overridden here.
 */

#pragma once

#include "libsync/theme.h"

namespace OCC {

class NoDriveTheme : public Theme
{
public:
    NoDriveTheme() = default;

    // Upstream points this at a Nextcloud trial server. NoDrive has one fixed
    // server, so there is no hint to show.
    [[nodiscard]] QString wizardUrlHint() const override { return {}; }
};

} // namespace OCC
