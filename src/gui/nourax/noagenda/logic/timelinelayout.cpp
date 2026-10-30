/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "timelinelayout.h"

#include "../edit/editability.h"
#include "workload.h"

#include <QHash>
#include <QMap>

#include <algorithm>
#include <cmath>
#include <utility>

namespace OCC::Nourax::Agenda {

// ---- the scale ---------------------------------------------------------------------------------------------------------

double TimelineScale::pxPerDayFor(TimelineZoom zoom)
{
    return zoom == TimelineZoom::Days ? 40.0 : 12.0;
}

double TimelineScale::xOfDay(const QDate &day) const
{
    return static_cast<double>(first.daysTo(day)) * pxPerDay;
}

QDate TimelineScale::dayAtX(double x) const
{
    return first.addDays(static_cast<qint64>(std::floor(x / pxPerDay)));
}

int TimelineScale::snapDays(double dx, double pxPerDay)
{
    if (pxPerDay <= 0) {
        return 0;
    }
    return static_cast<int>(std::lround(dx / pxPerDay));
}

namespace TimelineLayout {

namespace {

QString agentKindOf(const AgentRef &a)
{
    if (a.isSelf) {
        return QStringLiteral("self");
    }
    if (a.otherOrganisation) {
        return QStringLiteral("other");
    }
    if (a.unknown) {
        return QStringLiteral("unknown");
    }
    if (a.retired) {
        return QStringLiteral("retired");
    }
    if (a.paused) {
        return QStringLiteral("paused");
    }
    return QStringLiteral("agent");
}

/// One thing to draw, before the rows are known.
struct Candidate
{
    const AgendaItem *item = nullptr;
    bool diamond = false;
    QDate start;
    QDate end;
};

bool wanted(const AgendaItem &item, Candidate *out)
{
    if (item.info || item.problemOnly || item.agentLoad || item.done || item.workKind == WorkKind::Team) {
        return false;
    }
    out->item = &item;
    switch (item.kind) {
    case ItemKind::GanttTask: {
        if (item.projectId <= 0) {
            return false;
        }
        QDate s = item.startDate;
        QDate e = item.endDate.isValid() ? item.endDate : item.due;
        if (!s.isValid() && !e.isValid()) {
            return false;
        }
        if (!s.isValid()) {
            s = e;
        }
        if (!e.isValid() || e < s) {
            e = s;
        }
        out->start = s;
        out->end = e;
        out->diamond = false;
        return true;
    }
    case ItemKind::Todo:
        // Only a to-do with both a start and a due date is a bar, and a repeating one is not (it is changed in NoTasks).
        if (item.repeating || !item.startDate.isValid() || !item.due.isValid() || item.due < item.startDate) {
            return false;
        }
        out->start = item.startDate;
        out->end = item.due;
        out->diamond = false;
        return true;
    case ItemKind::Milestone:
        if (!item.startDate.isValid()) {
            return false;
        }
        out->start = out->end = item.startDate;
        out->diamond = true;
        return true;
    case ItemKind::Card:
        if (!item.due.isValid()) {
            return false;
        }
        out->start = out->end = item.due;
        out->diamond = true;
        return true;
    case ItemKind::Event:
        return false;
    }
    return false;
}

struct Group
{
    QString key;
    QString title;
    QString color;
    int order = 0; ///< 0 projects, 1 lists, 2 boards
    int projectId = 0;
    QList<Candidate> loose;
    QMap<int, QList<Candidate>> phases;
    QMap<int, QString> phaseTitles;
};

bool before(const Candidate &a, const Candidate &b)
{
    if (a.start != b.start) {
        return a.start < b.start;
    }
    if (a.end != b.end) {
        return a.end < b.end;
    }
    const int t = QString::compare(a.item->title, b.item->title, Qt::CaseInsensitive);
    if (t != 0) {
        return t < 0;
    }
    return a.item->key < b.item->key;
}

QDate earliest(const QList<Candidate> &list)
{
    QDate d;
    for (const auto &c : list) {
        if (!d.isValid() || c.start < d) {
            d = c.start;
        }
    }
    return d;
}

QString dash()
{
    return QString(QChar(0x203A)); // a single right-pointing angle quotation mark
}

} // namespace

QString typeText(const QString &type)
{
    if (type == QLatin1String("AA")) {
        return AgendaText::tr("Start to start");
    }
    if (type == QLatin1String("EE")) {
        return AgendaText::tr("Finish to finish");
    }
    if (type == QLatin1String("AE")) {
        return AgendaText::tr("Start to finish");
    }
    return AgendaText::tr("Finish to start");
}

QList<QPointF> route(const QPointF &from, bool fromFinish, const QPointF &to, bool toStart)
{
    const double sf = fromFinish ? 1.0 : -1.0; // the line leaves the predecessor to the right (finish) or to the left (start)
    const double st = toStart ? -1.0 : 1.0; // and arrives from the left (start) or from the right (finish)
    const QPointF a(from.x() + sf * kElbow, from.y());
    const QPointF b(to.x() + st * kElbow, to.y());
    QList<QPointF> points;
    points.append(from);
    if (sf > 0 && st < 0 && b.x() >= a.x()) {
        // The usual finish -> start: one vertical line in the channel next to the predecessor, then straight in.
        points.append(a);
        points.append(QPointF(a.x(), to.y()));
    } else {
        const double midY = (from.y() + to.y()) / 2.0;
        points.append(a);
        points.append(QPointF(a.x(), midY));
        points.append(QPointF(b.x(), midY));
        points.append(b);
    }
    points.append(to);
    return points;
}

TimelineDrag drag(const TimelineEntry &entry, double dx, double pxPerDay)
{
    TimelineDrag d;
    if (entry.type != TimelineEntry::Type::Bar || !entry.canMove) {
        d.reason = entry.lockReason;
        d.start = entry.start;
        d.end = entry.end;
        return d;
    }
    d.allowed = true;
    d.days = TimelineScale::snapDays(dx, pxPerDay);
    d.start = entry.start.addDays(d.days);
    d.end = entry.end.addDays(d.days);
    return d;
}

TimelineLayoutResult build(const TimelineLayoutInput &input)
{
    TimelineLayoutResult result;
    result.scale = input.scale;
    const auto &scale = input.scale;
    const QDate last = scale.last();

    // ---- what is drawn --------------------------------------------------------------------------------------------------
    QMap<QString, Group> groups;
    for (const auto &item : input.items) {
        Candidate c;
        if (!wanted(item, &c)) {
            continue;
        }
        if (c.end < scale.first || c.start > last) {
            continue; // outside the range
        }
        QString key;
        Group fresh;
        if (item.home == Home::Gantt) {
            key = QStringLiteral("gantt:p") + QString::number(item.projectId);
            fresh.title = item.projectName.isEmpty() ? item.sourceName : item.projectName;
            fresh.order = 0;
            fresh.projectId = item.projectId;
        } else if (item.kind == ItemKind::Todo) {
            key = QStringLiteral("todo:") + item.calendarUri;
            fresh.title = AgendaText::tr("NoTasks") + QLatin1Char(' ') + dash() + QLatin1Char(' ') + item.sourceName;
            fresh.order = 1;
        } else {
            key = QStringLiteral("board:") + QString::number(item.boardId);
            fresh.title = AgendaText::tr("NoBoard") + QLatin1Char(' ') + dash() + QLatin1Char(' ') + item.sourceName;
            fresh.order = 2;
        }
        auto it = groups.find(key);
        if (it == groups.end()) {
            fresh.key = key;
            fresh.color = item.sourceColor;
            it = groups.insert(key, fresh);
        }
        if (item.home == Home::Gantt && item.phaseId > 0 && !item.phaseName.isEmpty() && item.kind == ItemKind::GanttTask) {
            it->phases[item.phaseId].append(c);
            it->phaseTitles.insert(item.phaseId, item.phaseName);
        } else {
            it->loose.append(c);
        }
    }

    QList<Group> ordered = groups.values();
    std::sort(ordered.begin(), ordered.end(), [](const Group &a, const Group &b) {
        if (a.order != b.order) {
            return a.order < b.order;
        }
        const int t = QString::compare(a.title, b.title, Qt::CaseInsensitive);
        return t != 0 ? t < 0 : a.key < b.key;
    });

    // ---- rows ---------------------------------------------------------------------------------------------------------------
    int row = 0;
    QHash<QString, int> entryOfKey; // item key -> index in result.entries
    QHash<QString, const AgendaItem *> itemOfKey;
    auto addItem = [&](const Candidate &c, const QString &groupKey) {
        const auto &item = *c.item;
        TimelineEntry e;
        e.type = c.diamond ? TimelineEntry::Type::Diamond : TimelineEntry::Type::Bar;
        e.row = row++;
        e.key = item.key;
        e.groupKey = groupKey;
        e.title = item.title;
        e.color = item.sourceColor;
        e.projectId = item.projectId;
        switch (item.kind) {
        case ItemKind::GanttTask:
            e.glyph = QStringLiteral("task");
            break;
        case ItemKind::Todo:
            e.glyph = QStringLiteral("todo");
            break;
        case ItemKind::Milestone:
            e.glyph = QStringLiteral("milestone");
            break;
        default:
            e.glyph = QStringLiteral("card");
            break;
        }
        e.start = c.start;
        e.end = c.end;
        e.percent = item.percentDone;
        e.agentText = item.agent.displayText();
        e.agentKind = agentKindOf(item.agent);
        e.late = item.late;
        e.blocked = item.blocked;
        e.waiting = item.pending == PendingState::Waiting || item.pending == PendingState::Sending;
        e.notSent = item.pending == PendingState::Conflict;

        const auto rules = EditRules::of(item);
        const bool isBar = !c.diamond;
        if (isBar) {
            // A NoGantt task with only an end date is drawn as a one-day bar, but there is no start to move.
            const bool noStart = item.kind == ItemKind::GanttTask && !item.startDate.isValid();
            e.canMove = rules.canEdit && item.pending != PendingState::Conflict && !noStart;
            if (!e.canMove) {
                e.locked = true;
                if (noStart && rules.canEdit) {
                    e.lockReason = AgendaText::tr("This task has no dates to move.");
                } else if (item.pending == PendingState::Conflict) {
                    e.lockReason = AgendaText::tr("A change to this was not sent. See the list of changes that were not sent.");
                } else {
                    e.lockReason = rules.reason.isEmpty() ? AgendaText::tr("This can't be moved here.") : rules.reason;
                }
            }
        } else {
            e.locked = item.readOnly || item.versionReadOnly;
            e.lockReason = rules.reason;
        }

        const double y = e.row * kRowHeight;
        if (c.diamond) {
            const double cx = scale.xOfDay(c.start) + scale.pxPerDay / 2.0;
            e.rect = QRectF(cx - kDiamond / 2.0, y + (kRowHeight - kDiamond) / 2.0, kDiamond, kDiamond);
        } else {
            const QDate shownStart = qMax(c.start, scale.first);
            const QDate shownEnd = qMin(c.end, last);
            e.clippedLeft = c.start < scale.first;
            e.clippedRight = c.end > last;
            const double x0 = scale.xOfDay(shownStart);
            const double x1 = scale.xAfterDay(shownEnd);
            e.rect = QRectF(x0, y + (kRowHeight - kBarHeight) / 2.0, qMax(x1 - x0, scale.pxPerDay), kBarHeight);
        }
        entryOfKey.insert(e.key, static_cast<int>(result.entries.size()));
        itemOfKey.insert(e.key, &item);
        result.entries.append(e);
    };
    auto addGroup = [&](const QString &key, const QString &title, const QString &color, int level) {
        TimelineEntry e;
        e.type = TimelineEntry::Type::Group;
        e.level = level;
        e.row = row++;
        e.key = key;
        e.groupKey = key;
        e.title = title;
        e.color = color;
        result.entries.append(e);
    };

    for (auto &group : ordered) {
        addGroup(group.key, group.title, group.color, 0);
        std::sort(group.loose.begin(), group.loose.end(), before);
        for (const auto &c : std::as_const(group.loose)) {
            addItem(c, group.key);
        }
        QList<int> phaseIds = group.phases.keys();
        std::sort(phaseIds.begin(), phaseIds.end(), [&group](int a, int b) {
            const QDate da = earliest(group.phases.value(a));
            const QDate db = earliest(group.phases.value(b));
            if (da != db) {
                return da < db;
            }
            const int t = QString::compare(group.phaseTitles.value(a), group.phaseTitles.value(b), Qt::CaseInsensitive);
            return t != 0 ? t < 0 : a < b;
        });
        for (const int phaseId : std::as_const(phaseIds)) {
            const QString phaseKey = group.key + QStringLiteral(":s") + QString::number(phaseId);
            addGroup(phaseKey, group.phaseTitles.value(phaseId), group.color, 1);
            auto list = group.phases.value(phaseId);
            std::sort(list.begin(), list.end(), before);
            for (const auto &c : std::as_const(list)) {
                addItem(c, phaseKey);
            }
        }
    }
    result.rowCount = row;
    result.height = row * kRowHeight;

    // ---- arrows (read only) ---------------------------------------------------------------------------------------------
    for (const auto &entry : std::as_const(result.entries)) {
        if (entry.type == TimelineEntry::Type::Group) {
            continue;
        }
        // The dependencies live on the item.
        const AgendaItem *item = itemOfKey.value(entry.key, nullptr);
        if (!item) {
            continue;
        }
        for (const auto &dep : item->dependencies) {
            const bool toStart = dep.type == QLatin1String("EA") || dep.type == QLatin1String("AA");
            const bool fromFinish = dep.type == QLatin1String("EA") || dep.type == QLatin1String("EE");
            const double toX = toStart ? entry.rect.left() : entry.rect.right();
            const double toY = entry.rect.center().y();
            TimelineArrow arrow;
            arrow.toKey = entry.key;
            arrow.type = dep.type;
            arrow.lagDays = dep.lagDays;
            const auto found = entryOfKey.constFind(dep.predecessorKey);
            if (found != entryOfKey.constEnd()) {
                const auto &pred = result.entries.at(found.value());
                arrow.fromKey = pred.key;
                const double fromX = fromFinish ? pred.rect.right() : pred.rect.left();
                arrow.points = route(QPointF(fromX, pred.rect.center().y()), fromFinish, QPointF(toX, toY), toStart);
                QString text = AgendaText::tr("%1 to %2 (%3)").arg(pred.title, entry.title, typeText(dep.type));
                if (!qFuzzyIsNull(dep.lagDays)) {
                    text += QStringLiteral(", ") + AgendaText::tr("lag %1 days").arg(QString::number(dep.lagDays, 'g', 4));
                }
                arrow.tooltip = text;
            } else {
                // A predecessor that is not on the screen (someone else's task, or outside the range): a short stub.
                arrow.stub = true;
                const double sx = toStart ? toX - kStub : toX + kStub;
                arrow.points = {QPointF(sx, toY), QPointF(toX, toY)};
                const QString title = dep.predecessorTitle.isEmpty() ? AgendaText::tr("another task") : dep.predecessorTitle;
                arrow.tooltip = dep.predecessorOwner.isEmpty() ? AgendaText::tr("After: %1").arg(title) : AgendaText::tr("After: %1 (%2)").arg(title, dep.predecessorOwner);
                if (!qFuzzyIsNull(dep.lagDays)) {
                    arrow.tooltip += QStringLiteral(", ") + AgendaText::tr("lag %1 days").arg(QString::number(dep.lagDays, 'g', 4));
                }
            }
            result.arrows.append(arrow);
        }
    }

    // ---- my busy time ---------------------------------------------------------------------------------------------------
    const auto &all = input.allItems.isEmpty() ? input.items : input.allItems;
    for (int i = 0; i < scale.days; ++i) {
        const QDate day = scale.first.addDays(i);
        const auto load = Workload::forDay(all, input.dayModel, day);
        TimelineShade shade;
        shade.day = day;
        shade.x = i * scale.pxPerDay;
        shade.width = scale.pxPerDay;
        shade.dayOff = !load.workingDay;
        if (load.workingDay && load.working > 0) {
            shade.alpha = 0.35 * qBound(0.0, load.busy / load.working, 1.0);
        }
        result.shading.append(shade);
    }
    if (input.today.isValid() && input.today >= scale.first && input.today <= last) {
        result.todayX = scale.xOfDay(input.today);
    }
    return result;
}

} // namespace TimelineLayout

} // namespace OCC::Nourax::Agenda
