/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * NoDrive device calendar and contacts sync: Login Flow v2 for the separate app password, against a scripted network
 * manager. plans/noagenda-desktop-plan.md, 4.3 and 4.6. The planted password must never show up in log output, in the
 * settings or in any file.
 */

#include "gui/nourax/devicesync/deviceapppasswordflow.h"

#include "configfile.h"
#include "syncenginetestutils.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QDirIterator>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>

using namespace OCC;
using namespace OCC::Nourax::DeviceSync;

namespace {

const QByteArray plantedPassword = QByteArrayLiteral("PLANTED-fake-app-pw-9f3a");
const QString serverUrl = QStringLiteral("https://drive.example.test");

QStringList &captured()
{
    static QStringList messages;
    return messages;
}

// "category: message" of what our own code logged (the test network manager and the client's HttpLogger log requests too).
QStringList &capturedOwn()
{
    static QStringList messages;
    return messages;
}

QtMessageHandler &previousHandler()
{
    static QtMessageHandler handler = nullptr;
    return handler;
}

void captureMessage(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    captured() << message;
    if (context.category && QByteArray(context.category).startsWith("nextcloud.gui.nourax.devicesync")) {
        capturedOwn() << message;
    }
    if (message.contains(QString::fromUtf8(QByteArray("ali:" + plantedPassword).toBase64())) && previousHandler()) {
        // Information for the test log: which logger printed the Basic credentials (the test network manager, not our code).
        const auto note = QStringLiteral("NOTE: category %1 printed the Basic credentials").arg(QString::fromUtf8(context.category ? context.category : "?"));
        previousHandler()(QtWarningMsg, context, note);
    }
    // Warnings and worse still reach QtTest's output (a fatal message must not vanish).
    if (type != QtDebugMsg && type != QtInfoMsg && previousHandler()) {
        previousHandler()(type, context, message);
    }
}

QByteArray initAnswer(const QString &loginHost = QStringLiteral("drive.example.test"), const QString &endpointHost = QStringLiteral("drive.example.test"))
{
    QJsonObject poll{{"token", "tok-123"}, {"endpoint", QStringLiteral("https://%1/login/v2/poll").arg(endpointHost)}};
    QJsonObject root{{"poll", poll}, {"login", QStringLiteral("https://%1/login/v2/flow/tok-123").arg(loginHost)}};
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QByteArray grantAnswer(const QString &server = serverUrl, const QByteArray &password = plantedPassword)
{
    QJsonObject root{{"server", server}, {"loginName", "ali"}, {"appPassword", QString::fromUtf8(password)}};
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

struct Request
{
    QString verb;
    QUrl url;
    QByteArray userAgent;
    QByteArray body;
    QByteArray authorization;
};

// A tiny scripted server: records the requests and answers from the members.
struct Script
{
    QList<Request> requests;
    QByteArray init = initAnswer();
    int initStatus = 200;
    int pendingPolls = 2; // 404 this many times, then the grant
    bool neverGrant = false;
    QByteArray grant = grantAnswer();
    int revokeStatus = 200;

    int count(const QString &verb, const QString &pathEnd) const
    {
        int n = 0;
        for (const auto &r : requests) {
            if (r.verb == verb && r.url.path().endsWith(pathEnd)) {
                ++n;
            }
        }
        return n;
    }
};

FakeQNAM *makeNam(Script &script, QObject *parent)
{
    auto *nam = new FakeQNAM({});
    nam->setParent(parent);
    nam->setOverride([&script, nam](QNetworkAccessManager::Operation op, const QNetworkRequest &request, QIODevice *device) -> QNetworkReply * {
        Request r;
        r.verb = op == QNetworkAccessManager::CustomOperation ? request.attribute(QNetworkRequest::CustomVerbAttribute).toString()
                                                              : (op == QNetworkAccessManager::PostOperation ? QStringLiteral("POST") : QStringLiteral("OTHER"));
        r.url = request.url();
        r.userAgent = request.header(QNetworkRequest::UserAgentHeader).toByteArray();
        r.authorization = request.rawHeader("Authorization");
        if (device) {
            r.body = device->peek(device->size()); // peek: the client's HttpLogger asserts that the position is 0
        }
        script.requests << r;
        const auto path = r.url.path();
        if (path.endsWith(QLatin1String("/index.php/login/v2"))) {
            if (script.initStatus != 200) {
                return new FakeErrorReply(op, request, nam, script.initStatus);
            }
            return new FakePayloadReply(op, request, script.init, nam);
        }
        if (path.endsWith(QLatin1String("/login/v2/poll"))) {
            if (script.neverGrant || script.count(QStringLiteral("POST"), QStringLiteral("/login/v2/poll")) <= script.pendingPolls) {
                return new FakeErrorReply(op, request, nam, 404);
            }
            return new FakePayloadReply(op, request, script.grant, nam);
        }
        if (path.endsWith(QLatin1String("/ocs/v2.php/core/apppassword"))) {
            if (script.revokeStatus != 200) {
                return new FakeErrorReply(op, request, nam, script.revokeStatus);
            }
            return new FakePayloadReply(op, request, QByteArrayLiteral("{\"ocs\":{\"meta\":{\"status\":\"ok\",\"statuscode\":200},\"data\":[]}}"), nam);
        }
        return new FakeErrorReply(op, request, nam, 500);
    });
    return nam;
}

std::unique_ptr<DeviceAppPasswordFlow> makeFlow(FakeQNAM *nam)
{
    auto flow = std::make_unique<DeviceAppPasswordFlow>(QUrl(serverUrl), QStringLiteral("test-mac"), nam);
    flow->setPollInterval(std::chrono::milliseconds(20));
    flow->setTimeout(std::chrono::seconds(20));
    return flow;
}

} // namespace

class TestNoDriveDeviceSyncFlow : public QObject
{
    Q_OBJECT

    QTemporaryDir _configDir;
    QDateTime _started = QDateTime::currentDateTime().addSecs(-2);
    QtMessageHandler _previousHandler = nullptr;

private Q_SLOTS:
    void initTestCase()
    {
        qRegisterMetaType<DeviceAppPasswordFlow::Failure>();
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY(_configDir.isValid());
        ConfigFile::setConfDir(_configDir.path());
        QLoggingCategory::setFilterRules(QStringLiteral("*.debug=true\n*.info=true"));
        _previousHandler = qInstallMessageHandler(captureMessage);
        previousHandler() = _previousHandler;
    }

    void cleanupTestCase()
    {
        qInstallMessageHandler(_previousHandler);
    }

    void userAgentNamesTheTokenAndServerComparisonIsStrict()
    {
        QCOMPARE(DeviceAppPasswordFlow::userAgent(QStringLiteral("Ali's Mac")), QByteArrayLiteral("NoDrive Calendar & Contacts (Ali's Mac)"));
        QVERIFY(DeviceAppPasswordFlow::sameServer(QUrl("https://drive.example.test"), QUrl("https://DRIVE.example.test/")));
        QVERIFY(DeviceAppPasswordFlow::sameServer(QUrl("https://drive.example.test:443"), QUrl("https://drive.example.test")));
        QVERIFY(DeviceAppPasswordFlow::sameServer(QUrl("https://h.test/sub"), QUrl("https://h.test/sub/")));
        QVERIFY(!DeviceAppPasswordFlow::sameServer(QUrl("https://drive.example.test"), QUrl("https://evil.example.test")));
        QVERIFY(!DeviceAppPasswordFlow::sameServer(QUrl("https://drive.example.test"), QUrl("http://drive.example.test")));
        QVERIFY(!DeviceAppPasswordFlow::sameServer(QUrl("https://drive.example.test"), QUrl("https://drive.example.test:8443")));
        QVERIFY(!DeviceAppPasswordFlow::sameServer(QUrl("https://h.test/sub"), QUrl("https://h.test")));
        QVERIFY(!DeviceAppPasswordFlow::sameServer(QUrl(), QUrl()));
        QByteArray secret(plantedPassword.constData(), plantedPassword.size()); // a heap copy: never write into a literal
        const QByteArray sharedCopy = secret;
        DeviceAppPasswordFlow::wipe(secret);
        QVERIFY(secret.isEmpty());
        QVERIFY(!sharedCopy.contains("PLANTED")); // the shared buffer was zeroed too
    }

    void initUsesOurUserAgentAndPollingContinuesOn404ThenGrants()
    {
        captured().clear();
        Script script;
        script.pendingPolls = 2;
        std::unique_ptr<FakeQNAM> nam(makeNam(script, nullptr));
        auto flow = makeFlow(nam.get());

        QSignalSpy urlSpy(flow.get(), &DeviceAppPasswordFlow::loginUrlReady);
        QSignalSpy failedSpy(flow.get(), &DeviceAppPasswordFlow::failed);
        QString loginName;
        QByteArray passwordInSlot;
        bool granted = false;
        QObject::connect(flow.get(), &DeviceAppPasswordFlow::granted, flow.get(), [&](const QString &name, const QByteArray &pw) {
            granted = true;
            loginName = name;
            passwordInSlot = QByteArray(pw.constData(), pw.size()); // our own copy, as the real receiver does
        }, Qt::DirectConnection);
        flow->start();
        QTRY_VERIFY_WITH_TIMEOUT(granted, 10000);

        QCOMPARE(urlSpy.size(), 1);
        QCOMPARE(urlSpy.first().first().toUrl(), QUrl("https://drive.example.test/login/v2/flow/tok-123"));
        QCOMPARE(flow->loginUrl(), QUrl("https://drive.example.test/login/v2/flow/tok-123"));
        QVERIFY(failedSpy.isEmpty());
        QCOMPARE(loginName, QStringLiteral("ali"));
        QCOMPARE(passwordInSlot, plantedPassword);

        // First request: init, with our user agent; no credentials, no body.
        QCOMPARE(script.requests.first().verb, QStringLiteral("POST"));
        QCOMPARE(script.requests.first().url, QUrl("https://drive.example.test/index.php/login/v2"));
        QCOMPARE(script.requests.first().userAgent, QByteArrayLiteral("NoDrive Calendar & Contacts (test-mac)"));
        QVERIFY(script.requests.first().authorization.isEmpty());
        // Then three polls (two 404, one 200), each with the token and the same user agent.
        QCOMPARE(script.count(QStringLiteral("POST"), QStringLiteral("/login/v2/poll")), 3);
        for (const auto &r : std::as_const(script.requests)) {
            QCOMPARE(r.userAgent, QByteArrayLiteral("NoDrive Calendar & Contacts (test-mac)"));
            QVERIFY(r.authorization.isEmpty());
            if (r.url.path().endsWith(QLatin1String("/poll"))) {
                QCOMPARE(QUrlQuery(QString::fromUtf8(r.body)).queryItemValue(QStringLiteral("token")), QStringLiteral("tok-123"));
            }
        }
        // No further polling after the grant.
        const auto polls = script.count(QStringLiteral("POST"), QStringLiteral("/login/v2/poll"));
        QTest::qWait(150);
        QCOMPARE(script.count(QStringLiteral("POST"), QStringLiteral("/login/v2/poll")), polls);

        // The password is in no log line.
        for (const auto &line : std::as_const(captured())) {
            QVERIFY2(!line.contains(QString::fromUtf8(plantedPassword)), "the planted password is in the log output");
        }
        QVERIFY(!captured().isEmpty()); // logging was on, so the check above means something
    }

    void aMismatchingServerIsRefused()
    {
        captured().clear();
        Script script;
        script.pendingPolls = 0;
        script.grant = grantAnswer(QStringLiteral("https://other.example.test"));
        std::unique_ptr<FakeQNAM> nam(makeNam(script, nullptr));
        auto flow = makeFlow(nam.get());
        QSignalSpy grantedSpy(flow.get(), &DeviceAppPasswordFlow::granted);
        QSignalSpy failedSpy(flow.get(), &DeviceAppPasswordFlow::failed);
        flow->start();
        QTRY_COMPARE_WITH_TIMEOUT(failedSpy.size(), 1, 10000);
        QCOMPARE(failedSpy.first().at(0).value<DeviceAppPasswordFlow::Failure>(), DeviceAppPasswordFlow::Failure::ServerMismatch);
        QVERIFY(grantedSpy.isEmpty());
        for (const auto &line : std::as_const(captured())) {
            QVERIFY(!line.contains(QString::fromUtf8(plantedPassword)));
        }
    }

    void addressesOfAnotherServerInTheStartAnswerAreRefused()
    {
        for (const auto &answer : {initAnswer(QStringLiteral("phishing.example.test")), initAnswer(QStringLiteral("drive.example.test"), QStringLiteral("phishing.example.test"))}) {
            Script script;
            script.init = answer;
            std::unique_ptr<FakeQNAM> nam(makeNam(script, nullptr));
            auto flow = makeFlow(nam.get());
            QSignalSpy urlSpy(flow.get(), &DeviceAppPasswordFlow::loginUrlReady);
            QSignalSpy failedSpy(flow.get(), &DeviceAppPasswordFlow::failed);
            flow->start();
            QTRY_COMPARE_WITH_TIMEOUT(failedSpy.size(), 1, 10000);
            QCOMPARE(failedSpy.first().at(0).value<DeviceAppPasswordFlow::Failure>(), DeviceAppPasswordFlow::Failure::ServerMismatch);
            QVERIFY(urlSpy.isEmpty()); // the browser is never sent there
            QCOMPARE(script.count(QStringLiteral("POST"), QStringLiteral("/login/v2/poll")), 0);
        }
    }

    void aServerThatDoesNotStartTheFlowGivesTheAdministratorText()
    {
        {
            Script script;
            script.initStatus = 403;
            std::unique_ptr<FakeQNAM> nam(makeNam(script, nullptr));
            auto flow = makeFlow(nam.get());
            QSignalSpy failedSpy(flow.get(), &DeviceAppPasswordFlow::failed);
            flow->start();
            QTRY_COMPARE_WITH_TIMEOUT(failedSpy.size(), 1, 10000);
            QCOMPARE(failedSpy.first().at(0).value<DeviceAppPasswordFlow::Failure>(), DeviceAppPasswordFlow::Failure::NotStarted);
            QCOMPARE(failedSpy.first().at(1).toString(), QStringLiteral("The server didn't create a password. Ask your administrator."));
        }
        {
            Script script;
            script.init = QByteArrayLiteral("<html>not json</html>");
            std::unique_ptr<FakeQNAM> nam(makeNam(script, nullptr));
            auto flow = makeFlow(nam.get());
            QSignalSpy failedSpy(flow.get(), &DeviceAppPasswordFlow::failed);
            flow->start();
            QTRY_COMPARE_WITH_TIMEOUT(failedSpy.size(), 1, 10000);
            QCOMPARE(failedSpy.first().at(0).value<DeviceAppPasswordFlow::Failure>(), DeviceAppPasswordFlow::Failure::BadAnswer);
        }
    }

    void timesOutWhenNobodyAllows()
    {
        Script script;
        script.neverGrant = true;
        std::unique_ptr<FakeQNAM> nam(makeNam(script, nullptr));
        auto flow = makeFlow(nam.get());
        flow->setTimeout(std::chrono::milliseconds(300));
        QSignalSpy grantedSpy(flow.get(), &DeviceAppPasswordFlow::granted);
        QSignalSpy failedSpy(flow.get(), &DeviceAppPasswordFlow::failed);
        flow->start();
        QTRY_COMPARE_WITH_TIMEOUT(failedSpy.size(), 1, 10000);
        QCOMPARE(failedSpy.first().at(0).value<DeviceAppPasswordFlow::Failure>(), DeviceAppPasswordFlow::Failure::TimedOut);
        QCOMPARE(failedSpy.first().at(1).toString(), QStringLiteral("The sign-in took too long. Try again."));
        QVERIFY(grantedSpy.isEmpty());
        QVERIFY(script.count(QStringLiteral("POST"), QStringLiteral("/login/v2/poll")) >= 2); // it did keep asking
        const auto polls = script.count(QStringLiteral("POST"), QStringLiteral("/login/v2/poll"));
        QTest::qWait(150);
        QCOMPARE(script.count(QStringLiteral("POST"), QStringLiteral("/login/v2/poll")), polls); // and stopped
    }

    void theDefaultsAreFiveSecondsAndTwentyMinutes()
    {
        Script script;
        std::unique_ptr<FakeQNAM> nam(makeNam(script, nullptr));
        DeviceAppPasswordFlow flow(QUrl(serverUrl), QStringLiteral("x"), nam.get());
        // The header documents 5 s and 20 min; the source of truth is the member initialisers, read through the source file.
        QFile header(QStringLiteral(NODRIVE_SOURCE_DIR "/src/gui/nourax/devicesync/deviceapppasswordflow.h"));
        QVERIFY(header.open(QIODevice::ReadOnly));
        const auto text = QString::fromUtf8(header.readAll());
        QVERIFY(text.contains(QStringLiteral("_pollInterval{5000}")));
        QVERIFY(text.contains(QStringLiteral("_timeout{20 * 60 * 1000}")));
    }

    void cancelStopsPollingAndNothingIsGranted()
    {
        Script script;
        script.pendingPolls = 1000;
        std::unique_ptr<FakeQNAM> nam(makeNam(script, nullptr));
        auto flow = makeFlow(nam.get());
        QSignalSpy urlSpy(flow.get(), &DeviceAppPasswordFlow::loginUrlReady);
        QSignalSpy grantedSpy(flow.get(), &DeviceAppPasswordFlow::granted);
        QSignalSpy failedSpy(flow.get(), &DeviceAppPasswordFlow::failed);
        flow->start();
        QTRY_VERIFY_WITH_TIMEOUT(script.count(QStringLiteral("POST"), QStringLiteral("/login/v2/poll")) >= 2, 10000);
        flow->cancel();
        const auto polls = script.count(QStringLiteral("POST"), QStringLiteral("/login/v2/poll"));
        QTest::qWait(200);
        QCOMPARE(script.count(QStringLiteral("POST"), QStringLiteral("/login/v2/poll")), polls);
        QVERIFY(grantedSpy.isEmpty());
        QVERIFY(failedSpy.isEmpty());
        QCOMPARE(urlSpy.size(), 1);
    }

    void itDidntWorkRevokesWithTheNewPassword()
    {
        captured().clear();
        capturedOwn().clear();
        Script script;
        std::unique_ptr<FakeQNAM> nam(makeNam(script, nullptr));
        auto flow = makeFlow(nam.get());
        QSignalSpy revokedSpy(flow.get(), &DeviceAppPasswordFlow::revoked);
        flow->revoke(QStringLiteral("ali"), plantedPassword);
        QTRY_COMPARE_WITH_TIMEOUT(revokedSpy.size(), 1, 10000);
        QCOMPARE(revokedSpy.first().first().toBool(), true);

        QCOMPARE(script.requests.size(), 1);
        const auto &r = script.requests.first();
        QCOMPARE(r.verb, QStringLiteral("DELETE"));
        QCOMPARE(r.url, QUrl("https://drive.example.test/ocs/v2.php/core/apppassword"));
        QCOMPARE(r.authorization, "Basic " + QByteArray("ali:" + plantedPassword).toBase64());
        QCOMPARE(r.userAgent, QByteArrayLiteral("NoDrive Calendar & Contacts (test-mac)"));
        for (const auto &line : std::as_const(captured())) {
            QVERIFY(!line.contains(QString::fromUtf8(plantedPassword)));
        }
        // What our own code logged has neither the password nor the Basic credentials.
        QVERIFY(!capturedOwn().isEmpty());
        for (const auto &line : std::as_const(capturedOwn())) {
            QVERIFY2(!line.contains(QString::fromUtf8(QByteArray("ali:" + plantedPassword).toBase64())), qPrintable(line));
        }
    }

    void aRefusedRevokeIsReportedNotHidden()
    {
        Script script;
        script.revokeStatus = 401;
        std::unique_ptr<FakeQNAM> nam(makeNam(script, nullptr));
        auto flow = makeFlow(nam.get());
        QSignalSpy revokedSpy(flow.get(), &DeviceAppPasswordFlow::revoked);
        flow->revoke(QStringLiteral("ali"), plantedPassword);
        QTRY_COMPARE_WITH_TIMEOUT(revokedSpy.size(), 1, 10000);
        QCOMPARE(revokedSpy.first().first().toBool(), false);
    }

    void thePlantedPasswordIsInNoSettingAndNoFile()
    {
        // The whole flow ran in the tests above. Nothing of it may be on disk: not in the config folder, not in the
        // temp folder, not in the application's settings.
        const QStringList roots{_configDir.path(), QDir::tempPath()};
        for (const auto &root : roots) {
            QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
            int scanned = 0;
            while (it.hasNext()) {
                const auto path = it.next();
                // Only files written during this test run (the temp folder can be big).
                if (QFileInfo(path).lastModified() < _started || QFileInfo(path).size() > 5 * 1024 * 1024) {
                    continue;
                }
                QFile file(path);
                if (!file.open(QIODevice::ReadOnly)) {
                    continue;
                }
                ++scanned;
                QVERIFY2(!file.readAll().contains(plantedPassword), qPrintable(path));
            }
            Q_UNUSED(scanned)
        }
        QSettings ini(ConfigFile().configFile(), QSettings::IniFormat);
        for (const auto &key : ini.allKeys()) {
            QVERIFY2(!ini.value(key).toString().contains(QString::fromUtf8(plantedPassword)), qPrintable(key));
        }
    }
};

QTEST_GUILESS_MAIN(TestNoDriveDeviceSyncFlow)
#include "testnodrivedevicesyncflow.moc"
