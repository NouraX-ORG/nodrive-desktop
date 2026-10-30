/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "timelinecontroller.h"

#include <QLocale>
#include <QScopeGuard>

namespace OCC::Nourax::Agenda {

namespace {

constexpr int kDaysBefore = 7;
constexpr int kDaysShown = 7 + 8 * 7; // from today - 7 days to + 8 weeks

QString shortDate(const QDate &d)
{
    return QLocale().toString(d, QStringLiteral("d MMM"));
}

} // namespace

TimelineController::TimelineController(AccountAgenda *agenda, EditController *edit, QObject *parent)
    : QObject(parent)
    , _agenda(agenda)
    , _edit(edit)
{
}

void TimelineController::setZoom(const QString &zoom)
{
    const auto z = zoom == QLatin1String("days") ? TimelineZoom::Days : TimelineZoom::Weeks;
    if (z == _zoom) {
        return;
    }
    _zoom = z;
    if (_anchor.isValid()) {
        rebuild(_anchor, _filter);
    }
}

QString TimelineController::rangeText(const QDate &a, const QDate &b) const
{
    return AgendaText::tr("%1 - %2").arg(shortDate(a), shortDate(b));
}

const TimelineEntry *TimelineController::entryWithKey(const QString &key) const
{
    for (const auto &e : _layout.entries) {
        if (e.type != TimelineEntry::Type::Group && e.key == key) {
            return &e;
        }
    }
    return nullptr;
}

QStringList TimelineController::itemKeys() const
{
    QStringList keys;
    for (const auto &e : _layout.entries) {
        if (e.type != TimelineEntry::Type::Group) {
            keys.append(e.key);
        }
    }
    return keys;
}

void TimelineController::rebuild(const QDate &anchor, const std::function<bool(const AgendaItem &)> &filter)
{
    if (!_agenda) {
        return;
    }
    _anchor = anchor;
    _filter = filter;
    if (_ghost.value(QStringLiteral("show")).toBool()) {
        // A bar is held (a refresh, the 5-minute timer or another saved edit): the chart waits for the drop or the cancel, so the drag is not lost.
        _rebuildHeld = true;
        return;
    }
    _rebuildHeld = false;
    const auto &state = _agenda->state();

    TimelineLayoutInput input;
    input.scale.first = anchor.addDays(-kDaysBefore);
    input.scale.days = kDaysShown;
    input.scale.pxPerDay = TimelineScale::pxPerDayFor(_zoom);
    input.today = state.today;
    input.dayModel = state.dayModel;
    input.allItems = state.items;
    for (const auto &item : state.items) {
        if (!filter || filter(item)) {
            input.items.append(item);
        }
    }
    _layout = TimelineLayout::build(input);

    _entries.clear();
    for (const auto &e : _layout.entries) {
        QString dates;
        if (e.type != TimelineEntry::Type::Group) {
            dates = e.type == TimelineEntry::Type::Diamond || e.start == e.end ? QLocale().toString(e.start, QStringLiteral("ddd d MMM")) : rangeText(e.start, e.end);
        }
        const bool group = e.type == TimelineEntry::Type::Group;
        _entries.append(QVariantMap{
            {QStringLiteral("type"), group ? QStringLiteral("group") : (e.type == TimelineEntry::Type::Bar ? QStringLiteral("bar") : QStringLiteral("diamond"))},
            {QStringLiteral("level"), e.level},
            {QStringLiteral("row"), e.row},
            {QStringLiteral("key"), e.key},
            {QStringLiteral("groupKey"), e.groupKey},
            {QStringLiteral("title"), e.title},
            {QStringLiteral("color"), e.color},
            {QStringLiteral("glyph"), e.glyph},
            {QStringLiteral("x"), e.rect.x()},
            {QStringLiteral("y"), group ? e.row * TimelineLayout::kRowHeight : e.rect.y()},
            {QStringLiteral("w"), e.rect.width()},
            {QStringLiteral("h"), e.rect.height()},
            {QStringLiteral("startIso"), e.start.toString(Qt::ISODate)},
            {QStringLiteral("endIso"), e.end.toString(Qt::ISODate)},
            {QStringLiteral("dates"), dates},
            {QStringLiteral("percent"), e.percent},
            {QStringLiteral("agentText"), e.agentText},
            {QStringLiteral("agentKind"), e.agentKind},
            {QStringLiteral("late"), e.late},
            {QStringLiteral("blocked"), e.blocked},
            {QStringLiteral("waiting"), e.waiting},
            {QStringLiteral("notSent"), e.notSent},
            {QStringLiteral("locked"), e.locked},
            {QStringLiteral("canMove"), e.canMove},
            {QStringLiteral("lockReason"), e.lockReason},
            {QStringLiteral("clippedLeft"), e.clippedLeft},
            {QStringLiteral("clippedRight"), e.clippedRight},
        });
    }

    _arrows.clear();
    for (const auto &a : _layout.arrows) {
        QVariantList pts;
        for (const auto &p : a.points) {
            pts.append(p.x());
            pts.append(p.y());
        }
        // The place to hover for the tooltip: the last line of the arrow, a little thicker.
        const QPointF p = a.points.at(a.points.size() - 1);
        const QPointF q = a.points.at(a.points.size() - 2);
        const QRectF hit = QRectF(QPointF(qMin(p.x(), q.x()), qMin(p.y(), q.y())), QPointF(qMax(p.x(), q.x()), qMax(p.y(), q.y()))).adjusted(-4, -5, 4, 5);
        _arrows.append(QVariantMap{
            {QStringLiteral("points"), pts},
            {QStringLiteral("stub"), a.stub},
            {QStringLiteral("type"), a.type},
            {QStringLiteral("fromKey"), a.fromKey},
            {QStringLiteral("toKey"), a.toKey},
            {QStringLiteral("tooltip"), a.tooltip},
            {QStringLiteral("hx"), hit.x()},
            {QStringLiteral("hy"), hit.y()},
            {QStringLiteral("hw"), hit.width()},
            {QStringLiteral("hh"), hit.height()},
        });
    }

    _shading.clear();
    for (const auto &s : _layout.shading) {
        _shading.append(QVariantMap{{QStringLiteral("x"), s.x}, {QStringLiteral("w"), s.width}, {QStringLiteral("alpha"), s.alpha}, {QStringLiteral("dayOff"), s.dayOff}});
    }

    rebuildScale();

    bool any = false;
    for (const auto &e : _layout.entries) {
        any = any || e.type != TimelineEntry::Type::Group;
    }
    _emptyText = any ? QString() : AgendaText::tr("No dated work in this range.");
    _ghost.clear();
    Q_EMIT ghostChanged();
    Q_EMIT changed();
}

void TimelineController::rebuildScale()
{
    _scaleTop.clear();
    _scaleBottom.clear();
    const auto &scale = _layout.scale;
    const QLocale locale;
    // The top line: months.
    int i = 0;
    while (i < scale.days) {
        const QDate day = scale.first.addDays(i);
        int n = 0;
        while (i + n < scale.days && scale.first.addDays(i + n).month() == day.month()) {
            ++n;
        }
        _scaleTop.append(QVariantMap{{QStringLiteral("x"), i * scale.pxPerDay}, {QStringLiteral("w"), n * scale.pxPerDay}, {QStringLiteral("text"), locale.toString(day, QStringLiteral("MMMM yyyy"))}});
        i += n;
    }
    // The second line: the start of every week (Weeks), or every day (Days).
    if (_zoom == TimelineZoom::Days) {
        for (int d = 0; d < scale.days; ++d) {
            const QDate day = scale.first.addDays(d);
            _scaleBottom.append(QVariantMap{{QStringLiteral("x"), d * scale.pxPerDay}, {QStringLiteral("w"), scale.pxPerDay}, {QStringLiteral("text"), QString::number(day.day())},
                {QStringLiteral("sub"), locale.toString(day, QStringLiteral("ddd"))}, {QStringLiteral("dayOff"), _layout.shading.value(d).dayOff}, {QStringLiteral("today"), day == _agenda->state().today}});
        }
    } else {
        const int firstDay = static_cast<int>(locale.firstDayOfWeek());
        int d = 0;
        while (d < scale.days) {
            const QDate day = scale.first.addDays(d);
            int back = day.dayOfWeek() - firstDay;
            if (back < 0) {
                back += 7;
            }
            const int n = qMin(7 - back, scale.days - d); // days until the next week starts
            _scaleBottom.append(QVariantMap{{QStringLiteral("x"), d * scale.pxPerDay}, {QStringLiteral("w"), n * scale.pxPerDay}, {QStringLiteral("text"), QString::number(day.day())},
                {QStringLiteral("sub"), QString()}, {QStringLiteral("dayOff"), false}, {QStringLiteral("today"), false}});
            d += n;
        }
    }
}

double TimelineController::scrollTargetX() const
{
    if (_layout.todayX < 0) {
        return 0;
    }
    return qMax(0.0, _layout.todayX - 2 * _layout.scale.pxPerDay * 7);
}

QString TimelineController::datesText(const QString &key, int days) const
{
    const auto *e = entryWithKey(key);
    if (!e) {
        return QString();
    }
    return rangeText(e->start.addDays(days), e->end.addDays(days));
}

QString TimelineController::dragPreview(const QString &key, double dx)
{
    const auto *e = entryWithKey(key);
    if (!e) {
        return QString();
    }
    const auto d = TimelineLayout::drag(*e, dx, _layout.scale.pxPerDay);
    if (!d.allowed) {
        _ghost = QVariantMap{{QStringLiteral("key"), key}, {QStringLiteral("show"), false}};
        Q_EMIT ghostChanged();
        return d.reason;
    }
    const QString text = rangeText(d.start, d.end);
    _ghost = QVariantMap{
        {QStringLiteral("key"), key},
        {QStringLiteral("show"), true},
        {QStringLiteral("days"), d.days},
        {QStringLiteral("x"), e->rect.x() + d.days * _layout.scale.pxPerDay},
        {QStringLiteral("y"), e->rect.y()},
        {QStringLiteral("w"), e->rect.width()},
        {QStringLiteral("h"), e->rect.height()},
        {QStringLiteral("text"), text},
    };
    Q_EMIT ghostChanged();
    return text;
}

void TimelineController::releaseHeldRebuild()
{
    if (_rebuildHeld && _anchor.isValid()) {
        rebuild(_anchor, _filter);
    }
}

void TimelineController::dragCancel()
{
    _ghost.clear();
    Q_EMIT ghostChanged();
    releaseHeldRebuild();
}

QString TimelineController::dragCommit(const QString &key, double dx)
{
    const auto *e = entryWithKey(key);
    _ghost.clear();
    Q_EMIT ghostChanged();
    const auto release = qScopeGuard([this] { releaseHeldRebuild(); });
    if (!e || !_edit) {
        return QString();
    }
    const auto d = TimelineLayout::drag(*e, dx, _layout.scale.pxPerDay);
    if (!d.allowed) {
        // A locked bar gives no command at all: the person is told why.
        if (!d.reason.isEmpty()) {
            _edit->showToast(d.reason, QStringLiteral("warn"));
        }
        return d.reason;
    }
    if (d.days == 0) {
        return QString();
    }
    const QString error = _edit->moveByDays(key, d.days);
    if (!error.isEmpty()) {
        _edit->showToast(error, QStringLiteral("warn"));
    }
    return error;
}

QString TimelineController::moveByKeys(const QString &key, int days)
{
    if (!entryWithKey(key)) {
        return QString();
    }
    return dragCommit(key, days * _layout.scale.pxPerDay);
}

} // namespace OCC::Nourax::Agenda
