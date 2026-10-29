/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda (edit step): the four conflict kinds of design 7 (clash, overbooked, deadline on a day off, blocked), the
// workload of a day, and the load of an agent against its registry availability. Agent items never take the
// person's time.

#include "nodrive/noagenda/agendatestsupport.h"

#include "gui/nourax/noagenda/logic/agentload.h"
#include "gui/nourax/noagenda/logic/conflicts.h"
#include "gui/nourax/noagenda/logic/workload.h"

#include <QJsonArray>
#include <QJsonDocument>

using namespace AgendaTest;

namespace {

QDateTime at(int day, int hour, int minute = 0)
{
    return QDateTime(QDate(2026, 10, day), QTime(hour, minute), QTimeZone::UTC);
}

AgendaItem timedEvent(const QString &title, const QDateTime &start, const QDateTime &end)
{
    AgendaItem i;
    i.key = QStringLiteral("cal:x/") + title;
    i.kind = ItemKind::Event;
    i.home = Home::Calendar;
    i.title = title;
    i.timed = true;
    i.start = start;
    i.end = end;
    i.workKind = WorkKind::Self;
    return i;
}

AgendaItem allDayEvent(const QString &title, const QDate &first, const QDate &last)
{
    AgendaItem i;
    i.key = QStringLiteral("cal:x/") + title;
    i.kind = ItemKind::Event;
    i.home = Home::Calendar;
    i.title = title;
    i.allDay = true;
    i.startDate = first;
    i.endDate = last;
    i.workKind = WorkKind::Self;
    return i;
}

AgendaItem task(const QString &title, const QDate &first, const QDate &last, double hours)
{
    AgendaItem i;
    i.key = QStringLiteral("gantt:task:") + title;
    i.kind = ItemKind::GanttTask;
    i.home = Home::Gantt;
    i.title = title;
    i.startDate = first;
    i.endDate = last;
    i.due = last;
    i.myWorkHours = hours;
    i.workKind = WorkKind::Self;
    return i;
}

DayModel model(const QList<AgendaItem> &items, const QSet<QDate> &holidays = {})
{
    return DayModel(items, WorkingHours(), holidays, QTimeZone(QTimeZone::UTC));
}

AgentRef agentRef(const QString &id)
{
    AgentRef a;
    a.isSelf = false;
    a.id = id;
    a.name = QStringLiteral("Writer-Bot");
    return a;
}

} // namespace

class TestNoDriveAgendaConflicts : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // ---- clash -------------------------------------------------------------------------------------------------

    void twoTimedItemsThatOverlapClash()
    {
        QList<AgendaItem> items{timedEvent(QStringLiteral("Dentist"), at(28, 10), at(28, 11)), timedEvent(QStringLiteral("Call"), at(28, 10, 30), at(28, 11, 30)),
            timedEvent(QStringLiteral("Later"), at(28, 11, 30), at(28, 12))};
        Conflicts::annotate(&items, model(items));
        QVERIFY(items.at(0).clash && items.at(1).clash);
        QVERIFY(!items.at(0).softClash);
        QVERIFY(!items.at(2).clash); // touching ends do not overlap
        QCOMPARE(items.at(0).clashWith, QStringList{QStringLiteral("Call")});
        QCOMPARE(items.at(1).clashWith, QStringList{QStringLiteral("Dentist")});
    }

    void aTentativeOverlapIsASoftClash()
    {
        QList<AgendaItem> items{timedEvent(QStringLiteral("Dentist"), at(28, 10), at(28, 11)), timedEvent(QStringLiteral("Maybe"), at(28, 10, 30), at(28, 11, 30))};
        items[1].tentative = true;
        Conflicts::annotate(&items, model(items));
        QVERIFY(items.at(0).softClash && items.at(1).softClash);
        QVERIFY(!items.at(0).clash && !items.at(1).clash);
        // A firm overlap wins over a soft one.
        items.append(timedEvent(QStringLiteral("Firm"), at(28, 10, 15), at(28, 10, 45)));
        Conflicts::annotate(&items, model(items));
        QVERIFY(items.at(0).clash);
        QVERIFY(items.at(1).softClash || items.at(1).clash);
    }

    void freeEventsAgentItemsAndInfoNeverClash()
    {
        QList<AgendaItem> items{timedEvent(QStringLiteral("Dentist"), at(28, 10), at(28, 11)), timedEvent(QStringLiteral("Free"), at(28, 10), at(28, 11)),
            timedEvent(QStringLiteral("Bot review"), at(28, 10), at(28, 11)), timedEvent(QStringLiteral("Holiday"), at(28, 10), at(28, 11)),
            timedEvent(QStringLiteral("Team row"), at(28, 10), at(28, 11)), timedEvent(QStringLiteral("Allday"), at(28, 10), at(28, 11))};
        items[1].transparent = true;
        items[2].workKind = WorkKind::MyAgent;
        items[2].agent = agentRef(QStringLiteral("AG-0001"));
        items[3].info = true;
        items[4].workKind = WorkKind::Team;
        items[5].timed = false;
        items[5].allDay = true;
        items[5].startDate = QDate(2026, 10, 28);
        items[5].endDate = QDate(2026, 10, 28);
        Conflicts::annotate(&items, model(items));
        for (const auto &i : items) {
            QVERIFY2(!i.clash && !i.softClash, qPrintable(i.title));
        }
    }

    void aSecondRunDoesNotKeepOldFlags()
    {
        QList<AgendaItem> items{timedEvent(QStringLiteral("A"), at(28, 10), at(28, 11)), timedEvent(QStringLiteral("B"), at(28, 10, 30), at(28, 11, 30))};
        Conflicts::annotate(&items, model(items));
        QVERIFY(items.at(0).clash);
        items[1].start = at(28, 14);
        items[1].end = at(28, 15);
        Conflicts::annotate(&items, model(items));
        QVERIFY(!items.at(0).clash && !items.at(1).clash);
        QVERIFY(items.at(0).clashWith.isEmpty());
    }

    // ---- workload ------------------------------------------------------------------------------------------------

    void aFiveDayTaskOfTenHoursPlansTwoHoursADay()
    {
        QList<AgendaItem> items{task(QStringLiteral("Big"), QDate(2026, 10, 26), QDate(2026, 10, 30), 10)}; // Monday to Friday
        const auto m = model(items);
        for (int d = 26; d <= 30; ++d) {
            const auto load = Workload::forDay(items, m, QDate(2026, 10, d));
            QVERIFY2(qAbs(load.planned - 2.0) < 1e-9, qPrintable(QString::number(d)));
            QVERIFY(!load.overbooked);
            QCOMPARE(load.text(), QStringLiteral("2 h planned / 8 h free"));
        }
        QCOMPARE(Workload::forDay(items, m, QDate(2026, 10, 31)).planned, 0.0); // Saturday
        QCOMPARE(Workload::forDay(items, m, QDate(2026, 11, 2)).planned, 0.0);
    }

    void aHolidayAndMyDaysOffAreLeftOutOfTheSpread()
    {
        QList<AgendaItem> items{task(QStringLiteral("Big"), QDate(2026, 10, 26), QDate(2026, 10, 30), 8), allDayEvent(QStringLiteral("Day off"), QDate(2026, 10, 28), QDate(2026, 10, 28))};
        QSet<QDate> holidays{QDate(2026, 10, 27)};
        const auto m = model(items, holidays);
        QCOMPARE(m.workingDaysBetween(QDate(2026, 10, 26), QDate(2026, 10, 30)), 3);
        QVERIFY(qAbs(Workload::forDay(items, m, QDate(2026, 10, 26)).planned - 8.0 / 3) < 1e-9);
        QCOMPARE(Workload::forDay(items, m, QDate(2026, 10, 27)).planned, 0.0);
        const auto off = Workload::forDay(items, m, QDate(2026, 10, 28));
        QVERIFY(!off.workingDay);
        QCOMPARE(off.free, 0.0);
        QCOMPARE(off.planned, 0.0);
    }

    void aDayWithMoreWorkThanFreeTimeIsOverbooked()
    {
        QList<AgendaItem> items{task(QStringLiteral("Six"), QDate(2026, 10, 28), QDate(2026, 10, 28), 6), timedEvent(QStringLiteral("Meeting"), at(28, 9), at(28, 13))};
        const auto load = Workload::forDay(items, model(items), QDate(2026, 10, 28));
        QCOMPARE(load.free, 4.0);
        QCOMPARE(load.planned, 6.0);
        QVERIFY(load.overbooked);
        QCOMPARE(load.text(), QStringLiteral("6 h planned / 4 h free"));
    }

    void busyTimeIsMergedClippedAndTentativeCountsAsBusy()
    {
        QList<AgendaItem> items{timedEvent(QStringLiteral("A"), at(28, 8), at(28, 10)), timedEvent(QStringLiteral("B"), at(28, 9, 30), at(28, 11)),
            timedEvent(QStringLiteral("Late"), at(28, 16), at(28, 19)), timedEvent(QStringLiteral("Maybe"), at(28, 13), at(28, 14))};
        items[3].tentative = true;
        const auto load = Workload::forDay(items, model(items), QDate(2026, 10, 28));
        // 09:00-11:00 (merged and clipped to the working hours) + 16:00-17:00 + the tentative hour = 4 h busy
        QCOMPARE(load.busy, 4.0);
        QCOMPARE(load.free, 4.0);
    }

    void freeEventsAndAgentItemsDoNotReduceMyFreeTimeAndAgentWorkIsNotMine()
    {
        QList<AgendaItem> items{timedEvent(QStringLiteral("Free"), at(28, 9), at(28, 17)), timedEvent(QStringLiteral("Bot"), at(28, 9), at(28, 17)),
            task(QStringLiteral("Supervise"), QDate(2026, 10, 28), QDate(2026, 10, 28), 1), task(QStringLiteral("BotTask"), QDate(2026, 10, 28), QDate(2026, 10, 28), 6)};
        items[0].transparent = true;
        items[1].workKind = WorkKind::MyAgent;
        items[1].agent = agentRef(QStringLiteral("AG-0001"));
        items[3].workKind = WorkKind::MyAgent;
        items[3].agent = agentRef(QStringLiteral("AG-0001"));
        const auto load = Workload::forDay(items, model(items), QDate(2026, 10, 28));
        QCOMPARE(load.free, 8.0);
        QCOMPARE(load.planned, 1.0); // only my own share counts
    }

    void aTaskWithoutAnEstimateAddsNoHoursButIsCounted()
    {
        QList<AgendaItem> items{task(QStringLiteral("Vague"), QDate(2026, 10, 28), QDate(2026, 10, 29), -1)};
        const auto load = Workload::forDay(items, model(items), QDate(2026, 10, 28));
        QCOMPARE(load.planned, 0.0);
        QCOMPARE(load.noEstimate, 1);
        QVERIFY(!load.overbooked);
    }

    void hoursAreShownWithOneDecimalAtMost()
    {
        QCOMPARE(Workload::hoursText(6), QStringLiteral("6"));
        QCOMPARE(Workload::hoursText(3.333333), QStringLiteral("3.3"));
        QCOMPARE(Workload::hoursText(0.25), QStringLiteral("0.3"));
        QCOMPARE(Workload::hoursText(8), QStringLiteral("8"));
    }

    // ---- deadline on a day off -------------------------------------------------------------------------------------------

    void aDeadlineOnADayOffIsFoundFromTheThreeSources()
    {
        AgendaItem onWeekend = task(QStringLiteral("Weekend"), QDate(2026, 10, 29), QDate(2026, 10, 31), 2); // a Saturday
        AgendaItem onHoliday = task(QStringLiteral("Holiday"), QDate(2026, 10, 26), QDate(2026, 10, 27), 2);
        AgendaItem onAllDay = task(QStringLiteral("AllDay"), QDate(2026, 10, 26), QDate(2026, 10, 28), 2);
        AgendaItem fine = task(QStringLiteral("Fine"), QDate(2026, 10, 26), QDate(2026, 10, 29), 2);
        AgendaItem card = fine;
        card.kind = ItemKind::Card;
        card.title = QStringLiteral("Card on day off");
        card.due = QDate(2026, 10, 28);
        QList<AgendaItem> items{onWeekend, onHoliday, onAllDay, fine, card, allDayEvent(QStringLiteral("Away"), QDate(2026, 10, 28), QDate(2026, 10, 28))};
        Conflicts::annotate(&items, model(items, {QDate(2026, 10, 27)}));
        QVERIFY(items.at(0).deadlineDayOff);
        QVERIFY(items.at(1).deadlineDayOff);
        QVERIFY(items.at(2).deadlineDayOff);
        QVERIFY(!items.at(3).deadlineDayOff);
        QVERIFY(items.at(4).deadlineDayOff);
    }

    void anAgentsDeadlineIsNotMyDayOff()
    {
        auto bot = task(QStringLiteral("Bot"), QDate(2026, 10, 29), QDate(2026, 10, 31), 2);
        bot.workKind = WorkKind::MyAgent;
        bot.agent = agentRef(QStringLiteral("AG-0001"));
        QList<AgendaItem> items{bot};
        Conflicts::annotate(&items, model(items));
        QVERIFY(!items.first().deadlineDayOff);
    }

    void theHolidaysOfMyPlanningPersonComeFromTheGanttSnapshot()
    {
        const auto input = loadAliAnswered();
        const auto holidays = Conflicts::holidaysFor(input.gantt, QStringLiteral("ali"));
        QVERIFY(holidays.contains(QDate(2026, 12, 25)));
    }

    // ---- blocked and the whole scenario ---------------------------------------------------------------------------------------

    void blockedComesFromALateDependency()
    {
        const auto out = AgendaBuilder::build(loadAliAnswered());
        const auto *publish = findTitle(out.items, QStringLiteral("Publish banners"));
        QVERIFY(publish);
        QVERIFY(publish->blocked);
        QVERIFY(publish->blockedBy.contains(QStringLiteral("Banner design")));
    }

    void aliTheDentistAndTheClientCallClashAndHerDayIsTooFull()
    {
        const auto input = loadAliAnswered();
        auto out = AgendaBuilder::build(input);
        const auto hours = AgendaBuilder::workingHours(input);
        const DayModel m(out.items, hours, Conflicts::holidaysFor(input.gantt, QStringLiteral("ali")), input.zone);
        Conflicts::annotate(&out.items, m);
        const auto *dentist = findTitle(out.items, QStringLiteral("Dentist"));
        const auto *call = findTitle(out.items, QStringLiteral("Client call"));
        const auto *lunch = findTitle(out.items, QStringLiteral("Maybe lunch"));
        const auto *bot = findTitle(out.items, QStringLiteral("Writer-Bot review"));
        QVERIFY(dentist && call && lunch && bot);
        QVERIFY(dentist->clash && call->clash);
        QVERIFY(dentist->clashWith.contains(QStringLiteral("Client call")));
        QVERIFY(!lunch->clash && !lunch->softClash); // free time
        QVERIFY(!bot->clash && !bot->softClash); // the agent's own time
        const auto load = Workload::forDay(out.items, m, QDate(2026, 10, 28));
        QVERIFY(load.workingDay);
        QVERIFY2(load.overbooked, qPrintable(load.text()));
        QVERIFY(load.planned > load.free);
        QVERIFY(load.busy > 0);
    }

    // ---- agents --------------------------------------------------------------------------------------------------------------------

    void anAgentsCapacityComesFromItsWeeklyPatternAndItsDaysOff()
    {
        const auto registry = aliContextRegistry();
        const auto *writer = registry.byId(QStringLiteral("AG-0001"));
        QVERIFY(writer);
        // the fixture: Monday to Friday, 40 h a week
        QCOMPARE(AgentLoad::capacityOn(*writer, QDate(2026, 10, 28)), 8.0);
        QCOMPARE(AgentLoad::capacityOn(*writer, QDate(2026, 10, 31)), 0.0);
        // no weekly pattern at all: every day of the week counts
        Agent anyDay = *writer;
        anyDay.weekly.clear();
        QVERIFY(qAbs(AgentLoad::capacityOn(anyDay, QDate(2026, 10, 31)) - 40.0 / 7) < 1e-9);

        Agent weekdays = *writer;
        weekdays.weekly = {AgentWeekly{{QStringLiteral("mon"), QStringLiteral("tue"), QStringLiteral("wed"), QStringLiteral("thu"), QStringLiteral("fri")}, QStringLiteral("09:00"), QStringLiteral("17:00")}};
        weekdays.capacityHoursPerWeek = 40;
        weekdays.daysOff = {QStringLiteral("2026-10-29")};
        QCOMPARE(AgentLoad::capacityOn(weekdays, QDate(2026, 10, 28)), 8.0);
        QCOMPARE(AgentLoad::capacityOn(weekdays, QDate(2026, 10, 29)), 0.0); // a day off
        QCOMPARE(AgentLoad::capacityOn(weekdays, QDate(2026, 10, 31)), 0.0); // Saturday
        QVERIFY(!AgentLoad::isAvailable(weekdays, QDate(2026, 11, 1)));
    }

    void anOverbookedAgentIsReportedAsAProblemRowOnItsDay()
    {
        const auto registry = aliContextRegistry();
        Agent weekdays = *registry.byId(QStringLiteral("AG-0001"));
        weekdays.weekly = {AgentWeekly{{QStringLiteral("mon"), QStringLiteral("tue"), QStringLiteral("wed"), QStringLiteral("thu"), QStringLiteral("fri")}, QStringLiteral("09:00"), QStringLiteral("17:00")}};
        weekdays.capacityHoursPerWeek = 20; // 4 h a day
        weekdays.timezone = QStringLiteral("UTC");
        // Rebuild a registry with this agent: parse a small file.
        QJsonObject agent{{QStringLiteral("id"), weekdays.id}, {QStringLiteral("name"), weekdays.name}, {QStringLiteral("person"), QStringLiteral("ali")}, {QStringLiteral("status"), QStringLiteral("active")},
            {QStringLiteral("organisation"), QStringLiteral("demo-org")}, {QStringLiteral("skills"), QJsonArray()},
            {QStringLiteral("availability"), QJsonObject{{QStringLiteral("timezone"), QStringLiteral("UTC")},
                {QStringLiteral("weekly"), QJsonArray{QJsonObject{{QStringLiteral("days"), QJsonArray{QStringLiteral("mon"), QStringLiteral("tue"), QStringLiteral("wed"), QStringLiteral("thu"), QStringLiteral("fri")}}, {QStringLiteral("from"), QStringLiteral("09:00")}, {QStringLiteral("to"), QStringLiteral("17:00")}}}},
                {QStringLiteral("capacity_hours_per_week"), 20}, {QStringLiteral("days_off"), QJsonArray()}}},
            {QStringLiteral("links"), QJsonObject{{QStringLiteral("nogantt_resource_ids"), QJsonArray()}, {QStringLiteral("noboard_label"), QStringLiteral("Writer-Bot")}, {QStringLiteral("calendar_tag"), QStringLiteral("agent:AG-0001")}}}};
        const auto small = AgentRegistry::parse(QJsonDocument(QJsonObject{{QStringLiteral("schema"), QStringLiteral("nourax-agent-registry")}, {QStringLiteral("version"), 1},
            {QStringLiteral("agents"), QJsonArray{agent}}}).toJson());
        QCOMPARE(small.agents().size(), 1);

        auto bot = [&](const QString &title, const QDateTime &s, const QDateTime &e) {
            auto i = timedEvent(title, s, e);
            i.workKind = WorkKind::MyAgent;
            i.agent = agentRef(QStringLiteral("AG-0001"));
            return i;
        };
        // Wednesday: two events of 3 h = 6 h against 4 h. Thursday: one of 3 h.
        QList<AgendaItem> items{bot(QStringLiteral("Draft"), at(28, 9), at(28, 12)), bot(QStringLiteral("Review"), at(28, 13), at(28, 16)), bot(QStringLiteral("Other"), at(29, 9), at(29, 12))};
        auto rows = AgentLoad::compute(small, items, QDate(2026, 10, 26), QDate(2026, 11, 2), QTimeZone(QTimeZone::UTC), QStringLiteral("ali"));
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.first().day, QDate(2026, 10, 28));
        QVERIFY(rows.first().mine);
        QCOMPARE(rows.first().load, 6.0);
        QCOMPARE(rows.first().capacity, 4.0);
        const auto asRows = AgentLoad::asItems(rows, small);
        QCOMPARE(asRows.size(), 1);
        QVERIFY(asRows.first().agentLoad);
        QVERIFY(asRows.first().title.contains(QStringLiteral("Writer-Bot is overbooked on")));
        QCOMPARE(asRows.first().workKind, WorkKind::MyAgent);
        QVERIFY(asRows.first().problemOnly);

        // A NoGantt task: the agent's own 12 h spread over its three working days (Wed-Fri) = 4 h a day, which is the capacity.
        auto gantt = task(QStringLiteral("Bot task"), QDate(2026, 10, 28), QDate(2026, 10, 30), 1);
        gantt.workKind = WorkKind::MyAgent;
        gantt.agent = agentRef(QStringLiteral("AG-0001"));
        gantt.agentWorkHours = 12;
        QCOMPARE(AgentLoad::loadOn(*small.byId(QStringLiteral("AG-0001")), {gantt}, QDate(2026, 10, 29), QTimeZone(QTimeZone::UTC)), 4.0);
        QVERIFY(AgentLoad::compute(small, {gantt}, QDate(2026, 10, 26), QDate(2026, 11, 2), QTimeZone(QTimeZone::UTC), QStringLiteral("ali")).isEmpty());
        gantt.agentWorkHours = 15; // 5 h a day: over
        QCOMPARE(AgentLoad::compute(small, {gantt}, QDate(2026, 10, 26), QDate(2026, 11, 2), QTimeZone(QTimeZone::UTC), QStringLiteral("ali")).size(), 3);
    }

    void aTimeZoneMovesTheDayOfAnAgentsEvents()
    {
        // 23:30 UTC on Wednesday is already Thursday in Tokyo (UTC+9).
        QJsonObject agent{{QStringLiteral("id"), QStringLiteral("AG-0001")}, {QStringLiteral("name"), QStringLiteral("Writer-Bot")}, {QStringLiteral("person"), QStringLiteral("ali")}, {QStringLiteral("status"), QStringLiteral("active")},
            {QStringLiteral("organisation"), QStringLiteral("demo-org")}, {QStringLiteral("skills"), QJsonArray()},
            {QStringLiteral("availability"), QJsonObject{{QStringLiteral("timezone"), QStringLiteral("Asia/Tokyo")},
                {QStringLiteral("weekly"), QJsonArray{QJsonObject{{QStringLiteral("days"), QJsonArray{QStringLiteral("thu")}}, {QStringLiteral("from"), QStringLiteral("00:00")}, {QStringLiteral("to"), QStringLiteral("24:00")}}}},
                {QStringLiteral("capacity_hours_per_week"), 1}, {QStringLiteral("days_off"), QJsonArray()}}},
            {QStringLiteral("links"), QJsonObject{{QStringLiteral("nogantt_resource_ids"), QJsonArray()}, {QStringLiteral("noboard_label"), QStringLiteral("Writer-Bot")}, {QStringLiteral("calendar_tag"), QStringLiteral("agent:AG-0001")}}}};
        const auto registry = AgentRegistry::parse(QJsonDocument(QJsonObject{{QStringLiteral("schema"), QStringLiteral("nourax-agent-registry")}, {QStringLiteral("version"), 1}, {QStringLiteral("agents"), QJsonArray{agent}}}).toJson());
        const auto *writer = registry.byId(QStringLiteral("AG-0001"));
        QVERIFY(writer);
        auto event = timedEvent(QStringLiteral("Night"), at(28, 23, 30), at(29, 0, 30));
        event.workKind = WorkKind::MyAgent;
        event.agent = agentRef(QStringLiteral("AG-0001"));
        // In Tokyo the event is on Thursday 29th (08:30-09:30) and the agent works on Thursdays only: one hour against one hour.
        QCOMPARE(AgentLoad::loadOn(*writer, {event}, QDate(2026, 10, 29), QTimeZone(QTimeZone::UTC)), 1.0);
        QCOMPARE(AgentLoad::loadOn(*writer, {event}, QDate(2026, 10, 28), QTimeZone(QTimeZone::UTC)), 0.0);
        QCOMPARE(AgentLoad::capacityOn(*writer, QDate(2026, 10, 29)), 1.0);
    }

private:
    static AgentRegistry aliContextRegistry() { return AgentRegistry::parse(fixture(QStringLiteral("agent-registry.json"))); }
};

QTEST_GUILESS_MAIN(TestNoDriveAgendaConflicts)
#include "testnodriveagendaconflicts.moc"
