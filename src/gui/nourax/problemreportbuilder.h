/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoDrive problem reports: makes the report zip (report.json, crash.txt, logs/, settings).
// Everything is scrubbed before it is zipped. The zip is at most 5,000,000 bytes.

#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

namespace OCC::Nourax {

enum class ReportKind {
    Crash,
    Manual,
    UncleanExit,
};

QString reportKindToString(ReportKind kind);

struct ProblemReportInfo {
    ReportKind kind = ReportKind::Manual;
    QString userNote;
    /// For ReportKind::Crash: the crash marker line and the qFatal log (not scrubbed yet).
    QString crashText;
    /// Host of the signed-in account, empty when there is none yet.
    QString serverHost;
    QDateTime createdUtc = QDateTime::currentDateTimeUtc();
};

/// Where the report content comes from. The same sources as "Create Debug Archive", minus the
/// binary databases (they cannot be scrubbed and can be very large).
struct ProblemReportSources {
    QString logDir;
    /// Single log files outside logDir (only when the app was started with --logfile).
    QStringList logFiles;
    /// The log file the running app writes to now (not "the previous session").
    QString currentLogFile;
    QString configFile;
    QString buildInfo;
    QStringList arguments;
    /// macOS File Provider: folder with *.jsonl log files, empty elsewhere.
    QString extraLogDir;

    /// The live app: Logger, ConfigFile, Theme, command line. Call it on the GUI thread.
    static ProblemReportSources live();
};

class ProblemReportBuilder
{
public:
    struct Entry {
        QString name;
        QByteArray data;
    };

    /// "windows", "macos" or "linux".
    static QString platformName();
    /// <YYYY-MM-DD_HH-MM-SS>_<platform>_<app_version>.zip (UTC).
    static QString zipFileName(const QDateTime &createdUtc);
    /// The report.json content.
    static QByteArray reportJson(const ProblemReportInfo &info);
    /// Best effort device model, never the host name.
    static QString deviceModel();

    /// Builds the zip into outDir and returns its path, or an empty string.
    /// Keeps only the newest ProblemReportSettings::maxPending zips there afterwards.
    static QString build(const ProblemReportInfo &info, const ProblemReportSources &sources, const QString &outDir,
        QString *errorString = nullptr);

    /// Deletes the app's own older report zips in dir, keeping the newest `keep`.
    static void prunePending(const QString &dir, int keep);

    /// Unique file name inside dir: name, then name_2, name_3, ...
    static QString uniqueZipPath(const QString &dir, const QString &zipName);

    /// Reads every file of logDir (".gz" is decompressed), newest first, scrubbed, at most maxBytes of text.
    static QList<Entry> collectLogs(const QString &logDir, qint64 maxBytes);

    /// The last `lines` lines of the newest log that is not `currentLogFile` (or of the newest one).
    static QString tailOfPreviousLog(const QString &logDir, const QString &currentLogFile, int lines);

private:
    static bool writeZip(const QString &path, const QList<Entry> &fixedEntries, const QList<Entry> &logEntries, QString *errorString);
};

} // namespace OCC::Nourax
