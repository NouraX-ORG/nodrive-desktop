/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * NoDrive problem reports: the report builder (zip, 5 MB cap, newest 5 kept) and the run marker
 * (unclean exit and crash detection, including real crashes of a helper process).
 */

#include "gui/nourax/problemreportbuilder.h"
#include "gui/nourax/problemreportsettings.h"
#include "gui/nourax/runmarker.h"

#include <KArchiveDirectory>
#include <KArchiveFile>
#include <KCompressionDevice>
#include <KZip>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimeZone>

#ifdef Q_OS_WIN
#include <windows.h>
#ifdef _MSC_VER
#include <stdlib.h>
#endif
#endif

#include <csignal>
#include <cstdio>
#include <cstdlib>

using namespace OCC::Nourax;

namespace {

void collectEntries(const KArchiveDirectory *dir, const QString &prefix, QMap<QString, QByteArray> &out)
{
    for (const auto &name : dir->entries()) {
        const auto *entry = dir->entry(name);
        if (entry->isFile()) {
            out.insert(prefix + name, static_cast<const KArchiveFile *>(entry)->data());
        } else {
            collectEntries(static_cast<const KArchiveDirectory *>(entry), prefix + name + QLatin1Char('/'), out);
        }
    }
}

QMap<QString, QByteArray> readZip(const QString &path)
{
    QMap<QString, QByteArray> entries;
    KZip zip(path);
    if (zip.open(QIODevice::ReadOnly)) {
        collectEntries(zip.directory(), QString(), entries);
        zip.close();
    }
    return entries;
}

void writeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(path));
    file.write(data);
}

void setModified(const QString &path, const QDateTime &when)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.setFileTime(when, QFileDevice::FileModificationTime));
}

QStringList zipsIn(const QString &dir)
{
    return QDir(dir).entryList(QStringList{ QStringLiteral("*.zip") }, QDir::Files, QDir::Name);
}

QString randomLine(QRandomGenerator &rng, int number)
{
    QString line = QStringLiteral("L%1 ").arg(number, 8, 10, QLatin1Char('0'));
    for (int i = 0; i < 100; ++i) {
        line += QChar('a' + int(rng.bounded(26)));
    }
    return line;
}

// Entry point of the crash helper process (see crashHelper tests).
int crashHelperMain(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto dir = QString::fromLocal8Bit(argv[2]);
    const auto mode = QByteArray(argv[3]);

#ifdef Q_OS_WIN
    SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
#ifdef _MSC_VER
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
#endif

    RunMarker marker(dir, QStringLiteral("35.0.0.1"));
    marker.markRunning();
    marker.installHandlers();

    if (mode == "segv") {
        volatile int *nothing = nullptr;
        *nothing = 1;
    } else if (mode == "abort") {
        std::abort();
    } else if (mode == "wait") {
        std::fputs("ready\n", stdout);
        std::fflush(stdout);
        for (int i = 0; i < 600; ++i) {
            QThread::msleep(100);
        }
    }
    return 0;
}

} // namespace

class TestNoDriveProblemReport : public QObject
{
    Q_OBJECT

    static ProblemReportSources sourcesIn(const QString &root)
    {
        ProblemReportSources sources;
        sources.logDir = root + QStringLiteral("/logs");
        sources.currentLogFile = sources.logDir + QStringLiteral("/20261028_1500_nodrive.log");
        sources.configFile = root + QStringLiteral("/nodrive.cfg");
        sources.buildInfo = QStringLiteral("<p>NoDrive Desktop 35.0.0-nourax.1</p> owner jane.doe@example.com");
        sources.arguments = { QStringLiteral("nodrive"), QStringLiteral("--apppassword"), QStringLiteral("Zk8sLq3VnX0pTt4RbYw9UcHgJm2EaDf7") };
        return sources;
    }

private Q_SLOTS:
    void zipFileNameHasTheContractFormat()
    {
        const auto name = ProblemReportBuilder::zipFileName(QDateTime(QDate(2026, 10, 28), QTime(14, 3, 22), QTimeZone::utc()));
        const QRegularExpression re(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}_\\d{2}-\\d{2}-\\d{2}_(windows|macos|linux)_[0-9A-Za-z.\\-]+\\.zip$"));
        QVERIFY2(re.match(name).hasMatch(), qPrintable(name));
        QVERIFY(name.startsWith(QStringLiteral("2026-10-28_14-03-22_")));
    }

    void reportJsonHasEveryFieldWithTheRightType()
    {
        ProblemReportInfo info;
        info.kind = ReportKind::UncleanExit;
        info.userNote = QStringLiteral("it froze, mail me at jane.doe@example.com");
        info.serverHost = QStringLiteral("drive.example.org");
        info.createdUtc = QDateTime(QDate(2026, 10, 28), QTime(14, 3, 22), QTimeZone::utc());
        const auto json = QJsonDocument::fromJson(ProblemReportBuilder::reportJson(info)).object();

        const QStringList keys{ "schema", "kind", "app", "app_version", "build", "platform", "os_version", "device_model",
            "locale", "created_utc", "server_host", "user_note" };
        for (const auto &key : keys) {
            QVERIFY2(json.contains(key), qPrintable(key));
        }
        QCOMPARE(json.size(), keys.size());
        QVERIFY(json.value(QStringLiteral("schema")).isDouble());
        QCOMPARE(json.value(QStringLiteral("schema")).toInt(), 1);
        QCOMPARE(json.value(QStringLiteral("kind")).toString(), QStringLiteral("unclean-exit"));
        QCOMPARE(json.value(QStringLiteral("app")).toString(), QStringLiteral("NoDrive Desktop"));
        QVERIFY(json.value(QStringLiteral("app_version")).toString().contains(QStringLiteral("nourax")));
        QVERIFY(!json.value(QStringLiteral("build")).toString().isEmpty());
        QVERIFY(QStringList({ "windows", "macos", "linux" }).contains(json.value(QStringLiteral("platform")).toString()));
        QVERIFY(!json.value(QStringLiteral("os_version")).toString().isEmpty());
        QVERIFY(!json.value(QStringLiteral("device_model")).toString().isEmpty());
        QVERIFY(!json.value(QStringLiteral("locale")).toString().isEmpty());
        QCOMPARE(json.value(QStringLiteral("created_utc")).toString(), QStringLiteral("2026-10-28T14:03:22Z"));
        QCOMPARE(json.value(QStringLiteral("server_host")).toString(), QStringLiteral("drive.example.org"));
        QVERIFY(json.value(QStringLiteral("user_note")).toString().contains(QStringLiteral("<email>")));
        QVERIFY(!json.value(QStringLiteral("user_note")).toString().contains(QStringLiteral("jane.doe")));
    }

    void buildsAZipWithLogsConfigAndNoSecrets()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto sources = sourcesIn(root.path());
        writeFile(sources.logDir + QStringLiteral("/20261028_1500_nodrive.log"),
            "2026-10-28 15:00:01 current session, Cookie: oc_sessionPassphrase=Zk8sLq3VnX0pTt4RbYw9UcHgJm2EaDf7; x=1\n");
        // The previous session's log is a .gz file.
        {
            KCompressionDevice gz(sources.logDir + QStringLiteral("/20261028_1400_nodrive.log.1.gz"), KCompressionDevice::GZip);
            QVERIFY(gz.open(QIODevice::WriteOnly));
            gz.write("2026-10-28 14:00:01 previous session for jane.doe@example.com, last line of the old log\n");
            gz.close();
        }
        writeFile(sources.configFile, "[General]\nlanguage=en\n[Proxy]\npass=hunter22\n");

        ProblemReportInfo info;
        info.kind = ReportKind::Crash;
        info.crashText = QStringLiteral("signal 11 (SIGSEGV)\nLast log lines before the fatal error:\nAuthorization: Basic YWRtaW46c2VjcmV0");
        info.userNote = QStringLiteral("clicked Sync");
        info.serverHost = QStringLiteral("drive.example.org");
        QString error;
        const auto path = ProblemReportBuilder::build(info, sources, root.path() + QStringLiteral("/pending"), &error);
        QVERIFY2(!path.isEmpty(), qPrintable(error));
        QVERIFY(QFileInfo(path).size() <= ProblemReportSettings::maxZipBytes);

        const auto entries = readZip(path);
        QVERIFY(entries.contains(QStringLiteral("report.json")));
        QVERIFY(entries.contains(QStringLiteral("crash.txt")));
        QVERIFY(entries.contains(QStringLiteral("logs/20261028_1500_nodrive.log")));
        QVERIFY2(entries.contains(QStringLiteral("logs/20261028_1400_nodrive.log.1")), qPrintable(entries.keys().join(',')));
        QVERIFY(entries.contains(QStringLiteral("config/nodrive.cfg")));
        QVERIFY(entries.contains(QStringLiteral("_client_buildinfo.txt")));
        QVERIFY(entries.contains(QStringLiteral("_client_parameters.txt")));
        // The .gz log was decompressed.
        QVERIFY(entries.value(QStringLiteral("logs/20261028_1400_nodrive.log.1")).contains("last line of the old log"));
        QVERIFY(entries.value(QStringLiteral("crash.txt")).contains("SIGSEGV"));
        QCOMPARE(QJsonDocument::fromJson(entries.value(QStringLiteral("report.json"))).object().value(QStringLiteral("kind")).toString(),
            QStringLiteral("crash"));

        for (auto it = entries.cbegin(); it != entries.cend(); ++it) {
            const auto &data = it.value();
            QVERIFY2(!data.contains("Zk8sLq3VnX0pTt4RbYw9UcHgJm2EaDf7"), qPrintable(it.key()));
            QVERIFY2(!data.contains("hunter22"), qPrintable(it.key()));
            QVERIFY2(!data.contains("jane.doe@example.com"), qPrintable(it.key()));
            QVERIFY2(!data.contains("YWRtaW46c2VjcmV0"), qPrintable(it.key()));
        }
        QVERIFY(entries.value(QStringLiteral("logs/20261028_1400_nodrive.log.1")).contains("<email>"));
    }

    void manualReportHasNoCrashText()
    {
        QTemporaryDir root;
        const auto sources = sourcesIn(root.path());
        writeFile(sources.logDir + QStringLiteral("/a.log"), "line\n");
        ProblemReportInfo info;
        info.kind = ReportKind::Manual;
        const auto path = ProblemReportBuilder::build(info, sources, root.path() + QStringLiteral("/pending"));
        QVERIFY(!path.isEmpty());
        QVERIFY(!readZip(path).contains(QStringLiteral("crash.txt")));
    }

    void bigLogsAreCutToFiveMegabytesOldestFirst()
    {
        QTemporaryDir root;
        const auto sources = sourcesIn(root.path());
        QRandomGenerator rng(1028);
        const QString oldFile = sources.logDir + QStringLiteral("/20261027_1000_nodrive.log");
        const QString newFile = sources.logDir + QStringLiteral("/20261028_1500_nodrive.log");
        for (const auto &file : { oldFile, newFile }) {
            QByteArray data;
            if (file == oldFile) {
                data += "OLDEST-LINE-MARKER\n";
            }
            for (int i = 0; i < 50000; ++i) {
                data += randomLine(rng, i).toLatin1();
                data += '\n';
            }
            if (file == newFile) {
                data += "NEWEST-LINE-MARKER\n";
            }
            writeFile(file, data);
        }
        setModified(oldFile, QDateTime::currentDateTimeUtc().addDays(-1));
        setModified(newFile, QDateTime::currentDateTimeUtc());
        QVERIFY(QFileInfo(oldFile).size() + QFileInfo(newFile).size() > 10 * 1000 * 1000);

        ProblemReportInfo info;
        info.kind = ReportKind::Crash;
        info.crashText = QStringLiteral("signal 6 (SIGABRT)");
        QString error;
        const auto path = ProblemReportBuilder::build(info, sources, root.path() + QStringLiteral("/pending"), &error);
        QVERIFY2(!path.isEmpty(), qPrintable(error));
        QVERIFY2(QFileInfo(path).size() <= 5'000'000, qPrintable(QString::number(QFileInfo(path).size())));

        const auto entries = readZip(path);
        QVERIFY(entries.contains(QStringLiteral("report.json")));
        QVERIFY(entries.contains(QStringLiteral("crash.txt")));
        QByteArray allLogs;
        for (auto it = entries.cbegin(); it != entries.cend(); ++it) {
            if (it.key().startsWith(QStringLiteral("logs/"))) {
                allLogs += it.value();
            }
        }
        QVERIFY(allLogs.contains("NEWEST-LINE-MARKER"));
        QVERIFY(!allLogs.contains("OLDEST-LINE-MARKER"));
    }

    void onlyTheNewestFiveReportsAreKept()
    {
        QTemporaryDir root;
        const auto sources = sourcesIn(root.path());
        writeFile(sources.logDir + QStringLiteral("/a.log"), "line\n");
        const QString pending = root.path() + QStringLiteral("/pending");
        const auto start = QDateTime(QDate(2026, 10, 28), QTime(10, 0, 0), QTimeZone::utc());
        for (int i = 0; i < 7; ++i) {
            ProblemReportInfo info;
            info.createdUtc = start.addSecs(i * 60);
            const auto path = ProblemReportBuilder::build(info, sources, pending);
            QVERIFY(!path.isEmpty());
        }
        // Seven reports were made, the oldest two are gone.
        const auto zips = zipsIn(pending);
        QCOMPARE(zips.size(), 5);
        QVERIFY(zips.first().startsWith(QStringLiteral("2026-10-28_10-02-00_")));
        QVERIFY(zips.last().startsWith(QStringLiteral("2026-10-28_10-06-00_")));
    }

    void sameSecondGetsANumberedName()
    {
        QTemporaryDir root;
        const auto sources = sourcesIn(root.path());
        writeFile(sources.logDir + QStringLiteral("/a.log"), "line\n");
        const QString pending = root.path() + QStringLiteral("/pending");
        ProblemReportInfo info;
        info.createdUtc = QDateTime(QDate(2026, 10, 28), QTime(10, 0, 0), QTimeZone::utc());
        const auto first = ProblemReportBuilder::build(info, sources, pending);
        const auto second = ProblemReportBuilder::build(info, sources, pending);
        QVERIFY(!first.isEmpty());
        QVERIFY(!second.isEmpty());
        QVERIFY(first != second);
        QVERIFY(second.endsWith(QStringLiteral("_2.zip")));
        QVERIFY(QFile::exists(first));
    }

    // ---- run marker ----

    void noMarkerMeansNothing()
    {
        QTemporaryDir dir;
        RunMarker marker(dir.path(), QStringLiteral("35.0.0.1"));
        QCOMPARE(marker.inspectPrevious().run, RunMarker::PreviousRun::None);
    }

    void cleanExitIsNotAProblem()
    {
        QTemporaryDir dir;
        RunMarker first(dir.path(), QStringLiteral("35.0.0.1"));
        QVERIFY(first.markRunning());
        QVERIFY(first.markClean());
        RunMarker second(dir.path(), QStringLiteral("35.0.0.1"));
        QCOMPARE(second.inspectPrevious().run, RunMarker::PreviousRun::Clean);
    }

    void aRunThatNeverQuitIsUncleanExit()
    {
        QTemporaryDir dir;
        RunMarker first(dir.path(), QStringLiteral("35.0.0.1"));
        QVERIFY(first.markRunning());
        RunMarker second(dir.path(), QStringLiteral("35.0.0.1"));
        QCOMPARE(second.inspectPrevious().run, RunMarker::PreviousRun::Unclean);
    }

    void anotherBuildIsAnUpdateNotAProblem()
    {
        QTemporaryDir dir;
        RunMarker first(dir.path(), QStringLiteral("35.0.0.1"));
        QVERIFY(first.markRunning());
        RunMarker second(dir.path(), QStringLiteral("35.0.0.2"));
        QCOMPARE(second.inspectPrevious().run, RunMarker::PreviousRun::OtherBuild);
    }

    void crashMarkerOrFatalLogMeansCrash()
    {
        QTemporaryDir dir;
        const QString fatalLog = dir.path() + QStringLiteral("/fatal-crash.log");
        RunMarker first(dir.path(), QStringLiteral("35.0.0.1"), fatalLog);
        QVERIFY(first.markRunning());
        writeFile(first.crashMarkerPath(), "signal 11 (SIGSEGV)\n");
        auto previous = RunMarker(dir.path(), QStringLiteral("35.0.0.1"), fatalLog).inspectPrevious();
        QCOMPARE(previous.run, RunMarker::PreviousRun::Crash);
        QVERIFY(previous.crashText.contains(QStringLiteral("SIGSEGV")));

        // The qFatal crash log alone is evidence too.
        QFile::remove(first.crashMarkerPath());
        writeFile(fatalLog, "the last log lines\n");
        previous = RunMarker(dir.path(), QStringLiteral("35.0.0.1"), fatalLog).inspectPrevious();
        QCOMPARE(previous.run, RunMarker::PreviousRun::Crash);
        QVERIFY(previous.crashText.contains(QStringLiteral("the last log lines")));
    }

    void anOldCrashMarkerFromEarlierIsIgnored()
    {
        QTemporaryDir dir;
        RunMarker first(dir.path(), QStringLiteral("35.0.0.1"));
        QVERIFY(first.markRunning());
        writeFile(first.crashMarkerPath(), "signal 11 (SIGSEGV)\n");
        setModified(first.crashMarkerPath(), QDateTime::currentDateTimeUtc().addDays(-2));
        QCOMPARE(RunMarker(dir.path(), QStringLiteral("35.0.0.1")).inspectPrevious().run, RunMarker::PreviousRun::Unclean);
    }

    void aCaseIsAskedOnlyOnce()
    {
        QTemporaryDir dir;
        RunMarker session1(dir.path(), QStringLiteral("35.0.0.1"));
        QVERIFY(session1.markRunning());
        writeFile(session1.crashMarkerPath(), "signal 11 (SIGSEGV)\n");

        // Session 2 starts: it sees the crash, then closes the case (answer or automatic send).
        RunMarker session2(dir.path(), QStringLiteral("35.0.0.1"));
        QCOMPARE(session2.inspectPrevious().run, RunMarker::PreviousRun::Crash);
        session2.markRunning();
        session2.closeCase();
        QVERIFY(session2.markClean());

        // Session 3 starts: nothing to ask.
        QCOMPARE(RunMarker(dir.path(), QStringLiteral("35.0.0.1")).inspectPrevious().run, RunMarker::PreviousRun::Clean);
    }

    void closingTheCaseDoesNotReportTheSameCrashAgain()
    {
        QTemporaryDir dir;
        RunMarker session1(dir.path(), QStringLiteral("35.0.0.1"));
        QVERIFY(session1.markRunning());
        writeFile(session1.crashMarkerPath(), "signal 11 (SIGSEGV)\n");
        RunMarker session2(dir.path(), QStringLiteral("35.0.0.1"));
        session2.markRunning();
        session2.closeCase();
        // Session 2 itself ends unclean (no crash evidence): a new case, not the old crash.
        QCOMPARE(RunMarker(dir.path(), QStringLiteral("35.0.0.1")).inspectPrevious().run, RunMarker::PreviousRun::Unclean);
    }

    // ---- real crashes of a helper process (this executable, started with --crash-helper) ----

    void aSegmentationFaultLeavesACrashMarker()
    {
        QTemporaryDir dir;
        QProcess helper;
        helper.start(QCoreApplication::applicationFilePath(), { QStringLiteral("--crash-helper"), dir.path(), QStringLiteral("segv") });
        QVERIFY(helper.waitForFinished(60000));
        QVERIFY(helper.exitStatus() == QProcess::CrashExit || helper.exitCode() != 0);

        QFile marker(dir.path() + QStringLiteral("/crash.marker"));
        QVERIFY2(marker.open(QIODevice::ReadOnly), "no crash.marker was written");
        const auto text = QString::fromUtf8(marker.readAll());
#ifdef Q_OS_WIN
        QVERIFY2(text.contains(QStringLiteral("exception 0xC0000005")), qPrintable(text));
#else
        QVERIFY2(text.contains(QStringLiteral("SIGSEGV")) || text.contains(QStringLiteral("SIGBUS")) || text.contains(QStringLiteral("SIGILL")),
            qPrintable(text));
#endif
        QCOMPARE(RunMarker(dir.path(), QStringLiteral("35.0.0.1")).inspectPrevious().run, RunMarker::PreviousRun::Crash);
    }

    void anAbortLeavesACrashMarker()
    {
        QTemporaryDir dir;
        QProcess helper;
        helper.start(QCoreApplication::applicationFilePath(), { QStringLiteral("--crash-helper"), dir.path(), QStringLiteral("abort") });
        QVERIFY(helper.waitForFinished(60000));

        QFile marker(dir.path() + QStringLiteral("/crash.marker"));
        QVERIFY2(marker.open(QIODevice::ReadOnly), "no crash.marker was written");
        QVERIFY(QString::fromUtf8(marker.readAll()).contains(QStringLiteral("SIGABRT")));
        QCOMPARE(RunMarker(dir.path(), QStringLiteral("35.0.0.1")).inspectPrevious().run, RunMarker::PreviousRun::Crash);
    }

    void sigtermIsACleanEndOnUnix()
    {
#ifdef Q_OS_WIN
        QSKIP("SIGTERM is a Unix signal");
#else
        QTemporaryDir dir;
        QProcess helper;
        helper.start(QCoreApplication::applicationFilePath(), { QStringLiteral("--crash-helper"), dir.path(), QStringLiteral("wait") });
        QVERIFY(helper.waitForStarted(30000));
        QVERIFY(helper.waitForReadyRead(30000)); // "ready": the handlers are installed
        helper.terminate();
        QVERIFY(helper.waitForFinished(30000));

        QVERIFY(!QFileInfo::exists(dir.path() + QStringLiteral("/crash.marker")));
        QCOMPARE(RunMarker(dir.path(), QStringLiteral("35.0.0.1")).inspectPrevious().run, RunMarker::PreviousRun::Clean);
#endif
    }
};

int main(int argc, char **argv)
{
    if (argc >= 4 && QByteArray(argv[1]) == "--crash-helper") {
        return crashHelperMain(argc, argv);
    }
    QCoreApplication app(argc, argv);
    TestNoDriveProblemReport test;
    return QTest::qExec(&test, argc, argv);
}

#include "testnodriveproblemreport.moc"
