/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoDrive device calendar and contacts sync: the one place that opens an address in the default browser.
// Tests replace it, so no browser starts.

#pragma once

#include <QDesktopServices>
#include <QUrl>

#include <functional>

namespace OCC::Nourax::DeviceSync {

using UrlOpenerFn = std::function<bool(const QUrl &)>;

inline UrlOpenerFn &urlOpenerHook()
{
    static UrlOpenerFn hook;
    return hook;
}

inline bool openInBrowser(const QUrl &url)
{
    return urlOpenerHook() ? urlOpenerHook()(url) : QDesktopServices::openUrl(url);
}

} // namespace OCC::Nourax::DeviceSync
