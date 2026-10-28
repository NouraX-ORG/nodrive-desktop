/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "problemreportdialog.h"

#include "problemreportsettings.h"

#include "theme.h"

#include <QColor>
#include <QDialogButtonBox>
#include <QEvent>
#include <QLabel>
#include <QAbstractButton>
#include <QMessageBox>
#include <QPalette>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

#include <memory>

namespace OCC::Nourax {

ProblemReportDialog::ProblemReportDialog(QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("problemReportDialog"));
    setWindowTitle(tr("Report a problem"));
    setMinimumWidth(420);

    auto *layout = new QVBoxLayout(this);

    auto *question = new QLabel(tr("What went wrong?"), this);
    question->setObjectName(QStringLiteral("problemReportQuestion"));
    layout->addWidget(question);

    _note = new QPlainTextEdit(this);
    _note->setObjectName(QStringLiteral("problemReportNote"));
    _note->setTabChangesFocus(true);
    _note->setMinimumHeight(110);
    question->setBuddy(_note);
    layout->addWidget(_note);

    _includedLink = new QLabel(this);
    _includedLink->setObjectName(QStringLiteral("problemReportIncludedLink"));
    _includedLink->setTextFormat(Qt::RichText);
    _includedLink->setTextInteractionFlags(Qt::TextBrowserInteraction);
    connect(_includedLink, &QLabel::linkActivated, this, &ProblemReportDialog::slotShowWhatsIncluded);
    layout->addWidget(_includedLink);

    _busy = new QProgressBar(this);
    _busy->setObjectName(QStringLiteral("problemReportBusy"));
    _busy->setRange(0, 0);
    _busy->setTextVisible(false);
    _busy->setMaximumHeight(8);
    _busy->hide();
    layout->addWidget(_busy);

    auto *buttons = new QDialogButtonBox(this);
    _sendButton = buttons->addButton(tr("Send"), QDialogButtonBox::AcceptRole);
    _sendButton->setObjectName(QStringLiteral("problemReportSendButton"));
    _sendButton->setDefault(true);
    _cancelButton = buttons->addButton(tr("Cancel"), QDialogButtonBox::RejectRole);
    _cancelButton->setObjectName(QStringLiteral("problemReportCancelButton"));
    connect(_sendButton, &QPushButton::clicked, this, &ProblemReportDialog::slotSend);
    connect(_cancelButton, &QPushButton::clicked, this, &ProblemReportDialog::reject);
    layout->addWidget(buttons);

    setTabOrder(_note, _sendButton);
    _note->setFocus();
    updateLinkColor();
}

// The default link colour is dark blue, unreadable on a dark window: use the Nourax blue,
// and a lighter one in dark mode.
void ProblemReportDialog::updateLinkColor()
{
    const auto dark = palette().color(QPalette::Window).lightness() < 128;
    const auto link = dark ? QColor(0x60, 0xA5, 0xFA) : QColor(0x25, 0x63, 0xEB);
    // Rich text takes the link colour from the application palette, so the colour is set in the text.
    const auto html = QStringLiteral("<a href=\"#included\" style=\"color:%1;\">%2</a>").arg(link.name(), tr("What's included"));
    if (_includedLink->text() != html) {
        _includedLink->setText(html);
    }
}

void ProblemReportDialog::changeEvent(QEvent *event)
{
    QDialog::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ThemeChange) {
        updateLinkColor();
    }
}

QString ProblemReportDialog::whatsIncludedText()
{
    const auto app = Theme::instance()->appNameGUI();
    return tr("A problem report contains:\n"
              "• your note, if you write one;\n"
              "• the app version, your computer's system and version, the computer model, your language setting, "
              "the time and the name of your %1 server;\n"
              "• what happened when the app stopped, if it crashed;\n"
              "• the app's recent logs and its settings file.\n"
              "\n"
              "Before the report is made, %1 removes passwords, app passwords, tokens, cookies, private keys and "
              "encryption keys, and replaces email addresses with <email>. File and folder names stay in the report, "
              "because we need them to find the problem.\n"
              "\n"
              "The report is saved in your %1 in the folder \"%2\" and shared, read-only, with Nourax Support.")
        .arg(app, ProblemReportSettings::remoteFolder());
}

QMessageBox *ProblemReportDialog::createWhatsIncludedBox(QWidget *parent)
{
    auto *box = new QMessageBox(QMessageBox::Information, tr("What's included"), whatsIncludedText(), QMessageBox::Ok, parent);
    box->setObjectName(QStringLiteral("problemReportIncludedBox"));
    box->setTextFormat(Qt::PlainText);
    return box;
}

void ProblemReportDialog::slotShowWhatsIncluded()
{
    const std::unique_ptr<QMessageBox> box(createWhatsIncludedBox(this));
    box->exec();
}

void ProblemReportDialog::slotSend()
{
    if (_sending) {
        return;
    }
    _sending = true;
    _sendButton->setEnabled(false);
    _cancelButton->setEnabled(false);
    _note->setEnabled(false);
    _busy->show();

    ProblemReportInfo info;
    info.kind = ReportKind::Manual;
    info.userNote = _note->toPlainText();

    auto *manager = ProblemReportManager::instance();
    connect(manager, &ProblemReportManager::submissionFinished, this, &ProblemReportDialog::slotSubmissionFinished);
    _submissionId = manager->submit(info, false);
}

void ProblemReportDialog::slotSubmissionFinished(quint64 id, ProblemReportManager::Outcome outcome)
{
    if (id != _submissionId) {
        return;
    }
    _sending = false;
    _busy->hide();
    QMessageBox::information(this, tr("Report a problem"), ProblemReportManager::outcomeText(outcome));
    accept();
}

void ProblemReportDialog::reject()
{
    if (_sending) {
        return; // the report is being made; the result message closes the dialog
    }
    QDialog::reject();
}

} // namespace OCC::Nourax
