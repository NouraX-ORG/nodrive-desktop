/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "deckclient.h"

#include "../ocs/ocsjson.h"

#include <QJsonDocument>
#include <QLoggingCategory>

#include <memory>

namespace OCC::Nourax::Agenda {

Q_LOGGING_CATEGORY(lcAgendaDeck, "nodrive.agenda.deck", QtInfoMsg)

QJsonObject DeckSnapshot::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("unavailable"), unavailable);
    o.insert(QStringLiteral("version"), version);
    o.insert(QStringLiteral("fetchedAt"), fetchedAt.isValid() ? fetchedAt.toUTC().toString(Qt::ISODate) : QString());
    o.insert(QStringLiteral("boards"), boards);
    return o;
}

DeckSnapshot DeckSnapshot::fromJson(const QJsonObject &o)
{
    DeckSnapshot s;
    s.unavailable = o.value(QLatin1String("unavailable")).toBool();
    s.version = o.value(QLatin1String("version")).toString();
    const auto stamp = o.value(QLatin1String("fetchedAt")).toString();
    s.fetchedAt = stamp.isEmpty() ? QDateTime() : QDateTime::fromString(stamp, Qt::ISODate).toUTC();
    s.boards = o.value(QLatin1String("boards")).toArray();
    s.loaded = s.fetchedAt.isValid();
    return s;
}

QString DeckClient::apiBase()
{
    return QStringLiteral("index.php/apps/deck/api/v1.0/");
}

QString DeckClient::testedVersionPrefix()
{
    return QStringLiteral("1.19");
}

bool DeckClient::versionIsTested(const QString &version)
{
    const auto parts = version.split(QLatin1Char('.'));
    if (parts.size() < 2) {
        return false;
    }
    return QString(parts.at(0) + QLatin1Char('.') + parts.at(1)) == testedVersionPrefix();
}

namespace {

struct State
{
    HttpTransport *transport = nullptr;
    DeckFetchOptions options;
    DeckSnapshot previous;
    DeckSnapshot result;
    std::function<void(const DeckFetchResult &)> done;
    QList<QJsonObject> boards;
    int index = 0;
    bool finished = false;
};
using StatePtr = std::shared_ptr<State>;

void finish(const StatePtr &s, bool ok, int status, bool networkError)
{
    if (s->finished) {
        return;
    }
    s->finished = true;
    DeckFetchResult r;
    r.ok = ok;
    r.status = status;
    r.networkError = networkError;
    if (ok) {
        s->result.loaded = true;
        s->result.version = s->options.version;
        s->result.fetchedAt = s->options.now.isValid() ? s->options.now : QDateTime::currentDateTimeUtc();
    }
    r.snapshot = ok ? s->result : s->previous;
    s->done(r);
}

HttpRequest deckGet(const HttpTransport &t, const QString &path)
{
    auto r = makeRequest("GET", t.url(DeckClient::apiBase() + path));
    // The CSRF header keeps the server's CORS check on the path that does not log the session out.
    r.headers.append({QByteArrayLiteral("OCS-APIRequest"), QByteArrayLiteral("true")});
    r.headers.append({QByteArrayLiteral("Accept"), QByteArrayLiteral("application/json")});
    return r;
}

void boardLoop(const StatePtr &s)
{
    while (s->index < s->boards.size()) {
        auto board = s->boards.at(s->index);
        const int id = intOf(board.value(QLatin1String("id")));
        const auto relation = BoardRelation::of(board, s->options.davUser);
        if (s->options.wantBoard && !s->options.wantBoard(relation)) {
            // Not wanted (Hidden): its cards are not read, but the board itself stays in the snapshot, so that the
            // Sources screen still lists it and it can be shown again.
            board.remove(QStringLiteral("_stacks"));
            s->result.boards.append(board);
            ++s->index;
            continue;
        }
        s->transport->send(deckGet(*s->transport, QStringLiteral("boards/") + QString::number(id) + QStringLiteral("/stacks")), [s, board, id](const HttpResult &r) mutable {
            QJsonParseError error;
            const auto doc = QJsonDocument::fromJson(r.body, &error);
            if (r.ok() && error.error == QJsonParseError::NoError && doc.isArray()) {
                board.insert(QStringLiteral("_stacks"), doc.array());
                s->result.boards.append(board);
            } else {
                qCInfo(lcAgendaDeck) << "stacks answered" << r.status;
                s->result.failedBoards.insert(id);
                // Keep the old copy of this board.
                bool kept = false;
                for (const auto &old : std::as_const(s->previous.boards)) {
                    if (intOf(old.toObject().value(QLatin1String("id"))) == id) {
                        s->result.boards.append(old);
                        kept = true;
                        break;
                    }
                }
                if (!kept) {
                    s->result.boards.append(board);
                }
            }
            ++s->index;
            boardLoop(s);
        });
        return;
    }
    finish(s, true, 0, false);
}

} // namespace

void DeckClient::fetch(HttpTransport *transport, const DeckFetchOptions &options, const DeckSnapshot &previous,
    const std::function<void(const DeckFetchResult &)> &done)
{
    auto s = std::make_shared<State>();
    s->transport = transport;
    s->options = options;
    s->previous = previous;
    s->done = done;

    if (options.version.isEmpty()) {
        // No "deck" capability: the app is not on the server.
        s->result.unavailable = true;
        finish(s, true, 404, false);
        return;
    }
    transport->send(deckGet(*transport, QStringLiteral("boards?details=true")), [s](const HttpResult &r) {
        if (r.status == 404) {
            s->result.unavailable = true;
            finish(s, true, 404, false);
            return;
        }
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(r.body, &error);
        if (!r.ok() || error.error != QJsonParseError::NoError || !doc.isArray()) {
            finish(s, false, r.status, r.networkError);
            return;
        }
        const auto boards = doc.array();
        for (const auto &b : boards) {
            const auto board = b.toObject();
            if (board.value(QLatin1String("archived")).toBool() || board.value(QLatin1String("deletedAt")).toInt() > 0) {
                continue;
            }
            s->boards.append(board);
        }
        boardLoop(s);
    });
}

} // namespace OCC::Nourax::Agenda
