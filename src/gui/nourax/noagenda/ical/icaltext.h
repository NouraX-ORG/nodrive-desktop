/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: a small iCalendar (RFC 5545) text reader. It only reads what NoAgenda needs.

#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

namespace OCC::Nourax::Agenda {

struct ICalProperty
{
    QString name; ///< upper case
    QMap<QString, QString> params; ///< keys upper case, values without quotes
    QString raw; ///< value as written (still escaped)

    /// The value as text: \n \, \; \\ unescaped.
    [[nodiscard]] QString text() const;
    /// The value split at unescaped commas, each part unescaped (CATEGORIES).
    [[nodiscard]] QStringList textList() const;
    [[nodiscard]] QString param(const QString &key) const { return params.value(key.toUpper()); }
};

struct ICalComponent
{
    QString type; ///< VEVENT, VTODO, VTIMEZONE, VALARM, ...
    QList<ICalProperty> props;
    QList<ICalComponent> children;

    [[nodiscard]] const ICalProperty *first(const QString &name) const;
    [[nodiscard]] QList<const ICalProperty *> all(const QString &name) const;
    [[nodiscard]] bool has(const QString &name) const { return first(name) != nullptr; }
    [[nodiscard]] QString text(const QString &name) const;
};

class ICalText
{
public:
    /// Components of the first level inside VCALENDAR (VEVENT, VTODO, VTIMEZONE), in order.
    static QList<ICalComponent> parse(const QString &text);
    /// Unfolds lines (a line that starts with a space or tab continues the one before).
    static QStringList unfold(const QString &text);
    static QString unescape(const QString &value);
};

} // namespace OCC::Nourax::Agenda
