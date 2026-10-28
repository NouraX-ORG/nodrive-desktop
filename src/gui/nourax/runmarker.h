/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoDrive problem reports: finds out whether the last run of the app ended cleanly.
//
// The app writes a small "running" marker at start and rewrites it as "clean" when it
// quits. Markers are only ever overwritten, never deleted. Fatal signals (Unix) and
// unhandled exceptions (Windows) additionally write a crash marker; the original
// handler still runs afterwards, so the operating system's own crash handling is unchanged.

#pragma once

#include <QDateTime>
#include <QString>

namespace OCC::Nourax {

class RunMarker
{
public:
    enum class PreviousRun {
        None, ///< no marker yet (first start with this feature)
        Clean, ///< the last run quit normally
        Unclean, ///< the last run did not quit normally (power loss, killed, ...)
        Crash, ///< the last run crashed and left evidence
        OtherBuild, ///< the last run was another build (an update kills the app), so it does not count
    };

    struct Previous {
        PreviousRun run = PreviousRun::None;
        /// Crash marker line and the qFatal crash log, not scrubbed yet. Only for PreviousRun::Crash.
        QString crashText;
    };

    /// @param dir folder for run.marker and crash.marker (created when needed)
    /// @param build the build of this app, for example "35.0.0.47"
    /// @param fatalLogPath file that Logger::dumpCrashLog() writes on qFatal, may be empty
    explicit RunMarker(const QString &dir, const QString &build, const QString &fatalLogPath = {});

    /// The build of this app (MIRALL_VERSION_FULL).
    static QString currentBuild();

    [[nodiscard]] QString runMarkerPath() const;
    [[nodiscard]] QString crashMarkerPath() const;

    /// What happened last time. Reads the markers, changes nothing.
    [[nodiscard]] Previous inspectPrevious() const;

    /// Writes state "running" for this process.
    bool markRunning();
    /// Writes state "clean". Called on aboutToQuit.
    bool markClean();
    /// The user was asked (or the report was sent automatically): write "running" for this
    /// session again and empty the crash marker, so the same case is never reported twice.
    void closeCase();

    /// Installs the signal handlers (Unix) or the exception filter and abort handler (Windows).
    /// Call once per process, after markRunning().
    void installHandlers();

private:
    bool writeState(const QString &state);

    QString _dir;
    QString _build;
    QString _fatalLogPath;
};

} // namespace OCC::Nourax
