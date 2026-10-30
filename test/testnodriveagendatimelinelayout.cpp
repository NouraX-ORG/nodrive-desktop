/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda Timeline (plans/noagenda-desktop-plan.md, 3.5): the pure maths of the layout. Date <-> x at both zooms, the
// groups and their order, the bar and diamond rectangles, the arrows of the four dependency types and the stub for a
// predecessor that is not on the screen, the shading of my busy time, and how far a drag moves a bar (and that a locked
// bar gives no command).

#include "gui/nourax/noagenda/logic/timelinelayout.h"

#include <QTest>

#include <algorithm>

using namespace OCC::Nourax::Agenda;

namespace {

const QDate kFirst(2026, 10, 21); // a Wednesday; the 24th and 25th are the weekend

TimelineScale scaleFor(TimelineZoom zoom, int days = 63)
{
    TimelineScale s;
    s.first = kFirst;
    s.days = days;
    s.pxPerDay = TimelineScale::pxPerDayFor(zoom);
    return s;
}

QDate day(int d)
{
    return QDate(2026, 10, d);
}

AgendaItem task(int id, const QString &title, const QDate &start, const QDate &end, int project = 1, const QString &projectName = QStringLiteral("Website"))
{
    AgendaItem i;
    i.key = QStringLiteral("gantt:task:") + QString::number(id);
    i.kind = ItemKind::GanttTask;
    i.home = Home::Gantt;
    i.title = title;
    i.startDate = start;
    i.endDate = end;
    i.due = end;
    i.projectId = project;
    i.projectName = projectName;
    i.sourceName = projectName;
    i.sourceColor = QStringLiteral("#2563EB");
    i.workKind = WorkKind::Self;
    i.taskId = id;
    return i;
}

AgendaItem milestone(int id, const QString &title, const QDate &when, int project = 1)
{
    AgendaItem i;
    i.key = QStringLiteral("gantt:ms:") + QString::number(id);
    i.kind = ItemKind::Milestone;
    i.home = Home::Gantt;
    i.title = title;
    i.startDate = i.endDate = i.due = when;
    i.projectId = project;
    i.projectName = QStringLiteral("Website");
    i.sourceColor = QStringLiteral("#2563EB");
    i.readOnly = true;
    i.readOnlyReason = QStringLiteral("Milestones are changed in NoGantt.");
    return i;
}

AgendaItem todo(const QString &title, const QDate &start, const QDate &due)
{
    AgendaItem i;
    i.key = QStringLiteral("cal:personal/") + title + QStringLiteral(".ics");
    i.kind = ItemKind::Todo;
    i.home = Home::Tasks;
    i.title = title;
    i.startDate = start;
    i.due = due;
    i.endDate = due;
    i.calendarUri = QStringLiteral("personal");
    i.sourceName = QStringLiteral("Personal");
    i.sourceColor = QStringLiteral("#16A34A");
    return i;
}

AgendaItem card(int id, const QString &title, const QDate &due)
{
    AgendaItem i;
    i.key = QStringLiteral("deck:card:") + QString::number(id);
    i.kind = ItemKind::Card;
    i.home = Home::Board;
    i.title = title;
    i.due = i.endDate = due;
    i.boardId = 10;
    i.cardId = id;
    i.sourceName = QStringLiteral("Team board");
    i.sourceColor = QStringLiteral("#2563EB");
    return i;
}

TimelineLayoutResult layout(const QList<AgendaItem> &items, TimelineZoom zoom = TimelineZoom::Weeks, const DayModel &model = DayModel(), const QList<AgendaItem> &all = {})
{
    TimelineLayoutInput in;
    in.items = items;
    in.allItems = all;
    in.dayModel = model;
    in.scale = scaleFor(zoom);
    in.today = day(28);
    return TimelineLayout::build(in);
}

const TimelineEntry *entryOf(const TimelineLayoutResult &r, const QString &key)
{
    for (const auto &e : r.entries) {
        if (e.key == key) {
            return &e;
        }
    }
    return nullptr;
}

bool axisAligned(const QList<QPointF> &p)
{
    for (int i = 1; i < p.size(); ++i) {
        if (!qFuzzyCompare(p.at(i).x() + 1, p.at(i - 1).x() + 1) && !qFuzzyCompare(p.at(i).y() + 1, p.at(i - 1).y() + 1)) {
            return false;
        }
    }
    return true;
}

} // namespace

class TestNoDriveAgendaTimelineLayout : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void pixelsPerDayAtBothZooms()
    {
        QCOMPARE(TimelineScale::pxPerDayFor(TimelineZoom::Weeks), 12.0);
        QCOMPARE(TimelineScale::pxPerDayFor(TimelineZoom::Days), 40.0);
    }

    void dateAndXRoundTrip_data()
    {
        QTest::addColumn<int>("zoom");
        QTest::newRow("weeks") << static_cast<int>(TimelineZoom::Weeks);
        QTest::newRow("days") << static_cast<int>(TimelineZoom::Days);
    }

    void dateAndXRoundTrip()
    {
        QFETCH(int, zoom);
        const auto s = scaleFor(static_cast<TimelineZoom>(zoom));
        for (int i = 0; i < s.days; ++i) {
            const QDate d = kFirst.addDays(i);
            const double x = s.xOfDay(d);
            QCOMPARE(x, i * s.pxPerDay);
            QCOMPARE(s.dayAtX(x), d); // the left edge belongs to the day
            QCOMPARE(s.dayAtX(x + s.pxPerDay / 2), d); // the middle too
            QCOMPARE(s.dayAtX(x + s.pxPerDay - 0.01), d); // and so does the last pixel
            QCOMPARE(s.dayAtX(s.xAfterDay(d)), d.addDays(1)); // the right edge is the next day
        }
        QCOMPARE(s.width(), 63 * s.pxPerDay);
        QCOMPARE(s.last(), kFirst.addDays(62));
    }

    void dragSnapsToWholeDays_data()
    {
        QTest::addColumn<int>("zoom");
        QTest::addColumn<double>("days");
        QTest::addColumn<int>("moved");
        struct Zoom
        {
            const char *name;
            TimelineZoom zoom;
        };
        const Zoom zooms[] = {{"weeks", TimelineZoom::Weeks}, {"days", TimelineZoom::Days}};
        for (const auto &z : zooms) {
            const QByteArray name = z.name;
            const int zoom = static_cast<int>(z.zoom);
            QTest::newRow(QByteArray(name).append(" 0").constData()) << zoom << 0.0 << 0;
            QTest::newRow(QByteArray(name).append(" +0.49 stays").constData()) << zoom << 0.49 << 0;
            QTest::newRow(QByteArray(name).append(" -0.49 stays").constData()) << zoom << -0.49 << 0;
            QTest::newRow(QByteArray(name).append(" +0.51 moves").constData()) << zoom << 0.51 << 1;
            QTest::newRow(QByteArray(name).append(" -0.51 moves").constData()) << zoom << -0.51 << -1;
            QTest::newRow(QByteArray(name).append(" +2.3").constData()) << zoom << 2.3 << 2;
            QTest::newRow(QByteArray(name).append(" -3.7").constData()) << zoom << -3.7 << -4;
        }
    }

    void dragSnapsToWholeDays()
    {
        QFETCH(int, zoom);
        QFETCH(double, days);
        QFETCH(int, moved);
        const double px = TimelineScale::pxPerDayFor(static_cast<TimelineZoom>(zoom));
        QCOMPARE(TimelineScale::snapDays(days * px, px), moved);
    }

    void groupsAreProjectThenPhaseThenItemsInStartOrder()
    {
        auto a = task(1, QStringLiteral("Late start"), day(30), day(31));
        auto b = task(2, QStringLiteral("Early start"), day(26), day(27));
        auto p1 = task(3, QStringLiteral("In phase 2"), day(27), day(28));
        p1.phaseId = 502;
        p1.phaseName = QStringLiteral("Phase 2");
        auto p2 = task(4, QStringLiteral("In phase 1"), day(29), day(30));
        p2.phaseId = 501;
        p2.phaseName = QStringLiteral("Phase 1");
        auto other = task(5, QStringLiteral("Other project"), day(26), day(27), 2, QStringLiteral("Accounts"));
        auto ms = milestone(301, QStringLiteral("Launch"), day(29));
        const auto r = layout({a, b, p1, p2, other, ms});

        QStringList order;
        for (const auto &e : r.entries) {
            QCOMPARE(e.row, static_cast<int>(order.size())); // one row each, top to bottom
            order.append(e.title);
        }
        // "Accounts" before "Website" (by name); in Website: the items without a phase in start order (the milestone sits
        // between them by its date), then the phases by their first start, each with its items.
        QCOMPARE(order,
            (QStringList{QStringLiteral("Accounts"), QStringLiteral("Other project"), QStringLiteral("Website"), QStringLiteral("Early start"), QStringLiteral("Launch"),
                QStringLiteral("Late start"), QStringLiteral("Phase 2"), QStringLiteral("In phase 2"), QStringLiteral("Phase 1"), QStringLiteral("In phase 1")}));
        QCOMPARE(r.rowCount, static_cast<int>(order.size()));
        QCOMPARE(r.height, static_cast<double>(order.size()) * TimelineLayout::kRowHeight);
        QCOMPARE(entryOf(r, QStringLiteral("gantt:p1"))->level, 0);
        QCOMPARE(entryOf(r, QStringLiteral("gantt:p1:s501"))->level, 1);
        QCOMPARE(entryOf(r, QStringLiteral("gantt:task:4"))->groupKey, QStringLiteral("gantt:p1:s501"));
    }

    void barRectanglesCoverTheFirstToTheLastDayIncluded()
    {
        const auto r = layout({task(1, QStringLiteral("T"), day(26), day(28))});
        const auto *e = entryOf(r, QStringLiteral("gantt:task:1"));
        QVERIFY(e);
        QCOMPARE(e->type, TimelineEntry::Type::Bar);
        const auto s = scaleFor(TimelineZoom::Weeks);
        QCOMPARE(e->rect.left(), s.xOfDay(day(26)));
        QCOMPARE(e->rect.right(), s.xAfterDay(day(28)));
        QCOMPARE(e->rect.width(), 3 * 12.0);
        QCOMPARE(e->rect.height(), TimelineLayout::kBarHeight);
        QCOMPARE(e->rect.center().y(), e->row * TimelineLayout::kRowHeight + TimelineLayout::kRowHeight / 2);
        const auto z = layout({task(1, QStringLiteral("T"), day(26), day(28))}, TimelineZoom::Days);
        QCOMPARE(entryOf(z, QStringLiteral("gantt:task:1"))->rect.width(), 3 * 40.0);
        // A one-day task is one day wide.
        const auto one = layout({task(2, QStringLiteral("One"), day(26), day(26))});
        QCOMPARE(entryOf(one, QStringLiteral("gantt:task:2"))->rect.width(), 12.0);
    }

    void thePercentDoneAndTheAgentTravelWithTheBar()
    {
        auto t = task(1, QStringLiteral("T"), day(26), day(28));
        t.percentDone = 40;
        t.agent.isSelf = false;
        t.agent.id = QStringLiteral("AG-0001");
        t.agent.name = QStringLiteral("Writer-Bot");
        t.workKind = WorkKind::MyAgent;
        auto self = task(2, QStringLiteral("S"), day(26), day(28));
        const auto r = layout({t, self});
        QCOMPARE(entryOf(r, QStringLiteral("gantt:task:1"))->percent, 40);
        QCOMPARE(entryOf(r, QStringLiteral("gantt:task:1"))->agentText, QStringLiteral("Writer-Bot"));
        QCOMPARE(entryOf(r, QStringLiteral("gantt:task:1"))->agentKind, QStringLiteral("agent"));
        QCOMPARE(entryOf(r, QStringLiteral("gantt:task:2"))->agentText, QStringLiteral("Self"));
        QCOMPARE(entryOf(r, QStringLiteral("gantt:task:2"))->agentKind, QStringLiteral("self"));
        QCOMPARE(entryOf(r, QStringLiteral("gantt:task:2"))->percent, -1);
    }

    void aBarThatStartsBeforeTheRangeIsCutAndMarked()
    {
        const auto r = layout({task(1, QStringLiteral("Long"), day(10), day(23)), task(2, QStringLiteral("Beyond"), QDate(2026, 12, 28), QDate(2027, 1, 5)),
            task(3, QStringLiteral("Before"), day(1), day(5))});
        const auto *e = entryOf(r, QStringLiteral("gantt:task:1"));
        QVERIFY(e);
        QVERIFY(e->clippedLeft);
        QVERIFY(!e->clippedRight);
        QCOMPARE(e->rect.left(), 0.0);
        QCOMPARE(e->rect.right(), scaleFor(TimelineZoom::Weeks).xAfterDay(day(23)));
        QVERIFY(!entryOf(r, QStringLiteral("gantt:task:3"))); // wholly before the range: not drawn
        // The range ends on 2026-12-22 (63 days from 2026-10-21): this one starts after it.
        QVERIFY(!entryOf(r, QStringLiteral("gantt:task:2")));
        const auto end = layout({task(4, QStringLiteral("Edge"), QDate(2026, 12, 20), QDate(2026, 12, 30))});
        QVERIFY(entryOf(end, QStringLiteral("gantt:task:4"))->clippedRight);
    }

    void diamondsSitInTheMiddleOfTheirDay()
    {
        const auto r = layout({milestone(301, QStringLiteral("Launch"), day(29)), card(501, QStringLiteral("Call supplier"), day(27))});
        const auto s = scaleFor(TimelineZoom::Weeks);
        const QPair<QString, QDate> pairs[] = {qMakePair(QStringLiteral("gantt:ms:301"), day(29)), qMakePair(QStringLiteral("deck:card:501"), day(27))};
        for (const auto &pair : pairs) {
            const auto *e = entryOf(r, pair.first);
            QVERIFY(e);
            QCOMPARE(e->type, TimelineEntry::Type::Diamond);
            QCOMPARE(e->rect.center().x(), s.xOfDay(pair.second) + 6.0);
            QCOMPARE(e->rect.width(), TimelineLayout::kDiamond);
            QCOMPARE(e->start, pair.second);
            QCOMPARE(e->end, pair.second);
        }
        QCOMPARE(entryOf(r, QStringLiteral("gantt:ms:301"))->glyph, QStringLiteral("milestone"));
        QCOMPARE(entryOf(r, QStringLiteral("deck:card:501"))->glyph, QStringLiteral("card"));
    }

    void todosNeedAStartAndADueDateAndNeverRepeat()
    {
        auto both = todo(QStringLiteral("Both"), day(26), day(30));
        auto onlyDue = todo(QStringLiteral("OnlyDue"), QDate(), day(30));
        auto onlyStart = todo(QStringLiteral("OnlyStart"), day(26), QDate());
        auto repeating = todo(QStringLiteral("Repeating"), day(26), day(30));
        repeating.repeating = true;
        auto backwards = todo(QStringLiteral("Backwards"), day(30), day(26));
        const auto r = layout({both, onlyDue, onlyStart, repeating, backwards});
        QVERIFY(entryOf(r, both.key));
        QCOMPARE(entryOf(r, both.key)->type, TimelineEntry::Type::Bar);
        QVERIFY(!entryOf(r, onlyDue.key));
        QVERIFY(!entryOf(r, onlyStart.key));
        QVERIFY(!entryOf(r, repeating.key));
        QVERIFY(!entryOf(r, backwards.key));
        QVERIFY(entryOf(r, both.key)->canMove);
    }

    void todosAndCardsAreGroupedUnderTheirListAndBoard()
    {
        const auto r = layout({todo(QStringLiteral("Draft"), day(26), day(30)), card(501, QStringLiteral("Call"), day(27)), task(1, QStringLiteral("T"), day(26), day(27))});
        QStringList groups;
        for (const auto &e : r.entries) {
            if (e.type == TimelineEntry::Type::Group) {
                groups.append(e.title);
            }
        }
        const QString sep = QString(QChar(0x203A));
        QCOMPARE(groups, (QStringList{QStringLiteral("Website"), QStringLiteral("NoTasks ") + sep + QStringLiteral(" Personal"), QStringLiteral("NoBoard ") + sep + QStringLiteral(" Team board")}));
        QCOMPARE(entryOf(r, QStringLiteral("deck:card:501"))->groupKey, QStringLiteral("board:10"));
    }

    void onlyMyOwnWorkIsDrawn()
    {
        auto team = task(1, QStringLiteral("Team"), day(26), day(27));
        team.workKind = WorkKind::Team;
        auto info = todo(QStringLiteral("Info"), day(26), day(27));
        info.info = true;
        auto problem = task(2, QStringLiteral("Problem"), day(26), day(27));
        problem.problemOnly = true;
        auto done = task(3, QStringLiteral("Done"), day(26), day(27));
        done.done = true;
        AgendaItem event;
        event.key = QStringLiteral("cal:x/e.ics");
        event.kind = ItemKind::Event;
        event.home = Home::Calendar;
        event.title = QStringLiteral("Event");
        event.timed = true;
        event.start = QDateTime(day(26), QTime(10, 0), QTimeZone::UTC);
        auto load = task(4, QStringLiteral("Load"), day(26), day(27));
        load.agentLoad = true;
        auto noProject = task(5, QStringLiteral("NoProject"), day(26), day(27), 0);
        const auto r = layout({team, info, problem, done, event, load, noProject});
        QVERIFY(r.entries.isEmpty());
        QCOMPARE(r.rowCount, 0);
        QVERIFY(r.arrows.isEmpty());
        // The shading is still there: it is about my time, whatever is drawn.
        QCOMPARE(static_cast<int>(r.shading.size()), 63);
    }

    void arrowEndpointsForTheFourTypes_data()
    {
        QTest::addColumn<QString>("type");
        QTest::addColumn<bool>("fromFinish");
        QTest::addColumn<bool>("toStart");
        QTest::newRow("finish to start") << QStringLiteral("EA") << true << true;
        QTest::newRow("start to start") << QStringLiteral("AA") << false << true;
        QTest::newRow("finish to finish") << QStringLiteral("EE") << true << false;
        QTest::newRow("start to finish") << QStringLiteral("AE") << false << false;
    }

    void arrowEndpointsForTheFourTypes()
    {
        QFETCH(QString, type);
        QFETCH(bool, fromFinish);
        QFETCH(bool, toStart);
        const auto first = task(1, QStringLiteral("First"), day(26), day(28));
        auto second = task(2, QStringLiteral("Second"), day(29), day(31));
        ItemDependency d;
        d.predecessorKey = first.key;
        d.type = type;
        second.dependencies.append(d);
        for (const auto zoom : {TimelineZoom::Weeks, TimelineZoom::Days}) {
            const auto r = layout({first, second}, zoom);
            QCOMPARE(static_cast<int>(r.arrows.size()), 1);
            const auto &a = r.arrows.first();
            QVERIFY(!a.stub);
            QCOMPARE(a.fromKey, first.key);
            QCOMPARE(a.toKey, second.key);
            QCOMPARE(a.type, type);
            const auto *p = entryOf(r, first.key);
            const auto *s = entryOf(r, second.key);
            const QPointF want0(fromFinish ? p->rect.right() : p->rect.left(), p->rect.center().y());
            const QPointF want1(toStart ? s->rect.left() : s->rect.right(), s->rect.center().y());
            QCOMPARE(a.points.first(), want0);
            QCOMPARE(a.points.last(), want1);
            QVERIFY(axisAligned(a.points));
            // The arrow arrives from outside the bar: from the left at a start, from the right at a finish.
            const QPointF before = a.points.at(a.points.size() - 2);
            if (toStart) {
                QVERIFY2(before.x() < want1.x(), "arrives from the left");
            } else {
                QVERIFY2(before.x() > want1.x(), "arrives from the right");
            }
            // And it leaves the predecessor to the right at a finish, to the left at a start.
            const QPointF after = a.points.at(1);
            if (fromFinish) {
                QVERIFY2(after.x() > want0.x(), "leaves to the right");
            } else {
                QVERIFY2(after.x() < want0.x(), "leaves to the left");
            }
        }
    }

    void aPredecessorThatIsNotOnTheScreenIsAShortStub()
    {
        auto second = task(2, QStringLiteral("Publish"), day(29), day(31));
        ItemDependency d;
        d.predecessorKey = QStringLiteral("gantt:task:99");
        d.predecessorTitle = QStringLiteral("Banner design");
        d.predecessorOwner = QStringLiteral("Sara");
        second.dependencies.append(d);
        ItemDependency noOwner;
        noOwner.predecessorKey = QStringLiteral("gantt:ms:7");
        noOwner.predecessorTitle = QStringLiteral("Kick-off");
        noOwner.type = QStringLiteral("EE");
        second.dependencies.append(noOwner);
        const auto r = layout({second});
        QCOMPARE(static_cast<int>(r.arrows.size()), 2);
        const auto *s = entryOf(r, second.key);
        const auto &stub = r.arrows.at(0);
        QVERIFY(stub.stub);
        QVERIFY(stub.fromKey.isEmpty());
        QCOMPARE(static_cast<int>(stub.points.size()), 2);
        QCOMPARE(stub.points.last(), QPointF(s->rect.left(), s->rect.center().y()));
        QCOMPARE(stub.points.first().x(), s->rect.left() - TimelineLayout::kStub);
        QCOMPARE(stub.tooltip, QStringLiteral("After: Banner design (Sara)"));
        // A finish type enters from the right.
        QCOMPARE(r.arrows.at(1).points.last(), QPointF(s->rect.right(), s->rect.center().y()));
        QVERIFY(r.arrows.at(1).points.first().x() > s->rect.right());
        QCOMPARE(r.arrows.at(1).tooltip, QStringLiteral("After: Kick-off"));
    }

    void aPredecessorThatIsFilteredOutIsAStubToo()
    {
        const auto first = task(1, QStringLiteral("First"), day(26), day(28));
        auto second = task(2, QStringLiteral("Second"), day(29), day(31));
        ItemDependency d;
        d.predecessorKey = first.key;
        d.predecessorTitle = first.title;
        second.dependencies.append(d);
        const auto r = layout({second}); // the chips hid the first one
        QCOMPARE(static_cast<int>(r.arrows.size()), 1);
        QVERIFY(r.arrows.first().stub);
        QCOMPARE(r.arrows.first().tooltip, QStringLiteral("After: First"));
    }

    void theLagIsInTheTooltip()
    {
        const auto first = task(1, QStringLiteral("First"), day(26), day(28));
        auto second = task(2, QStringLiteral("Second"), day(30), day(31));
        ItemDependency d;
        d.predecessorKey = first.key;
        d.lagDays = 2;
        second.dependencies.append(d);
        const auto r = layout({first, second});
        QCOMPARE(static_cast<int>(r.arrows.size()), 1);
        QCOMPARE(r.arrows.first().lagDays, 2.0);
        QVERIFY2(r.arrows.first().tooltip.contains(QStringLiteral("First")), qPrintable(r.arrows.first().tooltip));
        QVERIFY2(r.arrows.first().tooltip.contains(QStringLiteral("Finish to start")), qPrintable(r.arrows.first().tooltip));
        QVERIFY2(r.arrows.first().tooltip.contains(QStringLiteral("lag 2 days")), qPrintable(r.arrows.first().tooltip));
        QCOMPARE(TimelineLayout::typeText(QStringLiteral("AA")), QStringLiteral("Start to start"));
        QCOMPARE(TimelineLayout::typeText(QStringLiteral("EE")), QStringLiteral("Finish to finish"));
        QCOMPARE(TimelineLayout::typeText(QStringLiteral("AE")), QStringLiteral("Start to finish"));
    }

    void aMilestoneCanBeAPredecessor()
    {
        const auto m = milestone(301, QStringLiteral("Kick-off"), day(26));
        auto second = task(2, QStringLiteral("Second"), day(28), day(30));
        ItemDependency d;
        d.predecessorKey = m.key;
        second.dependencies.append(d);
        const auto r = layout({m, second});
        QCOMPARE(static_cast<int>(r.arrows.size()), 1);
        QVERIFY(!r.arrows.first().stub);
        QCOMPARE(r.arrows.first().points.first().x(), entryOf(r, m.key)->rect.right());
    }

    void shadingFollowsMyBusyTimeAndDaysOffAreMarked()
    {
        WorkingHours hours; // Monday to Friday, 09:00 - 17:00
        AgendaItem busy;
        busy.key = QStringLiteral("cal:work/long.ics");
        busy.kind = ItemKind::Event;
        busy.home = Home::Calendar;
        busy.title = QStringLiteral("Workshop");
        busy.timed = true;
        busy.workKind = WorkKind::Self;
        busy.start = QDateTime(day(28), QTime(9, 0), QTimeZone::UTC);
        busy.end = QDateTime(day(28), QTime(13, 0), QTimeZone::UTC);
        // An agent's event never shades my time.
        auto agentEvent = busy;
        agentEvent.key = QStringLiteral("cal:work/agent.ics");
        agentEvent.start = QDateTime(day(29), QTime(9, 0), QTimeZone::UTC);
        agentEvent.end = QDateTime(day(29), QTime(17, 0), QTimeZone::UTC);
        agentEvent.workKind = WorkKind::MyAgent;
        agentEvent.agent.isSelf = false;
        agentEvent.agent.id = QStringLiteral("AG-0001");
        // A free ("transparent") event neither.
        auto freeEvent = busy;
        freeEvent.key = QStringLiteral("cal:work/free.ics");
        freeEvent.start = QDateTime(day(30), QTime(9, 0), QTimeZone::UTC);
        freeEvent.end = QDateTime(day(30), QTime(17, 0), QTimeZone::UTC);
        freeEvent.transparent = true;
        const QList<AgendaItem> mine{busy, agentEvent, freeEvent};
        const DayModel model(mine, hours, {day(27)}, QTimeZone(QTimeZone::UTC)); // the 27th is a holiday
        const auto r = layout({}, TimelineZoom::Weeks, model, mine);
        QCOMPARE(static_cast<int>(r.shading.size()), 63);
        auto shade = [&r](int d) { return r.shading.at(static_cast<int>(kFirst.daysTo(day(d)))); };
        QCOMPARE(shade(28).x, 7 * 12.0);
        QCOMPARE(shade(28).width, 12.0);
        QVERIFY(!shade(28).dayOff);
        QVERIFY2(qAbs(shade(28).alpha - 0.35 * 0.5) < 0.001, qPrintable(QString::number(shade(28).alpha))); // 4 of 8 hours
        QCOMPARE(shade(29).alpha, 0.0); // an agent's event
        QCOMPARE(shade(30).alpha, 0.0); // a free event
        QCOMPARE(shade(26).alpha, 0.0); // nothing that day
        QVERIFY(shade(24).dayOff); // Saturday
        QVERIFY(shade(25).dayOff); // Sunday
        QVERIFY(shade(27).dayOff); // the holiday
        QCOMPARE(shade(24).alpha, 0.0);
        QVERIFY(!shade(26).dayOff);
        for (const auto &s : r.shading) {
            QVERIFY(s.alpha >= 0.0 && s.alpha <= 0.35 + 1e-9);
        }
        // A full day of busy time is the strongest tint.
        auto full = busy;
        full.end = QDateTime(day(28), QTime(17, 0), QTimeZone::UTC);
        const DayModel model2({full}, hours, {}, QTimeZone(QTimeZone::UTC));
        const auto r2 = layout({}, TimelineZoom::Weeks, model2, {full});
        QVERIFY(qAbs(r2.shading.at(7).alpha - 0.35) < 0.001);
    }

    void todayIsMarkedOnlyInsideTheRange()
    {
        TimelineLayoutInput in;
        in.scale = scaleFor(TimelineZoom::Weeks);
        in.today = day(28);
        QCOMPARE(TimelineLayout::build(in).todayX, 7 * 12.0);
        in.today = QDate(2027, 3, 1);
        QCOMPARE(TimelineLayout::build(in).todayX, -1.0);
    }

    void aDragMovesStartAndEndTogetherByWholeDays()
    {
        const auto r = layout({task(1, QStringLiteral("T"), day(26), day(28))});
        const auto *e = entryOf(r, QStringLiteral("gantt:task:1"));
        QVERIFY(e->canMove);
        QVERIFY(!e->locked);
        auto d = TimelineLayout::drag(*e, 2 * 12.0, 12.0);
        QVERIFY(d.allowed);
        QCOMPARE(d.days, 2);
        QCOMPARE(d.start, day(28));
        QCOMPARE(d.end, day(30));
        d = TimelineLayout::drag(*e, -1 * 12.0 - 2, 12.0);
        QCOMPARE(d.days, -1);
        QCOMPARE(d.start, day(25));
        QCOMPARE(d.end, day(27));
        d = TimelineLayout::drag(*e, 0.49 * 12.0, 12.0);
        QVERIFY(d.allowed);
        QCOMPARE(d.days, 0);
        QCOMPARE(d.start, day(26));
        QCOMPARE(d.end, day(28));
        d = TimelineLayout::drag(*e, 0.51 * 40.0, 40.0);
        QCOMPARE(d.days, 1);
    }

    void readOnlyItemsGiveNoDragCommand()
    {
        auto viewer = task(1, QStringLiteral("Viewer"), day(26), day(28));
        viewer.readOnly = true;
        viewer.readOnlyReason = QStringLiteral("You can view this project only. Ask the project manager for editor rights.");
        auto guard = task(2, QStringLiteral("Guard"), day(26), day(28));
        guard.versionReadOnly = true;
        auto invitationLike = todo(QStringLiteral("Locked list"), day(26), day(28));
        invitationLike.readOnly = true;
        invitationLike.readOnlyReason = QStringLiteral("This calendar is read only for you.");
        auto notSent = task(3, QStringLiteral("NotSent"), day(26), day(28));
        notSent.pending = PendingState::Conflict;
        auto team = milestone(301, QStringLiteral("Launch"), day(29));
        const auto r = layout({viewer, guard, invitationLike, notSent, team});
        for (const auto &key : {viewer.key, guard.key, invitationLike.key, notSent.key}) {
            const auto *e = entryOf(r, key);
            QVERIFY(e);
            QVERIFY2(!e->canMove, qPrintable(key));
            QVERIFY2(e->locked, qPrintable(key));
            QVERIFY2(!e->lockReason.isEmpty(), qPrintable(key));
            const auto d = TimelineLayout::drag(*e, 5 * 12.0, 12.0);
            QVERIFY2(!d.allowed, qPrintable(key));
            QCOMPARE(d.days, 0);
            QCOMPARE(d.start, e->start);
            QCOMPARE(d.end, e->end);
            QCOMPARE(d.reason, e->lockReason);
        }
        QCOMPARE(entryOf(r, viewer.key)->lockReason, viewer.readOnlyReason);
        // A diamond is never dragged.
        const auto d = TimelineLayout::drag(*entryOf(r, team.key), 5 * 12.0, 12.0);
        QVERIFY(!d.allowed);
        QCOMPARE(d.days, 0);
    }

    void aTaskWithOnlyAnEndDateIsDrawnButCannotBeMoved()
    {
        auto t = task(7, QStringLiteral("Only an end"), QDate(), day(28));
        t.due = day(28);
        const auto r = layout({t});
        const auto *e = entryOf(r, t.key);
        QVERIFY(e); // it is still on the chart, as a one-day bar
        QCOMPARE(e->start, day(28));
        QCOMPARE(e->end, day(28));
        QVERIFY(!e->canMove);
        QVERIFY(e->locked);
        QCOMPARE(e->lockReason, QStringLiteral("This task has no dates to move."));
        const auto d = TimelineLayout::drag(*e, 3 * 12.0, 12.0);
        QVERIFY(!d.allowed);
        QCOMPARE(d.days, 0);
        QCOMPARE(d.reason, e->lockReason);
    }

    void waitingAndLateAndBlockedAreCarried()
    {
        auto t = task(1, QStringLiteral("T"), day(20), day(26));
        t.late = true;
        t.blocked = true;
        t.pending = PendingState::Waiting;
        const auto r = layout({t});
        const auto *e = entryOf(r, t.key);
        QVERIFY(e->late);
        QVERIFY(e->blocked);
        QVERIFY(e->waiting);
        QVERIFY(!e->notSent);
        QVERIFY(e->canMove); // a change that waits can be followed by another
    }

    void theLayoutIsTheSameEveryTime()
    {
        const QList<AgendaItem> items{task(3, QStringLiteral("C"), day(28), day(29)), task(1, QStringLiteral("A"), day(26), day(27)), task(2, QStringLiteral("B"), day(26), day(27)),
            milestone(9, QStringLiteral("M"), day(27)), todo(QStringLiteral("D"), day(26), day(29)), card(5, QStringLiteral("E"), day(30))};
        const auto a = layout(items);
        auto reversed = items;
        std::reverse(reversed.begin(), reversed.end());
        const auto b = layout(reversed);
        QCOMPARE(a.entries.size(), b.entries.size());
        for (int i = 0; i < a.entries.size(); ++i) {
            QCOMPARE(a.entries.at(i).key, b.entries.at(i).key);
            QCOMPARE(a.entries.at(i).rect, b.entries.at(i).rect);
        }
    }
};

QTEST_GUILESS_MAIN(TestNoDriveAgendaTimelineLayout)
#include "testnodriveagendatimelinelayout.moc"
