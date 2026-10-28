/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * NoDrive problem reports: upload through the account's own connection (upstream's in-process
 * fake server), the read-only share to Support, and the pending queue in the manager.
 */

#include "gui/nourax/problemreportbuilder.h"
#include "gui/nourax/problemreportmanager.h"
#include "gui/nourax/problemreportuploader.h"

#include "account.h"
#include "syncenginetestutils.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopedPointer>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>

using namespace OCC;
using namespace OCC::Nourax;

namespace {

const QString accountHost = QStringLiteral("drive.example.test");

QJsonDocument ocsReply(int statusCode, const QJsonValue &data)
{
    QJsonObject meta{ { "status", statusCode == 200 ? "ok" : "failure" }, { "statuscode", statusCode }, { "message", "" } };
    QJsonObject ocs{ { "meta", meta }, { "data", data } };
    return QJsonDocument(QJsonObject{ { "ocs", ocs } });
}

int zipCount(const QString &dir)
{
    return QDir(dir).entryList(QStringList{ QStringLiteral("*.zip") }, QDir::Files).size();
}

} // namespace

class TestNoDriveProblemReportUpload : public QObject
{
    Q_OBJECT

    struct Recorded {
        QString verb;
        QUrl url;
        QMap<QByteArray, QByteArray> headers;
        QByteArray body;
    };

    QTemporaryDir _root;
    QScopedPointer<FakeQNAM> _qnam;
    AccountPtr _account;
    QList<Recorded> _requests;
    QSet<QString> _existingFiles;
    bool _folderExists = false;
    bool _supportShareExists = false;
    int _postStatus = 200; // 200 = the share is created, 404 = "no such user"
    int _putStatus = 201; // anything else: the upload fails

    [[nodiscard]] int count(const QString &verb, const QString &pathPart = {}) const
    {
        int n = 0;
        for (const auto &r : _requests) {
            if (r.verb == verb && r.url.path().contains(pathPart)) {
                ++n;
            }
        }
        return n;
    }

    [[nodiscard]] QString makeZip(const QString &name = QStringLiteral("2026-10-28_14-03-22_linux_35.0.0-nourax.1.zip"))
    {
        const QString path = _root.path() + QLatin1Char('/') + name;
        QFile file(path);
        file.open(QIODevice::WriteOnly);
        file.write("PK fake report zip, the content does not matter here");
        return path;
    }

    ProblemReportUploader::Result runUploader(const QString &zip, QString *remoteName = nullptr)
    {
        ProblemReportUploader uploader(_account, zip);
        bool done = false;
        ProblemReportUploader::Result result = ProblemReportUploader::Result::NetworkError;
        QObject::connect(&uploader, &ProblemReportUploader::finished, &uploader, [&](ProblemReportUploader::Result r, const QString &name) {
            done = true;
            result = r;
            if (remoteName) {
                *remoteName = name;
            }
        });
        uploader.start();
        if (!QTest::qWaitFor([&done] { return done; }, 10000)) {
            QTest::qFail("the uploader did not finish", __FILE__, __LINE__);
        }
        return result;
    }

    // The safety rules of the plan: only MKCOL, PUT, GET and POST; only the account's own host.
    void verifyOnlyOwnHostAndSafeMethods() const
    {
        const QSet<QString> allowed{ QStringLiteral("MKCOL"), QStringLiteral("PUT"), QStringLiteral("GET"), QStringLiteral("POST") };
        QVERIFY(!_requests.isEmpty());
        for (const auto &r : _requests) {
            QVERIFY2(allowed.contains(r.verb), qPrintable(r.verb));
            QCOMPARE(r.url.host(), accountHost);
            QVERIFY(!r.url.toString().contains(QStringLiteral("nourax.org")));
        }
    }

    QNetworkReply *serve(QNetworkAccessManager::Operation op, const QNetworkRequest &request, QIODevice *device)
    {
        Recorded r;
        const auto customVerb = request.attribute(QNetworkRequest::CustomVerbAttribute).toString();
        switch (op) {
        case QNetworkAccessManager::GetOperation:
            r.verb = QStringLiteral("GET");
            break;
        case QNetworkAccessManager::PutOperation:
            r.verb = QStringLiteral("PUT");
            break;
        case QNetworkAccessManager::PostOperation:
            r.verb = QStringLiteral("POST");
            break;
        case QNetworkAccessManager::DeleteOperation:
            r.verb = QStringLiteral("DELETE");
            break;
        default:
            r.verb = customVerb;
        }
        r.url = request.url();
        for (const auto &name : request.rawHeaderList()) {
            r.headers.insert(name.toLower(), request.rawHeader(name)); // Qt 6 keeps header names in lower case
        }
        if (device) {
            if (!device->isOpen()) {
                device->open(QIODevice::ReadOnly);
            }
            r.body = device->readAll();
            device->seek(0);
        }
        _requests.append(r);

        const auto path = request.url().path();
        auto *parent = _qnam.data();
        if (r.verb == QLatin1String("MKCOL")) {
            if (_folderExists) {
                return new FakeErrorReply(op, request, parent, 405);
            }
            _folderExists = true;
            return new FakeJsonReply(op, request, parent, 201);
        }
        if (r.verb == QLatin1String("PUT")) {
            const auto name = path.mid(path.lastIndexOf(QLatin1Char('/')) + 1);
            if (_putStatus != 201) {
                return new FakeErrorReply(op, request, parent, _putStatus);
            }
            if (r.headers.value("if-none-match") == "*" && _existingFiles.contains(name)) {
                return new FakeErrorReply(op, request, parent, 412);
            }
            _existingFiles.insert(name);
            return new FakeJsonReply(op, request, parent, 201);
        }
        if (path.contains(QStringLiteral("/files_sharing/api/v1/shares"))) {
            if (r.verb == QLatin1String("GET")) {
                QJsonArray shares;
                if (_supportShareExists) {
                    shares.append(QJsonObject{ { "id", "7" }, { "share_type", 0 }, { "share_with", "nourax-support" }, { "permissions", 1 } });
                }
                return new FakeJsonReply(op, request, parent, 200, ocsReply(200, shares));
            }
            if (r.verb == QLatin1String("POST")) {
                if (_postStatus == 200) {
                    _supportShareExists = true;
                    return new FakeJsonReply(op, request, parent, 200, ocsReply(200, QJsonObject{ { "id", "8" } }));
                }
                return new FakeJsonErrorReply(op, request, parent, _postStatus, ocsReply(_postStatus, QJsonValue()));
            }
        }
        return new FakeErrorReply(op, request, parent, 404);
    }

    static ProblemReportSources sourcesIn(const QString &root)
    {
        QDir().mkpath(root + QStringLiteral("/logs"));
        QFile log(root + QStringLiteral("/logs/a.log"));
        log.open(QIODevice::WriteOnly);
        log.write("2026-10-28 15:00:01 some log line\n");
        ProblemReportSources sources;
        sources.logDir = root + QStringLiteral("/logs");
        sources.buildInfo = QStringLiteral("build info");
        return sources;
    }

private Q_SLOTS:
    void init()
    {
        _requests.clear();
        _existingFiles.clear();
        _folderExists = false;
        _supportShareExists = false;
        _postStatus = 200;
        _putStatus = 201;
        _qnam.reset(new FakeQNAM({}));
        _qnam->setOverride([this](QNetworkAccessManager::Operation op, const QNetworkRequest &request, QIODevice *device) {
            return serve(op, request, device);
        });
        _account = Account::create();
        _account->setCredentials(new FakeCredentials{ _qnam.data() });
        _account->setUrl(QUrl(QStringLiteral("http://") + accountHost));
    }

    void cleanup()
    {
        _account.reset();
        _qnam.reset();
    }

    void uploadsCreatesTheFolderAndSharesItReadOnly()
    {
        QString remote;
        QCOMPARE(runUploader(makeZip(), &remote), ProblemReportUploader::Result::Sent);
        QCOMPARE(remote, QStringLiteral("2026-10-28_14-03-22_linux_35.0.0-nourax.1.zip"));

        // MKCOL of the folder, then the PUT with If-None-Match: *, then the share check and one POST.
        QCOMPARE(count(QStringLiteral("MKCOL")), 1);
        QCOMPARE(count(QStringLiteral("PUT")), 1);
        QCOMPARE(count(QStringLiteral("GET"), QStringLiteral("/shares")), 1);
        QCOMPARE(count(QStringLiteral("POST"), QStringLiteral("/shares")), 1);
        for (const auto &r : std::as_const(_requests)) {
            if (r.verb == QLatin1String("MKCOL")) {
                QVERIFY2(r.url.path().endsWith(QStringLiteral("/Problem reports")), qPrintable(r.url.path()));
            } else if (r.verb == QLatin1String("PUT")) {
                QCOMPARE(r.headers.value("if-none-match"), QByteArray("*"));
                QVERIFY(r.url.path().contains(QStringLiteral("/Problem reports/2026-10-28_14-03-22_linux_35.0.0-nourax.1.zip")));
                QVERIFY(r.body.startsWith("PK fake report zip"));
            } else if (r.verb == QLatin1String("POST")) {
                const QUrlQuery body(QString::fromUtf8(r.body));
                QCOMPARE(body.queryItemValue(QStringLiteral("shareType"), QUrl::FullyDecoded), QStringLiteral("0"));
                QCOMPARE(body.queryItemValue(QStringLiteral("shareWith"), QUrl::FullyDecoded), QStringLiteral("nourax-support"));
                QCOMPARE(body.queryItemValue(QStringLiteral("permissions"), QUrl::FullyDecoded), QStringLiteral("1"));
                QCOMPARE(body.queryItemValue(QStringLiteral("path"), QUrl::FullyDecoded), QStringLiteral("/Problem reports"));
            }
        }
        verifyOnlyOwnHostAndSafeMethods();
    }

    void anExistingFolderIsFine()
    {
        _folderExists = true; // MKCOL answers 405
        QCOMPARE(runUploader(makeZip()), ProblemReportUploader::Result::Sent);
        QCOMPARE(count(QStringLiteral("PUT")), 1);
        verifyOnlyOwnHostAndSafeMethods();
    }

    void anExistingSupportShareMeansNoNewShare()
    {
        _supportShareExists = true;
        QCOMPARE(runUploader(makeZip()), ProblemReportUploader::Result::Sent);
        QCOMPARE(count(QStringLiteral("POST")), 0);
        verifyOnlyOwnHostAndSafeMethods();
    }

    void aFailedShareIsQuietAndTriedOnlyOncePerReport()
    {
        _postStatus = 404; // nourax-support does not exist yet
        QCOMPARE(runUploader(makeZip(QStringLiteral("2026-10-28_14-03-22_linux_35.0.0-nourax.1.zip"))), ProblemReportUploader::Result::SavedNotShared);
        QCOMPARE(count(QStringLiteral("POST")), 1); // no retry loop
        QCOMPARE(count(QStringLiteral("PUT")), 1); // the report is saved

        // The next report tries once more.
        QCOMPARE(runUploader(makeZip(QStringLiteral("2026-10-28_15-00-00_linux_35.0.0-nourax.1.zip"))), ProblemReportUploader::Result::SavedNotShared);
        QCOMPARE(count(QStringLiteral("POST")), 2);

        // Once the account exists, the next report shares the folder.
        _postStatus = 200;
        QCOMPARE(runUploader(makeZip(QStringLiteral("2026-10-28_16-00-00_linux_35.0.0-nourax.1.zip"))), ProblemReportUploader::Result::Sent);
        QCOMPARE(count(QStringLiteral("POST")), 3);
        verifyOnlyOwnHostAndSafeMethods();
    }

    void anExistingFileIsNeverReplaced()
    {
        _existingFiles.insert(QStringLiteral("2026-10-28_14-03-22_linux_35.0.0-nourax.1.zip"));
        QString remote;
        QCOMPARE(runUploader(makeZip(), &remote), ProblemReportUploader::Result::Sent);
        QCOMPARE(remote, QStringLiteral("2026-10-28_14-03-22_linux_35.0.0-nourax.1_2.zip"));
        QCOMPARE(count(QStringLiteral("PUT")), 2); // the first was refused with 412
        QCOMPARE(count(QStringLiteral("DELETE")), 0);
        QCOMPARE(count(QStringLiteral("MOVE")), 0);
        verifyOnlyOwnHostAndSafeMethods();
    }

    void aFailedUploadIsAnErrorAndSharesNothing()
    {
        _putStatus = 500;
        QCOMPARE(runUploader(makeZip()), ProblemReportUploader::Result::NetworkError);
        QCOMPARE(count(QStringLiteral("POST")), 0);
    }

    // ---- the manager ----

    void aReportWithoutAnAccountStaysPendingAndIsSentAfterSignIn()
    {
        QTemporaryDir work;
        const QString pending = work.path() + QStringLiteral("/pending");
        ProblemReportManager manager;
        manager.setPendingDirectory(pending);
        manager.setSourcesProvider([&work] { return sourcesIn(work.path()); });
        bool signedIn = false;
        manager.setAccountProvider([this, &signedIn] { return signedIn ? _account : AccountPtr(); });

        quint64 finishedId = 0;
        ProblemReportManager::Outcome outcome = ProblemReportManager::Outcome::Sent;
        QObject::connect(&manager, &ProblemReportManager::submissionFinished, &manager, [&](quint64 id, ProblemReportManager::Outcome o) {
            finishedId = id;
            outcome = o;
        });
        QStringList messages;
        QObject::connect(&manager, &ProblemReportManager::userMessage, &manager, [&messages](const QString &text) {
            messages << text;
        });

        ProblemReportInfo info;
        info.kind = ReportKind::Manual;
        info.userNote = QStringLiteral("nobody is signed in");
        const auto id = manager.submit(info, true);
        QTRY_VERIFY_WITH_TIMEOUT(finishedId == id, 30000);
        QCOMPARE(outcome, ProblemReportManager::Outcome::SavedLocally);
        QVERIFY(manager.hasPendingReports());
        QCOMPARE(QDir(pending).entryList(QStringList{ QStringLiteral("*.zip") }, QDir::Files).size(), 1);
        QVERIFY(messages.last().startsWith(QStringLiteral("Saved on this computer.")));
        QVERIFY(_requests.isEmpty()); // nothing went anywhere

        // The account connects: the waiting report is sent and removed from the pending folder.
        signedIn = true;
        messages.clear();
        manager.sendPending();
        QTRY_VERIFY_WITH_TIMEOUT(!manager.hasPendingReports(), 30000);
        QTRY_VERIFY_WITH_TIMEOUT(!messages.isEmpty(), 10000);
        QCOMPARE(messages.last(), QStringLiteral("Problem report sent"));
        QCOMPARE(count(QStringLiteral("PUT")), 1);
        verifyOnlyOwnHostAndSafeMethods();

        // Nothing is left to send: no further request.
        const auto before = _requests.size();
        manager.sendPending();
        QTest::qWait(300);
        QCOMPARE(_requests.size(), before);
    }

    void aNetworkErrorKeepsTheReportPendingUntilTheNextTry()
    {
        QTemporaryDir work;
        const QString pending = work.path() + QStringLiteral("/pending");
        ProblemReportManager manager;
        manager.setPendingDirectory(pending);
        manager.setSourcesProvider([&work] { return sourcesIn(work.path()); });
        manager.setAccountProvider([this] { return _account; });

        quint64 finishedId = 0;
        ProblemReportManager::Outcome outcome = ProblemReportManager::Outcome::Sent;
        QObject::connect(&manager, &ProblemReportManager::submissionFinished, &manager, [&](quint64 id, ProblemReportManager::Outcome o) {
            finishedId = id;
            outcome = o;
        });

        _putStatus = 500;
        const auto id = manager.submit(ProblemReportInfo(), false);
        QTRY_VERIFY_WITH_TIMEOUT(finishedId == id, 30000);
        QCOMPARE(outcome, ProblemReportManager::Outcome::TryAgainLater);
        QVERIFY(manager.hasPendingReports());

        _putStatus = 201;
        manager.sendPending();
        QTRY_VERIFY_WITH_TIMEOUT(!manager.hasPendingReports(), 30000);
        verifyOnlyOwnHostAndSafeMethods();
    }

    // ---- signed in, but not connected (offline, still connecting at start-up) ----

    void aSignedInAccountThatIsNotConnectedWaitsQuietlyAndSaysSentOnce()
    {
        QTemporaryDir work;
        const QString pending = work.path() + QStringLiteral("/pending");
        ProblemReportManager manager;
        manager.setPendingDirectory(pending);
        manager.setSourcesProvider([&work] { return sourcesIn(work.path()); });
        bool connected = false;
        manager.setAccountProvider([this, &connected] { return connected ? _account : AccountPtr(); });
        manager.setAccountExistsProvider([] { return true; }); // signed in, only not connected yet

        quint64 finishedId = 0;
        ProblemReportManager::Outcome outcome = ProblemReportManager::Outcome::Sent;
        QObject::connect(&manager, &ProblemReportManager::submissionFinished, &manager, [&](quint64 id, ProblemReportManager::Outcome o) {
            finishedId = id;
            outcome = o;
        });
        QStringList messages;
        QObject::connect(&manager, &ProblemReportManager::userMessage, &manager, [&messages](const QString &text) {
            messages << text;
        });

        // An automatic report (tray message wanted): no "sign in" text, no interim message.
        ProblemReportInfo info;
        info.kind = ReportKind::UncleanExit;
        const auto id = manager.submit(info, true);
        QTRY_VERIFY_WITH_TIMEOUT(finishedId == id, 30000);
        QCOMPARE(outcome, ProblemReportManager::Outcome::TryAgainLater);
        QVERIFY(messages.isEmpty());
        QVERIFY(manager.hasPendingReports());
        QVERIFY(_requests.isEmpty());

        // The account connects: one "Problem report sent".
        connected = true;
        manager.sendPending();
        QTRY_VERIFY_WITH_TIMEOUT(!manager.hasPendingReports(), 30000);
        QTRY_VERIFY_WITH_TIMEOUT(!messages.isEmpty(), 10000);
        QCOMPARE(messages, QStringList{ QStringLiteral("Problem report sent") });
        verifyOnlyOwnHostAndSafeMethods();
    }

    void aManualReportWhileNotConnectedSaysItWillTryAgain()
    {
        QTemporaryDir work;
        ProblemReportManager manager;
        manager.setPendingDirectory(work.path() + QStringLiteral("/pending"));
        manager.setSourcesProvider([&work] { return sourcesIn(work.path()); });
        manager.setAccountProvider([] { return AccountPtr(); });
        manager.setAccountExistsProvider([] { return true; });

        quint64 finishedId = 0;
        ProblemReportManager::Outcome outcome = ProblemReportManager::Outcome::Sent;
        QObject::connect(&manager, &ProblemReportManager::submissionFinished, &manager, [&](quint64 id, ProblemReportManager::Outcome o) {
            finishedId = id;
            outcome = o;
        });
        const auto id = manager.submit(ProblemReportInfo(), false);
        QTRY_VERIFY_WITH_TIMEOUT(finishedId == id, 30000);
        QCOMPARE(outcome, ProblemReportManager::Outcome::TryAgainLater);
        QCOMPARE(ProblemReportManager::outcomeText(outcome),
            QStringLiteral("Could not send the problem report now. NoDrive will try again when it is connected."));
        QVERIFY(manager.hasPendingReports());
        QVERIFY(_requests.isEmpty());
    }

    // ---- the report of a crash or unclean exit is built at once and kept until the user answers ----

    void aHeldReportIsBuiltAtOnceAndSentOnSendWithoutAnInterimMessage()
    {
        QTemporaryDir work;
        const QString pending = work.path() + QStringLiteral("/pending");
        const QString held = pending + QStringLiteral("/awaiting-answer");
        ProblemReportManager manager;
        manager.setPendingDirectory(pending);
        manager.setSourcesProvider([&work] { return sourcesIn(work.path()); });
        bool connected = false;
        manager.setAccountProvider([this, &connected] { return connected ? _account : AccountPtr(); });
        manager.setAccountExistsProvider([] { return true; });
        QStringList messages;
        QObject::connect(&manager, &ProblemReportManager::userMessage, &manager, [&messages](const QString &text) {
            messages << text;
        });

        ProblemReportInfo info;
        info.kind = ReportKind::UncleanExit;
        manager.holdReport(info);
        // Built before anybody answered; nothing is pending, so nothing can be sent by a reconnect.
        QTRY_COMPARE_WITH_TIMEOUT(zipCount(held), 1, 30000);
        QCOMPARE(zipCount(pending), 0);
        QVERIFY(!manager.hasPendingReports());

        manager.answerHeldReport(true);
        QTRY_VERIFY_WITH_TIMEOUT(manager.hasPendingReports(), 10000);
        QCOMPARE(zipCount(held), 0);
        QTest::qWait(200);
        QVERIFY(messages.isEmpty()); // signed in but not connected: no interim message
        QVERIFY(_requests.isEmpty());

        connected = true;
        manager.sendPending();
        QTRY_VERIFY_WITH_TIMEOUT(!manager.hasPendingReports(), 30000);
        QTRY_VERIFY_WITH_TIMEOUT(!messages.isEmpty(), 10000);
        QCOMPARE(messages, QStringList{ QStringLiteral("Problem report sent") });
        verifyOnlyOwnHostAndSafeMethods();
    }

    void aHeldReportAnsweredBeforeTheBuildEndsIsStillSent()
    {
        QTemporaryDir work;
        const QString pending = work.path() + QStringLiteral("/pending");
        ProblemReportManager manager;
        manager.setPendingDirectory(pending);
        manager.setSourcesProvider([&work] { return sourcesIn(work.path()); });
        manager.setAccountProvider([this] { return _account; });
        QStringList messages;
        QObject::connect(&manager, &ProblemReportManager::userMessage, &manager, [&messages](const QString &text) {
            messages << text;
        });

        ProblemReportInfo info;
        info.kind = ReportKind::Crash;
        info.crashText = QStringLiteral("signal 11 (SIGSEGV)");
        manager.holdReport(info);
        manager.answerHeldReport(true); // the worker thread is probably still busy
        QTRY_VERIFY_WITH_TIMEOUT(!messages.isEmpty(), 30000);
        QCOMPARE(messages, QStringList{ QStringLiteral("Problem report sent") });
        QVERIFY(!manager.hasPendingReports());
        QCOMPARE(count(QStringLiteral("PUT")), 1);
    }

    void aHeldReportThatTheUserDeclinesIsRemovedAndNeverSent()
    {
        QTemporaryDir work;
        const QString pending = work.path() + QStringLiteral("/pending");
        const QString held = pending + QStringLiteral("/awaiting-answer");
        ProblemReportManager manager;
        manager.setPendingDirectory(pending);
        manager.setSourcesProvider([&work] { return sourcesIn(work.path()); });
        manager.setAccountProvider([this] { return _account; });

        ProblemReportInfo info;
        info.kind = ReportKind::UncleanExit;
        manager.holdReport(info);
        QTRY_COMPARE_WITH_TIMEOUT(zipCount(held), 1, 30000);
        manager.answerHeldReport(false); // "Not now"
        QCOMPARE(zipCount(held), 0);
        QVERIFY(!manager.hasPendingReports());
        manager.sendPending();
        QTest::qWait(300);
        QVERIFY(_requests.isEmpty()); // nothing left the computer
    }

    // ---- one share attempt per batch ----

    void aBatchOfReportsTriesTheShareOnlyOnce()
    {
        QTemporaryDir work;
        const QString pending = work.path() + QStringLiteral("/pending");
        QDir().mkpath(pending);
        const QStringList names{ QStringLiteral("2026-10-28_10-00-00_linux_35.0.0-nourax.1.zip"),
            QStringLiteral("2026-10-28_11-00-00_linux_35.0.0-nourax.1.zip"),
            QStringLiteral("2026-10-28_12-00-00_linux_35.0.0-nourax.1.zip") };
        for (const auto &name : names) {
            QFile file(pending + QLatin1Char('/') + name);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("PK fake report zip, the content does not matter here");
        }
        ProblemReportManager manager;
        manager.setPendingDirectory(pending);
        manager.setAccountProvider([this] { return _account; });
        _postStatus = 404; // nourax-support does not exist yet
        QStringList messages;
        QObject::connect(&manager, &ProblemReportManager::userMessage, &manager, [&messages](const QString &text) {
            messages << text;
        });

        manager.sendPending();
        QTRY_VERIFY_WITH_TIMEOUT(!manager.hasPendingReports(), 30000);
        QTRY_VERIFY_WITH_TIMEOUT(!messages.isEmpty(), 10000);
        QCOMPARE(messages, QStringList{ QStringLiteral("Saved in your NoDrive › Problem reports.") });
        QCOMPARE(count(QStringLiteral("PUT")), 3); // every report is uploaded
        QCOMPARE(count(QStringLiteral("GET"), QStringLiteral("/shares")), 1); // the share is checked once
        QCOMPARE(count(QStringLiteral("POST"), QStringLiteral("/shares")), 1); // and created once
        verifyOnlyOwnHostAndSafeMethods();
    }

    void aSentReportWithAFailedShareSaysSaved()
    {
        QTemporaryDir work;
        ProblemReportManager manager;
        manager.setPendingDirectory(work.path() + QStringLiteral("/pending"));
        manager.setSourcesProvider([&work] { return sourcesIn(work.path()); });
        manager.setAccountProvider([this] { return _account; });
        _postStatus = 404;

        quint64 finishedId = 0;
        ProblemReportManager::Outcome outcome = ProblemReportManager::Outcome::Sent;
        QObject::connect(&manager, &ProblemReportManager::submissionFinished, &manager, [&](quint64 id, ProblemReportManager::Outcome o) {
            finishedId = id;
            outcome = o;
        });
        const auto id = manager.submit(ProblemReportInfo(), false);
        QTRY_VERIFY_WITH_TIMEOUT(finishedId == id, 30000);
        QCOMPARE(outcome, ProblemReportManager::Outcome::SavedNotShared);
        QCOMPARE(ProblemReportManager::outcomeText(outcome), QStringLiteral("Saved in your NoDrive › Problem reports."));
        QVERIFY(!manager.hasPendingReports()); // it is on the Drive
        QCOMPARE(count(QStringLiteral("POST")), 1);
    }
};

QTEST_GUILESS_MAIN(TestNoDriveProblemReportUpload)
#include "testnodriveproblemreportupload.moc"
