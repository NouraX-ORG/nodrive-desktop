/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: WebDAV / CalDAV request bodies and multistatus parsing (Qt XML streams).

#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QMap>
#include <QPair>
#include <QString>
#include <QStringList>

namespace OCC::Nourax::Agenda {

namespace Dav {
inline QString nsDav() { return QStringLiteral("DAV:"); }
inline QString nsCalDav() { return QStringLiteral("urn:ietf:params:xml:ns:caldav"); }
inline QString nsCs() { return QStringLiteral("http://calendarserver.org/ns/"); }
inline QString nsApple() { return QStringLiteral("http://apple.com/ns/ical/"); }
inline QString nsOc() { return QStringLiteral("http://owncloud.org/ns"); }
inline QString nsNc() { return QStringLiteral("http://nextcloud.org/ns"); }
/// "{namespace}local", the key of a property in DavResponse::props.
inline QString key(const QString &ns, const QString &local) { return QLatin1Char('{') + ns + QLatin1Char('}') + local; }
}

struct DavProp
{
    QString text; ///< the text directly inside the property
    QStringList descendants; ///< local names of every element inside it
    QStringList hrefs; ///< text of every <href> inside it
    QStringList compNames; ///< "name" attribute of every <comp> inside it
    [[nodiscard]] bool has(const QString &local) const { return descendants.contains(local); }
};

struct DavResponse
{
    QString href;
    int status = 0; ///< status of the whole response (for example 404 of a removed href), 0 if none
    QMap<QString, DavProp> props; ///< properties that the server answered with a 2xx status
    QStringList missing; ///< keys of properties answered with 404
};

struct DavMultistatus
{
    bool valid = false;
    QList<DavResponse> responses;
    QString syncToken;
    QString errorCondition; ///< for an <error> body: the first condition element, such as valid-sync-token
};

class CalDavXml
{
public:
    /// PROPFIND body: each entry is {namespace, local name}.
    static QByteArray propfindBody(const QList<QPair<QString, QString>> &props);
    /// calendar-query for VEVENT with a time range and expanded recurrences (answers are in UTC).
    static QByteArray eventQueryBody(const QDateTime &fromUtc, const QDateTime &toUtc);
    /// calendar-query for VTODO that are not completed. Never expanded: the server drops every VTODO on expand.
    static QByteArray todoQueryBody();

    static DavMultistatus parseMultistatus(const QByteArray &xml);
    /// The status code of "HTTP/1.1 207 Multi-Status" style text; 0 if it cannot be read.
    static int statusCodeOf(const QString &statusLine);
};

} // namespace OCC::Nourax::Agenda
