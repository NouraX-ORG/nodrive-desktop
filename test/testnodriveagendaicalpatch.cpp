/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda (edit step): ICalPatch changes only the named properties of one event or to-do and leaves every other
// line byte for byte (alarms, attendees, unknown X- properties, time zones). Also: the text of a new item.

#include "gui/nourax/noagenda/ical/icalpatch.h"
#include "gui/nourax/noagenda/ical/icaltext.h"
#include "gui/nourax/noagenda/ical/icaltime.h"

#include <QTest>

using namespace OCC::Nourax::Agenda;

namespace {

const QString crlf = QStringLiteral("\r\n");

QString joined(const QStringList &lines, const QString &eol = crlf)
{
    return lines.join(eol) + eol;
}

QString sampleEvent(const QString &eol = crlf)
{
    return joined({
        QStringLiteral("BEGIN:VCALENDAR"),
        QStringLiteral("VERSION:2.0"),
        QStringLiteral("PRODID:-//Someone//Calendar//EN"),
        QStringLiteral("BEGIN:VTIMEZONE"),
        QStringLiteral("TZID:Europe/Berlin"),
        QStringLiteral("BEGIN:STANDARD"),
        QStringLiteral("DTSTART:19701025T030000"),
        QStringLiteral("RRULE:FREQ=YEARLY;BYMONTH=10;BYDAY=-1SU"),
        QStringLiteral("TZOFFSETFROM:+0200"),
        QStringLiteral("TZOFFSETTO:+0100"),
        QStringLiteral("END:STANDARD"),
        QStringLiteral("END:VTIMEZONE"),
        QStringLiteral("BEGIN:VEVENT"),
        QStringLiteral("UID:abc-1"),
        QStringLiteral("DTSTAMP:20261001T080000Z"),
        QStringLiteral("SUMMARY:Old title that is long enough to be folded because it goes beyond the limit of"),
        QStringLiteral("  seventy-five octets in a line"),
        QStringLiteral("DTSTART;TZID=Europe/Berlin:20261104T090000"),
        QStringLiteral("DTEND;TZID=Europe/Berlin:20261104T100000"),
        QStringLiteral("X-CUSTOM-PROP;X-PARAM=1:keep me exactly"),
        QStringLiteral("ATTENDEE;CN=Sara;PARTSTAT=ACCEPTED:mailto:sara@example.test"),
        QStringLiteral("CATEGORIES:work,Meeting"),
        QStringLiteral("BEGIN:VALARM"),
        QStringLiteral("ACTION:DISPLAY"),
        QStringLiteral("TRIGGER:-PT15M"),
        QStringLiteral("DESCRIPTION:Reminder"),
        QStringLiteral("END:VALARM"),
        QStringLiteral("END:VEVENT"),
        QStringLiteral("END:VCALENDAR"),
    }, eol);
}

QString sampleTodo()
{
    return joined({
        QStringLiteral("BEGIN:VCALENDAR"),
        QStringLiteral("VERSION:2.0"),
        QStringLiteral("PRODID:-//Someone//Tasks//EN"),
        QStringLiteral("BEGIN:VTODO"),
        QStringLiteral("UID:todo-1"),
        QStringLiteral("DTSTAMP:20261001T080000Z"),
        QStringLiteral("SUMMARY:Pay the bill"),
        QStringLiteral("DUE;VALUE=DATE:20261105"),
        QStringLiteral("STATUS:NEEDS-ACTION"),
        QStringLiteral("X-APPLE-SORT-ORDER:12"),
        QStringLiteral("BEGIN:VALARM"),
        QStringLiteral("ACTION:DISPLAY"),
        QStringLiteral("TRIGGER;RELATED=END:-P1D"),
        QStringLiteral("END:VALARM"),
        QStringLiteral("END:VTODO"),
        QStringLiteral("END:VCALENDAR"),
    });
}

/// The text without the entries (a line and its folded continuation lines) of these property names.
QString without(const QString &text, const QStringList &names)
{
    const auto lines = text.split(QStringLiteral("\n"));
    QStringList kept;
    bool skipping = false;
    for (const auto &line : lines) {
        if (!line.isEmpty() && (line.startsWith(QLatin1Char(' ')) || line.startsWith(QLatin1Char('\t')))) {
            if (!skipping) {
                kept.append(line);
            }
            continue;
        }
        skipping = false;
        for (const auto &name : names) {
            if (line.startsWith(name + QLatin1Char(':')) || line.startsWith(name + QLatin1Char(';'))) {
                skipping = true;
            }
        }
        if (!skipping) {
            kept.append(line);
        }
    }
    return kept.join(QStringLiteral("\n"));
}

QDateTime utc(int y, int m, int d, int h, int min)
{
    return QDateTime(QDate(y, m, d), QTime(h, min), QTimeZone::UTC);
}

ICalEdit baseEdit()
{
    ICalEdit e;
    e.now = utc(2026, 10, 28, 12, 0);
    return e;
}

const QStringList kStampNames{QStringLiteral("DTSTAMP"), QStringLiteral("LAST-MODIFIED"), QStringLiteral("SEQUENCE")};

} // namespace

class TestNoDriveAgendaICalPatch : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void aNewTitleKeepsEveryOtherLineByteForByte()
    {
        auto edit = baseEdit();
        edit.summary = QStringLiteral("New title");
        const auto result = ICalPatch::apply(sampleEvent(), edit);
        QVERIFY2(result.ok, qPrintable(result.error));
        QStringList changed = kStampNames;
        changed.append(QStringLiteral("SUMMARY"));
        QCOMPARE(without(result.text, changed), without(sampleEvent(), changed));
        QVERIFY(result.text.contains(QStringLiteral("SUMMARY:New title\r\n")));
        QVERIFY(result.text.contains(QStringLiteral("DTSTAMP:20261028T120000Z")));
        QVERIFY(result.text.contains(QStringLiteral("LAST-MODIFIED:20261028T120000Z")));
        QVERIFY(result.text.contains(QStringLiteral("SEQUENCE:1")));
        // The old folded title is gone completely.
        QVERIFY(!result.text.contains(QStringLiteral("seventy-five")));
    }

    void aSecondEditRaisesTheSequence()
    {
        auto edit = baseEdit();
        edit.summary = QStringLiteral("One");
        auto first = ICalPatch::apply(sampleEvent(), edit);
        edit.summary = QStringLiteral("Two");
        auto second = ICalPatch::apply(first.text, edit);
        QVERIFY(second.ok);
        QVERIFY(second.text.contains(QStringLiteral("SEQUENCE:2")));
    }

    void newTimesAreWrittenInUtcAndReplaceTheZoneBoundOnes()
    {
        auto edit = baseEdit();
        edit.start = ICalDateValue::ofUtc(utc(2026, 11, 5, 13, 30));
        edit.end = ICalDateValue::ofUtc(utc(2026, 11, 5, 14, 45));
        const auto result = ICalPatch::apply(sampleEvent(), edit);
        QVERIFY(result.ok);
        QVERIFY(result.text.contains(QStringLiteral("DTSTART:20261105T133000Z\r\n")));
        QVERIFY(result.text.contains(QStringLiteral("DTEND:20261105T144500Z\r\n")));
        QVERIFY(!result.text.contains(QStringLiteral("TZID=Europe/Berlin:20261104")));
        // The time zone definition is still there, untouched.
        QVERIFY(result.text.contains(QStringLiteral("BEGIN:VTIMEZONE\r\nTZID:Europe/Berlin\r\n")));
        QStringList changed = kStampNames;
        changed << QStringLiteral("DTSTART") << QStringLiteral("DTEND");
        // DTSTART of the VTIMEZONE (1970) is a different entry and must survive: compare the rest.
        QVERIFY(result.text.contains(QStringLiteral("DTSTART:19701025T030000\r\n")));
        QVERIFY(result.text.contains(QStringLiteral("X-CUSTOM-PROP;X-PARAM=1:keep me exactly\r\n")));
        QVERIFY(result.text.contains(QStringLiteral("ATTENDEE;CN=Sara;PARTSTAT=ACCEPTED:mailto:sara@example.test\r\n")));
        QVERIFY(result.text.contains(QStringLiteral("BEGIN:VALARM\r\nACTION:DISPLAY\r\nTRIGGER:-PT15M\r\nDESCRIPTION:Reminder\r\nEND:VALARM\r\n")));
    }

    void anEventBecomesAllDayAndBackWithTheExclusiveEnd()
    {
        auto edit = baseEdit();
        edit.start = ICalDateValue::ofDate(QDate(2026, 11, 4));
        edit.end = ICalDateValue::ofDate(QDate(2026, 11, 6)); // the day after the last day
        const auto result = ICalPatch::apply(sampleEvent(), edit);
        QVERIFY(result.ok);
        QVERIFY(result.text.contains(QStringLiteral("DTSTART;VALUE=DATE:20261104\r\n")));
        QVERIFY(result.text.contains(QStringLiteral("DTEND;VALUE=DATE:20261106\r\n")));
        const auto components = ICalText::parse(result.text);
        const ICalComponent *event = nullptr;
        for (const auto &c : components) {
            if (c.type == QLatin1String("VEVENT")) {
                event = &c;
            }
        }
        QVERIFY(event);
        QVERIFY(ICalTime::parse(*event->first(QStringLiteral("DTSTART")), QTimeZone(QTimeZone::UTC)).isDate);
    }

    void aDurationIsRemovedWhenTheEndIsSet()
    {
        QString text = sampleEvent();
        text.replace(QStringLiteral("DTEND;TZID=Europe/Berlin:20261104T100000\r\n"), QStringLiteral("DURATION:PT1H\r\n"));
        auto edit = baseEdit();
        edit.start = ICalDateValue::ofUtc(utc(2026, 11, 4, 9, 0));
        edit.end = ICalDateValue::ofUtc(utc(2026, 11, 4, 11, 0));
        const auto result = ICalPatch::apply(text, edit);
        QVERIFY(result.ok);
        QVERIFY(!result.text.contains(QStringLiteral("DURATION")));
        QVERIFY(result.text.contains(QStringLiteral("DTEND:20261104T110000Z\r\n")));
    }

    void anAgentAddsTheIdAndTheTagAndKeepsTheOtherCategories()
    {
        auto edit = baseEdit();
        edit.changeAgent = true;
        edit.agentId = QStringLiteral("AG-0001");
        edit.addCategory = QStringLiteral("agent:AG-0001");
        edit.removeCategories = {QStringLiteral("agent:AG-0001"), QStringLiteral("agent:AG-0002")};
        const auto result = ICalPatch::apply(sampleEvent(), edit);
        QVERIFY(result.ok);
        QVERIFY(result.text.contains(QStringLiteral("X-NOURAX-AGENT-ID:AG-0001\r\n")));
        QVERIFY(result.text.contains(QStringLiteral("CATEGORIES:work,Meeting,agent:AG-0001\r\n")));
    }

    void anotherAgentReplacesTheOldTagInPlace()
    {
        QString text = sampleEvent();
        text.replace(QStringLiteral("CATEGORIES:work,Meeting\r\n"), QStringLiteral("CATEGORIES:work,agent:AG-0001,Meeting\r\nX-NOURAX-AGENT-ID:AG-0001\r\n"));
        auto edit = baseEdit();
        edit.changeAgent = true;
        edit.agentId = QStringLiteral("AG-0002");
        edit.addCategory = QStringLiteral("agent:AG-0002");
        edit.removeCategories = {QStringLiteral("agent:AG-0001"), QStringLiteral("agent:AG-0002")};
        const auto result = ICalPatch::apply(text, edit);
        QVERIFY(result.ok);
        QVERIFY(result.text.contains(QStringLiteral("X-NOURAX-AGENT-ID:AG-0002\r\n")));
        QVERIFY(!result.text.contains(QStringLiteral("AG-0001")));
        QVERIFY(result.text.contains(QStringLiteral("CATEGORIES:work,Meeting,agent:AG-0002\r\n")));
        // Exactly one id property.
        QCOMPARE(result.text.count(QStringLiteral("X-NOURAX-AGENT-ID")), 1);
    }

    void selfRemovesBothAndAnEmptyCategoriesLineGoesAway()
    {
        QString text = sampleEvent();
        text.replace(QStringLiteral("CATEGORIES:work,Meeting\r\n"), QStringLiteral("CATEGORIES:agent:AG-0001\r\nX-NOURAX-AGENT-ID:AG-0001\r\n"));
        auto edit = baseEdit();
        edit.changeAgent = true;
        edit.agentId = QString();
        edit.removeCategories = {QStringLiteral("agent:AG-0001")};
        const auto result = ICalPatch::apply(text, edit);
        QVERIFY(result.ok);
        QVERIFY(!result.text.contains(QStringLiteral("X-NOURAX-AGENT-ID")));
        QVERIFY(!result.text.contains(QStringLiteral("CATEGORIES")));
        QVERIFY(!result.text.contains(QStringLiteral("agent:AG-0001")));
    }

    void categoriesWithSpecialCharactersAreEscaped()
    {
        auto edit = baseEdit();
        edit.changeAgent = true;
        edit.agentId = QStringLiteral("AG-0009");
        edit.addCategory = QStringLiteral("agent:a,b;c");
        const auto result = ICalPatch::apply(sampleEvent(), edit);
        QVERIFY(result.ok);
        QVERIFY(result.text.contains(QStringLiteral("CATEGORIES:work,Meeting,agent:a\\,b\\;c\r\n")));
        // Read back through the strict reader.
        const auto components = ICalText::parse(result.text);
        for (const auto &c : components) {
            if (c.type == QLatin1String("VEVENT")) {
                QVERIFY(c.first(QStringLiteral("CATEGORIES"))->textList().contains(QStringLiteral("agent:a,b;c")));
            }
        }
    }

    void aTodoIsTickedDoneAndUntickedAgain()
    {
        auto edit = baseEdit();
        edit.isTodo = true;
        edit.done = true;
        const auto done = ICalPatch::apply(sampleTodo(), edit);
        QVERIFY2(done.ok, qPrintable(done.error));
        QVERIFY(done.text.contains(QStringLiteral("STATUS:COMPLETED\r\n")));
        QVERIFY(done.text.contains(QStringLiteral("COMPLETED:20261028T120000Z\r\n")));
        QVERIFY(done.text.contains(QStringLiteral("PERCENT-COMPLETE:100\r\n")));
        QVERIFY(done.text.contains(QStringLiteral("TRIGGER;RELATED=END:-P1D\r\n")));
        QVERIFY(done.text.contains(QStringLiteral("X-APPLE-SORT-ORDER:12\r\n")));

        edit.done = false;
        const auto undone = ICalPatch::apply(done.text, edit);
        QVERIFY(undone.ok);
        QVERIFY(undone.text.contains(QStringLiteral("STATUS:NEEDS-ACTION\r\n")));
        QVERIFY(!undone.text.contains(QStringLiteral("COMPLETED")));
        QVERIFY(!undone.text.contains(QStringLiteral("PERCENT-COMPLETE")));
    }

    void aTodoDueDateCanBeChangedAndRemoved()
    {
        auto edit = baseEdit();
        edit.isTodo = true;
        edit.due = ICalDateValue::ofDate(QDate(2026, 11, 9));
        auto result = ICalPatch::apply(sampleTodo(), edit);
        QVERIFY(result.ok);
        QVERIFY(result.text.contains(QStringLiteral("DUE;VALUE=DATE:20261109\r\n")));
        QCOMPARE(result.text.count(QStringLiteral("DUE")), 1);
        edit.due = ICalDateValue::remove();
        result = ICalPatch::apply(result.text, edit);
        QVERIFY(result.ok);
        QVERIFY(!result.text.contains(QStringLiteral("DUE")));
    }

    void aRepeatingItemIsRefused()
    {
        QString text = sampleEvent();
        text.replace(QStringLiteral("UID:abc-1\r\n"), QStringLiteral("UID:abc-1\r\nRRULE:FREQ=DAILY\r\n"));
        auto edit = baseEdit();
        edit.summary = QStringLiteral("x");
        const auto result = ICalPatch::apply(text, edit);
        QVERIFY(!result.ok);
        QCOMPARE(result.error, QStringLiteral("repeats"));
        QVERIFY(ICalPatch::isRepeating(text));
        // The repeat rule of the time zone does not count.
        QVERIFY(!ICalPatch::isRepeating(sampleEvent()));
    }

    void anObjectWithoutTheComponentIsRefused()
    {
        auto edit = baseEdit();
        edit.isTodo = true;
        edit.summary = QStringLiteral("x");
        QVERIFY(!ICalPatch::apply(sampleEvent(), edit).ok);
    }

    void aTextWithPlainLineFeedsKeepsThem()
    {
        auto edit = baseEdit();
        edit.summary = QStringLiteral("LF only");
        const auto result = ICalPatch::apply(sampleEvent(QStringLiteral("\n")), edit);
        QVERIFY(result.ok);
        QVERIFY(!result.text.contains(QLatin1Char('\r')));
        QVERIFY(result.text.contains(QStringLiteral("SUMMARY:LF only\n")));
    }

    void aLongTitleIsFoldedWithoutSplittingACharacter()
    {
        auto edit = baseEdit();
        QString title;
        for (int i = 0; i < 40; ++i) {
            title += QStringLiteral("Zeitübersicht "); // two-byte characters
        }
        edit.summary = title;
        const auto result = ICalPatch::apply(sampleEvent(), edit);
        QVERIFY(result.ok);
        const auto lines = result.text.split(QStringLiteral("\r\n"));
        for (const auto &line : lines) {
            QVERIFY2(line.toUtf8().size() <= 75, qPrintable(line));
        }
        // And it reads back as the same title.
        const auto components = ICalText::parse(result.text);
        QString read;
        for (const auto &c : components) {
            if (c.type == QLatin1String("VEVENT")) {
                read = c.text(QStringLiteral("SUMMARY"));
            }
        }
        QCOMPARE(read, title);
    }

    void aDescriptionWithSpecialCharactersRoundTrips()
    {
        auto edit = baseEdit();
        edit.description = QStringLiteral("Line one\nLine two; with, commas and a \\ backslash");
        const auto result = ICalPatch::apply(sampleEvent(), edit);
        QVERIFY(result.ok);
        const auto components = ICalText::parse(result.text);
        for (const auto &c : components) {
            if (c.type == QLatin1String("VEVENT")) {
                QCOMPARE(c.text(QStringLiteral("DESCRIPTION")), *edit.description);
            }
        }
        // An empty description removes the property.
        edit.description = QString();
        const auto removed = ICalPatch::apply(result.text, edit);
        QVERIFY(removed.ok);
        QVERIFY(!removed.text.contains(QStringLiteral("\r\nDESCRIPTION:Line")));
    }

    void aNewEventPassesTheStrictReader()
    {
        auto edit = baseEdit();
        edit.summary = QStringLiteral("Lunch, with Sara");
        edit.description = QStringLiteral("Bring the contract");
        edit.start = ICalDateValue::ofUtc(utc(2026, 11, 6, 11, 0));
        edit.changeAgent = true;
        edit.agentId = QStringLiteral("AG-0001");
        edit.addCategory = QStringLiteral("agent:AG-0001");
        const auto result = ICalPatch::create(QStringLiteral("uid-new-1"), edit);
        QVERIFY2(result.ok, qPrintable(result.error));
        QVERIFY(result.text.startsWith(QStringLiteral("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Nourax//NoAgenda//EN\r\n")));
        QVERIFY(result.text.endsWith(QStringLiteral("END:VEVENT\r\nEND:VCALENDAR\r\n")));
        const auto components = ICalText::parse(result.text);
        QCOMPARE(components.size(), 1);
        const auto &event = components.first();
        QCOMPARE(event.type, QStringLiteral("VEVENT"));
        QCOMPARE(event.text(QStringLiteral("UID")), QStringLiteral("uid-new-1"));
        QCOMPARE(event.text(QStringLiteral("SUMMARY")), QStringLiteral("Lunch, with Sara"));
        QCOMPARE(event.text(QStringLiteral("X-NOURAX-AGENT-ID")), QStringLiteral("AG-0001"));
        QVERIFY(event.has(QStringLiteral("DTSTAMP")));
        QVERIFY(event.has(QStringLiteral("DTEND"))); // one hour by default
        QCOMPARE(event.first(QStringLiteral("DTEND"))->raw, QStringLiteral("20261106T120000Z"));
        QCOMPARE(event.first(QStringLiteral("SEQUENCE"))->raw, QStringLiteral("0"));
    }

    void aNewAllDayEventNeedsNoZone()
    {
        auto edit = baseEdit();
        edit.summary = QStringLiteral("Conference");
        edit.start = ICalDateValue::ofDate(QDate(2026, 11, 9));
        const auto result = ICalPatch::create(QStringLiteral("uid-new-2"), edit);
        QVERIFY(result.ok);
        QVERIFY(result.text.contains(QStringLiteral("DTSTART;VALUE=DATE:20261109\r\n")));
        QVERIFY(result.text.contains(QStringLiteral("DTEND;VALUE=DATE:20261110\r\n")));
        QVERIFY(!result.text.contains(QStringLiteral("TZID")));
        QVERIFY(!result.text.contains(QStringLiteral("X-NOURAX-AGENT-ID"))); // Self: no agent id and no tag
        QVERIFY(!result.text.contains(QStringLiteral("CATEGORIES")));
    }

    void aNewTodoMayHaveNoDates()
    {
        auto edit = baseEdit();
        edit.isTodo = true;
        edit.summary = QStringLiteral("Call the bank");
        const auto result = ICalPatch::create(QStringLiteral("uid-new-3"), edit);
        QVERIFY(result.ok);
        QVERIFY(result.text.contains(QStringLiteral("BEGIN:VTODO\r\n")));
        QVERIFY(result.text.contains(QStringLiteral("STATUS:NEEDS-ACTION\r\n")));
        QVERIFY(!result.text.contains(QStringLiteral("DUE")));
        const auto components = ICalText::parse(result.text);
        QCOMPARE(components.size(), 1);
        QCOMPARE(components.first().type, QStringLiteral("VTODO"));
    }

    void aNewEventWithoutAStartIsRefused()
    {
        auto edit = baseEdit();
        edit.summary = QStringLiteral("x");
        QVERIFY(!ICalPatch::create(QStringLiteral("u"), edit).ok);
    }
};

QTEST_GUILESS_MAIN(TestNoDriveAgendaICalPatch)
#include "testnodriveagendaicalpatch.moc"
