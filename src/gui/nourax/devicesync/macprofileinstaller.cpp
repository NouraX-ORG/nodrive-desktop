/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "macprofileinstaller.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>

namespace OCC::Nourax::DeviceSync {

Q_LOGGING_CATEGORY(lcDeviceSyncInstaller, "nextcloud.gui.nourax.devicesync.installer", QtInfoMsg)

namespace {
MacProfileInstaller::Opener &opener()
{
    static MacProfileInstaller::Opener instance;
    return instance;
}
} // namespace

void MacProfileInstaller::setOpenerForTests(Opener value)
{
    opener() = std::move(value);
}

QString MacProfileInstaller::fileName()
{
    return QStringLiteral("NoDrive-Calendar-and-Contacts.mobileconfig");
}

MacProfileInstaller::MacProfileInstaller(QObject *parent)
    : QObject(parent)
{
    _lifetime.setSingleShot(true);
    _lifetime.setInterval(lifetimeMs);
    connect(&_lifetime, &QTimer::timeout, this, [this] {
        qCInfo(lcDeviceSyncInstaller) << "the profile file was not needed any more after 10 minutes: removed";
        cleanup();
    });
    // The app quits: the file must not stay behind.
    if (QCoreApplication::instance()) {
        connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &MacProfileInstaller::cleanup);
    }
}

MacProfileInstaller::~MacProfileInstaller()
{
    cleanup();
}

bool MacProfileInstaller::installProfile(const QByteArray &profile)
{
    cleanup();
    _error.clear();
    _dir = std::make_unique<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/nodrive-dav-XXXXXX"));
    if (!_dir->isValid()) {
        _error = tr("NoDrive could not prepare the profile file.");
        _dir.reset();
        return false;
    }
    QFile::setPermissions(_dir->path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

    _filePath = _dir->filePath(fileName());
    QFile file(_filePath);
    // Created as owner-only first, then filled.
    if (!file.open(QIODevice::WriteOnly)) {
        _error = tr("NoDrive could not prepare the profile file.");
        cleanup();
        return false;
    }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const auto written = file.write(profile);
    file.close();
    if (written != profile.size()) {
        _error = tr("NoDrive could not prepare the profile file.");
        cleanup();
        return false;
    }

    _lifetime.start();
    qCInfo(lcDeviceSyncInstaller) << "the profile file is ready; handing it to the system";
    const auto url = QUrl::fromLocalFile(_filePath);
    const auto opened = opener() ? opener()(url) : QDesktopServices::openUrl(url);
    if (!opened) {
        _error = tr("The profile could not be opened.");
        cleanup();
        return false;
    }
    return true;
}

void MacProfileInstaller::cleanup()
{
    _lifetime.stop();
    if (!_filePath.isEmpty()) {
        QFile file(_filePath);
        if (file.exists() && file.open(QIODevice::ReadWrite)) {
            const auto size = file.size();
            file.seek(0);
            file.write(QByteArray(int(size), '\0'));
            file.flush();
            file.close();
        }
        QFile::remove(_filePath);
        _filePath.clear();
    }
    _dir.reset(); // removes the folder
}

} // namespace OCC::Nourax::DeviceSync
