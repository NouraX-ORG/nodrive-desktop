/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda Timeline: what the QML view needs (plans/noagenda-desktop-plan.md, 3.2): the rows with their geometry (from
// the pure TimelineLayout), the scale, the read-only arrows, the shading of my busy time, the zoom, and the drag of a
// bar. A drop is never written here: it goes through EditController::moveByDays, so the rules, the queue, the
// conflict handling and "Undo" are the ones of the editor.

#pragma once

#include "../accountagenda.h"
#include "../logic/timelinelayout.h"
#include "editcontroller.h"

#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

namespace OCC::Nourax::Agenda {

class TimelineController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString zoom READ zoom WRITE setZoom NOTIFY changed)
    Q_PROPERTY(double pxPerDay READ pxPerDay NOTIFY changed)
    Q_PROPERTY(double contentWidth READ contentWidth NOTIFY changed)
    Q_PROPERTY(double contentHeight READ contentHeight NOTIFY changed)
    Q_PROPERTY(double rowHeight READ rowHeight CONSTANT)
    Q_PROPERTY(double todayX READ todayX NOTIFY changed)
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)
    Q_PROPERTY(QVariantList arrows READ arrows NOTIFY changed)
    Q_PROPERTY(QVariantList shading READ shading NOTIFY changed)
    Q_PROPERTY(QVariantList scaleTop READ scaleTop NOTIFY changed)
    Q_PROPERTY(QVariantList scaleBottom READ scaleBottom NOTIFY changed)
    Q_PROPERTY(QVariantMap ghost READ ghost NOTIFY ghostChanged)
    Q_PROPERTY(QString emptyText READ emptyText NOTIFY changed)

public:
    TimelineController(AccountAgenda *agenda, EditController *edit, QObject *parent = nullptr);

    /// Lays the Timeline out again around `anchor` (from anchor - 7 days to anchor + 8 weeks).
    void rebuild(const QDate &anchor, const std::function<bool(const AgendaItem &)> &filter);

    [[nodiscard]] QString zoom() const { return _zoom == TimelineZoom::Days ? QStringLiteral("days") : QStringLiteral("weeks"); }
    void setZoom(const QString &zoom);
    [[nodiscard]] double pxPerDay() const { return _layout.scale.pxPerDay; }
    [[nodiscard]] double contentWidth() const { return _layout.scale.width(); }
    [[nodiscard]] double contentHeight() const { return _layout.height; }
    [[nodiscard]] double rowHeight() const { return TimelineLayout::kRowHeight; }
    [[nodiscard]] double todayX() const { return _layout.todayX; }
    [[nodiscard]] QVariantList entries() const { return _entries; }
    [[nodiscard]] QVariantList arrows() const { return _arrows; }
    [[nodiscard]] QVariantList shading() const { return _shading; }
    [[nodiscard]] QVariantList scaleTop() const { return _scaleTop; }
    [[nodiscard]] QVariantList scaleBottom() const { return _scaleBottom; }
    [[nodiscard]] QVariantMap ghost() const { return _ghost; }
    [[nodiscard]] QString emptyText() const { return _emptyText; }
    [[nodiscard]] const TimelineLayoutResult &layout() const { return _layout; }
    /// The keys of the items, top to bottom (for the arrow keys of the window).
    [[nodiscard]] QStringList itemKeys() const;
    /// The first and the last day that are drawn.
    [[nodiscard]] QPair<QDate, QDate> range() const { return {_layout.scale.first, _layout.scale.last()}; }

    /// Shows the ghost bar of a drag of `dx` pixels and returns its dates ("4 Nov - 8 Nov"), or the reason it can't move.
    Q_INVOKABLE QString dragPreview(const QString &key, double dx);
    /// A drop: moves the bar. Plain words when it can't be done (also shown as a message); empty when it was handed over.
    Q_INVOKABLE QString dragCommit(const QString &key, double dx);
    /// Esc: the ghost goes, nothing is saved.
    Q_INVOKABLE void dragCancel();
    /// The keyboard: the same as a drop of `days` days.
    Q_INVOKABLE QString moveByKeys(const QString &key, int days);
    /// The text of the dates of a bar moved by `days` days.
    Q_INVOKABLE QString datesText(const QString &key, int days) const;
    /// Where "Today" scrolls to (the left edge of the visible part).
    Q_INVOKABLE double scrollTargetX() const;
    /// Asks the view to scroll sideways (to the start of the range, or to today).
    void requestScroll(bool toToday) { Q_EMIT scrollRequested(toToday ? scrollTargetX() : 0.0); }

Q_SIGNALS:
    void changed();
    void ghostChanged();
    void scrollRequested(double x);

private:
    [[nodiscard]] const TimelineEntry *entryWithKey(const QString &key) const;
    [[nodiscard]] QString rangeText(const QDate &a, const QDate &b) const;
    void rebuildScale();

    QPointer<AccountAgenda> _agenda;
    QPointer<EditController> _edit;
    TimelineZoom _zoom = TimelineZoom::Weeks;
    QDate _anchor;
    std::function<bool(const AgendaItem &)> _filter;
    TimelineLayoutResult _layout;
    QVariantList _entries;
    QVariantList _arrows;
    QVariantList _shading;
    QVariantList _scaleTop;
    QVariantList _scaleBottom;
    QVariantMap _ghost;
    QString _emptyText;
};

} // namespace OCC::Nourax::Agenda
