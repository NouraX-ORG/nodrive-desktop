/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: the one row type that every view shows (plans/noagenda-desktop-plan.md, 1.3).

#pragma once

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <QTimeZone>

namespace OCC::Nourax::Agenda {

/// Translations for code that is not a QObject.
class AgendaText
{
    Q_DECLARE_TR_FUNCTIONS(AgendaText)
};

enum class ItemKind { Event, Todo, GanttTask, Milestone, Card };
enum class Home { Calendar, Tasks, Gantt, Board };
/// Design 6.0: the three kinds of work.
enum class WorkKind { Self, MyAgent, Team };
/// For the All / Work / Personal switch.
enum class Category { Personal, Work, Info };
/// A change of mine that is waiting to be sent (edit stage, plans/noagenda-desktop-plan.md, 2.3).
enum class PendingState { None, Waiting, Sending, Conflict };

struct AgentRef
{
    bool isSelf = true;
    QString id; ///< AG-0001, or the unrecognised label / tag text
    QString name; ///< empty when the agent is not in the registry
    QString status; ///< active | paused | retired | empty
    bool unknown = false;
    bool retired = false;
    bool paused = false;
    bool otherOrganisation = false; ///< name is hidden ("Agent of Ali")
    QString owner; ///< display name used for "Agent of %1" (empty when it is not known: never a user id)

    /// "Self", "Writer-Bot", "AG-0001 (unknown agent)", "Writer-Bot (retired)" or "Agent of Ali".
    [[nodiscard]] QString displayText() const;
};

/// A problem sentence with the audience that is shown it (design 4.4).
struct ProblemFlag
{
    enum class Kind {
        Late,
        DueToday,
        Blocked,
        Overbooked,
        NotAssigned,
        AgentNoPerson,
        AgentWrongPerson,
        TwoAgents,
        UnknownAgent,
        RetiredAgent,
        NotLinked,
        RoleOnly,
        NotShared,
    };
    enum class Audience { Person, Manager, Both };

    Kind kind = Kind::Late;
    Audience audience = Audience::Both;
    QString text;
};

/// Timeline stage: one dependency of a NoGantt task (plans/noagenda-desktop-plan.md, 3.2). `type` is NoGantt's own code:
/// EA finish to start, AA start to start, EE finish to finish, AE start to finish.
struct ItemDependency
{
    QString predecessorKey; ///< gantt:task:<id> or gantt:ms:<id>
    QString type = QStringLiteral("EA");
    double lagDays = 0;
    QString predecessorTitle;
    QString predecessorOwner; ///< display names of the people on the predecessor (empty when unknown)
};

struct AgendaItem
{
    QString key; ///< cal:<calendarUri>/<objectUri>[#<recurrence-id>], gantt:task:<id>, gantt:ms:<id>, deck:card:<id>
    ItemKind kind = ItemKind::Event;
    Home home = Home::Calendar;
    QString title;

    // Times. A timed item has start (and usually end) in UTC. A date-based item has startDate / endDate.
    bool timed = false;
    bool allDay = false;
    QDateTime start;
    QDateTime end;
    QDate startDate;
    QDate endDate;
    QDate due;
    /// A card or a NoGantt task with real times on one day (UTC). It takes that time like an event does: it can clash
    /// and it is busy time; the row itself is still placed by its dates. Events use start / end.
    QDateTime slotStart;
    QDateTime slotEnd;

    QString sourceKey;
    QString sourceName;
    QString sourceColor;
    Category category = Category::Work;
    WorkKind workKind = WorkKind::Self;
    AgentRef agent;
    QString personName; ///< team rows: who the work belongs to
    QString personUid;

    QString status;
    bool done = false;
    int percentDone = -1;

    // Flags
    bool late = false;
    int lateDays = 0;
    bool dueToday = false;
    bool blocked = false;
    QString blockedBy;
    bool tentative = false;
    bool transparent = false;
    bool declined = false;
    bool repeating = false;
    bool invitation = false;
    bool info = false; ///< holidays, birthdays, subscriptions: shown, never block time
    bool readOnly = false;
    QString readOnlyReason;
    bool versionReadOnly = false;
    bool boardBadge = false; ///< a Gantt task that is linked to a board card
    bool timeApproximated = false;
    QList<ProblemFlag> problems;
    /// A row that only reports a problem that concerns me (for example my agent sits on a task without me).
    /// It is not my work: no date, no workload, no clash, not in the "No date" list.
    bool problemOnly = false;

    QString description;
    bool descriptionCut = false; ///< the description is shown cut to 2,000 characters: changing it here would lose the rest
    QString location;
    QString version; ///< ETag, updated_at or lastModified (the base version of an edit)

    // Edit stage: what the editor needs, and what the conflict rules found.
    QString progressMode; ///< NoGantt: manual | deck | ... (% done is read only when it comes from the board)
    int stackId = 0; ///< NoBoard: the stack of the card
    double agentWorkHours = -1; ///< NoGantt: the work of the agent's own planning person on the task (hours; -1 = none)
    PendingState pending = PendingState::None;
    bool clash = false; ///< overlaps another timed item of mine
    bool softClash = false; ///< the overlap involves a tentative item
    QStringList clashWith; ///< titles of the items it overlaps
    bool deadlineDayOff = false; ///< the due or end date is on a day off
    /// A synthetic row of the conflict rules: one of the agents is over its capacity on `agentLoadDay`.
    bool agentLoad = false;
    QDate agentLoadDay;

    // Timeline stage: the phase (NoGantt subproject), the dependencies, and the time of a to-do's start.
    int phaseId = 0;
    QString phaseName;
    QList<ItemDependency> dependencies;
    QDateTime startStamp; ///< a to-do whose DTSTART has a time of day (UTC): a move keeps that time

    // References
    int projectId = 0;
    QString projectName;
    int taskId = 0;
    int boardId = 0;
    int cardId = 0;
    int deckCardId = 0;
    QString calendarUri;
    QString objectUri;
    QString uid;
    QString recurrenceId;
    bool organizerIsMe = false;
    bool teamCalendar = false; ///< the item is in a shared calendar that the person set to "Team"
    QString myPartstat;
    double myWorkHours = -1;

    /// Date used to place the row in a day list (start date for timed items).
    [[nodiscard]] QDate firstDay(const QTimeZone &zone) const;
    [[nodiscard]] QDate lastDay(const QTimeZone &zone) const;
    [[nodiscard]] bool hasDate() const;
};

/// Result of reading one source.
struct SourceStatus
{
    /// Saved: read from the cache at start, no attempt yet (no banner). Cached: the last attempt failed, the saved copy is shown.
    /// Refused: the app on the server (NoGantt or NoBoard) answered 401 or 403 to a signed-in, connected account
    /// (design 5: that part turns into "Open in NoGantt" / "Open in NoBoard"; the sign-in state is AccountState's).
    enum class State { NeverLoaded, Fresh, Saved, Cached, Failed, ReadOnly, Unavailable, NotLinked, NeedsLogin, Refused };

    QString key; ///< calendar | gantt | board | registry
    State state = State::NeverLoaded;
    QDateTime savedAt; ///< when the shown data was read
    QString message;
    bool readOnlyByGuard = false;
};

/// A row of the Sources screen.
struct SourceEntry
{
    enum class Type { Calendar, Project, Board };

    Type type = Type::Calendar;
    QString key; ///< calendar uri, project id or board id
    QString name;
    QString color;
    QString note;
    QString mode; ///< effective mode: personal | work | team | info | hidden | doOversee | do
    QString defaultMode;
    bool autoHidden = false;
    QString autoHiddenReason;
    bool personalQuestion = false; ///< an own calendar (listed in the one-time question)
};

} // namespace OCC::Nourax::Agenda
