/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoDrive problem reports: the switch and the "Report a problem" button on
// Settings > Advanced, placed next to "Create Debug Archive".

#pragma once

class QAbstractButton;
class QGroupBox;
class QHBoxLayout;
class QVBoxLayout;
class QWidget;

namespace OCC::Nourax {

class ProblemReportsPanel
{
public:
    /// Adds the switch panel above the actions row and the button after the debug archive button.
    /// Call it before the page styles itself (SettingsPanelStyle::apply).
    static void install(QWidget *page, QVBoxLayout *pageLayout, QGroupBox *actionsGroupBox, QHBoxLayout *actionsLayout,
        QAbstractButton *debugArchiveButton);
};

} // namespace OCC::Nourax
