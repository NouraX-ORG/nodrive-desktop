/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: the colours of the window, light and dark (NoDrive themes: Nourax cobalt).

#pragma once

#include <QColor>
#include <QObject>

namespace OCC::Nourax::Agenda {

class AgendaStyle : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool dark READ dark WRITE setDark NOTIFY changed)
    Q_PROPERTY(QColor bg READ bg NOTIFY changed)
    Q_PROPERTY(QColor surface READ surface NOTIFY changed)
    Q_PROPERTY(QColor surfaceAlt READ surfaceAlt NOTIFY changed)
    Q_PROPERTY(QColor border READ border NOTIFY changed)
    Q_PROPERTY(QColor text READ text NOTIFY changed)
    Q_PROPERTY(QColor textMuted READ textMuted NOTIFY changed)
    Q_PROPERTY(QColor primary READ primary NOTIFY changed)
    Q_PROPERTY(QColor onPrimary READ onPrimary NOTIFY changed)
    Q_PROPERTY(QColor primarySoft READ primarySoft NOTIFY changed)
    Q_PROPERTY(QColor warning READ warning NOTIFY changed)
    Q_PROPERTY(QColor danger READ danger NOTIFY changed)
    Q_PROPERTY(QColor selected READ selected NOTIFY changed)
    Q_PROPERTY(QColor hover READ hover NOTIFY changed)

public:
    explicit AgendaStyle(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    [[nodiscard]] bool dark() const { return _dark; }
    void setDark(bool dark)
    {
        if (_dark != dark) {
            _dark = dark;
            Q_EMIT changed();
        }
    }

    [[nodiscard]] QColor bg() const { return _dark ? QColor(0x0B, 0x12, 0x20) : QColor(0xF8, 0xFA, 0xFC); }
    [[nodiscard]] QColor surface() const { return _dark ? QColor(0x0F, 0x17, 0x2A) : QColor(0xFF, 0xFF, 0xFF); }
    [[nodiscard]] QColor surfaceAlt() const { return _dark ? QColor(0x16, 0x20, 0x33) : QColor(0xF1, 0xF5, 0xF9); }
    [[nodiscard]] QColor border() const { return _dark ? QColor(0x1E, 0x29, 0x3B) : QColor(0xE2, 0xE8, 0xF0); }
    [[nodiscard]] QColor text() const { return _dark ? QColor(0xE2, 0xE8, 0xF0) : QColor(0x0F, 0x17, 0x2A); }
    [[nodiscard]] QColor textMuted() const { return _dark ? QColor(0x94, 0xA3, 0xB8) : QColor(0x47, 0x55, 0x69); }
    [[nodiscard]] QColor primary() const { return _dark ? QColor(0x3B, 0x82, 0xF6) : QColor(0x25, 0x63, 0xEB); }
    [[nodiscard]] QColor onPrimary() const { return QColor(0xFF, 0xFF, 0xFF); }
    [[nodiscard]] QColor primarySoft() const { return _dark ? QColor(0x1E, 0x3A, 0x6B) : QColor(0xDB, 0xEA, 0xFE); }
    [[nodiscard]] QColor warning() const { return _dark ? QColor(0xF9, 0x73, 0x16) : QColor(0xEA, 0x58, 0x0C); }
    [[nodiscard]] QColor danger() const { return _dark ? QColor(0xF8, 0x71, 0x71) : QColor(0xDC, 0x26, 0x26); }
    [[nodiscard]] QColor selected() const { return _dark ? QColor(0x1E, 0x3A, 0x6B) : QColor(0xEF, 0xF6, 0xFF); }
    [[nodiscard]] QColor hover() const { return _dark ? QColor(0x16, 0x20, 0x33) : QColor(0xF1, 0xF5, 0xF9); }

Q_SIGNALS:
    void changed();

private:
    bool _dark = false;
};

} // namespace OCC::Nourax::Agenda
