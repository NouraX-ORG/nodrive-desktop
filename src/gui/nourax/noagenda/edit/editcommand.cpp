/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "editcommand.h"

#include <QUuid>

namespace OCC::Nourax::Agenda {

QString normalizeVersion(const QString &version)
{
    QString v = version.trimmed();
    if (v.startsWith(QLatin1String("W/"))) {
        v.remove(0, 2);
    }
    if (v.size() >= 2 && v.startsWith(QLatin1Char('"')) && v.endsWith(QLatin1Char('"'))) {
        v = v.mid(1, v.size() - 2);
    }
    v.replace(QLatin1String("&quot;"), QString());
    return v;
}

QString EditCommand::kindName(EditKind kind)
{
    switch (kind) {
    case EditKind::CalendarPatch:
        return QStringLiteral("calendar-patch");
    case EditKind::CalendarCreate:
        return QStringLiteral("calendar-create");
    case EditKind::CalendarDelete:
        return QStringLiteral("calendar-delete");
    case EditKind::GanttUpdate:
        return QStringLiteral("gantt-update");
    case EditKind::GanttAgentChange:
        return QStringLiteral("gantt-agent");
    case EditKind::DeckCardUpdate:
        return QStringLiteral("deck-card");
    case EditKind::DeckAgentLabel:
        return QStringLiteral("deck-agent");
    }
    return QString();
}

EditKind EditCommand::kindFromName(const QString &name, bool *ok)
{
    static const QList<EditKind> all{EditKind::CalendarPatch, EditKind::CalendarCreate, EditKind::CalendarDelete, EditKind::GanttUpdate,
        EditKind::GanttAgentChange, EditKind::DeckCardUpdate, EditKind::DeckAgentLabel};
    for (const auto kind : all) {
        if (kindName(kind) == name) {
            if (ok) {
                *ok = true;
            }
            return kind;
        }
    }
    if (ok) {
        *ok = false;
    }
    return EditKind::CalendarPatch;
}

QJsonObject EditCommand::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("id"), id);
    o.insert(QStringLiteral("created_utc"), createdUtc.toUTC().toString(Qt::ISODate));
    o.insert(QStringLiteral("item_key"), itemKey);
    o.insert(QStringLiteral("kind"), kindName(kind));
    o.insert(QStringLiteral("change"), change);
    o.insert(QStringLiteral("base_version"), baseVersion);
    o.insert(QStringLiteral("attempts"), attempts);
    o.insert(QStringLiteral("state"), state == CommandState::Conflict ? QStringLiteral("conflict") : QStringLiteral("waiting"));
    o.insert(QStringLiteral("title"), title);
    o.insert(QStringLiteral("summary"), summary);
    o.insert(QStringLiteral("home"), home);
    o.insert(QStringLiteral("message"), message);
    o.insert(QStringLiteral("conflict_kind"), conflictKind);
    return o;
}

EditCommand EditCommand::fromJson(const QJsonObject &o)
{
    EditCommand c;
    c.id = o.value(QLatin1String("id")).toString();
    c.createdUtc = QDateTime::fromString(o.value(QLatin1String("created_utc")).toString(), Qt::ISODate);
    c.itemKey = o.value(QLatin1String("item_key")).toString();
    c.kind = kindFromName(o.value(QLatin1String("kind")).toString());
    c.change = o.value(QLatin1String("change")).toObject();
    c.baseVersion = o.value(QLatin1String("base_version")).toString();
    c.attempts = o.value(QLatin1String("attempts")).toInt();
    // A command that was being sent when NoDrive stopped is sent again: its precondition makes that safe.
    c.state = o.value(QLatin1String("state")).toString() == QLatin1String("conflict") ? CommandState::Conflict : CommandState::Waiting;
    c.title = o.value(QLatin1String("title")).toString();
    c.summary = o.value(QLatin1String("summary")).toString();
    c.home = o.value(QLatin1String("home")).toString();
    c.message = o.value(QLatin1String("message")).toString();
    c.conflictKind = o.value(QLatin1String("conflict_kind")).toString();
    return c;
}

bool EditCommand::canMerge(const EditCommand &newer) const
{
    if (state != CommandState::Waiting || newer.itemKey != itemKey || newer.kind != kind) {
        return false;
    }
    switch (kind) {
    case EditKind::CalendarPatch:
    case EditKind::GanttUpdate:
    case EditKind::GanttAgentChange:
    case EditKind::DeckCardUpdate:
    case EditKind::DeckAgentLabel:
        return true;
    case EditKind::CalendarCreate:
    case EditKind::CalendarDelete:
        return false;
    }
    return false;
}

void EditCommand::merge(const EditCommand &newer)
{
    for (auto it = newer.change.begin(); it != newer.change.end(); ++it) {
        change.insert(it.key(), it.value());
    }
    summary = newer.summary;
    title = newer.title.isEmpty() ? title : newer.title;
}

namespace EditCommands {

namespace {

EditCommand base(const AgendaItem &item, EditKind kind)
{
    EditCommand c;
    c.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    c.createdUtc = QDateTime::currentDateTimeUtc();
    c.itemKey = item.key;
    c.kind = kind;
    c.baseVersion = item.version;
    c.title = item.title;
    switch (item.home) {
    case Home::Calendar:
        c.home = QStringLiteral("calendar");
        break;
    case Home::Tasks:
        c.home = QStringLiteral("tasks");
        break;
    case Home::Gantt:
        c.home = QStringLiteral("gantt");
        break;
    case Home::Board:
        c.home = QStringLiteral("board");
        break;
    }
    return c;
}

QString describe(const QJsonObject &fields)
{
    QStringList parts;
    if (fields.contains(QLatin1String("title"))) {
        parts.append(AgendaText::tr("new title"));
    }
    if (fields.contains(QLatin1String("start")) || fields.contains(QLatin1String("end")) || fields.contains(QLatin1String("allDay"))
        || fields.contains(QLatin1String("start_date")) || fields.contains(QLatin1String("end_date"))) {
        parts.append(AgendaText::tr("new time"));
    }
    if (fields.contains(QLatin1String("due"))) {
        parts.append(AgendaText::tr("new due date"));
    }
    if (fields.contains(QLatin1String("description"))) {
        parts.append(AgendaText::tr("new description"));
    }
    if (fields.contains(QLatin1String("manual_progress"))) {
        parts.append(AgendaText::tr("new % done"));
    }
    if (fields.contains(QLatin1String("status"))) {
        parts.append(AgendaText::tr("new status"));
    }
    if (fields.contains(QLatin1String("done"))) {
        parts.append(fields.value(QLatin1String("done")).toBool() ? AgendaText::tr("marked done") : AgendaText::tr("marked not done"));
    }
    if (fields.contains(QLatin1String("agent"))) {
        parts.append(AgendaText::tr("new agent"));
    }
    return parts.isEmpty() ? AgendaText::tr("your change") : parts.join(QStringLiteral(", "));
}

} // namespace

EditCommand calendarPatch(const AgendaItem &item, const QJsonObject &fields)
{
    auto c = base(item, EditKind::CalendarPatch);
    c.change = fields;
    c.change.insert(QStringLiteral("calendarUri"), item.calendarUri);
    c.change.insert(QStringLiteral("objectUri"), item.objectUri);
    c.change.insert(QStringLiteral("isTodo"), item.kind == ItemKind::Todo);
    c.summary = describe(fields);
    return c;
}

EditCommand calendarCreate(const QString &calendarUri, bool isTodo, const QString &uid, const QJsonObject &fields, const QString &title)
{
    EditCommand c;
    c.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    c.createdUtc = QDateTime::currentDateTimeUtc();
    c.itemKey = QStringLiteral("new:") + uid;
    c.kind = EditKind::CalendarCreate;
    c.change = fields;
    c.change.insert(QStringLiteral("calendarUri"), calendarUri);
    c.change.insert(QStringLiteral("isTodo"), isTodo);
    c.change.insert(QStringLiteral("uid"), uid);
    c.title = title;
    c.summary = isTodo ? AgendaText::tr("new to-do") : AgendaText::tr("new event");
    c.home = isTodo ? QStringLiteral("tasks") : QStringLiteral("calendar");
    return c;
}

EditCommand calendarDelete(const AgendaItem &item)
{
    auto c = base(item, EditKind::CalendarDelete);
    c.change.insert(QStringLiteral("calendarUri"), item.calendarUri);
    c.change.insert(QStringLiteral("objectUri"), item.objectUri);
    c.change.insert(QStringLiteral("isTodo"), item.kind == ItemKind::Todo);
    c.summary = item.kind == ItemKind::Todo ? AgendaText::tr("delete this to-do") : AgendaText::tr("delete this event");
    return c;
}

EditCommand ganttUpdate(const AgendaItem &item, const QJsonObject &fields)
{
    auto c = base(item, EditKind::GanttUpdate);
    c.change = fields;
    c.change.insert(QStringLiteral("projectId"), item.projectId);
    c.change.insert(QStringLiteral("taskId"), item.taskId);
    c.summary = describe(fields);
    return c;
}

EditCommand ganttAgent(const AgendaItem &item, const QString &agentId, const QString &myUid)
{
    auto c = base(item, EditKind::GanttAgentChange);
    c.change.insert(QStringLiteral("projectId"), item.projectId);
    c.change.insert(QStringLiteral("taskId"), item.taskId);
    c.change.insert(QStringLiteral("agentId"), agentId);
    c.change.insert(QStringLiteral("me"), myUid);
    c.summary = AgendaText::tr("new agent");
    return c;
}

EditCommand deckCard(const AgendaItem &item, const QJsonObject &fields)
{
    auto c = base(item, EditKind::DeckCardUpdate);
    c.change = fields;
    c.change.insert(QStringLiteral("boardId"), item.boardId);
    c.change.insert(QStringLiteral("stackId"), item.stackId);
    c.change.insert(QStringLiteral("cardId"), item.cardId);
    c.summary = describe(fields);
    return c;
}

EditCommand deckAgent(const AgendaItem &item, const QString &agentId, const QString &myUid)
{
    auto c = base(item, EditKind::DeckAgentLabel);
    c.change.insert(QStringLiteral("boardId"), item.boardId);
    c.change.insert(QStringLiteral("stackId"), item.stackId);
    c.change.insert(QStringLiteral("cardId"), item.cardId);
    c.change.insert(QStringLiteral("agentId"), agentId);
    c.change.insert(QStringLiteral("me"), myUid);
    c.summary = AgendaText::tr("new agent");
    return c;
}

} // namespace EditCommands

} // namespace OCC::Nourax::Agenda
