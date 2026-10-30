/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * NoDrive device calendar and contacts sync: the Apple profile (all three systems), its private file, the settings and
 * the removal note. plans/noagenda-desktop-plan.md, 4.6. On macOS the file is also linted with plutil.
 */

#include "gui/nourax/devicesync/appleprofilebuilder.h"
#include "gui/nourax/devicesync/devicesyncservice.h"
#include "gui/nourax/devicesync/devicesyncsettings.h"
#include "gui/nourax/devicesync/macprofileinstaller.h"

#include "configfile.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
#include <QVariantMap>
#include <QXmlStreamReader>

using namespace OCC;
using namespace OCC::Nourax::DeviceSync;

namespace {

const QByteArray plantedPassword = QByteArrayLiteral("Pl4nted&<fake>\"app'pw");

// Reads the plist subset that the builder writes (dict, array, string, integer, true, false) into variants.
QVariant readValue(QXmlStreamReader &xml);

QVariantMap readDict(QXmlStreamReader &xml)
{
    QVariantMap map;
    QString key;
    while (xml.readNextStartElement()) {
        if (xml.name() == QLatin1String("key")) {
            key = xml.readElementText();
        } else {
            map.insert(key, readValue(xml));
        }
    }
    return map;
}

QVariant readValue(QXmlStreamReader &xml)
{
    const auto name = xml.name().toString();
    if (name == QLatin1String("dict")) {
        return readDict(xml);
    }
    if (name == QLatin1String("array")) {
        QVariantList list;
        while (xml.readNextStartElement()) {
            list << readValue(xml);
        }
        return list;
    }
    if (name == QLatin1String("string")) {
        return xml.readElementText();
    }
    if (name == QLatin1String("integer")) {
        return xml.readElementText().toInt();
    }
    if (name == QLatin1String("true") || name == QLatin1String("false")) {
        xml.skipCurrentElement();
        return name == QLatin1String("true");
    }
    xml.skipCurrentElement();
    return {};
}

// Returns the top-level dict. `ok` is false when the XML is not well formed or not a plist.
QVariantMap parseProfile(const QByteArray &data, bool *ok = nullptr)
{
    QXmlStreamReader xml(data);
    QVariantMap root;
    if (xml.readNextStartElement() && xml.name() == QLatin1String("plist") && xml.attributes().value(QLatin1String("version")) == QLatin1String("1.0")) {
        if (xml.readNextStartElement() && xml.name() == QLatin1String("dict")) {
            root = readDict(xml);
        }
    }
    if (ok) {
        *ok = !xml.hasError() && !root.isEmpty();
    }
    return root;
}

AppleProfileInput input()
{
    AppleProfileInput in;
    in.server = QUrl(QStringLiteral("https://drive.example.test"));
    in.davUser = QStringLiteral("ali");
    in.loginName = QStringLiteral("ali");
    in.appPassword = plantedPassword;
    in.accountId = QStringLiteral("ali@drive.example.test");
    in.bundleId = QStringLiteral("org.example.nodrive");
    return in;
}

} // namespace

class TestNoDriveDeviceSyncProfile : public QObject
{
    Q_OBJECT

    QTemporaryDir _configDir;

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY(_configDir.isValid());
        ConfigFile::setConfDir(_configDir.path());
    }

    void cleanup()
    {
        MacProfileInstaller::setOpenerForTests({});
        DeviceSyncService::setMessageHookForTests({});
        DeviceSyncService::setMacForTests(-1);
        DeviceSyncService::setMacMajorVersionForTests(-1);
    }

    void xmlIsValidAndHasExactlyTheAppleKeys()
    {
        const auto data = AppleProfileBuilder::build(input());
        QVERIFY(!data.isEmpty());
        QVERIFY(data.startsWith("<?xml version=\"1.0\""));
        QVERIFY(data.contains("<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\""));

        bool ok = false;
        const auto top = parseProfile(data, &ok);
        QVERIFY(ok);

        const QSet<QString> topKeys{"PayloadContent", "PayloadDescription", "PayloadDisplayName", "PayloadIdentifier", "PayloadOrganization",
            "PayloadRemovalDisallowed", "PayloadType", "PayloadUUID", "PayloadVersion"};
        QCOMPARE(QSet<QString>(top.keyBegin(), top.keyEnd()), topKeys);
        QCOMPARE(top.value("PayloadType").toString(), QStringLiteral("Configuration"));
        QCOMPARE(top.value("PayloadVersion").toInt(), 1);
        QCOMPARE(top.value("PayloadDisplayName").toString(), QStringLiteral("NoDrive Calendar & Contacts"));
        QCOMPARE(top.value("PayloadOrganization").toString(), QStringLiteral("Nourax"));
        QCOMPARE(top.value("PayloadDescription").toString(), QStringLiteral("Adds your NoDrive calendars and contacts to Calendar and Contacts on this Mac."));
        QCOMPARE(top.value("PayloadRemovalDisallowed").typeId(), QMetaType::Bool);
        QCOMPARE(top.value("PayloadRemovalDisallowed").toBool(), false);

        const auto content = top.value("PayloadContent").toList();
        QCOMPARE(content.size(), 2);
        for (const auto &prefix : {QStringLiteral("CalDAV"), QStringLiteral("CardDAV")}) {
            const auto it = std::find_if(content.cbegin(), content.cend(), [&](const QVariant &v) {
                return v.toMap().contains(prefix + QStringLiteral("HostName"));
            });
            QVERIFY2(it != content.cend(), qPrintable(prefix));
            const auto payload = it->toMap();
            QSet<QString> expected;
            for (const auto &k : AppleProfileBuilder::accountKeys(prefix)) {
                expected.insert(k);
            }
            expected.unite({"PayloadType", "PayloadVersion", "PayloadIdentifier", "PayloadUUID", "PayloadDisplayName"});
            QCOMPARE(QSet<QString>(payload.keyBegin(), payload.keyEnd()), expected);

            // The documented keys of Apple's CalDAV and CardDAV payloads, with their types.
            QCOMPARE(AppleProfileBuilder::accountKeys(prefix),
                (QStringList{prefix + "AccountDescription", prefix + "HostName", prefix + "Password", prefix + "Port",
                    prefix + "PrincipalURL", prefix + "UseSSL", prefix + "Username"}));
            QCOMPARE(payload.value(prefix + "AccountDescription").toString(), QStringLiteral("NoDrive"));
            QCOMPARE(payload.value(prefix + "HostName").toString(), QStringLiteral("drive.example.test"));
            QCOMPARE(payload.value(prefix + "Port").typeId(), QMetaType::Int);
            QCOMPARE(payload.value(prefix + "Port").toInt(), 443);
            QCOMPARE(payload.value(prefix + "UseSSL").typeId(), QMetaType::Bool);
            QCOMPARE(payload.value(prefix + "UseSSL").toBool(), true);
            QCOMPARE(payload.value(prefix + "Username").toString(), QStringLiteral("ali"));
            QCOMPARE(payload.value(prefix + "Password").toString().toUtf8(), plantedPassword); // XML escaping round trip
            QCOMPARE(payload.value(prefix + "PrincipalURL").toString(), QStringLiteral("https://drive.example.test/remote.php/dav/principals/users/ali/"));
            QCOMPARE(payload.value("PayloadType").toString(), QStringLiteral("com.apple.%1.account").arg(prefix.toLower()));
            QCOMPARE(payload.value("PayloadVersion").toInt(), 1);
            QVERIFY(payload.value("PayloadIdentifier").toString().startsWith(top.value("PayloadIdentifier").toString() + QLatin1Char('.')));
        }
        QCOMPARE(content.at(0).toMap().value("PayloadDisplayName").toString(), QStringLiteral("NoDrive Calendar"));
        QCOMPARE(content.at(1).toMap().value("PayloadDisplayName").toString(), QStringLiteral("NoDrive Contacts"));
    }

    void identifierIsStablePerAccountAndUuidsAreFresh()
    {
        const auto a = parseProfile(AppleProfileBuilder::build(input()));
        const auto b = parseProfile(AppleProfileBuilder::build(input()));
        QCOMPARE(a.value("PayloadIdentifier"), b.value("PayloadIdentifier"));
        QVERIFY(a.value("PayloadIdentifier").toString().startsWith(QStringLiteral("org.example.nodrive.davprofile.")));
        QCOMPARE(a.value("PayloadIdentifier").toString(), AppleProfileBuilder::payloadIdentifier(QStringLiteral("org.example.nodrive"), QStringLiteral("ali@drive.example.test")));
        QVERIFY(a.value("PayloadUUID").toString() != b.value("PayloadUUID").toString());
        const auto ca = a.value("PayloadContent").toList();
        const auto cb = b.value("PayloadContent").toList();
        for (int i = 0; i < ca.size(); ++i) {
            QVERIFY(ca.at(i).toMap().value("PayloadUUID") != cb.at(i).toMap().value("PayloadUUID"));
            QCOMPARE(ca.at(i).toMap().value("PayloadIdentifier"), cb.at(i).toMap().value("PayloadIdentifier"));
        }
        QVERIFY(!QUuid::fromString(a.value("PayloadUUID").toString()).isNull());

        auto other = input();
        other.accountId = QStringLiteral("mara@drive.example.test");
        const auto c = parseProfile(AppleProfileBuilder::build(other));
        QVERIFY(c.value("PayloadIdentifier") != a.value("PayloadIdentifier"));
    }

    void noNextcloudAnywhere()
    {
        const auto data = AppleProfileBuilder::build(input());
        QVERIFY(!data.toLower().contains("nextcloud"));
        QVERIFY(data.contains("NoDrive"));
    }

    void portsPathsAndInsecureServers()
    {
        auto in = input();
        in.server = QUrl(QStringLiteral("https://drive.example.test:8443/sub/"));
        auto payload = parseProfile(AppleProfileBuilder::build(in)).value("PayloadContent").toList().at(0).toMap();
        QCOMPARE(payload.value("CalDAVPort").toInt(), 8443);
        QCOMPARE(payload.value("CalDAVPrincipalURL").toString(), QStringLiteral("https://drive.example.test:8443/sub/remote.php/dav/principals/users/ali/"));
        QCOMPARE(AppleProfileBuilder::calendarHomeUrl(in.server, "ali"), QStringLiteral("https://drive.example.test:8443/sub/remote.php/dav/calendars/ali/"));
        QCOMPARE(AppleProfileBuilder::addressBookHomeUrl(in.server, "ali"), QStringLiteral("https://drive.example.test:8443/sub/remote.php/dav/addressbooks/users/ali/"));

        in.server = QUrl(QStringLiteral("http://127.0.0.1:8080"));
        payload = parseProfile(AppleProfileBuilder::build(in)).value("PayloadContent").toList().at(0).toMap();
        QCOMPARE(payload.value("CalDAVUseSSL").toBool(), false);
        QCOMPARE(payload.value("CalDAVPort").toInt(), 8080);

        // A user name with special characters is percent-encoded in the address.
        QCOMPARE(AppleProfileBuilder::calendarHomeUrl(QUrl(QStringLiteral("https://h.test")), "a b@c"), QStringLiteral("https://h.test/remote.php/dav/calendars/a%20b%40c/"));
    }

    void incompleteInputGivesNothing()
    {
        for (int field = 0; field < 5; ++field) {
            auto in = input();
            switch (field) {
            case 0: in.server = QUrl(); break;
            case 1: in.davUser.clear(); break;
            case 2: in.loginName.clear(); break;
            case 3: in.appPassword.clear(); break;
            case 4: in.bundleId.clear(); break;
            }
            QVERIFY2(AppleProfileBuilder::build(in).isEmpty(), qPrintable(QString::number(field)));
        }
    }

    void theFileIsPrivateAndGoneAfterCleanup()
    {
        QUrl opened;
        MacProfileInstaller::setOpenerForTests([&](const QUrl &url) {
            opened = url;
            return true;
        });
        const auto profile = AppleProfileBuilder::build(input());
        QString folder;
        {
            MacProfileInstaller installer;
            QVERIFY2(installer.installProfile(profile), qPrintable(installer.errorText()));
            const auto path = installer.filePath();
            QCOMPARE(opened, QUrl::fromLocalFile(path));
            QVERIFY(path.endsWith(MacProfileInstaller::fileName()));
            QVERIFY(!MacProfileInstaller::fileName().contains(QLatin1Char('&')));
            QVERIFY(!MacProfileInstaller::fileName().contains(QLatin1Char(' ')));
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), profile);
            file.close();
            folder = QFileInfo(path).absolutePath();
#ifndef Q_OS_WIN
            // 0600 and 0700: the owner only. (QFileInfo reports the owner bits twice, as Owner and as User.)
            const auto others = QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup | QFileDevice::ReadOther
                | QFileDevice::WriteOther | QFileDevice::ExeOther;
            const auto filePermissions = QFileInfo(path).permissions();
            QVERIFY(filePermissions.testFlags(QFileDevice::ReadOwner | QFileDevice::WriteOwner));
            QVERIFY(!(filePermissions & QFileDevice::ExeOwner));
            QVERIFY(!(filePermissions & others));
            const auto folderPermissions = QFileInfo(folder).permissions();
            QVERIFY(folderPermissions.testFlags(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
            QVERIFY(!(folderPermissions & others));
#endif
            QVERIFY(QFileInfo(folder).fileName().startsWith(QStringLiteral("nodrive-dav-")));

#ifdef Q_OS_MACOS
            QProcess plutil;
            plutil.start(QStringLiteral("/usr/bin/plutil"), {QStringLiteral("-lint"), path});
            QVERIFY(plutil.waitForFinished(30000));
            QCOMPARE(plutil.exitCode(), 0);
            QVERIFY2(plutil.readAllStandardOutput().contains("OK"), "plutil -lint says OK");
#endif
            installer.cleanup();
            QVERIFY(!QFileInfo::exists(path));
            QVERIFY(!QFileInfo::exists(folder));
            installer.cleanup(); // again: harmless
        }
        QVERIFY(!QFileInfo::exists(folder));
    }

    void theFileIsGoneWhenTheInstallerIsDeletedOrTheAppQuits()
    {
        MacProfileInstaller::setOpenerForTests([](const QUrl &) { return true; });
        QString folder;
        {
            MacProfileInstaller installer;
            QVERIFY(installer.installProfile(QByteArrayLiteral("<plist/>")));
            folder = QFileInfo(installer.filePath()).absolutePath();
            QVERIFY(QFileInfo::exists(folder));
        }
        QVERIFY(!QFileInfo::exists(folder)); // deleted

        MacProfileInstaller quitting;
        QVERIFY(quitting.installProfile(QByteArrayLiteral("<plist/>")));
        const auto path = quitting.filePath();
        QVERIFY(QFileInfo::exists(path));
        QMetaObject::invokeMethod(QCoreApplication::instance(), "aboutToQuit", Qt::DirectConnection); // a private signal: by name
        QVERIFY(!QFileInfo::exists(path));
        QCOMPARE(MacProfileInstaller::lifetimeMs, 10 * 60 * 1000);
    }

    void aFailingOpenerIsAnErrorAndLeavesNothingBehind()
    {
        MacProfileInstaller::setOpenerForTests([](const QUrl &) { return false; });
        MacProfileInstaller installer;
        QVERIFY(!installer.installProfile(QByteArrayLiteral("<plist/>")));
        QVERIFY(!installer.errorText().isEmpty());
        QVERIFY(installer.filePath().isEmpty());
    }

    void removalNoteAppearsOnlyOnAMacWhereTheProfileWasOffered()
    {
        const QString accountId = QStringLiteral("ali@drive.example.test");
        QStringList notes;
        DeviceSyncService::setMessageHookForTests([&](const QString &text) { notes << text; });

        // Not offered: nothing.
        DeviceSyncService::setMacForTests(1);
        DeviceSyncService::instance()->accountDeleted(accountId);
        QVERIFY(notes.isEmpty());

        // Offered, but not a Mac: remembered state is cleared, no note.
        DeviceSyncSettings::setProfileOffered(accountId, true);
        QVERIFY(DeviceSyncSettings::profileOffered(accountId));
        DeviceSyncService::setMacForTests(0);
        DeviceSyncService::instance()->accountDeleted(accountId);
        QVERIFY(notes.isEmpty());
        QVERIFY(!DeviceSyncSettings::profileOffered(accountId));

        // Offered on a Mac: the note, once.
        DeviceSyncService::setMacForTests(1);
        DeviceSyncService::setMacMajorVersionForTests(15);
        DeviceSyncSettings::setProfileOffered(accountId, true);
        DeviceSyncService::instance()->accountDeleted(accountId);
        QCOMPARE(notes.size(), 1);
        QCOMPARE(notes.first(),
            QStringLiteral("To remove NoDrive calendars and contacts from this Mac, open System Settings › General › Device Management, "
                           "select NoDrive Calendar & Contacts and click Remove. You can also remove its password in NoDrive's Security settings on the web."));
        DeviceSyncService::instance()->accountDeleted(accountId);
        QCOMPARE(notes.size(), 1);
        QVERIFY(!DeviceSyncSettings::profileOffered(accountId));

        // macOS 13 and 14 have another path.
        DeviceSyncService::setMacMajorVersionForTests(14);
        QVERIFY(DeviceSyncService::removalText(14).contains(QStringLiteral("System Settings › Privacy & Security › Profiles")));
        QVERIFY(DeviceSyncService::removalText(13).contains(QStringLiteral("Privacy & Security")));
        QVERIFY(DeviceSyncService::removalText(15).contains(QStringLiteral("General › Device Management")));
        QVERIFY(DeviceSyncService::removalText(26).contains(QStringLiteral("General › Device Management")));
    }

    void onlyTheOfferedFlagIsSaved()
    {
        const QString accountId = QStringLiteral("mara@drive.example.test");
        DeviceSyncSettings::setProfileOffered(accountId, true);
        QSettings ini(ConfigFile().configFile(), QSettings::IniFormat);
        QCOMPARE(ini.value(DeviceSyncSettings::profileOfferedKey(accountId)).toBool(), true);
        QVERIFY(!DeviceSyncSettings::profileOfferedKey(accountId).contains(QStringLiteral("mara")));
        for (const auto &key : ini.allKeys()) {
            if (key.contains(QStringLiteral("deviceSync"), Qt::CaseInsensitive)) {
                QVERIFY2(key.endsWith(QStringLiteral("profileOffered")), qPrintable(key));
            }
        }
        DeviceSyncSettings::setProfileOffered(accountId, false);
        QVERIFY(!DeviceSyncSettings::profileOffered(accountId));
    }
};

QTEST_GUILESS_MAIN(TestNoDriveDeviceSyncProfile)
#include "testnodrivedevicesyncprofile.moc"
