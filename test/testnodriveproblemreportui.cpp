/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * NoDrive problem reports: the Settings > Advanced switch and button, the "Report a problem"
 * dialog, the "What's included" box and the "closed unexpectedly" question, with screenshots in
 * light and dark mode (NODRIVE_SCREENSHOT_DIR, default <bin>/nodrive-screenshots).
 */

#include "advancedsettings.h"
#include "configfile.h"
#include "gui/nourax/problemreportdialog.h"
#include "gui/nourax/problemreportmanager.h"
#include "gui/nourax/problemreportsettings.h"
#include "settingsdialog.h"
#include "settingsswitch.h"

#include <QAbstractButton>
#include <QApplication>
#include <QGuiApplication>
#include <QDir>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QStyleHints>
#include <QTemporaryDir>
#include <QTest>
#include <QToolBar>

using namespace OCC;
using namespace OCC::Nourax;

namespace {

QString screenshotDir()
{
    const auto dir = qEnvironmentVariable("NODRIVE_SCREENSHOT_DIR", QCoreApplication::applicationDirPath() + QStringLiteral("/nodrive-screenshots"));
    QDir().mkpath(dir);
    return dir;
}

void saveShot(QWidget *widget, const QString &name)
{
    QPixmap shot = widget->grab();
    QVERIFY2(!shot.isNull(), qPrintable(name));
    QVERIFY2(shot.save(screenshotDir() + QLatin1Char('/') + name + QStringLiteral(".png")), qPrintable(name));
}

double luminance(const QColor &c)
{
    return 0.2126 * c.red() + 0.7152 * c.green() + 0.0722 * c.blue();
}

QColor pixelAt(QWidget *widget, const QPoint &point)
{
    const auto image = widget->grab().toImage();
    const auto ratio = image.devicePixelRatio();
    return image.pixelColor(QPoint(int(point.x() * ratio), int(point.y() * ratio)));
}

void collectTexts(QWidget *root, QStringList &out)
{
    for (auto *label : root->findChildren<QLabel *>()) {
        out << label->text();
    }
    for (auto *button : root->findChildren<QAbstractButton *>()) {
        out << button->text();
    }
    out << root->windowTitle();
}

} // namespace

class TestNoDriveProblemReportUi : public QObject
{
    Q_OBJECT

    QTemporaryDir _configDir;
    QPalette _originalPalette;

    void applyColorScheme(bool dark)
    {
        // The style follows the colour scheme where the platform supports it (Windows, macOS);
        // the palette below makes the same true everywhere (Linux, offscreen).
        QApplication::setPalette(_originalPalette);
        QGuiApplication::styleHints()->setColorScheme(dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
        QTest::qWait(100);
        auto palette = QApplication::palette();
        const QColor background(dark ? "#202020" : "#ffffff");
        const QColor foreground(dark ? Qt::white : Qt::black);
        palette.setColor(QPalette::Window, background);
        palette.setColor(QPalette::WindowText, foreground);
        palette.setColor(QPalette::Base, dark ? QColor("#2b2b2b") : QColor(Qt::white));
        palette.setColor(QPalette::Text, foreground);
        palette.setColor(QPalette::Button, dark ? QColor("#3a3a3a") : QColor("#efefef"));
        palette.setColor(QPalette::ButtonText, foreground);
        palette.setColor(QPalette::Light, background);
        palette.setColor(QPalette::AlternateBase, background);
        QApplication::setPalette(palette);
    }

    static AdvancedSettings *advancedPage(SettingsDialog &dialog)
    {
        auto *page = dialog.findChild<AdvancedSettings *>();
        return page;
    }

    static void showAdvancedPage(SettingsDialog &dialog)
    {
        const auto toolbar = dialog.findChild<QToolBar *>();
        QVERIFY(toolbar);
        for (auto *action : toolbar->actions()) {
            if (action->text() == AdvancedSettings::tr("Advanced")) {
                action->trigger();
                return;
            }
        }
        QFAIL("no Advanced page in the Settings toolbar");
    }

private Q_SLOTS:
    void initTestCase()
    {
        Q_INIT_RESOURCE(resources);
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY(_configDir.isValid());
        ConfigFile::setConfDir(_configDir.path());
        _originalPalette = QApplication::palette();
    }

    void cleanup()
    {
        ProblemReportSettings::setEnabled(false);
        QGuiApplication::styleHints()->unsetColorScheme();
        QApplication::setPalette(_originalPalette);
    }

    void theSwitchIsOffByDefaultAndKeepsItsState()
    {
        QVERIFY(!ProblemReportSettings::enabled());
        SettingsDialog dialog(nullptr);
        auto *page = advancedPage(dialog);
        QVERIFY(page);

        auto *toggle = page->findChild<SettingsSwitch *>(QStringLiteral("problemReportsSwitch"));
        QVERIFY(toggle);
        QVERIFY(!toggle->isChecked());

        auto *label = page->findChild<QLabel *>(QStringLiteral("problemReportsLabel"));
        auto *summary = page->findChild<QLabel *>(QStringLiteral("problemReportsSummary"));
        QVERIFY(label);
        QVERIFY(summary);
        QCOMPARE(label->text(), QStringLiteral("Send problem reports to Nourax"));
        QCOMPARE(summary->text(),
            QStringLiteral("If the app has a problem, send a report to Nourax Support so we can fix it. "
                           "Reports are saved in your NoDrive and shared with Nourax Support. They never include passwords."));

        toggle->click();
        QVERIFY(toggle->isChecked());
        QVERIFY(ProblemReportSettings::enabled());
        QSettings ini(ConfigFile().configFile(), QSettings::IniFormat);
        QCOMPARE(ini.value(QStringLiteral("NoDrive/problemReportsEnabled")).toBool(), true);

        toggle->click();
        QVERIFY(!ProblemReportSettings::enabled());

        // A new page starts with the saved state.
        ProblemReportSettings::setEnabled(true);
        SettingsDialog other(nullptr);
        QVERIFY(advancedPage(other)->findChild<SettingsSwitch *>(QStringLiteral("problemReportsSwitch"))->isChecked());
    }

    void theButtonSitsRightAfterCreateDebugArchive()
    {
        SettingsDialog dialog(nullptr);
        auto *page = advancedPage(dialog);
        QVERIFY(page);
        auto *debugArchive = page->findChild<QPushButton *>(QStringLiteral("debugArchiveButton"));
        auto *report = page->findChild<QPushButton *>(QStringLiteral("problemReportButton"));
        auto *actions = page->findChild<QGroupBox *>(QStringLiteral("advancedActionsGroupBox"));
        QVERIFY(debugArchive);
        QVERIFY(report);
        QVERIFY(actions);
        QCOMPARE(report->text(), QStringLiteral("Report a problem"));
        auto *layout = qobject_cast<QHBoxLayout *>(actions->layout());
        QVERIFY(layout);
        QCOMPARE(layout->indexOf(report), layout->indexOf(debugArchive) + 1);

        // The switch panel sits directly above the actions row.
        auto *toggle = page->findChild<SettingsSwitch *>(QStringLiteral("problemReportsSwitch"));
        auto *panel = toggle->parentWidget();
        auto *pageLayout = page->layout();
        QVERIFY(pageLayout->indexOf(panel) >= 0);
        QCOMPARE(pageLayout->indexOf(panel) + 1, pageLayout->indexOf(actions));
    }

    void reportDialogTexts()
    {
        ProblemReportDialog dialog;
        QCOMPARE(dialog.windowTitle(), QStringLiteral("Report a problem"));
        auto *question = dialog.findChild<QLabel *>(QStringLiteral("problemReportQuestion"));
        auto *link = dialog.findChild<QLabel *>(QStringLiteral("problemReportIncludedLink"));
        QVERIFY(question);
        QVERIFY(link);
        QCOMPARE(question->text(), QStringLiteral("What went wrong?"));
        QVERIFY(link->text().contains(QStringLiteral("What's included")));
        QCOMPARE(dialog.sendButton()->text(), QStringLiteral("Send"));
        QVERIFY(dialog.sendButton()->isDefault());
        QCOMPARE(dialog.cancelButton()->text(), QStringLiteral("Cancel"));
        QVERIFY(dialog.noteEdit()->toPlainText().isEmpty());
        QVERIFY(dialog.sendButton()->isEnabled());

        const auto included = ProblemReportDialog::whatsIncludedText();
        QVERIFY(included.contains(QStringLiteral("A problem report contains:")));
        QVERIFY(included.contains(QStringLiteral("File and folder names stay in the report")));
        QVERIFY(included.contains(QStringLiteral("shared, read-only, with Nourax Support")));
        QVERIFY(included.contains(QStringLiteral("Problem reports")));
        std::unique_ptr<QMessageBox> box(ProblemReportDialog::createWhatsIncludedBox());
#ifndef Q_OS_MACOS // macOS message boxes have no title
        QCOMPARE(box->windowTitle(), QStringLiteral("What's included"));
#endif
        QCOMPARE(box->text(), included);
    }

    void closedUnexpectedlyQuestion()
    {
        QCOMPARE(ProblemReportManager::closedUnexpectedlyText(), QStringLiteral("NoDrive closed unexpectedly. Send a problem report?"));
        std::unique_ptr<QMessageBox> box(ProblemReportManager::createClosedUnexpectedlyPrompt());
#ifndef Q_OS_MACOS // macOS message boxes have no title
        QCOMPARE(box->windowTitle(), QStringLiteral("NoDrive"));
#endif
        QCOMPARE(box->text(), QStringLiteral("NoDrive closed unexpectedly. Send a problem report?"));
        QVERIFY(box->defaultButton());
        QCOMPARE(box->defaultButton()->text(), QStringLiteral("Send"));
        QVERIFY(box->escapeButton());
        QCOMPARE(box->escapeButton()->text(), QStringLiteral("Not now"));
        QCOMPARE(box->buttons().size(), 2);
    }

    void resultTexts()
    {
        QCOMPARE(ProblemReportManager::outcomeText(ProblemReportManager::Outcome::Sent), QStringLiteral("Problem report sent"));
        QCOMPARE(ProblemReportManager::outcomeText(ProblemReportManager::Outcome::SavedNotShared),
            QStringLiteral("Saved in your NoDrive › Problem reports."));
        QCOMPARE(ProblemReportManager::outcomeText(ProblemReportManager::Outcome::SavedLocally),
            QStringLiteral("Saved on this computer. NoDrive sends it after you sign in."));
        QCOMPARE(ProblemReportManager::outcomeText(ProblemReportManager::Outcome::TryAgainLater),
            QStringLiteral("Could not send the problem report now. NoDrive will try again when it is connected."));
    }

    void noVisibleTextSaysNextcloud()
    {
        QStringList texts;
        SettingsDialog settings(nullptr);
        collectTexts(advancedPage(settings), texts);
        ProblemReportDialog dialog;
        collectTexts(&dialog, texts);
        std::unique_ptr<QMessageBox> included(ProblemReportDialog::createWhatsIncludedBox());
        texts << included->text() << included->windowTitle();
        std::unique_ptr<QMessageBox> prompt(ProblemReportManager::createClosedUnexpectedlyPrompt());
        texts << prompt->text() << prompt->windowTitle();
        for (auto outcome : { ProblemReportManager::Outcome::Sent, ProblemReportManager::Outcome::SavedNotShared,
                 ProblemReportManager::Outcome::SavedLocally, ProblemReportManager::Outcome::TryAgainLater }) {
            texts << ProblemReportManager::outcomeText(outcome);
        }
        for (const auto &text : std::as_const(texts)) {
            QVERIFY2(!text.contains(QStringLiteral("nextcloud"), Qt::CaseInsensitive), qPrintable(text));
        }
    }

    void screenshots_data()
    {
        QTest::addColumn<bool>("dark");
        QTest::newRow("light") << false;
        QTest::newRow("dark") << true;
    }

    void screenshots()
    {
        QFETCH(bool, dark);
        const auto scheme = dark ? QStringLiteral("dark") : QStringLiteral("light");
        applyColorScheme(dark);

        // Settings > Advanced, with the switch off and on.
        SettingsDialog dialog(nullptr);
        dialog.resize(900, 760);
        QTest::qWait(50); // the dialog opens its first page after 1 ms
        showAdvancedPage(dialog);
        auto *page = advancedPage(dialog);
        QVERIFY(page);
        auto *toggle = page->findChild<SettingsSwitch *>(QStringLiteral("problemReportsSwitch"));
        QVERIFY(toggle);
        auto *panel = toggle->parentWidget();

        QVERIFY(!toggle->isChecked());
        saveShot(&dialog, QStringLiteral("advanced-%1").arg(scheme));

        // Pixel checks on the panel: its background follows the colour scheme.
        const auto background = pixelAt(panel, QPoint(panel->width() / 2, 3));
        if (dark) {
            QVERIFY2(luminance(background) < 90, qPrintable(background.name()));
        } else {
            QVERIFY2(luminance(background) > 200, qPrintable(background.name()));
        }

        // The switched-on track is the Nourax blue.
        toggle->setChecked(true);
        const auto track = pixelAt(toggle, QPoint(toggle->width() / 2, toggle->height() / 2));
        QCOMPARE(track.name(), QStringLiteral("#2563eb"));
        saveShot(&dialog, QStringLiteral("advanced-switch-on-%1").arg(scheme));
        toggle->setChecked(false);

        // The "Report a problem" dialog and the "What's included" box.
        ProblemReportDialog report;
        report.resize(480, 340);
        report.noteEdit()->setPlainText(QStringLiteral("Sync stopped after I renamed a folder."));
        saveShot(&report, QStringLiteral("report-dialog-%1").arg(scheme));

        std::unique_ptr<QMessageBox> included(ProblemReportDialog::createWhatsIncludedBox());
        included->setMinimumWidth(560);
        saveShot(included.get(), QStringLiteral("whats-included-%1").arg(scheme));

        std::unique_ptr<QMessageBox> prompt(ProblemReportManager::createClosedUnexpectedlyPrompt());
        saveShot(prompt.get(), QStringLiteral("unclean-exit-prompt-%1").arg(scheme));
    }
};

QTEST_MAIN(TestNoDriveProblemReportUi)
#include "testnodriveproblemreportui.moc"
