/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "deckmapper.h"

#include "../model/agendacolors.h"
#include "../ocs/ocsjson.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QMap>

namespace OCC::Nourax::Agenda::DeckMapper {

BoardRelation relationOf(const BuildContext &ctx, const QJsonObject &board)
{
    return BoardRelation::of(board, ctx.me);
}

namespace {

/// The registry agent a board label stands for (design 4.2: matched by title among the agents of the people
/// the card is assigned to). Names are unique only within an organisation, so the same label can belong to
/// agents of different people. When none of the assigned people has such an agent, the label may still name
/// an agent of someone else (the "belongs to" problem); one I may see is preferred then.
const Agent *agentOfLabel(const BuildContext &ctx, const QString &title, const QStringList &assignedUids)
{
    const auto candidates = ctx.registry.allByLabel(title);
    for (const auto *agent : candidates) {
        for (const auto &uid : assignedUids) {
            if (uid.compare(agent->person, Qt::CaseInsensitive) == 0) {
                return agent;
            }
        }
    }
    for (const auto *agent : candidates) {
        if (ctx.mayNameAgent(*agent)) {
            return agent;
        }
    }
    return candidates.isEmpty() ? nullptr : candidates.first();
}

/// Display names of the people the board shows (owner, members, assigned users), by lower case uid. A user id is
/// never written on screen: a person without a name here is left out of the sentence.
QMap<QString, QString> displayNamesOf(const BuildContext &ctx, const QJsonObject &board)
{
    QMap<QString, QString> names;
    auto add = [&names](const QJsonObject &person) {
        const auto uid = person.value(QLatin1String("uid")).toString().toLower();
        const auto display = person.value(QLatin1String("displayname")).toString();
        if (!uid.isEmpty() && !display.isEmpty() && !names.contains(uid)) {
            names.insert(uid, display);
        }
    };
    if (!ctx.me.isEmpty() && !ctx.myName.isEmpty()) {
        names.insert(ctx.me.toLower(), ctx.myName);
    }
    add(board.value(QLatin1String("owner")).toObject());
    const auto acl = board.value(QLatin1String("acl")).toArray();
    for (const auto &entry : acl) {
        add(entry.toObject().value(QLatin1String("participant")).toObject());
    }
    const auto stacks = board.value(QLatin1String("_stacks")).toArray();
    for (const auto &stack : stacks) {
        const auto cards = stack.toObject().value(QLatin1String("cards")).toArray();
        for (const auto &card : cards) {
            const auto assigned = card.toObject().value(QLatin1String("assignedUsers")).toArray();
            for (const auto &a : assigned) {
                add(a.toObject().value(QLatin1String("participant")).toObject());
            }
        }
    }
    return names;
}

} // namespace

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
        const auto personNames = displayNamesOf(ctx, board);
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
                    if (const auto *agent = agentOfLabel(ctx, title, assignedUids)) {
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
                                    FlagText::agentWrongPerson(ctx.agentNameInText(*agent), personNames.value(agent->person.toLower()), ctx.myName)));
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
                    // One of my agents is on a card that has nobody assigned (design 4.4, row 1: the person is told).
                    // The card is not mine, so this row only reports the problem; it is not my work. When I run the
                    // board, the team row below reports it.
                    if (assignedUids.isEmpty()) {
                        QSet<QString> told;
                        for (const auto *agent : std::as_const(labelAgents)) {
                            if (!ctx.isMe(agent->person) || told.contains(agent->id)) {
                                continue;
                            }
                            told.insert(agent->id);
                            AgendaItem problem;
                            problem.home = Home::Board;
                            problem.kind = ItemKind::Card;
                            problem.key = QStringLiteral("deck:noperson:") + QString::number(cardId) + QLatin1Char(':') + agent->id;
                            problem.title = FlagText::agentWithoutYou(agent->name, item.title);
                            problem.sourceKey = item.sourceKey;
                            problem.sourceName = boardTitle;
                            problem.sourceColor = boardColor;
                            problem.category = Category::Work;
                            problem.problemOnly = true; // not my work: no date, so no workload, no clash, no "No date" entry
                            problem.workKind = WorkKind::MyAgent;
                            problem.agent = AgentRegistry::refFor(*agent);
                            problem.boardId = boardId;
                            problem.cardId = cardId;
                            problem.projectName = boardTitle;
                            problem.versionReadOnly = readOnlyByGuard;
                            problem.readOnly = true;
                            problem.readOnlyReason = AgendaText::tr("This row only reports a problem. Change the card in NoBoard.");
                            problem.problems.append(makeFlag(ProblemFlag::Kind::AgentNoPerson, ProblemFlag::Audience::Person, problem.title));
                            out->append(problem);
                        }
                    }
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
                            FlagText::agentWrongPerson(ctx.agentNameInText(*agent), personNames.value(agent->person.toLower()), assignedNames.first())));
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
