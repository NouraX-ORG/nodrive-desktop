/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: the offline queue of edits and the edit conflicts that wait for the person's click
// (plans/noagenda-desktop-plan.md, 2.3 and 2.6). One file per account, queue.json in the account's NoAgenda folder:
//   {"cache_schema": 1, "commands": [...waiting, oldest first...], "conflicts": [...not sent...]}
// It survives a restart. Nothing leaves the queue without a result from the server or a click of the person:
// a conflict stays in "Not sent" until the person applies it again or keeps the other change.

#pragma once

#include "editcommand.h"

#include <QList>
#include <QString>

namespace OCC::Nourax::Agenda {

class EditQueue
{
public:
    static constexpr int schema = 1;
    static QString fileName() { return QStringLiteral("queue.json"); }

    /// `directory` is <config>/noagenda/<account id>/ (created when the first command is saved).
    explicit EditQueue(const QString &directory);

    /// Reads the file. A file that cannot be read, or has another schema, is left alone and treated as empty
    /// (it is only replaced when the next command is saved).
    void load();
    bool save() const;
    /// Removes the file (when the account is removed).
    void removeFile() const;

    [[nodiscard]] const QList<EditCommand> &commands() const { return _commands; }
    [[nodiscard]] const QList<EditCommand> &conflicts() const { return _conflicts; }
    [[nodiscard]] int pendingCount() const { return static_cast<int>(_commands.size()); }
    [[nodiscard]] bool isEmpty() const { return _commands.isEmpty() && _conflicts.isEmpty(); }

    /// Adds a command at the end. An edit of the same item that is still waiting (same kind) is folded into it.
    /// Returns the id of the command that now holds the change.
    QString enqueue(const EditCommand &command);
    [[nodiscard]] const EditCommand *find(const QString &id) const;
    /// The first waiting command, or null.
    [[nodiscard]] const EditCommand *next() const;
    void markAttempt(const QString &id);
    /// The command that is being sent right now: a new edit is never folded into it.
    void setSending(const QString &id) { _sendingId = id; }
    [[nodiscard]] QString sendingId() const { return _sendingId; }
    /// Takes the command out of the waiting list (it was sent).
    bool remove(const QString &id);
    /// Moves a waiting command to the "Not sent" list.
    bool moveToConflict(const QString &id, const QString &message, const QString &conflictKind);
    /// "Apply my change again": the conflict goes back to the waiting list, with the newer base version.
    bool reopen(const QString &id, const QString &newBaseVersion);
    /// "Keep the other change": the conflict is dropped.
    bool dropConflict(const QString &id);
    [[nodiscard]] const EditCommand *findConflict(const QString &id) const;
    /// After a command of an item was sent: later commands of that item that still hold the old base version get the new one.
    void rebase(const QString &itemKey, const QString &oldBase, const QString &newBase);

private:
    QString _directory;
    QString _sendingId;
    QList<EditCommand> _commands;
    QList<EditCommand> _conflicts;
};

} // namespace OCC::Nourax::Agenda
