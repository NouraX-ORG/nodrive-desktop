/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "problemreportuploader.h"

#include "problemreportsettings.h"
#include "problemreportsharejob.h"

#include "account.h"
#include "common/utility.h"
#include "networkjobs.h"

#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <utility>

namespace OCC::Nourax {

Q_LOGGING_CATEGORY(lcProblemReportUpload, "nodrive.problemreports.upload", QtInfoMsg)

namespace {
constexpr int maxNameAttempts = 9; // name, name_2 ... name_9
}

ProblemReportUploader::ProblemReportUploader(AccountPtr account, const QString &zipPath, QObject *parent)
    : QObject(parent)
    , _account(std::move(account))
    , _zipPath(zipPath)
    , _body(this)
{
}

QString ProblemReportUploader::remoteFileName(int attempt) const
{
    const auto name = QFileInfo(_zipPath).fileName();
    if (attempt == 0) {
        return name;
    }
    return QStringLiteral("%1_%2.zip").arg(name.left(name.size() - 4), QString::number(attempt + 1));
}

void ProblemReportUploader::start()
{
    QFile file(_zipPath);
    if (!_account || !file.open(QIODevice::ReadOnly)) {
        qCWarning(lcProblemReportUpload) << "cannot read the report" << _zipPath;
        done(Result::NetworkError);
        return;
    }
    _body.setData(file.readAll());
    createFolder();
}

void ProblemReportUploader::createFolder()
{
    const auto url = Utility::concatUrlPath(_account->davUrl(), ProblemReportSettings::remoteFolder());
    const auto job = _account->sendRequest("MKCOL", url);
    connect(job, &SimpleNetworkJob::finishedSignal, this, [this](QNetworkReply *reply) {
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        // 201 = created, 405 = the folder already exists.
        if (status == 201 || status == 405) {
            uploadFile(0);
        } else {
            qCInfo(lcProblemReportUpload) << "MKCOL of the report folder answered" << status;
            done(Result::NetworkError);
        }
    });
}

void ProblemReportUploader::uploadFile(int attempt)
{
    const auto name = remoteFileName(attempt);
    const auto url = Utility::concatUrlPath(_account->davUrl(), ProblemReportSettings::remoteFolder() + QLatin1Char('/') + name);

    QNetworkRequest request;
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/zip"));
    // An existing file is never replaced: the server answers 412 instead.
    request.setRawHeader("If-None-Match", "*");

    _body.close();
    _body.open(QIODevice::ReadOnly);
    const auto job = _account->sendRequest("PUT", url, request, &_body);
    connect(job, &SimpleNetworkJob::finishedSignal, this, [this, attempt, name](QNetworkReply *reply) {
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 201 || status == 204) {
            _uploadedName = name;
            findShare();
        } else if (status == 412 && attempt + 1 < maxNameAttempts) {
            uploadFile(attempt + 1);
        } else {
            qCInfo(lcProblemReportUpload) << "PUT of the report answered" << status;
            done(Result::NetworkError);
        }
    });
}

void ProblemReportUploader::findShare()
{
    const QString folder = QLatin1Char('/') + ProblemReportSettings::remoteFolder();
    const auto job = new ProblemReportShareJob(_account);
    connect(job, &OcsJob::jobFinished, this, [this](const QJsonDocument &reply, int) {
        if (ProblemReportShareJob::hasUserShare(reply, ProblemReportSettings::supportUserId())) {
            done(Result::Sent);
        } else {
            createShare();
        }
    });
    connect(job, &OcsJob::ocsError, this, [this](int statusCode, const QString &) {
        // Quiet: the report is saved. The next report tries again; there is no retry loop.
        qCInfo(lcProblemReportUpload) << "could not read the shares of the report folder:" << statusCode;
        done(Result::SavedNotShared);
    });
    job->findShares(folder);
}

void ProblemReportUploader::createShare()
{
    const QString folder = QLatin1Char('/') + ProblemReportSettings::remoteFolder();
    const auto job = new ProblemReportShareJob(_account);
    connect(job, &OcsJob::jobFinished, this, [this](const QJsonDocument &, int) {
        done(Result::Sent);
    });
    connect(job, &OcsJob::ocsError, this, [this](int statusCode, const QString &) {
        qCInfo(lcProblemReportUpload) << "the report folder was not shared with Support:" << statusCode;
        done(Result::SavedNotShared);
    });
    job->createReadOnlyUserShare(folder, ProblemReportSettings::supportUserId());
}

void ProblemReportUploader::done(Result result)
{
    if (_finished) {
        return;
    }
    _finished = true;
    Q_EMIT finished(result, _uploadedName);
}

} // namespace OCC::Nourax
