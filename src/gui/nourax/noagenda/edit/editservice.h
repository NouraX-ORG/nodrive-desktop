/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: runs the edits (plans/noagenda-desktop-plan.md, 2.2 and 2.6).
// Every edit becomes a command in the queue, is drawn on top of the cached rows at once ("Waiting to send"), and is
// sent when the account is online, oldest first and one at a time. A precondition that fails (412, another
// `updated_at`, another `lastModified`) moves the command to the "Not sent" list: the other change wins and
// nothing is lost silently. "Apply my change again" re-reads the item and applies the same change on top of it.

#pragma once

#include "../net/httptransport.h"
#include "editcommand.h"
#include "editqueue.h"

#include <QObject>

#include <functional>

namespace OCC::Nourax::Agenda {

class EditService : public QObject
{
    Q_OBJECT

public:
    using ContextProvider = std::function<EditContext()>;

    /// `transport` must outlive the service. `directory` is the account's NoAgenda folder.
    EditService(HttpTransport *transport, const QString &directory, ContextProvider context, QObject *parent = nullptr);

    [[nodiscard]] EditQueue &queue() { return _queue; }
    [[nodiscard]] const EditQueue &queue() const { return _queue; }
    void load();

    void setOnline(bool online);
    [[nodiscard]] bool isOnline() const { return _online; }
    [[nodiscard]] bool isBusy() const { return _busy; }

    /// Puts the command in the queue (it shows at once) and sends it when online. Returns the id of the command.
    QString submit(const EditCommand &command);
    /// Sends what is waiting, oldest first. Does nothing when offline or when a send is running.
    void process();
    /// The next try after an offline answer or a refresh that worked.
    void retry();

    void applyAgain(const QString &commandId);
    void keepOther(const QString &commandId);
    /// "Undo" of a NoGantt move (the op id of the result).
    void undo(const QString &opId);

    /// Draws the waiting commands, the "Not sent" ones and the ones that were just sent (until the next refresh
    /// has read them back) on top of the rows.
    void overlay(QList<AgendaItem> *items, const QDate &today) const;
    /// The same change on one row (public for the tests).
    static void applyCommand(AgendaItem *item, const EditCommand &command, const EditContext &context, const QDate &today);
    static AgendaItem syntheticItem(const EditCommand &command, const EditContext &context);

    /// The sequence number of the last command that was sent; a refresh that starts now has read everything up to it.
    [[nodiscard]] int appliedSequence() const { return _sequence; }
    /// The refresh that started when the sequence was `sequence` finished: those commands are in the data now.
    void clearAppliedUpTo(int sequence);

Q_SIGNALS:
    /// The queue or the sent-but-not-yet-read list changed: the rows must be drawn again.
    void changed();
    /// A command got its answer (also an offline one).
    void finished(const OCC::Nourax::Agenda::EditCommand &command, const OCC::Nourax::Agenda::EditResult &result);
    /// The data on the server changed: read it again.
    void refreshNeeded();
    void loginNeeded();

private:
    void dispatch(const EditCommand &command, const EditDone &done);
    void finishCommand(const EditCommand &command, const EditResult &result);

    struct Applied
    {
        EditCommand command;
        int sequence = 0;
    };

    HttpTransport *_transport;
    EditQueue _queue;
    ContextProvider _context;
    QList<Applied> _applied;
    bool _online = true;
    bool _busy = false;
    bool _stalled = false; ///< the last try got no answer: wait for retry() or a new edit
    int _sequence = 0;
};

} // namespace OCC::Nourax::Agenda
