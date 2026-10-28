/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: the iCalendar text reader and the dates and times.

#include "nodrive/noagenda/agendatestsupport.h"

#include "gui/nourax/noagenda/ical/icaltext.h"
#include "gui/nourax/noagenda/ical/icaltime.h"

using namespace AgendaTest;

class TestNoDriveAgendaICal : public QObject
{
    Q_OBJECT

    static ICalComponent first(const QString &ics, const QString &type = QStringLiteral("VEVENT"))
    {
        const auto components = ICalText::parse(ics);
        for (const auto &c : components) {
            if (c.type == type) {
                return c;
            }
        }
        return ICalComponent();
    }

private Q_SLOTS:
    void unfoldsLongLines()
    {
        const auto lines = ICalText::unfold(QStringLiteral("SUMMARY:A very long\r\n  title that continues\r\nUID:1\r\n"));
        QCOMPARE(lines.at(0), QStringLiteral("SUMMARY:A very long title that continues"));
        QCOMPARE(lines.at(1), QStringLiteral("UID:1"));
    }

    void readsPropertiesParametersAndEscapes()
    {
        const auto c = first(QStringLiteral("BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nUID:x\r\nSUMMARY:Lunch\\, with\\; Ali\\nsecond line\r\n"
                                            "ATTENDEE;CN=\"Doe, Jane\";PARTSTAT=ACCEPTED:mailto:jane@example.test\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n"));
        QCOMPARE(c.text(QStringLiteral("SUMMARY")), QStringLiteral("Lunch, with; Ali\nsecond line"));
        const auto *attendee = c.first(QStringLiteral("attendee"));
        QVERIFY(attendee);
        QCOMPARE(attendee->param(QStringLiteral("PARTSTAT")), QStringLiteral("ACCEPTED"));
        QCOMPARE(attendee->param(QStringLiteral("cn")), QStringLiteral("Doe, Jane")); // quoted comma kept
        QCOMPARE(attendee->raw, QStringLiteral("mailto:jane@example.test"));
    }

    void categoriesAsAListAndAsRepeatedProperties()
    {
        const auto c = first(QStringLiteral("BEGIN:VCALENDAR\nBEGIN:VEVENT\nCATEGORIES:agent:AG-0001,review\\, final\nCATEGORIES:extra\nEND:VEVENT\nEND:VCALENDAR\n"));
        QStringList all;
        const auto props = c.all(QStringLiteral("CATEGORIES"));
        for (const auto *p : props) {
            all.append(p->textList());
        }
        QCOMPARE(all, (QStringList{QStringLiteral("agent:AG-0001"), QStringLiteral("review, final"), QStringLiteral("extra")}));
    }

    void keepsNestedAlarmsAsChildren()
    {
        const auto c = first(QStringLiteral("BEGIN:VCALENDAR\nBEGIN:VEVENT\nUID:1\nBEGIN:VALARM\nTRIGGER:-PT15M\nEND:VALARM\nSUMMARY:S\nEND:VEVENT\nEND:VCALENDAR\n"));
        QCOMPARE(c.children.size(), 1);
        QCOMPARE(c.children.first().type, QStringLiteral("VALARM"));
        QCOMPARE(c.text(QStringLiteral("SUMMARY")), QStringLiteral("S"));
    }

    void listsEventsTodosAndTimezones()
    {
        const auto comps = ICalText::parse(QStringLiteral("BEGIN:VCALENDAR\nBEGIN:VTIMEZONE\nTZID:X\nEND:VTIMEZONE\nBEGIN:VEVENT\nUID:e\nEND:VEVENT\nBEGIN:VTODO\nUID:t\nEND:VTODO\nEND:VCALENDAR\n"));
        QStringList types;
        for (const auto &c : comps) {
            types.append(c.type);
        }
        QCOMPARE(types, (QStringList{QStringLiteral("VTIMEZONE"), QStringLiteral("VEVENT"), QStringLiteral("VTODO")}));
    }

    void dateAndDateTimeValues()
    {
        const QTimeZone zone(QTimeZone::UTC);
        const auto date = ICalTime::parseValue(QStringLiteral("20261030"), QString(), false, zone);
        QVERIFY(date.valid && date.isDate);
        QCOMPARE(date.date, QDate(2026, 10, 30));
        const auto utc = ICalTime::parseValue(QStringLiteral("20261028T100000Z"), QString(), false, zone);
        QVERIFY(utc.valid && !utc.isDate);
        QCOMPARE(utc.utc, QDateTime(QDate(2026, 10, 28), QTime(10, 0), QTimeZone::UTC));
        QVERIFY(!ICalTime::parseValue(QStringLiteral("nonsense"), QString(), false, zone).valid);
        QVERIFY(!ICalTime::parseValue(QStringLiteral("20261328T100000Z"), QString(), false, zone).valid);
    }

    void tzidWithAnIanaName()
    {
        const QTimeZone zone(QTimeZone::UTC);
        // Summer time in London: 10:00 local is 09:00 UTC.
        const auto summer = ICalTime::parseValue(QStringLiteral("20260715T100000"), QStringLiteral("Europe/London"), false, zone);
        QVERIFY(summer.valid && !summer.approximated);
        QCOMPARE(summer.utc, QDateTime(QDate(2026, 7, 15), QTime(9, 0), QTimeZone::UTC));
        // New York is four hours behind in summer.
        const auto ny = ICalTime::parseValue(QStringLiteral("20260715T100000"), QStringLiteral("America/New_York"), false, zone);
        QCOMPARE(ny.utc, QDateTime(QDate(2026, 7, 15), QTime(14, 0), QTimeZone::UTC));
    }

    void tzidWithAWindowsName()
    {
        const QTimeZone zone(QTimeZone::UTC);
        const auto t = ICalTime::parseValue(QStringLiteral("20261201T120000"), QStringLiteral("GMT Standard Time"), false, zone);
        QVERIFY(t.valid);
        QVERIFY(!t.approximated);
        QCOMPARE(t.utc, QDateTime(QDate(2026, 12, 1), QTime(12, 0), QTimeZone::UTC)); // London in winter
    }

    void anUnknownZoneIsApproximatedNotGuessedSilently()
    {
        const QTimeZone zone(QTimeZone::UTC);
        const auto t = ICalTime::parseValue(QStringLiteral("20261028T100000"), QStringLiteral("Nowhere/Land"), false, zone);
        QVERIFY(t.valid);
        QVERIFY(t.approximated);
    }

    void floatingTimeUsesTheGivenZone()
    {
        const QTimeZone london(QByteArrayLiteral("Europe/London"));
        const auto t = ICalTime::parseValue(QStringLiteral("20260715T100000"), QString(), false, london);
        QVERIFY(t.floating);
        QCOMPARE(t.utc, QDateTime(QDate(2026, 7, 15), QTime(9, 0), QTimeZone::UTC));
    }

    void durations()
    {
        QCOMPARE(ICalTime::parseDurationSeconds(QStringLiteral("PT1H30M")), qint64(5400));
        QCOMPARE(ICalTime::parseDurationSeconds(QStringLiteral("P1D")), qint64(86400));
        QCOMPARE(ICalTime::parseDurationSeconds(QStringLiteral("P1W")), qint64(7 * 86400));
        QCOMPARE(ICalTime::parseDurationSeconds(QStringLiteral("-PT15M")), qint64(-900));
        QCOMPARE(ICalTime::parseDurationSeconds(QStringLiteral("nonsense")), qint64(-1));
    }

    void readsRepeatingAndAgentMarks()
    {
        const auto records = CalendarReader::parseObject(QStringLiteral("work"), QStringLiteral("/x/a.ics"), QStringLiteral("e"),
            QStringLiteral("BEGIN:VCALENDAR\nBEGIN:VEVENT\nUID:1\nSUMMARY:Stand-up\nDTSTART:20261028T093000Z\nDTEND:20261028T094500Z\nRECURRENCE-ID:20261028T093000Z\n"
                           "X-NOURAX-AGENT-ID:AG-0001\nCATEGORIES:agent:AG-0001\nEND:VEVENT\nEND:VCALENDAR\n"),
            false, QTimeZone(QTimeZone::UTC));
        QCOMPARE(records.size(), 1);
        QVERIFY(records.first().repeating);
        QCOMPARE(records.first().agentId, QStringLiteral("AG-0001"));
        QCOMPARE(records.first().categories, QStringList{QStringLiteral("agent:AG-0001")});
        QCOMPARE(records.first().end, QDateTime(QDate(2026, 10, 28), QTime(9, 45), QTimeZone::UTC));
    }

    void anAllDayEventEndsOnTheLastDay()
    {
        const auto records = CalendarReader::parseObject(QStringLiteral("work"), QStringLiteral("/x/a.ics"), QString(),
            QStringLiteral("BEGIN:VCALENDAR\nBEGIN:VEVENT\nUID:1\nSUMMARY:Trip\nDTSTART;VALUE=DATE:20261030\nDTEND;VALUE=DATE:20261102\nEND:VEVENT\nEND:VCALENDAR\n"),
            false, QTimeZone(QTimeZone::UTC));
        QCOMPARE(records.size(), 1);
        QVERIFY(records.first().allDay);
        QCOMPARE(records.first().startDate, QDate(2026, 10, 30));
        QCOMPARE(records.first().endDate, QDate(2026, 11, 1)); // DTEND is exclusive
    }

    void todosKeepTheirDueAndAreNeverReadAsEvents()
    {
        const QString ics = QStringLiteral("BEGIN:VCALENDAR\nBEGIN:VTODO\nUID:t\nSUMMARY:Pay\nDUE;VALUE=DATE:20261030\nPERCENT-COMPLETE:30\nEND:VTODO\nEND:VCALENDAR\n");
        QVERIFY(CalendarReader::parseObject(QStringLiteral("p"), QStringLiteral("/x"), QString(), ics, false, QTimeZone(QTimeZone::UTC)).isEmpty());
        const auto todos = CalendarReader::parseObject(QStringLiteral("p"), QStringLiteral("/x"), QString(), ics, true, QTimeZone(QTimeZone::UTC));
        QCOMPARE(todos.size(), 1);
        QVERIFY(todos.first().isTodo);
        QVERIFY(todos.first().dueIsDate);
        QCOMPARE(todos.first().dueDate, QDate(2026, 10, 30));
        QCOMPARE(todos.first().percent, 30);
    }
};

QTEST_GUILESS_MAIN(TestNoDriveAgendaICal)
#include "testnodriveagendaical.moc"
