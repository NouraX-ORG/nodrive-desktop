/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "runmarker.h"

#include "version.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStringList>
#include <QTimeZone>

#include <csignal>
#include <cstring>
#include <cwchar>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace OCC::Nourax {

namespace {

const auto runMarkerName = QStringLiteral("run.marker");
const auto crashMarkerName = QStringLiteral("crash.marker");

// ---- Text helpers that are safe to call inside a signal handler (no allocation, no stdio). ----

constexpr int markerPathMax = 4096;

char *appendText(char *out, const char *text)
{
    while (*text) {
        *out++ = *text++;
    }
    return out;
}

char *appendNumber(char *out, long long value)
{
    if (value < 0) {
        *out++ = '-';
        value = -value;
    }
    char digits[24];
    int n = 0;
    do {
        digits[n++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value > 0 && n < 22);
    while (n > 0) {
        *out++ = digits[--n];
    }
    return out;
}

[[maybe_unused]] char *appendHex(char *out, unsigned long long value)
{
    static const char hex[] = "0123456789ABCDEF";
    char digits[20];
    int n = 0;
    do {
        digits[n++] = hex[value & 0xF];
        value >>= 4;
    } while (value > 0 && n < 18);
    *out++ = '0';
    *out++ = 'x';
    while (n > 0) {
        *out++ = digits[--n];
    }
    return out;
}

const char *signalName(int sig)
{
    switch (sig) {
    case SIGSEGV:
        return "SIGSEGV";
    case SIGILL:
        return "SIGILL";
    case SIGFPE:
        return "SIGFPE";
    case SIGABRT:
        return "SIGABRT";
#ifdef SIGBUS
    case SIGBUS:
        return "SIGBUS";
#endif
    default:
        return "signal";
    }
}

char *buildSignalMessage(char *buffer, int sig)
{
    char *out = appendText(buffer, "signal ");
    out = appendNumber(out, sig);
    out = appendText(out, " (");
    out = appendText(out, signalName(sig));
    out = appendText(out, ")\n");
    return out;
}

#ifdef Q_OS_WIN

wchar_t s_runMarkerPathW[markerPathMax] = {};
wchar_t s_crashMarkerPathW[markerPathMax] = {};
LPTOP_LEVEL_EXCEPTION_FILTER s_previousFilter = nullptr;

void writeFileW(const wchar_t *path, const char *data, size_t length)
{
    if (!path[0]) {
        return;
    }
    const HANDLE file = ::CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD written = 0;
    ::WriteFile(file, data, static_cast<DWORD>(length), &written, nullptr);
    ::CloseHandle(file);
}

LONG WINAPI onUnhandledException(EXCEPTION_POINTERS *info)
{
    char buffer[160];
    char *out = appendText(buffer, "exception ");
    out = appendHex(out, info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0);
    out = appendText(out, " at ");
    out = appendHex(out, info && info->ExceptionRecord ? reinterpret_cast<unsigned long long>(info->ExceptionRecord->ExceptionAddress) : 0);
    out = appendText(out, "\n");
    writeFileW(s_crashMarkerPathW, buffer, static_cast<size_t>(out - buffer));
    if (s_previousFilter) {
        return s_previousFilter(info);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void onAbortSignal(int sig)
{
    char buffer[64];
    char *out = buildSignalMessage(buffer, sig);
    writeFileW(s_crashMarkerPathW, buffer, static_cast<size_t>(out - buffer));
    // Return, do not re-raise with SIG_DFL: the MSVC raise() would then call _exit(3) and abort() would never
    // reach Windows Error Reporting (_CALL_REPORTFAULT / __fastfail). After this handler returns, abort() carries on as before.
}

#else // Unix

char s_runMarkerPath[markerPathMax] = {};
char s_crashMarkerPath[markerPathMax] = {};
const char s_cleanJson[] = "{\"state\":\"clean\"}";

void writeFileUnix(const char *path, const char *data, size_t length)
{
    if (!path[0]) {
        return;
    }
    const int fd = ::open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        return;
    }
    const auto written = ::write(fd, data, length);
    (void)written;
    ::close(fd);
}

// SIGTERM, SIGINT and SIGHUP end the app without Qt's clean-up (for example at logout on Linux).
// That is a normal end, so the marker is rewritten as "clean"; then the default action runs.
void onTerminateSignal(int sig)
{
    writeFileUnix(s_runMarkerPath, s_cleanJson, sizeof(s_cleanJson) - 1);
    std::signal(sig, SIG_DFL);
    ::raise(sig);
}

void onCrashSignal(int sig)
{
    char buffer[64];
    char *out = buildSignalMessage(buffer, sig);
    writeFileUnix(s_crashMarkerPath, buffer, static_cast<size_t>(out - buffer));
    std::signal(sig, SIG_DFL);
    ::raise(sig);
}

char s_alternateStack[1 << 15];

void installUnixHandler(int sig, void (*handler)(int), bool onAlternateStack)
{
    struct sigaction action;
    std::memset(&action, 0, sizeof(action));
    action.sa_handler = handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESETHAND;
    if (onAlternateStack) {
        action.sa_flags |= SA_ONSTACK;
    }
    sigaction(sig, &action, nullptr);
}

#endif

} // namespace

RunMarker::RunMarker(const QString &dir, const QString &build, const QString &fatalLogPath)
    : _dir(dir)
    , _build(build)
    , _fatalLogPath(fatalLogPath)
{
}

QString RunMarker::currentBuild()
{
    return QString::fromLatin1(MIRALL_STRINGIFY(MIRALL_VERSION_FULL));
}

QString RunMarker::runMarkerPath() const
{
    return _dir + QLatin1Char('/') + runMarkerName;
}

QString RunMarker::crashMarkerPath() const
{
    return _dir + QLatin1Char('/') + crashMarkerName;
}

RunMarker::Previous RunMarker::inspectPrevious() const
{
    Previous result;

    QFile file(runMarkerPath());
    if (!file.open(QIODevice::ReadOnly)) {
        return result;
    }
    const auto json = QJsonDocument::fromJson(file.readAll()).object();
    file.close();

    const auto state = json.value(QStringLiteral("state")).toString();
    if (state == QLatin1String("clean")) {
        result.run = PreviousRun::Clean;
        return result;
    }
    if (state != QLatin1String("running")) {
        return result; // unreadable marker: nothing to report
    }
    if (json.value(QStringLiteral("build")).toString() != _build) {
        result.run = PreviousRun::OtherBuild;
        return result;
    }

    const auto started = QDateTime::fromString(json.value(QStringLiteral("started_utc")).toString(), Qt::ISODateWithMs).toUTC();
    const auto notBefore = started.isValid() ? started.addSecs(-5) : QDateTime::fromSecsSinceEpoch(0, QTimeZone::utc());

    QStringList evidence;
    QFile crashFile(crashMarkerPath());
    if (crashFile.exists() && crashFile.size() > 0 && QFileInfo(crashFile).lastModified().toUTC() >= notBefore) {
        if (crashFile.open(QIODevice::ReadOnly)) {
            evidence << QString::fromUtf8(crashFile.readAll()).trimmed();
            crashFile.close();
        }
    }
    if (!_fatalLogPath.isEmpty()) {
        QFile fatalLog(_fatalLogPath);
        if (fatalLog.exists() && QFileInfo(fatalLog).lastModified().toUTC() >= notBefore && fatalLog.open(QIODevice::ReadOnly)) {
            evidence << QStringLiteral("Last log lines before the fatal error:") << QString::fromUtf8(fatalLog.read(256 * 1024));
            fatalLog.close();
        }
    }

    if (evidence.isEmpty()) {
        result.run = PreviousRun::Unclean;
    } else {
        result.run = PreviousRun::Crash;
        result.crashText = evidence.join(QLatin1Char('\n'));
    }
    return result;
}

bool RunMarker::writeState(const QString &state)
{
    if (!QDir().mkpath(_dir)) {
        return false;
    }
    QJsonObject json;
    json.insert(QStringLiteral("state"), state);
    if (state == QLatin1String("running")) {
        json.insert(QStringLiteral("pid"), static_cast<qint64>(QCoreApplication::applicationPid()));
        json.insert(QStringLiteral("started_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
        json.insert(QStringLiteral("build"), _build);
    }
    QSaveFile file(runMarkerPath());
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QJsonDocument(json).toJson(QJsonDocument::Compact));
    return file.commit();
}

bool RunMarker::markRunning()
{
    return writeState(QStringLiteral("running"));
}

bool RunMarker::markClean()
{
    return writeState(QStringLiteral("clean"));
}

void RunMarker::closeCase()
{
    markRunning();
    QFile crash(crashMarkerPath());
    if (crash.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        crash.close();
    }
}

void RunMarker::installHandlers()
{
#ifdef Q_OS_WIN
    const auto runPath = QDir::toNativeSeparators(runMarkerPath()).toStdWString();
    const auto crashPath = QDir::toNativeSeparators(crashMarkerPath()).toStdWString();
    if (runPath.size() >= markerPathMax || crashPath.size() >= markerPathMax) {
        return;
    }
    std::wcsncpy(s_runMarkerPathW, runPath.c_str(), markerPathMax - 1);
    std::wcsncpy(s_crashMarkerPathW, crashPath.c_str(), markerPathMax - 1);
    s_previousFilter = ::SetUnhandledExceptionFilter(&onUnhandledException);
    std::signal(SIGABRT, onAbortSignal);
#else
    const auto runPath = QFile::encodeName(runMarkerPath());
    const auto crashPath = QFile::encodeName(crashMarkerPath());
    if (runPath.size() >= markerPathMax || crashPath.size() >= markerPathMax) {
        return;
    }
    std::strncpy(s_runMarkerPath, runPath.constData(), markerPathMax - 1);
    std::strncpy(s_crashMarkerPath, crashPath.constData(), markerPathMax - 1);

    // A stack overflow needs its own stack to run the handler.
    stack_t stack;
    stack.ss_sp = s_alternateStack;
    stack.ss_size = sizeof(s_alternateStack);
    stack.ss_flags = 0;
    sigaltstack(&stack, nullptr);

    for (const int sig : {SIGSEGV, SIGILL, SIGFPE, SIGABRT
#ifdef SIGBUS
             , SIGBUS
#endif
         }) {
        installUnixHandler(sig, onCrashSignal, true);
    }
    for (const int sig : {SIGTERM, SIGINT, SIGHUP}) {
        installUnixHandler(sig, onTerminateSignal, false);
    }
#endif
}

} // namespace OCC::Nourax
