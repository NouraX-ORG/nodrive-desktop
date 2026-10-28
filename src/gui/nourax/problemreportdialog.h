/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoDrive problem reports: the "Report a problem" dialog.

#pragma once

#include "problemreportmanager.h"

#include <QDialog>

class QDialogButtonBox;
class QLabel;
class QMessageBox;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;

namespace OCC::Nourax {

class ProblemReportDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ProblemReportDialog(QWidget *parent = nullptr);

    /// The plain-words list of what a report contains (shown by the "What's included" link).
    [[nodiscard]] static QString whatsIncludedText();
    /// The message box behind the "What's included" link (not shown yet).
    [[nodiscard]] static QMessageBox *createWhatsIncludedBox(QWidget *parent = nullptr);

    [[nodiscard]] QPlainTextEdit *noteEdit() const { return _note; }
    [[nodiscard]] QPushButton *sendButton() const { return _sendButton; }
    [[nodiscard]] QPushButton *cancelButton() const { return _cancelButton; }

public Q_SLOTS:
    void reject() override;

private Q_SLOTS:
    void slotSend();
    void slotShowWhatsIncluded();
    void slotSubmissionFinished(quint64 id, OCC::Nourax::ProblemReportManager::Outcome outcome);

private:
    QPlainTextEdit *_note = nullptr;
    QLabel *_includedLink = nullptr;
    QProgressBar *_busy = nullptr;
    QPushButton *_sendButton = nullptr;
    QPushButton *_cancelButton = nullptr;
    quint64 _submissionId = 0;
    bool _sending = false;
};

} // namespace OCC::Nourax
