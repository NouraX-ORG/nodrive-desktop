/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "appleprofilebuilder.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QUuid>
#include <QXmlStreamWriter>

namespace OCC::Nourax::DeviceSync {

namespace {

QString basePath(const QUrl &server)
{
    auto path = server.path();
    while (path.endsWith(QLatin1Char('/'))) {
        path.chop(1);
    }
    return path;
}

QString hostAndPort(const QUrl &server)
{
    const auto defaultPort = server.scheme() == QLatin1String("http") ? 80 : 443;
    const auto port = server.port(defaultPort);
    return port == defaultPort ? server.host() : QStringLiteral("%1:%2").arg(server.host()).arg(port);
}

QString davUrl(const QUrl &server, const QString &davUser, const QString &what)
{
    return QStringLiteral("%1://%2%3/remote.php/dav/%4/%5/")
        .arg(server.scheme(), hostAndPort(server), basePath(server), what, QString::fromUtf8(QUrl::toPercentEncoding(davUser)));
}

QString newUuid()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).toUpper();
}

void key(QXmlStreamWriter &xml, const QString &name)
{
    xml.writeTextElement(QStringLiteral("key"), name);
}

void stringValue(QXmlStreamWriter &xml, const QString &name, const QString &value)
{
    key(xml, name);
    xml.writeTextElement(QStringLiteral("string"), value);
}

void integerValue(QXmlStreamWriter &xml, const QString &name, int value)
{
    key(xml, name);
    xml.writeTextElement(QStringLiteral("integer"), QString::number(value));
}

void boolValue(QXmlStreamWriter &xml, const QString &name, bool value)
{
    key(xml, name);
    xml.writeEmptyElement(value ? QStringLiteral("true") : QStringLiteral("false"));
}

void accountPayload(QXmlStreamWriter &xml, const QString &prefix, const AppleProfileInput &in, const QString &topIdentifier,
    const QString &displayName)
{
    const auto useSsl = in.server.scheme() != QLatin1String("http");
    const auto port = in.server.port(useSsl ? 443 : 80);
    const auto lower = prefix.toLower(); // caldav, carddav
    xml.writeStartElement(QStringLiteral("dict"));
    stringValue(xml, prefix + QStringLiteral("AccountDescription"), QStringLiteral("NoDrive"));
    stringValue(xml, prefix + QStringLiteral("HostName"), in.server.host());
    stringValue(xml, prefix + QStringLiteral("Password"), QString::fromUtf8(in.appPassword));
    integerValue(xml, prefix + QStringLiteral("Port"), port);
    stringValue(xml, prefix + QStringLiteral("PrincipalURL"), AppleProfileBuilder::principalUrl(in.server, in.davUser));
    boolValue(xml, prefix + QStringLiteral("UseSSL"), useSsl);
    stringValue(xml, prefix + QStringLiteral("Username"), in.loginName);
    stringValue(xml, QStringLiteral("PayloadType"), QStringLiteral("com.apple.%1.account").arg(lower));
    integerValue(xml, QStringLiteral("PayloadVersion"), 1);
    stringValue(xml, QStringLiteral("PayloadIdentifier"), topIdentifier + QLatin1Char('.') + lower);
    stringValue(xml, QStringLiteral("PayloadUUID"), newUuid());
    stringValue(xml, QStringLiteral("PayloadDisplayName"), displayName);
    xml.writeEndElement(); // dict
}

} // namespace

QStringList AppleProfileBuilder::accountKeys(const QString &prefix)
{
    return {
        prefix + QStringLiteral("AccountDescription"),
        prefix + QStringLiteral("HostName"),
        prefix + QStringLiteral("Password"),
        prefix + QStringLiteral("Port"),
        prefix + QStringLiteral("PrincipalURL"),
        prefix + QStringLiteral("UseSSL"),
        prefix + QStringLiteral("Username"),
    };
}

QString AppleProfileBuilder::payloadIdentifier(const QString &bundleId, const QString &accountId)
{
    const auto hash = QCryptographicHash::hash(accountId.toUtf8(), QCryptographicHash::Sha256).toHex().left(16);
    return bundleId + QStringLiteral(".davprofile.") + QString::fromLatin1(hash);
}

QString AppleProfileBuilder::principalUrl(const QUrl &server, const QString &davUser)
{
    return davUrl(server, davUser, QStringLiteral("principals/users"));
}

QString AppleProfileBuilder::calendarHomeUrl(const QUrl &server, const QString &davUser)
{
    return davUrl(server, davUser, QStringLiteral("calendars"));
}

QString AppleProfileBuilder::addressBookHomeUrl(const QUrl &server, const QString &davUser)
{
    return davUrl(server, davUser, QStringLiteral("addressbooks/users"));
}

QByteArray AppleProfileBuilder::build(const AppleProfileInput &input)
{
    if (input.server.host().isEmpty() || input.davUser.isEmpty() || input.loginName.isEmpty() || input.appPassword.isEmpty()
        || input.bundleId.isEmpty()) {
        return {};
    }

    const auto identifier = payloadIdentifier(input.bundleId, input.accountId);

    QByteArray out;
    QBuffer buffer(&out);
    buffer.open(QIODevice::WriteOnly);
    QXmlStreamWriter xml(&buffer);
    xml.setAutoFormatting(true);
    xml.setAutoFormattingIndent(2);
    xml.writeStartDocument(QStringLiteral("1.0"));
    xml.writeDTD(QStringLiteral("<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">"));
    xml.writeStartElement(QStringLiteral("plist"));
    xml.writeAttribute(QStringLiteral("version"), QStringLiteral("1.0"));
    xml.writeStartElement(QStringLiteral("dict"));

    key(xml, QStringLiteral("PayloadContent"));
    xml.writeStartElement(QStringLiteral("array"));
    accountPayload(xml, QStringLiteral("CalDAV"), input, identifier, QStringLiteral("NoDrive Calendar"));
    accountPayload(xml, QStringLiteral("CardDAV"), input, identifier, QStringLiteral("NoDrive Contacts"));
    xml.writeEndElement(); // array

    stringValue(xml, QStringLiteral("PayloadDescription"), QStringLiteral("Adds your NoDrive calendars and contacts to Calendar and Contacts on this Mac."));
    stringValue(xml, QStringLiteral("PayloadDisplayName"), QStringLiteral("NoDrive Calendar & Contacts"));
    stringValue(xml, QStringLiteral("PayloadIdentifier"), identifier);
    stringValue(xml, QStringLiteral("PayloadOrganization"), QStringLiteral("Nourax"));
    boolValue(xml, QStringLiteral("PayloadRemovalDisallowed"), false);
    stringValue(xml, QStringLiteral("PayloadType"), QStringLiteral("Configuration"));
    stringValue(xml, QStringLiteral("PayloadUUID"), newUuid());
    integerValue(xml, QStringLiteral("PayloadVersion"), 1);

    xml.writeEndElement(); // dict
    xml.writeEndElement(); // plist
    xml.writeEndDocument();
    buffer.close();
    return out;
}

} // namespace OCC::Nourax::DeviceSync
