/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: the NoBoard (Deck) calls of plans/noagenda-desktop-plan.md, 1.5, and the version guard.
// The REST routes are index.php/apps/deck/api/v1.0/... and answer plain JSON (no OCS envelope).

#pragma once

#include "../logic/sourcesettings.h"
#include "../net/httptransport.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>

#include <functional>

namespace OCC::Nourax::Agenda {

struct DeckSnapshot
{
    bool loaded = false;
    bool unavailable = false; ///< the app is not on the server
    QString version; ///< from the capabilities, for example 1.19.1
    QDateTime fetchedAt;
    /// Boards with their "details"; each board object also has "_stacks": the stacks with their cards.
    QJsonArray boards;
    QSet<int> failedBoards;

    [[nodiscard]] QJsonObject toJson() const;
    static DeckSnapshot fromJson(const QJsonObject &o);
};

struct DeckFetchOptions
{
    QString davUser;
    QString version; ///< from the capabilities ("" = unknown / app missing)
    QDateTime now;
    std::function<bool(const BoardRelation &)> wantBoard;
};

struct DeckFetchResult
{
    bool ok = false;
    int status = 0;
    bool networkError = false;
    DeckSnapshot snapshot;
};

class DeckClient
{
public:
    static QString apiBase();
    static void fetch(HttpTransport *transport, const DeckFetchOptions &options, const DeckSnapshot &previous,
        const std::function<void(const DeckFetchResult &)> &done);

    /// The version guard: only the tested major.minor (1.19) is read-write; anything else is read only.
    static bool versionIsTested(const QString &version);
    static QString testedVersionPrefix();
};

} // namespace OCC::Nourax::Agenda
