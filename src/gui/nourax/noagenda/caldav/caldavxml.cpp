/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "caldavxml.h"

#include <QBuffer>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace OCC::Nourax::Agenda {

namespace {

void declareAll(QXmlStreamWriter &w)
{
    w.writeNamespace(Dav::nsDav(), QStringLiteral("d"));
    w.writeNamespace(Dav::nsCalDav(), QStringLiteral("c"));
    w.writeNamespace(Dav::nsCs(), QStringLiteral("cs"));
    w.writeNamespace(Dav::nsApple(), QStringLiteral("ic"));
    w.writeNamespace(Dav::nsOc(), QStringLiteral("oc"));
    w.writeNamespace(Dav::nsNc(), QStringLiteral("nc"));
}

QString utcStamp(const QDateTime &dt)
{
    return dt.toUTC().toString(QStringLiteral("yyyyMMdd'T'HHmmss'Z'"));
}

} // namespace

QByteArray CalDavXml::propfindBody(const QList<QPair<QString, QString>> &props)
{
    QByteArray out;
    QXmlStreamWriter w(&out);
    w.writeStartDocument();
    w.writeStartElement(Dav::nsDav(), QStringLiteral("propfind"));
    declareAll(w);
    w.writeStartElement(Dav::nsDav(), QStringLiteral("prop"));
    for (const auto &p : props) {
        w.writeEmptyElement(p.first, p.second);
    }
    w.writeEndElement(); // prop
    w.writeEndElement(); // propfind
    w.writeEndDocument();
    return out;
}

QByteArray CalDavXml::eventQueryBody(const QDateTime &fromUtc, const QDateTime &toUtc)
{
    QByteArray out;
    QXmlStreamWriter w(&out);
    w.writeStartDocument();
    w.writeStartElement(Dav::nsCalDav(), QStringLiteral("calendar-query"));
    declareAll(w);
    w.writeStartElement(Dav::nsDav(), QStringLiteral("prop"));
    w.writeEmptyElement(Dav::nsDav(), QStringLiteral("getetag"));
    w.writeStartElement(Dav::nsCalDav(), QStringLiteral("calendar-data"));
    w.writeStartElement(Dav::nsCalDav(), QStringLiteral("expand"));
    w.writeAttribute(QStringLiteral("start"), utcStamp(fromUtc));
    w.writeAttribute(QStringLiteral("end"), utcStamp(toUtc));
    w.writeEndElement(); // expand
    w.writeEndElement(); // calendar-data
    w.writeEndElement(); // prop
    w.writeStartElement(Dav::nsCalDav(), QStringLiteral("filter"));
    w.writeStartElement(Dav::nsCalDav(), QStringLiteral("comp-filter"));
    w.writeAttribute(QStringLiteral("name"), QStringLiteral("VCALENDAR"));
    w.writeStartElement(Dav::nsCalDav(), QStringLiteral("comp-filter"));
    w.writeAttribute(QStringLiteral("name"), QStringLiteral("VEVENT"));
    w.writeEmptyElement(Dav::nsCalDav(), QStringLiteral("time-range"));
    w.writeAttribute(QStringLiteral("start"), utcStamp(fromUtc));
    w.writeAttribute(QStringLiteral("end"), utcStamp(toUtc));
    w.writeEndElement(); // comp-filter VEVENT
    w.writeEndElement(); // comp-filter VCALENDAR
    w.writeEndElement(); // filter
    w.writeEndElement(); // calendar-query
    w.writeEndDocument();
    return out;
}

QByteArray CalDavXml::todoQueryBody()
{
    QByteArray out;
    QXmlStreamWriter w(&out);
    w.writeStartDocument();
    w.writeStartElement(Dav::nsCalDav(), QStringLiteral("calendar-query"));
    declareAll(w);
    w.writeStartElement(Dav::nsDav(), QStringLiteral("prop"));
    w.writeEmptyElement(Dav::nsDav(), QStringLiteral("getetag"));
    w.writeEmptyElement(Dav::nsCalDav(), QStringLiteral("calendar-data"));
    w.writeEndElement(); // prop
    w.writeStartElement(Dav::nsCalDav(), QStringLiteral("filter"));
    w.writeStartElement(Dav::nsCalDav(), QStringLiteral("comp-filter"));
    w.writeAttribute(QStringLiteral("name"), QStringLiteral("VCALENDAR"));
    w.writeStartElement(Dav::nsCalDav(), QStringLiteral("comp-filter"));
    w.writeAttribute(QStringLiteral("name"), QStringLiteral("VTODO"));
    w.writeStartElement(Dav::nsCalDav(), QStringLiteral("prop-filter"));
    w.writeAttribute(QStringLiteral("name"), QStringLiteral("COMPLETED"));
    w.writeEmptyElement(Dav::nsCalDav(), QStringLiteral("is-not-defined"));
    w.writeEndElement(); // prop-filter
    w.writeEndElement(); // comp-filter VTODO
    w.writeEndElement(); // comp-filter VCALENDAR
    w.writeEndElement(); // filter
    w.writeEndElement(); // calendar-query
    w.writeEndDocument();
    return out;
}

int CalDavXml::statusCodeOf(const QString &statusLine)
{
    const auto parts = statusLine.trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.size() >= 2) {
        bool ok = false;
        const int code = parts.at(1).toInt(&ok);
        return ok ? code : 0;
    }
    return 0;
}

DavMultistatus CalDavXml::parseMultistatus(const QByteArray &xml)
{
    DavMultistatus result;
    QXmlStreamReader r(xml);
    r.setNamespaceProcessing(true);

    struct Frame
    {
        QString ns;
        QString local;
    };
    QList<Frame> stack;

    DavResponse response;
    bool inResponse = false;
    QMap<QString, DavProp> propstatProps;
    int propstatStatus = 0;
    bool inPropstat = false;

    QString propKey;
    DavProp prop;
    int propDepth = -1; // depth of the property element
    QString hrefText;
    bool inNestedHref = false;
    QString textTarget; // which direct child of response/multistatus is collecting text

    while (!r.atEnd()) {
        const auto token = r.readNext();
        if (token == QXmlStreamReader::StartElement) {
            const Frame frame{r.namespaceUri().toString(), r.name().toString()};
            stack.append(frame);
            const int depth = stack.size();
            const bool isDav = frame.ns == Dav::nsDav();

            if (depth == 1) {
                result.valid = (frame.local == QLatin1String("multistatus") && isDav);
                if (frame.local == QLatin1String("error")) {
                    result.valid = false;
                }
                continue;
            }
            if (propDepth > 0 && depth > propDepth) {
                // Inside a property.
                prop.descendants.append(frame.local);
                if (frame.local == QLatin1String("href") && isDav) {
                    inNestedHref = true;
                    hrefText.clear();
                }
                if (frame.local == QLatin1String("comp")) {
                    prop.compNames.append(r.attributes().value(QLatin1String("name")).toString());
                }
                continue;
            }
            if (depth == 2 && isDav && frame.local == QLatin1String("response")) {
                response = DavResponse();
                inResponse = true;
            } else if (depth == 2 && frame.local == QLatin1String("sync-token") && isDav) {
                textTarget = QStringLiteral("sync-token");
                hrefText.clear();
            } else if (depth == 2 && !result.valid && result.errorCondition.isEmpty()) {
                // An <error> body: the condition element, for example <d:valid-sync-token/>.
                result.errorCondition = frame.local;
            } else if (inResponse && depth == 3 && isDav && frame.local == QLatin1String("href")) {
                textTarget = QStringLiteral("href");
                hrefText.clear();
            } else if (inResponse && depth == 3 && isDav && frame.local == QLatin1String("status")) {
                textTarget = QStringLiteral("status");
                hrefText.clear();
            } else if (inResponse && depth == 3 && isDav && frame.local == QLatin1String("propstat")) {
                inPropstat = true;
                propstatProps.clear();
                propstatStatus = 0;
            } else if (inPropstat && depth == 4 && isDav && frame.local == QLatin1String("status")) {
                textTarget = QStringLiteral("propstat-status");
                hrefText.clear();
            } else if (inPropstat && depth == 5 && stack.at(3).local == QLatin1String("prop")) {
                propKey = Dav::key(frame.ns, frame.local);
                prop = DavProp();
                propDepth = depth;
            }
        } else if (token == QXmlStreamReader::Characters) {
            if (propDepth > 0 && stack.size() == propDepth) {
                prop.text += r.text().toString();
            } else if (inNestedHref || !textTarget.isEmpty()) {
                hrefText += r.text().toString();
            }
        } else if (token == QXmlStreamReader::EndElement) {
            const int depth = stack.size();
            const Frame frame = stack.takeLast();
            if (propDepth > 0 && depth > propDepth) {
                if (frame.local == QLatin1String("href") && inNestedHref) {
                    prop.hrefs.append(hrefText.trimmed());
                    inNestedHref = false;
                }
                continue;
            }
            if (propDepth > 0 && depth == propDepth) {
                prop.text = prop.text.trimmed();
                propstatProps.insert(propKey, prop);
                propDepth = -1;
                continue;
            }
            if (!textTarget.isEmpty()) {
                const QString value = hrefText.trimmed();
                if (textTarget == QLatin1String("sync-token")) {
                    result.syncToken = value;
                } else if (textTarget == QLatin1String("href")) {
                    response.href = value;
                } else if (textTarget == QLatin1String("status")) {
                    response.status = statusCodeOf(value);
                } else if (textTarget == QLatin1String("propstat-status")) {
                    propstatStatus = statusCodeOf(value);
                }
                textTarget.clear();
                continue;
            }
            if (inPropstat && depth == 3 && frame.local == QLatin1String("propstat")) {
                for (auto it = propstatProps.constBegin(); it != propstatProps.constEnd(); ++it) {
                    if (propstatStatus == 0 || (propstatStatus >= 200 && propstatStatus < 300)) {
                        response.props.insert(it.key(), it.value());
                    } else {
                        response.missing.append(it.key());
                    }
                }
                inPropstat = false;
            } else if (inResponse && depth == 2 && frame.local == QLatin1String("response")) {
                result.responses.append(response);
                inResponse = false;
            }
        }
    }
    if (r.hasError()) {
        result.valid = false;
    }
    return result;
}

} // namespace OCC::Nourax::Agenda
