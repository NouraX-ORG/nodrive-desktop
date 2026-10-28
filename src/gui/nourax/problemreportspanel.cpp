/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "problemreportspanel.h"

#include "problemreportdialog.h"
#include "problemreportsettings.h"

#include "settingsswitch.h"

#include <QAbstractButton>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

namespace OCC::Nourax {

void ProblemReportsPanel::install(QWidget *page, QVBoxLayout *pageLayout, QGroupBox *actionsGroupBox, QHBoxLayout *actionsLayout,
    QAbstractButton *debugArchiveButton)
{
    // The switch panel, in the same style as the other rows of the page.
    auto *panel = new QGroupBox(page);
    // The Settings dialog styles its panels by object name (rounded panel colour, label margins),
    // in light and dark mode. Share the name of the Advanced panel, so this panel looks the same
    // without touching the upstream style sheet.
    panel->setObjectName(QStringLiteral("advancedGroupBox"));
    panel->setTitle(QString());
    auto *panelLayout = new QVBoxLayout(panel);
    panelLayout->setObjectName(QStringLiteral("problemReportsPanelLayout"));

    auto *row = new QHBoxLayout();
    row->setObjectName(QStringLiteral("problemReportsRow"));
    auto *textColumn = new QVBoxLayout();
    textColumn->setObjectName(QStringLiteral("problemReportsTextColumn"));

    auto *label = new QLabel(QObject::tr("Send problem reports to Nourax"), panel);
    label->setObjectName(QStringLiteral("problemReportsLabel"));
    label->setWordWrap(true);
    textColumn->addWidget(label);

    auto *summary = new QLabel(QObject::tr("If the app has a problem, send a report to Nourax Support so we can fix it. "
                                           "Reports are saved in your NoDrive and shared with Nourax Support. "
                                           "They never include passwords."),
        panel);
    summary->setObjectName(QStringLiteral("problemReportsSummary"));
    summary->setWordWrap(true);
    summary->setEnabled(true);
    auto summaryFont = summary->font();
    summaryFont.setPointSizeF(summaryFont.pointSizeF() * 0.9);
    summary->setFont(summaryFont);
    textColumn->addWidget(summary);

    row->addLayout(textColumn, 1);

    auto *toggle = new SettingsSwitch(panel);
    toggle->setObjectName(QStringLiteral("problemReportsSwitch"));
    toggle->setAccessibleName(label->text());
    toggle->setChecked(ProblemReportSettings::enabled()); // off unless the user turned it on
    QObject::connect(toggle, &QAbstractButton::toggled, toggle, [](bool checked) {
        ProblemReportSettings::setEnabled(checked);
    });
    row->addWidget(toggle, 0, Qt::AlignVCenter);
    label->setBuddy(toggle);

    panelLayout->addLayout(row);
    pageLayout->insertWidget(pageLayout->indexOf(actionsGroupBox), panel);

    // "Report a problem" directly after "Create Debug Archive".
    auto *button = new QPushButton(QObject::tr("Report a problem"), actionsGroupBox);
    button->setObjectName(QStringLiteral("problemReportButton"));
    actionsLayout->insertWidget(actionsLayout->indexOf(debugArchiveButton) + 1, button);
    QWidget::setTabOrder(debugArchiveButton, button);
    QObject::connect(button, &QAbstractButton::clicked, page, [page] {
        auto *dialog = new ProblemReportDialog(page->window());
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->open();
    });
}

} // namespace OCC::Nourax
