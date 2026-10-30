/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * NoDrive device calendar and contacts sync: the panel on the account page (macOS and the other variant, by a test
 * switch), the Mac setup steps, the Thunderbird guide and the removal note. plans/noagenda-desktop-plan.md, 4.5 and 4.6.
 * Screenshots in light and dark mode go to $NODRIVE_SCREENSHOT_DIR/devicesync/ (default: <bin>/nodrive-screenshots).
 * The clipboard after each copy button must hold exactly the address.
 */

#include "accountsettings.h"
#include "configfile.h"
#include "foldermantestutils.h"
#include "gui/nourax/devicesync/devicesyncpanel.h"
#include "gui/nourax/devicesync/devicesyncservice.h"
#include "gui/nourax/devicesync/devicesyncsettings.h"
#include "gui/nourax/devicesync/deviceapppasswordflow.h"
#include "gui/nourax/devicesync/macprofileinstaller.h"
#include "gui/nourax/devicesync/macsetupdialog.h"
#include "gui/nourax/devicesync/thunderbirdguidedialog.h"
#include "gui/nourax/devicesync/urlopener.h"
#include "syncenginetestutils.h"
#include "testhelper.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QDateTime>
#include <QDirIterator>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLoggingCategory>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QStyleHints>
#include <QTemporaryDir>
#include <QTest>
#include <QVBoxLayout>

using namespace OCC;
using namespace OCC::Nourax::DeviceSync;

namespace {

const QByteArray plantedPassword = QByteArrayLiteral("PLANTED-fake-app-pw-77c1");
const QString serverUrl = QStringLiteral("https://drive.example.test");

QStringList &captured()
{
    static QStringList messages;
    return messages;
}

void captureMessage(QtMsgType, const QMessageLogContext &, const QString &message)
{
    captured() << message;
}

QString screenshotDir()
{
    const auto dir = qEnvironmentVariable("NODRIVE_SCREENSHOT_DIR", QCoreApplication::applicationDirPath() + QStringLiteral("/nodrive-screenshots"))
        + QStringLiteral("/devicesync");
    QDir().mkpath(dir);
    return dir;
}

struct Script
{
    QStringList verbs;
    QList<QUrl> urls;
    QList<QByteArray> authorizations;
    int pendingPolls = 1;
    bool neverGrant = false;
    int initStatus = 200;
    QString grantServer = serverUrl;
    int polls = 0;
};

FakeQNAM *makeNam(Script &script)
{
    auto *nam = new FakeQNAM({});
    nam->setOverride([&script, nam](QNetworkAccessManager::Operation op, const QNetworkRequest &request, QIODevice *) -> QNetworkReply * {
        const auto verb = op == QNetworkAccessManager::CustomOperation ? request.attribute(QNetworkRequest::CustomVerbAttribute).toString() : QStringLiteral("POST");
        script.verbs << verb;
        script.urls << request.url();
        script.authorizations << request.rawHeader("Authorization");
        const auto path = request.url().path();
        if (path.endsWith(QLatin1String("/index.php/login/v2"))) {
            if (script.initStatus != 200) {
                return new FakeErrorReply(op, request, nam, script.initStatus);
            }
            QJsonObject poll{{"token", "tok-ui"}, {"endpoint", QString(serverUrl + QStringLiteral("/login/v2/poll"))}};
            QJsonObject root{{"poll", poll}, {"login", QString(serverUrl + QStringLiteral("/login/v2/flow/tok-ui"))}};
            return new FakePayloadReply(op, request, QJsonDocument(root).toJson(QJsonDocument::Compact), nam);
        }
        if (path.endsWith(QLatin1String("/login/v2/poll"))) {
            ++script.polls;
            if (script.neverGrant || script.polls <= script.pendingPolls) {
                return new FakeErrorReply(op, request, nam, 404);
            }
            QJsonObject root{{"server", script.grantServer}, {"loginName", "ali"}, {"appPassword", QString::fromUtf8(plantedPassword)}};
            return new FakePayloadReply(op, request, QJsonDocument(root).toJson(QJsonDocument::Compact), nam);
        }
        if (path.endsWith(QLatin1String("/ocs/v2.php/core/apppassword"))) {
            return new FakePayloadReply(op, request, QByteArrayLiteral("{}"), nam);
        }
        return new FakeErrorReply(op, request, nam, 500);
    });
    return nam;
}

AccountPtr makeAccount(const QString &url = serverUrl)
{
    auto account = Account::create();
    account->setUrl(QUrl(url));
    account->setDavUser(QStringLiteral("ali"));
    account->setProperty("id", QString(QStringLiteral("ali@") + QUrl(url).host()));
    account->setCredentials(new FakeCredentials{new FakeQNAM({})});
    return account;
}

} // namespace

class TestNoDriveDeviceSyncUi : public QObject
{
    Q_OBJECT

    QTemporaryDir _configDir;
    QPalette _originalPalette;
    QtMessageHandler _previousHandler = nullptr;
    FolderManTestHelper _helper;
    QDateTime _started = QDateTime::currentDateTime().addSecs(-2);

    void applyColorScheme(bool dark)
    {
        QApplication::setPalette(_originalPalette);
        QGuiApplication::styleHints()->setColorScheme(dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
        QTest::qWait(100);
        auto palette = QApplication::palette();
        const QColor background(dark ? "#202020" : "#ffffff");
        const QColor foreground(dark ? Qt::white : Qt::black);
        palette.setColor(QPalette::Window, background);
        palette.setColor(QPalette::WindowText, foreground);
        palette.setColor(QPalette::Base, dark ? QColor("#2b2b2b") : QColor(Qt::white));
        palette.setColor(QPalette::Text, foreground);
        palette.setColor(QPalette::Button, dark ? QColor("#3a3a3a") : QColor("#efefef"));
        palette.setColor(QPalette::ButtonText, foreground);
        palette.setColor(QPalette::Light, background);
        palette.setColor(QPalette::AlternateBase, dark ? QColor("#2b2b2b") : QColor("#f3f3f3"));
        QApplication::setPalette(palette);
    }

    bool saveShot(QWidget *widget, const QString &name)
    {
        if (qobject_cast<QMessageBox *>(widget) && !widget->isVisible()) {
#ifndef Q_OS_MACOS
            QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
            widget->setAttribute(Qt::WA_DontShowOnScreen);
            widget->show();
            QCoreApplication::processEvents();
#endif
        }
        widget->resize(widget->sizeHint().expandedTo(widget->size()));
        QCoreApplication::processEvents();
        const auto shot = widget->grab();
        return !shot.isNull() && shot.save(screenshotDir() + QLatin1Char('/') + name + QStringLiteral(".png"));
    }

    // One picture in light and one in dark mode.
    template <typename Make>
    void bothModes(const QString &name, Make make)
    {
        for (const auto dark : {false, true}) {
            applyColorScheme(dark);
            std::unique_ptr<QWidget> widget(make());
            QVERIFY2(saveShot(widget.get(), name + (dark ? QStringLiteral("-dark") : QStringLiteral("-light"))), qPrintable(name));
        }
    }

private Q_SLOTS:
    void initTestCase()
    {
        Q_INIT_RESOURCE(resources);
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY(_configDir.isValid());
        ConfigFile::setConfDir(_configDir.path());
        _originalPalette = QApplication::palette();
        MacSetupDialog::setFlowTimingForTests(20, -1);
        QLoggingCategory::setFilterRules(QStringLiteral("*.debug=true\n*.info=true"));
        _previousHandler = qInstallMessageHandler(captureMessage);
    }

    void cleanupTestCase()
    {
        qInstallMessageHandler(_previousHandler);
    }

    void cleanup()
    {
        DeviceSyncService::setMacForTests(-1);
        DeviceSyncService::setMacMajorVersionForTests(-1);
        urlOpenerHook() = {};
        MacProfileInstaller::setOpenerForTests({});
        QGuiApplication::styleHints()->unsetColorScheme();
        QApplication::setPalette(_originalPalette);
    }

    // ---- the panel on the account page -------------------------------------------------------------------------

    void macPanelSitsBeforeTheAccountActionsAndHasTheTexts()
    {
        DeviceSyncService::setMacForTests(1);
        auto account = makeAccount();
        FakeAccountState state(account);
        AccountSettings settings(&state);
        auto *panel = settings.findChild<DeviceSyncPanel *>(QStringLiteral("deviceSyncPanel"));
        QVERIFY(panel);
        auto *layout = settings.findChild<QVBoxLayout *>(QStringLiteral("verticalLayout_2"));
        auto *actions = settings.findChild<QWidget *>(QStringLiteral("accountActionsPanel"));
        QVERIFY(layout);
        QVERIFY(actions);
        QCOMPARE(layout->indexOf(panel) + 1, layout->indexOf(actions));

        QCOMPARE(panel->titleLabel()->text(), QStringLiteral("Calendar and contacts"));
        QCOMPARE(panel->descriptionLabel()->text(),
            QStringLiteral("Use your NoDrive calendars, reminders and contacts in the Calendar, Reminders and Contacts apps on this Mac."));
        QVERIFY(panel->macButton());
        QCOMPARE(panel->macButton()->text(), QStringLiteral("Add to my Mac's Calendar & Contacts"));
        QCOMPARE(panel->thunderbirdButton()->text(), QStringLiteral("Use with Thunderbird…"));
        QVERIFY(panel->thunderbirdButton()->isFlat()); // the link
        QVERIFY(panel->macButton()->isEnabled());
        QVERIFY(panel->thunderbirdButton()->isEnabled());
        QVERIFY(panel->hintLabel()->isHidden());
        QVERIFY(!panel->macButton()->text().contains(QStringLiteral("nextcloud"), Qt::CaseInsensitive));
    }

    void otherPanelHasOnlyTheThunderbirdButton()
    {
        DeviceSyncService::setMacForTests(0);
        auto account = makeAccount();
        FakeAccountState state(account);
        AccountSettings settings(&state);
        auto *panel = settings.findChild<DeviceSyncPanel *>(QStringLiteral("deviceSyncPanel"));
        QVERIFY(panel);
        QCOMPARE(panel->titleLabel()->text(), QStringLiteral("Calendar and contacts"));
        QCOMPARE(panel->descriptionLabel()->text(), QStringLiteral("Use your NoDrive calendars and contacts in Thunderbird."));
        QVERIFY(!panel->macButton());
        QCOMPARE(panel->thunderbirdButton()->text(), QStringLiteral("Use with Thunderbird…"));
        QVERIFY(!panel->thunderbirdButton()->isFlat());
    }

    void theButtonsAreOffWhileTheAccountIsNotConnected()
    {
        for (const auto mac : {1, 0}) {
            DeviceSyncService::setMacForTests(mac);
            auto account = makeAccount();
            FakeAccountState state(account);
            state.setStateForTesting(AccountState::SignedOut);
            AccountSettings settings(&state);
            auto *panel = settings.findChild<DeviceSyncPanel *>(QStringLiteral("deviceSyncPanel"));
            QVERIFY(panel);
            QVERIFY(!panel->hintLabel()->isHidden());
            QCOMPARE(panel->hintLabel()->text(), QStringLiteral("Connect to NoDrive first."));
            QVERIFY(!panel->thunderbirdButton()->isEnabled());
            if (panel->macButton()) {
                QVERIFY(!panel->macButton()->isEnabled());
            }
            state.setStateForTesting(AccountState::Connected);
            QVERIFY(panel->hintLabel()->isHidden());
            QVERIFY(panel->thunderbirdButton()->isEnabled());
            if (panel->macButton()) {
                QVERIFY(panel->macButton()->isEnabled());
            }
        }
    }

    void panelScreenshots()
    {
        for (const auto mac : {1, 0}) {
            DeviceSyncService::setMacForTests(mac);
            bothModes(mac ? QStringLiteral("account-panel-mac") : QStringLiteral("account-panel-other"), [] {
                auto account = makeAccount();
                auto *state = new FakeAccountState(account);
                auto *holder = new QWidget;
                holder->setAutoFillBackground(true);
                auto *layout = new QVBoxLayout(holder);
                layout->setContentsMargins(16, 16, 16, 16);
                auto *panel = new DeviceSyncPanel(state, holder);
                layout->addWidget(panel);
                holder->setMinimumWidth(640);
                QObject::connect(holder, &QObject::destroyed, state, &QObject::deleteLater);
                return holder;
            });
        }
    }

    void panelOpensTheGuideAndTheMacSetup()
    {
        DeviceSyncService::setMacForTests(1);
        auto account = makeAccount();
        FakeAccountState state(account);
        QWidget host;
        DeviceSyncPanel panel(&state, &host);
        panel.thunderbirdButton()->click();
        QTRY_VERIFY(host.findChild<ThunderbirdGuideDialog *>());
        panel.macButton()->click();
        QTRY_VERIFY(host.findChild<MacSetupDialog *>());
        qDeleteAll(host.findChildren<QDialog *>());
    }

    // ---- the Thunderbird guide ----------------------------------------------------------------------------------

    void guideTextsAndTheClipboardHoldsExactlyTheAddress()
    {
        ThunderbirdGuideDialog dialog(QUrl(serverUrl), QStringLiteral("ali"), QStringLiteral("ali"));
        QCOMPARE(dialog.windowTitle(), QStringLiteral("Use NoDrive calendars and contacts in Thunderbird"));
        QCOMPARE(dialog.findChild<QLabel *>(QStringLiteral("thunderbirdTitle"))->text(), QStringLiteral("Use NoDrive calendars and contacts in Thunderbird"));
        QCOMPARE(dialog.findChild<QLabel *>(QStringLiteral("thunderbirdStep1"))->text(),
            QStringLiteral("1. Create an app password for Thunderbird: click Open Security settings, then Create new app password, and name it Thunderbird."));
        QCOMPARE(dialog.findChild<QLabel *>(QStringLiteral("thunderbirdStep2"))->text(),
            QStringLiteral("2. In Thunderbird's Calendar, click New Calendar… › On the Network. Enter your user name (ali) and paste the calendar address as the location. "
                           "Click Find Calendars, sign in with the app password and choose your calendars."));
        QCOMPARE(dialog.findChild<QLabel *>(QStringLiteral("thunderbirdStep3"))->text(),
            QStringLiteral("3. In the Address Book, open the menu next to New Address Book › Add CardDAV Address Book, paste the contacts address and sign in with the app password."));
        QCOMPARE(dialog.findChild<QLabel *>(QStringLiteral("thunderbirdFooter"))->text(), QStringLiteral("NoDrive never copies passwords."));

        auto *security = dialog.findChild<QPushButton *>(QStringLiteral("openSecuritySettingsButton"));
        auto *calendar = dialog.findChild<QPushButton *>(QStringLiteral("copyCalendarAddressButton"));
        auto *contacts = dialog.findChild<QPushButton *>(QStringLiteral("copyContactsAddressButton"));
        QVERIFY(security && calendar && contacts);
        QCOMPARE(security->text(), QStringLiteral("Open Security settings"));
        QCOMPARE(calendar->text(), QStringLiteral("Copy calendar address"));
        QCOMPARE(contacts->text(), QStringLiteral("Copy contacts address"));

        // Open Security settings: the account's own server, and the clipboard is not touched.
        QUrl opened;
        urlOpenerHook() = [&](const QUrl &url) {
            opened = url;
            return true;
        };
        QGuiApplication::clipboard()->setText(QStringLiteral("before"));
        security->click();
        QCOMPARE(opened, QUrl(QStringLiteral("https://drive.example.test/index.php/settings/user/security")));
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("before"));

        auto *copied1 = dialog.findChild<QLabel *>(QStringLiteral("thunderbirdCopied2"));
        auto *copied2 = dialog.findChild<QLabel *>(QStringLiteral("thunderbirdCopied3"));
        QVERIFY(copied1->text().isEmpty());
        calendar->click();
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("https://drive.example.test/remote.php/dav/calendars/ali/"));
        QCOMPARE(copied1->text(), QStringLiteral("Copied"));
        QVERIFY(copied2->text().isEmpty());
        contacts->click();
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("https://drive.example.test/remote.php/dav/addressbooks/users/ali/"));
        QCOMPARE(copied2->text(), QStringLiteral("Copied"));
        QVERIFY(copied1->text().isEmpty());

        // Exactly an address: no credentials, no user secret, nothing but the URL.
        const auto clip = QGuiApplication::clipboard()->text();
        QVERIFY(QUrl(clip).isValid());
        QVERIFY(!clip.contains(QLatin1Char('@')));
        QVERIFY(!clip.contains(QLatin1Char(' ')));
        QVERIFY(!clip.contains(QStringLiteral("password"), Qt::CaseInsensitive));
        QVERIFY(!clip.toLower().contains(QStringLiteral("nextcloud")));
        QCOMPARE(dialog.calendarAddress(), QStringLiteral("https://drive.example.test/remote.php/dav/calendars/ali/"));
    }

    void guideScreenshots()
    {
        bothModes(QStringLiteral("thunderbird-guide"), [] {
            auto *dialog = new ThunderbirdGuideDialog(QUrl(serverUrl), QStringLiteral("ali"), QStringLiteral("ali"));
            dialog->setAttribute(Qt::WA_DontShowOnScreen);
            dialog->show();
            return dialog;
        });
    }

    // ---- the Mac setup ------------------------------------------------------------------------------------------

    void macStepTexts()
    {
        QCOMPARE(MacSetupDialog::introText(),
            QStringLiteral("NoDrive will open your browser. Allow access there to create a separate password for this Mac's Calendar and Contacts. "
                           "You won't see it, and NoDrive doesn't keep it."));
        QCOMPARE(MacSetupDialog::waitingText(), QStringLiteral("Waiting for you to allow access in the browser…"));
        QCOMPARE(MacSetupDialog::installText(15),
            QStringLiteral("Now install the profile: open System Settings › General › Device Management, double-click \"NoDrive Calendar & Contacts\" in Downloaded, then click Install."));
        QCOMPARE(MacSetupDialog::installText(14),
            QStringLiteral("Now install the profile: open System Settings › Privacy & Security › Profiles, double-click \"NoDrive Calendar & Contacts\" in Downloaded, then click Install."));
        QVERIFY(!MacSetupDialog::installText(13).toLower().contains(QStringLiteral("nextcloud")));
    }

    void macFlowEndToEndInstalled()
    {
        captured().clear();
        Script script;
        script.pendingPolls = 1;
        std::unique_ptr<FakeQNAM> nam(makeNam(script));
        QUrl browser;
        urlOpenerHook() = [&](const QUrl &url) {
            browser = url;
            return true;
        };
        QUrl profileUrl;
        QByteArray profileText;
        MacProfileInstaller::setOpenerForTests([&](const QUrl &url) {
            profileUrl = url;
            QFile file(url.toLocalFile());
            if (file.open(QIODevice::ReadOnly)) {
                profileText = file.readAll();
            }
            return true;
        });

        auto account = makeAccount();
        MacSetupDialog dialog(account, nullptr, nam.get());
        dialog.setComputerName(QStringLiteral("test-mac"));
        QCOMPARE(dialog.step(), MacSetupDialog::Step::Intro);
        DeviceSyncService::setMacMajorVersionForTests(15);

        dialog.continueButton()->click();
        QCOMPARE(dialog.step(), MacSetupDialog::Step::Waiting);
        QTRY_COMPARE_WITH_TIMEOUT(browser, QUrl(QStringLiteral("https://drive.example.test/login/v2/flow/tok-ui")), 10000);
        // "Open the browser again" opens the same address.
        browser.clear();
        dialog.reopenButton()->click();
        QCOMPARE(browser, QUrl(QStringLiteral("https://drive.example.test/login/v2/flow/tok-ui")));

        QTRY_COMPARE_WITH_TIMEOUT(dialog.step(), MacSetupDialog::Step::Install, 20000);
        QVERIFY(profileUrl.isLocalFile());
        QVERIFY(QFileInfo::exists(profileUrl.toLocalFile()));
        QVERIFY(profileText.contains("CalDAVPassword"));
        QVERIFY(profileText.contains(plantedPassword));
        QVERIFY(DeviceSyncSettings::profileOffered(account->id()));
        QVERIFY(dialog.installLabel()->text().contains(QStringLiteral("General › Device Management")));

        dialog.installedButton()->click();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QVERIFY(!QFileInfo::exists(profileUrl.toLocalFile())); // the file is gone
        QVERIFY(!QFileInfo::exists(QFileInfo(profileUrl.toLocalFile()).absolutePath()));
        // "Installed" does not revoke anything.
        QVERIFY(!script.verbs.contains(QStringLiteral("DELETE")));

        // The password is nowhere: not in the log output, not in the settings, not in any file of the config or temp folder.
        for (const auto &line : std::as_const(captured())) {
            QVERIFY2(!line.contains(QString::fromUtf8(plantedPassword)), "the planted password is in the log output");
        }
        QSettings ini(ConfigFile().configFile(), QSettings::IniFormat);
        for (const auto &key : ini.allKeys()) {
            QVERIFY2(!ini.value(key).toString().contains(QString::fromUtf8(plantedPassword)), qPrintable(key));
        }
        // Only files written during this test run are looked at (the temp folder can be big).
        for (const auto &root : {_configDir.path(), QDir::tempPath()}) {
            QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const auto path = it.next();
                QFile file(path);
                if (QFileInfo(path).lastModified() >= _started && QFileInfo(path).size() < 5 * 1024 * 1024 && file.open(QIODevice::ReadOnly)) {
                    QVERIFY2(!file.readAll().contains(plantedPassword), qPrintable(path));
                }
            }
        }
        DeviceSyncSettings::setProfileOffered(account->id(), false);
    }

    void macFlowItDidntWorkRevokesAndRemovesTheFile()
    {
        Script script;
        script.pendingPolls = 0;
        std::unique_ptr<FakeQNAM> nam(makeNam(script));
        urlOpenerHook() = [](const QUrl &) { return true; };
        QString profilePath;
        MacProfileInstaller::setOpenerForTests([&](const QUrl &url) {
            profilePath = url.toLocalFile();
            return true;
        });
        auto account = makeAccount();
        MacSetupDialog dialog(account, nullptr, nam.get());
        dialog.setComputerName(QStringLiteral("test-mac"));
        dialog.continueButton()->click();
        QTRY_COMPARE_WITH_TIMEOUT(dialog.step(), MacSetupDialog::Step::Install, 20000);
        QVERIFY(QFileInfo::exists(profilePath));

        dialog.didNotWorkButton()->click();
        QTRY_COMPARE_WITH_TIMEOUT(dialog.result(), int(QDialog::Rejected), 20000);
        QVERIFY(script.verbs.contains(QStringLiteral("DELETE")));
        const auto index = script.verbs.indexOf(QStringLiteral("DELETE"));
        QCOMPARE(script.urls.at(index), QUrl(QStringLiteral("https://drive.example.test/ocs/v2.php/core/apppassword")));
        QCOMPARE(script.authorizations.at(index), "Basic " + QByteArray("ali:" + plantedPassword).toBase64());
        QVERIFY(!QFileInfo::exists(profilePath));
        DeviceSyncSettings::setProfileOffered(account->id(), false);
    }

    void macFlowFailuresShowTheTexts()
    {
        urlOpenerHook() = [](const QUrl &) { return true; };
        MacProfileInstaller::setOpenerForTests([](const QUrl &) { return true; });
        {
            // The server answers for another address: dropped with an error, no profile.
            Script script;
            script.pendingPolls = 0;
            script.grantServer = QStringLiteral("https://other.example.test");
            std::unique_ptr<FakeQNAM> nam(makeNam(script));
            auto account = makeAccount();
            MacSetupDialog dialog(account, nullptr, nam.get());
            dialog.continueButton()->click();
            QTRY_COMPARE_WITH_TIMEOUT(dialog.step(), MacSetupDialog::Step::Failed, 20000);
            QVERIFY(!dialog.errorText().isEmpty());
            QVERIFY(!DeviceSyncSettings::profileOffered(account->id()));
        }
        {
            // The server does not start the flow.
            Script script;
            script.initStatus = 403;
            std::unique_ptr<FakeQNAM> nam(makeNam(script));
            auto account = makeAccount();
            MacSetupDialog dialog(account, nullptr, nam.get());
            dialog.continueButton()->click();
            QTRY_COMPARE_WITH_TIMEOUT(dialog.step(), MacSetupDialog::Step::Failed, 20000);
            QCOMPARE(dialog.errorText(), QStringLiteral("The server didn't create a password. Ask your administrator."));
        }
        {
            // Nobody allows access in time.
            Script script;
            script.neverGrant = true;
            std::unique_ptr<FakeQNAM> nam(makeNam(script));
            auto account = makeAccount();
            MacSetupDialog dialog(account, nullptr, nam.get());
            QCOMPARE(dialog.step(), MacSetupDialog::Step::Intro);
            MacSetupDialog::setFlowTimingForTests(20, 300);
            dialog.continueButton()->click();
            MacSetupDialog::setFlowTimingForTests(20, -1);
            QTRY_COMPARE_WITH_TIMEOUT(dialog.step(), MacSetupDialog::Step::Failed, 20000);
            QCOMPARE(dialog.errorText(), QStringLiteral("The sign-in took too long. Try again."));
        }
        {
            // Plain http: refused before anything is sent.
            Script script;
            std::unique_ptr<FakeQNAM> nam(makeNam(script));
            auto account = makeAccount(QStringLiteral("http://drive.example.test"));
            MacSetupDialog dialog(account, nullptr, nam.get());
            dialog.continueButton()->click();
            QCOMPARE(dialog.step(), MacSetupDialog::Step::Failed);
            QCOMPARE(dialog.errorText(), QStringLiteral("A secure connection (https) is needed to add the calendars and contacts to this Mac."));
            QVERIFY(script.verbs.isEmpty());
        }
    }

    void cancellingLeavesNothingBehind()
    {
        urlOpenerHook() = [](const QUrl &) { return true; };
        Script script;
        script.neverGrant = true;
        std::unique_ptr<FakeQNAM> nam(makeNam(script));
        auto account = makeAccount();
        MacSetupDialog dialog(account, nullptr, nam.get());
        dialog.cancelButton()->click();
        QCOMPARE(dialog.result(), int(QDialog::Rejected));
        QVERIFY(script.verbs.isEmpty());

        MacSetupDialog second(account, nullptr, nam.get());
        second.continueButton()->click();
        QTRY_VERIFY_WITH_TIMEOUT(script.polls >= 1, 10000);
        QCOMPARE(second.step(), MacSetupDialog::Step::Waiting);
        QVERIFY(second.reopenButton());
        second.reject();
        const auto polls = script.polls;
        QTest::qWait(150);
        QCOMPARE(script.polls, polls);
        QVERIFY(!DeviceSyncSettings::profileOffered(account->id()));
    }

    void macScreenshots()
    {
        DeviceSyncService::setMacMajorVersionForTests(15);
        bothModes(QStringLiteral("mac-step1"), [] {
            auto account = makeAccount();
            auto *dialog = new MacSetupDialog(account);
            dialog->setAttribute(Qt::WA_DontShowOnScreen);
            dialog->show();
            return dialog;
        });
        bothModes(QStringLiteral("mac-step3"), [this] {
            static Script script; // the manager lives as long as the dialog
            script = Script();
            script.pendingPolls = 0;
            std::unique_ptr<FakeQNAM> nam(makeNam(script));
            urlOpenerHook() = [](const QUrl &) { return true; };
            MacProfileInstaller::setOpenerForTests([](const QUrl &) { return true; });
            auto account = makeAccount();
            auto *dialog = new MacSetupDialog(account, nullptr, nam.get());
            dialog->continueButton()->click();
            QTest::qWait(300);
            for (int i = 0; i < 100 && dialog->step() != MacSetupDialog::Step::Install; ++i) {
                QTest::qWait(50);
            }
            dialog->setAttribute(Qt::WA_DontShowOnScreen);
            dialog->show();
            // The dialog keeps the manager alive until it is deleted: hand it over.
            nam.release()->setParent(dialog);
            DeviceSyncSettings::setProfileOffered(account->id(), false);
            return dialog;
        });
    }

    // ---- the removal note --------------------------------------------------------------------------------------

    void removalNoteScreenshots()
    {
        DeviceSyncService::setMacMajorVersionForTests(15);
        bothModes(QStringLiteral("removal-note"), [] { return DeviceSyncService::createRemovalBox(DeviceSyncService::currentMacMajorVersion()); });
        std::unique_ptr<QMessageBox> box(DeviceSyncService::createRemovalBox(15));
        QCOMPARE(box->text(),
            QStringLiteral("To remove NoDrive calendars and contacts from this Mac, open System Settings › General › Device Management, "
                           "select NoDrive Calendar & Contacts and click Remove. You can also remove its password in NoDrive's Security settings on the web."));
    }
};

QTEST_MAIN(TestNoDriveDeviceSyncUi)
#include "testnodrivedevicesyncui.moc"
