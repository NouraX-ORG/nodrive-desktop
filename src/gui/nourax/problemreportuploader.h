/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoDrive problem reports: uploads one report zip through the signed-in account's own
// connection, and shares the "Problem reports" folder read-only with Nourax Support.
//
// Data safety: nothing is ever deleted, moved or overwritten on the Drive. The upload
// carries "If-None-Match: *", so an existing file makes the server answer 412 and the
// next free numbered name is used instead.

#pragma once

#include "accountfwd.h"

#include <QByteArray>
#include <QObject>
#include <QString>

namespace OCC::Nourax {

class ProblemReportUploader : public QObject
{
    Q_OBJECT

public:
    enum class Result {
        Sent, ///< uploaded, and the folder is shared with Support (now or before)
        SavedNotShared, ///< uploaded, but the share to Support failed (quietly)
        NetworkError, ///< not uploaded; the report stays pending
    };
    Q_ENUM(Result)

    ProblemReportUploader(AccountPtr account, const QString &zipPath, QObject *parent = nullptr);

    void start();

Q_SIGNALS:
    void finished(OCC::Nourax::ProblemReportUploader::Result result, const QString &remoteFileName);

private:
    void createFolder();
    void uploadFile(int attempt);
    void findShare();
    void createShare();
    void done(Result result);
    [[nodiscard]] QString remoteFileName(int attempt) const;

    AccountPtr _account;
    QString _zipPath;
    QByteArray _data;
    QString _uploadedName;
    bool _finished = false;
};

} // namespace OCC::Nourax
