/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "registrysource.h"

namespace OCC::Nourax::Agenda {

/// WebDAV GET of remote.php/dav/files/<user>/NoAgenda/agent-registry.json with If-None-Match.
class DriveRegistrySource : public RegistrySource
{
public:
    explicit DriveRegistrySource(HttpTransport *transport);

    void fetch(const QByteArray &etag, const std::function<void(const RegistryFetch &)> &done) override;
    [[nodiscard]] QString description() const override;

    static QString remotePath();

private:
    HttpTransport *_transport;
};

/// A source that reads an address of the Nourax agents app (same JSON). Used when
/// kAgentsAppRegistryUrl is not empty; the address must be on the account's own server.
class UrlRegistrySource : public RegistrySource
{
public:
    UrlRegistrySource(HttpTransport *transport, const QString &url);

    void fetch(const QByteArray &etag, const std::function<void(const RegistryFetch &)> &done) override;
    [[nodiscard]] QString description() const override;

private:
    HttpTransport *_transport;
    QString _url;
};

} // namespace OCC::Nourax::Agenda
