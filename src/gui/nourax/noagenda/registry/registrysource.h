/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: the one swappable source of the agent registry (design 4.1).
// Now: the signed-in user's own NoDrive file /NoAgenda/agent-registry.json.
// Later: a URL of the Nourax agents app that returns the same JSON. That switch is one change in
// registrysourcefactory.cpp.

#pragma once

#include "../net/httptransport.h"

#include <QByteArray>
#include <QString>

#include <functional>
#include <memory>

namespace OCC::Nourax::Agenda {

struct RegistryFetch
{
    enum class Outcome { Fetched, NotModified, Missing, Failed };
    Outcome outcome = Outcome::Failed;
    QByteArray bytes;
    QByteArray etag;
};

class RegistrySource
{
public:
    virtual ~RegistrySource() = default;
    /// Reads the registry. `etag` is the ETag of the cached copy (empty if none).
    virtual void fetch(const QByteArray &etag, const std::function<void(const RegistryFetch &)> &done) = 0;
    /// A short description for the logs (never a secret).
    [[nodiscard]] virtual QString description() const = 0;
};

/// The registry source of this build (see registrysourcefactory.cpp).
std::unique_ptr<RegistrySource> makeRegistrySource(HttpTransport *transport);

} // namespace OCC::Nourax::Agenda
