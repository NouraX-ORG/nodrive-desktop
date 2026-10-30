/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoDrive device calendar and contacts sync (macOS): "Add to my Mac's Calendar & Contacts". Three steps:
// 1. what will happen, 2. waiting for the person to allow access in the browser (Login Flow v2: a separate app password),
// 3. install the profile in System Settings. plans/noagenda-desktop-plan.md, 4.3 and 4.5.
//
// The password is never shown, logged or saved: it goes from the flow into the profile, and is overwritten with zeros
// after "Installed" or "It didn't work" (which revokes it).

#pragma once

#include "accountfwd.h"

#include <QByteArray>
#include <QDialog>
#include <QString>
#include <QUrl>

class QLabel;
class QNetworkAccessManager;
class QPushButton;
class QStackedWidget;

namespace OCC::Nourax::DeviceSync {

class DeviceAppPasswordFlow;
class MacProfileInstaller;

class MacSetupDialog : public QDialog
{
    Q_OBJECT

public:
    enum class Step { Intro = 0, Waiting, Install, Failed };
    Q_ENUM(Step)

    /// `nam` is injected by tests (the flow's plain network manager).
    explicit MacSetupDialog(const AccountPtr &account, QWidget *parent = nullptr, QNetworkAccessManager *nam = nullptr);
    ~MacSetupDialog() override;

    [[nodiscard]] Step step() const;
    [[nodiscard]] QString errorText() const;
    /// Tests make the flow fast: poll interval and time limit in milliseconds (-1 = the real values: 5 s and 20 min).
    static void setFlowTimingForTests(int pollMs, int timeoutMs);

    /// The computer name that ends up in the token's name; tests set it.
    void setComputerName(const QString &name);

    // For tests.
    [[nodiscard]] QPushButton *continueButton() const { return _continueButton; }
    [[nodiscard]] QPushButton *cancelButton() const { return _cancelButton; }
    [[nodiscard]] QPushButton *reopenButton() const { return _reopenButton; }
    [[nodiscard]] QPushButton *installedButton() const { return _installedButton; }
    [[nodiscard]] QPushButton *didNotWorkButton() const { return _didNotWorkButton; }
    [[nodiscard]] QLabel *installLabel() const { return _installLabel; }
    [[nodiscard]] DeviceAppPasswordFlow *flow() const { return _flow; }

    [[nodiscard]] static QString introText();
    [[nodiscard]] static QString waitingText();
    [[nodiscard]] static QString installText(int macMajorVersion);
    [[nodiscard]] static QString insecureText();

    void done(int result) override;

private:
    void onContinue();
    void onGranted(const QString &loginName, const QByteArray &appPassword);
    void onFlowFailed(const QString &text);
    void onInstalled();
    void onDidNotWork();
    void showFailure(const QString &text);
    void forgetPassword();
    void finishUp();

    AccountPtr _account;
    QNetworkAccessManager *_nam = nullptr;
    QString _computerName;
    DeviceAppPasswordFlow *_flow = nullptr;
    MacProfileInstaller *_installer = nullptr;
    QUrl _loginUrl;
    QString _loginName;
    QByteArray _password;
    bool _revoking = false;

    QStackedWidget *_stack = nullptr;
    QPushButton *_continueButton = nullptr;
    QPushButton *_cancelButton = nullptr;
    QPushButton *_reopenButton = nullptr;
    QPushButton *_waitingCancelButton = nullptr;
    QPushButton *_installedButton = nullptr;
    QPushButton *_didNotWorkButton = nullptr;
    QPushButton *_closeButton = nullptr;
    QLabel *_waitingLabel = nullptr;
    QLabel *_installLabel = nullptr;
    QLabel *_failureLabel = nullptr;
};

} // namespace OCC::Nourax::DeviceSync
