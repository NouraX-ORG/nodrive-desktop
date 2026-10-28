/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "agendacolors.h"

#include <QRegularExpression>

namespace OCC::Nourax::Agenda {

QString cobaltColor()
{
    return QStringLiteral("#2563EB");
}

QString sourceColor(const QString &raw, const QString &fallback)
{
    QString c = raw.trimmed();
    if (c.startsWith(QLatin1Char('#'))) {
        c.remove(0, 1);
    }
    static const QRegularExpression hex(QStringLiteral("^[0-9a-fA-F]{6}([0-9a-fA-F]{2})?$"));
    if (!hex.match(c).hasMatch()) {
        return fallback.isEmpty() ? cobaltColor() : fallback;
    }
    const QString rgb = QLatin1Char('#') + c.left(6).toUpper();
    // The default blues of the server (its two standard accent colours) become cobalt.
    if (rgb == QLatin1String("#0082C9") || rgb == QLatin1String("#00679E")) {
        return cobaltColor();
    }
    return rgb;
}

} // namespace OCC::Nourax::Agenda
