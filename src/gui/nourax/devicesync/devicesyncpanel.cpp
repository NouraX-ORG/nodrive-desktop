/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "devicesyncpanel.h"

#include "devicesyncservice.h"
#include "macsetupdialog.h"
#include "thunderbirdguidedialog.h"

#include "account.h"
#include "accountstate.h"
#include "theme.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace OCC::Nourax::DeviceSync {

namespace {

QString trPanel(const char *text)
{
    return QCoreApplication::translate("OCC::Nourax::DeviceSync::DeviceSyncPanel", text);
}

} // namespace

QString DeviceSyncPanel::titleText()
{
    return trPanel("Calendar and contacts");
}

QString DeviceSyncPanel::macDescriptionText()
{
    return trPanel("Use your NoDrive calendars, reminders and contacts in the Calendar, Reminders and Contacts apps on this Mac.");
}

QString DeviceSyncPanel::otherDescriptionText()
{
    return trPanel("Use your NoDrive calendars and contacts in Thunderbird.");
}

QString DeviceSyncPanel::notConnectedText()
{
    return trPanel("Connect to NoDrive first.");
}

DeviceSyncPanel *DeviceSyncPanel::install(QWidget *page, OCC::AccountState *state, QVBoxLayout *layout, QWidget *before)
{
    // Makes the service that shows the removal note (once per run).
    DeviceSyncService::instance();

    auto *panel = new DeviceSyncPanel(state, page);
    layout->insertWidget(before ? layout->indexOf(before) : layout->count(), panel);
    return panel;
}

DeviceSyncPanel::DeviceSyncPanel(OCC::AccountState *state, QWidget *parent)
    : QFrame(parent)
    , _state(state)
{
    setObjectName(QStringLiteral("deviceSyncPanel"));
    setFrameShape(QFrame::NoFrame);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(8);

    const auto mac = DeviceSyncService::isMac();

    _title = new QLabel(titleText(), this);
    _title->setObjectName(QStringLiteral("deviceSyncTitle"));
    auto titleFont = _title->font();
    titleFont.setBold(true);
    _title->setFont(titleFont);
    layout->addWidget(_title);

    _description = new QLabel(mac ? macDescriptionText() : otherDescriptionText(), this);
    _description->setObjectName(QStringLiteral("deviceSyncDescription"));
    _description->setWordWrap(true);
    layout->addWidget(_description);

    _hint = new QLabel(notConnectedText(), this);
    _hint->setObjectName(QStringLiteral("deviceSyncHint"));
    _hint->setWordWrap(true);
    auto hintFont = _hint->font();
    hintFont.setItalic(true);
    _hint->setFont(hintFont);
    layout->addWidget(_hint);

    auto *buttons = new QHBoxLayout();
    buttons->setSpacing(12);
    if (mac) {
        // "&&" shows one "&": a button text reads a single "&" as a shortcut marker.
        _macButton = new QPushButton(trPanel("Add to my Mac's Calendar && Contacts"), this);
        _macButton->setObjectName(QStringLiteral("deviceSyncMacButton"));
        buttons->addWidget(_macButton, 0, Qt::AlignLeft);
        connect(_macButton, &QPushButton::clicked, this, &DeviceSyncPanel::showMacSetup);

        // The guide is also offered on macOS, as a link.
        _thunderbirdButton = new QPushButton(trPanel("Use with Thunderbird…"), this);
        _thunderbirdButton->setFlat(true);
        auto linkFont = _thunderbirdButton->font();
        linkFont.setUnderline(true);
        _thunderbirdButton->setFont(linkFont);
        _thunderbirdButton->setCursor(Qt::PointingHandCursor);
    } else {
        _thunderbirdButton = new QPushButton(trPanel("Use with Thunderbird…"), this);
    }
    _thunderbirdButton->setObjectName(QStringLiteral("deviceSyncThunderbirdButton"));
    buttons->addWidget(_thunderbirdButton, 0, Qt::AlignLeft);
    buttons->addStretch();
    connect(_thunderbirdButton, &QPushButton::clicked, this, &DeviceSyncPanel::showThunderbirdGuide);
    layout->addLayout(buttons);

    if (_state) {
        connect(_state, &AccountState::stateChanged, this, &DeviceSyncPanel::updateConnectionState);
    }
    updateConnectionState();
}

void DeviceSyncPanel::updateConnectionState()
{
    const auto connected = _state && _state->isConnected();
    _hint->setVisible(!connected);
    if (_macButton) {
        _macButton->setEnabled(connected);
    }
    _thunderbirdButton->setEnabled(connected);
}

void DeviceSyncPanel::showThunderbirdGuide()
{
    if (!_state || !_state->account()) {
        return;
    }
    const auto account = _state->account();
    auto loginName = account->userFromCredentials();
    if (loginName.isEmpty()) {
        loginName = account->davUser();
    }
    auto *dialog = new ThunderbirdGuideDialog(account->url(), account->davUser(), loginName, window());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->open();
}

void DeviceSyncPanel::showMacSetup()
{
    if (!_state || !_state->account()) {
        return;
    }
    auto *dialog = new MacSetupDialog(_state->account(), window());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->open();
}

void DeviceSyncPanel::paintEvent(QPaintEvent *event)
{
    // The same rounded panel colour as the other panels of the Settings dialog (it paints them with a style sheet).
#ifdef Q_OS_WIN
    const auto color = palette().color(QPalette::AlternateBase);
#else
    const auto color = Theme::settingsPanelColor(QGuiApplication::palette());
#endif
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawRoundedRect(rect(), 12, 12);
    QFrame::paintEvent(event);
}

} // namespace OCC::Nourax::DeviceSync
