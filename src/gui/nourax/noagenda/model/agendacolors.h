/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: the colour of a source (calendar, project, board) as shown on a row.
// The server's default blues are mapped to Nourax cobalt, so they never show (no-Nextcloud rule).

#pragma once

#include <QString>

namespace OCC::Nourax::Agenda {

/// Nourax cobalt.
QString cobaltColor();
/// "#RRGGBB" from a server colour ("#RRGGBBAA", "RRGGBB", ...), with the server's default blues mapped
/// to cobalt. An empty or invalid colour gives `fallback` (cobalt when empty).
QString sourceColor(const QString &raw, const QString &fallback = QString());

} // namespace OCC::Nourax::Agenda
