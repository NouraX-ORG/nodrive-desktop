/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "serverinfo.h"

#include "ocsjson.h"

#include <QJsonArray>

#include <memory>

namespace OCC::Nourax::Agenda {

QJsonObject ServerInfo::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("uid"), uid);
    o.insert(QStringLiteral("displayName"), displayName);
    o.insert(QStringLiteral("email"), email);
    o.insert(QStringLiteral("groups"), QJsonArray::fromStringList(groups));
    o.insert(QStringLiteral("admin"), isServerAdmin);
    o.insert(QStringLiteral("deckVersion"), deckVersion);
    o.insert(QStringLiteral("fetchedAt"), fetchedAt.isValid() ? fetchedAt.toUTC().toString(Qt::ISODate) : QString());
    return o;
}

ServerInfo ServerInfo::fromJson(const QJsonObject &o)
{
    ServerInfo i;
    i.uid = o.value(QLatin1String("uid")).toString();
    i.displayName = o.value(QLatin1String("displayName")).toString();
    i.email = o.value(QLatin1String("email")).toString();
    const auto groups = o.value(QLatin1String("groups")).toArray();
    for (const auto &g : groups) {
        i.groups.append(g.toString());
    }
    i.isServerAdmin = o.value(QLatin1String("admin")).toBool();
    i.deckVersion = o.value(QLatin1String("deckVersion")).toString();
    const auto stamp = o.value(QLatin1String("fetchedAt")).toString();
    i.fetchedAt = stamp.isEmpty() ? QDateTime() : QDateTime::fromString(stamp, Qt::ISODate).toUTC();
    i.loaded = !i.uid.isEmpty();
    return i;
}

void ServerInfo::applyUser(const QJsonObject &data, ServerInfo *info)
{
    info->uid = data.value(QLatin1String("id")).toString();
    info->displayName = data.value(QLatin1String("displayname")).toString();
    if (info->displayName.isEmpty()) {
        info->displayName = data.value(QLatin1String("display-name")).toString();
    }
    if (info->displayName.isEmpty()) {
        info->displayName = info->uid;
    }
    info->email = data.value(QLatin1String("email")).toString();
    info->groups.clear();
    const auto groups = data.value(QLatin1String("groups")).toArray();
    for (const auto &g : groups) {
        info->groups.append(g.toString());
    }
    info->isServerAdmin = info->groups.contains(QLatin1String("admin"));
}

void ServerInfo::applyCapabilities(const QJsonObject &data, ServerInfo *info)
{
    const auto capabilities = data.value(QLatin1String("capabilities")).toObject();
    const auto deck = capabilities.value(QLatin1String("deck")).toObject();
    info->deckVersion = deck.value(QLatin1String("version")).toString();
}

void ServerInfoClient::fetch(HttpTransport *transport, const ServerInfo &previous, const std::function<void(const ServerInfoResult &)> &done)
{
    transport->send(ocsGet(*transport, QStringLiteral("ocs/v2.php/cloud/user?format=json")), [transport, previous, done](const HttpResult &userReply) {
        const auto user = parseOcs(userReply);
        if (!user.ok() || !user.data.isObject()) {
            ServerInfoResult r;
            r.status = userReply.status;
            r.networkError = userReply.networkError;
            r.info = previous;
            done(r);
            return;
        }
        ServerInfo info = previous;
        ServerInfo::applyUser(user.data.toObject(), &info);
        transport->send(ocsGet(*transport, QStringLiteral("ocs/v1.php/cloud/capabilities?format=json")), [info, done](const HttpResult &capsReply) mutable {
            const auto caps = parseOcs(capsReply);
            if (caps.ok() && caps.data.isObject()) {
                ServerInfo::applyCapabilities(caps.data.toObject(), &info);
            }
            info.loaded = true;
            info.fetchedAt = QDateTime::currentDateTimeUtc();
            ServerInfoResult r;
            r.ok = true;
            r.status = capsReply.status;
            r.info = info;
            done(r);
        });
    });
}

} // namespace OCC::Nourax::Agenda
