/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "thunderbirdguidedialog.h"

#include "appleprofilebuilder.h"
#include "urlopener.h"

#include <QCoreApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

namespace OCC::Nourax::DeviceSync {

namespace {

QString tr2(const char *text)
{
    return QCoreApplication::translate("OCC::Nourax::DeviceSync::ThunderbirdGuideDialog", text);
}

} // namespace

QString ThunderbirdGuideDialog::titleText()
{
    return tr2("Use NoDrive calendars and contacts in Thunderbird");
}

QString ThunderbirdGuideDialog::stepOneText()
{
    return tr2("Create an app password for Thunderbird: click Open Security settings, then Create new app password, and name it Thunderbird.");
}

QString ThunderbirdGuideDialog::stepTwoText(const QString &loginName)
{
    return tr2("In Thunderbird's Calendar, click New Calendar… › On the Network. Enter your user name (%1) and paste the calendar address as the location. "
               "Click Find Calendars, sign in with the app password and choose your calendars.")
        .arg(loginName);
}

QString ThunderbirdGuideDialog::stepThreeText()
{
    return tr2("In the Address Book, open the menu next to New Address Book › Add CardDAV Address Book, paste the contacts address and sign in with the app password.");
}

QString ThunderbirdGuideDialog::footerText()
{
    return tr2("NoDrive never copies passwords.");
}

ThunderbirdGuideDialog::ThunderbirdGuideDialog(const QUrl &server, const QString &davUser, const QString &loginName, QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("thunderbirdGuideDialog"));
    setWindowTitle(titleText());
    setMinimumWidth(520);

    _calendarAddress = AppleProfileBuilder::calendarHomeUrl(server, davUser);
    _contactsAddress = AppleProfileBuilder::addressBookHomeUrl(server, davUser);
    _securityUrl = server;
    {
        auto path = server.path();
        while (path.endsWith(QLatin1Char('/'))) {
            path.chop(1);
        }
        _securityUrl.setPath(path + QStringLiteral("/index.php/settings/user/security"));
    }

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(12);

    auto *title = new QLabel(titleText(), this);
    title->setObjectName(QStringLiteral("thunderbirdTitle"));
    auto titleFont = title->font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.2);
    title->setFont(titleFont);
    title->setWordWrap(true);
    layout->addWidget(title);

    auto *intro = new QLabel(tr2("Thunderbird 102 or newer has calendars and contacts built in. Follow these three steps."), this);
    intro->setObjectName(QStringLiteral("thunderbirdIntro"));
    intro->setWordWrap(true);
    layout->addWidget(intro);

    const auto addStep = [this, layout](int number, const QString &text, const QString &buttonText, const QString &buttonName, auto onClick) {
        auto *row = new QHBoxLayout();
        auto *label = new QLabel(QStringLiteral("%1. %2").arg(number).arg(text), this);
        label->setObjectName(QStringLiteral("thunderbirdStep%1").arg(number));
        label->setWordWrap(true);
        label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        row->addWidget(label, 1);
        auto *column = new QVBoxLayout();
        auto *button = new QPushButton(buttonText, this);
        button->setObjectName(buttonName);
        column->addWidget(button);
        auto *confirmation = new QLabel(QString(), this);
        confirmation->setObjectName(QStringLiteral("thunderbirdCopied%1").arg(number));
        confirmation->setAlignment(Qt::AlignHCenter);
        column->addWidget(confirmation);
        column->addStretch();
        row->addLayout(column, 0);
        layout->addLayout(row);
        connect(button, &QPushButton::clicked, this, [onClick, confirmation] { onClick(confirmation); });
    };

    addStep(1, stepOneText(), tr2("Open Security settings"), QStringLiteral("openSecuritySettingsButton"), [this](QLabel *) {
        openInBrowser(_securityUrl);
    });
    addStep(2, stepTwoText(loginName), tr2("Copy calendar address"), QStringLiteral("copyCalendarAddressButton"), [this](QLabel *confirmation) {
        copyAddress(_calendarAddress, confirmation);
    });
    addStep(3, stepThreeText(), tr2("Copy contacts address"), QStringLiteral("copyContactsAddressButton"), [this](QLabel *confirmation) {
        copyAddress(_contactsAddress, confirmation);
    });

    auto *footer = new QLabel(footerText(), this);
    footer->setObjectName(QStringLiteral("thunderbirdFooter"));
    footer->setWordWrap(true);
    layout->addWidget(footer);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void ThunderbirdGuideDialog::copyAddress(const QString &address, QLabel *confirmation)
{
    // Only the address. Never a password, a user secret or anything else.
    QGuiApplication::clipboard()->setText(address);
    // One "Copied" at a time.
    for (auto *label : findChildren<QLabel *>(QRegularExpression(QStringLiteral("^thunderbirdCopied")))) {
        label->clear();
    }
    confirmation->setText(tr2("Copied"));
}

} // namespace OCC::Nourax::DeviceSync
