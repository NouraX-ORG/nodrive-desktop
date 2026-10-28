/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: the rules - setting defaults, event rules, agents, integrity flags, dedupe, problems of a team,
// the organisation privacy and one account end to end (refresh, cache, offline, back-off).

#include "nodrive/noagenda/agendatestsupport.h"

#include <QJsonArray>
#include <QTemporaryDir>

using namespace AgendaTest;

class TestNoDriveAgendaRules : public QObject
{
    Q_OBJECT

    static QString modeOf(const BuildOutput &out, const QString &key)
    {
        for (const auto &s : out.sources) {
            if (s.key == key && s.type == SourceEntry::Type::Calendar) {
                return s.mode;
            }
        }
        return QStringLiteral("(none)");
    }
    static int teamProblems(const QList<AgendaItem> &items)
    {
        int n = 0;
        for (const auto &i : items) {
            n += (i.workKind == WorkKind::Team && !i.problems.isEmpty()) ? 1 : 0;
        }
        return n;
    }

private Q_SLOTS:
    // ---- defaults (design 6.1) ----------------------------------------------------------------------------------
    void calendarDefaults()
    {
        auto input = loadScenario(QStringLiteral("ali"));
        input.gantt.taskSync = fixtureJson(QStringLiteral("gantt/task-sync-on.json")).toObject();
        const auto out = AgendaBuilder::build(input);
        QCOMPARE(modeOf(out, QStringLiteral("personal")), QStringLiteral("personal"));
        QCOMPARE(modeOf(out, QStringLiteral("work")), QStringLiteral("personal")); // own calendars are Personal until the question
        QCOMPARE(modeOf(out, QStringLiteral("mara-team_shared_by_mara")), QStringLiteral("team"));
        QCOMPARE(modeOf(out, QStringLiteral("holidays")), QStringLiteral("info"));
        QCOMPARE(modeOf(out, QStringLiteral("contact_birthdays")), QStringLiteral("info"));
        QCOMPARE(modeOf(out, QStringLiteral("app-generated--deck--board-10")), QStringLiteral("hidden"));
        // NoGantt's own copy of my tasks is hidden automatically, and the person is told.
        QCOMPARE(modeOf(out, QStringLiteral("ali-task-copy")), QStringLiteral("hidden"));
        QCOMPARE(out.taskCopyCalendarUri, QStringLiteral("ali-task-copy"));
        for (const auto &s : out.sources) {
            if (s.key == QLatin1String("ali-task-copy") || s.key.startsWith(QLatin1String("app-generated--"))) {
                QVERIFY(s.autoHidden);
                QVERIFY(!s.autoHiddenReason.isEmpty());
            }
        }
    }

    void theOneTimeQuestionListsMyOwnCalendars()
    {
        const auto out = AgendaBuilder::build(loadScenario(QStringLiteral("ali")));
        QVERIFY(out.needsPersonalQuestion);
        QStringList uris;
        for (const auto &c : out.ownCalendars) {
            uris.append(c.first);
        }
        QCOMPARE(uris, (QStringList{QStringLiteral("personal"), QStringLiteral("work"), QStringLiteral("ali-task-copy")}));
    }

    void answeringTheQuestionMakesTheOthersWork()
    {
        const auto out = AgendaBuilder::build(loadAliAnswered());
        QVERIFY(!out.needsPersonalQuestion);
        QCOMPARE(modeOf(out, QStringLiteral("personal")), QStringLiteral("personal"));
        QCOMPARE(modeOf(out, QStringLiteral("work")), QStringLiteral("work"));
        QCOMPARE(find(out.items, QStringLiteral("cal:work/call.ics"))->category, Category::Work);
        QCOMPARE(find(out.items, QStringLiteral("cal:personal/dentist.ics"))->category, Category::Personal);
    }

    void skippingKeepsEverythingPersonalAndIsNeverAskedAgain()
    {
        auto input = loadScenario(QStringLiteral("ali"));
        input.settings.answerPersonalQuestion({}, {QStringLiteral("personal"), QStringLiteral("work")}, true);
        const auto out = AgendaBuilder::build(input);
        QVERIFY(!out.needsPersonalQuestion);
        QCOMPARE(modeOf(out, QStringLiteral("work")), QStringLiteral("personal"));
        // The answer survives the settings file.
        const auto copy = SourceSettings::fromJson(input.settings.toJson());
        QVERIFY(copy.personalAnswered());
    }

    void projectAndBoardDefaults()
    {
        QSet<int> mine{1};
        const auto project = [](int id, const QString &owner, const QString &permission) {
            return QJsonObject{{"id", id}, {"title", "P"}, {"color", "#e11d48"}, {"owner_id", owner}, {"permission", permission}};
        };
        const QStringList groups{QStringLiteral("org-a")};
        // I own it: Do + Oversee.
        QCOMPARE(ProjectRelation::of(project(1, QStringLiteral("ali"), QStringLiteral("admin")), QStringLiteral("ali"), false, groups, {}, {}, false).defaultMode(), WorkMode::DoOversee);
        // Admin through a real share: Do + Oversee.
        QCOMPARE(ProjectRelation::of(project(2, QStringLiteral("mara"), QStringLiteral("admin")), QStringLiteral("ali"), false, groups, {}, {}, false).defaultMode(), WorkMode::DoOversee);
        // Editor or viewer with tasks of mine: Do. Without: Hidden.
        QCOMPARE(ProjectRelation::of(project(1, QStringLiteral("mara"), QStringLiteral("editor")), QStringLiteral("ali"), false, groups, mine, {}, false).defaultMode(), WorkMode::Do);
        QCOMPARE(ProjectRelation::of(project(3, QStringLiteral("mara"), QStringLiteral("viewer")), QStringLiteral("ali"), false, groups, mine, {}, false).defaultMode(), WorkMode::Hidden);
    }

    void aServerAdminSeesEveryProjectAsAdminButThatIsNotAShare()
    {
        const auto project = QJsonObject{{"id", 7}, {"title", "Other"}, {"owner_id", "mara"}, {"permission", "admin"}, {"color", "#e11d48"}};
        const QStringList groups{QStringLiteral("admin"), QStringLiteral("org-a")};
        // No share to me: seen only as server admin, Hidden (never guessed).
        auto relation = ProjectRelation::of(project, QStringLiteral("ali"), true, groups, {}, QJsonArray(), true);
        QVERIFY(relation.seenOnlyAsServerAdmin);
        QVERIFY(!relation.runByRealShare);
        QCOMPARE(relation.defaultMode(), WorkMode::Hidden);
        // An admin share to me counts.
        const QJsonArray userShare{QJsonObject{{"principal_type", "user"}, {"principal_id", "ali"}, {"permission", "admin"}}};
        QCOMPARE(ProjectRelation::of(project, QStringLiteral("ali"), true, groups, {}, userShare, true).defaultMode(), WorkMode::DoOversee);
        // A group share to a group of mine counts, to another group it does not.
        const QJsonArray groupShare{QJsonObject{{"principal_type", "group"}, {"principal_id", "org-a"}, {"permission", "admin"}}};
        QCOMPARE(ProjectRelation::of(project, QStringLiteral("ali"), true, groups, {}, groupShare, true).defaultMode(), WorkMode::DoOversee);
        const QJsonArray otherGroup{QJsonObject{{"principal_type", "group"}, {"principal_id", "org-b"}, {"permission", "admin"}}};
        QCOMPARE(ProjectRelation::of(project, QStringLiteral("ali"), true, groups, {}, otherGroup, true).defaultMode(), WorkMode::Hidden);
        // A team share cannot be resolved: it counts as not mine.
        const QJsonArray teamShare{QJsonObject{{"principal_type", "team"}, {"principal_id", "t1"}, {"permission", "admin"}}};
        QCOMPARE(ProjectRelation::of(project, QStringLiteral("ali"), true, groups, {}, teamShare, true).defaultMode(), WorkMode::Hidden);
    }

    void theStaffAndTheManagerSeeTheirOwnProjects()
    {
        {
            const auto out = AgendaBuilder::build(loadScenario(QStringLiteral("ali")));
            QString website;
            QString internal;
            for (const auto &s : out.sources) {
                if (s.type == SourceEntry::Type::Project) {
                    (s.name == QLatin1String("Website") ? website : internal) = s.mode;
                }
            }
            QCOMPARE(website, QStringLiteral("do"));
            QCOMPARE(internal, QStringLiteral("hidden"));
            QVERIFY(!out.anyOversee);
        }
        {
            const auto out = AgendaBuilder::build(loadScenario(QStringLiteral("mara")));
            int oversee = 0;
            for (const auto &s : out.sources) {
                oversee += (s.type != SourceEntry::Type::Calendar && s.mode == QLatin1String("doOversee")) ? 1 : 0;
            }
            QCOMPARE(oversee, 3); // two projects and the board
            QVERIFY(out.anyOversee);
        }
    }

    void aChosenSettingBeatsTheDefault()
    {
        auto input = loadAliAnswered();
        input.settings.setProjectMode(1, WorkMode::Hidden);
        input.settings.setBoardMode(10, WorkMode::Hidden);
        input.settings.setCalendarMode(QStringLiteral("holidays"), CalendarMode::Hidden);
        const auto out = AgendaBuilder::build(input);
        for (const auto &i : out.items) {
            QVERIFY2(i.home != Home::Gantt && i.home != Home::Board, qPrintable(i.key));
        }
        QVERIFY(!findTitle(out.items, QStringLiteral("Local holiday")));
        QVERIFY(findTitle(out.items, QStringLiteral("Dentist")));
    }

    void theSettingsFileKeepsEveryChoice()
    {
        SourceSettings s;
        s.setCalendarMode(QStringLiteral("work"), CalendarMode::Work);
        s.setProjectMode(4, WorkMode::DoOversee);
        s.setBoardMode(9, WorkMode::Hidden);
        WorkingHours hours;
        hours.daysMask = 3;
        hours.from = QTime(10, 0);
        hours.to = QTime(14, 0);
        s.setWorkingHours(hours);
        s.lastView = QStringLiteral("week");
        s.chipAgents = false;
        s.everOpened = true;
        const auto copy = SourceSettings::fromJson(s.toJson());
        CalendarInfo work;
        work.uri = QStringLiteral("work");
        work.mine = true;
        QCOMPARE(copy.calendarMode(work, QString()), CalendarMode::Work);
        ProjectRelation relation;
        relation.id = 4;
        QCOMPARE(copy.projectMode(relation), WorkMode::DoOversee);
        BoardRelation board;
        board.id = 9;
        QCOMPARE(copy.boardMode(board), WorkMode::Hidden);
        QCOMPARE(copy.workingHours().daysMask, 3);
        QCOMPARE(copy.workingHours().from, QTime(10, 0));
        QCOMPARE(copy.lastView, QStringLiteral("week"));
        QVERIFY(!copy.chipAgents && copy.chipMe && copy.chipTeam);
        QVERIFY(copy.everOpened);
    }

    // ---- event rules (design 6.1) ---------------------------------------------------------------------------------------
    void eventRules()
    {
        const auto out = AgendaBuilder::build(loadAliAnswered());
        // free events do not block time
        QVERIFY(find(out.items, QStringLiteral("cal:work/free.ics"))->transparent);
        QVERIFY(!find(out.items, QStringLiteral("cal:work/call.ics"))->transparent);
        // declined invitations are hidden
        QVERIFY(!findTitle(out.items, QStringLiteral("Budget talk")));
        // tentative ones are soft
        QVERIFY(find(out.items, QStringLiteral("cal:mara-team_shared_by_mara/t2.ics"))->tentative);
        // a shared team calendar: only what I organise or am invited to
        QVERIFY(!findTitle(out.items, QStringLiteral("Mara and Olaf")));
        QVERIFY(findTitle(out.items, QStringLiteral("Ali's workshop")));
        QVERIFY(find(out.items, QStringLiteral("cal:mara-team_shared_by_mara/t4.ics"))->organizerIsMe);
        // an all-day event keeps its last day
        const auto *dayOff = find(out.items, QStringLiteral("cal:work/dayoff.ics"));
        QVERIFY(dayOff->allDay);
        QCOMPARE(dayOff->startDate, QDate(2026, 10, 30));
        QCOMPARE(dayOff->endDate, QDate(2026, 10, 30));
        // repeating
        QVERIFY(find(out.items, QStringLiteral("cal:work/standup.ics#20261028T093000Z"))->repeating);
        // information calendars are shown but never block time
        const auto *holiday = findTitle(out.items, QStringLiteral("Local holiday"));
        QVERIFY(holiday->info && holiday->transparent);
        QCOMPARE(holiday->category, Category::Info);
        // the times are kept in UTC
        QCOMPARE(find(out.items, QStringLiteral("cal:personal/dentist.ics"))->start, QDateTime(QDate(2026, 10, 28), QTime(10, 0), QTimeZone::UTC));
        QCOMPARE(find(out.items, QStringLiteral("cal:personal/dentist.ics"))->description, QStringLiteral("Check-up, bring the card"));
    }

    void everyHomeGivesItsRows()
    {
        const auto out = AgendaBuilder::build(loadAliAnswered());
        QCOMPARE(out.items.size(), 32); // 19 calendar and to-do rows, 9 NoGantt, 4 NoBoard
        int events = 0;
        int todos = 0;
        int tasks = 0;
        int milestones = 0;
        int cards = 0;
        for (const auto &i : out.items) {
            events += i.kind == ItemKind::Event ? 1 : 0;
            todos += i.kind == ItemKind::Todo ? 1 : 0;
            tasks += i.kind == ItemKind::GanttTask ? 1 : 0;
            milestones += i.kind == ItemKind::Milestone ? 1 : 0;
            cards += i.kind == ItemKind::Card ? 1 : 0;
        }
        QCOMPARE(events, 14);
        QCOMPARE(todos, 5);
        QCOMPARE(tasks, 8);
        QCOMPARE(milestones, 1);
        QCOMPARE(cards, 4);
    }

    void todos()
    {
        const auto out = AgendaBuilder::build(loadAliAnswered());
        QCOMPARE(find(out.items, QStringLiteral("cal:personal/td1.ics"))->due, QDate(2026, 10, 30));
        const auto *late = find(out.items, QStringLiteral("cal:personal/td2.ics"));
        QVERIFY(late->late);
        QCOMPARE(late->lateDays, 2);
        QVERIFY(!find(out.items, QStringLiteral("cal:personal/td3.ics"))->hasDate()); // goes to "No date"
        const auto *draft = find(out.items, QStringLiteral("cal:personal/td4.ics"));
        QCOMPARE(draft->startDate, QDate(2026, 10, 27));
        QCOMPARE(draft->due, QDate(2026, 10, 31));
        QCOMPARE(draft->percentDone, 30);
        QVERIFY(!find(out.items, QStringLiteral("cal:personal/td5.ics"))); // completed
        const auto *callBack = find(out.items, QStringLiteral("cal:personal/td6.ics"));
        QVERIFY(callBack->dueToday);
        QCOMPARE(callBack->home, Home::Tasks);
    }

    // ---- agents (design 4.2) ---------------------------------------------------------------------------------------------
    void agentsOnEventsAndTodos()
    {
        const auto out = AgendaBuilder::build(loadAliAnswered());
        QCOMPARE(find(out.items, QStringLiteral("cal:work/agent.ics"))->agent.displayText(), QStringLiteral("Writer-Bot"));
        QCOMPARE(find(out.items, QStringLiteral("cal:work/agent.ics"))->workKind, WorkKind::MyAgent);
        // the category alone is enough when a client dropped the property
        QCOMPARE(find(out.items, QStringLiteral("cal:work/tagonly.ics"))->agent.displayText(), QStringLiteral("Writer-Bot"));
        QCOMPARE(find(out.items, QStringLiteral("cal:personal/td4.ics"))->agent.displayText(), QStringLiteral("Writer-Bot"));
        // an id that is not in the registry is shown as such and flagged, never guessed
        const auto *unknown = find(out.items, QStringLiteral("cal:work/unknownagent.ics"));
        QCOMPARE(unknown->agent.displayText(), QStringLiteral("AG-0999 (unknown agent)"));
        QVERIFY(hasProblem(*unknown, ProblemFlag::Kind::UnknownAgent));
        // a retired agent
        const auto *retired = find(out.items, QStringLiteral("cal:work/retiredagent.ics"));
        QCOMPARE(retired->agent.displayText(), QStringLiteral("Invoice-Bot (retired)"));
        QVERIFY(hasProblem(*retired, ProblemFlag::Kind::RetiredAgent));
        // a calendar event without an agent is Self
        QVERIFY(find(out.items, QStringLiteral("cal:work/call.ics"))->agent.isSelf);
    }

    void aMeetingSomeoneElseOrganisesIsAlwaysSelf()
    {
        const auto out = AgendaBuilder::build(loadAliAnswered());
        const auto *invite = find(out.items, QStringLiteral("cal:work/invite.ics"));
        QVERIFY(invite);
        QVERIFY(invite->invitation);
        QVERIFY(invite->agent.isSelf); // the tag in the file is ignored
        QCOMPARE(invite->workKind, WorkKind::Self);
    }

    void theSameInvitationInTwoCalendarsIsOneRow()
    {
        const auto out = AgendaBuilder::build(loadAliAnswered());
        int count = 0;
        for (const auto &i : out.items) {
            count += i.title == QLatin1String("Planning meeting") ? 1 : 0;
        }
        QCOMPARE(count, 1);
        QVERIFY(find(out.items, QStringLiteral("cal:work/invite.ics"))); // the copy in my own calendar wins
    }

    void agentsWithoutARegistryAreNotGuessed()
    {
        auto input = loadAliAnswered();
        input.registry = AgentRegistry::missing();
        const auto out = AgendaBuilder::build(input);
        const auto *item = find(out.items, QStringLiteral("cal:work/agent.ics"));
        QVERIFY(item->agent.unknown);
        QCOMPARE(item->agent.displayText(), QStringLiteral("AG-0001 (unknown agent)"));
    }

    void anAgentOfAnotherPersonOnMyOwnItemIsNotMine()
    {
        auto input = loadAliAnswered();
        // AG-0002 (Design-Bot) belongs to Sara.
        QJsonArray none;
        auto records = input.calendar.records;
        for (auto &r : records) {
            if (r.href.endsWith(QLatin1String("agent.ics"))) {
                r.agentId = QStringLiteral("AG-0002");
            }
        }
        input.calendar.records = records;
        const auto built_item = AgendaBuilder::build(input);
        const auto *item = find(built_item.items, QStringLiteral("cal:work/agent.ics"));
        QVERIFY(item->agent.unknown);
        QVERIFY(hasProblem(*item, ProblemFlag::Kind::UnknownAgent));
    }

    // ---- integrity (design 4.4, 4.5) -------------------------------------------------------------------------------------
    void theManagerSeesEveryIntegrityProblem()
    {
        const auto out = AgendaBuilder::build(loadScenario(QStringLiteral("mara")));
        // agent with no person (and no person on the task)
        const auto *orphan = find(out.items, QStringLiteral("gantt:task:106"));
        QVERIFY(orphan);
        QVERIFY(hasProblem(*orphan, ProblemFlag::Kind::AgentNoPerson));
        QVERIFY(hasProblem(*orphan, ProblemFlag::Kind::NotAssigned));
        // agent paired with someone who is not its person
        const auto *wrong = find(out.items, QStringLiteral("gantt:task:107:ali"));
        QVERIFY(wrong);
        QVERIFY(hasProblem(*wrong, ProblemFlag::Kind::AgentWrongPerson));
        bool sentence = false;
        for (const auto &p : wrong->problems) {
            sentence = sentence || p.text == QLatin1String("Design-Bot belongs to Sara, not Ali");
        }
        QVERIFY(sentence);
        // two agents for the same person
        QVERIFY(hasProblem(*find(out.items, QStringLiteral("gantt:task:108:ali")), ProblemFlag::Kind::TwoAgents));
        // a planning person that is not linked to an account
        QVERIFY(hasProblem(*find(out.items, QStringLiteral("gantt:task:109:sara")), ProblemFlag::Kind::NotLinked));
        // a task assigned to a role only
        const auto *role = find(out.items, QStringLiteral("gantt:task:105"));
        QVERIFY(hasProblem(*role, ProblemFlag::Kind::NotAssigned));
        QVERIFY(role->personName.isEmpty());
        // a person in a project that is not shared with them
        const auto *shared = find(out.items, QStringLiteral("gantt:notshared:2:sara"));
        QVERIFY(shared);
        QCOMPARE(shared->title, QStringLiteral("Sara has 1 task in a project not shared with Sara"));
        QVERIFY(!find(out.items, QStringLiteral("gantt:notshared:1:ali"))); // shared in the other project
    }

    void theIntegrityAudiencesAreRespected()
    {
        // Ali does not run the project: the manager's flags are not shown to him...
        const auto aliOut = AgendaBuilder::build(loadScenario(QStringLiteral("ali")));
        QVERIFY(find(aliOut.items, QStringLiteral("gantt:task:107"))->problems.isEmpty()); // "belongs to Sara, not Ali" is for the manager
        QVERIFY(!find(aliOut.items, QStringLiteral("gantt:task:106")));
        // ...but his own problems are: two agents, and an unknown agent.
        QVERIFY(hasProblem(*find(aliOut.items, QStringLiteral("gantt:task:108")), ProblemFlag::Kind::TwoAgents));
        QVERIFY(hasProblem(*find(aliOut.items, QStringLiteral("gantt:task:113")), ProblemFlag::Kind::UnknownAgent));
        // The manager does not get "unknown agent": it is for the person.
        const auto maraOut = AgendaBuilder::build(loadScenario(QStringLiteral("mara")));
        QVERIFY(!hasProblem(*find(maraOut.items, QStringLiteral("gantt:task:113:ali")), ProblemFlag::Kind::UnknownAgent));
    }

    // ---- what a manager runs (design 6.3) ---------------------------------------------------------------------------------
    void problemsOfAManagersTeam()
    {
        const auto out = AgendaBuilder::build(loadScenario(QStringLiteral("mara")));
        QCOMPARE(out.items.size(), 21);
        QCOMPARE(teamProblems(out.items), 13);
        for (const auto &i : out.items) {
            QCOMPARE(i.workKind, WorkKind::Team); // Mara has no work of her own in the fixtures
        }
        // late, blocked, due today
        const auto *late = find(out.items, QStringLiteral("gantt:task:103:sara"));
        QVERIFY(hasProblem(*late, ProblemFlag::Kind::Late));
        QCOMPARE(late->problems.first().text, QStringLiteral("2 days late"));
        const auto *blocked = find(out.items, QStringLiteral("gantt:task:104:ali"));
        QVERIFY(hasProblem(*blocked, ProblemFlag::Kind::Blocked));
        QVERIFY(blocked->problems.first().text.contains(QStringLiteral("Banner design")));
        QVERIFY(hasProblem(*find(out.items, QStringLiteral("gantt:task:112:ali")), ProblemFlag::Kind::DueToday));
        // someone overbooked (NoGantt's capacity check)
        const auto *overbooked = find(out.items, QStringLiteral("gantt:capacity:1"));
        QVERIFY(overbooked);
        QCOMPARE(overbooked->title, QStringLiteral("Ali is overbooked"));
        // rows without a problem are still there for "Show all team tasks"
        QVERIFY(find(out.items, QStringLiteral("gantt:task:101:ali"))->problems.isEmpty());
        QVERIFY(find(out.items, QStringLiteral("gantt:task:101:ali"))->agent.displayText() == QLatin1String("Writer-Bot"));
        // a finished task is not a team row
        QVERIFY(!find(out.items, QStringLiteral("gantt:task:110:ali")));
    }

    void blockedAndLateOnMyOwnRows()
    {
        const auto out = AgendaBuilder::build(loadScenario(QStringLiteral("ali")));
        const auto *blocked = find(out.items, QStringLiteral("gantt:task:104"));
        QVERIFY(blocked->blocked);
        QCOMPARE(blocked->blockedBy, QStringLiteral("Banner design"));
        QVERIFY(find(out.items, QStringLiteral("gantt:task:112"))->dueToday);
        QVERIFY(!find(out.items, QStringLiteral("gantt:task:102"))->late);
    }

    void aGanttTaskWithABoardCardIsOneRowWithABadge()
    {
        const auto out = AgendaBuilder::build(loadScenario(QStringLiteral("ali")));
        const auto *task = find(out.items, QStringLiteral("gantt:task:111"));
        QVERIFY(task->boardBadge);
        QCOMPARE(task->deckCardId, 501);
        QVERIFY(!find(out.items, QStringLiteral("deck:card:501")));
    }

    void aViewerCannotChangeEvenHisOwnTask()
    {
        auto input = loadScenario(QStringLiteral("ali"));
        QJsonArray projects;
        for (const auto &p : std::as_const(input.gantt.projects)) {
            auto o = p.toObject();
            if (o.value(QLatin1String("id")).toInt() == 1) {
                o.insert(QStringLiteral("permission"), QStringLiteral("viewer"));
            }
            projects.append(o);
        }
        input.gantt.projects = projects;
        const auto built_task = AgendaBuilder::build(input);
        const auto *task = find(built_task.items, QStringLiteral("gantt:task:102"));
        QVERIFY(task);
        QVERIFY(task->readOnly);
        QVERIFY(!task->readOnlyReason.isEmpty());
    }

    // ---- privacy between organisations (design 4.1) ----------------------------------------------------------------------------
    void anAgentOfAnotherOrganisationIsNotNamed()
    {
        const auto registry = AgentRegistry::parse(fixture(QStringLiteral("agent-registry.json")));
        BuildContext ctx;
        ctx.registry = registry;
        ctx.me = QStringLiteral("mara"); // her agent is of demo-org
        QCOMPARE(ctx.refFor(*registry.byId(QStringLiteral("AG-0002")), QStringLiteral("Sara")).displayText(), QStringLiteral("Design-Bot"));
        const auto hidden = ctx.refFor(*registry.byId(QStringLiteral("AG-0005")), QStringLiteral("Olaf"));
        QVERIFY(hidden.otherOrganisation);
        QCOMPARE(hidden.displayText(), QStringLiteral("Agent of Olaf"));
        ctx.me = QStringLiteral("olaf");
        QCOMPARE(ctx.refFor(*registry.byId(QStringLiteral("AG-0005")), QStringLiteral("Olaf")).displayText(), QStringLiteral("Olaf-Bot"));
    }

    // ---- the working pattern ------------------------------------------------------------------------------------------------------------
    void theWorkingPatternComesFromNoGanttOrFromTheDefault()
    {
        auto input = loadScenario(QStringLiteral("ali"));
        auto hours = AgendaBuilder::workingHours(input);
        QCOMPARE(hours.daysMask, 31);
        QCOMPARE(hours.from, QTime(9, 0));
        QVERIFY(hours.isWorkingDay(QDate(2026, 10, 28)));
        QVERIFY(!hours.isWorkingDay(QDate(2026, 10, 31)));
        QCOMPARE(hours.hoursPerDay(), 8.0);

        BuildInput empty;
        empty.davUser = QStringLiteral("ali");
        hours = AgendaBuilder::workingHours(empty);
        QCOMPARE(hours.daysMask, 31); // Monday to Friday, 09:00 - 17:00

        WorkingHours own;
        own.daysMask = 3;
        own.from = QTime(10, 0);
        own.to = QTime(14, 0);
        input.settings.setWorkingHours(own);
        QCOMPARE(AgendaBuilder::workingHours(input).hoursPerDay(), 4.0);
    }

    // ---- one account end to end ------------------------------------------------------------------------------------------------------------
    void anAccountReadsSavesAndComesBackOffline()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        auto fake = std::make_unique<FakeTransport>();
        auto *transport = fake.get();
        routeScenario(transport, QStringLiteral("ali"));
        AccountAgenda agenda(std::move(fake), dir.path() + QStringLiteral("/noagenda/test"));
        agenda.setClock([] { return fixedNow(); });
        agenda.setZone(QTimeZone(QTimeZone::UTC));
        agenda.loadFromCache();
        QVERIFY(agenda.state().neverLoaded);
        agenda.refresh(true);
        QVERIFY(!agenda.isRefreshing());
        const auto &state = agenda.state();
        QCOMPARE(state.items.size(), 32);
        QCOMPARE(state.personName, QStringLiteral("Ali"));
        QVERIFY(state.build.needsPersonalQuestion);
        for (const auto &s : state.statuses) {
            QCOMPARE(s.state, SourceStatus::State::Fresh);
        }
        QCOMPARE(state.registryStatus, AgentRegistry::Status::Ok);
        QVERIFY(QFile::exists(dir.path() + QStringLiteral("/noagenda/test/gantt.json")));

        // Another start of the app: everything is there before the first request.
        auto offlineFake = std::make_unique<FakeTransport>();
        AccountAgenda second(std::move(offlineFake), dir.path() + QStringLiteral("/noagenda/test"));
        second.setClock([] { return fixedNow().addSecs(600); });
        second.setZone(QTimeZone(QTimeZone::UTC));
        second.loadFromCache();
        QCOMPARE(second.state().items.size(), 32);
        QVERIFY(!second.state().neverLoaded);
        for (const auto &s : second.state().statuses) {
            QCOMPARE(s.state, SourceStatus::State::Saved); // read from the cache, no attempt yet: no banner
            QVERIFY(s.savedAt.isValid());
        }

        // The server is gone: the last data stays, marked with its time.
        transport->offline = true;
        agenda.refresh(true);
        QCOMPARE(agenda.state().items.size(), 32);
        QCOMPARE(agenda.state().status(QStringLiteral("gantt"))->state, SourceStatus::State::Cached);
        QCOMPARE(agenda.state().status(QStringLiteral("gantt"))->savedAt, fixedNow());
    }

    void aChoiceIsSavedAndRebuildsWithoutANewRequest()
    {
        QTemporaryDir dir;
        auto fake = std::make_unique<FakeTransport>();
        auto *transport = fake.get();
        routeScenario(transport, QStringLiteral("ali"));
        AccountAgenda agenda(std::move(fake), dir.path() + QStringLiteral("/noagenda/test"));
        agenda.setClock([] { return fixedNow(); });
        agenda.setZone(QTimeZone(QTimeZone::UTC));
        agenda.refresh(true);
        const auto requests = transport->requests.size();
        agenda.answerPersonalQuestion({QStringLiteral("personal")}, false);
        QVERIFY(!agenda.state().build.needsPersonalQuestion);
        QCOMPARE(find(agenda.state().items, QStringLiteral("cal:work/call.ics"))->category, Category::Work);
        QCOMPARE(transport->requests.size(), requests);
        // And it is still there after a restart.
        AccountAgenda again(std::make_unique<FakeTransport>(), dir.path() + QStringLiteral("/noagenda/test"));
        again.setClock([] { return fixedNow(); });
        again.setZone(QTimeZone(QTimeZone::UTC));
        again.loadFromCache();
        QCOMPARE(find(again.state().items, QStringLiteral("cal:work/call.ics"))->category, Category::Work);
        QVERIFY(!again.state().build.needsPersonalQuestion);
    }

    void aFailingSourceBacksOffAndAManualRefreshIgnoresIt()
    {
        QCOMPARE(AccountAgenda::backoffMinutes(0), 0);
        QCOMPARE(AccountAgenda::backoffMinutes(1), 5);
        QCOMPARE(AccountAgenda::backoffMinutes(2), 10);
        QCOMPARE(AccountAgenda::backoffMinutes(3), 20);
        QCOMPARE(AccountAgenda::backoffMinutes(4), 30);
        QCOMPARE(AccountAgenda::backoffMinutes(9), 30);

        QTemporaryDir dir;
        auto fake = std::make_unique<FakeTransport>();
        auto *transport = fake.get();
        routeScenario(transport, QStringLiteral("ali"));
        AccountAgenda agenda(std::move(fake), dir.path() + QStringLiteral("/noagenda/test"));
        QDateTime now = fixedNow();
        agenda.setClock([&now] { return now; });
        agenda.setZone(QTimeZone(QTimeZone::UTC));
        agenda.refresh(true);
        // NoGantt starts to answer 500.
        for (auto &a : transport->answers) {
            if (a.path.contains(QStringLiteral("/nc_gantt/"))) {
                a.status = 500;
            }
        }
        now = now.addSecs(120);
        agenda.refresh(false);
        QCOMPARE(agenda.state().status(QStringLiteral("gantt"))->state, SourceStatus::State::Cached);
        QCOMPARE(agenda.state().items.size(), 32); // the old rows stay
        const auto after = transport->count("GET", QStringLiteral("/ocs/v2.php/apps/nc_gantt/api/v1/my-tasks"));
        // Two minutes later: in back-off, no new request to NoGantt.
        now = now.addSecs(120);
        agenda.refresh(false);
        QTRY_VERIFY(!agenda.isRefreshing());
        QCOMPARE(transport->count("GET", QStringLiteral("/ocs/v2.php/apps/nc_gantt/api/v1/my-tasks")), after);
        // A manual refresh asks again.
        agenda.refresh(true);
        QCOMPARE(transport->count("GET", QStringLiteral("/ocs/v2.php/apps/nc_gantt/api/v1/my-tasks")), after + 1);
        // The other sources were never held back.
        QCOMPARE(agenda.state().status(QStringLiteral("calendar"))->state, SourceStatus::State::Fresh);
    }

    void theOpenInTargetsAreOnTheAccountsOwnServer()
    {
        QTemporaryDir dir;
        auto fake = std::make_unique<FakeTransport>();
        routeScenario(fake.get(), QStringLiteral("ali"));
        AccountAgenda agenda(std::move(fake), dir.path() + QStringLiteral("/noagenda/test"));
        agenda.setClock([] { return fixedNow(); });
        agenda.setZone(QTimeZone(QTimeZone::UTC));
        agenda.refresh(true);
        const auto &items = agenda.state().items;
        auto url = [&](const QString &key) { return agenda.openUrlFor(*find(items, key)); };
        QCOMPARE(url(QStringLiteral("gantt:task:101")).toString(), QStringLiteral("https://drive.example.test/index.php/apps/nc_gantt/?project=1&task=101"));
        QCOMPARE(url(QStringLiteral("deck:card:502")).toString(), QStringLiteral("https://drive.example.test/index.php/apps/deck/card/502"));
        QVERIFY(url(QStringLiteral("cal:work/call.ics")).toString().startsWith(QStringLiteral("https://drive.example.test/index.php/apps/calendar/edit/")));
        const auto todo = url(QStringLiteral("cal:personal/td1.ics"));
        QCOMPARE(todo.path(), QStringLiteral("/index.php/apps/tasks/"));
        QCOMPARE(todo.fragment(), QStringLiteral("/calendars/personal/tasks/td1.ics"));
        QCOMPARE(AccountAgenda::openLabelFor(*find(items, QStringLiteral("gantt:task:101"))), QStringLiteral("Open in NoGantt"));
        QCOMPARE(AccountAgenda::openLabelFor(*find(items, QStringLiteral("deck:card:502"))), QStringLiteral("Open in NoBoard"));
        QCOMPARE(AccountAgenda::openLabelFor(*find(items, QStringLiteral("cal:work/call.ics"))), QStringLiteral("Open in NoCalendar"));
        QCOMPARE(AccountAgenda::openLabelFor(*find(items, QStringLiteral("cal:personal/td1.ics"))), QStringLiteral("Open in NoTasks"));
    }
};

QTEST_GUILESS_MAIN(TestNoDriveAgendaRules)
#include "testnodriveagendarules.moc"
