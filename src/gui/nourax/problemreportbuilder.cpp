/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "problemreportbuilder.h"

#include "problemreportscrubber.h"
#include "problemreportsettings.h"

#include "configfile.h"
#include "logger.h"
#include "theme.h"
#include "version.h"

#ifdef BUILD_FILE_PROVIDER_MODULE
#include "macOS/fileproviderutils.h"
#endif

#include <KCompressionDevice>
#include <KZip>

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QLoggingCategory>
#include <QSettings>
#include <QSysInfo>

#include <algorithm>

#ifdef Q_OS_MACOS
#include <sys/sysctl.h>
#include <sys/types.h>
#endif

namespace OCC::Nourax {

Q_LOGGING_CATEGORY(lcProblemReportBuilder, "nodrive.problemreports.builder", QtInfoMsg)

namespace {

constexpr qint64 maxLogTextBytes = 40LL * 1024 * 1024;
constexpr int maxTrimRounds = 12;

// Reads a log file as text. ".gz" files (the previous sessions) are decompressed.
// If more than maxBytes are available, the newest part (the end) is kept.
QByteArray readLogFile(const QFileInfo &info, qint64 maxBytes)
{
    QByteArray data;
    if (info.suffix().compare(QLatin1String("gz"), Qt::CaseInsensitive) == 0) {
        KCompressionDevice device(info.absoluteFilePath(), KCompressionDevice::GZip);
        if (!device.open(QIODevice::ReadOnly)) {
            return data;
        }
        // The decompressed size is unknown: read in blocks and keep the end.
        while (true) {
            const auto block = device.read(1024 * 1024);
            if (block.isEmpty()) {
                break;
            }
            data.append(block);
            if (data.size() > maxBytes * 2) {
                data.remove(0, data.size() - maxBytes);
            }
        }
        if (data.size() > maxBytes) {
            data.remove(0, data.size() - maxBytes);
        }
    } else {
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) {
            return data;
        }
        if (file.size() > maxBytes) {
            file.seek(file.size() - maxBytes);
        }
        data = file.readAll();
    }
    if (!data.isEmpty() && data.size() >= maxBytes) {
        // Start at a line boundary.
        const auto newline = data.indexOf('\n');
        if (newline >= 0) {
            data.remove(0, newline + 1);
        }
    }
    return data;
}

QByteArray scrubbedUtf8(const QByteArray &raw)
{
    return ProblemReportScrubber::scrubText(QString::fromUtf8(raw)).toUtf8();
}

QByteArray readFileLimited(const QString &path, qint64 maxBytes)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    if (file.size() > maxBytes) {
        file.seek(file.size() - maxBytes);
    }
    return file.readAll();
}

QString lastLines(const QString &text, int lines)
{
    auto pos = text.size();
    int found = 0;
    while (pos > 0 && found <= lines) {
        pos = text.lastIndexOf(QLatin1Char('\n'), pos - 1);
        if (pos < 0) {
            return text;
        }
        ++found;
    }
    return text.mid(pos + 1);
}

} // namespace

QString reportKindToString(ReportKind kind)
{
    switch (kind) {
    case ReportKind::Crash:
        return QStringLiteral("crash");
    case ReportKind::UncleanExit:
        return QStringLiteral("unclean-exit");
    case ReportKind::Manual:
        break;
    }
    return QStringLiteral("manual");
}

ProblemReportSources ProblemReportSources::live()
{
    ProblemReportSources sources;
    const ConfigFile cfg;
    const auto logger = Logger::instance();
    sources.currentLogFile = logger->logFile();
    sources.logDir = logger->logDir();
    if (sources.logDir.isEmpty() && !sources.currentLogFile.isEmpty()) {
        sources.logDir = QFileInfo(sources.currentLogFile).absolutePath();
    }
    sources.configFile = cfg.configFile();
    sources.buildInfo = Theme::instance()->aboutInfo() + QStringLiteral("\n\n") + Theme::instance()->aboutDetails();
    sources.arguments = QCoreApplication::arguments();
#ifdef BUILD_FILE_PROVIDER_MODULE
    const auto domains = Mac::FileProviderUtils::fileProviderDomainsSupportDirectory();
    if (domains.exists()) {
        sources.extraLogDir = domains.path();
    }
#endif
    return sources;
}

QString ProblemReportBuilder::platformName()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#else
    return QStringLiteral("linux");
#endif
}

QString ProblemReportBuilder::zipFileName(const QDateTime &createdUtc)
{
    return QStringLiteral("%1_%2_%3.zip")
        .arg(createdUtc.toUTC().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss")),
            platformName(),
            QString::fromLatin1(MIRALL_HUMAN_VERSION_STRING));
}

QString ProblemReportBuilder::deviceModel()
{
    QString model;
#if defined(Q_OS_WIN)
    QSettings bios(QStringLiteral("HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\BIOS"), QSettings::NativeFormat);
    model = bios.value(QStringLiteral("SystemProductName")).toString();
#elif defined(Q_OS_MACOS)
    char buffer[256] = {};
    size_t length = sizeof(buffer);
    if (sysctlbyname("hw.model", buffer, &length, nullptr, 0) == 0) {
        model = QString::fromUtf8(buffer);
    }
#else
    QFile file(QStringLiteral("/sys/devices/virtual/dmi/id/product_name"));
    if (file.open(QIODevice::ReadOnly)) {
        model = QString::fromUtf8(file.read(256));
    }
#endif
    model = model.trimmed();
    if (model.isEmpty()) {
        model = QSysInfo::currentCpuArchitecture();
    }
    return model;
}

QByteArray ProblemReportBuilder::reportJson(const ProblemReportInfo &info)
{
    QJsonObject json;
    json.insert(QStringLiteral("schema"), 1);
    json.insert(QStringLiteral("kind"), reportKindToString(info.kind));
    json.insert(QStringLiteral("app"), QStringLiteral("NoDrive Desktop"));
    json.insert(QStringLiteral("app_version"), QString::fromLatin1(MIRALL_HUMAN_VERSION_STRING));
    json.insert(QStringLiteral("build"), QString::number(MIRALL_VERSION_BUILD));
    json.insert(QStringLiteral("platform"), platformName());
    json.insert(QStringLiteral("os_version"), ProblemReportScrubber::scrubText(QSysInfo::prettyProductName()));
    json.insert(QStringLiteral("device_model"), ProblemReportScrubber::scrubText(deviceModel()));
    json.insert(QStringLiteral("locale"), QLocale::system().bcp47Name());
    json.insert(QStringLiteral("created_utc"), info.createdUtc.toUTC().toString(Qt::ISODate));
    json.insert(QStringLiteral("server_host"), info.serverHost);
    json.insert(QStringLiteral("user_note"), ProblemReportScrubber::scrubText(info.userNote));
    return QJsonDocument(json).toJson(QJsonDocument::Indented);
}

QList<ProblemReportBuilder::Entry> ProblemReportBuilder::collectLogs(const QString &logDir, qint64 maxBytes)
{
    QList<Entry> result;
    if (logDir.isEmpty()) {
        return result;
    }
    const QDir dir(logDir);
    const auto files = dir.entryInfoList(QDir::Files | QDir::Readable, QDir::Time);
    qint64 remaining = maxBytes;
    QStringList used;
    for (const auto &info : files) {
        if (remaining <= 0) {
            break;
        }
        const auto raw = readLogFile(info, remaining);
        if (raw.isEmpty()) {
            continue;
        }
        remaining -= raw.size();
        auto name = info.fileName();
        if (info.suffix().compare(QLatin1String("gz"), Qt::CaseInsensitive) == 0) {
            name.chop(3);
        }
        while (used.contains(name)) {
            name += QLatin1String("_dup");
        }
        used << name;
        result.append({ QStringLiteral("logs/") + name, scrubbedUtf8(raw) });
    }
    return result;
}

QString ProblemReportBuilder::tailOfPreviousLog(const QString &logDir, const QString &currentLogFile, int lines)
{
    if (logDir.isEmpty()) {
        return {};
    }
    const QDir dir(logDir);
    const auto files = dir.entryInfoList(QDir::Files | QDir::Readable, QDir::Time);
    const auto current = QFileInfo(currentLogFile).fileName();
    QFileInfo chosen;
    for (const auto &info : files) {
        if (!currentLogFile.isEmpty() && info.fileName() == current) {
            continue;
        }
        chosen = info;
        break;
    }
    if (chosen.filePath().isEmpty() && !files.isEmpty()) {
        chosen = files.first();
    }
    if (chosen.filePath().isEmpty()) {
        return {};
    }
    const auto text = QString::fromUtf8(readLogFile(chosen, 2 * 1024 * 1024));
    return lastLines(text, lines);
}

bool ProblemReportBuilder::writeZip(const QString &path, const QList<Entry> &fixedEntries, const QList<Entry> &logEntries, QString *errorString)
{
    KZip zip(path);
    if (!zip.open(QIODevice::WriteOnly)) {
        if (errorString) {
            *errorString = zip.errorString();
        }
        return false;
    }
    bool ok = true;
    const auto add = [&zip, &ok](const Entry &entry) {
        ok = zip.writeFile(entry.name, entry.data) && ok;
    };
    std::for_each(fixedEntries.cbegin(), fixedEntries.cend(), add);
    std::for_each(logEntries.cbegin(), logEntries.cend(), add);
    ok = zip.close() && ok;
    if (!ok && errorString) {
        *errorString = zip.errorString();
    }
    return ok;
}

QString ProblemReportBuilder::uniqueZipPath(const QString &dir, const QString &zipName)
{
    QString path = dir + QLatin1Char('/') + zipName;
    if (!QFile::exists(path)) {
        return path;
    }
    const auto base = zipName.left(zipName.size() - 4);
    for (int i = 2; i < 1000; ++i) {
        path = QStringLiteral("%1/%2_%3.zip").arg(dir, base, QString::number(i));
        if (!QFile::exists(path)) {
            return path;
        }
    }
    return path;
}

void ProblemReportBuilder::prunePending(const QString &dir, int keep)
{
    const QDir d(dir);
    auto files = d.entryInfoList(QStringList{ QStringLiteral("*.zip") }, QDir::Files, QDir::NoSort);
    std::sort(files.begin(), files.end(), [](const QFileInfo &a, const QFileInfo &b) {
        if (a.lastModified() != b.lastModified()) {
            return a.lastModified() > b.lastModified();
        }
        return a.fileName() > b.fileName();
    });
    // Only the app's own report files in its own pending folder are ever removed here.
    for (qsizetype i = keep; i < files.size(); ++i) {
        QFile::remove(files.at(i).absoluteFilePath());
    }
}

QString ProblemReportBuilder::build(const ProblemReportInfo &info, const ProblemReportSources &sources, const QString &outDir,
    QString *errorString)
{
    if (!QDir().mkpath(outDir)) {
        if (errorString) {
            *errorString = QStringLiteral("cannot create %1").arg(outDir);
        }
        return {};
    }

    QList<Entry> fixed;
    fixed.append({ QStringLiteral("report.json"), reportJson(info) });

    if (info.kind == ReportKind::Crash) {
        auto text = info.crashText;
        const auto tail = tailOfPreviousLog(sources.logDir, sources.currentLogFile, 200);
        if (!tail.isEmpty()) {
            text += QStringLiteral("\n\nLast log lines of the previous session:\n") + tail;
        }
        fixed.append({ QStringLiteral("crash.txt"), scrubbedUtf8(text.toUtf8()) });
    }

    if (!sources.configFile.isEmpty() && QFile::exists(sources.configFile)) {
        const auto config = readFileLimited(sources.configFile, 1024 * 1024);
        fixed.append({ QStringLiteral("config/") + QFileInfo(sources.configFile).fileName(), ProblemReportScrubber::scrubConfig(config) });
    }
    if (!sources.buildInfo.isEmpty()) {
        fixed.append({ QStringLiteral("_client_buildinfo.txt"), scrubbedUtf8(sources.buildInfo.toUtf8()) });
    }
    if (!sources.arguments.isEmpty()) {
        fixed.append({ QStringLiteral("_client_parameters.txt"), ProblemReportScrubber::scrubArguments(sources.arguments).toUtf8() });
    }
    if (!sources.extraLogDir.isEmpty()) {
        const QDir root(sources.extraLogDir);
        QDirIterator it(sources.extraLogDir, QStringList{ QStringLiteral("*.jsonl") }, QDir::Files | QDir::NoDotAndDotDot,
            QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const auto path = it.next();
            const auto data = readFileLimited(path, 2 * 1024 * 1024);
            if (!data.isEmpty()) {
                fixed.append({ QStringLiteral("File Provider Domains/") + root.relativeFilePath(path), scrubbedUtf8(data) });
            }
        }
    }

    auto logs = collectLogs(sources.logDir, maxLogTextBytes);

    const auto finalPath = uniqueZipPath(outDir, zipFileName(info.createdUtc));
    const QString tempPath = outDir + QStringLiteral("/.building-%1.tmp").arg(QCoreApplication::applicationPid());
    QFile::remove(tempPath);

    const qint64 limit = ProblemReportSettings::maxZipBytes;
    const qint64 target = limit - limit / 33; // about 3 % below the limit
    for (int round = 0; round < maxTrimRounds; ++round) {
        QString zipError;
        if (!writeZip(tempPath, fixed, logs, &zipError)) {
            QFile::remove(tempPath);
            if (errorString) {
                *errorString = zipError;
            }
            return {};
        }
        const auto size = QFileInfo(tempPath).size();
        if (size <= limit) {
            if (!QFile::rename(tempPath, finalPath)) {
                QFile::remove(tempPath);
                if (errorString) {
                    *errorString = QStringLiteral("cannot move the report to %1").arg(finalPath);
                }
                return {};
            }
            prunePending(outDir, ProblemReportSettings::maxPending);
            qCInfo(lcProblemReportBuilder) << "report written:" << QFileInfo(finalPath).fileName() << size << "bytes after" << round << "trim round(s)";
            return finalPath;
        }

        // Too large: drop the oldest log lines first (the logs are ordered newest first).
        qint64 rawLogBytes = 0;
        for (const auto &log : std::as_const(logs)) {
            rawLogBytes += log.data.size();
        }
        if (rawLogBytes == 0) {
            break; // the rest of the report alone is too large; never cut report.json or crash.txt
        }
        const double excessFraction = double(size - target) / double(size);
        auto removeBytes = qint64(double(rawLogBytes) * excessFraction * 1.15) + 1;
        while (removeBytes > 0 && !logs.isEmpty()) {
            auto &oldest = logs.last();
            if (oldest.data.size() <= removeBytes) {
                removeBytes -= oldest.data.size();
                logs.removeLast();
            } else {
                auto cut = oldest.data.indexOf('\n', int(removeBytes));
                cut = cut < 0 ? oldest.data.size() : cut + 1;
                oldest.data.remove(0, cut);
                removeBytes = 0;
            }
        }
    }

    QFile::remove(tempPath);
    if (errorString) {
        *errorString = QStringLiteral("the report is larger than %1 bytes even without logs").arg(limit);
    }
    return {};
}

} // namespace OCC::Nourax
