/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: one account's readers, cache, merge and status.

#pragma once

#include "edit/editservice.h"
#include "logic/agendabuilder.h"
#include "logic/conflicts.h"
#include "net/httptransport.h"
#include "registry/registrysource.h"
#include "store/agendacache.h"

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>

#include <functional>
#include <memory>

namespace OCC::Nourax::Agenda {

/// Everything a view needs.
struct AgendaState
{
    QList<AgendaItem> items;
    QList<SourceEntry> sources;
    QList<SourceStatus> statuses; ///< calendar | gantt | board | registry
    BuildOutput build;
    AgentRegistry::Status registryStatus = AgentRegistry::Status::Missing;
    int registrySkipped = 0;
    QString personName;
    QString personUid;
    WorkingHours hours;
    DayModel dayModel; ///< the person's working days, holidays and days off (workload and conflicts)
    QDate today;
    QDateTime refreshedAt; ///< the last time anything was read from the server
    bool refreshing = false;
    bool offline = false;
    bool needsLogin = false;
    bool neverLoaded = true;

    [[nodiscard]] const SourceStatus *status(const QString &key) const;
};

class AccountAgenda : public QObject
{
    Q_OBJECT

public:
    /// `transport` is the connection to the account's own server; `cacheDir` is <config>/noagenda/<id>.
    AccountAgenda(std::unique_ptr<HttpTransport> transport, const QString &cacheDir, QObject *parent = nullptr);
    ~AccountAgenda() override;

    [[nodiscard]] const AgendaState &state() const { return _state; }
    [[nodiscard]] SourceSettings settings() const { return _settings; }
    [[nodiscard]] QString cacheDirectory() const { return _cache.directory(); }
    [[nodiscard]] QUrl serverUrl() const { return _transport->serverUrl(); }
    [[nodiscard]] QString davUser() const { return _transport->davUser(); }
    [[nodiscard]] bool isRefreshing() const { return _refreshing; }
    [[nodiscard]] QList<Agent> myAgents() const { return _state.build.myAgents; }

    // Edit stage (plans/noagenda-desktop-plan.md, 2): the edits, the offline queue and the edit conflicts.
    [[nodiscard]] EditService *edit() { return _edit.get(); }
    /// Everything the writers and the editor need besides the command.
    [[nodiscard]] EditContext editContext() const;
    [[nodiscard]] const CalendarSnapshot &calendarSnapshot() const { return _calendar; }
    [[nodiscard]] const GanttSnapshot &ganttSnapshot() const { return _gantt; }
    [[nodiscard]] const DeckSnapshot &deckSnapshot() const { return _deck; }
    [[nodiscard]] const AgentRegistry &registry() const { return _registry; }
    [[nodiscard]] const ServerInfo &serverInfo() const { return _server; }

    /// For tests: the clock, the zone and the e-mail of the account.
    void setClock(const std::function<QDateTime()> &clock) { _clock = clock; }
    void setZone(const QTimeZone &zone) { _zone = zone; }
    void setAccountEmail(const QString &email) { _accountEmail = email; }
    void setRegistrySource(std::unique_ptr<RegistrySource> source) { _registrySource = std::move(source); }

    /// Loads the cache and builds the first state (no network).
    void loadFromCache();
    /// Reads all sources. A manual refresh ignores the back-off.
    void refresh(bool manual);
    /// No network at all (the account is not connected): rows come from the cache.
    void setOnline(bool online);
    [[nodiscard]] bool isOnline() const { return _online; }
    void setNeedsLogin(bool needsLogin);

    // Settings (changes are saved and the rows rebuilt without a new request).
    void setCalendarMode(const QString &uri, CalendarMode mode);
    void setProjectMode(int id, WorkMode mode);
    void setBoardMode(int id, WorkMode mode);
    void answerPersonalQuestion(const QStringList &personalUris, bool skipped);
    void setWorkingHours(const WorkingHours &hours);
    void setView(const QString &view);
    void setCategory(const QString &category);
    void setChips(bool me, bool agents, bool team);
    void setShowAllTeam(bool show);
    void markOpened();

    /// Where "Open in NoCalendar / NoTasks / NoGantt / NoBoard" goes (the account's own server).
    [[nodiscard]] QUrl openUrlFor(const AgendaItem &item) const;
    /// The start page of one of the four apps on the account's own server.
    [[nodiscard]] QUrl appUrl(Home home) const;
    [[nodiscard]] QTimeZone zone() const { return _zone; }
    static QString openLabelFor(const AgendaItem &item);

    /// Removes the cache of this account (NoAgenda's own copy of server data).
    void removeCache();

    // Back-off of a source, in minutes, after `failures` failed reads: 5, 10, 20, then 30.
    static int backoffMinutes(int failures);

    /// 401 or 403: the server (or one of its apps) refuses the sign-in.
    static bool isRefusal(int httpStatus);

Q_SIGNALS:
    void stateChanged();
    void refreshFinished();

private:
    struct SourceRun
    {
        int failures = 0;
        QDateTime nextAllowed;
    };

    [[nodiscard]] QDateTime now() const;
    void rebuild();
    void finishPart();
    void saveSettings();
    void startSources();
    void markStatus(const QString &key, SourceStatus::State state, const QDateTime &savedAt, const QString &message = QString());
    bool allowed(const QString &key, bool manual);
    void failed(const QString &key, int httpStatus, bool networkError);
    void succeeded(const QString &key);

    std::unique_ptr<HttpTransport> _transport;
    AgendaCache _cache;
    std::unique_ptr<RegistrySource> _registrySource;
    std::unique_ptr<EditService> _edit;
    int _refreshSequence = 0;
    std::function<QDateTime()> _clock;
    QTimeZone _zone;
    QString _accountEmail;

    SourceSettings _settings;
    ServerInfo _server;
    AgentRegistry _registry;
    RegistryCache _registryCache;
    CalendarSnapshot _calendar;
    GanttSnapshot _gantt;
    DeckSnapshot _deck;
    AgendaState _state;
    QMap<QString, SourceRun> _runs;
    QMap<QString, SourceStatus> _statuses;
    bool _refreshing = false;
    bool _online = true;
    bool _manual = false;
    int _pending = 0;
    bool _forceSlow = false;
    QDateTime _lastCalendarFull;
    QPointer<QObject> _guard;
    /// The version guard of NoGantt and NoBoard as the last read in this connection found it (Unknown until then).
    AppGuard _ganttGuard = AppGuard::Unknown;
    AppGuard _deckGuard = AppGuard::Unknown;
};

} // namespace OCC::Nourax::Agenda
