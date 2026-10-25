/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * NoDrive branding checks: names, fixed server, no Nextcloud links,
 * version and a user agent that Nextcloud server 35 still recognises.
 */

#include "common/utility.h"
#include "config.h"
#include "configfile.h"
#include "gui/wizard/accountwizardcontroller.h"
#include "settings/managedsettings.h"
#include "theme.h"
#include "version.h"

#include <QRegularExpression>
#include <QStandardPaths>
#include <QTest>

using namespace OCC;

class TestNoDriveBranding : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    void init()
    {
        ConfigFile::setDeviceSourcesFactory([] {
            return std::vector<std::unique_ptr<SettingSource>>{};
        });
    }

    void cleanup()
    {
        ConfigFile::setDeviceSourcesFactory({});
    }

    void testNames()
    {
        const auto theme = Theme::instance();
        QCOMPARE(theme->appNameGUI(), QStringLiteral("NoDrive"));
        QCOMPARE(theme->appName(), QStringLiteral("NoDrive"));
        QVERIFY(theme->isBranded());
        QCOMPARE(QString::fromLatin1(APPLICATION_VENDOR), QStringLiteral("Nourax"));
        QCOMPARE(QString::fromLatin1(APPLICATION_EXECUTABLE), QStringLiteral("nodrive"));
        QCOMPARE(QString::fromLatin1(APPLICATION_REV_DOMAIN), QStringLiteral("org.nourax.nodrive.desktop"));
    }

    void testFixedServer()
    {
        const auto theme = Theme::instance();
        QCOMPARE(theme->overrideServerUrl(), QStringLiteral("https://drive.nourax.org"));
        QVERIFY(theme->forceOverrideServerUrl());
        QVERIFY(!theme->multipleOverrideServers());

        AccountWizardController controller;
        QVERIFY(!controller.serverUrlEditable());
        QCOMPARE(controller.serverUrl(), QStringLiteral("https://drive.nourax.org"));
    }

    void testNoNextcloudLinks()
    {
        const auto theme = Theme::instance();
        QVERIFY(theme->wizardUrlHint().isEmpty());
        QVERIFY2(!theme->helpUrl().isEmpty(), "an empty help URL falls back to Nextcloud's documentation");
        QVERIFY2(!theme->helpUrl().contains(QStringLiteral("nextcloud"), Qt::CaseInsensitive), qPrintable(theme->helpUrl()));
        QVERIFY2(!theme->updateCheckUrl().contains(QStringLiteral("nextcloud"), Qt::CaseInsensitive), qPrintable(theme->updateCheckUrl()));
        QVERIFY2(!theme->aboutDetails().contains(QStringLiteral("github.com/nextcloud"), Qt::CaseInsensitive), qPrintable(theme->aboutDetails()));
    }

    void testVersion()
    {
        QCOMPARE(QString::fromLatin1(MIRALL_VERSION_SUFFIX), QStringLiteral("-nourax.1"));
        QVERIFY2(QStringLiteral(MIRALL_VERSION_STRING).startsWith(QStringLiteral("35.0.0-nourax.1")), MIRALL_VERSION_STRING);
    }

    void testUserAgentMatchesServerRule()
    {
        // Copied from nextcloud/server v35.0.2, lib/public/IRequest.php,
        // USER_AGENT_CLIENT_DESKTOP. The server uses it to recognise desktop
        // clients (and BlockLegacyClientPlugin reads the captured version).
        static const QRegularExpression serverRule(
            QStringLiteral(R"(^Mozilla\/5\.0 \([A-Za-z ]+\) (?:mirall|csyncoC)\/([^ ]*).*$)"));
        const auto userAgent = QString::fromLatin1(Utility::userAgentString());
        const auto match = serverRule.match(userAgent);
        QVERIFY2(match.hasMatch(), qPrintable(userAgent));
        QCOMPARE(match.captured(1), QStringLiteral("35.0.0-nourax.1"));
        QVERIFY2(userAgent.contains(QStringLiteral("NoDrive")) || userAgent.contains(QCoreApplication::applicationName()), qPrintable(userAgent));
    }
};

QTEST_MAIN(TestNoDriveBranding)
#include "testnodrivebranding.moc"
