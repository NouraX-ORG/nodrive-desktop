/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "editcontroller.h"

#include "../edit/ganttagentchange.h"
#include "../logic/ganttmapper.h"

#include <QLocale>
#include <QUuid>

namespace OCC::Nourax::Agenda {

namespace {

constexpr auto kSelf = "SELF";

QString isoDate(const QDate &d)
{
    return d.isValid() ? d.toString(Qt::ISODate) : QString();
}

QString hhmm(const QTime &t)
{
    return t.toString(QStringLiteral("HH:mm"));
}

QString kindName(const AgendaItem &item)
{
    switch (item.kind) {
    case ItemKind::Event:
        return QStringLiteral("event");
    case ItemKind::Todo:
        return QStringLiteral("todo");
    case ItemKind::GanttTask:
        return QStringLiteral("gantt");
    case ItemKind::Milestone:
        return QStringLiteral("milestone");
    case ItemKind::Card:
        return QStringLiteral("card");
    }
    return QString();
}

Home homeOf(const QString &name)
{
    if (name == QLatin1String("tasks")) {
        return Home::Tasks;
    }
    if (name == QLatin1String("gantt")) {
        return Home::Gantt;
    }
    if (name == QLatin1String("board")) {
        return Home::Board;
    }
    return Home::Calendar;
}

QString openLabelOf(Home home)
{
    switch (home) {
    case Home::Calendar:
        return AgendaText::tr("Open in NoCalendar");
    case Home::Tasks:
        return AgendaText::tr("Open in NoTasks");
    case Home::Gantt:
        return AgendaText::tr("Open in NoGantt");
    case Home::Board:
        return AgendaText::tr("Open in NoBoard");
    }
    return QString();
}

QString utcText(const QDateTime &utc)
{
    return utc.toUTC().toString(Qt::ISODate);
}

QString checkDates()
{
    return AgendaText::tr("Check the date: write it like 2026-11-04.");
}

QString checkTimes()
{
    return AgendaText::tr("Check the time: write it like 09:30.");
}

QString endBeforeStart()
{
    return AgendaText::tr("The end can't be before the start.");
}

} // namespace

EditController::EditController(AccountAgenda *agenda, QObject *parent)
    : QObject(parent)
    , _agenda(agenda)
{
    _toastTimer.setSingleShot(true);
    _toastTimer.setInterval(10000);
    connect(&_toastTimer, &QTimer::timeout, this, &EditController::dismissToast);
    if (auto *edit = agenda->edit()) {
        connect(edit, &EditService::finished, this, &EditController::onFinished);
        connect(edit, &EditService::undoFinished, this, &EditController::onUndoFinished);
        connect(edit, &EditService::changed, this, [this] { rebuild(); });
    }
    rebuild();
}

int EditController::pendingCount() const
{
    return _agenda && _agenda->edit() ? _agenda->edit()->queue().pendingCount() : 0;
}

QString EditController::pendingText() const
{
    const int n = pendingCount();
    if (n <= 0) {
        return QString();
    }
    return n == 1 ? AgendaText::tr("Waiting to send 1 change") : AgendaText::tr("Waiting to send %1 changes").arg(n);
}

void EditController::setSelected(const AgendaItem *item)
{
    _hasItem = item != nullptr;
    if (item) {
        _item = *item;
    } else {
        _item = AgendaItem();
    }
    rebuild();
}

// ---- the form of one row -------------------------------------------------------------------------------------------------

QVariantMap EditController::fieldsOf(const AgendaItem &item) const
{
    const auto zone = _agenda->zone();
    QVariantMap f;
    f.insert(QStringLiteral("title"), item.title);
    f.insert(QStringLiteral("description"), item.description);
    f.insert(QStringLiteral("allDay"), item.allDay);
    QString startDate;
    QString startTime = QStringLiteral("09:00");
    QString endDate;
    QString endTime = QStringLiteral("10:00");
    QString dueDate;
    QString dueTime;
    if (item.kind == ItemKind::Event) {
        if (item.allDay) {
            startDate = isoDate(item.startDate);
            endDate = isoDate(item.endDate.isValid() ? item.endDate : item.startDate);
        } else if (item.start.isValid()) {
            const auto s = item.start.toTimeZone(zone);
            const auto e = (item.end.isValid() ? item.end : item.start).toTimeZone(zone);
            startDate = isoDate(s.date());
            startTime = hhmm(s.time());
            endDate = isoDate(e.date());
            endTime = hhmm(e.time());
        }
    } else if (item.kind == ItemKind::Todo) {
        startDate = isoDate(item.startDate);
        dueDate = isoDate(item.due);
        if (item.end.isValid() && item.due.isValid()) {
            dueTime = hhmm(item.end.toTimeZone(zone).time());
        }
    } else if (item.kind == ItemKind::GanttTask) {
        startDate = isoDate(item.startDate);
        endDate = isoDate(item.endDate.isValid() ? item.endDate : item.due);
    } else if (item.kind == ItemKind::Card) {
        dueDate = isoDate(item.due);
    }
    f.insert(QStringLiteral("startDate"), startDate);
    f.insert(QStringLiteral("startTime"), startTime);
    f.insert(QStringLiteral("endDate"), endDate);
    f.insert(QStringLiteral("endTime"), endTime);
    f.insert(QStringLiteral("dueDate"), dueDate);
    f.insert(QStringLiteral("dueTime"), dueTime);
    f.insert(QStringLiteral("percent"), item.percentDone < 0 ? 0 : item.percentDone);
    f.insert(QStringLiteral("status"), item.status);
    return f;
}

QVariantList EditController::agentChoicesFor(const AgendaItem &item, const Editability &e) const
{
    QVariantList out;
    const auto ctx = _agenda->editContext();
    out.append(QVariantMap{
        {QStringLiteral("id"), QString::fromLatin1(kSelf)},
        {QStringLiteral("label"), AgendaText::tr("Self")},
        {QStringLiteral("kind"), QStringLiteral("self")},
        {QStringLiteral("enabled"), e.canChangeAgent},
        {QStringLiteral("selected"), item.agent.isSelf},
        {QStringLiteral("note"), e.canChangeAgent ? QString() : e.agentReason},
    });
    QList<GanttAgentChoice> ganttChoices;
    if (item.home == Home::Gantt) {
        GanttProjectInfo project;
        const auto data = _agenda->ganttSnapshot().projectData.value(item.projectId);
        if (data.loaded) {
            project = GanttProjectInfo::of(ctx.ganttProjectOwners.value(item.projectId), data.tasks);
        }
        ganttChoices = GanttAgentChange::choices(ctx.me, ctx.myName, ctx.registry, ctx.ganttResources, project);
    }
    for (const auto *agent : ctx.registry.ofPerson(ctx.me)) {
        if (agent->isRetired()) {
            continue; // can't be chosen and is not listed
        }
        bool enabled = e.canChangeAgent && agent->isActive();
        QString note;
        if (agent->isPaused()) {
            note = AgendaText::tr("Paused");
        }
        if (item.home == Home::Gantt) {
            for (const auto &c : std::as_const(ganttChoices)) {
                if (c.agentId == agent->id && !c.choosable() && c.resourceId <= 0) {
                    enabled = false;
                    note = c.askManagerText;
                }
            }
        } else if (item.home == Home::Board && agent->boardLabel.isEmpty()) {
            enabled = false;
            note = AgendaText::tr("This agent has no label for boards yet.");
        }
        if (!e.canChangeAgent) {
            note = e.agentReason;
        }
        out.append(QVariantMap{
            {QStringLiteral("id"), agent->id},
            {QStringLiteral("label"), agent->name},
            {QStringLiteral("kind"), agent->isPaused() ? QStringLiteral("paused") : QStringLiteral("agent")},
            {QStringLiteral("enabled"), enabled},
            {QStringLiteral("selected"), !item.agent.isSelf && item.agent.id == agent->id},
            {QStringLiteral("note"), note},
        });
    }
    return out;
}

QVariantMap EditController::formFor(const AgendaItem &item) const
{
    const auto e = EditRules::of(item);
    QVariantMap f;
    f.insert(QStringLiteral("key"), item.key);
    f.insert(QStringLiteral("kind"), kindName(item));
    f.insert(QStringLiteral("canEdit"), e.canEdit);
    f.insert(QStringLiteral("locked"), e.locked);
    f.insert(QStringLiteral("reason"), e.reason);
    f.insert(QStringLiteral("repeats"), e.repeats);
    f.insert(QStringLiteral("canDelete"), e.canDelete);
    f.insert(QStringLiteral("canDone"), e.canDone);
    f.insert(QStringLiteral("canProgress"), e.canProgress);
    f.insert(QStringLiteral("canStatus"), e.canStatus);
    f.insert(QStringLiteral("progressReason"), e.progressReason);
    f.insert(QStringLiteral("canAgent"), e.canChangeAgent);
    f.insert(QStringLiteral("showAgentPicker"), (e.canChangeAgent || item.invitation) && !item.problemOnly && !item.agentLoad && item.workKind != WorkKind::Team
            && (item.kind == ItemKind::Event || item.kind == ItemKind::Todo || item.kind == ItemKind::GanttTask || item.kind == ItemKind::Card));
    f.insert(QStringLiteral("agentReason"), e.agentReason);
    f.insert(QStringLiteral("openLabel"), e.openLabel);
    // A long description is shown cut: it is not offered for changes, so the rest of the text can never be lost.
    const bool descriptionLocked = item.descriptionCut && (item.kind == ItemKind::Event || item.kind == ItemKind::Todo);
    f.insert(QStringLiteral("descriptionLocked"), descriptionLocked);
    f.insert(QStringLiteral("descriptionReason"), descriptionLocked ? AgendaText::tr("This description is too long to change here. %1.").arg(e.openLabel) : QString());
    f.insert(QStringLiteral("fields"), fieldsOf(item));
    f.insert(QStringLiteral("agentChoices"), agentChoicesFor(item, e));
    f.insert(QStringLiteral("pending"), item.pending == PendingState::Waiting);
    f.insert(QStringLiteral("notSent"), item.pending == PendingState::Conflict);
    QVariantList statuses;
    for (const auto &s : EditRules::ganttStatuses()) {
        if (s != QLatin1String("done")) {
            statuses.append(QVariantMap{{QStringLiteral("value"), s}, {QStringLiteral("label"), EditRules::statusText(s)}, {QStringLiteral("selected"), s == item.status}});
        }
    }
    f.insert(QStringLiteral("statuses"), statuses);
    // The conflicts of this row, in plain words.
    QStringList conflicts;
    if (item.clash) {
        conflicts.append(AgendaText::tr("Clash with %1").arg(item.clashWith.join(QStringLiteral(", "))));
    } else if (item.softClash) {
        conflicts.append(AgendaText::tr("Possible clash with %1").arg(item.clashWith.join(QStringLiteral(", "))));
    }
    if (item.deadlineDayOff) {
        conflicts.append(AgendaText::tr("Deadline on a day off"));
    }
    if (item.blocked) {
        conflicts.append(FlagText::blockedBy(item.blockedBy));
    }
    if (item.kind == ItemKind::GanttTask && item.workKind == WorkKind::Self && item.myWorkHours < 0 && !item.problemOnly) {
        conflicts.append(AgendaText::tr("No estimate"));
    }
    f.insert(QStringLiteral("conflicts"), conflicts);
    return f;
}

void EditController::rebuild()
{
    _form = _hasItem ? formFor(_item) : QVariantMap();

    _notSent.clear();
    if (_agenda && _agenda->edit()) {
        for (const auto &c : _agenda->edit()->queue().conflicts()) {
            const Home home = homeOf(c.home);
            _notSent.append(QVariantMap{
                {QStringLiteral("id"), c.id},
                {QStringLiteral("title"), c.title},
                {QStringLiteral("summary"), c.summary},
                {QStringLiteral("message"), c.message},
                {QStringLiteral("kind"), c.conflictKind},
                {QStringLiteral("canApply"), c.conflictKind == QLatin1String("changed")},
                {QStringLiteral("openLabel"), openLabelOf(home)},
            });
        }
    }

    // "+": the writable calendars and lists, and my agents (Self first, and preselected).
    _newChoices.clear();
    if (_agenda) {
        const auto &snapshot = _agenda->calendarSnapshot();
        const auto settings = _agenda->settings();
        const QString taskCopy = _agenda->state().build.taskCopyCalendarUri;
        QVariantList events;
        QVariantList lists;
        for (const auto &cal : snapshot.calendars) {
            if (cal.readOnly || cal.subscribed || cal.ignored || cal.deckBoard || cal.birthdays) {
                continue;
            }
            // Only my own calendars (Personal and Work): in a shared team calendar a new item would have no person,
            // so it would disappear from the list at the next refresh and show to the team (design 4.2, plan 2.4).
            const auto mode = settings.calendarMode(cal, taskCopy);
            if (mode != CalendarMode::Personal && mode != CalendarMode::Work) {
                continue;
            }
            const QVariantMap entry{{QStringLiteral("uri"), cal.uri}, {QStringLiteral("name"), cal.name}, {QStringLiteral("color"), cal.color}};
            if (cal.hasEvents) {
                events.append(entry);
            }
            if (cal.hasTodos) {
                lists.append(entry);
            }
        }
        auto pick = [&snapshot](const QVariantList &list, const QString &last) {
            for (const auto &v : list) {
                if (!last.isEmpty() && v.toMap().value(QStringLiteral("uri")).toString() == last) {
                    return last;
                }
            }
            for (const auto &v : list) {
                if (v.toMap().value(QStringLiteral("uri")).toString() == snapshot.defaultCalendarUri) {
                    return snapshot.defaultCalendarUri;
                }
            }
            return list.isEmpty() ? QString() : list.first().toMap().value(QStringLiteral("uri")).toString();
        };
        QVariantList agents;
        agents.append(QVariantMap{{QStringLiteral("id"), QString::fromLatin1(kSelf)}, {QStringLiteral("label"), AgendaText::tr("Self")}, {QStringLiteral("kind"), QStringLiteral("self")},
            {QStringLiteral("enabled"), true}, {QStringLiteral("selected"), true}, {QStringLiteral("note"), QString()}});
        const auto ctx = _agenda->editContext();
        for (const auto *agent : ctx.registry.ofPerson(ctx.me)) {
            if (agent->isRetired()) {
                continue;
            }
            agents.append(QVariantMap{{QStringLiteral("id"), agent->id}, {QStringLiteral("label"), agent->name}, {QStringLiteral("kind"), agent->isPaused() ? QStringLiteral("paused") : QStringLiteral("agent")},
                {QStringLiteral("enabled"), agent->isActive()}, {QStringLiteral("selected"), false}, {QStringLiteral("note"), agent->isPaused() ? AgendaText::tr("Paused") : QString()}});
        }
        _newChoices.insert(QStringLiteral("events"), events);
        _newChoices.insert(QStringLiteral("lists"), lists);
        _newChoices.insert(QStringLiteral("defaultEvent"), pick(events, _lastEventCalendar));
        _newChoices.insert(QStringLiteral("defaultList"), pick(lists, _lastTodoCalendar));
        _newChoices.insert(QStringLiteral("agents"), agents);
        _newChoices.insert(QStringLiteral("canCreate"), !events.isEmpty() || !lists.isEmpty());
    }
    Q_EMIT changed();
}

// ---- edits ---------------------------------------------------------------------------------------------------------------------

QString EditController::submit(const EditCommand &command)
{
    if (!_agenda || !_agenda->edit()) {
        return AgendaText::tr("Nothing can be changed right now.");
    }
    _moveTexts.remove(command.itemKey);
    _agenda->edit()->submit(command);
    return QString();
}

QJsonObject EditController::agentField(const QString &agentId) const
{
    const auto ctx = _agenda->editContext();
    QJsonObject o;
    o.insert(QStringLiteral("id"), agentId);
    QString tag;
    if (!agentId.isEmpty()) {
        if (const auto *agent = ctx.registry.byId(agentId)) {
            tag = agent->calendarTag;
        }
    }
    o.insert(QStringLiteral("tag"), tag);
    QJsonArray remove;
    for (const auto *agent : ctx.registry.ofPerson(ctx.me)) {
        if (!agent->calendarTag.isEmpty()) {
            remove.append(agent->calendarTag);
        }
    }
    o.insert(QStringLiteral("removeTags"), remove);
    return o;
}

QString EditController::save(const QVariantMap &v)
{
    if (!_hasItem) {
        return AgendaText::tr("Nothing is selected.");
    }
    const auto e = EditRules::of(_item);
    if (!e.canEdit) {
        return e.reason;
    }
    const auto zone = _agenda->zone();
    const auto cur = fieldsOf(_item);
    auto differs = [&](const QString &key) { return v.value(key).toString() != cur.value(key).toString(); };
    QJsonObject fields;

    if (_item.home == Home::Calendar || _item.home == Home::Tasks) {
        const QString title = v.value(QStringLiteral("title")).toString().trimmed();
        if (title.isEmpty()) {
            return AgendaText::tr("Give it a title.");
        }
        if (title != _item.title) {
            fields.insert(QStringLiteral("title"), title);
        }
        if (differs(QStringLiteral("description"))) {
            if (_item.descriptionCut) {
                return AgendaText::tr("This description is too long to change here. %1.").arg(e.openLabel);
            }
            fields.insert(QStringLiteral("description"), v.value(QStringLiteral("description")).toString());
        }
        if (_item.kind == ItemKind::Event) {
            const bool allDay = v.value(QStringLiteral("allDay")).toBool();
            const QDate sd = QDate::fromString(v.value(QStringLiteral("startDate")).toString(), Qt::ISODate);
            const QDate ed = QDate::fromString(v.value(QStringLiteral("endDate")).toString(), Qt::ISODate);
            if (!sd.isValid() || !ed.isValid()) {
                return checkDates();
            }
            const bool timeChanged = allDay != _item.allDay || differs(QStringLiteral("startDate")) || differs(QStringLiteral("endDate"))
                || (!allDay && (differs(QStringLiteral("startTime")) || differs(QStringLiteral("endTime"))));
            if (allDay) {
                if (ed < sd) {
                    return endBeforeStart();
                }
                if (timeChanged) {
                    fields.insert(QStringLiteral("start"), isoDate(sd));
                    fields.insert(QStringLiteral("end"), isoDate(ed.addDays(1))); // a file holds the day after the last day
                }
            } else {
                const QTime st = QTime::fromString(v.value(QStringLiteral("startTime")).toString(), QStringLiteral("HH:mm"));
                const QTime et = QTime::fromString(v.value(QStringLiteral("endTime")).toString(), QStringLiteral("HH:mm"));
                if (!st.isValid() || !et.isValid()) {
                    return checkTimes();
                }
                const QDateTime s(sd, st, zone);
                const QDateTime en(ed, et, zone);
                if (en < s) {
                    return endBeforeStart();
                }
                if (timeChanged) {
                    fields.insert(QStringLiteral("start"), utcText(s));
                    fields.insert(QStringLiteral("end"), utcText(en));
                }
            }
        } else {
            const QString startText = v.value(QStringLiteral("startDate")).toString().trimmed();
            const QString dueText = v.value(QStringLiteral("dueDate")).toString().trimmed();
            const QString dueTimeText = v.value(QStringLiteral("dueTime")).toString().trimmed();
            const QDate sd = QDate::fromString(startText, Qt::ISODate);
            const QDate dd = QDate::fromString(dueText, Qt::ISODate);
            if ((!startText.isEmpty() && !sd.isValid()) || (!dueText.isEmpty() && !dd.isValid())) {
                return checkDates();
            }
            if (sd.isValid() && dd.isValid() && dd < sd) {
                return AgendaText::tr("The due date can't be before the start.");
            }
            if (differs(QStringLiteral("startDate"))) {
                fields.insert(QStringLiteral("start"), startText.isEmpty() ? QString() : isoDate(sd));
            }
            if (differs(QStringLiteral("dueDate")) || differs(QStringLiteral("dueTime"))) {
                if (dueText.isEmpty()) {
                    fields.insert(QStringLiteral("due"), QString());
                } else if (dueTimeText.isEmpty()) {
                    fields.insert(QStringLiteral("due"), isoDate(dd));
                } else {
                    const QTime t = QTime::fromString(dueTimeText, QStringLiteral("HH:mm"));
                    if (!t.isValid()) {
                        return checkTimes();
                    }
                    fields.insert(QStringLiteral("due"), utcText(QDateTime(dd, t, zone)));
                }
            }
        }
        if (fields.isEmpty()) {
            return QString();
        }
        return submit(EditCommands::calendarPatch(_item, fields));
    }

    if (_item.home == Home::Gantt) {
        const QDate sd = QDate::fromString(v.value(QStringLiteral("startDate")).toString(), Qt::ISODate);
        const QDate ed = QDate::fromString(v.value(QStringLiteral("endDate")).toString(), Qt::ISODate);
        if (!sd.isValid() || !ed.isValid()) {
            return checkDates();
        }
        if (ed < sd) {
            return endBeforeStart();
        }
        if (differs(QStringLiteral("startDate"))) {
            fields.insert(QStringLiteral("start_date"), isoDate(sd));
        }
        if (differs(QStringLiteral("endDate"))) {
            fields.insert(QStringLiteral("end_date"), isoDate(ed));
        }
        if (e.canProgress) {
            bool ok = false;
            const int asNumber = v.value(QStringLiteral("percent")).toInt(&ok);
            const int current = cur.value(QStringLiteral("percent")).toInt();
            if (!ok || asNumber < 0 || asNumber > 100) {
                return AgendaText::tr("% done must be a number from 0 to 100.");
            }
            if (asNumber != current) {
                fields.insert(QStringLiteral("manual_progress"), asNumber);
            }
        }
        if (e.canStatus) {
            const QString status = v.value(QStringLiteral("status")).toString();
            if (!status.isEmpty() && status != _item.status) {
                if (status == QLatin1String("done")) {
                    fields.insert(QStringLiteral("done"), true);
                } else {
                    fields.insert(QStringLiteral("status"), status);
                }
            }
        }
        if (fields.isEmpty()) {
            return QString();
        }
        return submit(EditCommands::ganttUpdate(_item, fields));
    }

    if (_item.home == Home::Board) {
        const QString dueText = v.value(QStringLiteral("dueDate")).toString().trimmed();
        const QDate dd = QDate::fromString(dueText, Qt::ISODate);
        if (!dueText.isEmpty() && !dd.isValid()) {
            return checkDates();
        }
        if (differs(QStringLiteral("dueDate"))) {
            fields.insert(QStringLiteral("due"), dueText.isEmpty() ? QString() : isoDate(dd));
        }
        if (fields.isEmpty()) {
            return QString();
        }
        return submit(EditCommands::deckCard(_item, fields));
    }
    return AgendaText::tr("This can't be changed here.");
}

QString EditController::changeAgent(const QString &chosen)
{
    if (!_hasItem) {
        return AgendaText::tr("Nothing is selected.");
    }
    const auto e = EditRules::of(_item);
    if (!e.canChangeAgent) {
        return e.agentReason;
    }
    const QString agentId = chosen == QLatin1String(kSelf) ? QString() : chosen;
    const auto ctx = _agenda->editContext();
    if (!agentId.isEmpty()) {
        const auto *agent = ctx.registry.byId(agentId);
        if (!agent || agent->person.compare(ctx.me, Qt::CaseInsensitive) != 0) {
            return AgendaText::tr("This agent is not one of yours.");
        }
        if (!agent->isActive()) {
            return AgendaText::tr("This agent can't be chosen now.");
        }
    }
    const bool same = agentId.isEmpty() ? _item.agent.isSelf : (!_item.agent.isSelf && _item.agent.id == agentId);
    if (same) {
        return QString();
    }
    if (_item.home == Home::Gantt) {
        return submit(EditCommands::ganttAgent(_item, agentId, ctx.me));
    }
    if (_item.home == Home::Board) {
        return submit(EditCommands::deckAgent(_item, agentId, ctx.me));
    }
    QJsonObject fields;
    fields.insert(QStringLiteral("agent"), agentField(agentId));
    return submit(EditCommands::calendarPatch(_item, fields));
}

QString EditController::markDone()
{
    if (!_hasItem) {
        return AgendaText::tr("Nothing is selected.");
    }
    const auto e = EditRules::of(_item);
    if (!e.canDone) {
        return e.reason.isEmpty() ? AgendaText::tr("This can't be marked done here.") : e.reason;
    }
    QJsonObject fields;
    fields.insert(QStringLiteral("done"), true);
    if (_item.home == Home::Gantt) {
        return submit(EditCommands::ganttUpdate(_item, fields));
    }
    if (_item.home == Home::Board) {
        return submit(EditCommands::deckCard(_item, fields));
    }
    return submit(EditCommands::calendarPatch(_item, fields));
}

QString EditController::deleteSelected()
{
    if (!_hasItem) {
        return AgendaText::tr("Nothing is selected.");
    }
    const auto e = EditRules::of(_item);
    if (!e.canDelete) {
        // A NoGantt task or a card is never deleted here.
        return e.reason.isEmpty() ? AgendaText::tr("This can't be deleted here. %1.").arg(e.openLabel) : e.reason;
    }
    return submit(EditCommands::calendarDelete(_item));
}

QString EditController::create(const QVariantMap &v)
{
    const bool todo = v.value(QStringLiteral("kind")).toString() == QLatin1String("todo");
    const auto zone = _agenda->zone();
    const QString calendarUri = v.value(QStringLiteral("calendarUri")).toString();
    bool known = false;
    for (const auto &c : _newChoices.value(todo ? QStringLiteral("lists") : QStringLiteral("events")).toList()) {
        known = known || c.toMap().value(QStringLiteral("uri")).toString() == calendarUri;
    }
    if (!known) {
        return todo ? AgendaText::tr("Choose a list.") : AgendaText::tr("Choose a calendar.");
    }
    if (!v.contains(QStringLiteral("agentId")) || v.value(QStringLiteral("agentId")).toString().isEmpty()) {
        return AgendaText::tr("Choose who does it: Self or one of your agents.");
    }
    const QString chosen = v.value(QStringLiteral("agentId")).toString();
    const QString agentId = chosen == QLatin1String(kSelf) ? QString() : chosen;
    const auto ctx = _agenda->editContext();
    if (!agentId.isEmpty()) {
        const auto *agent = ctx.registry.byId(agentId);
        if (!agent || agent->person.compare(ctx.me, Qt::CaseInsensitive) != 0 || !agent->isActive()) {
            return AgendaText::tr("This agent can't be chosen now.");
        }
    }
    const QString title = v.value(QStringLiteral("title")).toString().trimmed();
    if (title.isEmpty()) {
        return AgendaText::tr("Give it a title.");
    }
    QJsonObject fields;
    fields.insert(QStringLiteral("title"), title);
    if (!v.value(QStringLiteral("description")).toString().isEmpty()) {
        fields.insert(QStringLiteral("description"), v.value(QStringLiteral("description")).toString());
    }
    if (!todo) {
        const bool allDay = v.value(QStringLiteral("allDay")).toBool();
        const QDate sd = QDate::fromString(v.value(QStringLiteral("startDate")).toString(), Qt::ISODate);
        QDate ed = QDate::fromString(v.value(QStringLiteral("endDate")).toString(), Qt::ISODate);
        if (!ed.isValid()) {
            ed = sd;
        }
        if (!sd.isValid()) {
            return checkDates();
        }
        if (allDay) {
            if (ed < sd) {
                return endBeforeStart();
            }
            fields.insert(QStringLiteral("start"), isoDate(sd));
            fields.insert(QStringLiteral("end"), isoDate(ed.addDays(1)));
        } else {
            const QTime st = QTime::fromString(v.value(QStringLiteral("startTime")).toString(), QStringLiteral("HH:mm"));
            const QTime et = QTime::fromString(v.value(QStringLiteral("endTime")).toString(), QStringLiteral("HH:mm"));
            if (!st.isValid() || !et.isValid()) {
                return checkTimes();
            }
            const QDateTime s(sd, st, zone);
            const QDateTime en(ed, et, zone);
            if (en < s) {
                return endBeforeStart();
            }
            fields.insert(QStringLiteral("start"), utcText(s));
            fields.insert(QStringLiteral("end"), utcText(en));
        }
    } else {
        const QString startText = v.value(QStringLiteral("startDate")).toString().trimmed();
        const QString dueText = v.value(QStringLiteral("dueDate")).toString().trimmed();
        const QString dueTimeText = v.value(QStringLiteral("dueTime")).toString().trimmed();
        const QDate sd = QDate::fromString(startText, Qt::ISODate);
        const QDate dd = QDate::fromString(dueText, Qt::ISODate);
        if ((!startText.isEmpty() && !sd.isValid()) || (!dueText.isEmpty() && !dd.isValid())) {
            return checkDates();
        }
        if (sd.isValid() && dd.isValid() && dd < sd) {
            return AgendaText::tr("The due date can't be before the start.");
        }
        if (sd.isValid()) {
            fields.insert(QStringLiteral("start"), isoDate(sd));
        }
        if (dd.isValid()) {
            if (dueTimeText.isEmpty()) {
                fields.insert(QStringLiteral("due"), isoDate(dd));
            } else {
                const QTime t = QTime::fromString(dueTimeText, QStringLiteral("HH:mm"));
                if (!t.isValid()) {
                    return checkTimes();
                }
                fields.insert(QStringLiteral("due"), utcText(QDateTime(dd, t, zone)));
            }
        }
    }
    if (!agentId.isEmpty()) {
        fields.insert(QStringLiteral("agent"), agentField(agentId)); // Self: neither the property nor the tag
        auto agent = fields.value(QStringLiteral("agent")).toObject();
        agent.insert(QStringLiteral("removeTags"), QJsonArray());
        fields.insert(QStringLiteral("agent"), agent);
    }
    (todo ? _lastTodoCalendar : _lastEventCalendar) = calendarUri;
    const QString uid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return submit(EditCommands::calendarCreate(calendarUri, todo, uid, fields, title));
}

QString EditController::moveByDays(const QString &itemKey, int days)
{
    if (!_agenda || !_agenda->edit()) {
        return AgendaText::tr("Nothing can be changed right now.");
    }
    const AgendaItem *found = nullptr;
    for (const auto &candidate : _agenda->state().items) {
        if (candidate.key == itemKey) {
            found = &candidate;
            break;
        }
    }
    if (!found) {
        return AgendaText::tr("Nothing is selected.");
    }
    const AgendaItem item = *found;
    const auto e = EditRules::of(item);
    if (!e.canEdit) {
        return e.reason.isEmpty() ? AgendaText::tr("This can't be moved here.") : e.reason;
    }
    if (days == 0) {
        return QString();
    }
    const auto zone = _agenda->zone();
    QJsonObject fields;
    QDate newStart;
    QDate newEnd;
    if (item.home == Home::Gantt && item.kind == ItemKind::GanttTask) {
        if (!item.startDate.isValid()) {
            return AgendaText::tr("This task has no dates to move.");
        }
        const QDate end = item.endDate.isValid() ? item.endDate : item.startDate;
        newStart = item.startDate.addDays(days);
        newEnd = end.addDays(days);
        fields.insert(QStringLiteral("start_date"), isoDate(newStart));
        fields.insert(QStringLiteral("end_date"), isoDate(newEnd));
        // The command itself carries the dates of the drop, so the sentence is right even for a command that is sent later (offline) or
        // that was merged with another move of the same bar. The toast is written in onFinished().
        auto command = EditCommands::ganttUpdate(item, fields);
        command.change.insert(QStringLiteral("move"), QJsonObject{{QStringLiteral("start"), isoDate(newStart)}, {QStringLiteral("end"), isoDate(newEnd)}});
        _agenda->edit()->submit(command);
        return QString();
    }
    if (item.home == Home::Tasks && item.kind == ItemKind::Todo) {
        if (item.repeating || !item.startDate.isValid() || !item.due.isValid()) {
            return AgendaText::tr("This can't be moved here.");
        }
        newStart = item.startDate.addDays(days);
        newEnd = item.due.addDays(days);
        // The time of day of the start and of the due date stays as it is.
        if (item.startStamp.isValid()) {
            fields.insert(QStringLiteral("start"), utcText(QDateTime(newStart, item.startStamp.toTimeZone(zone).time(), zone)));
        } else {
            fields.insert(QStringLiteral("start"), isoDate(newStart));
        }
        if (item.end.isValid()) {
            fields.insert(QStringLiteral("due"), utcText(QDateTime(newEnd, item.end.toTimeZone(zone).time(), zone)));
        } else {
            fields.insert(QStringLiteral("due"), isoDate(newEnd));
        }
        auto command = EditCommands::calendarPatch(item, fields);
        command.change.insert(QStringLiteral("move"), QJsonObject{{QStringLiteral("start"), isoDate(newStart)}, {QStringLiteral("end"), isoDate(newEnd)}});
        _agenda->edit()->submit(command);
        return QString();
    }
    return AgendaText::tr("This can't be moved here.");
}

// ---- conflicts, toast ---------------------------------------------------------------------------------------------------------

void EditController::applyAgain(const QString &commandId)
{
    if (_agenda && _agenda->edit()) {
        _agenda->edit()->applyAgain(commandId);
    }
}

void EditController::keepOther(const QString &commandId)
{
    if (_agenda && _agenda->edit()) {
        _agenda->edit()->keepOther(commandId);
    }
}

void EditController::openNotSent(const QString &commandId)
{
    if (!_agenda || !_agenda->edit()) {
        return;
    }
    if (const auto *c = _agenda->edit()->queue().findConflict(commandId)) {
        Q_EMIT openUrlRequested(_agenda->appUrl(homeOf(c->home)));
    }
}

void EditController::showToast(const QString &text, const QString &tone, const QString &undoOp)
{
    _toast = text;
    _toastTone = tone;
    _undoOp = undoOp;
    _toastTimer.start();
    Q_EMIT changed();
}

void EditController::dismissToast()
{
    _toast.clear();
    _toastTone.clear();
    _undoOp.clear();
    _toastTimer.stop();
    Q_EMIT changed();
}

void EditController::undo()
{
    if (_undoOp.isEmpty() || !_agenda || !_agenda->edit()) {
        return;
    }
    const QString op = _undoOp;
    // "Undone." is shown only when NoGantt has said so (onUndoFinished); until then the person is told it is being done.
    showToast(AgendaText::tr("Undoing the move."), QStringLiteral("info"));
    _agenda->edit()->undo(op);
}

void EditController::onUndoFinished(const EditResult &result)
{
    if (result.done()) {
        showToast(AgendaText::tr("Undone."), QStringLiteral("ok"));
        return;
    }
    QString text = result.message.isEmpty() ? AgendaText::tr("NoGantt could not undo the move.") : result.message;
    const QString open = AgendaText::tr("Open in NoGantt");
    if (!text.contains(open)) {
        text += QLatin1Char(' ') + open + QLatin1Char('.');
    }
    showToast(text, QStringLiteral("warn"));
}

// "Moved to 2 Nov - 4 Nov." for a drag of the Timeline (the command holds a "move" object), else "Saved.". The dates are the ones the
// server saved when it says so: NoGantt puts a moved task onto working days and works out its end from its length in working days,
// so they can differ from the drop; the person is told that, never the dates of the drop alone.
QString EditController::movedText(const EditCommand &command, const EditResult &result) const
{
    const auto move = command.change.value(QStringLiteral("move")).toObject();
    if (move.isEmpty()) {
        return AgendaText::tr("Saved.");
    }
    QDate start = QDate::fromString(move.value(QStringLiteral("start")).toString(), Qt::ISODate);
    QDate end = QDate::fromString(move.value(QStringLiteral("end")).toString(), Qt::ISODate);
    bool kept = false;
    if (command.kind == EditKind::GanttUpdate && result.savedStart.isValid() && result.savedEnd.isValid()) {
        kept = result.savedStart != start || result.savedEnd != end;
        start = result.savedStart;
        end = result.savedEnd;
    }
    if (!start.isValid() || !end.isValid()) {
        return AgendaText::tr("Saved.");
    }
    QString text = AgendaText::tr("Moved to %1 - %2").arg(QLocale().toString(start, QStringLiteral("d MMM")), QLocale().toString(end, QStringLiteral("d MMM"))) + QLatin1Char('.');
    if (kept) {
        text += QLatin1Char(' ') + AgendaText::tr("NoGantt kept it on working days.");
    }
    return text;
}

void EditController::onFinished(const EditCommand &command, const EditResult &result)
{
    switch (result.outcome) {
    case EditResult::Outcome::Done: {
        QString text = movedText(command, result);
        if (!result.alsoMoved.isEmpty()) {
            text += QLatin1Char(' ') + AgendaText::tr("Also moved: %1").arg(result.alsoMoved.join(QStringLiteral(", ")));
        }
        if (!result.note.isEmpty()) {
            text += QLatin1Char(' ') + result.note;
        }
        showToast(text, result.checkInNoGantt || !result.note.isEmpty() ? QStringLiteral("warn") : QStringLiteral("ok"), result.undoOpId);
        return;
    }
    case EditResult::Outcome::Offline:
        showToast(AgendaText::tr("There is no connection. Your change is waiting to be sent."), QStringLiteral("info"));
        return;
    case EditResult::Outcome::Conflict:
    case EditResult::Outcome::Refused:
        showToast(result.message + QLatin1Char(' ') + AgendaText::tr("It is in the list of changes that were not sent."), QStringLiteral("warn"));
        return;
    case EditResult::Outcome::NeedsLogin:
        showToast(result.message, QStringLiteral("warn"));
        return;
    case EditResult::Outcome::Failed:
        showToast(result.message + QLatin1Char(' ') + AgendaText::tr("NoAgenda will try again."), QStringLiteral("warn"));
        return;
    }
}

} // namespace OCC::Nourax::Agenda
