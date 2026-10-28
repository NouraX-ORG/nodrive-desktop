/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "noagendawindow.h"

#include "agendacontroller.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QVariantMap>

// Q_INIT_RESOURCE needs the global namespace.
static void nodriveInitNoAgendaQrc()
{
    Q_INIT_RESOURCE(noagenda);
}

namespace OCC::Nourax::Agenda {

void initNoAgendaResources()
{
    static bool done = false;
    if (!done) {
        nodriveInitNoAgendaQrc();
        done = true;
    }
}

QQuickWindow *createNoAgendaWindow(QQmlEngine *engine, AgendaController *controller, QString *error)
{
    initNoAgendaResources();
    QQmlComponent component(engine, QUrl(QStringLiteral("qrc:/qml/nourax/noagenda/NoAgendaWindow.qml")));
    if (component.isError()) {
        if (error) {
            *error = component.errorString();
        }
        return nullptr;
    }
    const QVariantMap initial{{QStringLiteral("ctrl"), QVariant::fromValue(static_cast<QObject *>(controller))}};
    QObject *created = component.createWithInitialProperties(initial);
    auto *window = qobject_cast<QQuickWindow *>(created);
    if (!window) {
        if (error) {
            *error = component.errorString().isEmpty() ? QStringLiteral("the NoAgenda window could not be created") : component.errorString();
        }
        delete created;
        return nullptr;
    }
    return window;
}

} // namespace OCC::Nourax::Agenda
