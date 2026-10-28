/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: makes the QML window for one controller (used by the launcher and by the tests).

#pragma once

#include <QString>

class QQmlEngine;
class QQuickWindow;

namespace OCC::Nourax::Agenda {

class AgendaController;

/// Makes the resources of NoAgenda (QML files) available. Safe to call more than once.
void initNoAgendaResources();

/// Creates the (hidden) window. The controller is not owned by the window. Returns null and fills `error`
/// when the QML could not be loaded.
QQuickWindow *createNoAgendaWindow(QQmlEngine *engine, AgendaController *controller, QString *error);

} // namespace OCC::Nourax::Agenda
