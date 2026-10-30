/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoDrive device calendar and contacts sync (macOS): puts the configuration profile into a fresh private temporary
// folder (0700, file 0600) and hands it to the system, which opens it in System Settings. The profile holds a password,
// so the file is overwritten with zeros and removed after the answer, after 10 minutes, or when the app quits.

#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include <functional>
#include <memory>

namespace OCC::Nourax::DeviceSync {

class MacProfileInstaller : public QObject
{
    Q_OBJECT

public:
    explicit MacProfileInstaller(QObject *parent = nullptr);
    ~MacProfileInstaller() override;

    /// How the file is opened. Tests replace it; the default is QDesktopServices::openUrl.
    using Opener = std::function<bool(const QUrl &)>;
    static void setOpenerForTests(Opener opener);

    /// Writes the profile and opens it. False (and errorText()) when the file could not be written or opened.
    bool installProfile(const QByteArray &profile);

    [[nodiscard]] QString filePath() const { return _filePath; }
    [[nodiscard]] QString errorText() const { return _error; }

    /// Overwrites the file with zeros, removes it and its folder. Safe to call again.
    void cleanup();

    /// Name of the profile file (no "&" and no spaces).
    [[nodiscard]] static QString fileName();
    static constexpr int lifetimeMs = 10 * 60 * 1000;

private:
    std::unique_ptr<QTemporaryDir> _dir;
    QString _filePath;
    QString _error;
    QTimer _lifetime;
};

} // namespace OCC::Nourax::DeviceSync
