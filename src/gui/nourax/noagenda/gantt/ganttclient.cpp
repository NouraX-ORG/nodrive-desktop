/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ganttclient.h"

#include "../ocs/ocsjson.h"
#include "ganttcontract.h"

#include <QLoggingCategory>

#include <memory>

namespace OCC::Nourax::Agenda {

Q_LOGGING_CATEGORY(lcAgendaGantt, "nodrive.agenda.gantt", QtInfoMsg)

namespace {

struct State
{
    HttpTransport *transport = nullptr;
    GanttFetchOptions options;
    GanttSnapshot previous;
    GanttSnapshot result;
    std::function<void(const GanttFetchResult &)> done;
    QSet<int> myProjectIds;
    QList<QJsonObject> projects;
    int projectIndex = 0;
    bool anyRunProject = false;
    bool finished = false;
};
using StatePtr = std::shared_ptr<State>;

void finish(const StatePtr &s, bool ok, int status, bool networkError)
{
    if (s->finished) {
        return;
    }
    s->finished = true;
    GanttFetchResult r;
    r.ok = ok;
    r.status = status;
    r.networkError = networkError;
    if (ok) {
        s->result.loaded = true;
        s->result.fetchedAt = s->options.now.isValid() ? s->options.now : QDateTime::currentDateTimeUtc();
    }
    r.snapshot = ok ? s->result : s->previous;
    s->done(r);
}

void get(const StatePtr &s, const QString &path, GanttContract::Endpoint endpoint, bool checkShape, const std::function<void(const OcsReply &)> &cb)
{
    const QString full = GanttClient::apiBase() + path + (path.contains(QLatin1Char('?')) ? QLatin1String("&format=json") : QLatin1String("?format=json"));
    s->transport->send(ocsGet(*s->transport, full), [s, endpoint, checkShape, cb](const HttpResult &r) {
        const auto reply = parseOcs(r);
        if (checkShape && reply.ok()) {
            const auto problem = GanttContract::check(endpoint, reply.data);
            if (!problem.isEmpty() && s->result.contractOk) {
                s->result.contractOk = false;
                s->result.contractProblem = problem;
                qCWarning(lcAgendaGantt) << "contract:" << problem;
            }
        }
        cb(reply);
    });
}

void projectLoop(const StatePtr &s);

void capacityStep(const StatePtr &s)
{
    if (!s->anyRunProject) {
        s->result.capacity = QJsonObject();
        finish(s, true, 0, false);
        return;
    }
    get(s, QStringLiteral("capacity-check"), GanttContract::Endpoint::CapacityCheck, true, [s](const OcsReply &r) {
        if (r.ok() && r.data.isObject()) {
            s->result.capacity = r.data.toObject();
        } else {
            s->result.capacity = s->previous.capacity;
        }
        finish(s, true, 0, false);
    });
}

void projectData(const StatePtr &s, const ProjectRelation &relation, GanttProjectData data)
{
    const QString base = QStringLiteral("projects/") + QString::number(relation.id);
    get(s, base + QStringLiteral("/tasks"), GanttContract::Endpoint::ProjectTasks, true, [s, relation, base, data](const OcsReply &tasks) mutable {
        if (!tasks.ok() || !tasks.data.isArray()) {
            s->result.failedProjects.insert(relation.id);
            const auto old = s->previous.projectData.value(relation.id);
            data.tasks = old.tasks;
            data.milestones = old.milestones;
            data.subprojects = old.subprojects;
            data.loaded = old.loaded;
            s->result.projectData.insert(relation.id, data);
            ++s->projectIndex;
            projectLoop(s);
            return;
        }
        data.tasks = tasks.data.toArray();
        get(s, base + QStringLiteral("/milestones"), GanttContract::Endpoint::Milestones, true, [s, relation, base, data](const OcsReply &ms) mutable {
            data.milestones = (ms.ok() && ms.data.isArray()) ? ms.data.toArray() : s->previous.projectData.value(relation.id).milestones;
            get(s, base + QStringLiteral("/subprojects"), GanttContract::Endpoint::Subprojects, true, [s, relation, data](const OcsReply &sub) mutable {
                data.subprojects = (sub.ok() && sub.data.isArray()) ? sub.data.toArray() : s->previous.projectData.value(relation.id).subprojects;
                data.loaded = true;
                s->result.projectData.insert(relation.id, data);
                ++s->projectIndex;
                projectLoop(s);
            });
        });
    });
}

void projectLoop(const StatePtr &s)
{
    while (s->projectIndex < s->projects.size()) {
        const auto project = s->projects.at(s->projectIndex);
        const int id = intOf(project.value(QLatin1String("id")));
        const bool owned = project.value(QLatin1String("owner_id")).toString().compare(s->options.davUser, Qt::CaseInsensitive) == 0;
        const bool adminPermission = project.value(QLatin1String("permission")).toString() == QLatin1String("admin");
        GanttProjectData data;
        const auto old = s->previous.projectData.value(id);
        const bool needShares = owned || adminPermission;
        const bool sharesFresh = old.sharesKnown && old.sharesFetchedAt.isValid()
            && old.sharesFetchedAt.secsTo(s->options.now.isValid() ? s->options.now : QDateTime::currentDateTimeUtc()) < 24 * 3600;

        auto proceed = [s, id, project](GanttProjectData d) {
            const auto relation = ProjectRelation::of(project, s->options.davUser, s->options.serverAdmin, s->options.groups,
                s->myProjectIds, d.shares, d.sharesKnown);
            if (s->options.wantProject && !s->options.wantProject(relation)) {
                // Hidden: its tasks are not read. The shares that were fetched stay, so that they are not
                // fetched again on every refresh (they are read once a day).
                if (d.sharesKnown) {
                    GanttProjectData kept;
                    kept.shares = d.shares;
                    kept.sharesKnown = true;
                    kept.sharesFetchedAt = d.sharesFetchedAt;
                    s->result.projectData.insert(id, kept);
                } else {
                    s->result.projectData.remove(id);
                }
                ++s->projectIndex;
                projectLoop(s);
                return;
            }
            if (relation.runByRealShare) {
                s->anyRunProject = true;
            }
            projectData(s, relation, d);
        };

        if (needShares && !sharesFresh) {
            get(s, QStringLiteral("projects/") + QString::number(id) + QStringLiteral("/shares"), GanttContract::Endpoint::Shares, true,
                [s, data, old, proceed](const OcsReply &r) mutable {
                    if (r.ok() && r.data.isArray()) {
                        data.shares = r.data.toArray();
                        data.sharesKnown = true;
                        data.sharesFetchedAt = s->options.now.isValid() ? s->options.now : QDateTime::currentDateTimeUtc();
                    } else {
                        data.shares = old.shares;
                        data.sharesKnown = old.sharesKnown;
                        data.sharesFetchedAt = old.sharesFetchedAt;
                    }
                    proceed(data);
                });
            return;
        }
        data.shares = old.shares;
        data.sharesKnown = old.sharesKnown;
        data.sharesFetchedAt = old.sharesFetchedAt;
        proceed(data);
        return;
    }
    capacityStep(s);
}

void slowStep(const StatePtr &s)
{
    const auto now = s->options.now.isValid() ? s->options.now : QDateTime::currentDateTimeUtc();
    const bool fresh = !s->options.forceSlow && s->previous.slowFetchedAt.isValid() && s->previous.slowFetchedAt.secsTo(now) < 3600;
    if (fresh) {
        s->result.workSchedules = s->previous.workSchedules;
        s->result.holidays = s->previous.holidays;
        s->result.workCalendars = s->previous.workCalendars;
        s->result.settings = s->previous.settings;
        s->result.taskSync = s->previous.taskSync;
        s->result.slowFetchedAt = s->previous.slowFetchedAt;
        s->projectIndex = 0;
        projectLoop(s);
        return;
    }
    s->result.slowFetchedAt = now;
    get(s, QStringLiteral("work-schedules"), GanttContract::Endpoint::Subprojects, false, [s](const OcsReply &a) {
        s->result.workSchedules = (a.ok() && a.data.isArray()) ? a.data.toArray() : s->previous.workSchedules;
        get(s, QStringLiteral("holidays"), GanttContract::Endpoint::Subprojects, false, [s](const OcsReply &b) {
            s->result.holidays = (b.ok() && b.data.isArray()) ? b.data.toArray() : s->previous.holidays;
            get(s, QStringLiteral("work-calendars"), GanttContract::Endpoint::Subprojects, false, [s](const OcsReply &c) {
                s->result.workCalendars = (c.ok() && c.data.isArray()) ? c.data.toArray() : s->previous.workCalendars;
                get(s, QStringLiteral("settings"), GanttContract::Endpoint::Subprojects, false, [s](const OcsReply &d) {
                    s->result.settings = (d.ok() && d.data.isObject()) ? d.data.toObject() : s->previous.settings;
                    get(s, QStringLiteral("task-sync"), GanttContract::Endpoint::TaskSync, true, [s](const OcsReply &e) {
                        s->result.taskSync = (e.ok() && e.data.isObject()) ? e.data.toObject() : s->previous.taskSync;
                        s->projectIndex = 0;
                        projectLoop(s);
                    });
                });
            });
        });
    });
}

} // namespace

QString GanttClient::apiBase()
{
    return QStringLiteral("ocs/v2.php/apps/nc_gantt/api/v1/");
}

void GanttClient::fetch(HttpTransport *transport, const GanttFetchOptions &options, const GanttSnapshot &previous,
    const std::function<void(const GanttFetchResult &)> &done)
{
    auto s = std::make_shared<State>();
    s->transport = transport;
    s->options = options;
    s->previous = previous;
    s->done = done;

    get(s, QStringLiteral("my-tasks?include_done=false&limit=500"), GanttContract::Endpoint::MyTasks, true, [s](const OcsReply &mine) {
        if (mine.notFound()) {
            s->result.unavailable = true;
            finish(s, true, mine.httpStatus, false);
            return;
        }
        if (!mine.ok() || !mine.data.isObject()) {
            finish(s, false, mine.httpStatus, mine.networkError);
            return;
        }
        const auto mineObject = mine.data.toObject();
        s->result.hasLinkedResource = mineObject.value(QLatin1String("has_linked_resource")).toBool(true);
        s->result.myTasks = mineObject.value(QLatin1String("tasks")).toArray();
        for (const auto &t : std::as_const(s->result.myTasks)) {
            s->myProjectIds.insert(intOf(t.toObject().value(QLatin1String("project_id"))));
        }
        get(s, QStringLiteral("projects"), GanttContract::Endpoint::Projects, true, [s](const OcsReply &projects) {
            if (!projects.ok() || !projects.data.isArray()) {
                finish(s, false, projects.httpStatus, projects.networkError);
                return;
            }
            s->result.projects = projects.data.toArray();
            for (const auto &p : std::as_const(s->result.projects)) {
                s->projects.append(p.toObject());
            }
            get(s, QStringLiteral("resources"), GanttContract::Endpoint::Resources, true, [s](const OcsReply &resources) {
                if (!resources.ok() || !resources.data.isArray()) {
                    finish(s, false, resources.httpStatus, resources.networkError);
                    return;
                }
                s->result.resources = resources.data.toArray();
                slowStep(s);
            });
        });
    });
}

} // namespace OCC::Nourax::Agenda
