/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: NoBoard (Deck) cards to rows, with the agent label (design 4.2, 6.2, 6.3).

#pragma once

#include "../deck/deckclient.h"
#include "buildcontext.h"

namespace OCC::Nourax::Agenda {

namespace DeckMapper {

/// `linkedCardIds`: cards that are one row with their Gantt task (they are not listed twice).
void map(const BuildContext &ctx, const DeckSnapshot &snapshot, const QSet<int> &linkedCardIds, QList<AgendaItem> *out);

BoardRelation relationOf(const BuildContext &ctx, const QJsonObject &board);

} // namespace DeckMapper

} // namespace OCC::Nourax::Agenda
