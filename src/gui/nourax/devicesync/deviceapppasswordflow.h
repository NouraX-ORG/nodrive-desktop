/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoDrive device calendar and contacts sync: a separate app password for this computer's Calendar and
// Contacts, made through Login Flow v2 (POST /index.php/login/v2, then poll). plans/noagenda-desktop-plan.md, 4.3.
//
// It runs on a plain QNetworkAccessManager and not on OCC::AccessManager, so that HttpLogger never sees a body.
// The password lives in a QByteArray in memory only and is overwritten with zeros after use. It is never logged.

#pragma once

#include <QByteArray>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QUrl>

#include <chrono>

class QNetworkAccessManager;

namespace OCC::Nourax::DeviceSync {

class DeviceAppPasswordFlow : public QObject
{
    Q_OBJECT

public:
    enum class Failure {
        /// The server could not be reached, or did not start the sign-in (any other answer than 200 to the first request).
        NotStarted,
        /// The server answered, but not in the shape of Login Flow v2, or with an address of another server.
        BadAnswer,
        /// The result names another server than the account's: dropped.
        ServerMismatch,
        /// The person did not allow access in time.
        TimedOut,
    };
    Q_ENUM(Failure)

    /// `nam` is injected by tests; when null the flow makes its own plain manager. `computerName` ends up in the token's name.
    DeviceAppPasswordFlow(const QUrl &server, const QString &computerName, QNetworkAccessManager *nam = nullptr, QObject *parent = nullptr);
    ~DeviceAppPasswordFlow() override;

    void setPollInterval(std::chrono::milliseconds interval) { _pollInterval = interval; }
    void setTimeout(std::chrono::milliseconds timeout) { _timeout = timeout; }

    /// "NoDrive Calendar & Contacts (<computer name>)": becomes the token's name in the web Security settings.
    [[nodiscard]] static QByteArray userAgent(const QString &computerName);
    [[nodiscard]] QUrl loginUrl() const { return _loginUrl; }

    /// Same scheme, host, port (default ports understood) and path (a trailing slash does not matter).
    [[nodiscard]] static bool sameServer(const QUrl &a, const QUrl &b);

    /// Overwrites the bytes with zeros (the buffer is detached first, so that no shared copy stays behind) and clears it.
    static void wipe(QByteArray &data);

    /// POST <server>/index.php/login/v2. Emits loginUrlReady(), then granted() or failed().
    void start();
    /// Stops polling. Nothing was made yet, so there is nothing to revoke.
    void cancel();

    /// "It didn't work": DELETE <server>/ocs/v2.php/core/apppassword, signed in with that password (it revokes itself).
    /// The caller wipes its own copy (DeviceAppPasswordFlow::wipe). Emits revoked().
    void revoke(const QString &loginName, QByteArray appPassword);

Q_SIGNALS:
    void loginUrlReady(const QUrl &loginUrl);
    /// The new password. It is valid only while the slot runs (direct connection): the flow zeroes the buffer right after.
    void granted(const QString &loginName, const QByteArray &appPassword);
    void failed(OCC::Nourax::DeviceSync::DeviceAppPasswordFlow::Failure failure, const QString &text);
    void revoked(bool ok);

private:
    [[nodiscard]] QUrl serverPath(const QString &path) const;
    void fail(Failure failure);
    void poll();
    void handleInit(QNetworkReply *reply);
    void handlePoll(QNetworkReply *reply);

    QUrl _server;
    QString _computerName;
    QNetworkAccessManager *_nam = nullptr;
    bool _ownsNam = false;
    std::chrono::milliseconds _pollInterval{5000};
    std::chrono::milliseconds _timeout{20 * 60 * 1000};
    QTimer _pollTimer;
    QTimer _timeoutTimer;
    QUrl _loginUrl;
    QUrl _pollEndpoint;
    QByteArray _pollToken;
    bool _running = false;
    QPointer<QNetworkReply> _pending;
};

} // namespace OCC::Nourax::DeviceSync
