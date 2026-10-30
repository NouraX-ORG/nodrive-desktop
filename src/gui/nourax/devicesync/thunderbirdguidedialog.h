/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoDrive device calendar and contacts sync (Windows, Linux; also offered on macOS): a short guide for Thunderbird,
// with buttons that copy the server's calendar and contacts addresses. No credential is ever copied.

#pragma once

#include <QDialog>
#include <QString>
#include <QUrl>

class QLabel;
class QPushButton;

namespace OCC::Nourax::DeviceSync {

class ThunderbirdGuideDialog : public QDialog
{
    Q_OBJECT

public:
    /// `loginName` is shown in step 2 ("Enter your user name"). `davUser` makes the addresses.
    ThunderbirdGuideDialog(const QUrl &server, const QString &davUser, const QString &loginName, QWidget *parent = nullptr);

    [[nodiscard]] QString calendarAddress() const { return _calendarAddress; }
    [[nodiscard]] QString contactsAddress() const { return _contactsAddress; }
    [[nodiscard]] QUrl securitySettingsUrl() const { return _securityUrl; }

    [[nodiscard]] static QString titleText();
    [[nodiscard]] static QString stepOneText();
    [[nodiscard]] static QString stepTwoText(const QString &loginName);
    [[nodiscard]] static QString stepThreeText();
    [[nodiscard]] static QString footerText();

private:
    void copyAddress(const QString &address, QLabel *confirmation);

    QString _calendarAddress;
    QString _contactsAddress;
    QUrl _securityUrl;
};

} // namespace OCC::Nourax::DeviceSync
