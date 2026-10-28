/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: what is read from NoGantt 2.4.1 (project planning), and its offline-cache form.

#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QString>

namespace OCC::Nourax::Agenda {

struct GanttProjectData
{
    QJsonArray tasks;
    QJsonArray milestones;
    QJsonArray subprojects;
    QJsonArray shares;
    bool sharesKnown = false;
    QDateTime sharesFetchedAt;
    bool loaded = false;
};

struct GanttSnapshot
{
    bool loaded = false;
    bool unavailable = false; ///< the app is not on the server
    bool contractOk = true; ///< every response had the 2.4.1 keys and types
    QString contractProblem; ///< "endpoint: missing key x" (no values)
    QDateTime fetchedAt;
    QDateTime slowFetchedAt;
    bool hasLinkedResource = true;
    QJsonArray myTasks;
    QJsonArray projects;
    QJsonArray resources;
    QJsonArray workSchedules;
    QJsonArray holidays;
    QJsonArray workCalendars;
    QJsonObject settings;
    QJsonObject taskSync;
    QJsonObject capacity;
    QMap<int, GanttProjectData> projectData;
    QSet<int> failedProjects;

    [[nodiscard]] QJsonObject toJson() const;
    static GanttSnapshot fromJson(const QJsonObject &o);
};

} // namespace OCC::Nourax::Agenda
