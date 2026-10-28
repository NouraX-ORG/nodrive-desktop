/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "noagendalauncher.h"

#include "../agendaservice.h"
#include "agendacontroller.h"
#include "noagendawindow.h"

#include "account.h"
#include "accountmanager.h"
#include "accountstate.h"
#include "systray.h"
#include "theme.h"
#include "tray/usermodel.h"

#ifdef Q_OS_MACOS
#include "foregroundbackground_interface.h"
#endif

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QMap>
#include <QMenu>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQuickWindow>

namespace OCC::Nourax::Agenda {

Q_LOGGING_CATEGORY(lcAgendaLauncher, "nodrive.agenda.launcher", QtInfoMsg)

namespace {

QMap<QString, QPointer<QQuickWindow>> &windows()
{
    static QMap<QString, QPointer<QQuickWindow>> map;
    return map;
}

} // namespace

QString NoAgendaLauncher::trayEntryText()
{
    return QCoreApplication::translate("NoAgendaLauncher", "NoAgenda");
}

void NoAgendaLauncher::addContextMenuEntry(QMenu *menu)
{
    if (!menu) {
        return;
    }
    auto *action = menu->addAction(trayEntryText(), menu, [] { NoAgendaLauncher::openForCurrentUser(); });
    action->setVisible(!AccountManager::instance()->accounts().isEmpty());
    QObject::connect(menu, &QMenu::aboutToShow, action, [action] {
        action->setVisible(!AccountManager::instance()->accounts().isEmpty());
    });
}

void NoAgendaLauncher::openForCurrentUser()
{
    const auto model = UserModel::instance();
    if (!model) {
        return;
    }
    openForUserIndex(model->currentUserId());
}

void NoAgendaLauncher::openForUserIndex(int userIndex)
{
    const auto model = UserModel::instance();
    if (!model) {
        return;
    }
    const auto user = model->user(userIndex >= 0 ? userIndex : model->currentUserId());
    if (!user) {
        qCWarning(lcAgendaLauncher) << "no account for the NoAgenda window";
        return;
    }
    const auto accountState = user->accountState();
    if (!accountState || !accountState->account()) {
        return;
    }
    auto *systray = Systray::instance();
    systray->hideWindow();
    if (!systray->trayEngine()) {
        qCWarning(lcAgendaLauncher) << "no QML engine for the NoAgenda window";
        return;
    }

    const auto key = accountState->account()->id();
    if (const auto existing = windows().value(key)) {
        systray->positionWindowAtScreenCenter(existing.data());
        existing->show();
        existing->raise();
        existing->requestActivate();
        if (auto *agenda = AgendaService::instance()->agendaFor(accountState.data())) {
            agenda->refresh(false);
        }
        return;
    }

    auto *agenda = AgendaService::instance()->agendaFor(accountState.data());
    if (!agenda) {
        return;
    }
    agenda->markOpened();

    auto *controller = new AgendaController(agenda);
    controller->setAccountName(accountState->account()->displayName());
    QString error;
    auto *window = createNoAgendaWindow(systray->trayEngine(), controller, &error);
    if (!window) {
        qCWarning(lcAgendaLauncher) << "could not create the NoAgenda window:" << error;
        delete controller;
        return;
    }
    controller->setParent(window); // lives and dies with the window
    windows().insert(key, window);
    window->setIcon(Theme::instance()->applicationIcon());

#ifdef Q_OS_MACOS
    auto *fgbg = new ForegroundBackground(window);
    window->installEventFilter(fgbg);
#endif

    QObject::connect(controller, &AgendaController::openUrlRequested, controller, [](const QUrl &url) {
        Systray::instance()->openUrlInBrowser(url);
    });
    QObject::connect(controller, &AgendaController::loginRequested, controller, [] {
        Systray::instance()->openSettings();
    });
    QObject::connect(window, &QObject::destroyed, window, [key] {
        windows().remove(key);
    });

    systray->positionWindowAtScreenCenter(window);
    window->show();
    window->raise();
    window->requestActivate();
    agenda->refresh(false);
}

} // namespace OCC::Nourax::Agenda
