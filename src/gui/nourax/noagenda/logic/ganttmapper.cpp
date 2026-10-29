/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ganttmapper.h"

#include "../model/agendacolors.h"
#include "../ocs/ocsjson.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QMap>

namespace OCC::Nourax::Agenda::GanttMapper {

namespace {

struct Person
{
    QString uid;
    QString name;
    int resourceId = 0;
    double workHours = -1;
    QList<const Agent *> agents;
    double agentWorkHours = -1; ///< the work of the first agent's own planning person on the task (hours)
    QStringList unknownAgentLabels;
};

struct TaskFlag
{
    ProblemFlag flag;
    QString personUid; ///< empty = the whole task
    const Agent *agent = nullptr; ///< the registry agent the flag is about (when there is one)
};

QString robotPrefix()
{
    return QString::fromUcs4(U"\U0001F916");
}

QString personName(const QJsonObject &resource)
{
    const auto display = resource.value(QLatin1String("uid_display_name")).toString();
    return display.isEmpty() ? resource.value(QLatin1String("name")).toString() : display;
}

bool isLate(const QDate &end, const QString &status, const QDate &today)
{
    return end.isValid() && end < today && status != QLatin1String("done");
}

} // namespace

QString taskCopyCalendarUri(const GanttSnapshot &snapshot)
{
    if (!snapshot.taskSync.value(QLatin1String("enabled")).toBool()) {
        return QString();
    }
    return snapshot.taskSync.value(QLatin1String("calendar_uri")).toString();
}

double workHours(const QJsonObject &row, const QJsonObject &resource, double hoursPerPt)
{
    const auto work = row.value(QLatin1String("work"));
    if (work.isNull() || work.isUndefined()) {
        return -1;
    }
    const double value = numberOf(work, -1);
    if (value < 0) {
        return -1;
    }
    return resource.value(QLatin1String("unit")).toString() == QLatin1String("PT") ? value * hoursPerPt : value;
}

ProjectRelation relationOf(const BuildContext &ctx, const GanttSnapshot &snapshot, const QJsonObject &project)
{
    QSet<int> myProjects;
    for (const auto &t : snapshot.myTasks) {
        myProjects.insert(intOf(t.toObject().value(QLatin1String("project_id"))));
    }
    const int id = intOf(project.value(QLatin1String("id")));
    const auto data = snapshot.projectData.value(id);
    return ProjectRelation::of(project, ctx.me, ctx.serverAdmin, ctx.myGroups, myProjects, data.shares, data.sharesKnown);
}

void map(const BuildContext &ctx, const GanttSnapshot &snapshot, Result *out)
{
    if (!snapshot.loaded || snapshot.unavailable) {
        return;
    }
    QMap<int, QJsonObject> resources;
    for (const auto &r : snapshot.resources) {
        resources.insert(intOf(r.toObject().value(QLatin1String("id"))), r.toObject());
    }
    const double hoursPerPt = ctx.hoursPerPt;
    const bool readOnlyByGuard = !snapshot.contractOk;

    for (const auto &projectValue : snapshot.projects) {
        const auto project = projectValue.toObject();
        const int projectId = intOf(project.value(QLatin1String("id")));
        const auto data = snapshot.projectData.value(projectId);
        const auto relation = relationOf(ctx, snapshot, project);
        const auto mode = ctx.settings.projectMode(relation);
        if (mode == WorkMode::Hidden || !data.loaded) {
            continue;
        }
        const bool oversee = mode == WorkMode::DoOversee;
        const QString projectTitle = project.value(QLatin1String("title")).toString();
        const QString color = sourceColor(project.value(QLatin1String("color")).toString());
        const bool viewerOnly = project.value(QLatin1String("permission")).toString() == QLatin1String("viewer");

        QMap<int, QJsonObject> tasksById;
        for (const auto &t : data.tasks) {
            tasksById.insert(intOf(t.toObject().value(QLatin1String("id"))), t.toObject());
        }
        QMap<int, QString> phaseNames;
        for (const auto &sp : data.subprojects) {
            phaseNames.insert(intOf(sp.toObject().value(QLatin1String("id"))), sp.toObject().value(QLatin1String("title")).toString());
        }

        bool haveMyTask = false;
        QMap<QString, int> notSharedCounts; // uid -> tasks
        QMap<QString, QString> notSharedNames;

        for (const auto &taskValue : data.tasks) {
            const auto task = taskValue.toObject();
            const int taskId = intOf(task.value(QLatin1String("id")));
            const QString status = task.value(QLatin1String("status")).toString();
            const bool done = status == QLatin1String("done");
            const QDate start = dateOfText(task.value(QLatin1String("start_date")));
            const QDate end = dateOfText(task.value(QLatin1String("end_date")));

            // ---- who is on the task --------------------------------------------------------------
            QList<Person> persons;
            struct Candidate
            {
                int id = 0;
                QString name;
                double work = -1; ///< the work of this planning person on the task, in hours
            };
            QList<Candidate> candidates;
            int roleRows = 0;
            const auto rows = task.value(QLatin1String("resources")).toArray();
            for (const auto &rowValue : rows) {
                const auto row = rowValue.toObject();
                const int resourceId = intOf(row.value(QLatin1String("resource_id")));
                const auto resource = resources.value(resourceId);
                if (resource.isEmpty()) {
                    continue; // not visible to me
                }
                const auto type = resource.value(QLatin1String("type")).toString();
                const auto uid = resource.value(QLatin1String("uid")).toString();
                if (type == QLatin1String("role")) {
                    ++roleRows;
                } else if (type == QLatin1String("person") && !uid.isEmpty()) {
                    Person p;
                    p.uid = uid;
                    p.name = personName(resource);
                    p.resourceId = resourceId;
                    p.workHours = workHours(row, resource, hoursPerPt);
                    persons.append(p);
                } else if (type == QLatin1String("person")) {
                    candidates.append({resourceId, resource.value(QLatin1String("name")).toString(), workHours(row, resource, hoursPerPt)});
                }
            }

            auto namesOfUid = [&](const QString &uid) {
                QStringList names;
                for (auto it = resources.constBegin(); it != resources.constEnd(); ++it) {
                    if (it->value(QLatin1String("type")).toString() == QLatin1String("person")
                        && it->value(QLatin1String("uid")).toString().compare(uid, Qt::CaseInsensitive) == 0) {
                        names.append(it->value(QLatin1String("uid_display_name")).toString());
                        names.append(it->value(QLatin1String("name")).toString());
                    }
                }
                names.removeAll(QString());
                return names;
            };

            QList<TaskFlag> flags;
            // ---- agents (design 4.5) --------------------------------------------------------------
            for (const auto &cand : candidates) {
                const Agent *agent = ctx.registry.byResourceId(cand.id);
                int owner = -1;
                if (!agent) {
                    // Placeholder phase (design 4.5 b): the exact name "<robot> <agent name> (<person's display name>)" of
                    // an agent of the registry. The person's display name is read from the visible planning people.
                    for (const auto &candidateAgent : ctx.registry.agents()) {
                        for (const auto &ownerName : namesOfUid(candidateAgent.person)) {
                            if (AgentRegistry::normalize(cand.name) == AgentRegistry::normalize(AgentRegistry::planningPersonName(candidateAgent.name, ownerName))) {
                                agent = &candidateAgent;
                                break;
                            }
                        }
                        if (agent) {
                            break;
                        }
                    }
                }
                if (agent) {
                    if (persons.isEmpty()) {
                        flags.append({makeFlag(ProblemFlag::Kind::AgentNoPerson, ProblemFlag::Audience::Both, FlagText::agentNoPerson()), agent->person, agent});
                        continue;
                    }
                    if (owner < 0) {
                        for (int i = 0; i < persons.size(); ++i) {
                            if (persons.at(i).uid.compare(agent->person, Qt::CaseInsensitive) == 0) {
                                owner = i;
                                break;
                            }
                        }
                    }
                    if (owner < 0) {
                        // The owner's display name comes from the planning people I can see; when it is not
                        // there the sentence leaves it out (a user id is never written on screen).
                        QString ownerName;
                        if (ctx.isMe(agent->person)) {
                            ownerName = ctx.myName;
                        }
                        for (auto it = resources.constBegin(); ownerName.isEmpty() && it != resources.constEnd(); ++it) {
                            if (it->value(QLatin1String("uid")).toString().compare(agent->person, Qt::CaseInsensitive) == 0) {
                                ownerName = personName(it.value());
                            }
                        }
                        flags.append({makeFlag(ProblemFlag::Kind::AgentWrongPerson, ProblemFlag::Audience::Manager,
                                          FlagText::agentWrongPerson(ctx.agentNameInText(*agent), ownerName, persons.first().name)),
                            QString()});
                        continue;
                    }
                    persons[owner].agents.append(agent);
                    if (persons[owner].agents.size() == 1) {
                        persons[owner].agentWorkHours = cand.work;
                    }
                    if (agent->isRetired()) {
                        flags.append({makeFlag(ProblemFlag::Kind::RetiredAgent, ProblemFlag::Audience::Person, FlagText::retiredAgent(agent->name)), persons.at(owner).uid});
                    }
                } else if (cand.name.startsWith(robotPrefix())) {
                    // Looks like an agent but is not in the registry: never guessed.
                    const QString who = persons.size() == 1 ? persons.first().uid : QString();
                    if (persons.size() == 1) {
                        persons[0].unknownAgentLabels.append(cand.name);
                    }
                    flags.append({makeFlag(ProblemFlag::Kind::UnknownAgent, ProblemFlag::Audience::Person, FlagText::unknownAgent(cand.name)), who});
                } else {
                    flags.append({makeFlag(ProblemFlag::Kind::NotLinked, ProblemFlag::Audience::Manager, FlagText::notLinked()), QString()});
                }
            }
            for (const auto &p : std::as_const(persons)) {
                if (p.agents.size() > 1) {
                    flags.append({makeFlag(ProblemFlag::Kind::TwoAgents, ProblemFlag::Audience::Both, FlagText::twoAgents(p.name)), p.uid});
                }
            }
            if (persons.isEmpty() && !done) {
                flags.append({makeFlag(ProblemFlag::Kind::NotAssigned, ProblemFlag::Audience::Manager, FlagText::notAssigned()), QString()});
            }
            Q_UNUSED(roleRows)

            // ---- lateness and blocking ------------------------------------------------------------
            const bool late = isLate(end, status, ctx.today);
            const int lateDays = late ? static_cast<int>(end.daysTo(ctx.today)) : 0;
            const bool dueToday = end.isValid() && end == ctx.today && !done;
            bool blocked = false;
            QString blockedBy;
            const auto deps = task.value(QLatin1String("dependencies")).toArray();
            for (const auto &depValue : deps) {
                // NoGantt writes the predecessor as "t-<task id>" (or "m-<milestone id>").
                const auto predecessorText = depValue.toObject().value(QLatin1String("predecessor_id")).toString();
                const int predecessor = predecessorText.startsWith(QLatin1String("t-")) ? predecessorText.mid(2).toInt() : 0;
                const auto pred = tasksById.value(predecessor);
                if (!pred.isEmpty() && isLate(dateOfText(pred.value(QLatin1String("end_date"))), pred.value(QLatin1String("status")).toString(), ctx.today)) {
                    blocked = true;
                    blockedBy = pred.value(QLatin1String("title")).toString();
                    break;
                }
            }

            auto fillCommon = [&](AgendaItem &item) {
                item.home = Home::Gantt;
                item.kind = ItemKind::GanttTask;
                item.title = task.value(QLatin1String("title")).toString();
                item.startDate = start;
                item.endDate = end;
                item.due = end;
                timeSlotOfIso(task.value(QLatin1String("start_date")).toString(), task.value(QLatin1String("end_date")).toString(), ctx.zone, &item.slotStart, &item.slotEnd);
                item.sourceKey = QStringLiteral("gantt:") + QString::number(projectId);
                item.sourceName = projectTitle;
                item.sourceColor = color;
                item.category = Category::Work;
                item.status = status;
                item.done = done;
                item.percentDone = intOf(task.value(QLatin1String("manual_progress")), -1);
                item.late = late;
                item.lateDays = lateDays;
                item.dueToday = dueToday;
                item.blocked = blocked;
                item.blockedBy = blockedBy;
                item.projectId = projectId;
                item.projectName = projectTitle;
                item.taskId = taskId;
                item.version = task.value(QLatin1String("updated_at")).toString();
                item.progressMode = task.value(QLatin1String("progress_mode")).toString();
                item.versionReadOnly = readOnlyByGuard;
                item.description = task.value(QLatin1String("description")).toString().left(2000);
                const auto cardId = task.value(QLatin1String("deck_card_id"));
                if (cardId.isDouble() && intOf(cardId) > 0) {
                    item.deckCardId = intOf(cardId);
                    item.boardBadge = true;
                    out->linkedCardIds.insert(item.deckCardId);
                }
                if (viewerOnly) {
                    item.readOnly = true;
                    item.readOnlyReason = AgendaText::tr("You can see this project but not change it.");
                }
                const auto phase = phaseNames.value(intOf(task.value(QLatin1String("subproject_id"))));
                if (!phase.isEmpty()) {
                    item.sourceName = projectTitle + QStringLiteral(" / ") + phase;
                }
            };

            auto agentFor = [&](const Person &p) {
                if (!p.agents.isEmpty()) {
                    return ctx.refFor(*p.agents.first(), p.name);
                }
                if (!p.unknownAgentLabels.isEmpty()) {
                    return AgentRegistry::unknownRef(p.unknownAgentLabels.first());
                }
                return AgentRegistry::selfRef();
            };

            // ---- my row ------------------------------------------------------------------------------
            const Person *mine = nullptr;
            for (const auto &p : std::as_const(persons)) {
                if (ctx.isMe(p.uid)) {
                    mine = &p;
                    break;
                }
            }
            if (mine && !done) {
                haveMyTask = true;
                AgendaItem item;
                fillCommon(item);
                item.key = QStringLiteral("gantt:task:") + QString::number(taskId);
                item.agent = agentFor(*mine);
                item.workKind = item.agent.isSelf ? WorkKind::Self : WorkKind::MyAgent;
                item.myWorkHours = mine->workHours;
                item.agentWorkHours = mine->agentWorkHours;
                for (const auto &f : std::as_const(flags)) {
                    const bool forMe = f.personUid.isEmpty() || ctx.isMe(f.personUid);
                    const bool personAudience = f.flag.audience == ProblemFlag::Audience::Person || f.flag.audience == ProblemFlag::Audience::Both;
                    const bool managerAudience = f.flag.audience == ProblemFlag::Audience::Manager || f.flag.audience == ProblemFlag::Audience::Both;
                    if ((forMe && personAudience) || (oversee && managerAudience)) {
                        item.problems.append(f.flag);
                    }
                }
                out->items.append(item);
            } else if (mine) {
                haveMyTask = haveMyTask || false;
            }

            // ---- my agent is on this task and its person (me) is not (design 4.4, row 1: the person is told) --------------
            // A row for me exists only when my own planning person is on the task, so this one is a row that only
            // reports the problem. It is not my work. When I run the project, the manager row below reports it.
            if (!mine && !oversee && !done) {
                QSet<QString> told;
                for (const auto &f : std::as_const(flags)) {
                    if (f.flag.kind != ProblemFlag::Kind::AgentNoPerson || !ctx.isMe(f.personUid)) {
                        continue;
                    }
                    const Agent *agent = f.agent;
                    if (!agent || told.contains(agent->id)) {
                        continue;
                    }
                    told.insert(agent->id);
                    AgendaItem item;
                    item.home = Home::Gantt;
                    item.kind = ItemKind::GanttTask;
                    item.key = QStringLiteral("gantt:noperson:") + QString::number(taskId) + QLatin1Char(':') + agent->id;
                    item.title = FlagText::agentWithoutYou(agent->name, task.value(QLatin1String("title")).toString());
                    item.sourceKey = QStringLiteral("gantt:") + QString::number(projectId);
                    item.sourceName = projectTitle;
                    item.sourceColor = color;
                    item.category = Category::Work;
                    item.problemOnly = true; // not my work: no date, so no workload, no clash, no "No date" entry
                    item.workKind = WorkKind::MyAgent;
                    item.agent = AgentRegistry::refFor(*agent);
                    item.projectId = projectId;
                    item.projectName = projectTitle;
                    item.taskId = taskId;
                    item.versionReadOnly = readOnlyByGuard;
                    item.readOnly = true;
                    item.readOnlyReason = AgendaText::tr("This row only reports a problem. Change the task in NoGantt.");
                    item.problems.append(makeFlag(ProblemFlag::Kind::AgentNoPerson, ProblemFlag::Audience::Person, item.title));
                    out->items.append(item);
                }
            }

            // ---- other people's rows (what I run) -------------------------------------------------------
            if (!oversee || done) {
                continue;
            }
            bool anyOther = false;
            for (const auto &p : std::as_const(persons)) {
                if (ctx.isMe(p.uid)) {
                    continue;
                }
                anyOther = true;
                AgendaItem item;
                fillCommon(item);
                item.key = QStringLiteral("gantt:task:") + QString::number(taskId) + QStringLiteral(":") + p.uid;
                item.workKind = WorkKind::Team;
                item.agentWorkHours = p.agentWorkHours;
                item.personName = p.name;
                item.personUid = p.uid;
                item.agent = agentFor(p);
                for (const auto &f : std::as_const(flags)) {
                    const bool forThem = f.personUid.isEmpty() || f.personUid.compare(p.uid, Qt::CaseInsensitive) == 0;
                    const bool managerAudience = f.flag.audience == ProblemFlag::Audience::Manager || f.flag.audience == ProblemFlag::Audience::Both;
                    if (forThem && managerAudience) {
                        item.problems.append(f.flag);
                    }
                }
                if (late) {
                    item.problems.prepend(makeFlag(ProblemFlag::Kind::Late, ProblemFlag::Audience::Manager, FlagText::lateDays(lateDays)));
                }
                if (dueToday) {
                    item.problems.prepend(makeFlag(ProblemFlag::Kind::DueToday, ProblemFlag::Audience::Manager, FlagText::dueToday()));
                }
                if (blocked) {
                    item.problems.prepend(makeFlag(ProblemFlag::Kind::Blocked, ProblemFlag::Audience::Manager, FlagText::blockedBy(blockedBy)));
                }
                out->items.append(item);

                // A person on a run project that is not shared with them.
                if (data.sharesKnown && !relation.seenOnlyAsServerAdmin) {
                    notSharedCounts[p.uid] += 1;
                    notSharedNames[p.uid] = p.name;
                }
            }
            if (!anyOther && !mine) {
                // Nobody (or only roles, or only unlinked people): a task row without a person.
                AgendaItem item;
                fillCommon(item);
                item.key = QStringLiteral("gantt:task:") + QString::number(taskId);
                item.workKind = WorkKind::Team;
                item.agent = AgentRegistry::selfRef();
                for (const auto &f : std::as_const(flags)) {
                    const bool managerAudience = f.flag.audience == ProblemFlag::Audience::Manager || f.flag.audience == ProblemFlag::Audience::Both;
                    if (managerAudience) {
                        item.problems.append(f.flag);
                    }
                }
                if (late) {
                    item.problems.prepend(makeFlag(ProblemFlag::Kind::Late, ProblemFlag::Audience::Manager, FlagText::lateDays(lateDays)));
                }
                if (dueToday) {
                    item.problems.prepend(makeFlag(ProblemFlag::Kind::DueToday, ProblemFlag::Audience::Manager, FlagText::dueToday()));
                }
                out->items.append(item);
            }
        }

        // ---- persons in a project that is not shared with them ---------------------------------------------
        if (oversee && data.sharesKnown && !relation.seenOnlyAsServerAdmin) {
            bool groupOrTeamShares = false;
            QSet<QString> sharedUsers;
            for (const auto &s : data.shares) {
                const auto share = s.toObject();
                const auto type = share.value(QLatin1String("principal_type")).toString();
                if (type == QLatin1String("user")) {
                    sharedUsers.insert(share.value(QLatin1String("principal_id")).toString().toLower());
                } else {
                    groupOrTeamShares = true;
                }
            }
            if (!groupOrTeamShares) {
                for (auto it = notSharedCounts.constBegin(); it != notSharedCounts.constEnd(); ++it) {
                    const QString uid = it.key();
                    if (sharedUsers.contains(uid.toLower()) || uid.compare(relation.ownerId, Qt::CaseInsensitive) == 0) {
                        continue;
                    }
                    AgendaItem item;
                    item.home = Home::Gantt;
                    item.kind = ItemKind::GanttTask;
                    item.key = QStringLiteral("gantt:notshared:") + QString::number(projectId) + QLatin1Char(':') + uid;
                    item.title = FlagText::notShared(notSharedNames.value(uid), it.value());
                    item.sourceKey = QStringLiteral("gantt:") + QString::number(projectId);
                    item.sourceName = projectTitle;
                    item.sourceColor = color;
                    item.workKind = WorkKind::Team;
                    item.personName = notSharedNames.value(uid);
                    item.personUid = uid;
                    item.projectId = projectId;
                    item.projectName = projectTitle;
                    item.problems.append(makeFlag(ProblemFlag::Kind::NotShared, ProblemFlag::Audience::Manager, item.title));
                    out->items.append(item);
                }
            }
        }

        // ---- milestones of projects where I have tasks ------------------------------------------------------
        if (haveMyTask) {
            for (const auto &m : data.milestones) {
                const auto milestone = m.toObject();
                AgendaItem item;
                item.home = Home::Gantt;
                item.kind = ItemKind::Milestone;
                item.key = QStringLiteral("gantt:ms:") + QString::number(intOf(milestone.value(QLatin1String("id"))));
                item.title = milestone.value(QLatin1String("title")).toString();
                item.startDate = dateOfText(milestone.value(QLatin1String("milestone_date")));
                item.endDate = item.startDate;
                item.due = item.startDate;
                item.dueToday = item.due == ctx.today;
                item.sourceKey = QStringLiteral("gantt:") + QString::number(projectId);
                item.sourceName = projectTitle;
                item.sourceColor = color;
                item.category = Category::Work;
                item.workKind = WorkKind::Self;
                item.agent = AgentRegistry::selfRef();
                item.projectId = projectId;
                item.projectName = projectTitle;
                item.taskId = intOf(milestone.value(QLatin1String("id")));
                item.version = milestone.value(QLatin1String("updated_at")).toString();
                item.readOnly = true;
                item.readOnlyReason = AgendaText::tr("Milestones are changed in NoGantt.");
                item.versionReadOnly = readOnlyByGuard;
                if (item.startDate.isValid()) {
                    out->items.append(item);
                }
            }
        }
    }

    // ---- someone overbooked (NoGantt's own capacity check) in what I run ---------------------------------------
    const auto findings = snapshot.capacity.value(QLatin1String("findings")).toArray();
    int index = 0;
    for (const auto &f : findings) {
        const auto finding = f.toObject();
        ++index;
        // The finding must touch a task of a project that I run.
        int projectId = 0;
        QString projectTitle;
        QString color;
        const auto taskIds = finding.value(QLatin1String("task_ids")).toArray();
        for (const auto &projectValue : snapshot.projects) {
            const auto project = projectValue.toObject();
            const int id = intOf(project.value(QLatin1String("id")));
            const auto relation = relationOf(ctx, snapshot, project);
            if (ctx.settings.projectMode(relation) != WorkMode::DoOversee) {
                continue;
            }
            const auto data = snapshot.projectData.value(id);
            bool touches = false;
            for (const auto &t : data.tasks) {
                const int taskId = intOf(t.toObject().value(QLatin1String("id")));
                for (const auto &tid : taskIds) {
                    if (intOf(tid) == taskId) {
                        touches = true;
                    }
                }
            }
            if (touches) {
                projectId = id;
                projectTitle = project.value(QLatin1String("title")).toString();
                color = sourceColor(project.value(QLatin1String("color")).toString());
                break;
            }
        }
        if (projectId == 0) {
            continue;
        }
        QStringList names;
        const auto personIds = finding.value(QLatin1String("person_ids")).toArray();
        for (const auto &pid : personIds) {
            const auto resource = resources.value(intOf(pid));
            if (!resource.isEmpty()) {
                names.append(personName(resource));
            }
        }
        if (names.isEmpty()) {
            continue;
        }
        AgendaItem item;
        item.home = Home::Gantt;
        item.kind = ItemKind::GanttTask;
        item.key = QStringLiteral("gantt:capacity:") + QString::number(index);
        item.workKind = WorkKind::Team;
        item.personName = names.join(QStringLiteral(", "));
        item.title = FlagText::overbooked(item.personName);
        item.startDate = dateOfText(finding.value(QLatin1String("start")));
        item.endDate = dateOfText(finding.value(QLatin1String("end")));
        item.sourceKey = QStringLiteral("gantt:") + QString::number(projectId);
        item.sourceName = projectTitle;
        item.sourceColor = color;
        item.projectId = projectId;
        item.projectName = projectTitle;
        item.problems.append(makeFlag(ProblemFlag::Kind::Overbooked, ProblemFlag::Audience::Manager, item.title));
        out->items.append(item);
    }
}

} // namespace OCC::Nourax::Agenda::GanttMapper
