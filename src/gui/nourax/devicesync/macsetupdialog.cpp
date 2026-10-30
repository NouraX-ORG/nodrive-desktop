/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "macsetupdialog.h"

#include "appleprofilebuilder.h"
#include "deviceapppasswordflow.h"
#include "devicesyncservice.h"
#include "devicesyncsettings.h"
#include "macprofileinstaller.h"
#include "urlopener.h"

#include "account.h"
#include "config.h"

#include <QCoreApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QSysInfo>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <memory>

namespace OCC::Nourax::DeviceSync {

namespace {

QString trMacSetup(const char *text)
{
    return QCoreApplication::translate("OCC::Nourax::DeviceSync::MacSetupDialog", text);
}

int &flowPollMs()
{
    static int value = -1;
    return value;
}

int &flowTimeoutMs()
{
    static int value = -1;
    return value;
}

int &revokeTimeoutMs()
{
    static int value = -1;
    return value;
}

QLabel *wrappedLabel(const QString &objectName, QWidget *parent)
{
    auto *label = new QLabel(parent);
    label->setObjectName(objectName);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::NoTextInteraction);
    return label;
}

QWidget *buttonRow(QWidget *parent, const QList<QPushButton *> &buttons)
{
    auto *row = new QWidget(parent);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addStretch();
    for (auto *button : buttons) {
        layout->addWidget(button);
    }
    return row;
}

} // namespace

QString MacSetupDialog::introText()
{
    return trMacSetup("NoDrive will open your browser. Allow access there to create a separate password for this Mac's Calendar and Contacts. "
               "You won't see it, and NoDrive doesn't keep it.");
}

QString MacSetupDialog::waitingText()
{
    return trMacSetup("Waiting for you to allow access in the browser…");
}

QString MacSetupDialog::installText(int macMajorVersion)
{
    return trMacSetup("Now install the profile: open %1, double-click \"NoDrive Calendar & Contacts\" in Downloaded, then click Install.")
        .arg(DeviceSyncService::profilesPath(macMajorVersion));
}

QString MacSetupDialog::insecureText()
{
    return trMacSetup("A secure connection (https) is needed to add the calendars and contacts to this Mac.");
}

QString MacSetupDialog::wrongPersonText(const QString &allowedAs, const QString &expected)
{
    return trMacSetup("You allowed access as %1 in the browser. Sign in there as %2 and try again.").arg(allowedAs, expected);
}

QString MacSetupDialog::revokeFailedText(const QString &computerName)
{
    return trMacSetup("NoDrive couldn't remove the separate password. Remove \"NoDrive Calendar & Contacts (%1)\" in Security settings on the web.")
        .arg(computerName);
}

void MacSetupDialog::setRevokeTimeoutForTests(int ms)
{
    revokeTimeoutMs() = ms;
}

void MacSetupDialog::setFlowTimingForTests(int pollMs, int timeoutMs)
{
    flowPollMs() = pollMs;
    flowTimeoutMs() = timeoutMs;
}

MacSetupDialog::MacSetupDialog(const AccountPtr &account, QWidget *parent, QNetworkAccessManager *nam)
    : QDialog(parent)
    , _account(account)
    , _nam(nam)
    , _computerName(QSysInfo::machineHostName())
{
    setObjectName(QStringLiteral("macSetupDialog"));
    setWindowTitle(trMacSetup("Add to my Mac's Calendar & Contacts"));
    setMinimumWidth(480);

    auto *layout = new QVBoxLayout(this);
    _stack = new QStackedWidget(this);
    _stack->setObjectName(QStringLiteral("macSetupStack"));
    layout->addWidget(_stack);

    // Step 1
    {
        auto *page = new QWidget(_stack);
        auto *pageLayout = new QVBoxLayout(page);
        auto *label = wrappedLabel(QStringLiteral("macSetupIntro"), page);
        label->setText(introText());
        pageLayout->addWidget(label);
        pageLayout->addStretch();
        _continueButton = new QPushButton(trMacSetup("Continue"), page);
        _continueButton->setObjectName(QStringLiteral("macSetupContinue"));
        _continueButton->setDefault(true);
        _cancelButton = new QPushButton(trMacSetup("Cancel"), page);
        _cancelButton->setObjectName(QStringLiteral("macSetupCancel"));
        pageLayout->addWidget(buttonRow(page, {_cancelButton, _continueButton}));
        _stack->addWidget(page);
    }
    // Step 2
    {
        auto *page = new QWidget(_stack);
        auto *pageLayout = new QVBoxLayout(page);
        _waitingLabel = wrappedLabel(QStringLiteral("macSetupWaiting"), page);
        _waitingLabel->setText(waitingText());
        pageLayout->addWidget(_waitingLabel);
        pageLayout->addStretch();
        _reopenButton = new QPushButton(trMacSetup("Open the browser again"), page);
        _reopenButton->setObjectName(QStringLiteral("macSetupReopen"));
        _waitingCancelButton = new QPushButton(trMacSetup("Cancel"), page);
        _waitingCancelButton->setObjectName(QStringLiteral("macSetupWaitingCancel"));
        pageLayout->addWidget(buttonRow(page, {_waitingCancelButton, _reopenButton}));
        _stack->addWidget(page);
    }
    // Step 3
    {
        auto *page = new QWidget(_stack);
        auto *pageLayout = new QVBoxLayout(page);
        _installLabel = wrappedLabel(QStringLiteral("macSetupInstall"), page);
        _installLabel->setText(installText(DeviceSyncService::currentMacMajorVersion()));
        pageLayout->addWidget(_installLabel);
        pageLayout->addStretch();
        _didNotWorkButton = new QPushButton(trMacSetup("It didn't work"), page);
        _didNotWorkButton->setObjectName(QStringLiteral("macSetupDidNotWork"));
        _installedButton = new QPushButton(trMacSetup("Installed"), page);
        _installedButton->setObjectName(QStringLiteral("macSetupInstalled"));
        _installedButton->setDefault(true);
        pageLayout->addWidget(buttonRow(page, {_didNotWorkButton, _installedButton}));
        _stack->addWidget(page);
    }
    // Failed
    {
        auto *page = new QWidget(_stack);
        auto *pageLayout = new QVBoxLayout(page);
        _failureLabel = wrappedLabel(QStringLiteral("macSetupFailure"), page);
        pageLayout->addWidget(_failureLabel);
        pageLayout->addStretch();
        _closeButton = new QPushButton(trMacSetup("Close"), page);
        _closeButton->setObjectName(QStringLiteral("macSetupClose"));
        _openSecurityButton = new QPushButton(trMacSetup("Open Security settings"), page);
        _openSecurityButton->setObjectName(QStringLiteral("macSetupOpenSecurity"));
        _openSecurityButton->hide(); // only after a revoke that did not work
        pageLayout->addWidget(buttonRow(page, {_openSecurityButton, _closeButton}));
        _stack->addWidget(page);
    }

    _installer = new MacProfileInstaller(this);

    connect(_continueButton, &QPushButton::clicked, this, &MacSetupDialog::onContinue);
    connect(_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(_waitingCancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(_closeButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(_openSecurityButton, &QPushButton::clicked, this, [this] {
        auto url = _account->url();
        url.setPath(url.path().remove(QRegularExpression(QStringLiteral("/+$"))) + QStringLiteral("/index.php/settings/user/security"));
        openInBrowser(url);
    });
    connect(_reopenButton, &QPushButton::clicked, this, [this] {
        if (_loginUrl.isValid()) {
            openInBrowser(_loginUrl);
        }
    });
    connect(_installedButton, &QPushButton::clicked, this, &MacSetupDialog::onInstalled);
    connect(_didNotWorkButton, &QPushButton::clicked, this, &MacSetupDialog::onDidNotWork);
}

MacSetupDialog::~MacSetupDialog()
{
    finishUp();
}

DeviceAppPasswordFlow *MacSetupDialog::flow() const
{
    return _flow.data();
}

MacSetupDialog::Step MacSetupDialog::step() const
{
    return static_cast<Step>(_stack->currentIndex());
}

QString MacSetupDialog::errorText() const
{
    return step() == Step::Failed ? _failureLabel->text() : QString();
}

void MacSetupDialog::setComputerName(const QString &name)
{
    _computerName = name;
}

void MacSetupDialog::showFailure(const QString &text)
{
    _failureLabel->setText(text);
    _stack->setCurrentIndex(static_cast<int>(Step::Failed));
}

void MacSetupDialog::onContinue()
{
    if (!_account || _account->url().scheme() != QLatin1String("https")) {
        showFailure(insecureText());
        return;
    }
    _flow = new DeviceAppPasswordFlow(_account->url(), _computerName, _nam, this);
    if (!_nam && _account->networkAccessManager()) {
        // The proxy set for this account also applies to the flow's own plain network manager.
        _flow->setProxy(_account->networkAccessManager()->proxy());
    }
    if (revokeTimeoutMs() >= 0) {
        _flow->setRevokeTimeout(std::chrono::milliseconds(revokeTimeoutMs()));
    }
    if (flowPollMs() >= 0) {
        _flow->setPollInterval(std::chrono::milliseconds(flowPollMs()));
    }
    if (flowTimeoutMs() >= 0) {
        _flow->setTimeout(std::chrono::milliseconds(flowTimeoutMs()));
    }
    connect(_flow.data(), &DeviceAppPasswordFlow::loginUrlReady, this, [this](const QUrl &url) {
        _loginUrl = url;
        openInBrowser(url);
    });
    connect(_flow.data(), &DeviceAppPasswordFlow::granted, this, &MacSetupDialog::onGranted, Qt::DirectConnection);
    connect(_flow.data(), &DeviceAppPasswordFlow::failed, this, [this](DeviceAppPasswordFlow::Failure, const QString &text) { onFlowFailed(text); });
    _stack->setCurrentIndex(static_cast<int>(Step::Waiting));
    _flow->start();
}

void MacSetupDialog::onFlowFailed(const QString &text)
{
    showFailure(text);
}

void MacSetupDialog::onGranted(const QString &loginName, const QByteArray &appPassword)
{
    // The flow zeroes its buffer when this slot returns: keep a copy of our own, in memory only.
    _loginName = loginName;
    _password = QByteArray(appPassword.constData(), appPassword.size());

    // The browser may be signed in as somebody else (a second account, a shared Mac): the profile would pair that
    // person's password with this account's principal. Check the identity before anything is built.
    auto expected = _account->userFromCredentials();
    if (expected.isEmpty()) {
        expected = _account->davUser();
    }
    const auto sameAs = [&loginName](const QString &name) { return !name.isEmpty() && loginName.compare(name, Qt::CaseInsensitive) == 0; };
    if (!sameAs(_account->userFromCredentials()) && !sameAs(_account->davUser())) {
        showFailure(wrongPersonText(loginName, expected));
        onDidNotWork();
        return;
    }

    AppleProfileInput input;
    input.server = _account->url();
    input.davUser = _account->davUser();
    input.loginName = loginName;
    input.appPassword = _password;
    input.accountId = _account->id();
    input.bundleId = QStringLiteral(APPLICATION_REV_DOMAIN);
    QByteArray profile = AppleProfileBuilder::build(input);
    if (profile.isEmpty()) {
        showFailure(trMacSetup("NoDrive could not prepare the profile."));
        onDidNotWork();
        return;
    }
    const auto installed = _installer->installProfile(profile);
    DeviceAppPasswordFlow::wipe(profile);
    if (!installed) {
        const auto text = _installer->errorText();
        showFailure(text);
        // Nothing was installed: the new password is not needed any more.
        onDidNotWork();
        return;
    }
    DeviceSyncSettings::setProfileOffered(_account->id(), true);
    _installLabel->setText(installText(DeviceSyncService::currentMacMajorVersion()));
    _stack->setCurrentIndex(static_cast<int>(Step::Install));
}

void MacSetupDialog::forgetPassword()
{
    DeviceAppPasswordFlow::wipe(_password);
    _loginName.clear();
}

void MacSetupDialog::onInstalled()
{
    // The Mac keeps the password in its profile; NoDrive forgets it.
    forgetPassword();
    _installer->cleanup();
    accept();
}

void MacSetupDialog::onDidNotWork()
{
    // Whatever was offered is not installed: removing the account later must not talk about a profile.
    DeviceSyncSettings::setProfileOffered(_account->id(), false);
    if (_password.isEmpty() || !_flow) {
        forgetPassword();
        _installer->cleanup();
        return;
    }
    // The new password revokes itself. The flow (and its network manager) outlive this dialog until the DELETE has an
    // answer; the dialog stays open meanwhile, so the answer can always be shown.
    _revoking = true;
    _failureBeforeRevoke = step() == Step::Failed ? _failureLabel->text() : QString();
    _didNotWorkButton->setEnabled(false);
    _installedButton->setEnabled(false);
    _closeButton->setEnabled(false);
    _installer->cleanup();
    connect(_flow.data(), &DeviceAppPasswordFlow::revoked, this, &MacSetupDialog::onRevoked);
    _flow->deleteAfterRevoke();
    _flow->revoke(_loginName, QByteArray(_password.constData(), _password.size()));
    forgetPassword();
}

void MacSetupDialog::onRevoked(bool ok)
{
    _revoking = false;
    _closeButton->setEnabled(true);
    if (ok) {
        if (step() == Step::Install) {
            reject();
        }
        return;
    }
    // The password may still be valid on the server: tell the person where to remove it.
    const auto note = revokeFailedText(_computerName);
    showFailure(_failureBeforeRevoke.isEmpty() ? note : _failureBeforeRevoke + QStringLiteral("\n\n") + note);
    _openSecurityButton->show();
}

void MacSetupDialog::finishUp()
{
    if (_flow && !_revoking) {
        _flow->cancel();
    }
    forgetPassword();
    if (_installer) {
        _installer->cleanup();
    }
}

void MacSetupDialog::done(int result)
{
    // The window cannot be closed while the revoke waits for its answer (at most 10 s): the answer is always shown.
    if (_revoking) {
        return;
    }
    // Closing the window on step 3 counts as "Installed"; on step 2 as "Cancel". Either way nothing is kept.
    finishUp();
    QDialog::done(result);
}

} // namespace OCC::Nourax::DeviceSync
