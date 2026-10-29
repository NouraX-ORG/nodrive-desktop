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
//                                  | edit-calendar | conflict | edit-gantt | edit-deck | offline (the edit step)
//   NODRIVE_TEST_REPORT            file to which "| check | PASS/FAIL | detail |" lines are appended (no values)
//   NODRIVE_TEST_SEED              seed.json written by the seed script
//   NODRIVE_TEST_OUTDIR            folder for the contract key sets and the cache for the live screenshots

#include "nodrive/noagenda/edittestsupport.h"

#include "gui/nourax/noagenda/edit/editability.h"
#include "gui/nourax/noagenda/edit/editservice.h"
#include "gui/nourax/noagenda/net/accounttransport.h"
#include "gui/nourax/noagenda/ocs/ocsjson.h"

#include "accessmanager.h"
#include "account.h"
#include "creds/abstractcredentials.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>

using namespace AgendaTest;
using OCC::AbstractCredentials;
using OCC::AccessManager;
using OCC::Account;
using OCC::AccountPtr;

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
        } else if (mode == QLatin1String("edit-calendar")) {
            editCalendar();
        } else if (mode == QLatin1String("conflict")) {
            conflictPhase();
        } else if (mode == QLatin1String("edit-gantt")) {
            editGantt();
        } else if (mode == QLatin1String("edit-deck")) {
            editDeck();
        } else if (mode == QLatin1String("offline")) {
            offlinePhase();
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
        bool calendarChecked = false;
        for (const auto &r : ms.responses) {
            if (!r.props.value(Dav::key(Dav::nsDav(), QStringLiteral("resourcetype"))).has(QStringLiteral("calendar"))) {
                continue;
            }
            calendarChecked = true;
            for (const auto &p : {Dav::key(Dav::nsDav(), QStringLiteral("resourcetype")), Dav::key(Dav::nsDav(), QStringLiteral("displayname")),
                     Dav::key(Dav::nsDav(), QStringLiteral("sync-token")), Dav::key(Dav::nsCs(), QStringLiteral("getctag")),
                     Dav::key(Dav::nsCalDav(), QStringLiteral("supported-calendar-component-set")), Dav::key(Dav::nsOc(), QStringLiteral("owner-principal")),
                     Dav::key(Dav::nsDav(), QStringLiteral("current-user-privilege-set"))}) {
                check(QStringLiteral("contract CalDAV property ") + p, r.props.contains(p), r.props.contains(p) ? QStringLiteral("present") : QStringLiteral("missing"));
            }
            break;
        }
        check(QStringLiteral("contract CalDAV a calendar is listed"), calendarChecked, QString());
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
        // Design 4.4, row 1: Writer-Bot (Ali's agent) is on "Orphan writing" without a person. The task is not Ali's, but he is told.
        const auto *orphan = findTitle(state.items, QStringLiteral("Writer-Bot is on \"Orphan writing\" without you"));
        check(QStringLiteral("agent without its person: the person is told"), orphan && orphan->problemOnly && !orphan->hasDate() && hasProblem(*orphan, ProblemFlag::Kind::AgentNoPerson),
            orphan ? QStringLiteral("(row found)") : QStringLiteral("(no row)"));
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
        // the staff's work is not hers: every project and board row is a team row
        int mine = 0;
        for (const auto &i : state.items) {
            mine += ((i.home == Home::Gantt || i.home == Home::Board) && i.workKind != WorkKind::Team && i.sourceName != QLatin1String("Team board") ? 1 : 0);
        }
        check(QStringLiteral("no project or board row of the staff is hers"), mine == 0, QString::number(mine));
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
        int foreign = 0;
        int names = 0;
        for (const auto &i : state.items) {
            // The cards of his own welcome board are his; nothing of the other organisation's board or projects is.
            foreign += (i.home == Home::Gantt || i.sourceName == QLatin1String("Team board")) ? 1 : 0;
            names += (i.agent.displayText().contains(QStringLiteral("Writer-Bot")) || i.agent.displayText().contains(QStringLiteral("Design-Bot"))
                         || i.title.contains(QStringLiteral("Writer-Bot")) || i.title.contains(QStringLiteral("Design-Bot")))
                ? 1
                : 0;
        }
        check(QStringLiteral("nothing of the other organisation's projects and board"), foreign == 0, QString::number(foreign));
        check(QStringLiteral("no name of someone else's agent"), names == 0, QString::number(names));
        check(QStringLiteral("none of Ali's events"), byTitle(*agenda, QStringLiteral("Dentist")) == nullptr && byTitle(*agenda, QStringLiteral("Client call")) == nullptr, QString());
    }

    // ---- the version guard against the real server --------------------------------------------------------------------------------------------------
    // ---- the edit phases (design section 9; plans/noagenda-desktop-plan.md, 2.8) ----------------------------------------------------
    // Each phase checks the state of the server directly (as the same user), not only what the client says.

    HttpResult call(AccountTransport *transport, const HttpRequest &request)
    {
        HttpResult out;
        bool done = false;
        transport->send(request, [&](const HttpResult &r) {
            out = r;
            done = true;
        });
        for (int i = 0; i < 600 && !done; ++i) {
            QTest::qWait(100);
        }
        return out;
    }

    HttpResult putText(AccountTransport *transport, const QString &path, const QByteArray &body, const QByteArray &contentType, const QByteArray &ifMatch = QByteArray())
    {
        auto request = withBody(makeRequest("PUT", transport->url(path)), body, contentType);
        if (!ifMatch.isEmpty()) {
            request = withHeader(request, "If-Match", ifMatch);
        }
        return call(transport, request);
    }

    HttpResult sendJson(AccountTransport *transport, const QByteArray &verb, const QString &path, const QJsonObject &body)
    {
        auto request = makeRequest(verb, transport->url(path));
        request.headers.append({QByteArrayLiteral("OCS-APIRequest"), QByteArrayLiteral("true")});
        request.headers.append({QByteArrayLiteral("Accept"), QByteArrayLiteral("application/json")});
        request = withBody(request, QJsonDocument(body).toJson(QJsonDocument::Compact), QByteArrayLiteral("application/json"));
        return call(transport, request);
    }

    /// Reads a calendar object as the same user (the raw text and the ETag).
    HttpResult object(AccountTransport *transport, const QString &calendar, const QString &name)
    {
        return get(transport, QStringLiteral("remote.php/dav/calendars/%1/%2/%3").arg(_user, calendar, name), false);
    }

    /// Runs one command through the edit service of the agenda and waits for ITS answer. A server error (the throwaway
    /// server uses SQLite and can be busy) leaves the command waiting; it is tried again, as NoAgenda does at its next refresh.
    EditResult run(AccountAgenda *agenda, const EditCommand &command)
    {
        QSignalSpy spy(agenda->edit(), &EditService::finished);
        const QString id = agenda->edit()->submit(command);
        qsizetype seen = 0;
        int tries = 0;
        for (int i = 0; i < 1500; ++i) {
            for (; seen < spy.size(); ++seen) {
                const auto args = spy.at(seen);
                if (qvariant_cast<EditCommand>(args.at(0)).id != id) {
                    continue;
                }
                const auto result = qvariant_cast<EditResult>(args.at(1));
                if (result.outcome != EditResult::Outcome::Failed || ++tries > 3) {
                    return result;
                }
                QTest::qWait(1500);
                agenda->edit()->retry();
            }
            spy.wait(100);
        }
        return EditResult();
    }

    static QString why(const EditResult &r)
    {
        return QStringLiteral("%1 (HTTP %2)").arg(r.message).arg(r.httpStatus);
    }

    /// Lets the refresh that the edit started finish, then reads everything again.
    void settle(AccountAgenda *agenda)
    {
        for (int i = 0; i < 300 && agenda->isRefreshing(); ++i) {
            QTest::qWait(100);
        }
        QTest::qWait(300);
        for (int i = 0; i < 300 && agenda->isRefreshing(); ++i) {
            QTest::qWait(100);
        }
        refresh(agenda);
    }

    /// The id of the waiting "Not sent" entry of one item (refused changes of other items stay in the list until a click).
    static QString conflictOf(AccountAgenda *agenda, const QString &itemKey)
    {
        for (const auto &c : agenda->edit()->queue().conflicts()) {
            if (c.itemKey == itemKey) {
                return c.id;
            }
        }
        return QString();
    }

    static int responsesIn(const HttpResult &r)
    {
        return static_cast<int>(QString::fromUtf8(r.body).count(QRegularExpression(QStringLiteral("<(?:\\w+:)?response[ >]"))));
    }

    /// The places where the server may list the deleted calendar objects of the person (tried in turn).
    QStringList trashPaths() const
    {
        return {QStringLiteral("remote.php/dav/calendars/%1/trashbin/objects/").arg(_user), QStringLiteral("remote.php/dav/trashbin/%1/trash/").arg(_user),
            QStringLiteral("remote.php/dav/trashbin/%1/objects/").arg(_user), QStringLiteral("remote.php/dav/calendars/%1/trashbin/").arg(_user)};
    }

    HttpResult trashBin(AccountTransport *transport, const QString &path)
    {
        return dav(transport, "PROPFIND", path, QByteArrayLiteral("<?xml version=\"1.0\"?><d:propfind xmlns:d=\"DAV:\"><d:prop><d:displayname/></d:prop></d:propfind>"), 1);
    }

    static QString isoDay(int offset)
    {
        return QDateTime::currentDateTimeUtc().date().addDays(offset).toString(Qt::ISODate);
    }

    /// The first working day (Monday to Friday) from today plus `offset`: NoGantt moves a date on a weekend to Monday.
    static QString isoWorkday(int offset)
    {
        auto d = QDateTime::currentDateTimeUtc().date().addDays(offset);
        while (d.dayOfWeek() > 5) {
            d = d.addDays(1);
        }
        return d.toString(Qt::ISODate);
    }

    static QString isoUtc(int dayOffset, int hour)
    {
        return QDateTime(QDateTime::currentDateTimeUtc().date().addDays(dayOffset), QTime(hour, 0), QTimeZone::UTC).toString(Qt::ISODate);
    }

    QJsonObject taskOf(AccountTransport *transport, int project, int task)
    {
        const auto r = get(transport, gantt(QStringLiteral("projects/%1/tasks").arg(project)));
        for (const auto &t : parseOcs(r).data.toArray()) {
            if (t.toObject().value(QStringLiteral("id")).toInt() == task) {
                return t.toObject();
            }
        }
        return QJsonObject();
    }

    static QList<int> resourceIds(const QJsonObject &task)
    {
        QList<int> ids;
        for (const auto &r : task.value(QStringLiteral("resources")).toArray()) {
            ids.append(r.toObject().value(QStringLiteral("resource_id")).toInt());
        }
        return ids;
    }

    QJsonObject cardOf(AccountTransport *transport, int board, int stack, int card)
    {
        const auto r = get(transport, QStringLiteral("index.php/apps/deck/api/v1.0/boards/%1/stacks/%2/cards/%3").arg(board).arg(stack).arg(card));
        return QJsonDocument::fromJson(r.body).object();
    }

    int firstStackOf(AccountTransport *transport, int board)
    {
        const auto r = get(transport, QStringLiteral("index.php/apps/deck/api/v1.0/boards/%1/stacks").arg(board));
        const auto stacks = QJsonDocument::fromJson(r.body).array();
        return stacks.isEmpty() ? 0 : stacks.first().toObject().value(QStringLiteral("id")).toInt();
    }

    // ---- edit-calendar -------------------------------------------------------------------------------------------------------------------------
    void editCalendar()
    {
        QTemporaryDir cache;
        auto agenda = newAgenda(cache.path() + QStringLiteral("/noagenda/edit-calendar"));
        AccountTransport transport(_account);
        check(QStringLiteral("refresh finished"), refresh(agenda.get()), QString());

        // The event is first given lines that a plain editor would drop: they must survive the edit byte for byte.
        auto dentistObject = object(&transport, QStringLiteral("personal"), QStringLiteral("dentist.ics"));
        QString text = QString::fromUtf8(dentistObject.body);
        const QString eol = text.contains(QStringLiteral("\r\n")) ? QStringLiteral("\r\n") : QStringLiteral("\n");
        text.replace(QStringLiteral("END:VEVENT"), QStringLiteral("X-CUSTOM-KEEP:keepme") + eol + QStringLiteral("BEGIN:VALARM") + eol + QStringLiteral("ACTION:DISPLAY") + eol + QStringLiteral("TRIGGER:-PT15M") + eol
                + QStringLiteral("DESCRIPTION:Reminder") + eol + QStringLiteral("END:VALARM") + eol + QStringLiteral("END:VEVENT"));
        const auto prepared = putText(&transport, QStringLiteral("remote.php/dav/calendars/%1/personal/dentist.ics").arg(_user), text.toUtf8(), QByteArrayLiteral("text/calendar; charset=utf-8"), dentistObject.headers.value("etag"));
        check(QStringLiteral("prepared event with an alarm and an unknown property"), prepared.ok(), QStringLiteral("HTTP %1").arg(prepared.status));
        settle(agenda.get());

        const auto *dentist = byTitle(*agenda, QStringLiteral("Dentist"));
        check(QStringLiteral("event row found"), dentist != nullptr, QString());
        if (!dentist) {
            return;
        }
        check(QStringLiteral("event may be edited"), EditRules::of(*dentist).canEdit, QString());
        const auto before = object(&transport, QStringLiteral("personal"), QStringLiteral("dentist.ics"));
        auto result = run(agenda.get(), EditCommands::calendarPatch(*dentist, QJsonObject{{QStringLiteral("title"), QStringLiteral("Dentist (moved)")},
            {QStringLiteral("start"), isoUtc(0, 13)}, {QStringLiteral("end"), isoUtc(0, 14)}}));
        check(QStringLiteral("event: title and time sent"), result.done(), why(result));
        auto after = object(&transport, QStringLiteral("personal"), QStringLiteral("dentist.ics"));
        const QString afterText = QString::fromUtf8(after.body);
        check(QStringLiteral("event: server has the new title"), afterText.contains(QStringLiteral("SUMMARY:Dentist (moved)")), QString());
        check(QStringLiteral("event: server has the new time"), afterText.contains(QStringLiteral("T130000Z")) && afterText.contains(QStringLiteral("T140000Z")), QString());
        check(QStringLiteral("event: ETag changed"), after.headers.value("etag") != before.headers.value("etag"), QString());
        check(QStringLiteral("event: unknown property and alarm survive"),
            afterText.contains(QStringLiteral("X-CUSTOM-KEEP:keepme")) && afterText.contains(QStringLiteral("TRIGGER:-PT15M")) && afterText.contains(QStringLiteral("BEGIN:VALARM")), QString());
        check(QStringLiteral("event: uid kept"), afterText.contains(QStringLiteral("UID:ci-dentist")), QString());

        // My agent on an event: the id and the tag, then Self removes both.
        settle(agenda.get());
        dentist = byTitle(*agenda, QStringLiteral("Dentist (moved)"));
        check(QStringLiteral("event row follows the edit"), dentist != nullptr, QString());
        if (!dentist) {
            return;
        }
        const QJsonObject agentOn{{QStringLiteral("agent"), QJsonObject{{QStringLiteral("id"), QStringLiteral("AG-0001")}, {QStringLiteral("tag"), QStringLiteral("agent:AG-0001")},
            {QStringLiteral("removeTags"), QJsonArray{QStringLiteral("agent:AG-0001"), QStringLiteral("agent:AG-0003")}}}}};
        result = run(agenda.get(), EditCommands::calendarPatch(*dentist, agentOn));
        check(QStringLiteral("event: agent set"), result.done(), why(result));
        const QString withAgent = QString::fromUtf8(object(&transport, QStringLiteral("personal"), QStringLiteral("dentist.ics")).body);
        check(QStringLiteral("event: server has the agent id"), withAgent.contains(QStringLiteral("X-NOURAX-AGENT-ID:AG-0001")), QString());
        check(QStringLiteral("event: server has the agent tag"), withAgent.contains(QStringLiteral("agent:AG-0001")), QString());
        settle(agenda.get());
        dentist = byTitle(*agenda, QStringLiteral("Dentist (moved)"));
        check(QStringLiteral("event: the row now names the agent"), dentist && dentist->workKind == WorkKind::MyAgent && dentist->agent.displayText() == QLatin1String("Writer-Bot"), QString());
        if (!dentist) {
            return;
        }
        result = run(agenda.get(), EditCommands::calendarPatch(*dentist, QJsonObject{{QStringLiteral("agent"), QJsonObject{{QStringLiteral("id"), QString()}, {QStringLiteral("tag"), QString()},
            {QStringLiteral("removeTags"), QJsonArray{QStringLiteral("agent:AG-0001"), QStringLiteral("agent:AG-0003")}}}}}));
        check(QStringLiteral("event: back to Self"), result.done(), why(result));
        const QString withoutAgent = QString::fromUtf8(object(&transport, QStringLiteral("personal"), QStringLiteral("dentist.ics")).body);
        check(QStringLiteral("event: Self removes the id and the tag"), !withoutAgent.contains(QStringLiteral("X-NOURAX-AGENT-ID")) && !withoutAgent.contains(QStringLiteral("agent:AG-0001")), QString());
        check(QStringLiteral("event: the alarm still survives"), withoutAgent.contains(QStringLiteral("X-CUSTOM-KEEP:keepme")) && withoutAgent.contains(QStringLiteral("TRIGGER:-PT15M")), QString());

        // A to-do: done and not done again.
        settle(agenda.get());
        const auto *renew = byTitle(*agenda, QStringLiteral("Renew passport"));
        check(QStringLiteral("to-do row found"), renew != nullptr, QString());
        if (renew) {
            AgendaItem todo = *renew;
            result = run(agenda.get(), EditCommands::calendarPatch(todo, QJsonObject{{QStringLiteral("done"), true}}));
            check(QStringLiteral("to-do: ticked done"), result.done(), why(result));
            const auto doneObject = object(&transport, QStringLiteral("personal"), QStringLiteral("renew.ics"));
            const QString doneText = QString::fromUtf8(doneObject.body);
            check(QStringLiteral("to-do: server has COMPLETED, 100 percent and the status"),
                doneText.contains(QStringLiteral("STATUS:COMPLETED")) && doneText.contains(QStringLiteral("COMPLETED:")) && doneText.contains(QStringLiteral("PERCENT-COMPLETE:100")), QString());
            todo.version = QString::fromLatin1(doneObject.headers.value("etag"));
            result = run(agenda.get(), EditCommands::calendarPatch(todo, QJsonObject{{QStringLiteral("done"), false}}));
            check(QStringLiteral("to-do: ticked not done"), result.done(), why(result));
            const QString undoneText = QString::fromUtf8(object(&transport, QStringLiteral("personal"), QStringLiteral("renew.ics")).body);
            check(QStringLiteral("to-do: server has NEEDS-ACTION and no COMPLETED"),
                undoneText.contains(QStringLiteral("STATUS:NEEDS-ACTION")) && !undoneText.contains(QStringLiteral("\nCOMPLETED:")) && !undoneText.contains(QStringLiteral("PERCENT-COMPLETE")), QString());
        }

        // Create an event and a to-do, then delete the event (it goes to the calendar's trash bin).
        const QString createdUid = QStringLiteral("ci-edit-created");
        result = run(agenda.get(), EditCommands::calendarCreate(QStringLiteral("work"), false, createdUid,
            QJsonObject{{QStringLiteral("title"), QStringLiteral("Created by NoAgenda")}, {QStringLiteral("start"), isoUtc(1, 9)}, {QStringLiteral("end"), isoUtc(1, 10)},
                {QStringLiteral("agent"), QJsonObject{{QStringLiteral("id"), QStringLiteral("AG-0001")}, {QStringLiteral("tag"), QStringLiteral("agent:AG-0001")}, {QStringLiteral("removeTags"), QJsonArray()}}}},
            QStringLiteral("Created by NoAgenda")));
        check(QStringLiteral("create: event"), result.done(), why(result));
        const auto created = object(&transport, QStringLiteral("work"), createdUid + QStringLiteral(".ics"));
        const QString createdText = QString::fromUtf8(created.body);
        check(QStringLiteral("create: server has the event with the agent"),
            created.status == 200 && createdText.contains(QStringLiteral("SUMMARY:Created by NoAgenda")) && createdText.contains(QStringLiteral("X-NOURAX-AGENT-ID:AG-0001")), QStringLiteral("HTTP %1").arg(created.status));
        result = run(agenda.get(), EditCommands::calendarCreate(QStringLiteral("personal"), true, QStringLiteral("ci-edit-todo"),
            QJsonObject{{QStringLiteral("title"), QStringLiteral("To-do by NoAgenda")}, {QStringLiteral("due"), isoDay(2)}}, QStringLiteral("To-do by NoAgenda")));
        check(QStringLiteral("create: to-do"), result.done(), why(result));
        check(QStringLiteral("create: server has the to-do"), QString::fromUtf8(object(&transport, QStringLiteral("personal"), QStringLiteral("ci-edit-todo.ics")).body).contains(QStringLiteral("BEGIN:VTODO")), QString());

        QList<int> trashBefore;
        for (const auto &path : trashPaths()) {
            trashBefore.append(responsesIn(trashBin(&transport, path)));
        }
        AgendaItem gone;
        gone.key = QStringLiteral("cal:work/") + createdUid + QStringLiteral(".ics");
        gone.kind = ItemKind::Event;
        gone.home = Home::Calendar;
        gone.title = QStringLiteral("Created by NoAgenda");
        gone.calendarUri = QStringLiteral("work");
        gone.objectUri = createdUid + QStringLiteral(".ics");
        gone.version = QString::fromLatin1(created.headers.value("etag"));
        result = run(agenda.get(), EditCommands::calendarDelete(gone));
        check(QStringLiteral("delete: event"), result.done(), why(result));
        check(QStringLiteral("delete: the event is gone from the calendar"), object(&transport, QStringLiteral("work"), createdUid + QStringLiteral(".ics")).status == 404, QString());
        bool inTrash = false;
        QStringList trashNotes;
        const auto paths = trashPaths();
        for (int i = 0; i < paths.size(); ++i) {
            const auto trash = trashBin(&transport, paths.at(i));
            if (!_outDir.isEmpty()) {
                QFile trashFile(_outDir + QStringLiteral("/trashbin-%1.xml").arg(i));
                if (trashFile.open(QIODevice::WriteOnly)) {
                    trashFile.write(trash.body); // names of deleted objects of the throwaway server only
                }
            }
            const bool here = trash.status == 207 && (QString::fromUtf8(trash.body).contains(createdUid) || responsesIn(trash) > trashBefore.at(i));
            inTrash = inTrash || here;
            trashNotes.append(QStringLiteral("path %1: HTTP %2, %3 -> %4 entries%5").arg(i).arg(trash.status).arg(trashBefore.at(i)).arg(responsesIn(trash)).arg(here ? QStringLiteral(", the event is there") : QString()));
        }
        // Information, not a failure: where the server lists its calendar trash bin differs between versions.
        check(QStringLiteral("delete: the calendar trash bin holds the deleted event (%1)").arg(inTrash ? QStringLiteral("verified") : QStringLiteral("not verified")), true, trashNotes.join(QStringLiteral("; ")));

        // A repeating event is refused and left alone.
        const auto standupBefore = object(&transport, QStringLiteral("work"), QStringLiteral("standup.ics"));
        AgendaItem repeating;
        repeating.key = QStringLiteral("cal:work/standup.ics");
        repeating.kind = ItemKind::Event;
        repeating.home = Home::Calendar;
        repeating.title = QStringLiteral("Stand-up");
        repeating.calendarUri = QStringLiteral("work");
        repeating.objectUri = QStringLiteral("standup.ics");
        repeating.version = QString::fromLatin1(standupBefore.headers.value("etag"));
        result = run(agenda.get(), EditCommands::calendarPatch(repeating, QJsonObject{{QStringLiteral("title"), QStringLiteral("Changed stand-up")}}));
        check(QStringLiteral("repeating: refused"), result.outcome == EditResult::Outcome::Refused, why(result));
        check(QStringLiteral("repeating: the server object is untouched"), object(&transport, QStringLiteral("work"), QStringLiteral("standup.ics")).body == standupBefore.body, QString());
        // ... and the editor says so for the row itself, and for an invitation.
        settle(agenda.get());
        const auto *invite = byTitle(*agenda, QStringLiteral("Planning meeting"));
        check(QStringLiteral("invitation: always Self, the picker is locked"), invite && invite->invitation && !EditRules::of(*invite).canChangeAgent, QString());
    }

    // ---- conflict (calendar) ---------------------------------------------------------------------------------------------------------------
    void conflictPhase()
    {
        QTemporaryDir cache;
        auto agenda = newAgenda(cache.path() + QStringLiteral("/noagenda/conflict"));
        AccountTransport transport(_account);
        check(QStringLiteral("refresh finished"), refresh(agenda.get()), QString());
        const auto *late = byTitle(*agenda, QStringLiteral("Late bill"));
        check(QStringLiteral("to-do row found"), late != nullptr, QString());
        if (!late) {
            return;
        }
        const AgendaItem seen = *late; // what the person saw
        // Somebody else changes the to-do (the web page): its description.
        const auto current = object(&transport, QStringLiteral("personal"), QStringLiteral("late.ics"));
        QString text = QString::fromUtf8(current.body);
        text.replace(QStringLiteral("END:VTODO"), QStringLiteral("DESCRIPTION:Changed in the web page\r\nEND:VTODO"));
        const auto other = putText(&transport, QStringLiteral("remote.php/dav/calendars/%1/personal/late.ics").arg(_user), text.toUtf8(), QByteArrayLiteral("text/calendar; charset=utf-8"), current.headers.value("etag"));
        check(QStringLiteral("the other change was saved"), other.ok(), QStringLiteral("HTTP %1").arg(other.status));

        // The person's edit of the same to-do (title) meets it: the other change wins, nothing is written.
        auto result = run(agenda.get(), EditCommands::calendarPatch(seen, QJsonObject{{QStringLiteral("title"), QStringLiteral("Late bill (mine)")}}));
        check(QStringLiteral("edit conflict is found"), result.outcome == EditResult::Outcome::Conflict, why(result));
        const QString serverText = QString::fromUtf8(object(&transport, QStringLiteral("personal"), QStringLiteral("late.ics")).body);
        check(QStringLiteral("the server still has the other change and not mine"), serverText.contains(QStringLiteral("DESCRIPTION:Changed in the web page")) && !serverText.contains(QStringLiteral("Late bill (mine)")), QString());
        check(QStringLiteral("the change waits in the Not sent list"), agenda->edit()->queue().conflicts().size() == 1 && agenda->edit()->queue().pendingCount() == 0, QString());

        // "Apply my change again": both changes are there afterwards.
        const QString id = conflictOf(agenda.get(), seen.key);
        QSignalSpy spy(agenda->edit(), &EditService::finished);
        agenda->edit()->applyAgain(id);
        if (spy.isEmpty()) {
            spy.wait(120000);
        }
        const auto applied = spy.isEmpty() ? EditResult() : qvariant_cast<EditResult>(spy.last().at(1));
        check(QStringLiteral("apply again: sent"), applied.done(), why(applied));
        const QString both = QString::fromUtf8(object(&transport, QStringLiteral("personal"), QStringLiteral("late.ics")).body);
        check(QStringLiteral("apply again: the other change and mine are both on the server"), both.contains(QStringLiteral("DESCRIPTION:Changed in the web page")) && both.contains(QStringLiteral("SUMMARY:Late bill (mine)")), QString());
        check(QStringLiteral("apply again: nothing is left in the lists"), agenda->edit()->queue().conflicts().isEmpty() && agenda->edit()->queue().pendingCount() == 0, QString());
    }

    // ---- edit-gantt ----------------------------------------------------------------------------------------------------------------------------
    void editGantt()
    {
        QTemporaryDir cache;
        auto agenda = newAgenda(cache.path() + QStringLiteral("/noagenda/edit-gantt"));
        AccountTransport transport(_account);
        check(QStringLiteral("refresh finished"), refresh(agenda.get()), QString());
        const int website = seedId("projects", QStringLiteral("Website"));
        const int internal = seedId("projects", QStringLiteral("Internal"));

        // Dates: a task with a successor. NoGantt moves the dependent task and tells which; Undo puts it back.
        const int banner = seedId("tasks", QStringLiteral("Banner design"));
        const int publish = seedId("tasks", QStringLiteral("Publish banners"));
        const auto bannerBefore = taskOf(&transport, website, banner);
        const auto publishBefore = taskOf(&transport, website, publish);
        AgendaItem bannerItem;
        bannerItem.key = QStringLiteral("gantt:task:%1").arg(banner);
        bannerItem.kind = ItemKind::GanttTask;
        bannerItem.home = Home::Gantt;
        bannerItem.title = QStringLiteral("Banner design");
        bannerItem.projectId = website;
        bannerItem.taskId = banner;
        bannerItem.version = bannerBefore.value(QStringLiteral("updated_at")).toString();
        bannerItem.progressMode = bannerBefore.value(QStringLiteral("progress_mode")).toString();
        auto result = run(agenda.get(), EditCommands::ganttUpdate(bannerItem, QJsonObject{{QStringLiteral("end_date"), isoWorkday(3)}}));
        check(QStringLiteral("dates: sent"), result.done(), why(result));
        const auto bannerAfter = taskOf(&transport, website, banner);
        check(QStringLiteral("dates: the server has the new end date"), bannerAfter.value(QStringLiteral("end_date")).toString().startsWith(isoWorkday(3)), bannerAfter.value(QStringLiteral("end_date")).toString().left(10));
        const auto publishAfter = taskOf(&transport, website, publish);
        check(QStringLiteral("dates: the dependent task moved"), publishAfter.value(QStringLiteral("start_date")).toString() != publishBefore.value(QStringLiteral("start_date")).toString(),
            publishAfter.value(QStringLiteral("start_date")).toString().left(10));
        check(QStringLiteral("dates: NoAgenda names the task that moved"), result.alsoMoved.contains(QStringLiteral("Publish banners")), result.alsoMoved.join(QStringLiteral(", ")));
        check(QStringLiteral("dates: an Undo is offered"), !result.undoOpId.isEmpty(), QString());
        if (!result.undoOpId.isEmpty()) {
            agenda->edit()->undo(result.undoOpId);
            QTest::qWait(2000);
            const auto bannerUndone = taskOf(&transport, website, banner);
            check(QStringLiteral("undo: the end date is back"), bannerUndone.value(QStringLiteral("end_date")).toString().left(10) == bannerBefore.value(QStringLiteral("end_date")).toString().left(10),
                bannerUndone.value(QStringLiteral("end_date")).toString().left(10));
        }

        // % done, status and done.
        settle(agenda.get());
        const auto *demo = byTitle(*agenda, QStringLiteral("Prepare client demo"));
        check(QStringLiteral("task row found"), demo != nullptr, QString());
        if (demo) {
            result = run(agenda.get(), EditCommands::ganttUpdate(*demo, QJsonObject{{QStringLiteral("manual_progress"), 55}, {QStringLiteral("status"), QStringLiteral("in_progress")}}));
            check(QStringLiteral("progress: sent"), result.done(), why(result));
            const auto demoTask = taskOf(&transport, website, seedId("tasks", QStringLiteral("Prepare client demo")));
            check(QStringLiteral("progress: the server has 55 percent and the status"), demoTask.value(QStringLiteral("manual_progress")).toInt() == 55 && demoTask.value(QStringLiteral("status")).toString() == QLatin1String("in_progress"),
                QStringLiteral("%1 %2").arg(demoTask.value(QStringLiteral("manual_progress")).toInt()).arg(demoTask.value(QStringLiteral("status")).toString()));
        }
        settle(agenda.get());
        const auto *invoice = byTitle(*agenda, QStringLiteral("Send invoice"));
        if (invoice) {
            result = run(agenda.get(), EditCommands::ganttUpdate(*invoice, QJsonObject{{QStringLiteral("done"), true}}));
            check(QStringLiteral("done: sent"), result.done(), why(result));
            const auto invoiceTask = taskOf(&transport, website, seedId("tasks", QStringLiteral("Send invoice")));
            check(QStringLiteral("done: the server has the status done and 100 percent"), invoiceTask.value(QStringLiteral("status")).toString() == QLatin1String("done") && invoiceTask.value(QStringLiteral("manual_progress")).toInt() == 100, QString());
        }

        // My agent: Self -> Writer-Bot -> Self on a task of mine whose rows hold no work of an agent.
        settle(agenda.get());
        const int landing = seedId("tasks", QStringLiteral("Landing page build"));
        const int writerResource = seedId("resources", QString::fromUtf8("\xF0\x9F\xA4\x96 Writer-Bot (Ali)"));
        const auto *landingRow = byTitle(*agenda, QStringLiteral("Landing page build"));
        check(QStringLiteral("agent: task row found"), landingRow != nullptr, QString());
        if (landingRow) {
            const auto landingBefore = taskOf(&transport, website, landing);
            const auto idsBefore = resourceIds(landingBefore);
            result = run(agenda.get(), EditCommands::ganttAgent(*landingRow, QStringLiteral("AG-0001"), _user));
            check(QStringLiteral("agent: Self to Writer-Bot sent"), result.done() && !result.checkInNoGantt, why(result));
            const auto landingWith = taskOf(&transport, website, landing);
            check(QStringLiteral("agent: the server has my row and the agent's row, in that order"), resourceIds(landingWith) == (idsBefore + QList<int>{writerResource}), QString());
            check(QStringLiteral("agent: my own row and the work total are untouched"),
                landingWith.value(QStringLiteral("work")) == landingBefore.value(QStringLiteral("work")) && landingWith.value(QStringLiteral("resources")).toArray().first().toObject().value(QStringLiteral("work")) == landingBefore.value(QStringLiteral("resources")).toArray().first().toObject().value(QStringLiteral("work")),
                QString());
            settle(agenda.get());
            landingRow = byTitle(*agenda, QStringLiteral("Landing page build"));
            check(QStringLiteral("agent: the row now names Writer-Bot"), landingRow && landingRow->workKind == WorkKind::MyAgent && landingRow->agent.displayText() == QLatin1String("Writer-Bot"), QString());
            if (landingRow) {
                result = run(agenda.get(), EditCommands::ganttAgent(*landingRow, QString(), _user));
                check(QStringLiteral("agent: Writer-Bot to Self sent"), result.done(), why(result));
                const auto landingBack = taskOf(&transport, website, landing);
                check(QStringLiteral("agent: the server is back to my row only"), resourceIds(landingBack) == idsBefore && landingBack.value(QStringLiteral("work")) == landingBefore.value(QStringLiteral("work")), QString());
            }
        }

        // The refusals: nothing is sent and the server task is unchanged.
        settle(agenda.get());
        const auto *homepageRow = byTitle(*agenda, QStringLiteral("Write homepage copy"));
        if (homepageRow) {
            const AgendaItem homepageItem = *homepageRow;
            const AgendaItem *homepage = &homepageItem;
            const auto homepageBefore = taskOf(&transport, website, seedId("tasks", QStringLiteral("Write homepage copy")));
            result = run(agenda.get(), EditCommands::ganttAgent(*homepage, QString(), _user));
            check(QStringLiteral("refused: the agent's row holds work, so Self would change the estimate"), result.outcome == EditResult::Outcome::Refused && result.message.contains(QStringLiteral("work estimate")), why(result));
            check(QStringLiteral("refused: the task on the server is unchanged"), taskOf(&transport, website, seedId("tasks", QStringLiteral("Write homepage copy"))) == homepageBefore, QString());
            const auto paused = run(agenda.get(), EditCommands::ganttAgent(*homepage, QStringLiteral("AG-0003"), _user));
            check(QStringLiteral("refused: a paused agent can't be chosen"), paused.outcome == EditResult::Outcome::Refused, why(paused));
            const auto foreign = run(agenda.get(), EditCommands::ganttAgent(*homepage, QStringLiteral("AG-0002"), _user));
            check(QStringLiteral("refused: an agent of somebody else"), foreign.outcome == EditResult::Outcome::Refused, why(foreign));
        }
        // Four rows: made here with the API, then the fifth is refused.
        const int ali = seedId("resources", QStringLiteral("Ali"));
        const int sara = seedId("resources", QStringLiteral("Sara"));
        const int temp = seedId("resources", QStringLiteral("Temp worker"));
        const int designer = seedId("resources", QStringLiteral("Designer"));
        QJsonArray fourRows;
        for (const int rid : {ali, sara, temp, designer}) {
            fourRows.append(QJsonObject{{QStringLiteral("resource_id"), rid}, {QStringLiteral("work"), QJsonValue(QJsonValue::Null)}});
        }
        const auto createdFour = sendJson(&transport, "POST", gantt(QStringLiteral("projects/%1/tasks").arg(website)),
            QJsonObject{{QStringLiteral("title"), QStringLiteral("Edit test four rows")}, {QStringLiteral("start_date"), isoDay(1)}, {QStringLiteral("end_date"), isoDay(2)}, {QStringLiteral("resources"), fourRows}});
        const auto fourTask = parseOcs(createdFour).data.toObject();
        check(QStringLiteral("four rows: task made"), createdFour.ok(), QStringLiteral("HTTP %1").arg(createdFour.status));
        if (createdFour.ok()) {
            const int fourId = fourTask.contains(QStringLiteral("task")) ? fourTask.value(QStringLiteral("task")).toObject().value(QStringLiteral("id")).toInt() : fourTask.value(QStringLiteral("id")).toInt();
            AgendaItem four;
            four.key = QStringLiteral("gantt:task:%1").arg(fourId);
            four.kind = ItemKind::GanttTask;
            four.home = Home::Gantt;
            four.projectId = website;
            four.taskId = fourId;
            four.version = taskOf(&transport, website, fourId).value(QStringLiteral("updated_at")).toString();
            const auto fourAnswer = run(agenda.get(), EditCommands::ganttAgent(four, QStringLiteral("AG-0001"), _user));
            check(QStringLiteral("refused: a task that already has 4 people or agents"), fourAnswer.outcome == EditResult::Outcome::Refused && fourAnswer.message.contains(QStringLiteral("4 people or agents")), why(fourAnswer));
            check(QStringLiteral("refused: the four rows are unchanged"), resourceIds(taskOf(&transport, website, fourId)).size() == 4, QString());
        }

        // A project I only view: the server refuses even my own task (risk B1), and NoAgenda says why.
        const int internalTask = seedId("tasks", QStringLiteral("Sara's internal task"));
        AgendaItem viewerItem;
        viewerItem.key = QStringLiteral("gantt:task:%1").arg(internalTask);
        viewerItem.kind = ItemKind::GanttTask;
        viewerItem.home = Home::Gantt;
        viewerItem.projectId = internal;
        viewerItem.taskId = internalTask;
        viewerItem.version = taskOf(&transport, internal, internalTask).value(QStringLiteral("updated_at")).toString();
        const auto viewerBefore = taskOf(&transport, internal, internalTask);
        const auto viewer = run(agenda.get(), EditCommands::ganttUpdate(viewerItem, QJsonObject{{QStringLiteral("end_date"), isoWorkday(9)}}));
        check(QStringLiteral("viewer: refused with the plain sentence"), viewer.outcome == EditResult::Outcome::Refused && viewer.message.contains(QStringLiteral("view this project only")), why(viewer));
        check(QStringLiteral("viewer: the server task is unchanged"), taskOf(&transport, internal, internalTask) == viewerBefore, QString());

        // Conflict: the task changes between the person's read and the write.
        settle(agenda.get());
        const auto *sitemapOrDemo = byTitle(*agenda, QStringLiteral("Cross pairing"));
        if (sitemapOrDemo) {
            const AgendaItem seen = *sitemapOrDemo;
            const int crossId = seedId("tasks", QStringLiteral("Cross pairing"));
            const auto other = sendJson(&transport, "PUT", gantt(QStringLiteral("tasks/%1").arg(crossId)), QJsonObject{{QStringLiteral("title"), QStringLiteral("Cross pairing (renamed elsewhere)")}});
            check(QStringLiteral("conflict: the other change was saved"), other.ok(), QStringLiteral("HTTP %1").arg(other.status));
            const auto clash = run(agenda.get(), EditCommands::ganttUpdate(seen, QJsonObject{{QStringLiteral("end_date"), isoWorkday(4)}}));
            check(QStringLiteral("conflict: found by updated_at"), clash.outcome == EditResult::Outcome::Conflict, why(clash));
            const auto serverTask = taskOf(&transport, website, crossId);
            check(QStringLiteral("conflict: the server has the other change only"), serverTask.value(QStringLiteral("title")).toString() == QLatin1String("Cross pairing (renamed elsewhere)") && !serverTask.value(QStringLiteral("end_date")).toString().startsWith(isoWorkday(4)), QString());
            const QString id = conflictOf(agenda.get(), seen.key);
            check(QStringLiteral("conflict: the change waits in the Not sent list"), !id.isEmpty(), QString());
            QSignalSpy spy(agenda->edit(), &EditService::finished);
            agenda->edit()->applyAgain(id);
            if (spy.isEmpty()) {
                spy.wait(120000);
            }
            const auto applied = spy.isEmpty() ? EditResult() : qvariant_cast<EditResult>(spy.last().at(1));
            check(QStringLiteral("conflict: apply my change again"), applied.done(), why(applied));
            const auto both = taskOf(&transport, website, crossId);
            check(QStringLiteral("conflict: both changes are there"), both.value(QStringLiteral("title")).toString() == QLatin1String("Cross pairing (renamed elsewhere)") && both.value(QStringLiteral("end_date")).toString().startsWith(isoWorkday(4)), QString());
        }
    }

    // ---- edit-deck -------------------------------------------------------------------------------------------------------------------------------
    void editDeck()
    {
        QTemporaryDir cache;
        AccountTransport transport(_account);
        const int board = _seed.value(QLatin1String("board")).toInt();
        const int stack = firstStackOf(&transport, board);
        check(QStringLiteral("board and stack found"), board > 0 && stack > 0, QString());
        const int payId = seedId("cards", QStringLiteral("Pay invoice"));
        const int supplierId = seedId("cards", QStringLiteral("Call supplier"));
        const int ideaId = seedId("cards", QStringLiteral("Someday idea"));

        // One more active agent for the person, without a label on the seeded board (registry file of the person).
        {
            auto registryReply = get(&transport, QStringLiteral("remote.php/dav/files/%1/NoAgenda/agent-registry.json").arg(_user), false);
            auto root = QJsonDocument::fromJson(registryReply.body).object();
            auto agents = root.value(QStringLiteral("agents")).toArray();
            auto edit = agents.first().toObject();
            edit.insert(QStringLiteral("id"), QStringLiteral("AG-0007"));
            edit.insert(QStringLiteral("name"), QStringLiteral("Edit-Bot"));
            edit.insert(QStringLiteral("person"), _user);
            edit.insert(QStringLiteral("status"), QStringLiteral("active"));
            auto links = edit.value(QStringLiteral("links")).toObject();
            links.insert(QStringLiteral("nogantt_resource_ids"), QJsonArray());
            links.insert(QStringLiteral("noboard_label"), QString::fromUtf8("\xF0\x9F\xA4\x96 Edit-Bot"));
            links.insert(QStringLiteral("calendar_tag"), QStringLiteral("agent:AG-0007"));
            edit.insert(QStringLiteral("links"), links);
            agents.append(edit);
            root.insert(QStringLiteral("agents"), agents);
            const auto put = putText(&transport, QStringLiteral("remote.php/dav/files/%1/NoAgenda/agent-registry.json").arg(_user), QJsonDocument(root).toJson(), QByteArrayLiteral("application/json"));
            check(QStringLiteral("registry with one more agent"), put.ok(), QStringLiteral("HTTP %1").arg(put.status));
        }
        // A description and a start date that must stay when only the due date changes (Deck clears what is not sent).
        {
            const auto card = cardOf(&transport, board, stack, payId);
            QJsonObject full{{QStringLiteral("title"), card.value(QStringLiteral("title")).toString()}, {QStringLiteral("type"), QStringLiteral("plain")}, {QStringLiteral("owner"), QStringLiteral("mara")},
                {QStringLiteral("description"), QStringLiteral("Keep this text")}, {QStringLiteral("order"), card.value(QStringLiteral("order")).toInt()},
                {QStringLiteral("duedate"), card.value(QStringLiteral("duedate"))}, {QStringLiteral("startdate"), QString(isoDay(-2) + QStringLiteral("T00:00:00+00:00"))}};
            const auto r = sendJson(&transport, "PUT", QStringLiteral("index.php/apps/deck/api/v1.0/boards/%1/stacks/%2/cards/%3").arg(board).arg(stack).arg(payId), full);
            check(QStringLiteral("card prepared with a description and a start date"), r.ok(), QStringLiteral("HTTP %1").arg(r.status));
        }

        auto agenda = newAgenda(cache.path() + QStringLiteral("/noagenda/edit-deck"));
        check(QStringLiteral("refresh finished"), refresh(agenda.get()), QString());
        const auto *pay = byTitle(*agenda, QStringLiteral("Pay invoice"));
        check(QStringLiteral("card row found"), pay != nullptr, QString());
        if (pay) {
            check(QStringLiteral("card may be edited"), EditRules::of(*pay).canEdit && !EditRules::of(*pay).canDelete, QString());
            auto result = run(agenda.get(), EditCommands::deckCard(*pay, QJsonObject{{QStringLiteral("due"), isoDay(4)}}));
            check(QStringLiteral("due date: sent"), result.done(), why(result));
            auto card = cardOf(&transport, board, stack, payId);
            check(QStringLiteral("due date: the server has it"), card.value(QStringLiteral("duedate")).toString().startsWith(isoDay(4)), card.value(QStringLiteral("duedate")).toString());
            check(QStringLiteral("due date: description and start date are kept"), card.value(QStringLiteral("description")).toString() == QLatin1String("Keep this text") && card.value(QStringLiteral("startdate")).toString().startsWith(isoDay(-2)),
                card.value(QStringLiteral("startdate")).toString());
            settle(agenda.get());
            pay = byTitle(*agenda, QStringLiteral("Pay invoice"));
            if (pay) {
                result = run(agenda.get(), EditCommands::deckCard(*pay, QJsonObject{{QStringLiteral("done"), true}}));
                check(QStringLiteral("done: sent"), result.done(), why(result));
                card = cardOf(&transport, board, stack, payId);
                check(QStringLiteral("done: the server has it and the due date is kept"), card.value(QStringLiteral("done")).isString() && card.value(QStringLiteral("duedate")).toString().startsWith(isoDay(4))
                        && card.value(QStringLiteral("description")).toString() == QLatin1String("Keep this text"), QString());
            }
        }

        // My agent label: Writer-Bot -> Self -> Writer-Bot.
        settle(agenda.get());
        const auto *supplier = byTitle(*agenda, QStringLiteral("Call supplier"));
        check(QStringLiteral("labelled card found"), supplier && supplier->workKind == WorkKind::MyAgent, QString());
        if (supplier) {
            auto result = run(agenda.get(), EditCommands::deckAgent(*supplier, QString(), _user));
            check(QStringLiteral("label: to Self"), result.done(), why(result));
            check(QStringLiteral("label: the card has no agent label"), cardOf(&transport, board, stack, supplierId).value(QStringLiteral("labels")).toArray().isEmpty(), QString());
            settle(agenda.get());
            supplier = byTitle(*agenda, QStringLiteral("Call supplier"));
            if (supplier) {
                result = run(agenda.get(), EditCommands::deckAgent(*supplier, QStringLiteral("AG-0001"), _user));
                check(QStringLiteral("label: to Writer-Bot"), result.done(), why(result));
                const auto labels = cardOf(&transport, board, stack, supplierId).value(QStringLiteral("labels")).toArray();
                check(QStringLiteral("label: the card has the Writer-Bot label again"), labels.size() == 1 && labels.first().toObject().value(QStringLiteral("title")).toString().contains(QStringLiteral("Writer-Bot")), QString());
            }
        }

        // A label that the board does not have: refused where the person may only edit the board.
        settle(agenda.get());
        const auto *idea = byTitle(*agenda, QStringLiteral("Someday idea"));
        if (idea) {
            const auto refused = run(agenda.get(), EditCommands::deckAgent(*idea, QStringLiteral("AG-0007"), _user));
            check(QStringLiteral("label: refused on a board I may only edit"), refused.outcome == EditResult::Outcome::Refused, why(refused));
            check(QStringLiteral("label: the card is unchanged"), cardOf(&transport, board, stack, ideaId).value(QStringLiteral("labels")).toArray().isEmpty(), QString());
            const auto boards = get(&transport, QStringLiteral("index.php/apps/deck/api/v1.0/boards/%1").arg(board));
            check(QStringLiteral("label: none was created"), !QString::fromUtf8(boards.body).contains(QStringLiteral("Edit-Bot")), QString());
        }

        // ... and created on a board that the person manages.
        const auto ownBoard = sendJson(&transport, "POST", QStringLiteral("index.php/apps/deck/api/v1.0/boards"), QJsonObject{{QStringLiteral("title"), QStringLiteral("My own board")}, {QStringLiteral("color"), QStringLiteral("2563EB")}});
        const int ownBoardId = QJsonDocument::fromJson(ownBoard.body).object().value(QStringLiteral("id")).toInt();
        check(QStringLiteral("own board made"), ownBoard.ok() && ownBoardId > 0, QStringLiteral("HTTP %1").arg(ownBoard.status));
        if (ownBoardId > 0) {
            const auto ownStack = sendJson(&transport, "POST", QStringLiteral("index.php/apps/deck/api/v1.0/boards/%1/stacks").arg(ownBoardId), QJsonObject{{QStringLiteral("title"), QStringLiteral("To do")}, {QStringLiteral("order"), 1}});
            const int ownStackId = QJsonDocument::fromJson(ownStack.body).object().value(QStringLiteral("id")).toInt();
            const auto ownCard = sendJson(&transport, "POST", QStringLiteral("index.php/apps/deck/api/v1.0/boards/%1/stacks/%2/cards").arg(ownBoardId).arg(ownStackId),
                QJsonObject{{QStringLiteral("title"), QStringLiteral("My own card")}, {QStringLiteral("type"), QStringLiteral("plain")}, {QStringLiteral("order"), 1}});
            const int ownCardId = QJsonDocument::fromJson(ownCard.body).object().value(QStringLiteral("id")).toInt();
            sendJson(&transport, "PUT", QStringLiteral("index.php/apps/deck/api/v1.0/boards/%1/stacks/%2/cards/%3/assignUser").arg(ownBoardId).arg(ownStackId).arg(ownCardId), QJsonObject{{QStringLiteral("userId"), _user}, {QStringLiteral("type"), 0}});
            settle(agenda.get());
            const auto *own = byTitle(*agenda, QStringLiteral("My own card"));
            check(QStringLiteral("own card row found"), own != nullptr, QString());
            if (own) {
                const auto created = run(agenda.get(), EditCommands::deckAgent(*own, QStringLiteral("AG-0007"), _user));
                check(QStringLiteral("label: created on a board I manage and assigned"), created.done(), why(created));
                const auto boardJson = get(&transport, QStringLiteral("index.php/apps/deck/api/v1.0/boards/%1").arg(ownBoardId));
                check(QStringLiteral("label: the board has the new label"), QString::fromUtf8(boardJson.body).contains(QStringLiteral("Edit-Bot")), QString());
                check(QStringLiteral("label: the card has it"), cardOf(&transport, ownBoardId, ownStackId, ownCardId).value(QStringLiteral("labels")).toArray().size() == 1, QString());
            }
        }

        // Conflict: the card changes between the person's read and the write.
        settle(agenda.get());
        idea = byTitle(*agenda, QStringLiteral("Someday idea"));
        if (idea) {
            const AgendaItem seen = *idea;
            const auto fresh = cardOf(&transport, board, stack, ideaId);
            QJsonObject full{{QStringLiteral("title"), QStringLiteral("Someday idea (renamed elsewhere)")}, {QStringLiteral("type"), QStringLiteral("plain")}, {QStringLiteral("owner"), QStringLiteral("mara")},
                {QStringLiteral("description"), QString()}, {QStringLiteral("order"), fresh.value(QStringLiteral("order")).toInt()}};
            QTest::qWait(1500); // lastModified has the resolution of a second
            const auto other = sendJson(&transport, "PUT", QStringLiteral("index.php/apps/deck/api/v1.0/boards/%1/stacks/%2/cards/%3").arg(board).arg(stack).arg(ideaId), full);
            check(QStringLiteral("conflict: the other change was saved"), other.ok(), QStringLiteral("HTTP %1").arg(other.status));
            const auto clash = run(agenda.get(), EditCommands::deckCard(seen, QJsonObject{{QStringLiteral("due"), isoDay(5)}}));
            check(QStringLiteral("conflict: found by the version of the card"), clash.outcome == EditResult::Outcome::Conflict, why(clash));
            const auto card = cardOf(&transport, board, stack, ideaId);
            check(QStringLiteral("conflict: the server has the other change only"), card.value(QStringLiteral("title")).toString() == QLatin1String("Someday idea (renamed elsewhere)") && card.value(QStringLiteral("duedate")).isNull(), QString());
        }
    }

    // ---- offline: three edits wait while the server is not reachable, then go out in order ---------------------------------------------------------
    void offlinePhase()
    {
        QTemporaryDir cache;
        auto agenda = newAgenda(cache.path() + QStringLiteral("/noagenda/offline-source"));
        AccountTransport transport(_account);
        check(QStringLiteral("refresh finished"), refresh(agenda.get()), QString());
        // By key, not by title: the web-edit step changed the title of the "Draft report" to-do.
        const auto *a = find(agenda->state().items, QStringLiteral("cal:personal/renew.ics"));
        const auto *b = find(agenda->state().items, QStringLiteral("cal:personal/draft.ics"));
        const auto *c = find(agenda->state().items, QStringLiteral("cal:personal/someday.ics"));
        check(QStringLiteral("three to-do rows found"), a && b && c, QString());
        if (!a || !b || !c) {
            return;
        }
        const QString dir = cache.path() + QStringLiteral("/noagenda/offline-queue");

        // The same account, pointed at a closed port (nothing listens there).
        auto closed = Account::create();
        closed->setCredentials(new BasicCredentials(_user, _password));
        closed->setUrl(QUrl(QStringLiteral("http://127.0.0.1:9")));
        closed->setDavUser(_user);
        AccountTransport closedTransport(closed);
        QList<QString> expectedOrder;
        {
            EditService offline(&closedTransport, dir, [&agenda] { return agenda->editContext(); });
            QSignalSpy finished(&offline, &EditService::finished);
            for (const auto *item : {a, b, c}) {
                const auto command = EditCommands::calendarPatch(*item, QJsonObject{{QStringLiteral("title"), QString(item->title + QStringLiteral(" (offline edit)"))}});
                expectedOrder.append(command.id);
                offline.submit(command);
                if (finished.isEmpty()) {
                    finished.wait(30000);
                }
            }
            check(QStringLiteral("offline: three changes are waiting"), offline.queue().pendingCount() == 3, QString::number(offline.queue().pendingCount()));
            check(QStringLiteral("offline: none is lost and none is a conflict"), offline.queue().conflicts().isEmpty(), QString());
        }
        // NoDrive restarts and the connection is back: the queue is read from its file and sent oldest first.
        EditService online(&transport, dir, [&agenda] { return agenda->editContext(); });
        online.load();
        check(QStringLiteral("offline: the queue survived the restart"), online.queue().pendingCount() == 3, QString::number(online.queue().pendingCount()));
        QStringList sentOrder;
        connect(&online, &EditService::finished, this, [&sentOrder](const EditCommand &command, const EditResult &result) {
            if (result.done()) {
                sentOrder.append(command.id);
            }
        });
        online.retry();
        for (int i = 0; i < 600 && online.queue().pendingCount() > 0; ++i) {
            QTest::qWait(100);
        }
        check(QStringLiteral("offline: all three were sent"), online.queue().pendingCount() == 0 && online.queue().conflicts().isEmpty(), QString());
        check(QStringLiteral("offline: in the order they were made"), sentOrder == expectedOrder, QString());
        check(QStringLiteral("offline: the server has all three"),
            QString::fromUtf8(object(&transport, QStringLiteral("personal"), QStringLiteral("renew.ics")).body).contains(QStringLiteral("(offline edit)"))
                && QString::fromUtf8(object(&transport, QStringLiteral("personal"), QStringLiteral("draft.ics")).body).contains(QStringLiteral("(offline edit)"))
                && QString::fromUtf8(object(&transport, QStringLiteral("personal"), QStringLiteral("someday.ics")).body).contains(QStringLiteral("(offline edit)")),
            QString());
    }

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
