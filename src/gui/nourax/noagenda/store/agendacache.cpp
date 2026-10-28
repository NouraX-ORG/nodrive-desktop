/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "agendacache.h"

#include "configfile.h"

#include <QDir>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>

namespace OCC::Nourax::Agenda {

AgendaCache::AgendaCache(const QString &directory)
    : _dir(directory)
{
}

QString AgendaCache::directoryForAccount(const QString &accountId)
{
    QString safe = accountId;
    safe.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")), QStringLiteral("_"));
    if (safe.isEmpty()) {
        safe = QStringLiteral("account");
    }
    return ConfigFile().configPath() + QStringLiteral("noagenda/") + safe;
}

QString AgendaCache::fileName(const char *name)
{
    return QString::fromLatin1(name) + QStringLiteral(".json");
}

bool AgendaCache::write(const char *name, const QJsonObject &data) const
{
    if (!QDir().mkpath(_dir)) {
        return false;
    }
    QJsonObject root;
    root.insert(QStringLiteral("cache_schema"), schema);
    root.insert(QStringLiteral("data"), data);
    QSaveFile file(_dir + QLatin1Char('/') + fileName(name));
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    return file.commit();
}

bool AgendaCache::read(const char *name, QJsonObject *data) const
{
    QFile file(_dir + QLatin1Char('/') + fileName(name));
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }
    const auto root = doc.object();
    if (root.value(QLatin1String("cache_schema")).toInt(-1) != schema || !root.value(QLatin1String("data")).isObject()) {
        return false;
    }
    *data = root.value(QLatin1String("data")).toObject();
    return true;
}

bool AgendaCache::saveSettings(const SourceSettings &settings) const
{
    return write("settings", settings.toJson());
}

SourceSettings AgendaCache::loadSettings() const
{
    QJsonObject o;
    return read("settings", &o) ? SourceSettings::fromJson(o) : SourceSettings();
}

bool AgendaCache::saveRegistry(const RegistryCache &registry) const
{
    QJsonObject o;
    o.insert(QStringLiteral("json"), QString::fromUtf8(registry.bytes));
    o.insert(QStringLiteral("etag"), QString::fromUtf8(registry.etag));
    o.insert(QStringLiteral("fetchedAt"), registry.fetchedAt.isValid() ? registry.fetchedAt.toUTC().toString(Qt::ISODate) : QString());
    return write("registry", o);
}

RegistryCache AgendaCache::loadRegistry() const
{
    RegistryCache c;
    QJsonObject o;
    if (read("registry", &o)) {
        c.present = true;
        c.bytes = o.value(QLatin1String("json")).toString().toUtf8();
        c.etag = o.value(QLatin1String("etag")).toString().toUtf8();
        const auto stamp = o.value(QLatin1String("fetchedAt")).toString();
        c.fetchedAt = stamp.isEmpty() ? QDateTime() : QDateTime::fromString(stamp, Qt::ISODate).toUTC();
    }
    return c;
}

bool AgendaCache::saveCalendar(const CalendarSnapshot &snapshot) const
{
    return write("calendars", snapshot.toJson());
}

CalendarSnapshot AgendaCache::loadCalendar() const
{
    QJsonObject o;
    return read("calendars", &o) ? CalendarSnapshot::fromJson(o) : CalendarSnapshot();
}

bool AgendaCache::saveGantt(const GanttSnapshot &snapshot) const
{
    return write("gantt", snapshot.toJson());
}

GanttSnapshot AgendaCache::loadGantt() const
{
    QJsonObject o;
    return read("gantt", &o) ? GanttSnapshot::fromJson(o) : GanttSnapshot();
}

bool AgendaCache::saveDeck(const DeckSnapshot &snapshot) const
{
    return write("deck", snapshot.toJson());
}

DeckSnapshot AgendaCache::loadDeck() const
{
    QJsonObject o;
    return read("deck", &o) ? DeckSnapshot::fromJson(o) : DeckSnapshot();
}

bool AgendaCache::saveServerInfo(const ServerInfo &info) const
{
    return write("user", info.toJson());
}

ServerInfo AgendaCache::loadServerInfo() const
{
    QJsonObject o;
    return read("user", &o) ? ServerInfo::fromJson(o) : ServerInfo();
}

bool AgendaCache::removeAll() const
{
    // Only NoAgenda's own cache folder (<config>/noagenda/<account>), never anything else.
    if (_dir.isEmpty() || !QDir::fromNativeSeparators(_dir).contains(QLatin1String("/noagenda/"))) {
        return false;
    }
    return QDir(_dir).removeRecursively();
}

} // namespace OCC::Nourax::Agenda
