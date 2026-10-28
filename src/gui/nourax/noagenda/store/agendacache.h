/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: the offline cache (plans/noagenda-desktop-plan.md, 1.3). Plain JSON files per account,
// written atomically with QSaveFile; every file has "cache_schema": 1. No password, token or cookie
// is ever written. A file that fails to parse or has another schema is ignored.

#pragma once

#include "../caldav/calendardata.h"
#include "../deck/deckclient.h"
#include "../gantt/ganttdata.h"
#include "../logic/sourcesettings.h"
#include "../ocs/serverinfo.h"

#include <QByteArray>
#include <QDateTime>
#include <QString>

namespace OCC::Nourax::Agenda {

struct RegistryCache
{
    bool present = false;
    QByteArray bytes; ///< the last valid file
    QByteArray etag;
    QDateTime fetchedAt;
};

class AgendaCache
{
public:
    static constexpr int schema = 1;

    /// `directory` is <config>/noagenda/<account id>/.
    explicit AgendaCache(const QString &directory);

    static QString directoryForAccount(const QString &accountId);

    [[nodiscard]] QString directory() const { return _dir; }

    bool saveSettings(const SourceSettings &settings) const;
    [[nodiscard]] SourceSettings loadSettings() const;
    bool saveRegistry(const RegistryCache &registry) const;
    [[nodiscard]] RegistryCache loadRegistry() const;
    bool saveCalendar(const CalendarSnapshot &snapshot) const;
    [[nodiscard]] CalendarSnapshot loadCalendar() const;
    bool saveGantt(const GanttSnapshot &snapshot) const;
    [[nodiscard]] GanttSnapshot loadGantt() const;
    bool saveDeck(const DeckSnapshot &snapshot) const;
    [[nodiscard]] DeckSnapshot loadDeck() const;
    bool saveServerInfo(const ServerInfo &info) const;
    [[nodiscard]] ServerInfo loadServerInfo() const;

    /// Removes the whole folder (when the account is removed from NoDrive).
    bool removeAll() const;

    static QString fileName(const char *name);

private:
    bool write(const char *name, const QJsonObject &data) const;
    [[nodiscard]] bool read(const char *name, QJsonObject *data) const;

    QString _dir;
};

} // namespace OCC::Nourax::Agenda
