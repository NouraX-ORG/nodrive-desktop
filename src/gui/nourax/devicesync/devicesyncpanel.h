/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoDrive device calendar and contacts sync: the "Calendar and contacts" panel of the account page in Settings.
// macOS: "Add to my Mac's Calendar & Contacts" (a configuration profile) and a link to the Thunderbird guide.
// Windows and Linux: "Use with Thunderbird…".

#pragma once

#include <QFrame>

class QLabel;
class QPushButton;
class QVBoxLayout;

namespace OCC {
class AccountState;
}

namespace OCC::Nourax::DeviceSync {

class DeviceSyncPanel : public QFrame
{
    Q_OBJECT

public:
    /// Inserts the panel into the account page's layout, directly before `before` (the account actions panel).
    /// Call it while the page is built, before the page styles itself.
    static DeviceSyncPanel *install(QWidget *page, OCC::AccountState *state, QVBoxLayout *layout, QWidget *before);

    DeviceSyncPanel(OCC::AccountState *state, QWidget *parent);

    [[nodiscard]] QLabel *titleLabel() const { return _title; }
    [[nodiscard]] QLabel *descriptionLabel() const { return _description; }
    [[nodiscard]] QLabel *hintLabel() const { return _hint; }
    [[nodiscard]] QPushButton *macButton() const { return _macButton; }
    [[nodiscard]] QPushButton *thunderbirdButton() const { return _thunderbirdButton; }

    [[nodiscard]] static QString titleText();
    [[nodiscard]] static QString macDescriptionText();
    [[nodiscard]] static QString otherDescriptionText();
    [[nodiscard]] static QString notConnectedText();

    /// Opens the Thunderbird guide for this account.
    void showThunderbirdGuide();
    /// Opens the Mac setup for this account.
    void showMacSetup();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void updateConnectionState();

    OCC::AccountState *_state = nullptr;
    QLabel *_title = nullptr;
    QLabel *_description = nullptr;
    QLabel *_hint = nullptr;
    QPushButton *_macButton = nullptr;
    QPushButton *_thunderbirdButton = nullptr;
};

} // namespace OCC::Nourax::DeviceSync
