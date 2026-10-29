/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: the object the QML window talks to. It turns the state of one account into the rows of
// Day, Week and Month, the banners, the Sources screen and the detail panel.

#pragma once

#include "../accountagenda.h"
#include "agendastyle.h"
#include "editcontroller.h"

#include <QDate>
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>

namespace OCC::Nourax::Agenda {

class AgendaController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QObject *style READ style CONSTANT)
    Q_PROPERTY(QObject *edit READ edit CONSTANT)
    Q_PROPERTY(bool conflictsOnly READ conflictsOnly WRITE setConflictsOnly NOTIFY changed)
    Q_PROPERTY(QString selectedDateIso READ selectedDateIso NOTIFY changed)
    Q_PROPERTY(QString accountName READ accountName NOTIFY changed)
    Q_PROPERTY(QString viewMode READ viewMode WRITE setViewMode NOTIFY changed)
    Q_PROPERTY(QString category READ category WRITE setCategory NOTIFY changed)
    Q_PROPERTY(bool chipMe READ chipMe NOTIFY changed)
    Q_PROPERTY(bool chipAgents READ chipAgents NOTIFY changed)
    Q_PROPERTY(bool chipTeam READ chipTeam NOTIFY changed)
    Q_PROPERTY(bool showAllTeam READ showAllTeam WRITE setShowAllTeam NOTIFY changed)
    Q_PROPERTY(bool hasTeamSection READ hasTeamSection NOTIFY changed)
    Q_PROPERTY(QString rangeText READ rangeText NOTIFY changed)
    Q_PROPERTY(QString updatedText READ updatedText NOTIFY changed)
    Q_PROPERTY(bool refreshing READ refreshing NOTIFY changed)
    Q_PROPERTY(QString page READ page WRITE setPage NOTIFY changed)
    Q_PROPERTY(QVariantList rows READ rows NOTIFY changed)
    Q_PROPERTY(QVariantList monthGrid READ monthGrid NOTIFY changed)
    Q_PROPERTY(QVariantList banners READ banners NOTIFY changed)
    Q_PROPERTY(QString emptyText READ emptyText NOTIFY changed)
    Q_PROPERTY(bool loadingFirstTime READ loadingFirstTime NOTIFY changed)
    Q_PROPERTY(QString selectedKey READ selectedKey NOTIFY changed)
    Q_PROPERTY(QVariantMap detail READ detail NOTIFY changed)
    Q_PROPERTY(QVariantList sources READ sources NOTIFY changed)
    Q_PROPERTY(QVariantList agents READ agents NOTIFY changed)
    Q_PROPERTY(QString registryMessage READ registryMessage NOTIFY changed)
    Q_PROPERTY(QVariantMap hours READ hours NOTIFY changed)
    Q_PROPERTY(bool personalQuestionVisible READ personalQuestionVisible NOTIFY changed)
    Q_PROPERTY(QVariantList personalCalendars READ personalCalendars NOTIFY changed)

public:
    /// `agenda` must outlive the controller.
    explicit AgendaController(AccountAgenda *agenda, QObject *parent = nullptr);

    [[nodiscard]] QObject *style() { return &_style; }
    [[nodiscard]] QObject *edit() { return _edit; }
    [[nodiscard]] EditController *editController() { return _edit; }
    [[nodiscard]] bool conflictsOnly() const { return _conflictsOnly; }
    [[nodiscard]] QString selectedDateIso() const { return _date.toString(Qt::ISODate); }
    void setConflictsOnly(bool on);
    [[nodiscard]] AgendaStyle *agendaStyle() { return &_style; }
    [[nodiscard]] QString accountName() const { return _accountName; }
    void setAccountName(const QString &name);
    [[nodiscard]] QString viewMode() const { return _view; }
    void setViewMode(const QString &mode);
    [[nodiscard]] QString category() const { return _category; }
    void setCategory(const QString &category);
    [[nodiscard]] bool chipMe() const { return _chipMe; }
    [[nodiscard]] bool chipAgents() const { return _chipAgents; }
    [[nodiscard]] bool chipTeam() const { return _chipTeam; }
    [[nodiscard]] bool showAllTeam() const { return _showAllTeam; }
    void setShowAllTeam(bool show);
    [[nodiscard]] bool hasTeamSection() const;
    [[nodiscard]] QString rangeText() const;
    [[nodiscard]] QString updatedText() const;
    [[nodiscard]] bool refreshing() const;
    [[nodiscard]] QString page() const { return _page; }
    void setPage(const QString &page);
    [[nodiscard]] QVariantList rows() const { return _rows; }
    [[nodiscard]] QVariantList monthGrid() const { return _grid; }
    [[nodiscard]] QVariantList banners() const { return _banners; }
    [[nodiscard]] QString emptyText() const { return _emptyText; }
    [[nodiscard]] bool loadingFirstTime() const;
    [[nodiscard]] QString selectedKey() const { return _selected; }
    [[nodiscard]] QVariantMap detail() const { return _detail; }
    [[nodiscard]] QVariantList sources() const { return _sources; }
    [[nodiscard]] QVariantList agents() const { return _agents; }
    [[nodiscard]] QString registryMessage() const { return _registryMessage; }
    [[nodiscard]] QVariantMap hours() const { return _hours; }
    [[nodiscard]] bool personalQuestionVisible() const;
    [[nodiscard]] QVariantList personalCalendars() const { return _personalCalendars; }

    // The date shown (tests set it).
    [[nodiscard]] QDate date() const { return _date; }

    Q_INVOKABLE void chooseChip(const QString &which, bool on);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void goToday();
    Q_INVOKABLE void goPrevious();
    Q_INVOKABLE void goNext();
    Q_INVOKABLE void setDate(const QString &iso);
    Q_INVOKABLE void select(const QString &key);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void openSelected();
    Q_INVOKABLE void bannerAction(const QString &action);
    Q_INVOKABLE void toggleNoDate();
    Q_INVOKABLE void setSourceMode(const QString &type, const QString &key, const QString &mode);
    Q_INVOKABLE void answerPersonal(const QStringList &personalUris);
    Q_INVOKABLE void skipPersonal();
    Q_INVOKABLE void setWorkingHours(int daysMask, const QString &from, const QString &to);
    Q_INVOKABLE void moveSelection(int delta);

    /// Rebuilds all derived data from the state of the account (called on every change).
    void rebuild();

Q_SIGNALS:
    void changed();
    void loginRequested();
    void openUrlRequested(const QUrl &url);

private:
    [[nodiscard]] bool visible(const AgendaItem &item) const;
    void addDay(const QDate &day, bool dayView, QVariantList *out, bool header) const;
    [[nodiscard]] QVariantMap rowOf(const AgendaItem &item, bool inTeam) const;
    [[nodiscard]] QVariantMap headerRow(const QString &type, const QString &text, int count = 0) const;
    [[nodiscard]] QString timeText(const AgendaItem &item, const QDate &day) const;
    [[nodiscard]] QString dateText(const QDate &d) const;
    [[nodiscard]] QString timeOfDay(const QDateTime &utc) const;
    [[nodiscard]] QString savedAtText(const QDateTime &when) const;
    void rebuildBanners();
    void rebuildSources();
    void rebuildDetail();
    void rebuildGrid();
    [[nodiscard]] QPair<QDate, QDate> range() const;
    [[nodiscard]] const AgendaItem *itemWithKey(const QString &key) const;

    QPointer<AccountAgenda> _agenda;
    AgendaStyle _style;
    EditController *_edit = nullptr;
    bool _conflictsOnly = false;
    QString _accountName;
    QString _view = QStringLiteral("day");
    QString _category = QStringLiteral("all");
    bool _chipMe = true;
    bool _chipAgents = true;
    bool _chipTeam = true;
    bool _showAllTeam = false;
    QString _page = QStringLiteral("agenda");
    QDate _date;
    bool _noDateExpanded = false;
    QString _selected;
    QString _emptyText;
    QString _registryMessage;
    QVariantList _rows;
    QVariantList _grid;
    QVariantList _banners;
    QVariantList _sources;
    QVariantList _agents;
    QVariantList _personalCalendars;
    QVariantMap _detail;
    QVariantMap _hours;
    QStringList _selectableKeys;
    bool _personalDismissed = false;
};

} // namespace OCC::Nourax::Agenda
