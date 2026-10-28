/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: the setting of every source and its defaults (plans/noagenda-decisions.md, 6.1).
// Settings are stored per account on this computer.

#pragma once

#include "../caldav/calendardata.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTime>

namespace OCC::Nourax::Agenda {

enum class CalendarMode { Personal, Work, Team, Info, Hidden };
enum class WorkMode { DoOversee, Do, Hidden };

QString toString(CalendarMode mode);
CalendarMode calendarModeFromString(const QString &text, CalendarMode fallback);
QString toString(WorkMode mode);
WorkMode workModeFromString(const QString &text, WorkMode fallback);

/// How the signed-in person relates to a NoGantt project (design 6.1).
struct ProjectRelation
{
    int id = 0;
    QString title;
    QString color;
    QString ownerId;
    QString ownerName;
    QString permission; ///< viewer | editor | admin (as the server answers)
    bool owned = false;
    bool hasMyTasks = false;
    bool serverAdmin = false; ///< the signed-in person is in the server's admin group
    bool runByRealShare = false; ///< owner, or admin through a real share
    bool seenOnlyAsServerAdmin = false;
    bool sharesKnown = false;

    /// Builds the relation. `shares` is the project's share list (may be empty when unknown).
    static ProjectRelation of(const QJsonObject &project, const QString &me, bool serverAdmin, const QStringList &myGroups,
        const QSet<int> &myProjectIds, const QJsonArray &shares, bool sharesKnown);
    [[nodiscard]] WorkMode defaultMode() const;
};

/// How the signed-in person relates to a board.
struct BoardRelation
{
    int id = 0;
    QString title;
    QString color;
    bool owned = false;
    bool canManage = false;

    static BoardRelation of(const QJsonObject &board, const QString &me);
    [[nodiscard]] WorkMode defaultMode() const { return (owned || canManage) ? WorkMode::DoOversee : WorkMode::Do; }
};

struct WorkingHours
{
    int daysMask = 31; ///< bit 0 = Monday ... bit 6 = Sunday (NoGantt's own coding)
    QTime from = QTime(9, 0);
    QTime to = QTime(17, 0);
    [[nodiscard]] bool isWorkingDay(const QDate &day) const { return (daysMask >> (day.dayOfWeek() - 1)) & 1; }
    [[nodiscard]] double hoursPerDay() const { return qMax(0.0, from.secsTo(to) / 3600.0); }
};

class SourceSettings
{
public:
    /// The default of a calendar. Automatically hidden calendars are Hidden here.
    static CalendarMode defaultCalendarMode(const CalendarInfo &calendar);
    /// Why a calendar is hidden without the person's choice (empty if it is not).
    static QString autoHiddenReason(const CalendarInfo &calendar, const QString &taskCopyCalendarUri);

    [[nodiscard]] CalendarMode calendarMode(const CalendarInfo &calendar, const QString &taskCopyCalendarUri) const;
    void setCalendarMode(const QString &uri, CalendarMode mode);
    [[nodiscard]] bool hasCalendarOverride(const QString &uri) const { return _calendarModes.contains(uri); }

    [[nodiscard]] WorkMode projectMode(const ProjectRelation &relation) const;
    void setProjectMode(int id, WorkMode mode);
    [[nodiscard]] WorkMode boardMode(const BoardRelation &relation) const;
    void setBoardMode(int id, WorkMode mode);

    [[nodiscard]] bool personalAnswered() const { return _personalAnswered; }
    /// The one-time question "Which of your calendars are personal?": the listed ones stay Personal, the
    /// other own calendars become Work. An empty `answer` with skipped = true keeps everything Personal.
    void answerPersonalQuestion(const QStringList &personalUris, const QStringList &ownUris, bool skipped);

    [[nodiscard]] bool workingHoursOverridden() const { return _workingHoursSet; }
    [[nodiscard]] WorkingHours workingHours() const { return _workingHours; }
    void setWorkingHours(const WorkingHours &hours);

    // The last state of the window.
    QString lastView = QStringLiteral("day"); ///< day | week | month
    QString lastCategory = QStringLiteral("all"); ///< all | work | personal
    bool chipMe = true;
    bool chipAgents = true;
    bool chipTeam = true;
    bool showAllTeam = false;
    bool everOpened = false;

    [[nodiscard]] QJsonObject toJson() const;
    static SourceSettings fromJson(const QJsonObject &o);

private:
    QMap<QString, CalendarMode> _calendarModes;
    QMap<int, WorkMode> _projectModes;
    QMap<int, WorkMode> _boardModes;
    bool _personalAnswered = false;
    bool _workingHoursSet = false;
    WorkingHours _workingHours;
};

} // namespace OCC::Nourax::Agenda
