/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoDrive problem reports: the one object that ties the pieces together.
//
//  - start() (called once from Application) checks how the last run ended, installs the
//    crash handlers, and asks (or, with the switch on, sends) after a crash or unclean exit;
//  - submit() builds a report on a worker thread and sends it through the signed-in account;
//  - reports wait in <config>/problem-reports/pending while nobody is signed in or the
//    network is down, and are sent when an account becomes connected.
//
// No timer, no ping and no request is made while there is nothing to send.

#pragma once

#include "accountfwd.h"
#include "problemreportbuilder.h"
#include "runmarker.h"

#include <QObject>
#include <QQueue>
#include <QString>

#include <functional>
#include <memory>
#include <optional>

class QMessageBox;
class QWidget;

namespace OCC {
class AccountState;
}

namespace OCC::Nourax {

class ProblemReportManager : public QObject
{
    Q_OBJECT

public:
    enum class Outcome {
        Sent, ///< uploaded and shared with Support
        SavedNotShared, ///< uploaded, share to Support failed quietly
        SavedLocally, ///< nobody is signed in: waits for the next sign-in
        TryAgainLater, ///< could not be sent now: waits until an account is connected
    };
    Q_ENUM(Outcome)

    static ProblemReportManager *instance();

    explicit ProblemReportManager(QObject *parent = nullptr);
    ~ProblemReportManager() override;

    /// Called once from Application, after the tray is set up.
    void start();

    /// Builds a report and sends it. Returns an id that submissionFinished() repeats.
    quint64 submit(ProblemReportInfo info, bool showTrayMessage);

    /// Sends the reports waiting in the pending folder (used when an account becomes connected).
    void sendPending();

    [[nodiscard]] bool hasPendingReports() const;
    [[nodiscard]] QString pendingDirectory() const;
    [[nodiscard]] static QString outcomeText(Outcome outcome);
    [[nodiscard]] static QString closedUnexpectedlyText();
    /// The "closed unexpectedly" question with the buttons Send (default) and Not now (Escape).
    /// Not shown yet; the caller shows it.
    [[nodiscard]] static QMessageBox *createClosedUnexpectedlyPrompt(QWidget *parent = nullptr);

    // Seams for the unit tests.
    void setPendingDirectory(const QString &dir);
    void setSourcesProvider(std::function<ProblemReportSources()> provider);
    void setAccountProvider(std::function<AccountPtr()> provider);

Q_SIGNALS:
    void submissionFinished(quint64 id, OCC::Nourax::ProblemReportManager::Outcome outcome);
    /// A short message for the tray ("Problem report sent").
    void userMessage(const QString &text);

private Q_SLOTS:
    void handleUncleanExit();

private:
    struct Request {
        quint64 id = 0;
        bool build = true;
        bool showTray = true;
        ProblemReportInfo info;
        ProblemReportSources sources;
    };

    void process();
    void onBuilt(const Request &request, const QString &path);
    void sendAll(const Request &request);
    void sendNext(const AccountPtr &account, QStringList zips, std::optional<Outcome> best, const Request &request);
    void finish(const Request &request, Outcome outcome, bool uploaded);
    void watchAccount(AccountState *state);
    [[nodiscard]] AccountPtr connectedAccount() const;
    [[nodiscard]] QString serverHost() const;
    [[nodiscard]] QStringList pendingZips() const;

    std::unique_ptr<RunMarker> _marker;
    RunMarker::Previous _previous;
    QQueue<Request> _queue;
    bool _working = false;
    bool _started = false;
    quint64 _nextId = 1;
    QString _pendingDir;
    std::function<ProblemReportSources()> _sourcesProvider;
    std::function<AccountPtr()> _accountProvider;
};

} // namespace OCC::Nourax
