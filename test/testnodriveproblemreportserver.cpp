/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * NoDrive problem reports against a real throwaway server (tools/ci/report-server.sh).
 * Skipped unless NODRIVE_TEST_SERVER_URL is set, so a normal ctest run passes it as "skipped".
 *
 * Environment:
 *   NODRIVE_TEST_SERVER_URL       for example http://nodrive-ci-server
 *   NODRIVE_TEST_USER / _PASSWORD throwaway account (values come from the CI job, never from the source)
 *   NODRIVE_TEST_WORKDIR          folder for the fake logs, the pending reports and result.txt
 *   NODRIVE_TEST_MODE             manual | same-second | offline-then-online
 *   NODRIVE_TEST_EXPECT           not-shared | shared
 */

#include "gui/nourax/problemreportmanager.h"

#include "accessmanager.h"
#include "account.h"
#include "creds/abstractcredentials.h"

#include <QDir>
#include <QFile>
#include <QTest>
#include <QNetworkReply>
#include <QTextStream>
#include <QTimeZone>

using namespace OCC;
using namespace OCC::Nourax;

namespace {

class BasicAuthAccessManager : public AccessManager
{
public:
    BasicAuthAccessManager(const QString &user, const QString &password, QObject *parent = nullptr)
        : AccessManager(parent)
        , _auth("Basic " + QByteArray(user.toUtf8() + ":" + password.toUtf8()).toBase64())
    {
    }

protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request, QIODevice *outgoingData) override
    {
        QNetworkRequest req(request);
        req.setRawHeader("Authorization", _auth);
        return AccessManager::createRequest(op, req, outgoingData);
    }

private:
    QByteArray _auth;
};

class BasicCredentials : public AbstractCredentials
{
    Q_OBJECT

public:
    BasicCredentials(const QString &user, const QString &password)
        : _user(user)
        , _password(password)
    {
        _wasFetched = true;
    }
    [[nodiscard]] QString authType() const override { return QStringLiteral("http"); }
    [[nodiscard]] QString user() const override { return _user; }
    [[nodiscard]] QString password() const override { return _password; }
    [[nodiscard]] bool ready() const override { return true; }
    bool stillValid(QNetworkReply *) override { return true; }
    void askFromUser() override {}
    void fetchFromKeychain(const QString &) override
    {
        _wasFetched = true;
        Q_EMIT fetched();
    }
    void persist() override {}
    void invalidateToken() override {}
    void forgetSensitiveData() override {}
    [[nodiscard]] QNetworkAccessManager *createQNAM() const override { return new BasicAuthAccessManager(_user, _password); }

private:
    QString _user;
    QString _password;
};

void appendLine(const QString &path, const QString &line)
{
    QFile file(path);
    if (file.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream(&file) << line << '\n';
    }
}

} // namespace

class TestNoDriveProblemReportServer : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void sendsAReportToTheThrowawayServer()
    {
        const auto serverUrl = qEnvironmentVariable("NODRIVE_TEST_SERVER_URL");
        if (serverUrl.isEmpty()) {
            QSKIP("NODRIVE_TEST_SERVER_URL is not set (this test needs the throwaway server of tools/ci/report-server.sh)");
        }
        const auto user = qEnvironmentVariable("NODRIVE_TEST_USER");
        const auto password = qEnvironmentVariable("NODRIVE_TEST_PASSWORD");
        const auto work = qEnvironmentVariable("NODRIVE_TEST_WORKDIR");
        const auto mode = qEnvironmentVariable("NODRIVE_TEST_MODE", QStringLiteral("manual"));
        const auto expect = qEnvironmentVariable("NODRIVE_TEST_EXPECT", QStringLiteral("not-shared"));
        QVERIFY(!user.isEmpty() && !password.isEmpty() && !work.isEmpty());
        // This test never talks to the real Drive.
        QVERIFY2(!QUrl(serverUrl).host().contains(QStringLiteral("nourax.org")), "refusing to contact the real Drive");

        const auto resultFile = work + QStringLiteral("/result.txt");
        QDir().mkpath(work + QStringLiteral("/logs"));

        // A fake log with made-up secrets and the throwaway password; none may reach the zip.
        {
            QFile log(work + QStringLiteral("/logs/20261028_1500_nodrive.log"));
            QVERIFY(log.open(QIODevice::WriteOnly));
            QTextStream out(&log);
            out << "2026-10-28 15:00:01 [ info sync ] session start\n";
            out << "2026-10-28 15:00:02 [ info sync.httplogger ] Header: { Authorization: Basic "
                << QByteArray(user.toUtf8() + ":" + password.toUtf8()).toBase64() << ", Accept: */*, }\n";
            out << "2026-10-28 15:00:03 [ info sync ] login password=" << password << " for planted.user@example.org\n";
            out << "2026-10-28 15:00:04 [ info sync ] Cookie: oc_sessionPassphrase=Fake9Session8Passphrase7Value6Zz5Yy4Xx3; nc_session_id=fake1234\n";
            out << "2026-10-28 15:00:05 [ info sync ] app password Fk4pA-Pp9wD-Zz2Qq-Rr7Tt-Uu1Vv created\n";
        }

        auto account = Account::create();
        account->setCredentials(new BasicCredentials(user, password));
        account->setUrl(QUrl(serverUrl));
        account->setDavUser(user);

        ProblemReportManager manager;
        manager.setPendingDirectory(work + QStringLiteral("/pending"));
        manager.setSourcesProvider([work] {
            ProblemReportSources sources;
            sources.logDir = work + QStringLiteral("/logs");
            sources.buildInfo = QStringLiteral("NoDrive Desktop report-server test");
            sources.arguments = { QStringLiteral("nodrive"), QStringLiteral("--apppassword"), QStringLiteral("Fk4pA-Pp9wD-Zz2Qq-Rr7Tt-Uu1Vv") };
            return sources;
        });
        bool online = mode != QLatin1String("offline-then-online");
        manager.setAccountProvider([account, &online] { return online ? account : AccountPtr(); });

        quint64 finishedId = 0;
        ProblemReportManager::Outcome outcome = ProblemReportManager::Outcome::Sent;
        connect(&manager, &ProblemReportManager::submissionFinished, &manager, [&](quint64 id, ProblemReportManager::Outcome o) {
            finishedId = id;
            outcome = o;
        });

        ProblemReportInfo info;
        info.kind = ReportKind::Manual;
        info.userNote = QStringLiteral("report-server test, contact planted.user@example.org");
        if (mode == QLatin1String("same-second")) {
            info.createdUtc = QDateTime(QDate(2026, 10, 28), QTime(10, 0, 0), QTimeZone::utc());
        }
        const auto id = manager.submit(info, false);
        QTRY_VERIFY_WITH_TIMEOUT(finishedId == id, 120000);

        if (mode == QLatin1String("offline-then-online")) {
            QCOMPARE(outcome, ProblemReportManager::Outcome::SavedLocally);
            QVERIFY(manager.hasPendingReports());
            appendLine(resultFile, QStringLiteral("offline=saved-locally pending=1"));
            online = true;
            manager.sendPending();
            QTRY_VERIFY_WITH_TIMEOUT(!manager.hasPendingReports(), 120000);
            appendLine(resultFile, QStringLiteral("online=sent pending=0"));
            return;
        }

        appendLine(resultFile, QStringLiteral("mode=%1 outcome=%2").arg(mode, QString::number(int(outcome))));
        if (expect == QLatin1String("shared")) {
            QCOMPARE(outcome, ProblemReportManager::Outcome::Sent);
        } else {
            QCOMPARE(outcome, ProblemReportManager::Outcome::SavedNotShared);
            QCOMPARE(ProblemReportManager::outcomeText(outcome), QStringLiteral("Saved in your NoDrive › Problem reports."));
        }
        QVERIFY(!manager.hasPendingReports()); // the report is on the server
    }
};

QTEST_GUILESS_MAIN(TestNoDriveProblemReportServer)
#include "testnodriveproblemreportserver.moc"
