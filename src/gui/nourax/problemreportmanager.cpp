/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "problemreportmanager.h"

#include "problemreportsettings.h"
#include "problemreportuploader.h"

#include "account.h"
#include "accountmanager.h"
#include "accountstate.h"
#include "config.h"
#include "systray.h"
#include "theme.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QMessageBox>
#include <QPointer>
#include <QThreadPool>
#include <QTimer>

#include <utility>

namespace OCC::Nourax {

Q_LOGGING_CATEGORY(lcProblemReportManager, "nodrive.problemreports.manager", QtInfoMsg)

namespace {
constexpr int promptDelayMs = 3000;

QString appName()
{
    return Theme::instance()->appNameGUI();
}
}

ProblemReportManager *ProblemReportManager::instance()
{
    static ProblemReportManager manager;
    return &manager;
}

ProblemReportManager::ProblemReportManager(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<OCC::Nourax::ProblemReportManager::Outcome>("OCC::Nourax::ProblemReportManager::Outcome");
}

ProblemReportManager::~ProblemReportManager() = default;

QString ProblemReportManager::outcomeText(Outcome outcome)
{
    switch (outcome) {
    case Outcome::Sent:
        return tr("Problem report sent");
    case Outcome::SavedNotShared:
        return tr("Saved in your %1 › Problem reports.").arg(appName());
    case Outcome::SavedLocally:
        return tr("Saved on this computer. %1 sends it after you sign in.").arg(appName());
    case Outcome::TryAgainLater:
        break;
    }
    return tr("Could not send the problem report now. %1 will try again when it is connected.").arg(appName());
}

QString ProblemReportManager::closedUnexpectedlyText()
{
    return tr("%1 closed unexpectedly. Send a problem report?").arg(appName());
}

QMessageBox *ProblemReportManager::createClosedUnexpectedlyPrompt(QWidget *parent)
{
    auto *box = new QMessageBox(QMessageBox::Question, appName(), closedUnexpectedlyText(), QMessageBox::NoButton, parent);
    box->setObjectName(QStringLiteral("problemReportPrompt"));
    auto *sendButton = box->addButton(tr("Send"), QMessageBox::AcceptRole);
    auto *notNowButton = box->addButton(tr("Not now"), QMessageBox::RejectRole);
    box->setDefaultButton(sendButton);
    box->setEscapeButton(notNowButton);
    return box;
}

QString ProblemReportManager::pendingDirectory() const
{
    return _pendingDir.isEmpty() ? ProblemReportSettings::pendingDir() : _pendingDir;
}

void ProblemReportManager::setPendingDirectory(const QString &dir)
{
    _pendingDir = dir;
}

void ProblemReportManager::setSourcesProvider(std::function<ProblemReportSources()> provider)
{
    _sourcesProvider = std::move(provider);
}

void ProblemReportManager::setAccountProvider(std::function<AccountPtr()> provider)
{
    _accountProvider = std::move(provider);
}

QStringList ProblemReportManager::pendingZips() const
{
    // Oldest first; the names start with the UTC time.
    const QDir dir(pendingDirectory());
    const auto names = dir.entryList(QStringList{ QStringLiteral("*.zip") }, QDir::Files, QDir::Name);
    QStringList paths;
    for (const auto &name : names) {
        paths << dir.absoluteFilePath(name);
    }
    return paths;
}

bool ProblemReportManager::hasPendingReports() const
{
    return !pendingZips().isEmpty();
}

AccountPtr ProblemReportManager::connectedAccount() const
{
    if (_accountProvider) {
        return _accountProvider();
    }
    const auto accounts = AccountManager::instance()->accounts();
    for (const auto &state : accounts) {
        if (state && state->isConnected()) {
            return state->account();
        }
    }
    return {};
}

QString ProblemReportManager::serverHost() const
{
    if (const auto account = connectedAccount()) {
        return account->url().host();
    }
    if (!_accountProvider) {
        const auto accounts = AccountManager::instance()->accounts();
        if (!accounts.isEmpty() && accounts.first() && accounts.first()->account()) {
            return accounts.first()->account()->url().host();
        }
    }
    return {};
}

void ProblemReportManager::start()
{
    if (_started) {
        return;
    }
    _started = true;

    const auto reportsDir = ProblemReportSettings::reportsDir();
    const auto fatalLog = QDir::tempPath() + QStringLiteral("/" APPLICATION_NAME "-crash.log");
    _marker = std::make_unique<RunMarker>(reportsDir, RunMarker::currentBuild(), fatalLog);
    _previous = _marker->inspectPrevious();
    _marker->markRunning();
    _marker->installHandlers();
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
        _marker->markClean();
    });

    connect(this, &ProblemReportManager::userMessage, this, [](const QString &text) {
        Systray::instance()->showMessage(appName(), text);
    });

    // Reports that wait for a connection are sent when an account becomes connected.
    const auto accounts = AccountManager::instance()->accounts();
    for (const auto &state : accounts) {
        watchAccount(state.data());
    }
    connect(AccountManager::instance(), &AccountManager::accountAdded, this, &ProblemReportManager::watchAccount);

    if (_previous.run == RunMarker::PreviousRun::Crash || _previous.run == RunMarker::PreviousRun::Unclean) {
        qCInfo(lcProblemReportManager) << "the last run did not end cleanly:"
                                       << (_previous.run == RunMarker::PreviousRun::Crash ? "crash" : "unclean exit");
        QTimer::singleShot(promptDelayMs, this, &ProblemReportManager::handleUncleanExit);
    } else if (_previous.run == RunMarker::PreviousRun::OtherBuild) {
        qCInfo(lcProblemReportManager) << "updated since the last run, not a problem";
    }
}

void ProblemReportManager::watchAccount(AccountState *state)
{
    if (!state) {
        return;
    }
    connect(state, &AccountState::stateChanged, this, [this, state] {
        if (state->isConnected() && hasPendingReports()) {
            sendPending();
        }
    });
}

void ProblemReportManager::handleUncleanExit()
{
    ProblemReportInfo info;
    info.kind = _previous.run == RunMarker::PreviousRun::Crash ? ReportKind::Crash : ReportKind::UncleanExit;
    info.crashText = _previous.crashText;

    if (ProblemReportSettings::enabled()) {
        _marker->closeCase();
        submit(info, true);
        return;
    }

    // Switch off: ask once. Either answer closes the case.
    auto *box = createClosedUnexpectedlyPrompt();
    box->setAttribute(Qt::WA_DeleteOnClose);
    connect(box, &QDialog::finished, this, [this, box, info] {
        const auto send = box->clickedButton() == static_cast<QAbstractButton *>(box->defaultButton());
        _marker->closeCase();
        if (send) {
            submit(info, true);
        }
    });
    box->setModal(false);
    box->show();
    box->raise();
    box->activateWindow();
}

quint64 ProblemReportManager::submit(ProblemReportInfo info, bool showTrayMessage)
{
    Request request;
    request.id = _nextId++;
    request.build = true;
    request.showTray = showTrayMessage;
    request.info = std::move(info);
    request.info.serverHost = serverHost();
    request.sources = _sourcesProvider ? _sourcesProvider() : ProblemReportSources::live();
    _queue.enqueue(request);
    process();
    return request.id;
}

void ProblemReportManager::sendPending()
{
    for (const auto &queued : std::as_const(_queue)) {
        if (!queued.build) {
            return; // a send is already waiting
        }
    }
    Request request;
    request.id = _nextId++;
    request.build = false;
    request.showTray = true;
    _queue.enqueue(request);
    process();
}

void ProblemReportManager::process()
{
    if (_working || _queue.isEmpty()) {
        return;
    }
    _working = true;
    auto request = _queue.dequeue();

    if (!request.build) {
        sendAll(request);
        return;
    }

    const auto dir = pendingDirectory();
    QPointer<ProblemReportManager> self(this);
    QThreadPool::globalInstance()->start([self, request, dir] {
        QString error;
        const auto path = ProblemReportBuilder::build(request.info, request.sources, dir, &error);
        if (!error.isEmpty()) {
            qCWarning(lcProblemReportManager) << "could not make the report:" << error;
        }
        if (!self) {
            return;
        }
        QMetaObject::invokeMethod(self.data(), [self, request, path] {
            if (self) {
                self->onBuilt(request, path);
            }
        }, Qt::QueuedConnection);
    });
}

void ProblemReportManager::onBuilt(const Request &request, const QString &path)
{
    if (path.isEmpty()) {
        finish(request, Outcome::TryAgainLater, false);
        return;
    }
    sendAll(request);
}

void ProblemReportManager::sendAll(const Request &request)
{
    const auto account = connectedAccount();
    const auto zips = pendingZips();
    if (zips.isEmpty()) {
        finish(request, Outcome::SavedLocally, false);
        return;
    }
    if (!account) {
        finish(request, Outcome::SavedLocally, false);
        return;
    }
    sendNext(account, zips, std::nullopt, request);
}

void ProblemReportManager::sendNext(const AccountPtr &account, QStringList zips, std::optional<Outcome> best, const Request &request)
{
    if (zips.isEmpty()) {
        finish(request, best.value_or(Outcome::TryAgainLater), best.has_value());
        return;
    }
    const auto path = zips.takeFirst();
    auto *uploader = new ProblemReportUploader(account, path, this);
    connect(uploader, &ProblemReportUploader::finished, this,
        [this, uploader, account, path, zips, best, request](ProblemReportUploader::Result result, const QString &) {
            uploader->deleteLater();
            if (result == ProblemReportUploader::Result::NetworkError) {
                // The report stays in the pending folder until an account is connected again.
                finish(request, best.value_or(Outcome::TryAgainLater), best.has_value());
                return;
            }
            // Uploaded: remove the app's own pending copy (never a user file, never anything on the Drive).
            QFile::remove(path);
            auto next = best;
            if (result == ProblemReportUploader::Result::Sent) {
                if (!next) {
                    next = Outcome::Sent;
                }
            } else {
                next = Outcome::SavedNotShared;
            }
            sendNext(account, zips, next, request);
        });
    uploader->start();
}

void ProblemReportManager::finish(const Request &request, Outcome outcome, bool uploaded)
{
    if (request.showTray && (request.build || uploaded)) {
        Q_EMIT userMessage(outcomeText(outcome));
    }
    if (request.build) {
        Q_EMIT submissionFinished(request.id, outcome);
    }
    _working = false;
    QTimer::singleShot(0, this, &ProblemReportManager::process);
}

} // namespace OCC::Nourax
