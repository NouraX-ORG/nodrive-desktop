/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda against a real throwaway server (tools/ci/agenda-server.sh): a Nextcloud 35 with the calendar,
// tasks, Deck and nc_gantt 2.4.1 apps and the seed of tools/ci/agenda-seed.py. Skipped unless
// NODRIVE_TEST_SERVER_URL is set. This test talks to the throwaway server only.
//
// Environment:
//   NODRIVE_TEST_SERVER_URL        for example http://nodrive-ci-server
//   NODRIVE_TEST_USER / _PASSWORD  the person and an app password made by occ (throwaway, never in the source)
//   NODRIVE_TEST_MODE              auth-proof | contract | read-ali | read-mara | read-sara | read-olaf | guard
//   NODRIVE_TEST_REPORT            file to which "| check | PASS/FAIL | detail |" lines are appended (no values)
//   NODRIVE_TEST_SEED              seed.json written by the seed script
//   NODRIVE_TEST_OUTDIR            folder for the contract key sets and the cache for the live screenshots

#include "nodrive/noagenda/agendatestsupport.h"

#include "gui/nourax/noagenda/net/accounttransport.h"
#include "gui/nourax/noagenda/ocs/ocsjson.h"

#include "accessmanager.h"
#include "account.h"
#include "creds/abstractcredentials.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>

using namespace AgendaTest;
using namespace OCC;

namespace {

class BasicAuthAccessManager : public AccessManager
{
public:
    BasicAuthAccessManager(const QString &user, const QString &password, QObject *parent = nullptr)
        : AccessManager(parent)
        , _auth("Basic " + QByteArray(user.toUtf8() + ":" + password.toUtf8()).toBase64())
    {
    }

protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request, QIODevice *outgoingData) override
    {
        QNetworkRequest req(request);
        req.setRawHeader("Authorization", _auth);
        return AccessManager::createRequest(op, req, outgoingData);
    }

private:
    QByteArray _auth;
};

class BasicCredentials : public AbstractCredentials
{
    Q_OBJECT

public:
    BasicCredentials(const QString &user, const QString &password)
        : _user(user)
        , _password(password)
    {
        _wasFetched = true;
    }
    [[nodiscard]] QString authType() const override { return QStringLiteral("http"); }
    [[nodiscard]] QString user() const override { return _user; }
    [[nodiscard]] QString password() const override { return _password; }
    [[nodiscard]] bool ready() const override { return true; }
    bool stillValid(QNetworkReply *) override { return true; }
    void askFromUser() override {}
    void fetchFromKeychain(const QString &) override
    {
        _wasFetched = true;
        Q_EMIT fetched();
    }
    void persist() override {}
    void invalidateToken() override {}
    void forgetSensitiveData() override {}
    [[nodiscard]] QNetworkAccessManager *createQNAM() const override { return new BasicAuthAccessManager(_user, _password); }

private:
    QString _user;
    QString _password;
};

/// The transport of the account, with the answers of chosen calls rewritten: what a newer app would send.
class RewritingTransport : public HttpTransport
{
public:
    RewritingTransport(std::unique_ptr<AccountTransport> inner, std::function<QByteArray(const HttpRequest &, const HttpResult &)> rewrite)
        : _inner(std::move(inner))
        , _rewrite(std::move(rewrite))
    {
    }
    void send(const HttpRequest &request, const HttpCallback &done) override
    {
        _inner->send(request, [this, request, done](const HttpResult &r) {
            HttpResult copy = r;
            if (_rewrite) {
                const auto body = _rewrite(request, r);
                if (!body.isNull()) {
                    copy.body = body;
                }
            }
            done(copy);
        });
    }
    [[nodiscard]] QUrl serverUrl() const override { return _inner->serverUrl(); }
    [[nodiscard]] QString davUser() const override { return _inner->davUser(); }

private:
    std::unique_ptr<AccountTransport> _inner;
    std::function<QByteArray(const HttpRequest &, const HttpResult &)> _rewrite;
};

} // namespace

class TestNoDriveAgendaServer : public QObject
{
    Q_OBJECT

    QString _serverUrl;
    QString _user;
    QString _password;
    QString _reportPath;
    QString _outDir;
    QJsonObject _seed;
    QStringList _failures;
    AccountPtr _account;

    void check(const QString &name, bool ok, const QString &detail = QString())
    {
        if (!_reportPath.isEmpty()) {
            QFile f(_reportPath);
            if (f.open(QIODevice::Append | QIODevice::Text)) {
                QTextStream(&f) << "| " << _user << ": " << name << " | " << (ok ? "PASS" : "FAIL") << " | " << detail << " |\n";
            }
        }
        if (!ok) {
            _failures.append(name + QStringLiteral(": ") + detail);
        }
    }

    AccountPtr makeAccount()
    {
        auto account = Account::create();
        account->setCredentials(new BasicCredentials(_user, _password));
        account->setUrl(QUrl(_serverUrl));
        account->setDavUser(_user);
        return account;
    }

    std::unique_ptr<AccountAgenda> newAgenda(const QString &cacheDir, std::unique_ptr<HttpTransport> transport = nullptr)
    {
        if (!transport) {
            transport = std::make_unique<AccountTransport>(_account);
        }
        auto agenda = std::make_unique<AccountAgenda>(std::move(transport), cacheDir);
        agenda->setZone(QTimeZone::systemTimeZone());
        return agenda;
    }

    bool refresh(AccountAgenda *agenda)
    {
        QSignalSpy spy(agenda, &AccountAgenda::refreshFinished);
        agenda->refresh(true);
        return spy.count() > 0 || spy.wait(180000);
    }

    HttpResult get(AccountTransport *transport, const QString &path, bool ocsHeader = true)
    {
        HttpResult out;
        bool done = false;
        auto request = ocsHeader ? ocsGet(*transport, path) : makeRequest("GET", transport->url(path));
        transport->send(request, [&](const HttpResult &r) {
            out = r;
            done = true;
        });
        for (int i = 0; i < 600 && !done; ++i) {
            QTest::qWait(100);
        }
        return out;
    }

    HttpResult dav(AccountTransport *transport, const QByteArray &verb, const QString &path, const QByteArray &body, int depth)
    {
        HttpResult out;
        bool done = false;
        auto request = makeRequest(verb, transport->url(path));
        request.headers.append({QByteArrayLiteral("Depth"), QByteArray::number(depth)});
        request = withBody(request, body, QByteArrayLiteral("application/xml; charset=utf-8"));
        transport->send(request, [&](const HttpResult &r) {
            out = r;
            done = true;
        });
        for (int i = 0; i < 600 && !done; ++i) {
            QTest::qWait(100);
        }
        return out;
    }

    static QString gantt(const QString &path)
    {
        return QStringLiteral("ocs/v2.php/apps/nc_gantt/api/v1/") + path + QStringLiteral("?format=json");
    }

    int seedId(const char *group, const QString &name) const
    {
        return _seed.value(QLatin1String(group)).toObject().value(name).toInt();
    }

    const AgendaItem *byTitle(const AccountAgenda &agenda, const QString &title) const
    {
        return findTitle(agenda.state().items, title);
    }

    void finishPhase()
    {
        QVERIFY2(_failures.isEmpty(), qPrintable(_failures.join(QStringLiteral("; "))));
    }

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    void phase()
    {
        _serverUrl = qEnvironmentVariable("NODRIVE_TEST_SERVER_URL");
        if (_serverUrl.isEmpty()) {
            QSKIP("NODRIVE_TEST_SERVER_URL is not set (this test needs the throwaway server of tools/ci/agenda-server.sh)");
        }
        // This test never talks to the real Drive.
        QVERIFY2(!QUrl(_serverUrl).host().contains(QStringLiteral("nourax.org")), "refusing to contact the real Drive");
        _user = qEnvironmentVariable("NODRIVE_TEST_USER");
        _password = qEnvironmentVariable("NODRIVE_TEST_PASSWORD");
        _reportPath = qEnvironmentVariable("NODRIVE_TEST_REPORT");
        _outDir = qEnvironmentVariable("NODRIVE_TEST_OUTDIR");
        const auto mode = qEnvironmentVariable("NODRIVE_TEST_MODE");
        QVERIFY(!_user.isEmpty() && !_password.isEmpty() && !mode.isEmpty());
        QFile seedFile(qEnvironmentVariable("NODRIVE_TEST_SEED"));
        QVERIFY2(seedFile.open(QIODevice::ReadOnly), "seed.json is missing");
        _seed = QJsonDocument::fromJson(seedFile.readAll()).object();
        _account = makeAccount();

        if (mode == QLatin1String("auth-proof")) {
            authProof();
        } else if (mode == QLatin1String("contract")) {
            contract();
        } else if (mode == QLatin1String("read-ali")) {
            readAli();
        } else if (mode == QLatin1String("read-mara")) {
            readMara();
        } else if (mode == QLatin1String("read-sara")) {
            readSara();
        } else if (mode == QLatin1String("read-olaf")) {
            readOlaf();
        } else if (mode == QLatin1String("guard")) {
            guard();
        } else {
            QFAIL(qPrintable(QStringLiteral("unknown mode ") + mode));
        }
        finishPhase();
    }

private:
    // ---- the auth proof (design 5) ---------------------------------------------------------------------------------------------
    void authProof()
    {
        AccountTransport transport(_account);
        const auto website = seedId("projects", QStringLiteral("Website"));
        // NoGantt: the app password with Basic auth and the OCS header
        for (const auto &path : {QStringLiteral("my-tasks"), QStringLiteral("projects"), QStringLiteral("resources"), QStringLiteral("capacity-check"),
                 QStringLiteral("projects/%1/tasks").arg(website), QStringLiteral("projects/%1/milestones").arg(website), QStringLiteral("task-sync")}) {
            const auto r = get(&transport, gantt(path));
            check(QStringLiteral("auth NoGantt ") + path, parseOcs(r).ok(), QStringLiteral("HTTP %1").arg(r.status));
        }
        // Report only: the same call without the OCS header (the server's CSRF check).
        const auto without = get(&transport, gantt(QStringLiteral("my-tasks")), false);
        check(QStringLiteral("NoGantt without OCS-APIRequest (information)"), true, QStringLiteral("HTTP %1").arg(without.status));
        // Deck
        const auto boards = get(&transport, QStringLiteral("index.php/apps/deck/api/v1.0/boards?details=true"));
        check(QStringLiteral("auth Deck boards"), boards.status == 200, QStringLiteral("HTTP %1").arg(boards.status));
        const auto stacks = get(&transport, QStringLiteral("index.php/apps/deck/api/v1.0/boards/%1/stacks").arg(_seed.value(QLatin1String("board")).toInt()));
        check(QStringLiteral("auth Deck stacks"), stacks.status == 200, QStringLiteral("HTTP %1").arg(stacks.status));
        // CalDAV
        const auto principal = dav(&transport, "PROPFIND", QStringLiteral("remote.php/dav/principals/users/%1/").arg(_user), CalDavXml::propfindBody(CalendarReader::principalProps()), 0);
        check(QStringLiteral("auth CalDAV PROPFIND"), principal.status == 207, QStringLiteral("HTTP %1").arg(principal.status));
        const auto report = dav(&transport, "REPORT", QStringLiteral("remote.php/dav/calendars/%1/personal/").arg(_user),
            CalDavXml::eventQueryBody(QDateTime::currentDateTimeUtc().addDays(-30), QDateTime::currentDateTimeUtc().addDays(30)), 1);
        check(QStringLiteral("auth CalDAV REPORT"), report.status == 207, QStringLiteral("HTTP %1").arg(report.status));
        // The registry file
        const auto registry = get(&transport, QStringLiteral("remote.php/dav/files/%1/NoAgenda/agent-registry.json").arg(_user), false);
        check(QStringLiteral("auth registry file"), registry.status == 200, QStringLiteral("HTTP %1").arg(registry.status));
    }

    // ---- the contract: the live key sets against the committed fixtures --------------------------------------------------------
    QStringList missingKeys(const QJsonObject &live, const QJsonObject &fixtureObject) const
    {
        QStringList missing;
        for (auto it = fixtureObject.constBegin(); it != fixtureObject.constEnd(); ++it) {
            if (!live.contains(it.key())) {
                missing.append(it.key());
                continue;
            }
            const auto a = jsonTypeName(live.value(it.key()));
            const auto b = jsonTypeName(it.value());
            if (a != b && a != QLatin1String("null") && b != QLatin1String("null")) {
                missing.append(it.key() + QStringLiteral(" (type ") + a + QStringLiteral(" instead of ") + b + QLatin1Char(')'));
            }
        }
        return missing;
    }

    void contractEntry(const QString &name, const QJsonValue &liveData, const QJsonValue &fixtureData, QTextStream &keys, bool inner = false)
    {
        QJsonObject live;
        QJsonObject fix;
        if (liveData.isArray()) {
            if (liveData.toArray().isEmpty()) {
                check(QStringLiteral("contract ") + name, false, QStringLiteral("the live answer is empty: the seed is missing something"));
                return;
            }
            live = liveData.toArray().first().toObject();
        } else {
            live = liveData.toObject();
        }
        fix = fixtureData.isArray() ? fixtureData.toArray().first().toObject() : fixtureData.toObject();
        keys << name << '\t' << keySignature(live).join(QLatin1Char(' ')) << '\n';
        const auto missing = missingKeys(live, fix);
        check(QStringLiteral("contract ") + name, missing.isEmpty(), missing.isEmpty() ? QStringLiteral("%1 keys").arg(live.size()) : QStringLiteral("missing: ") + missing.join(QStringLiteral(", ")));
        Q_UNUSED(inner)
    }

    void contract()
    {
        AccountTransport transport(_account);
        QDir().mkpath(_outDir);
        QFile keysFile(_outDir + QStringLiteral("/contract-keys.txt"));
        QVERIFY(keysFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append));
        QTextStream keys(&keysFile);
        const auto website = seedId("projects", QStringLiteral("Website"));
        auto live = [&](const QString &path) { return parseOcs(get(&transport, gantt(path))).data; };

        const auto mine = live(QStringLiteral("my-tasks"));
        contractEntry(QStringLiteral("nogantt my-tasks"), mine, fixtureJson(QStringLiteral("gantt/my-tasks-mara.json")), keys);
        const auto myTasks = mine.toObject().value(QLatin1String("tasks")).toArray();
        if (!myTasks.isEmpty()) {
            contractEntry(QStringLiteral("nogantt my-tasks task"), myTasks.first(), fixtureJson(QStringLiteral("gantt/my-tasks-ali.json")).toObject().value(QLatin1String("tasks")).toArray().first(), keys);
        }
        contractEntry(QStringLiteral("nogantt projects"), live(QStringLiteral("projects")), fixtureJson(QStringLiteral("gantt/projects-mara.json")), keys);
        contractEntry(QStringLiteral("nogantt projects/{id}/tasks"), live(QStringLiteral("projects/%1/tasks").arg(website)), fixtureJson(QStringLiteral("gantt/tasks-1.json")), keys);
        const auto tasks = live(QStringLiteral("projects/%1/tasks").arg(website)).toArray();
        for (const auto &t : tasks) {
            const auto rows = t.toObject().value(QLatin1String("resources")).toArray();
            if (!rows.isEmpty()) {
                contractEntry(QStringLiteral("nogantt task resource row"), rows.first(), fixtureJson(QStringLiteral("gantt/tasks-1.json")).toArray().first().toObject().value(QLatin1String("resources")), keys);
                break;
            }
        }
        for (const auto &t : tasks) {
            const auto deps = t.toObject().value(QLatin1String("dependencies")).toArray();
            if (!deps.isEmpty()) {
                contractEntry(QStringLiteral("nogantt task dependency"), deps.first(), fixtureJson(QStringLiteral("gantt/tasks-1.json")).toArray().at(3).toObject().value(QLatin1String("dependencies")), keys);
                break;
            }
        }
        contractEntry(QStringLiteral("nogantt milestones"), live(QStringLiteral("projects/%1/milestones").arg(website)), fixtureJson(QStringLiteral("gantt/milestones-1.json")), keys);
        contractEntry(QStringLiteral("nogantt subprojects"), live(QStringLiteral("projects/%1/subprojects").arg(website)), fixtureJson(QStringLiteral("gantt/subprojects-1.json")), keys);
        contractEntry(QStringLiteral("nogantt resources"), live(QStringLiteral("resources")), fixtureJson(QStringLiteral("gantt/resources.json")), keys);
        contractEntry(QStringLiteral("nogantt shares"), live(QStringLiteral("projects/%1/shares").arg(website)), fixtureJson(QStringLiteral("gantt/shares-1.json")), keys);
        contractEntry(QStringLiteral("nogantt task-sync"), live(QStringLiteral("task-sync")), fixtureJson(QStringLiteral("gantt/task-sync-off.json")), keys);
        contractEntry(QStringLiteral("nogantt capacity-check"), live(QStringLiteral("capacity-check")), fixtureJson(QStringLiteral("gantt/capacity-mara.json")), keys);
        contractEntry(QStringLiteral("nogantt settings"), live(QStringLiteral("settings")), fixtureJson(QStringLiteral("gantt/settings.json")), keys);
        contractEntry(QStringLiteral("nogantt work-schedules"), live(QStringLiteral("work-schedules")), fixtureJson(QStringLiteral("gantt/work-schedules.json")), keys);

        // Deck
        const auto boards = QJsonDocument::fromJson(get(&transport, QStringLiteral("index.php/apps/deck/api/v1.0/boards?details=true")).body);
        contractEntry(QStringLiteral("deck boards"), boards.array(), fixtureJson(QStringLiteral("deck/boards-mara.json")), keys);
        const auto boardId = _seed.value(QLatin1String("board")).toInt();
        const auto stacks = QJsonDocument::fromJson(get(&transport, QStringLiteral("index.php/apps/deck/api/v1.0/boards/%1/stacks").arg(boardId)).body);
        contractEntry(QStringLiteral("deck stacks"), stacks.array(), fixtureJson(QStringLiteral("deck/stacks-10.json")), keys);
        const auto cards = stacks.array().first().toObject().value(QLatin1String("cards")).toArray();
        contractEntry(QStringLiteral("deck card"), cards, fixtureJson(QStringLiteral("deck/stacks-10.json")).toArray().first().toObject().value(QLatin1String("cards")), keys);
        for (const auto &c : cards) {
            const auto assigned = c.toObject().value(QLatin1String("assignedUsers")).toArray();
            if (!assigned.isEmpty()) {
                contractEntry(QStringLiteral("deck assignedUsers"), assigned, fixtureJson(QStringLiteral("deck/stacks-10.json")).toArray().first().toObject().value(QLatin1String("cards")).toArray().first().toObject().value(QLatin1String("assignedUsers")), keys);
                break;
            }
        }
        // Server and CalDAV properties that NoAgenda needs
        const auto user = parseOcs(get(&transport, QStringLiteral("ocs/v2.php/cloud/user?format=json")));
        check(QStringLiteral("contract cloud/user keys"), user.data.toObject().contains(QStringLiteral("id")) && user.data.toObject().contains(QStringLiteral("groups")), QStringLiteral("id and groups"));
        const auto caps = parseOcs(get(&transport, QStringLiteral("ocs/v1.php/cloud/capabilities?format=json")));
        check(QStringLiteral("contract capabilities deck.version"), !caps.data.toObject().value(QLatin1String("capabilities")).toObject().value(QLatin1String("deck")).toObject().value(QLatin1String("version")).toString().isEmpty(), QStringLiteral("deck version present"));
        const auto home = dav(&transport, "PROPFIND", QStringLiteral("remote.php/dav/calendars/%1/").arg(_user), CalDavXml::propfindBody(CalendarReader::calendarProps()), 1);
        const auto ms = CalDavXml::parseMultistatus(home.body);
        bool personalChecked = false;
        for (const auto &r : ms.responses) {
            if (r.href.endsWith(QLatin1String("/personal/"))) {
                personalChecked = true;
                for (const auto &p : {Dav::key(Dav::nsDav(), QStringLiteral("resourcetype")), Dav::key(Dav::nsDav(), QStringLiteral("displayname")),
                         Dav::key(Dav::nsDav(), QStringLiteral("sync-token")), Dav::key(Dav::nsCs(), QStringLiteral("getctag")),
                         Dav::key(Dav::nsCalDav(), QStringLiteral("supported-calendar-component-set")), Dav::key(Dav::nsOc(), QStringLiteral("owner-principal")),
                         Dav::key(Dav::nsDav(), QStringLiteral("current-user-privilege-set"))}) {
                    check(QStringLiteral("contract CalDAV property ") + p, r.props.contains(p), r.props.contains(p) ? QStringLiteral("present") : QStringLiteral("missing"));
                }
            }
        }
        check(QStringLiteral("contract CalDAV personal calendar listed"), personalChecked, QString());
    }

    // ---- Ali: staff -----------------------------------------------------------------------------------------------------------------------
    void readAli()
    {
        const auto cacheDir = _outDir + QStringLiteral("/live-ali-cache");
        QDir(cacheDir).removeRecursively();
        auto agenda = newAgenda(cacheDir);
        check(QStringLiteral("refresh finished"), refresh(agenda.get()), QString());
        const auto &state = agenda->state();
        for (const auto &s : state.statuses) {
            check(QStringLiteral("source %1 fresh").arg(s.key), s.state == SourceStatus::State::Fresh, QStringLiteral("state %1").arg(static_cast<int>(s.state)));
        }
        check(QStringLiteral("registry read"), state.registryStatus == AgentRegistry::Status::Ok && state.build.myAgents.size() == 3, QStringLiteral("%1 agents of mine").arg(state.build.myAgents.size()));
        check(QStringLiteral("person"), state.personName == QLatin1String("ali") || state.personName == QLatin1String("Ali"), state.personName);
        check(QStringLiteral("personal question is asked"), state.build.needsPersonalQuestion, QString());

        // NoGantt
        const auto *copy = byTitle(*agenda, QStringLiteral("Write homepage copy"));
        check(QStringLiteral("agent by NoGantt id"), copy && copy->workKind == WorkKind::MyAgent && copy->agent.displayText() == QLatin1String("Writer-Bot"), copy ? copy->agent.displayText() : QStringLiteral("(no row)"));
        check(QStringLiteral("my share of work"), copy && copy->myWorkHours == 2.0, copy ? QString::number(copy->myWorkHours) : QString());
        const auto *demo = byTitle(*agenda, QStringLiteral("Prepare client demo"));
        check(QStringLiteral("Self"), demo && demo->agent.isSelf && demo->workKind == WorkKind::Self, QString());
        const auto *blocked = byTitle(*agenda, QStringLiteral("Publish banners"));
        check(QStringLiteral("blocked by a late task"), blocked && blocked->blocked, blocked ? blocked->blockedBy : QStringLiteral("(no row)"));
        const auto *two = byTitle(*agenda, QStringLiteral("Two agents"));
        check(QStringLiteral("two agents flagged"), two && hasProblem(*two, ProblemFlag::Kind::TwoAgents), QString());
        const auto *ghost = byTitle(*agenda, QStringLiteral("Ghost agent task"));
        check(QStringLiteral("unknown agent flagged"), ghost && ghost->agent.unknown && hasProblem(*ghost, ProblemFlag::Kind::UnknownAgent), QString());
        const auto *send = byTitle(*agenda, QStringLiteral("Send invoice"));
        check(QStringLiteral("due today"), send && send->dueToday, QString());
        check(QStringLiteral("milestone"), byTitle(*agenda, QStringLiteral("Launch")) != nullptr, QString());
        for (const char *foreign : {"Banner design", "Orphan writing", "Review sitemap", "Temp work", "Sara's internal task"}) {
            check(QStringLiteral("not mine: %1").arg(QLatin1String(foreign)), byTitle(*agenda, QString::fromUtf8(foreign)) == nullptr, QString());
        }
        // NoBoard
        const auto *supplier = byTitle(*agenda, QStringLiteral("Call supplier"));
        check(QStringLiteral("card agent from the label"), supplier && supplier->workKind == WorkKind::MyAgent && supplier->agent.displayText() == QLatin1String("Writer-Bot"), QString());
        check(QStringLiteral("card due today"), byTitle(*agenda, QStringLiteral("Pay invoice")) && byTitle(*agenda, QStringLiteral("Pay invoice"))->dueToday, QString());
        const auto *idea = byTitle(*agenda, QStringLiteral("Someday idea"));
        check(QStringLiteral("card without a date"), idea && !idea->hasDate(), QString());
        const auto *stranger = byTitle(*agenda, QStringLiteral("Stranger bot"));
        check(QStringLiteral("unknown label flagged"), stranger && stranger->agent.unknown, QString());
        check(QStringLiteral("done card hidden"), byTitle(*agenda, QStringLiteral("Old task")) == nullptr, QString());
        check(QStringLiteral("other people's cards hidden"), byTitle(*agenda, QStringLiteral("Fix footer")) == nullptr && byTitle(*agenda, QStringLiteral("Unassigned backlog")) == nullptr, QString());
        if (_seed.value(QLatin1String("flags")).toObject().value(QLatin1String("deck_link")).toBool()) {
            const auto *task = byTitle(*agenda, QStringLiteral("Landing page build"));
            check(QStringLiteral("linked task and card are one row"), task && task->boardBadge && byTitle(*agenda, QStringLiteral("Landing page card")) == nullptr, QString());
        }
        // Calendars and to-dos
        check(QStringLiteral("event"), byTitle(*agenda, QStringLiteral("Dentist")) != nullptr && byTitle(*agenda, QStringLiteral("Client call")) != nullptr, QString());
        const auto *standup = byTitle(*agenda, QStringLiteral("Stand-up"));
        check(QStringLiteral("repeating event"), standup && standup->repeating, QString());
        const auto *lunch = byTitle(*agenda, QStringLiteral("Maybe lunch"));
        check(QStringLiteral("free event does not block"), lunch && lunch->transparent, QString());
        const auto *review = byTitle(*agenda, QStringLiteral("Writer-Bot review"));
        check(QStringLiteral("agent id on an event survives the server"), review && review->agent.displayText() == QLatin1String("Writer-Bot"), review ? review->agent.displayText() : QStringLiteral("(no row)"));
        const auto *tagOnly = byTitle(*agenda, QStringLiteral("Tag only"));
        check(QStringLiteral("agent from the category"), tagOnly && tagOnly->agent.displayText() == QLatin1String("Writer-Bot"), QString());
        const auto *invite = byTitle(*agenda, QStringLiteral("Planning meeting"));
        check(QStringLiteral("invitation is Self"), invite && invite->agent.isSelf, QString());
        int plannings = 0;
        for (const auto &i : state.items) {
            plannings += i.title == QLatin1String("Planning meeting") ? 1 : 0;
        }
        check(QStringLiteral("invitation in two calendars is one row"), plannings == 1, QString::number(plannings));
        check(QStringLiteral("to-do"), byTitle(*agenda, QStringLiteral("Renew passport")) != nullptr, QString());
        const auto *late = byTitle(*agenda, QStringLiteral("Late bill"));
        check(QStringLiteral("late to-do"), late && late->late && late->lateDays == 2, QString());
        const auto *someday = byTitle(*agenda, QStringLiteral("Someday"));
        check(QStringLiteral("to-do without a date"), someday && !someday->hasDate(), QString());
        const auto *draft = byTitle(*agenda, QStringLiteral("Draft report"));
        check(QStringLiteral("to-do agent from the category"), draft && draft->agent.displayText() == QLatin1String("Writer-Bot"), QString());
        check(QStringLiteral("completed to-do hidden"), byTitle(*agenda, QStringLiteral("Completed already")) == nullptr, QString());
        if (_seed.value(QLatin1String("flags")).toObject().value(QLatin1String("team_shared")).toBool()) {
            const auto *lunchTeam = byTitle(*agenda, QStringLiteral("Team lunch"));
            check(QStringLiteral("tentative team event"), lunchTeam && lunchTeam->tentative, QString());
            check(QStringLiteral("team calendar: what I organise"), byTitle(*agenda, QStringLiteral("Ali's workshop")) != nullptr, QString());
            check(QStringLiteral("team calendar: declined and others' events hidden"),
                byTitle(*agenda, QStringLiteral("Budget talk")) == nullptr && byTitle(*agenda, QStringLiteral("Mara and Olaf")) == nullptr, QString());
        }
        // Sources
        bool deckCalendarHidden = false;
        bool teamCalendar = false;
        for (const auto &s : state.sources) {
            if (s.type == SourceEntry::Type::Calendar && s.key.startsWith(QLatin1String("app-generated--deck--"))) {
                deckCalendarHidden = s.autoHidden;
            }
            if (s.type == SourceEntry::Type::Calendar && s.mode == QLatin1String("team")) {
                teamCalendar = true;
            }
        }
        check(QStringLiteral("the board's calendar is hidden automatically"), deckCalendarHidden, QString());
        check(QStringLiteral("a shared calendar is Team"), teamCalendar || !_seed.value(QLatin1String("flags")).toObject().value(QLatin1String("team_shared")).toBool(), QString());
        check(QStringLiteral("version guard is quiet on the tested versions"), !state.build.ganttReadOnlyByGuard && !state.build.boardReadOnlyByGuard, state.build.ganttContractProblem);

        // The next steps use the cache for the live screenshots: answer the one-time question first.
        agenda->answerPersonalQuestion({QStringLiteral("personal")}, false);
        check(QStringLiteral("work calendar after the answer"), byTitle(*agenda, QStringLiteral("Client call")) && byTitle(*agenda, QStringLiteral("Client call"))->category == Category::Work, QString());
        // A second refresh with unchanged data keeps the same rows (sync tokens).
        const auto before = agenda->state().items.size();
        check(QStringLiteral("second refresh"), refresh(agenda.get()), QString());
        check(QStringLiteral("same rows after a second refresh"), agenda->state().items.size() == before, QStringLiteral("%1 -> %2").arg(before).arg(agenda->state().items.size()));
        QFile summary(_outDir + QStringLiteral("/rows-ali.txt"));
        if (summary.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&summary);
            for (const auto &i : agenda->state().items) {
                out << i.key << '\t' << static_cast<int>(i.workKind) << '\t' << i.problems.size() << '\n';
            }
        }
    }

    // ---- Mara: manager ---------------------------------------------------------------------------------------------------------------------
    void readMara()
    {
        QTemporaryDir cache;
        auto agenda = newAgenda(cache.path() + QStringLiteral("/noagenda/mara"));
        check(QStringLiteral("refresh finished"), refresh(agenda.get()), QString());
        const auto &state = agenda->state();
        check(QStringLiteral("she runs what she planned"), state.build.anyOversee, QString());
        auto team = [&](const QString &title, ProblemFlag::Kind kind) {
            const auto *item = byTitle(*agenda, title);
            return item && item->workKind == WorkKind::Team && hasProblem(*item, kind);
        };
        const auto *banner = byTitle(*agenda, QStringLiteral("Banner design"));
        check(QStringLiteral("late task of Sara with her agent (named in NoGantt)"),
            banner && banner->personName == QLatin1String("Sara") && banner->late && banner->agent.displayText() == QLatin1String("Design-Bot"),
            banner ? banner->agent.displayText() : QStringLiteral("(no row)"));
        check(QStringLiteral("blocked task"), team(QStringLiteral("Publish banners"), ProblemFlag::Kind::Blocked), QString());
        check(QStringLiteral("role only: not assigned to a person"), team(QStringLiteral("Review sitemap"), ProblemFlag::Kind::NotAssigned), QString());
        check(QStringLiteral("agent with no person"), team(QStringLiteral("Orphan writing"), ProblemFlag::Kind::AgentNoPerson), QString());
        check(QStringLiteral("agent paired with someone else"), team(QStringLiteral("Cross pairing"), ProblemFlag::Kind::AgentWrongPerson), QString());
        check(QStringLiteral("two agents for one person"), team(QStringLiteral("Two agents"), ProblemFlag::Kind::TwoAgents), QString());
        check(QStringLiteral("not linked to an account"), team(QStringLiteral("Temp work"), ProblemFlag::Kind::NotLinked), QString());
        bool notShared = false;
        for (const auto &i : state.items) {
            notShared = notShared || hasProblem(i, ProblemFlag::Kind::NotShared);
        }
        check(QStringLiteral("a person in a project not shared with them"), notShared, QString());
        check(QStringLiteral("a card that is late"), team(QStringLiteral("Fix footer"), ProblemFlag::Kind::Late), QString());
        check(QStringLiteral("a card with nobody on it"), team(QStringLiteral("Unassigned backlog"), ProblemFlag::Kind::NotAssigned), QString());
        check(QStringLiteral("a card due today"), team(QStringLiteral("Pay invoice"), ProblemFlag::Kind::DueToday), QString());
        // her own rows: none of the staff's work is hers
        int mine = 0;
        for (const auto &i : state.items) {
            mine += i.workKind != WorkKind::Team ? 1 : 0;
        }
        check(QStringLiteral("only team rows"), mine == 0, QString::number(mine));
        // Mara never sees Ali's personal events
        check(QStringLiteral("no personal event of someone else"), byTitle(*agenda, QStringLiteral("Dentist")) == nullptr, QString());
    }

    // ---- Sara: viewer, invalid registry, copy of her tasks switched on ------------------------------------------------------------------------
    void readSara()
    {
        QTemporaryDir cache;
        auto agenda = newAgenda(cache.path() + QStringLiteral("/noagenda/sara"));
        check(QStringLiteral("refresh finished"), refresh(agenda.get()), QString());
        const auto &state = agenda->state();
        check(QStringLiteral("an invalid agent list is reported"), state.registryStatus == AgentRegistry::Status::Invalid, QString());
        const auto *banner = byTitle(*agenda, QStringLiteral("Banner design"));
        check(QStringLiteral("her task is shown"), banner != nullptr, QString());
        check(QStringLiteral("without a readable list the agent is not guessed"), banner && banner->agent.unknown, banner ? banner->agent.displayText() : QString());
        check(QStringLiteral("a viewer cannot change even her own task"), banner && banner->readOnly, QString());
        check(QStringLiteral("a project not shared with her is not there"), byTitle(*agenda, QStringLiteral("Sara's internal task")) == nullptr, QString());
        if (_seed.value(QLatin1String("flags")).toObject().value(QLatin1String("task_sync")).toBool()) {
            check(QStringLiteral("the copy of her tasks is hidden and she is told"), !state.build.taskCopyCalendarUri.isEmpty(), state.build.taskCopyCalendarUri);
            bool hidden = false;
            for (const auto &s : state.sources) {
                hidden = hidden || (s.type == SourceEntry::Type::Calendar && s.key == state.build.taskCopyCalendarUri && s.autoHidden);
            }
            check(QStringLiteral("the copy calendar is hidden automatically"), hidden, QString());
        }
    }

    // ---- Olaf: another organisation, nothing shared with him ------------------------------------------------------------------------------------
    void readOlaf()
    {
        QTemporaryDir cache;
        auto agenda = newAgenda(cache.path() + QStringLiteral("/noagenda/olaf"));
        check(QStringLiteral("refresh finished"), refresh(agenda.get()), QString());
        const auto &state = agenda->state();
        check(QStringLiteral("no agent list: missing"), state.registryStatus == AgentRegistry::Status::Missing, QString());
        const auto *gantt = state.status(QStringLiteral("gantt"));
        check(QStringLiteral("not linked to a person in NoGantt"), gantt && gantt->state == SourceStatus::State::NotLinked, gantt ? QString::number(static_cast<int>(gantt->state)) : QString());
        for (const auto &i : state.items) {
            check(QStringLiteral("nothing of another organisation: %1").arg(i.key.left(12)), i.home != Home::Gantt && i.home != Home::Board, i.key);
            check(QStringLiteral("no name of someone else's agent: %1").arg(i.key.left(12)),
                !i.title.contains(QStringLiteral("Bot")) && !i.agent.displayText().contains(QStringLiteral("Writer-Bot")) && !i.agent.displayText().contains(QStringLiteral("Design-Bot")), QString());
        }
        check(QStringLiteral("none of Ali's events"), byTitle(*agenda, QStringLiteral("Dentist")) == nullptr && byTitle(*agenda, QStringLiteral("Client call")) == nullptr, QString());
    }

    // ---- the version guard against the real server --------------------------------------------------------------------------------------------------
    void guard()
    {
        // A newer Deck (the capability says 1.20.0) and a changed NoGantt answer (a key is renamed).
        auto inner = std::make_unique<AccountTransport>(_account);
        auto rewriting = std::make_unique<RewritingTransport>(std::move(inner), [](const HttpRequest &request, const HttpResult &result) -> QByteArray {
            const auto path = request.url.path();
            if (path.endsWith(QLatin1String("/cloud/capabilities"))) {
                auto doc = QJsonDocument::fromJson(result.body);
                auto root = doc.object();
                auto ocs = root.value(QLatin1String("ocs")).toObject();
                auto data = ocs.value(QLatin1String("data")).toObject();
                auto caps = data.value(QLatin1String("capabilities")).toObject();
                auto deck = caps.value(QLatin1String("deck")).toObject();
                deck.insert(QStringLiteral("version"), QStringLiteral("1.20.0"));
                caps.insert(QStringLiteral("deck"), deck);
                data.insert(QStringLiteral("capabilities"), caps);
                ocs.insert(QStringLiteral("data"), data);
                root.insert(QStringLiteral("ocs"), ocs);
                return QJsonDocument(root).toJson(QJsonDocument::Compact);
            }
            if (path.endsWith(QLatin1String("/milestones"))) {
                auto text = QString::fromUtf8(result.body);
                text.replace(QStringLiteral("\"milestone_date\""), QStringLiteral("\"date_of_milestone\""));
                return text.toUtf8();
            }
            return QByteArray();
        });
        QTemporaryDir cache;
        auto agenda = newAgenda(cache.path() + QStringLiteral("/noagenda/guard"), std::move(rewriting));
        check(QStringLiteral("refresh finished"), refresh(agenda.get()), QString());
        const auto &state = agenda->state();
        check(QStringLiteral("a newer Deck: cards are read only"), state.build.boardReadOnlyByGuard, QString());
        const auto *card = byTitle(*agenda, QStringLiteral("Call supplier"));
        check(QStringLiteral("a card is shown, read only, with Open in NoBoard"), card && card->versionReadOnly, QString());
        check(QStringLiteral("a changed NoGantt answer: tasks are read only"), state.build.ganttReadOnlyByGuard, state.build.ganttContractProblem);
        const auto *task = byTitle(*agenda, QStringLiteral("Write homepage copy"));
        check(QStringLiteral("a task is shown, read only"), task && task->versionReadOnly, QString());
    }
};

QTEST_GUILESS_MAIN(TestNoDriveAgendaServer)
#include "testnodriveagendaserver.moc"
