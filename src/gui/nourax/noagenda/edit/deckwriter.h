/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: writes NoBoard (Deck) cards (plans/noagenda-desktop-plan.md, 2.4).
// The card update of Deck is a FULL replace: a field that is not sent is cleared (`done` and `startdate` included).
// So the PUT is built from the card read fresh right before the write, plus the one change. Deck has no real ETag
// for a write either, so the fresh card's version is compared with the one the person saw.

#pragma once

#include "../net/httptransport.h"
#include "editcommand.h"

namespace OCC::Nourax::Agenda {

class DeckWriter
{
public:
    static void run(HttpTransport *transport, const EditCommand &command, const EditContext &context, const EditDone &done);

    /// The body of the card PUT: every field from the fresh card, plus the change (public for the tests).
    static QJsonObject updateBody(const EditCommand &command, const QJsonObject &freshCard, const EditContext &context);
    /// The version of a card as the reader keeps it: its ETag, or else its lastModified.
    static QString versionOf(const QJsonObject &card);

private:
    static void freshCard(HttpTransport *transport, const EditCommand &command, const std::function<void(const QJsonObject &, const EditResult &)> &next);
    static void cardUpdate(HttpTransport *transport, const EditCommand &command, const EditContext &context, const EditDone &done);
    static void agentLabel(HttpTransport *transport, const EditCommand &command, const EditContext &context, const EditDone &done);
};

} // namespace OCC::Nourax::Agenda
