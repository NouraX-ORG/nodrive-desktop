/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "sourcesettings.h"

#include "../model/agendaitem.h"
#include "../ocs/ocsjson.h"

namespace OCC::Nourax::Agenda {

QString toString(CalendarMode mode)
{
    switch (mode) {
    case CalendarMode::Personal:
        return QStringLiteral("personal");
    case CalendarMode::Work:
        return QStringLiteral("work");
    case CalendarMode::Team:
        return QStringLiteral("team");
    case CalendarMode::Info:
        return QStringLiteral("info");
    case CalendarMode::Hidden:
        break;
    }
    return QStringLiteral("hidden");
}

CalendarMode calendarModeFromString(const QString &text, CalendarMode fallback)
{
    for (auto mode : {CalendarMode::Personal, CalendarMode::Work, CalendarMode::Team, CalendarMode::Info, CalendarMode::Hidden}) {
        if (toString(mode) == text) {
            return mode;
        }
    }
    return fallback;
}

QString toString(WorkMode mode)
{
    switch (mode) {
    case WorkMode::DoOversee:
        return QStringLiteral("doOversee");
    case WorkMode::Do:
        return QStringLiteral("do");
    case WorkMode::Hidden:
        break;
    }
    return QStringLiteral("hidden");
}

WorkMode workModeFromString(const QString &text, WorkMode fallback)
{
    for (auto mode : {WorkMode::DoOversee, WorkMode::Do, WorkMode::Hidden}) {
        if (toString(mode) == text) {
            return mode;
        }
    }
    return fallback;
}

ProjectRelation ProjectRelation::of(const QJsonObject &project, const QString &me, bool serverAdmin, const QStringList &myGroups,
    const QSet<int> &myProjectIds, const QJsonArray &shares, bool sharesKnown)
{
    ProjectRelation r;
    r.id = intOf(project.value(QLatin1String("id")));
    r.title = project.value(QLatin1String("title")).toString();
    r.color = project.value(QLatin1String("color")).toString();
    r.ownerId = project.value(QLatin1String("owner_id")).toString();
    r.ownerName = project.value(QLatin1String("owner_display_name")).toString();
    r.permission = project.value(QLatin1String("permission")).toString();
    r.owned = r.ownerId.compare(me, Qt::CaseInsensitive) == 0;
    r.hasMyTasks = myProjectIds.contains(r.id);
    r.serverAdmin = serverAdmin;
    r.sharesKnown = sharesKnown;
    if (r.owned) {
        r.runByRealShare = true;
    } else if (r.permission == QLatin1String("admin")) {
        if (!serverAdmin) {
            r.runByRealShare = true; // the server gives "admin" through a real share
        } else {
            // A server admin sees every project as admin: only a share to me (or to a group of mine) counts.
            for (const auto &entry : shares) {
                const auto share = entry.toObject();
                if (share.value(QLatin1String("permission")).toString() != QLatin1String("admin")) {
                    continue;
                }
                const auto type = share.value(QLatin1String("principal_type")).toString();
                const auto principal = share.value(QLatin1String("principal_id")).toString();
                if ((type == QLatin1String("user") && principal.compare(me, Qt::CaseInsensitive) == 0)
                    || (type == QLatin1String("group") && myGroups.contains(principal))) {
                    r.runByRealShare = true;
                    break;
                }
            }
            r.seenOnlyAsServerAdmin = !r.runByRealShare;
        }
    }
    return r;
}

WorkMode ProjectRelation::defaultMode() const
{
    if (runByRealShare) {
        return WorkMode::DoOversee;
    }
    // Editor or viewer with tasks of mine: Do. Without: Hidden. Seen only as a server admin: Hidden
    // (unless I have tasks there, then I am assigned and it is Do).
    return hasMyTasks ? WorkMode::Do : WorkMode::Hidden;
}

BoardRelation BoardRelation::of(const QJsonObject &board, const QString &me)
{
    BoardRelation r;
    r.id = intOf(board.value(QLatin1String("id")));
    r.title = board.value(QLatin1String("title")).toString();
    r.color = board.value(QLatin1String("color")).toString();
    const auto owner = board.value(QLatin1String("owner")).toObject();
    r.owned = owner.value(QLatin1String("uid")).toString().compare(me, Qt::CaseInsensitive) == 0;
    const auto permissions = board.value(QLatin1String("permissions")).toObject();
    r.canManage = permissions.value(QLatin1String("PERMISSION_MANAGE")).toBool();
    return r;
}

CalendarMode SourceSettings::defaultCalendarMode(const CalendarInfo &c)
{
    if (c.deckBoard) {
        return CalendarMode::Hidden;
    }
    if (c.subscribed || c.birthdays) {
        return CalendarMode::Info;
    }
    if (!c.mine) {
        return CalendarMode::Team;
    }
    return CalendarMode::Personal;
}

QString SourceSettings::autoHiddenReason(const CalendarInfo &c, const QString &taskCopyCalendarUri)
{
    if (c.deckBoard) {
        return AgendaText::tr("Board calendar: NoAgenda reads the board directly");
    }
    if (!taskCopyCalendarUri.isEmpty() && c.uri == taskCopyCalendarUri) {
        return AgendaText::tr("Copy of your NoGantt tasks: NoAgenda reads NoGantt directly");
    }
    return QString();
}

CalendarMode SourceSettings::calendarMode(const CalendarInfo &calendar, const QString &taskCopyCalendarUri) const
{
    if (!autoHiddenReason(calendar, taskCopyCalendarUri).isEmpty()) {
        return CalendarMode::Hidden;
    }
    const auto it = _calendarModes.constFind(calendar.uri);
    return it != _calendarModes.constEnd() ? it.value() : defaultCalendarMode(calendar);
}

void SourceSettings::setCalendarMode(const QString &uri, CalendarMode mode)
{
    _calendarModes.insert(uri, mode);
}

WorkMode SourceSettings::projectMode(const ProjectRelation &relation) const
{
    const auto it = _projectModes.constFind(relation.id);
    return it != _projectModes.constEnd() ? it.value() : relation.defaultMode();
}

void SourceSettings::setProjectMode(int id, WorkMode mode)
{
    _projectModes.insert(id, mode);
}

WorkMode SourceSettings::boardMode(const BoardRelation &relation) const
{
    const auto it = _boardModes.constFind(relation.id);
    return it != _boardModes.constEnd() ? it.value() : relation.defaultMode();
}

void SourceSettings::setBoardMode(int id, WorkMode mode)
{
    _boardModes.insert(id, mode);
}

void SourceSettings::answerPersonalQuestion(const QStringList &personalUris, const QStringList &ownUris, bool skipped)
{
    _personalAnswered = true;
    if (skipped) {
        return; // Skip keeps all as Personal
    }
    for (const auto &uri : ownUris) {
        _calendarModes.insert(uri, personalUris.contains(uri) ? CalendarMode::Personal : CalendarMode::Work);
    }
}

void SourceSettings::setWorkingHours(const WorkingHours &hours)
{
    _workingHours = hours;
    _workingHoursSet = true;
}

QJsonObject SourceSettings::toJson() const
{
    QJsonObject o;
    QJsonObject cal;
    for (auto it = _calendarModes.constBegin(); it != _calendarModes.constEnd(); ++it) {
        cal.insert(it.key(), toString(it.value()));
    }
    o.insert(QStringLiteral("calendars"), cal);
    QJsonObject projects;
    for (auto it = _projectModes.constBegin(); it != _projectModes.constEnd(); ++it) {
        projects.insert(QString::number(it.key()), toString(it.value()));
    }
    o.insert(QStringLiteral("projects"), projects);
    QJsonObject boards;
    for (auto it = _boardModes.constBegin(); it != _boardModes.constEnd(); ++it) {
        boards.insert(QString::number(it.key()), toString(it.value()));
    }
    o.insert(QStringLiteral("boards"), boards);
    o.insert(QStringLiteral("personalAnswered"), _personalAnswered);
    if (_workingHoursSet) {
        QJsonObject hours;
        hours.insert(QStringLiteral("days"), _workingHours.daysMask);
        hours.insert(QStringLiteral("from"), _workingHours.from.toString(QStringLiteral("HH:mm")));
        hours.insert(QStringLiteral("to"), _workingHours.to.toString(QStringLiteral("HH:mm")));
        o.insert(QStringLiteral("workingHours"), hours);
    }
    o.insert(QStringLiteral("view"), lastView);
    o.insert(QStringLiteral("category"), lastCategory);
    o.insert(QStringLiteral("chipMe"), chipMe);
    o.insert(QStringLiteral("chipAgents"), chipAgents);
    o.insert(QStringLiteral("chipTeam"), chipTeam);
    o.insert(QStringLiteral("showAllTeam"), showAllTeam);
    o.insert(QStringLiteral("everOpened"), everOpened);
    return o;
}

SourceSettings SourceSettings::fromJson(const QJsonObject &o)
{
    SourceSettings s;
    const auto cal = o.value(QLatin1String("calendars")).toObject();
    for (auto it = cal.constBegin(); it != cal.constEnd(); ++it) {
        s._calendarModes.insert(it.key(), calendarModeFromString(it.value().toString(), CalendarMode::Personal));
    }
    const auto projects = o.value(QLatin1String("projects")).toObject();
    for (auto it = projects.constBegin(); it != projects.constEnd(); ++it) {
        s._projectModes.insert(it.key().toInt(), workModeFromString(it.value().toString(), WorkMode::Hidden));
    }
    const auto boards = o.value(QLatin1String("boards")).toObject();
    for (auto it = boards.constBegin(); it != boards.constEnd(); ++it) {
        s._boardModes.insert(it.key().toInt(), workModeFromString(it.value().toString(), WorkMode::Hidden));
    }
    s._personalAnswered = o.value(QLatin1String("personalAnswered")).toBool();
    if (o.contains(QLatin1String("workingHours"))) {
        const auto hours = o.value(QLatin1String("workingHours")).toObject();
        s._workingHours.daysMask = hours.value(QLatin1String("days")).toInt(31);
        s._workingHours.from = QTime::fromString(hours.value(QLatin1String("from")).toString(), QStringLiteral("HH:mm"));
        s._workingHours.to = QTime::fromString(hours.value(QLatin1String("to")).toString(), QStringLiteral("HH:mm"));
        if (!s._workingHours.from.isValid() || !s._workingHours.to.isValid()) {
            s._workingHours = WorkingHours();
        } else {
            s._workingHoursSet = true;
        }
    }
    s.lastView = o.value(QLatin1String("view")).toString(QStringLiteral("day"));
    s.lastCategory = o.value(QLatin1String("category")).toString(QStringLiteral("all"));
    s.chipMe = o.value(QLatin1String("chipMe")).toBool(true);
    s.chipAgents = o.value(QLatin1String("chipAgents")).toBool(true);
    s.chipTeam = o.value(QLatin1String("chipTeam")).toBool(true);
    s.showAllTeam = o.value(QLatin1String("showAllTeam")).toBool(false);
    s.everOpened = o.value(QLatin1String("everOpened")).toBool(false);
    return s;
}

} // namespace OCC::Nourax::Agenda
