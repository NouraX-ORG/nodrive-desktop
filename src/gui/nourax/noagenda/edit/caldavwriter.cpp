/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "caldavwriter.h"

#include "writerutil.h"

#include <QUrl>

namespace OCC::Nourax::Agenda {

namespace {

QString quoted(const QString &etag)
{
    return QLatin1Char('"') + normalizeVersion(etag) + QLatin1Char('"');
}

QString encode(const QString &segment)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(segment));
}

} // namespace

QString CalDavWriter::objectPath(const QString &davUser, const QString &calendarUri, const QString &objectUri)
{
    return QStringLiteral("remote.php/dav/calendars/") + encode(davUser) + QLatin1Char('/') + encode(calendarUri) + QLatin1Char('/') + encode(objectUri);
}

ICalDateValue CalDavWriter::dateValue(const QJsonValue &value)
{
    if (!value.isString()) {
        return ICalDateValue::keep();
    }
    const QString text = value.toString();
    if (text.isEmpty()) {
        return ICalDateValue::remove();
    }
    if (text.size() == 10) {
        const auto day = QDate::fromString(text, Qt::ISODate);
        return day.isValid() ? ICalDateValue::ofDate(day) : ICalDateValue::keep();
    }
    const auto time = QDateTime::fromString(text, Qt::ISODate);
    return time.isValid() ? ICalDateValue::ofUtc(time) : ICalDateValue::keep();
}

ICalEdit CalDavWriter::editFrom(const QJsonObject &change, const QDateTime &now)
{
    ICalEdit edit;
    edit.now = now;
    edit.isTodo = change.value(QLatin1String("isTodo")).toBool();
    if (change.contains(QLatin1String("title"))) {
        edit.summary = change.value(QLatin1String("title")).toString();
    }
    if (change.contains(QLatin1String("description"))) {
        edit.description = change.value(QLatin1String("description")).toString();
    }
    edit.start = dateValue(change.value(QLatin1String("start")));
    edit.end = dateValue(change.value(QLatin1String("end")));
    edit.due = dateValue(change.value(QLatin1String("due")));
    if (change.contains(QLatin1String("done"))) {
        edit.done = change.value(QLatin1String("done")).toBool();
    }
    if (change.contains(QLatin1String("agent"))) {
        const auto agent = change.value(QLatin1String("agent")).toObject();
        edit.changeAgent = true;
        edit.agentId = agent.value(QLatin1String("id")).toString();
        edit.addCategory = agent.value(QLatin1String("tag")).toString();
        const auto remove = agent.value(QLatin1String("removeTags")).toArray();
        for (const auto &v : remove) {
            edit.removeCategories.append(v.toString());
        }
    }
    return edit;
}

void CalDavWriter::run(HttpTransport *transport, const EditCommand &command, const EditContext &context, const EditDone &done)
{
    switch (command.kind) {
    case EditKind::CalendarPatch:
        patch(transport, command, context, done);
        return;
    case EditKind::CalendarCreate:
        create(transport, command, context, done);
        return;
    case EditKind::CalendarDelete:
        remove(transport, command, context, done);
        return;
    default:
        done(WriterUtil::failed(AgendaText::tr("This change is not a calendar change.")));
        return;
    }
}

void CalDavWriter::patch(HttpTransport *transport, const EditCommand &command, const EditContext &context, const EditDone &done)
{
    const QString path = objectPath(context.davUser, command.change.value(QLatin1String("calendarUri")).toString(), command.change.value(QLatin1String("objectUri")).toString());
    const auto url = transport->url(path);
    transport->send(makeRequest("GET", url), [transport, command, context, done, url](const HttpResult &got) {
        EditResult result;
        if (WriterUtil::commonFailure(got, &result)) {
            done(result);
            return;
        }
        if (got.status == 404) {
            done(WriterUtil::conflict(QStringLiteral("gone"), AgendaText::tr("This was removed somewhere else while you were editing.")));
            return;
        }
        if (!got.ok()) {
            done(WriterUtil::failed(AgendaText::tr("The server could not give NoAgenda this item."), got.status));
            return;
        }
        const QString etag = QString::fromLatin1(got.headers.value("etag"));
        if (etag.isEmpty()) {
            done(WriterUtil::failed(AgendaText::tr("The server did not say which version this is, so nothing was changed."), got.status));
            return;
        }
        if (!command.baseVersion.isEmpty() && normalizeVersion(etag) != normalizeVersion(command.baseVersion)) {
            done(WriterUtil::conflict(QStringLiteral("changed"), AgendaText::tr("This changed somewhere else while you were editing. NoAgenda kept the other change.")));
            return;
        }
        const auto patched = ICalPatch::apply(QString::fromUtf8(got.body), editFrom(command.change, context.now));
        if (!patched.ok) {
            if (patched.error == QLatin1String("repeats")) {
                done(WriterUtil::refused(AgendaText::tr("This repeats. Change it in the Calendar or Tasks page.")));
            } else {
                done(WriterUtil::failed(AgendaText::tr("NoAgenda could not read this item, so nothing was changed.")));
            }
            return;
        }
        auto put = withBody(withHeader(makeRequest("PUT", url), "If-Match", quoted(etag).toUtf8()), patched.text.toUtf8(), "text/calendar; charset=utf-8");
        transport->send(put, [transport, done, url](const HttpResult &sent) {
            EditResult r;
            if (WriterUtil::commonFailure(sent, &r)) {
                done(r);
                return;
            }
            if (sent.status == 412) {
                done(WriterUtil::conflict(QStringLiteral("changed"), AgendaText::tr("This changed somewhere else while you were editing. NoAgenda kept the other change.")));
                return;
            }
            if (sent.status == 404) {
                done(WriterUtil::conflict(QStringLiteral("gone"), AgendaText::tr("This was removed somewhere else while you were editing.")));
                return;
            }
            if (!sent.ok()) {
                done(WriterUtil::failed(AgendaText::tr("The server did not accept the change."), sent.status));
                return;
            }
            const QString version = QString::fromLatin1(sent.headers.value("etag"));
            if (!version.isEmpty()) {
                done(WriterUtil::done(version));
                return;
            }
            // Some servers do not send the new ETag with the answer to a PUT: ask for it.
            transport->send(makeRequest("GET", url), [done](const HttpResult &again) {
                done(WriterUtil::done(again.ok() ? QString::fromLatin1(again.headers.value("etag")) : QString()));
            });
        });
    });
}

void CalDavWriter::create(HttpTransport *transport, const EditCommand &command, const EditContext &context, const EditDone &done)
{
    const QString uid = command.change.value(QLatin1String("uid")).toString();
    const auto edit = editFrom(command.change, context.now);
    const auto created = ICalPatch::create(uid, edit);
    if (!created.ok) {
        done(WriterUtil::refused(AgendaText::tr("This needs a start time.")));
        return;
    }
    const QString path = objectPath(context.davUser, command.change.value(QLatin1String("calendarUri")).toString(), uid + QStringLiteral(".ics"));
    auto put = withBody(withHeader(makeRequest("PUT", transport->url(path)), "If-None-Match", "*"), created.text.toUtf8(), "text/calendar; charset=utf-8");
    transport->send(put, [done](const HttpResult &sent) {
        EditResult r;
        if (WriterUtil::commonFailure(sent, &r)) {
            done(r);
            return;
        }
        if (sent.status == 412) {
            done(WriterUtil::conflict(QStringLiteral("changed"), AgendaText::tr("An item with this id already exists.")));
            return;
        }
        if (!sent.ok()) {
            done(WriterUtil::failed(AgendaText::tr("The server did not accept the new item."), sent.status));
            return;
        }
        done(WriterUtil::done(QString::fromLatin1(sent.headers.value("etag"))));
    });
}

void CalDavWriter::remove(HttpTransport *transport, const EditCommand &command, const EditContext &context, const EditDone &done)
{
    const QString path = objectPath(context.davUser, command.change.value(QLatin1String("calendarUri")).toString(), command.change.value(QLatin1String("objectUri")).toString());
    auto request = makeRequest("DELETE", transport->url(path));
    if (!command.baseVersion.isEmpty()) {
        request = withHeader(request, "If-Match", quoted(command.baseVersion).toUtf8());
    }
    transport->send(request, [done](const HttpResult &sent) {
        EditResult r;
        if (WriterUtil::commonFailure(sent, &r)) {
            done(r);
            return;
        }
        if (sent.status == 412) {
            done(WriterUtil::conflict(QStringLiteral("changed"), AgendaText::tr("This changed somewhere else while you were editing. NoAgenda kept the other change.")));
            return;
        }
        if (sent.status == 404) {
            done(WriterUtil::done()); // it is gone already: nothing is lost
            return;
        }
        if (!sent.ok()) {
            done(WriterUtil::failed(AgendaText::tr("The server did not delete it."), sent.status));
            return;
        }
        done(WriterUtil::done());
    });
}

} // namespace OCC::Nourax::Agenda
