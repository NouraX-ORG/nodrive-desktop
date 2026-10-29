/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "deckwriter.h"

#include "../deck/deckclient.h"
#include "../ocs/ocsjson.h"
#include "writerutil.h"

#include <QJsonDocument>
#include <QSet>

#include <memory>

namespace OCC::Nourax::Agenda {

namespace {

QString cardPath(const QJsonObject &change)
{
    return DeckClient::apiBase() + QStringLiteral("boards/") + QString::number(change.value(QLatin1String("boardId")).toInt()) + QStringLiteral("/stacks/")
        + QString::number(change.value(QLatin1String("stackId")).toInt()) + QStringLiteral("/cards/") + QString::number(change.value(QLatin1String("cardId")).toInt());
}

HttpRequest getRequest(const HttpTransport &transport, const QString &path)
{
    return ocsGet(transport, path);
}

/// Sends the requests one after the other; stops at the first answer that is not 2xx.
void runSteps(HttpTransport *transport, const std::shared_ptr<QList<HttpRequest>> &steps, int index, const std::function<void(int, const HttpResult &)> &finished)
{
    if (index >= steps->size()) {
        finished(-1, HttpResult());
        return;
    }
    transport->send(steps->at(index), [transport, steps, index, finished](const HttpResult &answer) {
        if (!answer.ok()) {
            finished(index, answer);
            return;
        }
        runSteps(transport, steps, index + 1, finished);
    });
}

QString uidOf(const QJsonValue &owner)
{
    if (owner.isString()) {
        return owner.toString();
    }
    return owner.toObject().value(QLatin1String("uid")).toString();
}

} // namespace

QString DeckWriter::versionOf(const QJsonObject &card)
{
    const auto etag = card.value(QLatin1String("ETag")).toString();
    return etag.isEmpty() ? QString::number(intOf(card.value(QLatin1String("lastModified")))) : etag;
}

void DeckWriter::freshCard(HttpTransport *transport, const EditCommand &command, const std::function<void(const QJsonObject &, const EditResult &)> &next)
{
    transport->send(getRequest(*transport, cardPath(command.change)), [command, next](const HttpResult &got) {
        EditResult problem;
        if (WriterUtil::commonFailure(got, &problem)) {
            if (problem.outcome == EditResult::Outcome::Refused) {
                problem.message = AgendaText::tr("You can see this board but not change it.");
            }
            next(QJsonObject(), problem);
            return;
        }
        if (got.status == 404) {
            next(QJsonObject(), WriterUtil::conflict(QStringLiteral("gone"), AgendaText::tr("This card was removed somewhere else while you were editing.")));
            return;
        }
        const auto doc = QJsonDocument::fromJson(got.body);
        if (!got.ok() || !doc.isObject()) {
            next(QJsonObject(), WriterUtil::failed(AgendaText::tr("NoBoard could not be read, so nothing was changed."), got.status));
            return;
        }
        const auto card = doc.object();
        const QString base = normalizeVersion(command.baseVersion);
        const bool same = base.isEmpty() || base == normalizeVersion(card.value(QLatin1String("ETag")).toString())
            || base == QString::number(intOf(card.value(QLatin1String("lastModified"))));
        if (!same) {
            next(card, WriterUtil::conflict(QStringLiteral("changed"), AgendaText::tr("This changed somewhere else while you were editing. NoAgenda kept the other change.")));
            return;
        }
        EditResult ok;
        ok.outcome = EditResult::Outcome::Done;
        next(card, ok);
    });
}

QJsonObject DeckWriter::updateBody(const EditCommand &command, const QJsonObject &card, const EditContext &context)
{
    QJsonObject body;
    body.insert(QStringLiteral("title"), card.value(QLatin1String("title")).toString());
    body.insert(QStringLiteral("type"), card.value(QLatin1String("type")).toString(QStringLiteral("plain")));
    body.insert(QStringLiteral("owner"), uidOf(card.value(QLatin1String("owner"))));
    body.insert(QStringLiteral("description"), card.value(QLatin1String("description")).toString());
    body.insert(QStringLiteral("order"), intOf(card.value(QLatin1String("order"))));
    body.insert(QStringLiteral("archived"), card.value(QLatin1String("archived")).toBool());
    body.insert(QStringLiteral("startdate"), card.value(QLatin1String("startdate")).isString() ? card.value(QLatin1String("startdate")) : QJsonValue(QJsonValue::Null));
    // A field that is not sent is cleared by Deck, so every field is always sent.
    QJsonValue due = card.value(QLatin1String("duedate")).isString() ? card.value(QLatin1String("duedate")) : QJsonValue(QJsonValue::Null);
    if (command.change.contains(QLatin1String("due"))) {
        const auto text = command.change.value(QLatin1String("due")).toString();
        const auto day = QDate::fromString(text, Qt::ISODate);
        if (text.isEmpty() || !day.isValid()) {
            due = QJsonValue(QJsonValue::Null);
        } else {
            // Local midnight, so the day reads back the same in the reader (which shows the due date in the local zone).
            const QTimeZone zone = context.zone.isValid() ? context.zone : QTimeZone(QTimeZone::UTC);
            due = QDateTime(day, QTime(0, 0), zone).toUTC().toString(Qt::ISODate);
        }
    }
    body.insert(QStringLiteral("duedate"), due);
    QJsonValue done = card.value(QLatin1String("done")).isString() && !card.value(QLatin1String("done")).toString().isEmpty() ? card.value(QLatin1String("done")) : QJsonValue(QJsonValue::Null);
    if (command.change.contains(QLatin1String("done"))) {
        done = command.change.value(QLatin1String("done")).toBool() ? QJsonValue((context.now.isValid() ? context.now : QDateTime::currentDateTimeUtc()).toUTC().toString(Qt::ISODate)) : QJsonValue(QJsonValue::Null);
    }
    body.insert(QStringLiteral("done"), done);
    return body;
}

void DeckWriter::run(HttpTransport *transport, const EditCommand &command, const EditContext &context, const EditDone &done)
{
    if (command.kind == EditKind::DeckCardUpdate) {
        cardUpdate(transport, command, context, done);
    } else if (command.kind == EditKind::DeckAgentLabel) {
        agentLabel(transport, command, context, done);
    } else {
        done(WriterUtil::failed(AgendaText::tr("This change is not a NoBoard change.")));
    }
}

void DeckWriter::cardUpdate(HttpTransport *transport, const EditCommand &command, const EditContext &context, const EditDone &done)
{
    freshCard(transport, command, [transport, command, context, done](const QJsonObject &card, const EditResult &state) {
        if (state.outcome != EditResult::Outcome::Done) {
            done(state);
            return;
        }
        const auto body = updateBody(command, card, context);
        transport->send(WriterUtil::jsonRequest(*transport, "PUT", cardPath(command.change), body), [done](const HttpResult &sent) {
            EditResult r;
            if (WriterUtil::commonFailure(sent, &r)) {
                if (r.outcome == EditResult::Outcome::Refused) {
                    r.message = AgendaText::tr("You can see this board but not change it.");
                }
                done(r);
                return;
            }
            if (sent.status == 404) {
                done(WriterUtil::conflict(QStringLiteral("gone"), AgendaText::tr("This card was removed somewhere else while you were editing.")));
                return;
            }
            const auto doc = QJsonDocument::fromJson(sent.body);
            if (!sent.ok() || !doc.isObject()) {
                done(WriterUtil::failed(AgendaText::tr("NoBoard did not accept this change. Open in NoBoard."), sent.status));
                return;
            }
            done(WriterUtil::done(versionOf(doc.object())));
        });
    });
}

void DeckWriter::agentLabel(HttpTransport *transport, const EditCommand &command, const EditContext &context, const EditDone &done)
{
    freshCard(transport, command, [transport, command, context, done](const QJsonObject &card, const EditResult &state) {
        if (state.outcome != EditResult::Outcome::Done) {
            done(state);
            return;
        }
        const QString me = command.change.value(QLatin1String("me")).toString();
        const QString chosenId = command.change.value(QLatin1String("agentId")).toString();
        const QString boardPath = DeckClient::apiBase() + QStringLiteral("boards/") + QString::number(command.change.value(QLatin1String("boardId")).toInt());

        const Agent *chosen = nullptr;
        if (!chosenId.isEmpty()) {
            chosen = context.registry.byId(chosenId);
            if (!chosen || chosen->person.compare(me, Qt::CaseInsensitive) != 0) {
                done(WriterUtil::refused(AgendaText::tr("This agent is not one of yours.")));
                return;
            }
            if (!chosen->isActive()) {
                done(WriterUtil::refused(AgendaText::tr("This agent can't be chosen now.")));
                return;
            }
            if (chosen->boardLabel.isEmpty()) {
                done(WriterUtil::refused(AgendaText::tr("This agent has no label for boards yet. Ask your administrator.")));
                return;
            }
        }
        const QString chosenNorm = chosen ? AgentRegistry::normalize(chosen->boardLabel) : QString();
        QSet<QString> mine;
        for (const auto *agent : context.registry.ofPerson(me)) {
            if (!agent->boardLabel.isEmpty()) {
                mine.insert(AgentRegistry::normalize(agent->boardLabel));
            }
        }
        QList<int> removeIds;
        int chosenOnCard = 0;
        for (const auto &l : card.value(QLatin1String("labels")).toArray()) {
            const auto label = l.toObject();
            const QString norm = AgentRegistry::normalize(label.value(QLatin1String("title")).toString());
            if (!chosenNorm.isEmpty() && norm == chosenNorm) {
                chosenOnCard = intOf(label.value(QLatin1String("id")));
            } else if (mine.contains(norm)) {
                removeIds.append(intOf(label.value(QLatin1String("id"))));
            }
        }
        if (removeIds.isEmpty() && (chosen ? chosenOnCard > 0 : true)) {
            done(WriterUtil::done(versionOf(card))); // the card already has this agent
            return;
        }

        const QString cardBase = cardPath(command.change);
        // The steps: remove my other agents' labels, then add the chosen one. When adding fails, the old labels are put back.
        auto finish = [transport, command, cardBase, removeIds, done](int chosenLabelId) {
            auto steps = std::make_shared<QList<HttpRequest>>();
            for (const int id : removeIds) {
                QJsonObject body;
                body.insert(QStringLiteral("labelId"), id);
                steps->append(WriterUtil::jsonRequest(*transport, "PUT", cardBase + QStringLiteral("/removeLabel"), body));
            }
            const int removed = static_cast<int>(steps->size());
            if (chosenLabelId > 0) {
                QJsonObject body;
                body.insert(QStringLiteral("labelId"), chosenLabelId);
                steps->append(WriterUtil::jsonRequest(*transport, "PUT", cardBase + QStringLiteral("/assignLabel"), body));
            }
            runSteps(transport, steps, 0, [transport, command, cardBase, removeIds, removed, done](int failedIndex, const HttpResult &answer) {
                if (failedIndex < 0) {
                    // The card changed: read it again for its new version.
                    transport->send(getRequest(*transport, cardPath(command.change)), [done](const HttpResult &again) {
                        const auto doc = QJsonDocument::fromJson(again.body);
                        done(WriterUtil::done(again.ok() && doc.isObject() ? versionOf(doc.object()) : QString()));
                    });
                    return;
                }
                EditResult r;
                if (WriterUtil::commonFailure(answer, &r) && r.outcome != EditResult::Outcome::Refused) {
                    done(r);
                    return;
                }
                if (failedIndex >= removed) {
                    // Adding the new label failed: put the old labels back.
                    auto back = std::make_shared<QList<HttpRequest>>();
                    for (const int id : removeIds) {
                        QJsonObject body;
                        body.insert(QStringLiteral("labelId"), id);
                        back->append(WriterUtil::jsonRequest(*transport, "PUT", cardBase + QStringLiteral("/assignLabel"), body));
                    }
                    runSteps(transport, back, 0, [done, answer](int, const HttpResult &) {
                        done(WriterUtil::refused(AgendaText::tr("NoBoard did not accept the new agent. The old agent was put back."), answer.status));
                    });
                    return;
                }
                done(WriterUtil::refused(AgendaText::tr("NoBoard did not accept this change. Open in NoBoard."), answer.status));
            });
        };

        if (!chosen || chosenOnCard > 0) {
            finish(0);
            return;
        }
        // The chosen agent's label on the board: it may have to be created (needs the "manage" permission).
        transport->send(getRequest(*transport, boardPath), [transport, boardPath, chosen, finish, done](const HttpResult &got) {
            EditResult problem;
            if (WriterUtil::commonFailure(got, &problem)) {
                if (problem.outcome == EditResult::Outcome::Refused) {
                    problem.message = AgendaText::tr("You can see this board but not change it.");
                }
                done(problem);
                return;
            }
            const auto doc = QJsonDocument::fromJson(got.body);
            if (!got.ok() || !doc.isObject()) {
                done(WriterUtil::failed(AgendaText::tr("NoBoard could not be read, so nothing was changed."), got.status));
                return;
            }
            const auto board = doc.object();
            const QString wanted = AgentRegistry::normalize(chosen->boardLabel);
            int labelId = 0;
            for (const auto &l : board.value(QLatin1String("labels")).toArray()) {
                if (AgentRegistry::normalize(l.toObject().value(QLatin1String("title")).toString()) == wanted) {
                    labelId = intOf(l.toObject().value(QLatin1String("id")));
                }
            }
            if (labelId > 0) {
                finish(labelId);
                return;
            }
            if (!board.value(QLatin1String("permissions")).toObject().value(QLatin1String("PERMISSION_MANAGE")).toBool()) {
                done(WriterUtil::refused(AgendaText::tr("This board has no label for %1 yet. Ask a board manager to add it.").arg(chosen->boardLabel)));
                return;
            }
            QJsonObject body;
            body.insert(QStringLiteral("title"), chosen->boardLabel);
            body.insert(QStringLiteral("color"), QStringLiteral("31CC7C"));
            transport->send(WriterUtil::jsonRequest(*transport, "POST", boardPath + QStringLiteral("/labels"), body), [finish, done](const HttpResult &created) {
                const auto labelDoc = QJsonDocument::fromJson(created.body);
                EditResult r;
                if (WriterUtil::commonFailure(created, &r)) {
                    done(r);
                    return;
                }
                const int id = labelDoc.isObject() ? intOf(labelDoc.object().value(QLatin1String("id"))) : 0;
                if (!created.ok() || id <= 0) {
                    done(WriterUtil::failed(AgendaText::tr("NoBoard could not add the label for this agent."), created.status));
                    return;
                }
                finish(id);
            });
        });
    });
}

} // namespace OCC::Nourax::Agenda
