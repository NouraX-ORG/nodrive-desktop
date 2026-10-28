/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: the "NoAgenda" entry of the tray menu and the window it opens (one window per account).

#pragma once

#include <QString>

class QMenu;

namespace OCC::Nourax::Agenda {

class NoAgendaLauncher
{
public:
    /// "NoAgenda"
    static QString trayEntryText();
    /// Windows right-click menu: the entry shows only when an account exists.
    static void addContextMenuEntry(QMenu *menu);
    /// Opens (or raises) the window of the account with this index in the user list.
    static void openForUserIndex(int userIndex);
    static void openForCurrentUser();
};

} // namespace OCC::Nourax::Agenda
