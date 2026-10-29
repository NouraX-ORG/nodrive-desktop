/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ganttwriter.h"

#include "../ocs/ocsjson.h"
#include "ganttagentchange.h"
#include "writerutil.h"

namespace OCC::Nourax::Agenda {

namespace {

QString apiBase()
{
    return QStringLiteral("ocs/v2.php/apps/nc_gantt/api/v1/");
}

QString taskTitle(const QJsonObject &task)
{
    return task.value(QLatin1String("title")).toString();
}

/// Plain words for the codes that NoGantt answers with (the answer body only; never a value of the task).
QString refusalFor(const QByteArray &body, int status)
{
    const QString text = QString::fromUtf8(body);
    if (text.contains(QLatin1String("too_many_resources"))) {
        return AgendaText::tr("This task already has 4 people or agents. Ask your project manager.");
    }
    if (text.contains(QLatin1String("roles_only_assignment"))) {
        return AgendaText::tr("This project only allows roles. Ask your project manager.");
    }
    if (text.contains(QLatin1String("invalid_date")) || text.contains(QLatin1String("negative_float"))) {
        return AgendaText::tr("NoGantt does not accept these dates, because they would break the plan. Change them in NoGantt.");
    }
    Q_UNUSED(status)
    return AgendaText::tr("NoGantt did not accept this change. Open in NoGantt.");
}

} // namespace

void GanttWriter::withFreshTask(HttpTransport *transport, const EditCommand &command, const std::function<void(const QJsonObject &, const EditResult &)> &next)
{
    const int projectId = command.change.value(QLatin1String("projectId")).toInt();
    const int taskId = command.change.value(QLatin1String("taskId")).toInt();
    auto request = ocsGet(*transport, apiBase() + QStringLiteral("projects/") + QString::number(projectId) + QStringLiteral("/tasks?format=json"));
    transport->send(request, [command, taskId, next](const HttpResult &got) {
        EditResult problem;
        if (WriterUtil::commonFailure(got, &problem)) {
            if (problem.outcome == EditResult::Outcome::Refused) {
                problem.message = AgendaText::tr("You can view this project only. Ask the project manager for editor rights.");
            }
            next(QJsonObject(), problem);
            return;
        }
        if (got.status == 404) {
            next(QJsonObject(), WriterUtil::conflict(QStringLiteral("gone"), AgendaText::tr("This task was removed somewhere else while you were editing.")));
            return;
        }
        const auto reply = parseOcs(got);
        if (!reply.ok() || !reply.data.isArray()) {
            next(QJsonObject(), WriterUtil::failed(AgendaText::tr("NoGantt could not be read, so nothing was changed."), got.status));
            return;
        }
        QJsonObject found;
        for (const auto &t : reply.data.toArray()) {
            if (intOf(t.toObject().value(QLatin1String("id"))) == taskId) {
                found = t.toObject();
            }
        }
        if (found.isEmpty()) {
            next(QJsonObject(), WriterUtil::conflict(QStringLiteral("gone"), AgendaText::tr("This task was removed somewhere else while you were editing.")));
            return;
        }
        if (!command.baseVersion.isEmpty() && found.value(QLatin1String("updated_at")).toString() != command.baseVersion) {
            next(found, WriterUtil::conflict(QStringLiteral("changed"), AgendaText::tr("This changed somewhere else while you were editing. NoAgenda kept the other change.")));
            return;
        }
        EditResult ok;
        ok.outcome = EditResult::Outcome::Done;
        next(found, ok);
    });
}

QJsonObject GanttWriter::updateBody(const EditCommand &command, const QJsonObject &freshTask, QString *refusal)
{
    QJsonObject body;
    const auto &c = command.change;
    const QString mode = freshTask.value(QLatin1String("progress_mode")).toString();
    bool dates = false;
    if (c.contains(QLatin1String("start_date"))) {
        body.insert(QStringLiteral("start_date"), QString(c.value(QLatin1String("start_date")).toString() + WriterUtil::timePartOf(freshTask.value(QLatin1String("start_date")).toString())));
        dates = true;
    }
    if (c.contains(QLatin1String("end_date"))) {
        body.insert(QStringLiteral("end_date"), QString(c.value(QLatin1String("end_date")).toString() + WriterUtil::timePartOf(freshTask.value(QLatin1String("end_date")).toString())));
        dates = true;
    }
    if (c.contains(QLatin1String("manual_progress"))) {
        if (mode == QLatin1String("deck")) {
            if (refusal) {
                *refusal = AgendaText::tr("% done comes from NoBoard");
            }
            return QJsonObject();
        }
        body.insert(QStringLiteral("manual_progress"), qBound(0, c.value(QLatin1String("manual_progress")).toInt(), 100));
    }
    if (c.contains(QLatin1String("status"))) {
        body.insert(QStringLiteral("status"), c.value(QLatin1String("status")).toString());
    }
    if (c.contains(QLatin1String("done"))) {
        if (c.value(QLatin1String("done")).toBool()) {
            body.insert(QStringLiteral("status"), QStringLiteral("done"));
            if (mode == QLatin1String("manual")) {
                body.insert(QStringLiteral("manual_progress"), 100);
            }
        } else {
            body.insert(QStringLiteral("status"), QStringLiteral("in_progress"));
        }
    }
    if (dates) {
        body.insert(QStringLiteral("adjust_lag"), true); // NoGantt's own behaviour when a task is moved
    }
    body.insert(QStringLiteral("deck_confirmed"), false); // a linked card is never moved without the person's say
    return body;
}

void GanttWriter::run(HttpTransport *transport, const EditCommand &command, const EditContext &context, const EditDone &done)
{
    if (command.kind == EditKind::GanttUpdate) {
        withFreshTask(transport, command, [transport, command, done](const QJsonObject &task, const EditResult &state) {
            if (state.outcome != EditResult::Outcome::Done) {
                done(state);
                return;
            }
            QString refusal;
            const auto body = updateBody(command, task, &refusal);
            if (body.isEmpty()) {
                done(WriterUtil::refused(refusal));
                return;
            }
            put(transport, command, task, body, QJsonArray(), done);
        });
        return;
    }
    if (command.kind != EditKind::GanttAgentChange) {
        done(WriterUtil::failed(AgendaText::tr("This change is not a NoGantt change.")));
        return;
    }
    // My agent: the visible planning people are read fresh too (the server drops a row that is not visible to me).
    withFreshTask(transport, command, [transport, command, context, done](const QJsonObject &task, const EditResult &state) {
        if (state.outcome != EditResult::Outcome::Done) {
            done(state);
            return;
        }
        auto request = ocsGet(*transport, apiBase() + QStringLiteral("resources?format=json"));
        transport->send(request, [transport, command, context, done, task](const HttpResult &got) {
            EditResult problem;
            if (WriterUtil::commonFailure(got, &problem)) {
                done(problem);
                return;
            }
            const auto reply = parseOcs(got);
            if (!reply.ok() || !reply.data.isArray()) {
                done(WriterUtil::failed(AgendaText::tr("NoGantt could not be read, so nothing was changed."), got.status));
                return;
            }
            const auto planned = GanttAgentChange::plan(task, reply.data.toArray(), context.registry, command.change.value(QLatin1String("me")).toString(),
                context.myName, command.change.value(QLatin1String("agentId")).toString(), context.hoursPerPt);
            if (!planned.ok) {
                done(WriterUtil::refused(planned.refusal));
                return;
            }
            if (planned.noChange) {
                done(WriterUtil::done(task.value(QLatin1String("updated_at")).toString()));
                return;
            }
            QJsonObject body;
            body.insert(QStringLiteral("resources"), planned.resources);
            body.insert(QStringLiteral("deck_confirmed"), false);
            put(transport, command, task, body, planned.resources, done);
        });
    });
}

void GanttWriter::put(HttpTransport *transport, const EditCommand &command, const QJsonObject &freshTask, const QJsonObject &body, const QJsonArray &sentResources, const EditDone &done)
{
    const int taskId = command.change.value(QLatin1String("taskId")).toInt();
    const bool datesChanged = body.contains(QLatin1String("start_date")) || body.contains(QLatin1String("end_date"));
    auto request = WriterUtil::jsonRequest(*transport, "PUT", apiBase() + QStringLiteral("tasks/") + QString::number(taskId) + QStringLiteral("?format=json"), body);
    transport->send(request, [freshTask, sentResources, datesChanged, done](const HttpResult &sent) {
        EditResult r;
        if (WriterUtil::commonFailure(sent, &r)) {
            if (r.outcome == EditResult::Outcome::Refused) {
                r.message = AgendaText::tr("You can view this project only. Ask the project manager for editor rights.");
            }
            done(r);
            return;
        }
        if (sent.status == 404) {
            done(WriterUtil::conflict(QStringLiteral("gone"), AgendaText::tr("This task was removed somewhere else while you were editing.")));
            return;
        }
        if (sent.status == 400 || sent.status == 409 || sent.status == 412 || sent.status == 422) {
            done(WriterUtil::refused(refusalFor(sent.body, sent.status), sent.status));
            return;
        }
        const auto reply = parseOcs(sent);
        if (!reply.ok() || !reply.data.isObject()) {
            done(WriterUtil::failed(AgendaText::tr("NoGantt did not accept this change. Open in NoGantt."), sent.status));
            return;
        }
        const auto data = reply.data.toObject();
        const auto task = data.value(QLatin1String("task")).toObject();
        EditResult ok = WriterUtil::done(task.value(QLatin1String("updated_at")).toString());
        const int editedId = intOf(task.value(QLatin1String("id")));
        for (const auto &p : data.value(QLatin1String("propagated")).toArray()) {
            const auto moved = p.toObject();
            if (intOf(moved.value(QLatin1String("id"))) != editedId && !taskTitle(moved).isEmpty()) {
                ok.alsoMoved.append(taskTitle(moved));
            }
        }
        const auto op = data.value(QLatin1String("op_id"));
        if (op.isString() && !op.toString().isEmpty()) {
            ok.undoOpId = op.toString();
        }
        if (datesChanged && intOf(freshTask.value(QLatin1String("deck_card_id"))) > 0) {
            const auto deck = data.value(QLatin1String("deck")).toObject();
            if (!deck.value(QLatin1String("pending")).toArray().isEmpty() || intOf(deck.value(QLatin1String("unconfirmed"))) > 0) {
                ok.note = AgendaText::tr("The linked card wasn't moved. Open in NoGantt to confirm.");
            }
        }
        if (!sentResources.isEmpty() && !GanttAgentChange::sameResources(sentResources, task.value(QLatin1String("resources")).toArray())) {
            ok.checkInNoGantt = true;
            ok.note = AgendaText::tr("Check this task in NoGantt");
        }
        done(ok);
    });
}

void GanttWriter::undo(HttpTransport *transport, const QString &opId, const EditDone &done)
{
    auto request = WriterUtil::jsonRequest(*transport, "POST", apiBase() + QStringLiteral("changes/") + opId + QStringLiteral("/revert?format=json"), QJsonObject());
    transport->send(request, [done](const HttpResult &sent) {
        EditResult r;
        if (WriterUtil::commonFailure(sent, &r)) {
            done(r);
            return;
        }
        if (!sent.ok()) {
            done(WriterUtil::failed(AgendaText::tr("NoGantt could not undo the move."), sent.status));
            return;
        }
        done(WriterUtil::done());
    });
}

} // namespace OCC::Nourax::Agenda
