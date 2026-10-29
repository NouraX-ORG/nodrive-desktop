/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "agendacontroller.h"

#include "../logic/agendacontext.h"
#include "theme.h"

#include <QLocale>
#include <QSet>

#include <algorithm>

namespace OCC::Nourax::Agenda {

namespace {

QString kindText(ItemKind kind)
{
    switch (kind) {
    case ItemKind::Event:
        return AgendaText::tr("Event");
    case ItemKind::Todo:
        return AgendaText::tr("To-do");
    case ItemKind::GanttTask:
        return AgendaText::tr("Task");
    case ItemKind::Milestone:
        return AgendaText::tr("Milestone");
    case ItemKind::Card:
        return AgendaText::tr("Card");
    }
    return QString();
}

QString homeText(Home home)
{
    switch (home) {
    case Home::Calendar:
        return AgendaText::tr("NoCalendar");
    case Home::Tasks:
        return AgendaText::tr("NoTasks");
    case Home::Gantt:
        return AgendaText::tr("NoGantt");
    case Home::Board:
        return AgendaText::tr("NoBoard");
    }
    return QString();
}

QString glyphOf(ItemKind kind)
{
    switch (kind) {
    case ItemKind::Event:
        return QStringLiteral("event");
    case ItemKind::Todo:
        return QStringLiteral("todo");
    case ItemKind::GanttTask:
        return QStringLiteral("task");
    case ItemKind::Milestone:
        return QStringLiteral("milestone");
    case ItemKind::Card:
        return QStringLiteral("card");
    }
    return QString();
}

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

QString sourceLabel(const QString &key)
{
    if (key == QLatin1String("calendar")) {
        return AgendaText::tr("NoCalendar");
    }
    if (key == QLatin1String("gantt")) {
        return AgendaText::tr("NoGantt");
    }
    if (key == QLatin1String("board")) {
        return AgendaText::tr("NoBoard");
    }
    return AgendaText::tr("The agent list");
}

QVariantMap option(const QString &value, const QString &label, const QString &current)
{
    return QVariantMap{{QStringLiteral("value"), value}, {QStringLiteral("label"), label}, {QStringLiteral("selected"), value == current}};
}

} // namespace

AgendaController::AgendaController(AccountAgenda *agenda, QObject *parent)
    : QObject(parent)
    , _agenda(agenda)
{
    const auto settings = agenda->settings();
    _view = settings.lastView;
    _category = settings.lastCategory;
    _chipMe = settings.chipMe;
    _chipAgents = settings.chipAgents;
    _chipTeam = settings.chipTeam;
    _showAllTeam = settings.showAllTeam;
    _date = agenda->state().today.isValid() ? agenda->state().today : QDate::currentDate();

    _style.setDark(Theme::instance()->darkMode());
    connect(Theme::instance(), &Theme::darkModeChanged, this, [this] { _style.setDark(Theme::instance()->darkMode()); });
    connect(agenda, &AccountAgenda::stateChanged, this, [this] { rebuild(); });
    rebuild();
}

void AgendaController::setAccountName(const QString &name)
{
    _accountName = name;
    Q_EMIT changed();
}

void AgendaController::setViewMode(const QString &mode)
{
    if (mode != QLatin1String("day") && mode != QLatin1String("week") && mode != QLatin1String("month")) {
        return;
    }
    _view = mode;
    _agenda->setView(mode);
    rebuild();
}

void AgendaController::setCategory(const QString &category)
{
    if (category != QLatin1String("all") && category != QLatin1String("work") && category != QLatin1String("personal")) {
        return;
    }
    _category = category;
    _agenda->setCategory(category);
    rebuild();
}

void AgendaController::chooseChip(const QString &which, bool on)
{
    if (which == QLatin1String("me")) {
        _chipMe = on;
    } else if (which == QLatin1String("agents")) {
        _chipAgents = on;
    } else if (which == QLatin1String("team")) {
        _chipTeam = on;
    }
    _agenda->setChips(_chipMe, _chipAgents, _chipTeam);
    rebuild();
}

void AgendaController::setShowAllTeam(bool show)
{
    _showAllTeam = show;
    _agenda->setShowAllTeam(show);
    rebuild();
}

void AgendaController::setPage(const QString &page)
{
    _page = page;
    Q_EMIT changed();
}

bool AgendaController::hasTeamSection() const
{
    return _agenda && _agenda->state().build.anyOversee;
}

bool AgendaController::refreshing() const
{
    return _agenda && _agenda->state().refreshing;
}

bool AgendaController::loadingFirstTime() const
{
    if (!_agenda) {
        return false;
    }
    const auto &state = _agenda->state();
    return state.neverLoaded && (state.refreshing || !state.refreshedAt.isValid()) && !state.offline;
}

bool AgendaController::personalQuestionVisible() const
{
    return _agenda && _agenda->state().build.needsPersonalQuestion && !_personalDismissed;
}

void AgendaController::refresh()
{
    _agenda->refresh(true);
}

void AgendaController::goToday()
{
    _date = _agenda->state().today.isValid() ? _agenda->state().today : QDate::currentDate();
    rebuild();
}

void AgendaController::goPrevious()
{
    if (_view == QLatin1String("day")) {
        _date = _date.addDays(-1);
    } else if (_view == QLatin1String("week")) {
        _date = _date.addDays(-7);
    } else {
        _date = _date.addMonths(-1);
    }
    rebuild();
}

void AgendaController::goNext()
{
    if (_view == QLatin1String("day")) {
        _date = _date.addDays(1);
    } else if (_view == QLatin1String("week")) {
        _date = _date.addDays(7);
    } else {
        _date = _date.addMonths(1);
    }
    rebuild();
}

void AgendaController::setDate(const QString &iso)
{
    const auto d = QDate::fromString(iso, Qt::ISODate);
    if (d.isValid()) {
        _date = d;
        rebuild();
    }
}

void AgendaController::toggleNoDate()
{
    _noDateExpanded = !_noDateExpanded;
    rebuild();
}

void AgendaController::select(const QString &key)
{
    _selected = key;
    rebuildDetail();
    Q_EMIT changed();
}

void AgendaController::clearSelection()
{
    select(QString());
}

void AgendaController::moveSelection(int delta)
{
    if (_selectableKeys.isEmpty()) {
        return;
    }
    int index = _selectableKeys.indexOf(_selected);
    index = index < 0 ? (delta > 0 ? 0 : _selectableKeys.size() - 1) : qBound(0, index + delta, static_cast<int>(_selectableKeys.size()) - 1);
    select(_selectableKeys.at(index));
}

const AgendaItem *AgendaController::itemWithKey(const QString &key) const
{
    for (const auto &item : _agenda->state().items) {
        if (item.key == key) {
            return &item;
        }
    }
    return nullptr;
}

void AgendaController::openSelected()
{
    if (const auto *item = itemWithKey(_selected)) {
        Q_EMIT openUrlRequested(_agenda->openUrlFor(*item));
    }
}

void AgendaController::bannerAction(const QString &action)
{
    if (action == QLatin1String("login")) {
        Q_EMIT loginRequested();
    } else if (action == QLatin1String("openGantt")) {
        Q_EMIT openUrlRequested(_agenda->appUrl(Home::Gantt));
    } else if (action == QLatin1String("openBoard")) {
        Q_EMIT openUrlRequested(_agenda->appUrl(Home::Board));
    }
}

void AgendaController::setSourceMode(const QString &type, const QString &key, const QString &mode)
{
    if (type == QLatin1String("calendar")) {
        _agenda->setCalendarMode(key, calendarModeFromString(mode, CalendarMode::Personal));
    } else if (type == QLatin1String("project")) {
        _agenda->setProjectMode(key.toInt(), workModeFromString(mode, WorkMode::Hidden));
    } else if (type == QLatin1String("board")) {
        _agenda->setBoardMode(key.toInt(), workModeFromString(mode, WorkMode::Hidden));
    }
}

void AgendaController::answerPersonal(const QStringList &personalUris)
{
    _agenda->answerPersonalQuestion(personalUris, false);
}

void AgendaController::skipPersonal()
{
    _personalDismissed = true;
    _agenda->answerPersonalQuestion(QStringList(), true);
}

void AgendaController::setWorkingHours(int daysMask, const QString &from, const QString &to)
{
    WorkingHours hours;
    hours.daysMask = daysMask & 127;
    const auto f = QTime::fromString(from, QStringLiteral("HH:mm"));
    const auto t = QTime::fromString(to, QStringLiteral("HH:mm"));
    if (f.isValid() && t.isValid() && f < t) {
        hours.from = f;
        hours.to = t;
        _agenda->setWorkingHours(hours);
    }
}

// ---- texts ------------------------------------------------------------------------------------------------------------

QString AgendaController::dateText(const QDate &d) const
{
    return QLocale().toString(d, QStringLiteral("ddd d MMM"));
}

QString AgendaController::timeOfDay(const QDateTime &utc) const
{
    return QLocale().toString(utc.toTimeZone(_agenda->zone()).time(), QLocale::ShortFormat);
}

QString AgendaController::savedAtText(const QDateTime &when) const
{
    if (!when.isValid()) {
        return QString();
    }
    const auto local = when.toTimeZone(_agenda->zone());
    const auto time = QLocale().toString(local.time(), QLocale::ShortFormat);
    if (local.date() == _agenda->state().today) {
        return time;
    }
    return dateText(local.date()) + QLatin1Char(' ') + time;
}

QString AgendaController::updatedText() const
{
    if (!_agenda) {
        return QString();
    }
    const auto when = _agenda->state().refreshedAt;
    if (!when.isValid()) {
        return QString();
    }
    return AgendaText::tr("Updated %1").arg(savedAtText(when));
}

QString AgendaController::rangeText() const
{
    const auto r = range();
    QLocale locale;
    if (_view == QLatin1String("day")) {
        return locale.toString(_date, QStringLiteral("dddd d MMMM yyyy"));
    }
    if (_view == QLatin1String("week")) {
        return locale.toString(r.first, QStringLiteral("d MMM")) + QStringLiteral(" - ") + locale.toString(r.second, QStringLiteral("d MMM yyyy"));
    }
    return locale.toString(_date, QStringLiteral("MMMM yyyy"));
}

QPair<QDate, QDate> AgendaController::range() const
{
    if (_view == QLatin1String("day")) {
        return {_date, _date};
    }
    if (_view == QLatin1String("week")) {
        const int first = static_cast<int>(QLocale().firstDayOfWeek());
        int back = _date.dayOfWeek() - first;
        if (back < 0) {
            back += 7;
        }
        const QDate start = _date.addDays(-back);
        return {start, start.addDays(6)};
    }
    const QDate start(_date.year(), _date.month(), 1);
    return {start, start.addMonths(1).addDays(-1)};
}

QString AgendaController::timeText(const AgendaItem &item, const QDate &day) const
{
    const auto zone = _agenda->zone();
    if (item.kind == ItemKind::Event) {
        if (item.allDay) {
            if (item.endDate.isValid() && item.endDate > item.startDate && day == item.startDate) {
                return AgendaText::tr("All day, until %1").arg(dateText(item.endDate));
            }
            return AgendaText::tr("All day");
        }
        const QDate first = item.firstDay(zone);
        const QDate last = item.lastDay(zone);
        if (first == last) {
            if (item.end.isValid() && item.end > item.start) {
                return timeOfDay(item.start) + QStringLiteral(" - ") + timeOfDay(item.end);
            }
            return timeOfDay(item.start);
        }
        if (day == first) {
            return timeOfDay(item.start) + QStringLiteral(" ->");
        }
        if (day == last) {
            return QStringLiteral("-> ") + timeOfDay(item.end);
        }
        return AgendaText::tr("All day");
    }
    if (item.kind == ItemKind::Todo && item.end.isValid() && item.due == day) {
        return AgendaText::tr("Due %1").arg(timeOfDay(item.end));
    }
    if (item.kind == ItemKind::Milestone) {
        return QString();
    }
    if (item.due.isValid() && item.due != day) {
        return AgendaText::tr("Due %1").arg(dateText(item.due));
    }
    return QString();
}

// ---- rows -------------------------------------------------------------------------------------------------------------

bool AgendaController::visible(const AgendaItem &item) const
{
    if (!item.info) {
        switch (item.workKind) {
        case WorkKind::Self:
            if (!_chipMe) {
                return false;
            }
            break;
        case WorkKind::MyAgent:
            if (!_chipAgents) {
                return false;
            }
            break;
        case WorkKind::Team:
            if (!_chipTeam) {
                return false;
            }
            break;
        }
    }
    if (_category == QLatin1String("work") && item.category == Category::Personal) {
        return false;
    }
    if (_category == QLatin1String("personal") && item.category == Category::Work) {
        return false;
    }
    return true;
}

QVariantMap AgendaController::headerRow(const QString &type, const QString &text, int count) const
{
    return QVariantMap{{QStringLiteral("type"), type}, {QStringLiteral("text"), text}, {QStringLiteral("count"), count}, {QStringLiteral("key"), QString()}};
}

QVariantMap AgendaController::rowOf(const AgendaItem &item, bool inTeam) const
{
    QStringList badges;
    QStringList problemTexts;
    for (const auto &p : item.problems) {
        if (p.text != item.title) { // a row that is only a problem says it once
            problemTexts.append(p.text);
        }
    }
    if (item.late && !inTeam) {
        badges.append(QStringLiteral("late"));
    }
    if (item.blocked && !inTeam) {
        badges.append(QStringLiteral("blocked"));
    }
    if (item.boardBadge) {
        badges.append(QStringLiteral("board"));
    }
    if (item.repeating) {
        badges.append(QStringLiteral("repeating"));
    }
    if (item.readOnly || item.versionReadOnly) {
        badges.append(QStringLiteral("lock"));
    }
    if (!item.problems.isEmpty() && !inTeam) {
        badges.append(QStringLiteral("flag"));
    }
    QString lateText;
    if (item.late) {
        lateText = FlagText::lateDays(item.lateDays);
    }
    return QVariantMap{
        {QStringLiteral("type"), QStringLiteral("item")},
        {QStringLiteral("key"), item.key},
        {QStringLiteral("title"), item.title},
        {QStringLiteral("glyph"), glyphOf(item.kind)},
        {QStringLiteral("sourceName"), item.sourceName},
        {QStringLiteral("sourceColor"), item.sourceColor},
        {QStringLiteral("agentText"), item.agent.displayText()},
        {QStringLiteral("agentKind"), agentKindOf(item.agent)},
        {QStringLiteral("workKind"), item.workKind == WorkKind::Self ? QStringLiteral("self") : (item.workKind == WorkKind::MyAgent ? QStringLiteral("agent") : QStringLiteral("team"))},
        {QStringLiteral("personName"), item.personName},
        {QStringLiteral("badges"), badges},
        {QStringLiteral("problemText"), problemTexts.join(QStringLiteral(", "))},
        {QStringLiteral("lateText"), lateText},
        {QStringLiteral("tentative"), item.tentative},
        {QStringLiteral("transparent"), item.transparent},
        {QStringLiteral("info"), item.info},
        {QStringLiteral("dim"), item.workKind == WorkKind::MyAgent && !item.problemOnly},
        {QStringLiteral("timeText"), QString()},
    };
}

void AgendaController::addDay(const QDate &day, bool dayView, QVariantList *out, bool header) const
{
    const auto &state = _agenda->state();
    const auto zone = _agenda->zone();
    const bool isToday = day == state.today;

    QList<const AgendaItem *> info;
    QList<const AgendaItem *> timed;
    QList<const AgendaItem *> due;
    QList<const AgendaItem *> working;
    QList<const AgendaItem *> late;
    QList<const AgendaItem *> team;
    QList<const AgendaItem *> attention; ///< rows that only report a problem with my agents (not my work)
    QSet<QString> teamKeys;

    for (const auto &item : state.items) {
        if (!visible(item)) {
            continue;
        }
        if (item.problemOnly) {
            if (isToday) {
                attention.append(&item);
            }
            continue;
        }
        if (item.workKind == WorkKind::Team) {
            const bool problem = !item.problems.isEmpty();
            bool activeToday = false;
            if (item.hasDate()) {
                const QDate first = item.firstDay(zone);
                const QDate last = item.lastDay(zone);
                activeToday = first <= day && day <= last;
            }
            if ((isToday && (problem || (_showAllTeam && activeToday))) || (!isToday && _showAllTeam && activeToday)) {
                if (!teamKeys.contains(item.key)) {
                    team.append(&item);
                    teamKeys.insert(item.key);
                }
            }
            continue;
        }
        if (!item.hasDate()) {
            continue;
        }
        if (item.kind == ItemKind::Event) {
            const QDate first = item.firstDay(zone);
            const QDate last = item.lastDay(zone);
            if (first <= day && day <= last) {
                (item.info ? info : timed).append(&item);
            }
            continue;
        }
        if (item.done) {
            continue;
        }
        const QDate dueDay = item.due.isValid() ? item.due : item.startDate;
        const QDate spanStart = item.startDate.isValid() ? item.startDate : dueDay;
        if (isToday && item.late) {
            late.append(&item);
        } else if (dueDay == day) {
            due.append(&item);
        } else if (dayView && spanStart <= day && day <= dueDay && !item.late) {
            working.append(&item);
        }
    }

    auto byStart = [](const AgendaItem *a, const AgendaItem *b) {
        const bool aAll = a->allDay;
        const bool bAll = b->allDay;
        if (aAll != bAll) {
            return aAll;
        }
        if (!aAll && a->start != b->start) {
            return a->start < b->start;
        }
        return a->title.localeAwareCompare(b->title) < 0;
    };
    auto byKind = [](const AgendaItem *a, const AgendaItem *b) {
        if (a->kind != b->kind) {
            return static_cast<int>(a->kind) > static_cast<int>(b->kind); // milestones first
        }
        return a->title.localeAwareCompare(b->title) < 0;
    };
    std::sort(info.begin(), info.end(), byStart);
    std::sort(timed.begin(), timed.end(), byStart);
    std::sort(due.begin(), due.end(), byKind);
    std::sort(working.begin(), working.end(), byKind);
    std::sort(late.begin(), late.end(), [](const AgendaItem *a, const AgendaItem *b) { return a->lateDays > b->lateDays; });
    std::sort(team.begin(), team.end(), [](const AgendaItem *a, const AgendaItem *b) { return a->title.localeAwareCompare(b->title) < 0; });
    std::sort(attention.begin(), attention.end(), [](const AgendaItem *a, const AgendaItem *b) { return a->title.localeAwareCompare(b->title) < 0; });

    QVariantList rows;
    auto addItems = [&](const QList<const AgendaItem *> &list, bool inTeam) {
        for (const auto *item : list) {
            auto row = rowOf(*item, inTeam);
            row.insert(QStringLiteral("timeText"), timeText(*item, day));
            row.insert(QStringLiteral("day"), day.toString(Qt::ISODate));
            rows.append(row);
        }
    };
    addItems(info, false);
    addItems(timed, false);
    if (!late.isEmpty()) {
        rows.append(headerRow(QStringLiteral("group"), AgendaText::tr("Late"), late.size()));
        addItems(late, false);
    }
    if (!due.isEmpty()) {
        rows.append(headerRow(QStringLiteral("group"), isToday ? AgendaText::tr("Due today") : AgendaText::tr("Due"), due.size()));
        addItems(due, false);
    }
    if (!working.isEmpty()) {
        rows.append(headerRow(QStringLiteral("group"), AgendaText::tr("In progress"), working.size()));
        addItems(working, false);
    }
    if (!attention.isEmpty()) {
        rows.append(headerRow(QStringLiteral("group"), AgendaText::tr("Problems with your agents"), attention.size()));
        addItems(attention, false);
    }
    const bool teamSection = state.build.anyOversee && _chipTeam;
    if (teamSection && (isToday || !team.isEmpty())) {
        if (!team.isEmpty()) {
            rows.append(headerRow(QStringLiteral("group"), AgendaText::tr("My team"), team.size()));
            addItems(team, true);
        } else if (dayView) {
            rows.append(headerRow(QStringLiteral("group"), AgendaText::tr("My team"), 0));
            rows.append(headerRow(QStringLiteral("quiet"), AgendaText::tr("No problems in what you run.")));
        }
    }

    // The folded list of items without a date (Day view only).
    if (dayView && isToday) {
        QList<const AgendaItem *> noDate;
        for (const auto &item : state.items) {
            if (visible(item) && item.workKind != WorkKind::Team && !item.problemOnly && !item.hasDate() && !item.done) {
                noDate.append(&item);
            }
        }
        std::sort(noDate.begin(), noDate.end(), byKind);
        if (!noDate.isEmpty()) {
            auto toggle = headerRow(QStringLiteral("noDate"), AgendaText::tr("No date"), noDate.size());
            toggle.insert(QStringLiteral("expanded"), _noDateExpanded);
            rows.append(toggle);
            if (_noDateExpanded) {
                addItems(noDate, false);
            }
        }
    }

    if (rows.isEmpty() && !header) {
        return;
    }
    if (header) {
        auto h = headerRow(QStringLiteral("day"), QLocale().toString(day, QStringLiteral("dddd d MMMM")));
        h.insert(QStringLiteral("today"), isToday);
        h.insert(QStringLiteral("day"), day.toString(Qt::ISODate));
        out->append(h);
        if (rows.isEmpty()) {
            out->append(headerRow(QStringLiteral("quiet"), AgendaText::tr("Nothing planned for this day.")));
            return;
        }
    }
    out->append(rows);
}

void AgendaController::rebuild()
{
    if (!_agenda) {
        return;
    }
    const auto &state = _agenda->state();
    if (!_date.isValid()) {
        _date = state.today.isValid() ? state.today : QDate::currentDate();
    }

    _rows.clear();
    const auto r = range();
    if (_view == QLatin1String("day")) {
        addDay(_date, true, &_rows, false);
    } else if (_view == QLatin1String("week")) {
        for (QDate d = r.first; d <= r.second; d = d.addDays(1)) {
            addDay(d, false, &_rows, true);
        }
    } else {
        // Month lists are grouped by day: build again with day headers for the days that have items.
        QVariantList grouped;
        for (QDate d = r.first; d <= r.second; d = d.addDays(1)) {
            QVariantList dayRows;
            addDay(d, false, &dayRows, false);
            if (!dayRows.isEmpty()) {
                auto h = headerRow(QStringLiteral("day"), QLocale().toString(d, QStringLiteral("dddd d MMMM")));
                h.insert(QStringLiteral("today"), d == state.today);
                h.insert(QStringLiteral("day"), d.toString(Qt::ISODate));
                grouped.append(h);
                grouped.append(dayRows);
            }
        }
        _rows = grouped;
    }

    _selectableKeys.clear();
    bool nothing = true;
    for (const auto &v : std::as_const(_rows)) {
        const auto m = v.toMap();
        if (m.value(QStringLiteral("type")).toString() == QLatin1String("item")) {
            _selectableKeys.append(m.value(QStringLiteral("key")).toString());
            nothing = false;
        }
    }
    if (_view == QLatin1String("day")) {
        _emptyText = AgendaText::tr("Nothing planned for this day.");
    } else if (_view == QLatin1String("week")) {
        _emptyText = AgendaText::tr("Nothing planned this week.");
    } else {
        _emptyText = AgendaText::tr("Nothing planned this month.");
    }
    if (!nothing) {
        _emptyText.clear();
    } else if (const auto *gantt = state.status(QStringLiteral("gantt")); gantt && gantt->state == SourceStatus::State::NotLinked) {
        _emptyText = AgendaText::tr("You're not linked to a person in NoGantt yet. Ask your project manager to link you.");
    }
    if (!_selected.isEmpty() && !itemWithKey(_selected)) {
        _selected.clear();
    }

    rebuildGrid();
    rebuildBanners();
    rebuildSources();
    rebuildDetail();

    // The one-time question: all own calendars ticked.
    _personalCalendars.clear();
    for (const auto &c : state.build.ownCalendars) {
        _personalCalendars.append(QVariantMap{{QStringLiteral("uri"), c.first}, {QStringLiteral("name"), c.second}, {QStringLiteral("checked"), true}});
    }
    Q_EMIT changed();
}

void AgendaController::rebuildGrid()
{
    _grid.clear();
    const auto &state = _agenda->state();
    const auto zone = _agenda->zone();
    const QDate first(_date.year(), _date.month(), 1);
    const int firstDow = static_cast<int>(QLocale().firstDayOfWeek());
    int back = first.dayOfWeek() - firstDow;
    if (back < 0) {
        back += 7;
    }
    const QDate start = first.addDays(-back);
    const QDate end = start.addDays(41);
    QSet<QDate> busy;
    for (const auto &item : state.items) {
        if (item.workKind == WorkKind::Team || !item.hasDate() || item.info || !visible(item)) {
            continue;
        }
        QDate a = item.firstDay(zone);
        QDate b = item.kind == ItemKind::Event ? item.lastDay(zone) : (item.due.isValid() ? item.due : a);
        if (!item.due.isValid() && item.kind != ItemKind::Event) {
            b = a;
        }
        if (!a.isValid() || !b.isValid()) {
            continue;
        }
        if (item.kind != ItemKind::Event) {
            a = b; // dots on the due day only
        }
        for (QDate d = qMax(a, start); d <= qMin(b, end) && d <= a.addDays(40); d = d.addDays(1)) {
            busy.insert(d);
        }
    }
    for (QDate d = start; d <= end; d = d.addDays(1)) {
        _grid.append(QVariantMap{
            {QStringLiteral("iso"), d.toString(Qt::ISODate)},
            {QStringLiteral("day"), d.day()},
            {QStringLiteral("inMonth"), d.month() == _date.month()},
            {QStringLiteral("today"), d == state.today},
            {QStringLiteral("selected"), d == _date},
            {QStringLiteral("busy"), busy.contains(d)},
        });
    }
}

void AgendaController::rebuildBanners()
{
    _banners.clear();
    const auto &state = _agenda->state();
    auto add = [this](const QString &text, const QString &kind, const QString &action = QString(), const QString &actionText = QString()) {
        _banners.append(QVariantMap{{QStringLiteral("text"), text}, {QStringLiteral("kind"), kind}, {QStringLiteral("action"), action}, {QStringLiteral("actionText"), actionText}});
    };
    if (state.needsLogin) {
        add(AgendaText::tr("Sign in to NoDrive to see new changes."), QStringLiteral("warn"), QStringLiteral("login"), AgendaText::tr("Log in"));
    }
    if (state.offline) {
        QDateTime latest;
        for (const auto &s : state.statuses) {
            if (s.savedAt.isValid() && (!latest.isValid() || s.savedAt > latest)) {
                latest = s.savedAt;
            }
        }
        if (latest.isValid()) {
            add(AgendaText::tr("You're offline. Showing what was saved at %1.").arg(savedAtText(latest)), QStringLiteral("warn"));
        } else {
            add(AgendaText::tr("You're offline. NoAgenda will load when NoDrive is connected."), QStringLiteral("warn"));
        }
    } else {
        for (const auto &s : state.statuses) {
            if (s.key == QLatin1String("registry")) {
                continue;
            }
            if (s.state == SourceStatus::State::Refused) {
                const bool gantt = s.key == QLatin1String("gantt");
                const QString text = s.savedAt.isValid()
                    ? AgendaText::tr("%1 didn't accept NoDrive's sign-in on your server. Showing what was saved at %2.").arg(sourceLabel(s.key), savedAtText(s.savedAt))
                    : AgendaText::tr("%1 didn't accept NoDrive's sign-in on your server. Open it in your browser instead.").arg(sourceLabel(s.key));
                add(text, QStringLiteral("warn"), gantt ? QStringLiteral("openGantt") : QStringLiteral("openBoard"),
                    gantt ? AgendaText::tr("Open in NoGantt") : AgendaText::tr("Open in NoBoard"));
            } else if (s.state == SourceStatus::State::Cached) {
                add(AgendaText::tr("%1 can't be reached; showing what was saved at %2.").arg(sourceLabel(s.key), savedAtText(s.savedAt)), QStringLiteral("warn"));
            } else if (s.state == SourceStatus::State::Failed) {
                add(AgendaText::tr("%1 can't be reached.").arg(sourceLabel(s.key)), QStringLiteral("warn"));
            }
        }
    }
    if (const auto *gantt = state.status(QStringLiteral("gantt"))) {
        if (gantt->state == SourceStatus::State::NotLinked) {
            add(AgendaText::tr("You're not linked to a person in NoGantt yet. Ask your project manager to link you."), QStringLiteral("info"));
        }
        if (gantt->readOnlyByGuard) {
            add(AgendaText::tr("NoGantt was updated on the server. NoAgenda shows its tasks read only until it has been checked with the new version."), QStringLiteral("info"), QStringLiteral("openGantt"), AgendaText::tr("Open in NoGantt"));
        }
    }
    if (const auto *board = state.status(QStringLiteral("board")); board && board->readOnlyByGuard) {
        add(AgendaText::tr("NoBoard was updated on the server. NoAgenda shows its cards read only until it has been checked with the new version."), QStringLiteral("info"), QStringLiteral("openBoard"), AgendaText::tr("Open in NoBoard"));
    }
    if (!state.build.taskCopyCalendarUri.isEmpty()) {
        add(AgendaText::tr("NoGantt copies your tasks into a calendar. NoAgenda hides that calendar. You can switch the copy off in NoGantt."), QStringLiteral("info"), QStringLiteral("openGantt"), AgendaText::tr("Open in NoGantt"));
    }
}

void AgendaController::rebuildSources()
{
    _sources.clear();
    const auto &state = _agenda->state();
    auto section = [this](const QString &text) {
        _sources.append(QVariantMap{{QStringLiteral("type"), QStringLiteral("header")}, {QStringLiteral("text"), text}});
    };
    auto note = [this](const QString &text) {
        _sources.append(QVariantMap{{QStringLiteral("type"), QStringLiteral("note")}, {QStringLiteral("text"), text}});
    };

    auto entries = [&](SourceEntry::Type type, const QString &typeName) {
        for (const auto &e : state.sources) {
            if (e.type != type) {
                continue;
            }
            QVariantList options;
            if (type == SourceEntry::Type::Calendar) {
                options = {option(QStringLiteral("personal"), AgendaText::tr("Personal"), e.mode), option(QStringLiteral("work"), AgendaText::tr("Work"), e.mode),
                    option(QStringLiteral("team"), AgendaText::tr("Team"), e.mode), option(QStringLiteral("info"), AgendaText::tr("Info"), e.mode),
                    option(QStringLiteral("hidden"), AgendaText::tr("Hidden"), e.mode)};
            } else {
                options = {option(QStringLiteral("doOversee"), AgendaText::tr("Do + Oversee"), e.mode), option(QStringLiteral("do"), AgendaText::tr("Do"), e.mode),
                    option(QStringLiteral("hidden"), AgendaText::tr("Hidden"), e.mode)};
            }
            _sources.append(QVariantMap{{QStringLiteral("type"), typeName}, {QStringLiteral("key"), e.key}, {QStringLiteral("name"), e.name}, {QStringLiteral("color"), e.color},
                {QStringLiteral("note"), e.autoHidden ? QString(AgendaText::tr("Hidden automatically") + QStringLiteral(": ") + e.autoHiddenReason) : e.note},
                {QStringLiteral("mode"), e.mode}, {QStringLiteral("autoHidden"), e.autoHidden}, {QStringLiteral("options"), options}});
        }
    };
    section(AgendaText::tr("Calendars and task lists"));
    entries(SourceEntry::Type::Calendar, QStringLiteral("calendar"));
    if (!state.build.taskCopyCalendarUri.isEmpty()) {
        note(AgendaText::tr("NoGantt copies your tasks into a calendar. NoAgenda hides that calendar. You can switch the copy off in NoGantt."));
    }
    section(AgendaText::tr("Projects (NoGantt)"));
    if (state.build.ganttUnavailable) {
        note(AgendaText::tr("NoGantt isn't available on your server."));
    }
    entries(SourceEntry::Type::Project, QStringLiteral("project"));
    section(AgendaText::tr("Boards (NoBoard)"));
    if (state.build.boardUnavailable) {
        note(AgendaText::tr("NoBoard isn't available on your server."));
    }
    entries(SourceEntry::Type::Board, QStringLiteral("board"));

    // Working hours
    const auto &hours = state.hours;
    _hours = QVariantMap{{QStringLiteral("daysMask"), hours.daysMask}, {QStringLiteral("from"), hours.from.toString(QStringLiteral("HH:mm"))},
        {QStringLiteral("to"), hours.to.toString(QStringLiteral("HH:mm"))}, {QStringLiteral("overridden"), _agenda->settings().workingHoursOverridden()}};

    // Agents
    _agents.clear();
    for (const auto &a : state.build.myAgents) {
        QString statusText = a.isActive() ? AgendaText::tr("Active") : (a.isPaused() ? AgendaText::tr("Paused") : AgendaText::tr("Retired"));
        _agents.append(QVariantMap{{QStringLiteral("id"), a.id}, {QStringLiteral("name"), a.name}, {QStringLiteral("status"), a.status}, {QStringLiteral("statusText"), statusText}});
    }
    switch (state.registryStatus) {
    case AgentRegistry::Status::Missing:
        _registryMessage = AgendaText::tr("No agent list found in your NoDrive (NoAgenda/agent-registry.json). Everything shows as Self.");
        break;
    case AgentRegistry::Status::Invalid:
        _registryMessage = AgendaText::tr("The agent list in your NoDrive can't be read. Everything shows as Self until it's fixed.");
        break;
    case AgentRegistry::Status::Ok:
        if (state.registrySkipped == 1) {
            _registryMessage = AgendaText::tr("1 agent in the list was skipped because it is incomplete.");
        } else if (state.registrySkipped > 1) {
            _registryMessage = AgendaText::tr("%1 agents in the list were skipped because they are incomplete.").arg(state.registrySkipped);
        } else if (_agents.isEmpty()) {
            _registryMessage = AgendaText::tr("You have no agents in the list.");
        } else {
            _registryMessage.clear();
        }
        break;
    }
}

void AgendaController::rebuildDetail()
{
    _detail.clear();
    const auto *item = itemWithKey(_selected);
    if (!item) {
        return;
    }
    const auto zone = _agenda->zone();
    QStringList flags;
    if (item->late) {
        flags.append(FlagText::lateDays(item->lateDays));
    }
    if (item->dueToday && item->kind != ItemKind::Event) {
        flags.append(FlagText::dueToday());
    }
    if (item->blocked) {
        flags.append(FlagText::blockedBy(item->blockedBy));
    }
    if (item->tentative) {
        flags.append(AgendaText::tr("Tentative: you have not accepted this yet."));
    }
    if (item->invitation) {
        flags.append(AgendaText::tr("Invitation from someone else: the agent is always Self."));
    }
    if (item->transparent && !item->info) {
        flags.append(AgendaText::tr("Free time: it does not block your time."));
    }
    if (item->repeating) {
        flags.append(AgendaText::tr("Repeats."));
    }
    if (item->timeApproximated) {
        flags.append(AgendaText::tr("Time approximated: the time zone is not known."));
    }
    for (const auto &p : item->problems) {
        flags.append(p.text);
    }
    if (item->versionReadOnly) {
        flags.append(AgendaText::tr("Read only: this version of the app on the server has not been checked yet."));
    } else if (item->readOnly && !item->readOnlyReason.isEmpty()) {
        flags.append(item->readOnlyReason);
    }

    QString when;
    const auto day = _date;
    if (item->kind == ItemKind::Event && item->allDay) {
        when = dateText(item->startDate);
        if (item->endDate > item->startDate) {
            when += QStringLiteral(" - ") + dateText(item->endDate);
        }
        when += QStringLiteral(" (") + AgendaText::tr("All day") + QLatin1Char(')');
    } else if (item->kind == ItemKind::Event) {
        when = dateText(item->firstDay(zone)) + QStringLiteral(", ") + timeText(*item, item->firstDay(zone));
    } else if (item->kind == ItemKind::Milestone) {
        when = dateText(item->startDate);
    } else if (item->startDate.isValid() && item->due.isValid() && item->startDate != item->due) {
        when = dateText(item->startDate) + QStringLiteral(" - ") + dateText(item->due);
    } else if (item->due.isValid()) {
        when = AgendaText::tr("Due %1").arg(dateText(item->due));
    } else {
        when = AgendaText::tr("No date");
    }
    Q_UNUSED(day)

    QString percent;
    if (item->percentDone >= 0) {
        percent = AgendaText::tr("%1% done").arg(item->percentDone);
    }
    _detail = QVariantMap{
        {QStringLiteral("title"), item->title},
        {QStringLiteral("kindText"), kindText(item->kind)},
        {QStringLiteral("homeText"), homeText(item->home)},
        {QStringLiteral("sourceName"), item->sourceName},
        {QStringLiteral("sourceColor"), item->sourceColor},
        {QStringLiteral("whenText"), when},
        {QStringLiteral("agentText"), item->agent.displayText()},
        {QStringLiteral("agentKind"), agentKindOf(item->agent)},
        {QStringLiteral("personName"), item->personName},
        {QStringLiteral("statusText"), item->status},
        {QStringLiteral("percentText"), percent},
        {QStringLiteral("description"), item->description},
        {QStringLiteral("location"), item->location},
        {QStringLiteral("flags"), flags},
        {QStringLiteral("openLabel"), AccountAgenda::openLabelFor(*item)},
    };
}

} // namespace OCC::Nourax::Agenda
