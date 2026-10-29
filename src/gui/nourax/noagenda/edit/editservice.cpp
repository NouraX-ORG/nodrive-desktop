/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "editservice.h"

#include "caldavwriter.h"
#include "deckwriter.h"
#include "ganttwriter.h"
#include "writerutil.h"

#include <QLoggingCategory>
#include <QPointer>

namespace OCC::Nourax::Agenda {

Q_LOGGING_CATEGORY(lcAgendaEdit, "nodrive.agenda.edit", QtInfoMsg)

namespace {

struct When
{
    bool present = false;
    bool remove = false;
    bool isDate = false;
    QDate date;
    QDateTime utc;
};

When whenOf(const QJsonObject &change, const QString &key)
{
    When w;
    if (!change.contains(key)) {
        return w;
    }
    w.present = true;
    const auto value = change.value(key);
    const QString text = value.toString();
    if (text.isEmpty()) {
        w.remove = true;
    } else if (text.size() == 10) {
        w.isDate = true;
        w.date = QDate::fromString(text, Qt::ISODate);
    } else {
        w.utc = QDateTime::fromString(text, Qt::ISODate).toUTC();
    }
    return w;
}

AgentRef agentRefFor(const EditContext &context, const QString &agentId)
{
    if (agentId.isEmpty()) {
        return AgentRegistry::selfRef();
    }
    if (const auto *agent = context.registry.byId(agentId)) {
        return AgentRegistry::refFor(*agent);
    }
    return AgentRegistry::unknownRef(agentId);
}

void setAgent(AgendaItem *item, const EditContext &context, const QString &agentId)
{
    item->agent = agentRefFor(context, agentId);
    item->workKind = item->agent.isSelf ? WorkKind::Self : WorkKind::MyAgent;
}

void recomputeLate(AgendaItem *item, const QDate &today)
{
    if (item->kind != ItemKind::Todo && item->kind != ItemKind::GanttTask && item->kind != ItemKind::Card) {
        return;
    }
    item->late = item->due.isValid() && item->due < today && !item->done;
    item->lateDays = item->late ? static_cast<int>(item->due.daysTo(today)) : 0;
    item->dueToday = item->due.isValid() && item->due == today && !item->done;
}

} // namespace

EditService::EditService(HttpTransport *transport, const QString &directory, ContextProvider context, QObject *parent)
    : QObject(parent)
    , _transport(transport)
    , _queue(directory)
    , _context(std::move(context))
{
}

void EditService::load()
{
    _queue.load();
    Q_EMIT changed();
}

void EditService::setOnline(bool online)
{
    if (_online == online) {
        return;
    }
    _online = online;
    if (online) {
        retry();
    }
    Q_EMIT changed();
}

QString EditService::submit(const EditCommand &command)
{
    const QString id = _queue.enqueue(command);
    _queue.save();
    _stalled = false;
    Q_EMIT changed();
    process();
    return id;
}

void EditService::retry()
{
    _stalled = false;
    process();
}

void EditService::process()
{
    if (_busy || !_online || _stalled) {
        return;
    }
    const auto *next = nextSendable();
    if (!next) {
        return;
    }
    const EditCommand command = *next;
    _busy = true;
    _queue.setSending(command.id);
    _queue.markAttempt(command.id);
    QPointer<EditService> self(this);
    dispatch(command, [self, command](const EditResult &result) {
        if (self) {
            self->finishCommand(command, result);
        }
    });
}

const EditCommand *EditService::nextSendable()
{
    const auto context = _context();
    for (;;) {
        const EditCommand *found = nullptr;
        const EditCommand *readOnly = nullptr;
        for (const auto &c : _queue.commands()) {
            AppGuard guard = AppGuard::Writable;
            if (c.kind == EditKind::GanttUpdate || c.kind == EditKind::GanttAgentChange) {
                guard = context.ganttGuard;
            } else if (c.kind == EditKind::DeckCardUpdate || c.kind == EditKind::DeckAgentLabel) {
                guard = context.deckGuard;
            }
            if (guard == AppGuard::Unknown) {
                continue; // waits for the next read; later commands of other apps are not held up by it
            }
            if (guard == AppGuard::ReadOnly) {
                readOnly = &c;
                break;
            }
            found = &c;
            break;
        }
        if (found || !readOnly) {
            return found;
        }
        const EditCommand command = *readOnly;
        const QString message = AgendaText::tr("Read only: this version of the app on the server has not been checked yet.");
        _queue.moveToConflict(command.id, message, QStringLiteral("refused"));
        _queue.save();
        qCInfo(lcAgendaEdit) << "edit" << EditCommand::kindName(command.kind) << "held back by the version guard";
        Q_EMIT finished(command, WriterUtil::refused(message));
        Q_EMIT changed();
    }
}

void EditService::dispatch(const EditCommand &command, const EditDone &done)
{
    auto context = _context();
    if (!context.now.isValid()) {
        context.now = QDateTime::currentDateTimeUtc();
    }
    switch (command.kind) {
    case EditKind::CalendarPatch:
    case EditKind::CalendarCreate:
    case EditKind::CalendarDelete:
        CalDavWriter::run(_transport, command, context, done);
        return;
    case EditKind::GanttUpdate:
    case EditKind::GanttAgentChange:
        GanttWriter::run(_transport, command, context, done);
        return;
    case EditKind::DeckCardUpdate:
    case EditKind::DeckAgentLabel:
        DeckWriter::run(_transport, command, context, done);
        return;
    }
}

void EditService::finishCommand(const EditCommand &command, const EditResult &result)
{
    _busy = false;
    _queue.setSending(QString());
    // Never a value of the item in the log: only the kind, the outcome and the status code.
    qCInfo(lcAgendaEdit) << "edit" << EditCommand::kindName(command.kind) << "outcome" << static_cast<int>(result.outcome) << "status" << result.httpStatus;
    switch (result.outcome) {
    case EditResult::Outcome::Done: {
        _queue.remove(command.id);
        _queue.rebase(command.itemKey, command.baseVersion, result.newVersion);
        _applied.append({command, ++_sequence});
        _queue.save();
        Q_EMIT finished(command, result);
        Q_EMIT changed();
        Q_EMIT refreshNeeded();
        process();
        return;
    }
    case EditResult::Outcome::Conflict:
    case EditResult::Outcome::Refused:
        _queue.moveToConflict(command.id, result.message, result.conflictKind.isEmpty() ? QStringLiteral("refused") : result.conflictKind);
        _queue.save();
        Q_EMIT finished(command, result);
        Q_EMIT changed();
        Q_EMIT refreshNeeded(); // the other change is shown
        process();
        return;
    case EditResult::Outcome::Offline:
        _stalled = true;
        if (result.partialChange) {
            _queue.markPartial(command.id);
        }
        _queue.save();
        Q_EMIT finished(command, result);
        Q_EMIT changed();
        return;
    case EditResult::Outcome::NeedsLogin:
        _stalled = true;
        _queue.save();
        Q_EMIT finished(command, result);
        Q_EMIT changed();
        Q_EMIT loginNeeded();
        return;
    case EditResult::Outcome::Failed: {
        const auto *waiting = _queue.find(command.id);
        if (waiting && waiting->attempts >= 3) {
            _queue.moveToConflict(command.id, result.message, QStringLiteral("refused"));
            _queue.save();
            Q_EMIT finished(command, result);
            Q_EMIT changed();
            process();
            return;
        }
        _stalled = true; // tried again at the next refresh
        _queue.save();
        Q_EMIT finished(command, result);
        Q_EMIT changed();
        return;
    }
    }
}

void EditService::applyAgain(const QString &commandId)
{
    // The writer reads the item fresh and applies the same change on top of it: no base version to compare.
    if (_queue.reopen(commandId, QString())) {
        _queue.save();
        _stalled = false;
        Q_EMIT changed();
        process();
    }
}

void EditService::keepOther(const QString &commandId)
{
    if (_queue.dropConflict(commandId)) {
        _queue.save();
        Q_EMIT changed();
    }
}

void EditService::undo(const QString &opId)
{
    if (opId.isEmpty()) {
        return;
    }
    QPointer<EditService> self(this);
    if (!_online) {
        EditResult offline;
        offline.outcome = EditResult::Outcome::Offline;
        offline.message = AgendaText::tr("There is no connection to the server.");
        Q_EMIT undoFinished(offline);
        return;
    }
    GanttWriter::undo(_transport, opId, [self](const EditResult &result) {
        if (!self) {
            return;
        }
        if (result.done()) {
            Q_EMIT self->refreshNeeded();
        }
        Q_EMIT self->undoFinished(result);
    });
}

void EditService::clearAppliedUpTo(int sequence)
{
    bool any = false;
    for (int i = _applied.size() - 1; i >= 0; --i) {
        if (_applied.at(i).sequence <= sequence) {
            _applied.removeAt(i);
            any = true;
        }
    }
    if (any) {
        Q_EMIT changed();
    }
}

// ---- drawn on top of the rows -----------------------------------------------------------------------------------------------------

void EditService::applyCommand(AgendaItem *item, const EditCommand &command, const EditContext &context, const QDate &today)
{
    const auto &c = command.change;
    switch (command.kind) {
    case EditKind::CalendarPatch: {
        if (c.contains(QLatin1String("title"))) {
            item->title = c.value(QLatin1String("title")).toString();
        }
        if (c.contains(QLatin1String("description"))) {
            item->description = c.value(QLatin1String("description")).toString();
        }
        if (item->kind == ItemKind::Event) {
            const auto start = whenOf(c, QStringLiteral("start"));
            const auto end = whenOf(c, QStringLiteral("end"));
            if (start.present && !start.remove) {
                if (start.isDate) {
                    item->allDay = true;
                    item->timed = false;
                    item->startDate = start.date;
                    item->start = QDateTime();
                } else {
                    item->allDay = false;
                    item->timed = true;
                    item->start = start.utc;
                    item->startDate = QDate();
                }
            }
            if (end.present && !end.remove) {
                if (end.isDate) {
                    item->endDate = qMax(end.date.addDays(-1), item->startDate); // the file holds the day after the last day
                } else {
                    item->end = end.utc;
                }
            }
        } else {
            const auto start = whenOf(c, QStringLiteral("start"));
            const auto due = whenOf(c, QStringLiteral("due"));
            if (start.present) {
                item->startDate = start.remove ? QDate() : (start.isDate ? start.date : start.utc.toTimeZone(context.zone).date());
            }
            if (due.present) {
                if (due.remove) {
                    item->due = QDate();
                    item->endDate = QDate();
                    item->end = QDateTime();
                } else if (due.isDate) {
                    item->due = due.date;
                    item->endDate = due.date;
                    item->end = QDateTime();
                } else {
                    item->due = due.utc.toTimeZone(context.zone).date();
                    item->endDate = item->due;
                    item->end = due.utc;
                }
            }
            if (c.contains(QLatin1String("done"))) {
                item->done = c.value(QLatin1String("done")).toBool();
            }
            recomputeLate(item, today);
        }
        if (c.contains(QLatin1String("agent"))) {
            setAgent(item, context, c.value(QLatin1String("agent")).toObject().value(QLatin1String("id")).toString());
        }
        return;
    }
    case EditKind::GanttUpdate: {
        if (c.contains(QLatin1String("start_date")) || c.contains(QLatin1String("end_date"))) {
            item->slotStart = QDateTime(); // the real times are read again at the next refresh
            item->slotEnd = QDateTime();
        }
        if (c.contains(QLatin1String("start_date"))) {
            item->startDate = QDate::fromString(c.value(QLatin1String("start_date")).toString().left(10), Qt::ISODate);
        }
        if (c.contains(QLatin1String("end_date"))) {
            item->endDate = QDate::fromString(c.value(QLatin1String("end_date")).toString().left(10), Qt::ISODate);
            item->due = item->endDate;
        }
        if (c.contains(QLatin1String("manual_progress"))) {
            item->percentDone = qBound(0, c.value(QLatin1String("manual_progress")).toInt(), 100);
        }
        if (c.contains(QLatin1String("status"))) {
            item->status = c.value(QLatin1String("status")).toString();
            item->done = item->status == QLatin1String("done");
        }
        if (c.contains(QLatin1String("done"))) {
            item->done = c.value(QLatin1String("done")).toBool();
            item->status = item->done ? QStringLiteral("done") : QStringLiteral("in_progress");
        }
        recomputeLate(item, today);
        return;
    }
    case EditKind::GanttAgentChange:
    case EditKind::DeckAgentLabel:
        setAgent(item, context, c.value(QLatin1String("agentId")).toString());
        return;
    case EditKind::DeckCardUpdate: {
        const auto due = whenOf(c, QStringLiteral("due"));
        if (due.present) {
            item->due = due.remove ? QDate() : due.date;
            item->endDate = item->due;
            item->slotStart = QDateTime(); // the real times are read again at the next refresh
            item->slotEnd = QDateTime();
        }
        if (c.contains(QLatin1String("done"))) {
            item->done = c.value(QLatin1String("done")).toBool();
        }
        recomputeLate(item, today);
        return;
    }
    case EditKind::CalendarCreate:
    case EditKind::CalendarDelete:
        return;
    }
}

AgendaItem EditService::syntheticItem(const EditCommand &command, const EditContext &context)
{
    const auto &c = command.change;
    AgendaItem item;
    const bool todo = c.value(QLatin1String("isTodo")).toBool();
    item.key = command.itemKey;
    item.kind = todo ? ItemKind::Todo : ItemKind::Event;
    item.home = todo ? Home::Tasks : Home::Calendar;
    item.title = c.value(QLatin1String("title")).toString();
    if (item.title.isEmpty()) {
        item.title = AgendaText::tr("(no title)");
    }
    item.description = c.value(QLatin1String("description")).toString();
    item.calendarUri = c.value(QLatin1String("calendarUri")).toString();
    item.sourceKey = QStringLiteral("cal:") + item.calendarUri;
    item.sourceName = context.calendarNames.value(item.calendarUri, item.calendarUri);
    item.sourceColor = context.calendarColors.value(item.calendarUri);
    item.category = Category::Work;
    item.readOnly = true;
    item.readOnlyReason = AgendaText::tr("Waiting to send");
    item.uid = c.value(QLatin1String("uid")).toString();
    setAgent(&item, context, c.value(QLatin1String("agent")).toObject().value(QLatin1String("id")).toString());
    const auto start = whenOf(c, QStringLiteral("start"));
    const auto end = whenOf(c, QStringLiteral("end"));
    const auto due = whenOf(c, QStringLiteral("due"));
    if (!todo) {
        if (start.present && start.isDate) {
            item.allDay = true;
            item.startDate = start.date;
            item.endDate = end.present && end.isDate ? qMax(end.date.addDays(-1), start.date) : start.date;
        } else if (start.present) {
            item.timed = true;
            item.start = start.utc;
            item.end = end.present && !end.isDate && end.utc.isValid() ? end.utc : start.utc.addSecs(3600);
        }
    } else {
        if (start.present && !start.remove) {
            item.startDate = start.isDate ? start.date : start.utc.toTimeZone(context.zone).date();
        }
        if (due.present && !due.remove) {
            item.due = due.isDate ? due.date : due.utc.toTimeZone(context.zone).date();
            item.endDate = item.due;
        }
    }
    return item;
}

void EditService::overlay(QList<AgendaItem> *items, const QDate &today) const
{
    const auto context = _context();
    QList<EditCommand> all;
    for (const auto &a : _applied) {
        all.append(a.command);
    }
    const int appliedCount = static_cast<int>(all.size());
    for (const auto &c : _queue.commands()) {
        all.append(c);
    }
    for (int n = 0; n < all.size(); ++n) {
        const auto &command = all.at(n);
        const bool waiting = n >= appliedCount;
        if (command.kind == EditKind::CalendarCreate) {
            auto item = syntheticItem(command, context);
            if (waiting) {
                item.pending = PendingState::Waiting;
            }
            items->append(item);
            continue;
        }
        for (int i = 0; i < items->size(); ++i) {
            if (items->at(i).key != command.itemKey) {
                continue;
            }
            if (command.kind == EditKind::CalendarDelete) {
                items->removeAt(i);
                break;
            }
            auto &item = (*items)[i];
            applyCommand(&item, command, context, today);
            if (waiting) {
                item.pending = PendingState::Waiting;
            }
        }
    }
    // A change that could not be sent: the row shows the other change, with a "Not sent" mark.
    for (const auto &c : _queue.conflicts()) {
        for (auto &item : *items) {
            if (item.key == c.itemKey && item.pending == PendingState::None) {
                item.pending = PendingState::Conflict;
            }
        }
    }
}

} // namespace OCC::Nourax::Agenda
