/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: what the QML window needs to edit (plans/noagenda-desktop-plan.md, 2.2): the editor of the selected
// row, "+" (a new event or to-do), the agent choices (person first, then Self or one of my agents), the "Not sent"
// list of edit conflicts, the "waiting to send" count and the toast with "Undo".

#pragma once

#include "../accountagenda.h"
#include "../edit/editability.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

namespace OCC::Nourax::Agenda {

class EditController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariantMap form READ form NOTIFY changed)
    Q_PROPERTY(QVariantList notSent READ notSent NOTIFY changed)
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY changed)
    Q_PROPERTY(QString pendingText READ pendingText NOTIFY changed)
    Q_PROPERTY(QVariantMap newChoices READ newChoices NOTIFY changed)
    Q_PROPERTY(QString toastText READ toastText NOTIFY changed)
    Q_PROPERTY(QString toastTone READ toastTone NOTIFY changed)
    Q_PROPERTY(bool toastCanUndo READ toastCanUndo NOTIFY changed)

public:
    explicit EditController(AccountAgenda *agenda, QObject *parent = nullptr);

    /// The row that is selected in the window (null = none). The editor follows it.
    void setSelected(const AgendaItem *item);

    [[nodiscard]] QVariantMap form() const { return _form; }
    [[nodiscard]] QVariantList notSent() const { return _notSent; }
    [[nodiscard]] int pendingCount() const;
    [[nodiscard]] QString pendingText() const;
    [[nodiscard]] QVariantMap newChoices() const { return _newChoices; }
    [[nodiscard]] QString toastText() const { return _toast; }
    [[nodiscard]] QString toastTone() const { return _toastTone; }
    [[nodiscard]] bool toastCanUndo() const { return !_undoOp.isEmpty(); }

    /// Each returns plain words when it can't be done (nothing is sent), and an empty text when the change was
    /// handed to the queue.
    Q_INVOKABLE QString save(const QVariantMap &values);
    Q_INVOKABLE QString changeAgent(const QString &agentId);
    Q_INVOKABLE QString markDone();
    Q_INVOKABLE QString deleteSelected();
    Q_INVOKABLE QString create(const QVariantMap &values);
    /// The Timeline: moves a NoGantt task (start and end) or a to-do (start and due date) by whole days, through the same
    /// rules and the same queue as the editor. Plain words when it can't be done (nothing is sent), empty when it was handed over.
    Q_INVOKABLE QString moveByDays(const QString &itemKey, int days);

    Q_INVOKABLE void applyAgain(const QString &commandId);
    Q_INVOKABLE void keepOther(const QString &commandId);
    Q_INVOKABLE void openNotSent(const QString &commandId);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void dismissToast();
    /// For the tests: shows a text in the toast.
    Q_INVOKABLE void showToast(const QString &text, const QString &tone, const QString &undoOp = QString());

    /// Used by the window to build the form of one row (public for the tests).
    [[nodiscard]] QVariantMap formFor(const AgendaItem &item) const;

Q_SIGNALS:
    void changed();
    void openUrlRequested(const QUrl &url);

private:
    void rebuild();
    void onFinished(const EditCommand &command, const EditResult &result);
    void onUndoFinished(const EditResult &result);
    [[nodiscard]] QVariantList agentChoicesFor(const AgendaItem &item, const Editability &e) const;
    [[nodiscard]] QVariantMap fieldsOf(const AgendaItem &item) const;
    [[nodiscard]] QJsonObject agentField(const QString &agentId) const;
    QString submit(const EditCommand &command);

    QPointer<AccountAgenda> _agenda;
    QString _lastEventCalendar;
    QString _lastTodoCalendar;
    bool _hasItem = false;
    AgendaItem _item;
    QVariantMap _form;
    QVariantList _notSent;
    QVariantMap _newChoices;
    QString _toast;
    QString _toastTone;
    QString _undoOp;
    QHash<QString, QString> _moveTexts; ///< item key -> "Moved to 4 Nov - 8 Nov" (the Timeline)
    QTimer _toastTimer;
};

} // namespace OCC::Nourax::Agenda
