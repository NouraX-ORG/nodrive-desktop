/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: one edit as data (plans/noagenda-desktop-plan.md, 2.2). A command holds the semantic change
// ("end = 2026-11-04", "agent = AG-0002") and the version of the item that the person saw. It can be written
// to the offline queue and sent later, even after a restart, and it can be applied again on top of a newer
// version of the item ("Apply my change again").

#pragma once

#include "../model/agendaitem.h"
#include "../registry/agentregistry.h"

#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QTimeZone>

#include <functional>

namespace OCC::Nourax::Agenda {

enum class EditKind {
    CalendarPatch, ///< event or to-do: change fields
    CalendarCreate,
    CalendarDelete,
    GanttUpdate, ///< dates, % done, status, done
    GanttAgentChange, ///< my agent: Self or one of my agents
    DeckCardUpdate, ///< due date, done
    DeckAgentLabel, ///< my agent label
};

enum class CommandState { Waiting, Sending, Conflict };

struct EditCommand
{
    QString id;
    QDateTime createdUtc;
    QString itemKey;
    EditKind kind = EditKind::CalendarPatch;
    QJsonObject change; ///< the semantic change and the address of the item (see editcommand.cpp)
    QString baseVersion; ///< the version of the item that the person saw (ETag, updated_at or lastModified)
    int attempts = 0;
    CommandState state = CommandState::Waiting;
    QString title; ///< the title of the item (for the "Not sent" panel)
    QString summary; ///< plain words: what the change was
    QString home; ///< calendar | tasks | gantt | board (for "Open in ...")
    QString message; ///< why it is in the "Not sent" panel
    QString conflictKind; ///< changed | gone | refused

    [[nodiscard]] QJsonObject toJson() const;
    static EditCommand fromJson(const QJsonObject &object);

    static QString kindName(EditKind kind);
    static EditKind kindFromName(const QString &name, bool *ok = nullptr);

    /// Whether `newer` (an edit of the same item, not sent yet) can be folded into this command.
    [[nodiscard]] bool canMerge(const EditCommand &newer) const;
    /// Folds `newer` into this command: its fields win. Only after canMerge().
    void merge(const EditCommand &newer);
};

/// What the version guard of an app on the server said at the last read (design 5: NoAgenda never writes blind).
/// Unknown = no read of this app has finished since the last (re)connection: its commands wait.
enum class AppGuard { Unknown, Writable, ReadOnly };

/// What the writers need besides the command itself.
struct EditContext
{
    AppGuard ganttGuard = AppGuard::Unknown; ///< NoGantt: the contract check of the last read
    AppGuard deckGuard = AppGuard::Unknown; ///< NoBoard: the tested-version check of the last read

    QString me; ///< user id of the person
    QString myName; ///< display name
    QString davUser;
    AgentRegistry registry;
    QJsonArray ganttResources; ///< NoGantt planning people visible to me (from the last read)
    QMap<int, QString> ganttProjectOwners; ///< project id -> owner user id (from the last read)
    double hoursPerPt = 8.0;
    QDateTime now; ///< UTC
    QTimeZone zone;
    QMap<QString, QString> calendarNames; ///< calendar uri -> name (for a new item that is still waiting to be sent)
    QMap<QString, QString> calendarColors;
};

struct EditResult
{
    enum class Outcome {
        Done,
        Conflict, ///< the item changed somewhere else: the other change wins
        Refused, ///< the server or a rule says no: plain words in `message`
        Offline, ///< no network: the command is queued
        NeedsLogin,
        Failed, ///< something else went wrong: plain words in `message`
    };

    Outcome outcome = Outcome::Failed;
    QString message;
    QString conflictKind; ///< changed | gone | refused
    QString newVersion;
    int httpStatus = 0;
    QStringList alsoMoved; ///< NoGantt: titles of the tasks that moved with this one
    QString undoOpId; ///< NoGantt: "Undo" for 10 seconds
    QString note; ///< a sentence to show next to the result (for example a linked card that did not move)
    /// NoGantt: the dates that the server saved for the task (from its answer), not the ones that were sent. NoGantt moves a
    /// dragged task onto working days, so they can differ from the drop. Invalid when the answer had none.
    QDate savedStart;
    QDate savedEnd;
    bool checkInNoGantt = false; ///< NoGantt answered with another list than the one sent
    /// NoBoard agent label: the answer was lost after the old label was already taken off. The retry must not count
    /// NoAgenda's own partial change as "changed somewhere else".
    bool partialChange = false;

    [[nodiscard]] bool done() const { return outcome == Outcome::Done; }
};

using EditDone = std::function<void(const EditResult &)>;

/// Builders of commands (the "change" object is documented per function).
namespace EditCommands {

/// Event or to-do of CalDAV. `fields` can hold: title, description, allDay, start, end, due (a date as "2026-11-04" or a
/// UTC date-time as "2026-11-04T09:00:00Z"; "" removes it), done (bool) and agent ({id, tag, removeTags}).
EditCommand calendarPatch(const AgendaItem &item, const QJsonObject &fields);
EditCommand calendarCreate(const QString &calendarUri, bool isTodo, const QString &uid, const QJsonObject &fields, const QString &title);
EditCommand calendarDelete(const AgendaItem &item);
/// `fields`: start_date and end_date ("2026-11-04"), manual_progress (0-100), status, done (bool).
EditCommand ganttUpdate(const AgendaItem &item, const QJsonObject &fields);
/// `agentId` empty = Self.
EditCommand ganttAgent(const AgendaItem &item, const QString &agentId, const QString &myUid);
/// `fields`: due ("2026-11-04" or "" to remove) and done (bool).
EditCommand deckCard(const AgendaItem &item, const QJsonObject &fields);
EditCommand deckAgent(const AgendaItem &item, const QString &agentId, const QString &myUid);

} // namespace EditCommands

/// The version of a calendar, NoGantt or Deck object without quotes and weak markers, for comparing.
QString normalizeVersion(const QString &version);

} // namespace OCC::Nourax::Agenda

Q_DECLARE_METATYPE(OCC::Nourax::Agenda::EditCommand)
Q_DECLARE_METATYPE(OCC::Nourax::Agenda::EditResult)
