/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda Timeline: the pure maths of design 8 / plans/noagenda-desktop-plan.md, 3.2 and 3.3. No widgets, no network:
// date <-> x, the groups (project > phase, NoTasks list, NoBoard board), the rows, the bar and diamond rectangles,
// the dependency arrows, the shading of my busy time, and how far a drag moves a bar. The same input always gives
// the same layout (this is what the unit tests check).

#pragma once

#include "../model/agendaitem.h"
#include "conflicts.h"

#include <QDate>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>

namespace OCC::Nourax::Agenda {

enum class TimelineZoom { Weeks, Days };

/// Days <-> pixels. `first` is the left edge (x = 0) of the first day; every day is `pxPerDay` wide.
struct TimelineScale
{
    QDate first;
    int days = 0;
    double pxPerDay = 12;

    /// Weeks: 12 px per day. Days: 40 px per day (plan 3.3).
    static double pxPerDayFor(TimelineZoom zoom);
    /// The left edge of a day.
    [[nodiscard]] double xOfDay(const QDate &day) const;
    /// The right edge of a day (the left edge of the next one).
    [[nodiscard]] double xAfterDay(const QDate &day) const { return xOfDay(day.addDays(1)); }
    /// The day under an x (the left edge belongs to the day, the right edge to the next).
    [[nodiscard]] QDate dayAtX(double x) const;
    [[nodiscard]] QDate last() const { return first.addDays(days - 1); }
    [[nodiscard]] double width() const { return days * pxPerDay; }
    /// A drag of `dx` pixels in whole days: 0.49 of a day stays, 0.51 moves (half a day goes away from zero).
    static int snapDays(double dx, double pxPerDay);
};

/// One line of the Timeline: a group header, a bar (a NoGantt task, a to-do with a start and a due date) or a
/// diamond (a milestone, a dated card).
struct TimelineEntry
{
    enum class Type { Group, Bar, Diamond };

    Type type = Type::Bar;
    int level = 0; ///< groups: 0 = project, list or board; 1 = phase
    int row = 0;
    QString key; ///< the key of the item, or of the group
    QString groupKey;
    QString title;
    QString color;
    QString glyph; ///< task | todo | milestone | card
    QDate start;
    QDate end; ///< bars: the last day, included. A diamond has start == end.
    int percent = -1;
    QString agentText;
    QString agentKind; ///< self | agent | unknown | retired | paused | other
    bool late = false;
    bool blocked = false;
    bool waiting = false; ///< a change of mine is waiting to be sent
    bool notSent = false;
    bool locked = false; ///< the person may see it but not move it
    bool canMove = false; ///< a bar that may be dragged
    QString lockReason; ///< plain words (also why a diamond or a lock does not move)
    bool clippedLeft = false;
    bool clippedRight = false;
    QRectF rect; ///< in pixels from the top left of the chart; bars and diamonds only
    int projectId = 0;
};

/// A dependency arrow, read only. `points` run from the predecessor's anchor to the successor's anchor.
struct TimelineArrow
{
    QString fromKey; ///< empty for a stub (the predecessor is not on the screen)
    QString toKey;
    QList<QPointF> points;
    bool stub = false;
    QString type; ///< EA | AA | EE | AE
    double lagDays = 0;
    QString tooltip;
};

/// The share of my working hours that my own busy events take on one day, and a day off.
struct TimelineShade
{
    QDate day;
    double x = 0;
    double width = 0;
    double alpha = 0; ///< 0 .. 0.35
    bool dayOff = false;
};

struct TimelineLayoutInput
{
    QList<AgendaItem> items; ///< what is shown (the window's chips and switch already applied)
    QList<AgendaItem> allItems; ///< for the shading: all my items, whatever the chips say (empty = items)
    DayModel dayModel;
    TimelineScale scale;
    QDate today;
};

struct TimelineLayoutResult
{
    TimelineScale scale;
    QList<TimelineEntry> entries; ///< in row order
    QList<TimelineArrow> arrows;
    QList<TimelineShade> shading;
    int rowCount = 0;
    double height = 0;
    double todayX = -1; ///< the left edge of today, -1 when today is outside the range
};

/// What a drag of a bar does.
struct TimelineDrag
{
    bool allowed = false;
    int days = 0;
    QDate start;
    QDate end;
    QString reason; ///< why not (plain words)
};

namespace TimelineLayout {

constexpr double kRowHeight = 34;
constexpr double kBarHeight = 20;
constexpr double kDiamond = 16;
constexpr double kStub = 14; ///< the length of the short line for a predecessor that is not on the screen
constexpr double kElbow = 8;

TimelineLayoutResult build(const TimelineLayoutInput &input);

/// The route of an arrow: orthogonal lines from `from` (leaving to the right if `fromFinish`, else to the left) to `to`
/// (entering from the left if `toStart`, else from the right). First and last point are exactly `from` and `to`.
QList<QPointF> route(const QPointF &from, bool fromFinish, const QPointF &to, bool toStart);

/// The dates after a drag of `dx` pixels. A bar that is locked, or is not a bar, gives allowed == false and no days.
TimelineDrag drag(const TimelineEntry &entry, double dx, double pxPerDay);

/// "Finish to start" and so on for NoGantt's EA, AA, EE and AE.
QString typeText(const QString &type);

} // namespace TimelineLayout

} // namespace OCC::Nourax::Agenda
