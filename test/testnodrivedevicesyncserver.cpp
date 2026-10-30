/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * NoDrive device calendar and contacts sync against a real throwaway server (tools/ci/devicesync-server.sh).
 * Skipped unless NODRIVE_TEST_SERVER_URL is set. This test talks to the throwaway server only.
 *
 * The person's grant in the browser is made by tools/ci/devicesync-grant.py (CI only): this test writes the sign-in
 * address of each flow to $NODRIVE_TEST_OUTDIR/login-url-<A|B>.txt and waits.
 *
 * Environment:
 *   NODRIVE_TEST_SERVER_URL        for example http://127.0.0.1:8080
 *   NODRIVE_TEST_USER / _PASSWORD  the person (throwaway, only for the server's own profile endpoint, never in the source)
 *   NODRIVE_TEST_COMPUTER          the computer name in the token's name ("ci")
 *   NODRIVE_TEST_REPORT            file to which "| check | PASS/FAIL | detail |" lines are appended (no values)
 *   NODRIVE_TEST_OUTDIR            folder for the login addresses and the recorded reply of the server's profile endpoint
 */

#include "gui/nourax/devicesync/appleprofilebuilder.h"
#include "gui/nourax/devicesync/deviceapppasswordflow.h"

#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>
#include <QTextStream>
#include <QTimer>
#include <QXmlStreamReader>

using namespace OCC::Nourax::DeviceSync;

namespace {

struct Answer
{
    int status = 0;
    QByteArray body;
    QString contentType;
};

Answer request(QNetworkAccessManager &nam, const QByteArray &verb, const QUrl &url, const QByteArray &authorization = {}, const QByteArray &depth = {})
{
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", "NoDrive Calendar & Contacts (ci-check)");
    req.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    req.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    if (!authorization.isEmpty()) {
        req.setRawHeader("Authorization", authorization);
    }
    if (!depth.isEmpty()) {
        req.setRawHeader("Depth", depth);
    }
    QNetworkReply *reply = nam.sendCustomRequest(req, verb);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(60000, &loop, &QEventLoop::quit);
    loop.exec();
    Answer answer;
    answer.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    answer.body = reply->readAll();
    answer.contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
    reply->deleteLater();
    return answer;
}

QByteArray basic(const QString &user, const QByteArray &password)
{
    return "Basic " + QByteArray(user.toUtf8() + ':' + password).toBase64();
}

} // namespace

class TestNoDriveDeviceSyncServer : public QObject
{
    Q_OBJECT

    QString _server;
    QString _user;
    QString _password;
    QString _computer;
    QString _reportPath;
    QString _outDir;
    bool _failed = false;

    void report(const QString &check, bool ok, const QString &detail)
    {
        if (!ok) {
            _failed = true;
        }
        if (_reportPath.isEmpty()) {
            return;
        }
        QFile file(_reportPath);
        if (file.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream(&file) << "| " << check << " | " << (ok ? "PASS" : "FAIL") << " | " << detail << " |\n";
        }
    }

    // One flow: writes the sign-in address for the grant script, waits for the grant, returns the new login name and password.
    bool grant(const QString &tag, QString *loginName, QByteArray *password, QString *failure)
    {
        QNetworkAccessManager nam;
        DeviceAppPasswordFlow flow(QUrl(_server), _computer, &nam);
        flow.setPollInterval(std::chrono::seconds(1));
        flow.setTimeout(std::chrono::minutes(5));
        bool done = false;
        bool ok = false;
        QObject::connect(&flow, &DeviceAppPasswordFlow::loginUrlReady, &flow, [&](const QUrl &url) {
            QFile file(_outDir + QStringLiteral("/login-url-") + tag + QStringLiteral(".txt"));
            if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                file.write(url.toString().toUtf8());
            }
        });
        QObject::connect(&flow, &DeviceAppPasswordFlow::granted, &flow, [&](const QString &name, const QByteArray &pw) {
            *loginName = name;
            *password = QByteArray(pw.constData(), pw.size());
            ok = true;
            done = true;
        }, Qt::DirectConnection);
        QObject::connect(&flow, &DeviceAppPasswordFlow::failed, &flow, [&](DeviceAppPasswordFlow::Failure, const QString &text) {
            *failure = text;
            done = true;
        });
        flow.start();
        QElapsedTimer timer;
        timer.start();
        while (!done && timer.elapsed() < 6 * 60 * 1000) {
            QTest::qWait(200);
        }
        return ok;
    }

private Q_SLOTS:
    void initTestCase()
    {
        _server = qEnvironmentVariable("NODRIVE_TEST_SERVER_URL");
        if (_server.isEmpty()) {
            QSKIP("NODRIVE_TEST_SERVER_URL is not set: this test only runs against the throwaway server of tools/ci/devicesync-server.sh");
        }
        _user = qEnvironmentVariable("NODRIVE_TEST_USER");
        _password = qEnvironmentVariable("NODRIVE_TEST_PASSWORD");
        _computer = qEnvironmentVariable("NODRIVE_TEST_COMPUTER", QStringLiteral("ci"));
        _reportPath = qEnvironmentVariable("NODRIVE_TEST_REPORT");
        _outDir = qEnvironmentVariable("NODRIVE_TEST_OUTDIR", QDir::tempPath());
        QDir().mkpath(_outDir);
        QStandardPaths::setTestModeEnabled(true);
    }

    // Evidence for plan 0.3: what the server's own Apple profile endpoint answers. NoDrive builds its own profile.
    void serverProfileEndpointIsRecorded()
    {
        QNetworkAccessManager nam;
        const auto url = QUrl(_server + QStringLiteral("/remote.php/dav/provisioning/apple-provisioning.mobileconfig"));
        const auto anonymous = request(nam, "GET", url);
        const auto signedIn = request(nam, "GET", url, basic(_user, _password.toUtf8()));
        const auto lower = signedIn.body.toLower();
        const bool hasPassword = signedIn.body.contains("CalDAVPassword") || signedIn.body.contains("CardDAVPassword");
        QFile out(_outDir + QStringLiteral("/server-apple-profile.txt"));
        if (out.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream(&out) << "anonymous: HTTP " << anonymous.status << "\n"
                              << "signed in: HTTP " << signedIn.status << ", content type " << signedIn.contentType << ", " << signedIn.body.size() << " bytes\n"
                              << "mentions Nextcloud: " << (lower.contains("nextcloud") ? "yes" : "no") << "\n"
                              << "has a password key: " << (hasPassword ? "yes" : "no") << "\n"
                              << "uses plain http in its address: " << (signedIn.body.contains("<key>CalDAVUseSSL</key>") ? "(see UseSSL key)" : "n/a") << "\n";
        }
        report(QStringLiteral("server-apple-profile-recorded"), true,
            QStringLiteral("anonymous HTTP %1; signed in HTTP %2, %3 bytes; mentions Nextcloud: %4; has a password key: %5; NoDrive does not use it (plan 0.3)")
                .arg(anonymous.status)
                .arg(signedIn.status)
                .arg(signedIn.body.size())
                .arg(lower.contains("nextcloud") ? QStringLiteral("yes") : QStringLiteral("no"))
                .arg(hasPassword ? QStringLiteral("yes") : QStringLiteral("no")));
    }

    // Flow A: a password that stays ("Installed").
    void grantedPasswordWorksForCalDavAndTheProfileIsBuilt()
    {
        QString loginName, failure;
        QByteArray password;
        const auto ok = grant(QStringLiteral("A"), &loginName, &password, &failure);
        report(QStringLiteral("flow-A-granted"), ok, ok ? QStringLiteral("the poll returned a login name and an app password (not shown)") : failure);
        QVERIFY2(ok, qPrintable(failure));
        QCOMPARE(loginName, _user);
        QVERIFY(!password.isEmpty());

        QNetworkAccessManager nam;
        const auto home = request(nam, "PROPFIND", QUrl(_server + QStringLiteral("/remote.php/dav/calendars/") + _user + QStringLiteral("/")), basic(loginName, password), "0");
        report(QStringLiteral("flow-A-caldav-propfind"), home.status == 207, QStringLiteral("PROPFIND the calendar home with the new password: HTTP %1").arg(home.status));
        QCOMPARE(home.status, 207);
        const auto books = request(nam, "PROPFIND", QUrl(_server + QStringLiteral("/remote.php/dav/addressbooks/users/") + _user + QStringLiteral("/")), basic(loginName, password), "0");
        report(QStringLiteral("flow-A-carddav-propfind"), books.status == 207, QStringLiteral("PROPFIND the address book home with the new password: HTTP %1").arg(books.status));
        QCOMPARE(books.status, 207);

        // The profile for the CI host: the key set of Apple's documentation.
        AppleProfileInput in;
        in.server = QUrl(_server);
        in.davUser = _user;
        in.loginName = loginName;
        in.appPassword = password;
        in.accountId = _user + QStringLiteral("@ci");
        in.bundleId = QStringLiteral("org.nourax.ci");
        const auto profile = AppleProfileBuilder::build(in);
        QXmlStreamReader xml(profile);
        QStringList keys;
        while (!xml.atEnd()) {
            if (xml.readNext() == QXmlStreamReader::StartElement && xml.name() == QLatin1String("key")) {
                keys << xml.readElementText();
            }
        }
        bool keysOk = !xml.hasError();
        for (const auto &prefix : {QStringLiteral("CalDAV"), QStringLiteral("CardDAV")}) {
            for (const auto &key : AppleProfileBuilder::accountKeys(prefix)) {
                keysOk = keysOk && keys.contains(key);
            }
        }
        const auto host = QUrl(_server).host();
        keysOk = keysOk && profile.contains(host.toUtf8()) && !profile.toLower().contains("nextcloud");
        report(QStringLiteral("profile-built-for-ci-host"), keysOk, QStringLiteral("well-formed, 14 Apple keys for host %1, no Nextcloud name").arg(host));
        QVERIFY(keysOk);
        DeviceAppPasswordFlow::wipe(password);
    }

    // Flow B: "It didn't work": the password revokes itself.
    void revokedPasswordIsRefusedAfterwards()
    {
        QString loginName, failure;
        QByteArray password;
        const auto ok = grant(QStringLiteral("B"), &loginName, &password, &failure);
        report(QStringLiteral("flow-B-granted"), ok, ok ? QStringLiteral("a second password was made") : failure);
        QVERIFY2(ok, qPrintable(failure));

        QNetworkAccessManager nam;
        const auto url = QUrl(_server + QStringLiteral("/remote.php/dav/calendars/") + _user + QStringLiteral("/"));
        const auto before = request(nam, "PROPFIND", url, basic(loginName, password), "0");
        QCOMPARE(before.status, 207);

        DeviceAppPasswordFlow flow(QUrl(_server), _computer, &nam);
        QSignalSpy revoked(&flow, &DeviceAppPasswordFlow::revoked);
        flow.revoke(loginName, QByteArray(password.constData(), password.size()));
        QTRY_COMPARE_WITH_TIMEOUT(revoked.size(), 1, 30000);
        report(QStringLiteral("flow-B-revoke"), revoked.first().first().toBool(), QStringLiteral("DELETE /ocs/v2.php/core/apppassword with the new password: accepted = %1").arg(revoked.first().first().toBool()));
        QVERIFY(revoked.first().first().toBool());

        const auto after = request(nam, "PROPFIND", url, basic(loginName, password), "0");
        report(QStringLiteral("flow-B-after-revoke-propfind"), after.status == 401, QStringLiteral("PROPFIND with the revoked password: HTTP %1 (before: %2)").arg(after.status).arg(before.status));
        QCOMPARE(after.status, 401);
        DeviceAppPasswordFlow::wipe(password);
    }

    void cleanupTestCase()
    {
        if (_failed) {
            qWarning() << "device sync server phase: at least one check failed";
        }
    }
};

QTEST_GUILESS_MAIN(TestNoDriveDeviceSyncServer)
#include "testnodrivedevicesyncserver.moc"
