/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// Helpers of the NoAgenda edit tests: a transport that answers from a handler and remembers every request, so a test
// can say exactly which verb, path, headers and body each edit must send.

#pragma once

#include "agendatestsupport.h"

#include "gui/nourax/noagenda/edit/editcommand.h"
#include "gui/nourax/noagenda/edit/editservice.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace AgendaTest {

class ScriptedTransport : public HttpTransport
{
public:
    using Handler = std::function<HttpResult(const HttpRequest &)>;

    void send(const HttpRequest &request, const HttpCallback &done) override
    {
        requests.append(request);
        HttpResult result;
        if (offline) {
            result.networkError = true;
            done(result);
            return;
        }
        if (deferred) {
            held.append({request, done});
            return;
        }
        if (handler) {
            result = handler(request);
        } else {
            result.status = 404;
        }
        done(result);
    }
    [[nodiscard]] QUrl serverUrl() const override { return QUrl(QStringLiteral("https://drive.example.test")); }
    [[nodiscard]] QString davUser() const override { return user; }

    static HttpResult answer(int status, const QByteArray &body = QByteArray(), const QMap<QByteArray, QByteArray> &headers = {})
    {
        HttpResult r;
        r.status = status;
        r.body = body;
        r.headers = headers;
        return r;
    }
    static QByteArray header(const HttpRequest &request, const QByteArray &name)
    {
        for (const auto &h : request.headers) {
            if (h.first.compare(name, Qt::CaseInsensitive) == 0) {
                return h.second;
            }
        }
        return QByteArray();
    }
    [[nodiscard]] QList<HttpRequest> of(const QByteArray &verb) const
    {
        QList<HttpRequest> out;
        for (const auto &r : requests) {
            if (r.verb == verb) {
                out.append(r);
            }
        }
        return out;
    }
    [[nodiscard]] int count(const QByteArray &verb) const { return static_cast<int>(of(verb).size()); }

    /// Answers the oldest held request (only with `deferred`): lets a test see what is outstanding at the same time.
    bool releaseOne()
    {
        if (held.isEmpty()) {
            return false;
        }
        const auto first = held.takeFirst();
        first.second(handler ? handler(first.first) : answer(404));
        return true;
    }

    QString user = QStringLiteral("ali");
    bool offline = false;
    bool deferred = false;
    Handler handler;
    QList<HttpRequest> requests;
    QList<QPair<HttpRequest, HttpCallback>> held;
};

inline QByteArray json(const QJsonValue &value)
{
    return value.isArray() ? QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact) : QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact);
}

inline QByteArray ocsBody(const QJsonValue &data)
{
    QJsonObject meta{{QStringLiteral("status"), QStringLiteral("ok")}, {QStringLiteral("statuscode"), 200}, {QStringLiteral("message"), QStringLiteral("OK")}};
    QJsonObject ocs{{QStringLiteral("meta"), meta}, {QStringLiteral("data"), data}};
    return json(QJsonObject{{QStringLiteral("ocs"), ocs}});
}

inline QJsonObject parseObject(const QByteArray &body)
{
    return QJsonDocument::fromJson(body).object();
}

/// The context of Ali with the agent registry of the test fixtures.
inline EditContext aliContext()
{
    EditContext c;
    c.me = QStringLiteral("ali");
    c.myName = QStringLiteral("Ali");
    c.davUser = QStringLiteral("ali");
    c.registry = AgentRegistry::parse(fixture(QStringLiteral("agent-registry.json")));
    c.now = QDateTime(QDate(2026, 10, 28), QTime(12, 0), QTimeZone::UTC);
    c.zone = QTimeZone(QTimeZone::UTC);
    const auto resources = fixtureJson(QStringLiteral("gantt/resources.json"));
    c.ganttResources = resources.toArray();
    return c;
}

inline AgendaItem eventItem()
{
    AgendaItem item;
    item.key = QStringLiteral("cal:work/call.ics");
    item.kind = ItemKind::Event;
    item.home = Home::Calendar;
    item.title = QStringLiteral("Client call");
    item.calendarUri = QStringLiteral("work");
    item.objectUri = QStringLiteral("call.ics");
    item.version = QStringLiteral("\"e1\"");
    item.timed = true;
    item.start = QDateTime(QDate(2026, 10, 28), QTime(10, 0), QTimeZone::UTC);
    item.end = QDateTime(QDate(2026, 10, 28), QTime(11, 0), QTimeZone::UTC);
    return item;
}

inline AgendaItem ganttItem()
{
    AgendaItem item;
    item.key = QStringLiteral("gantt:task:111");
    item.kind = ItemKind::GanttTask;
    item.home = Home::Gantt;
    item.title = QStringLiteral("Landing page build");
    item.projectId = 1;
    item.taskId = 111;
    item.version = QStringLiteral("2026-10-27 09:00:00");
    item.startDate = QDate(2026, 10, 28);
    item.endDate = QDate(2026, 10, 30);
    item.due = item.endDate;
    item.progressMode = QStringLiteral("manual");
    return item;
}

inline AgendaItem cardItem()
{
    AgendaItem item;
    item.key = QStringLiteral("deck:card:501");
    item.kind = ItemKind::Card;
    item.home = Home::Board;
    item.title = QStringLiteral("Landing page card");
    item.boardId = 10;
    item.stackId = 20;
    item.cardId = 501;
    item.version = QStringLiteral("etag501");
    item.due = QDate(2026, 10, 30);
    return item;
}

} // namespace AgendaTest
