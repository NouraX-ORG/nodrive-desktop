/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "accountagenda.h"

#include "caldav/calendarreader.h"
#include "deck/deckclient.h"
#include "gantt/ganttclient.h"
#include "logic/agentload.h"
#include "logic/ganttmapper.h"
#include "ocs/serverinfo.h"

#include <QLoggingCategory>
#include <QUrl>
#include <QUrlQuery>

namespace OCC::Nourax::Agenda {

Q_LOGGING_CATEGORY(lcAgendaAccount, "nodrive.agenda.account", QtInfoMsg)

const SourceStatus *AgendaState::status(const QString &key) const
{
    for (const auto &s : statuses) {
        if (s.key == key) {
            return &s;
        }
    }
    return nullptr;
}

AccountAgenda::AccountAgenda(std::unique_ptr<HttpTransport> transport, const QString &cacheDir, QObject *parent)
    : QObject(parent)
    , _transport(std::move(transport))
    , _cache(cacheDir)
    , _clock([] { return QDateTime::currentDateTimeUtc(); })
    , _zone(QTimeZone::systemTimeZone())
{
    _registrySource = makeRegistrySource(_transport.get());
    _registry = AgentRegistry::missing();
    _edit = std::make_unique<EditService>(_transport.get(), cacheDir, [this] { return editContext(); }, this);
    connect(_edit.get(), &EditService::changed, this, [this] { rebuild(); });
    connect(_edit.get(), &EditService::refreshNeeded, this, [this] {
        if (_online) {
            QPointer<AccountAgenda> self(this);
            QMetaObject::invokeMethod(this, [self] { if (self) { self->refresh(true); } }, Qt::QueuedConnection);
        }
    });
    connect(_edit.get(), &EditService::loginNeeded, this, [this] { setNeedsLogin(true); });
    for (const auto *key : {"calendar", "gantt", "board", "registry"}) {
        SourceStatus s;
        s.key = QString::fromLatin1(key);
        _statuses.insert(s.key, s);
    }
}

AccountAgenda::~AccountAgenda() = default;

bool AccountAgenda::isRefusal(int httpStatus)
{
    return httpStatus == 401 || httpStatus == 403;
}

QDateTime AccountAgenda::now() const
{
    return _clock().toUTC();
}

int AccountAgenda::backoffMinutes(int failures)
{
    if (failures <= 0) {
        return 0;
    }
    const int minutes = 5 << qMin(failures - 1, 3);
    return qMin(minutes, 30);
}

void AccountAgenda::loadFromCache()
{
    _settings = _cache.loadSettings();
    _server = _cache.loadServerInfo();
    _registryCache = _cache.loadRegistry();
    _registry = (_registryCache.present && !_registryCache.bytes.isEmpty()) ? AgentRegistry::parse(_registryCache.bytes) : AgentRegistry::missing();
    _calendar = _cache.loadCalendar();
    _gantt = _cache.loadGantt();
    _deck = _cache.loadDeck();
    _edit->load();
    if (_calendar.loaded) {
        markStatus(QStringLiteral("calendar"), SourceStatus::State::Saved, _calendar.fetchedAt);
    }
    if (_gantt.loaded) {
        markStatus(QStringLiteral("gantt"), _gantt.unavailable ? SourceStatus::State::Unavailable : SourceStatus::State::Saved, _gantt.fetchedAt);
    }
    if (_deck.loaded) {
        markStatus(QStringLiteral("board"), _deck.unavailable ? SourceStatus::State::Unavailable : SourceStatus::State::Saved, _deck.fetchedAt);
    }
    if (_registryCache.present) {
        markStatus(QStringLiteral("registry"), SourceStatus::State::Saved, _registryCache.fetchedAt);
    }
    rebuild();
}

void AccountAgenda::markStatus(const QString &key, SourceStatus::State state, const QDateTime &savedAt, const QString &message)
{
    SourceStatus s = _statuses.value(key);
    s.key = key;
    s.state = state;
    s.savedAt = savedAt;
    s.message = message;
    _statuses.insert(key, s);
}

void AccountAgenda::saveSettings()
{
    _cache.saveSettings(_settings);
}

void AccountAgenda::rebuild()
{
    BuildInput input;
    input.server = _server;
    input.davUser = _transport->davUser();
    input.accountEmail = _accountEmail;
    input.registry = _registry;
    input.settings = _settings;
    input.calendar = _calendar;
    input.gantt = _gantt;
    input.deck = _deck;
    input.now = now();
    input.zone = _zone;
    input.today = input.now.toTimeZone(_zone).date();

    auto build = AgendaBuilder::build(input);
    _state.hours = AgendaBuilder::workingHours(input);
    const QString me = input.server.uid.isEmpty() ? input.davUser : input.server.uid;

    // What the person changed and has not been read back yet is drawn on top, then the conflict rules run on the
    // result, so every local change shows its effect (clash, workload, day off, agent load) at once.
    auto items = build.items;
    _edit->overlay(&items, input.today);
    _state.dayModel = DayModel(items, _state.hours, Conflicts::holidaysFor(_gantt, me), _zone);
    Conflicts::annotate(&items, _state.dayModel);
    items.append(AgentLoad::asItems(AgentLoad::compute(_registry, items, input.today, input.today.addDays(28), _zone, me), _registry));
    _state.items = items;
    _state.sources = build.sources;
    _state.build = build;
    _state.registryStatus = _registry.status();
    _state.registrySkipped = _registry.skippedCount();
    _state.personUid = input.server.uid.isEmpty() ? input.davUser : input.server.uid;
    _state.personName = input.server.displayName.isEmpty() ? _state.personUid : input.server.displayName;
    _state.today = input.today;
    _state.refreshing = _refreshing;
    _state.offline = !_online;

    // The status list, with what the build knows (not linked, guard).
    _state.statuses.clear();
    for (const auto *key : {"calendar", "gantt", "board", "registry"}) {
        auto s = _statuses.value(QString::fromLatin1(key));
        if (s.key == QLatin1String("gantt")) {
            if (build.ganttUnavailable) {
                s.state = SourceStatus::State::Unavailable;
            } else if (_gantt.loaded && build.ganttNotLinked && s.state == SourceStatus::State::Fresh) {
                s.state = SourceStatus::State::NotLinked;
            }
            s.readOnlyByGuard = build.ganttReadOnlyByGuard;
        } else if (s.key == QLatin1String("board")) {
            if (build.boardUnavailable) {
                s.state = SourceStatus::State::Unavailable;
            }
            s.readOnlyByGuard = build.boardReadOnlyByGuard;
        }
        _state.statuses.append(s);
    }
    _state.neverLoaded = !_calendar.loaded && !_gantt.loaded && !_deck.loaded;
    Q_EMIT stateChanged();
}

void AccountAgenda::setOnline(bool online)
{
    if (_online == online) {
        return;
    }
    _online = online;
    _edit->setOnline(online);
    rebuild();
}

void AccountAgenda::setNeedsLogin(bool needsLogin)
{
    if (_state.needsLogin == needsLogin) {
        return;
    }
    _state.needsLogin = needsLogin;
    Q_EMIT stateChanged();
}

bool AccountAgenda::allowed(const QString &key, bool manual)
{
    if (manual) {
        return true;
    }
    const auto run = _runs.value(key);
    return !run.nextAllowed.isValid() || run.nextAllowed <= now();
}

void AccountAgenda::succeeded(const QString &key)
{
    _runs.remove(key);
}

void AccountAgenda::failed(const QString &key, int httpStatus, bool networkError)
{
    auto run = _runs.value(key);
    ++run.failures;
    run.nextAllowed = now().addSecs(backoffMinutes(run.failures) * 60);
    _runs.insert(key, run);
    qCInfo(lcAgendaAccount) << key << "failed: status" << httpStatus << "network error" << networkError << "failures" << run.failures;
    // 401/403 from NoGantt or NoBoard while the account is connected means that app refuses this sign-in: that part
    // turns into "Open in NoGantt" / "Open in NoBoard" (design 5). It is not a sign-in problem of the account: the
    // sign-in state belongs to AccountState. From the server itself (CalDAV, user info) it still means a bad sign-in.
    if (isRefusal(httpStatus) && key != QLatin1String("gantt") && key != QLatin1String("board")) {
        _state.needsLogin = true;
    }
}

void AccountAgenda::finishPart()
{
    if (--_pending > 0) {
        return;
    }
    _refreshing = false;
    _state.refreshedAt = now();
    // What was sent before this refresh started is in the data now (only when every source was read: after a
    // failed read the cached rows are old, so the sent change stays drawn on top of them).
    bool allRead = true;
    for (const auto *key : {"calendar", "gantt", "board"}) {
        const auto state = _statuses.value(QString::fromLatin1(key)).state;
        if (state == SourceStatus::State::Cached || state == SourceStatus::State::Failed || state == SourceStatus::State::Refused
            || state == SourceStatus::State::Saved || state == SourceStatus::State::NeverLoaded) {
            allRead = false;
        }
    }
    if (allRead) {
        _edit->clearAppliedUpTo(_refreshSequence);
    }
    rebuild();
    Q_EMIT refreshFinished();
    if (_online && !_state.needsLogin) {
        _edit->retry();
        if (_edit->appliedSequence() > _refreshSequence) {
            // Something was sent while this refresh was reading: read once more, so the data catches up.
            QPointer<AccountAgenda> self(this);
            QMetaObject::invokeMethod(this, [self] { if (self) { self->refresh(true); } }, Qt::QueuedConnection);
        }
    }
}

EditContext AccountAgenda::editContext() const
{
    EditContext c;
    c.davUser = _transport->davUser();
    c.me = _server.uid.isEmpty() ? c.davUser : _server.uid;
    c.myName = _server.displayName.isEmpty() ? c.me : _server.displayName;
    c.registry = _registry;
    c.ganttResources = _gantt.resources;
    const double hoursPerPt = _gantt.settings.value(QLatin1String("hours_per_pt")).toDouble(8.0);
    c.hoursPerPt = hoursPerPt > 0 ? hoursPerPt : 8.0;
    c.now = now();
    c.zone = _zone;
    for (const auto &cal : _calendar.calendars) {
        c.calendarNames.insert(cal.uri, cal.name);
        c.calendarColors.insert(cal.uri, cal.color);
    }
    return c;
}

void AccountAgenda::refresh(bool manual)
{
    if (_refreshing) {
        return;
    }
    if (!_online) {
        rebuild();
        Q_EMIT refreshFinished();
        return;
    }
    _refreshing = true;
    _refreshSequence = _edit->appliedSequence();
    _manual = manual;
    _state.refreshing = true;
    _state.needsLogin = false;
    Q_EMIT stateChanged();

    const bool needServer = !_server.loaded || manual || !_server.fetchedAt.isValid() || _server.fetchedAt.secsTo(now()) > 3600;
    if (!needServer) {
        startSources();
        return;
    }
    QPointer<AccountAgenda> self(this);
    ServerInfoClient::fetch(_transport.get(), _server, [self](const ServerInfoResult &r) {
        if (!self) {
            return;
        }
        if (!r.ok) {
            // Everything that was read before stays shown, marked with its time.
            self->failed(QStringLiteral("server"), r.status, r.networkError);
            for (const auto *key : {"calendar", "gantt", "board", "registry"}) {
                const QString k = QString::fromLatin1(key);
                auto s = self->_statuses.value(k);
                if (s.state != SourceStatus::State::NeverLoaded) {
                    self->markStatus(k, SourceStatus::State::Cached, s.savedAt, QString());
                } else {
                    self->markStatus(k, SourceStatus::State::Failed, QDateTime());
                }
            }
            self->_refreshing = false;
            self->rebuild();
            Q_EMIT self->refreshFinished();
            return;
        }
        self->_server = r.info;
        self->_server.fetchedAt = self->now();
        self->_cache.saveServerInfo(self->_server);
        self->startSources();
    });
}

void AccountAgenda::startSources()
{
    QPointer<AccountAgenda> self(this);
    const QString me = _server.uid.isEmpty() ? _transport->davUser() : _server.uid;
    const auto nowStamp = now();
    const auto today = nowStamp.toTimeZone(_zone).date();
    _pending = 4;

    // ---- registry --------------------------------------------------------------------------------------------
    if (!allowed(QStringLiteral("registry"), _manual)) {
        QMetaObject::invokeMethod(this, [self] { if (self) { self->finishPart(); } }, Qt::QueuedConnection);
    } else {
        _registrySource->fetch(_registryCache.etag, [self, nowStamp](const RegistryFetch &f) {
            if (!self) {
                return;
            }
            const QString key = QStringLiteral("registry");
            switch (f.outcome) {
            case RegistryFetch::Outcome::Fetched:
                self->_registryCache.present = true;
                self->_registryCache.bytes = f.bytes;
                self->_registryCache.etag = f.etag;
                self->_registryCache.fetchedAt = nowStamp;
                self->_cache.saveRegistry(self->_registryCache);
                self->_registry = AgentRegistry::parse(f.bytes);
                self->markStatus(key, SourceStatus::State::Fresh, nowStamp);
                self->succeeded(key);
                break;
            case RegistryFetch::Outcome::NotModified:
                self->_registry = self->_registryCache.bytes.isEmpty() ? AgentRegistry::missing() : AgentRegistry::parse(self->_registryCache.bytes);
                self->markStatus(key, SourceStatus::State::Fresh, nowStamp);
                self->succeeded(key);
                break;
            case RegistryFetch::Outcome::Missing:
                self->_registryCache.present = true;
                self->_registryCache.bytes.clear();
                self->_registryCache.etag.clear();
                self->_registryCache.fetchedAt = nowStamp;
                self->_cache.saveRegistry(self->_registryCache);
                self->_registry = AgentRegistry::missing();
                self->markStatus(key, SourceStatus::State::Fresh, nowStamp);
                self->succeeded(key);
                break;
            case RegistryFetch::Outcome::Failed:
                self->failed(key, 0, true);
                self->markStatus(key, SourceStatus::State::Cached, self->_registryCache.fetchedAt);
                break;
            }
            self->finishPart();
        });
    }

    // ---- calendars and to-dos -----------------------------------------------------------------------------------
    if (!allowed(QStringLiteral("calendar"), _manual)) {
        QMetaObject::invokeMethod(this, [self] { if (self) { self->finishPart(); } }, Qt::QueuedConnection);
    } else {
        CalendarReadOptions o;
        o.davUser = _transport->davUser();
        o.zone = _zone;
        CalendarReader::syncWindow(today, &o.from, &o.to);
        o.now = nowStamp;
        o.forceFull = !_lastCalendarFull.isValid() || _lastCalendarFull.secsTo(nowStamp) > 6 * 3600;
        o.wanted = [self](const CalendarInfo &c) {
            return self && self->_settings.calendarMode(c, GanttMapper::taskCopyCalendarUri(self->_gantt)) != CalendarMode::Hidden;
        };
        CalendarReader::read(_transport.get(), o, _calendar, [self, nowStamp, full = o.forceFull](const CalendarReadResult &r) {
            if (!self) {
                return;
            }
            const QString key = QStringLiteral("calendar");
            if (r.ok) {
                self->_calendar = r.snapshot;
                self->_cache.saveCalendar(self->_calendar);
                if (full) {
                    self->_lastCalendarFull = nowStamp;
                }
                self->markStatus(key, SourceStatus::State::Fresh, nowStamp);
                self->succeeded(key);
            } else {
                self->failed(key, r.status, r.networkError);
                self->markStatus(key, self->_calendar.loaded ? SourceStatus::State::Cached : SourceStatus::State::Failed, self->_calendar.fetchedAt);
            }
            self->finishPart();
        });
    }

    // ---- NoGantt ---------------------------------------------------------------------------------------------------
    if (!allowed(QStringLiteral("gantt"), _manual)) {
        QMetaObject::invokeMethod(this, [self] { if (self) { self->finishPart(); } }, Qt::QueuedConnection);
    } else {
        GanttFetchOptions o;
        o.davUser = me;
        o.serverAdmin = _server.isServerAdmin;
        o.groups = _server.groups;
        o.now = nowStamp;
        o.forceSlow = _manual && !_gantt.loaded;
        o.wantProject = [self](const ProjectRelation &relation) {
            return self && self->_settings.projectMode(relation) != WorkMode::Hidden;
        };
        GanttClient::fetch(_transport.get(), o, _gantt, [self, nowStamp](const GanttFetchResult &r) {
            if (!self) {
                return;
            }
            const QString key = QStringLiteral("gantt");
            if (r.ok) {
                self->_gantt = r.snapshot;
                self->_cache.saveGantt(self->_gantt);
                self->markStatus(key, self->_gantt.unavailable ? SourceStatus::State::Unavailable : SourceStatus::State::Fresh, nowStamp);
                self->succeeded(key);
            } else {
                self->failed(key, r.status, r.networkError);
                self->markStatus(key, isRefusal(r.status) ? SourceStatus::State::Refused : (self->_gantt.loaded ? SourceStatus::State::Cached : SourceStatus::State::Failed), self->_gantt.fetchedAt);
            }
            self->finishPart();
        });
    }

    // ---- NoBoard ----------------------------------------------------------------------------------------------------
    if (!allowed(QStringLiteral("board"), _manual)) {
        QMetaObject::invokeMethod(this, [self] { if (self) { self->finishPart(); } }, Qt::QueuedConnection);
    } else {
        DeckFetchOptions o;
        o.davUser = me;
        o.version = _server.deckVersion;
        o.now = nowStamp;
        o.wantBoard = [self](const BoardRelation &relation) {
            return self && self->_settings.boardMode(relation) != WorkMode::Hidden;
        };
        DeckClient::fetch(_transport.get(), o, _deck, [self, nowStamp](const DeckFetchResult &r) {
            if (!self) {
                return;
            }
            const QString key = QStringLiteral("board");
            if (r.ok) {
                self->_deck = r.snapshot;
                self->_cache.saveDeck(self->_deck);
                self->markStatus(key, self->_deck.unavailable ? SourceStatus::State::Unavailable : SourceStatus::State::Fresh, nowStamp);
                self->succeeded(key);
            } else {
                self->failed(key, r.status, r.networkError);
                self->markStatus(key, isRefusal(r.status) ? SourceStatus::State::Refused : (self->_deck.loaded ? SourceStatus::State::Cached : SourceStatus::State::Failed), self->_deck.fetchedAt);
            }
            self->finishPart();
        });
    }
}

// ---- settings ---------------------------------------------------------------------------------------------------------

void AccountAgenda::setCalendarMode(const QString &uri, CalendarMode mode)
{
    _settings.setCalendarMode(uri, mode);
    saveSettings();
    rebuild();
    if (_online && mode != CalendarMode::Hidden && !_refreshing) {
        QPointer<AccountAgenda> self(this);
        QMetaObject::invokeMethod(this, [self] { if (self) { self->_calendar.windowFrom = QDate(); self->refresh(true); } }, Qt::QueuedConnection);
    }
}

void AccountAgenda::setProjectMode(int id, WorkMode mode)
{
    _settings.setProjectMode(id, mode);
    saveSettings();
    rebuild();
    if (_online && mode != WorkMode::Hidden && !_refreshing) {
        QPointer<AccountAgenda> self(this);
        QMetaObject::invokeMethod(this, [self] { if (self) { self->refresh(true); } }, Qt::QueuedConnection);
    }
}

void AccountAgenda::setBoardMode(int id, WorkMode mode)
{
    _settings.setBoardMode(id, mode);
    saveSettings();
    rebuild();
    if (_online && mode != WorkMode::Hidden && !_refreshing) {
        QPointer<AccountAgenda> self(this);
        QMetaObject::invokeMethod(this, [self] { if (self) { self->refresh(true); } }, Qt::QueuedConnection);
    }
}

void AccountAgenda::answerPersonalQuestion(const QStringList &personalUris, bool skipped)
{
    QStringList own;
    for (const auto &c : _state.build.ownCalendars) {
        own.append(c.first);
    }
    _settings.answerPersonalQuestion(personalUris, own, skipped);
    saveSettings();
    rebuild();
}

void AccountAgenda::setWorkingHours(const WorkingHours &hours)
{
    _settings.setWorkingHours(hours);
    saveSettings();
    rebuild();
}

void AccountAgenda::setView(const QString &view)
{
    _settings.lastView = view;
    saveSettings();
}

void AccountAgenda::setCategory(const QString &category)
{
    _settings.lastCategory = category;
    saveSettings();
}

void AccountAgenda::setChips(bool me, bool agents, bool team)
{
    _settings.chipMe = me;
    _settings.chipAgents = agents;
    _settings.chipTeam = team;
    saveSettings();
}

void AccountAgenda::setShowAllTeam(bool show)
{
    _settings.showAllTeam = show;
    saveSettings();
}

void AccountAgenda::markOpened()
{
    if (!_settings.everOpened) {
        _settings.everOpened = true;
        saveSettings();
    }
}

void AccountAgenda::removeCache()
{
    _cache.removeAll();
}

// ---- "Open in ..." -------------------------------------------------------------------------------------------------------

QString AccountAgenda::openLabelFor(const AgendaItem &item)
{
    switch (item.home) {
    case Home::Calendar:
        return AgendaText::tr("Open in NoCalendar");
    case Home::Tasks:
        return AgendaText::tr("Open in NoTasks");
    case Home::Gantt:
        return AgendaText::tr("Open in NoGantt");
    case Home::Board:
        return AgendaText::tr("Open in NoBoard");
    }
    return QString();
}

QUrl AccountAgenda::appUrl(Home home) const
{
    switch (home) {
    case Home::Calendar:
        return _transport->url(QStringLiteral("index.php/apps/calendar/"));
    case Home::Tasks:
        return _transport->url(QStringLiteral("index.php/apps/tasks/"));
    case Home::Gantt:
        return _transport->url(QStringLiteral("index.php/apps/nc_gantt/"));
    case Home::Board:
        return _transport->url(QStringLiteral("index.php/apps/deck/"));
    }
    return _transport->serverUrl();
}

QUrl AccountAgenda::openUrlFor(const AgendaItem &item) const
{
    switch (item.home) {
    case Home::Calendar: {
        const QString href = QStringLiteral("/remote.php/dav/calendars/") + QString::fromUtf8(QUrl::toPercentEncoding(_transport->davUser())) + QLatin1Char('/')
            + QString::fromUtf8(QUrl::toPercentEncoding(item.calendarUri)) + QLatin1Char('/') + QString::fromUtf8(QUrl::toPercentEncoding(item.objectUri));
        const QByteArray encoded = QByteArray((_transport->serverUrl().path() + href).toUtf8()).toBase64();
        QString path = QStringLiteral("index.php/apps/calendar/edit/") + QString::fromLatin1(QUrl::toPercentEncoding(QString::fromLatin1(encoded)));
        if (item.repeating && item.start.isValid()) {
            path += QLatin1Char('/') + QString::number(item.start.toSecsSinceEpoch());
        }
        return _transport->url(path);
    }
    case Home::Tasks:
    {
        auto url = _transport->url(QStringLiteral("index.php/apps/tasks/"));
        url.setFragment(QStringLiteral("/calendars/") + item.calendarUri + QStringLiteral("/tasks/") + item.objectUri);
        return url;
    }
    case Home::Gantt: {
        QString path = QStringLiteral("index.php/apps/nc_gantt/?project=") + QString::number(item.projectId);
        if (item.kind == ItemKind::GanttTask && item.taskId > 0 && !item.key.contains(QLatin1String("notshared")) && !item.key.contains(QLatin1String("capacity"))) {
            path += QStringLiteral("&task=") + QString::number(item.taskId);
        }
        return _transport->url(path);
    }
    case Home::Board:
        return _transport->url(QStringLiteral("index.php/apps/deck/card/") + QString::number(item.cardId));
    }
    return _transport->serverUrl();
}

} // namespace OCC::Nourax::Agenda
