/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "deckmapper.h"

#include "../model/agendacolors.h"
#include "../ocs/ocsjson.h"

#include <QJsonArray>
#include <QJsonObject>

namespace OCC::Nourax::Agenda::DeckMapper {

BoardRelation relationOf(const BuildContext &ctx, const QJsonObject &board)
{
    return BoardRelation::of(board, ctx.me);
}

void map(const BuildContext &ctx, const DeckSnapshot &snapshot, const QSet<int> &linkedCardIds, QList<AgendaItem> *out)
{
    if (!snapshot.loaded || snapshot.unavailable) {
        return;
    }
    const bool readOnlyByGuard = !DeckClient::versionIsTested(snapshot.version);
    const QString robot = QString::fromUcs4(U"\U0001F916");

    for (const auto &boardValue : snapshot.boards) {
        const auto board = boardValue.toObject();
        const auto relation = relationOf(ctx, board);
        const auto mode = ctx.settings.boardMode(relation);
        if (mode == WorkMode::Hidden) {
            continue;
        }
        const bool oversee = mode == WorkMode::DoOversee;
        const QString boardTitle = board.value(QLatin1String("title")).toString();
        const QString boardColor = sourceColor(board.value(QLatin1String("color")).toString());
        const bool canEdit = board.value(QLatin1String("permissions")).toObject().value(QLatin1String("PERMISSION_EDIT")).toBool(true);
        const int boardId = relation.id;

        const auto stacks = board.value(QLatin1String("_stacks")).toArray();
        for (const auto &stackValue : stacks) {
            const auto stack = stackValue.toObject();
            const auto cards = stack.value(QLatin1String("cards")).toArray();
            for (const auto &cardValue : cards) {
                const auto card = cardValue.toObject();
                const int cardId = intOf(card.value(QLatin1String("id")));
                if (card.value(QLatin1String("archived")).toBool()) {
                    continue;
                }
                const auto doneValue = card.value(QLatin1String("done"));
                const bool done = (doneValue.isString() && !doneValue.toString().isEmpty()) || (doneValue.isBool() && doneValue.toBool());
                if (done || linkedCardIds.contains(cardId)) {
                    continue;
                }

                // People
                QStringList assignedUids;
                QStringList assignedNames;
                const auto assigned = card.value(QLatin1String("assignedUsers")).toArray();
                for (const auto &a : assigned) {
                    const auto participant = a.toObject().value(QLatin1String("participant")).toObject();
                    const auto uid = participant.value(QLatin1String("uid")).toString();
                    if (!uid.isEmpty()) {
                        assignedUids.append(uid);
                        const auto display = participant.value(QLatin1String("displayname")).toString();
                        assignedNames.append(display.isEmpty() ? uid : display);
                    }
                }
                bool mine = false;
                for (const auto &uid : std::as_const(assignedUids)) {
                    if (ctx.isMe(uid)) {
                        mine = true;
                    }
                }

                // Agent labels
                QList<const Agent *> labelAgents;
                QStringList unknownLabels;
                const auto labels = card.value(QLatin1String("labels")).toArray();
                for (const auto &l : labels) {
                    const auto title = l.toObject().value(QLatin1String("title")).toString();
                    if (const auto *agent = ctx.registry.byLabel(title)) {
                        labelAgents.append(agent);
                    } else if (title.startsWith(robot)) {
                        unknownLabels.append(title);
                    }
                }

                const QDate due = localDateOfIso(card.value(QLatin1String("duedate")).toString(), ctx.zone);
                const bool late = due.isValid() && due < ctx.today;
                const int lateDays = late ? static_cast<int>(due.daysTo(ctx.today)) : 0;
                const bool dueToday = due.isValid() && due == ctx.today;

                AgendaItem item;
                item.home = Home::Board;
                item.kind = ItemKind::Card;
                item.key = QStringLiteral("deck:card:") + QString::number(cardId);
                item.title = card.value(QLatin1String("title")).toString();
                item.description = card.value(QLatin1String("description")).toString().left(2000);
                item.due = due;
                item.endDate = due;
                item.sourceKey = QStringLiteral("board:") + QString::number(boardId);
                item.sourceName = boardTitle;
                item.sourceColor = boardColor;
                item.category = Category::Work;
                item.late = late;
                item.lateDays = lateDays;
                item.dueToday = dueToday;
                item.boardId = boardId;
                item.cardId = cardId;
                item.projectName = boardTitle;
                const auto etag = card.value(QLatin1String("ETag")).toString();
                item.version = etag.isEmpty() ? QString::number(intOf(card.value(QLatin1String("lastModified")))) : etag;
                item.versionReadOnly = readOnlyByGuard;
                if (!canEdit) {
                    item.readOnly = true;
                    item.readOnlyReason = AgendaText::tr("You can see this board but not change it.");
                }

                if (mine) {
                    QList<const Agent *> mineAgents;
                    QList<ProblemFlag> problems;
                    for (const auto *agent : std::as_const(labelAgents)) {
                        if (ctx.isMe(agent->person)) {
                            mineAgents.append(agent);
                            if (agent->isRetired()) {
                                problems.append(makeFlag(ProblemFlag::Kind::RetiredAgent, ProblemFlag::Audience::Person, FlagText::retiredAgent(agent->name)));
                            }
                        } else {
                            bool onCard = false;
                            for (const auto &uid : std::as_const(assignedUids)) {
                                onCard = onCard || uid.compare(agent->person, Qt::CaseInsensitive) == 0;
                            }
                            if (!onCard && oversee) {
                                problems.append(makeFlag(ProblemFlag::Kind::AgentWrongPerson, ProblemFlag::Audience::Manager,
                                    FlagText::agentWrongPerson(agent->name, agent->person, ctx.myName)));
                            }
                        }
                    }
                    for (const auto &label : std::as_const(unknownLabels)) {
                        problems.append(makeFlag(ProblemFlag::Kind::UnknownAgent, ProblemFlag::Audience::Person, FlagText::unknownAgent(label)));
                    }
                    if (mineAgents.size() > 1) {
                        problems.append(makeFlag(ProblemFlag::Kind::TwoAgents, ProblemFlag::Audience::Both, FlagText::twoAgents(ctx.myName)));
                    }
                    if (!mineAgents.isEmpty()) {
                        item.agent = AgentRegistry::refFor(*mineAgents.first());
                    } else if (!unknownLabels.isEmpty()) {
                        item.agent = AgentRegistry::unknownRef(unknownLabels.first());
                    } else {
                        item.agent = AgentRegistry::selfRef();
                    }
                    item.workKind = item.agent.isSelf ? WorkKind::Self : WorkKind::MyAgent;
                    item.problems = problems;
                    out->append(item);
                    continue;
                }

                if (!oversee) {
                    continue;
                }
                // A card of someone else on a board I run.
                item.workKind = WorkKind::Team;
                item.personName = assignedNames.join(QStringLiteral(", "));
                item.personUid = assignedUids.value(0);
                QList<ProblemFlag> problems;
                const Agent *shown = nullptr;
                for (const auto *agent : std::as_const(labelAgents)) {
                    bool onCard = false;
                    for (const auto &uid : std::as_const(assignedUids)) {
                        onCard = onCard || uid.compare(agent->person, Qt::CaseInsensitive) == 0;
                    }
                    if (assignedUids.isEmpty()) {
                        problems.append(makeFlag(ProblemFlag::Kind::AgentNoPerson, ProblemFlag::Audience::Manager, FlagText::agentNoPerson()));
                    } else if (!onCard) {
                        problems.append(makeFlag(ProblemFlag::Kind::AgentWrongPerson, ProblemFlag::Audience::Manager,
                            FlagText::agentWrongPerson(agent->name, agent->person, assignedNames.first())));
                    } else if (!shown) {
                        shown = agent;
                    } else if (shown->person.compare(agent->person, Qt::CaseInsensitive) == 0) {
                        problems.append(makeFlag(ProblemFlag::Kind::TwoAgents, ProblemFlag::Audience::Both, FlagText::twoAgents(assignedNames.first())));
                    }
                }
                if (shown) {
                    item.agent = ctx.refFor(*shown, assignedNames.value(0));
                }
                if (assignedUids.isEmpty() && due.isValid()) {
                    problems.append(makeFlag(ProblemFlag::Kind::NotAssigned, ProblemFlag::Audience::Manager, FlagText::notAssigned()));
                }
                if (late) {
                    problems.prepend(makeFlag(ProblemFlag::Kind::Late, ProblemFlag::Audience::Manager, FlagText::lateDays(lateDays)));
                }
                if (dueToday) {
                    problems.prepend(makeFlag(ProblemFlag::Kind::DueToday, ProblemFlag::Audience::Manager, FlagText::dueToday()));
                }
                item.problems = problems;
                out->append(item);
            }
        }
    }
}

} // namespace OCC::Nourax::Agenda::DeckMapper
