/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "icaltext.h"

namespace OCC::Nourax::Agenda {

QString ICalText::unescape(const QString &value)
{
    QString out;
    out.reserve(value.size());
    for (int i = 0; i < value.size(); ++i) {
        const QChar c = value.at(i);
        if (c == QLatin1Char('\\') && i + 1 < value.size()) {
            const QChar n = value.at(i + 1);
            if (n == QLatin1Char('n') || n == QLatin1Char('N')) {
                out.append(QLatin1Char('\n'));
            } else {
                out.append(n); // \, \; \\ and anything else
            }
            ++i;
        } else {
            out.append(c);
        }
    }
    return out;
}

QString ICalProperty::text() const
{
    return ICalText::unescape(raw);
}

QStringList ICalProperty::textList() const
{
    QStringList out;
    QString current;
    for (int i = 0; i < raw.size(); ++i) {
        const QChar c = raw.at(i);
        if (c == QLatin1Char('\\') && i + 1 < raw.size()) {
            current.append(c);
            current.append(raw.at(i + 1));
            ++i;
        } else if (c == QLatin1Char(',')) {
            out.append(ICalText::unescape(current).trimmed());
            current.clear();
        } else {
            current.append(c);
        }
    }
    out.append(ICalText::unescape(current).trimmed());
    out.removeAll(QString());
    return out;
}

const ICalProperty *ICalComponent::first(const QString &name) const
{
    const auto wanted = name.toUpper();
    for (const auto &p : props) {
        if (p.name == wanted) {
            return &p;
        }
    }
    return nullptr;
}

QList<const ICalProperty *> ICalComponent::all(const QString &name) const
{
    QList<const ICalProperty *> out;
    const auto wanted = name.toUpper();
    for (const auto &p : props) {
        if (p.name == wanted) {
            out.append(&p);
        }
    }
    return out;
}

QString ICalComponent::text(const QString &name) const
{
    const auto *p = first(name);
    return p ? p->text() : QString();
}

QStringList ICalText::unfold(const QString &text)
{
    QStringList lines;
    const auto raw = text.split(QLatin1Char('\n'));
    for (auto line : raw) {
        if (line.endsWith(QLatin1Char('\r'))) {
            line.chop(1);
        }
        if (!lines.isEmpty() && !line.isEmpty() && (line.at(0) == QLatin1Char(' ') || line.at(0) == QLatin1Char('\t'))) {
            lines.last().append(line.mid(1));
        } else {
            lines.append(line);
        }
    }
    return lines;
}

namespace {

bool parseProperty(const QString &line, ICalProperty *out)
{
    // name [;param=value[,value]]... : value
    int i = 0;
    const int n = line.size();
    while (i < n && line.at(i) != QLatin1Char(':') && line.at(i) != QLatin1Char(';')) {
        ++i;
    }
    if (i == 0 || i >= n) {
        return false;
    }
    out->name = line.left(i).trimmed().toUpper();
    while (i < n && line.at(i) == QLatin1Char(';')) {
        ++i;
        const int keyStart = i;
        while (i < n && line.at(i) != QLatin1Char('=') && line.at(i) != QLatin1Char(':') && line.at(i) != QLatin1Char(';')) {
            ++i;
        }
        const QString key = line.mid(keyStart, i - keyStart).trimmed().toUpper();
        QString value;
        if (i < n && line.at(i) == QLatin1Char('=')) {
            ++i;
            bool quoted = false;
            while (i < n && (quoted || (line.at(i) != QLatin1Char(':') && line.at(i) != QLatin1Char(';')))) {
                if (line.at(i) == QLatin1Char('"')) {
                    quoted = !quoted;
                } else {
                    value.append(line.at(i));
                }
                ++i;
            }
        }
        if (!key.isEmpty()) {
            out->params.insert(key, value);
        }
    }
    if (i >= n || line.at(i) != QLatin1Char(':')) {
        return false;
    }
    out->raw = line.mid(i + 1);
    return true;
}

} // namespace

QList<ICalComponent> ICalText::parse(const QString &text)
{
    QList<ICalComponent> result;
    QList<ICalComponent> stack;
    const auto lines = unfold(text);
    for (const auto &line : lines) {
        if (line.isEmpty()) {
            continue;
        }
        if (line.startsWith(QLatin1String("BEGIN:"), Qt::CaseInsensitive)) {
            ICalComponent c;
            c.type = line.mid(6).trimmed().toUpper();
            stack.append(c);
            continue;
        }
        if (line.startsWith(QLatin1String("END:"), Qt::CaseInsensitive)) {
            if (stack.isEmpty()) {
                continue;
            }
            auto done = stack.takeLast();
            if (done.type == QLatin1String("VCALENDAR")) {
                continue;
            }
            if (stack.isEmpty()) {
                continue; // outside VCALENDAR: ignore
            }
            if (stack.size() == 1 && stack.first().type == QLatin1String("VCALENDAR")) {
                result.append(done);
            } else {
                stack.last().children.append(done);
            }
            continue;
        }
        if (stack.isEmpty()) {
            continue;
        }
        ICalProperty p;
        if (parseProperty(line, &p)) {
            stack.last().props.append(p);
        }
    }
    return result;
}

} // namespace OCC::Nourax::Agenda
