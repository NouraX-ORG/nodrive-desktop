/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// NoAgenda: the window (QML), in light and dark mode, with screenshots.
//
// The window is built in a QML engine like the tray engine, from a canned scenario and a fixed clock
// (Wednesday 2026-10-28). Screenshots go to $NODRIVE_SCREENSHOT_DIR/noagenda/ (default: <bin>/nodrive-screenshots).
// Switches: NODRIVE_AGENDA_LIVE_DIR (a folder with a cache written by the server test) shows the data of
// a real throwaway server instead of the canned scenario (tools/ci/agenda-server.sh).

#include "nodrive/noagenda/agendatestsupport.h"

#include "gui/nourax/noagenda/ui/agendacontroller.h"
#include "gui/nourax/noagenda/ui/noagendalauncher.h"
#include "gui/nourax/noagenda/ui/noagendawindow.h"

#include "configfile.h"
#include "theme.h"

#include <QApplication>
#include <QGuiApplication>
#include <QImage>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QStyleHints>
#include <QTemporaryDir>

using namespace AgendaTest;

namespace {

QQuickItem *findItem(QQuickItem *root, const QString &objectName)
{
    if (!root) {
        return nullptr;
    }
    if (root->objectName() == objectName) {
        return root;
    }
    const auto children = root->childItems();
    for (auto *child : children) {
        if (auto *found = findItem(child, objectName)) {
            return found;
        }
    }
    return nullptr;
}

/// Every text that is on screen (visible items with a text property).
void collectTexts(QQuickItem *item, QStringList *out)
{
    if (!item || !item->isVisible()) {
        return;
    }
    const auto text = item->property("text");
    if (text.isValid() && text.typeId() == QMetaType::QString && !text.toString().isEmpty()) {
        out->append(text.toString());
    }
    const auto children = item->childItems();
    for (auto *child : children) {
        collectTexts(child, out);
    }
}

QStringList textsOf(QQuickWindow *window)
{
    QStringList out;
    collectTexts(window->contentItem(), &out);
    return out;
}

bool hasText(QQuickWindow *window, const QString &needle)
{
    const auto texts = textsOf(window);
    for (const auto &t : texts) {
        if (t.contains(needle)) {
            return true;
        }
    }
    return false;
}

int luminance(const QColor &c)
{
    return qRound(0.2126 * c.red() + 0.7152 * c.green() + 0.0722 * c.blue());
}

} // namespace

class TestNoDriveAgendaUi : public QObject
{
    Q_OBJECT

    QTemporaryDir _config;
    QQmlEngine _engine;
    QString _shots;

    struct Env
    {
        std::unique_ptr<AccountAgenda> agenda;
        std::unique_ptr<AgendaController> controller;
        QQuickWindow *window = nullptr;
        FakeTransport *transport = nullptr;
        ~Env() { delete window; }
    };

    std::unique_ptr<Env> open(const QString &user, bool dark, const std::function<void(FakeTransport *)> &tweak = {}, bool answerQuestion = true, const std::function<void(Env *)> &before = {})
    {
        auto env = std::make_unique<Env>();
        auto fake = std::make_unique<FakeTransport>();
        env->transport = fake.get();
        routeScenario(env->transport, user);
        if (tweak) {
            tweak(env->transport);
        }
        env->agenda = std::make_unique<AccountAgenda>(std::move(fake), _config.path() + QStringLiteral("/noagenda/") + user + (dark ? QStringLiteral("-dark") : QStringLiteral("-light")));
        env->agenda->setClock([] { return fixedNow(); });
        env->agenda->setZone(QTimeZone(QTimeZone::UTC));
        env->agenda->loadFromCache();
        if (before) {
            before(env.get());
        }
        env->agenda->refresh(true);
        if (answerQuestion && env->agenda->state().build.needsPersonalQuestion) {
            env->agenda->answerPersonalQuestion({QStringLiteral("personal")}, false);
        }
        env->controller = std::make_unique<AgendaController>(env->agenda.get());
        env->controller->setAccountName(user == QLatin1String("ali") ? QStringLiteral("Ali (drive.example.test)") : QStringLiteral("Mara (drive.example.test)"));
        QGuiApplication::styleHints()->setColorScheme(dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
        QTest::qWait(50);
        env->controller->agendaStyle()->setDark(dark);
        env->controller->setDate(QStringLiteral("2026-10-28"));

        QString error;
        env->window = createNoAgendaWindow(&_engine, env->controller.get(), &error);
        if (!env->window) {
            qWarning() << error;
        }
        return env;
    }

    void show(Env *env)
    {
        QVERIFY(env->window);
        env->window->resize(1040, 1000);
        env->window->show();
        QVERIFY(QTest::qWaitForWindowExposed(env->window));
        QTest::qWait(150);
    }

    void shoot(Env *env, const QString &name, bool dark)
    {
        QTest::qWait(150);
        const auto image = env->window->grabWindow();
        QVERIFY(!image.isNull());
        QDir().mkpath(_shots);
        const auto file = QStringLiteral("%1/%2-%3.png").arg(_shots, name, dark ? QStringLiteral("dark") : QStringLiteral("light"));
        QVERIFY2(image.save(file), qPrintable(file));
    }

    void checkBackground(Env *env, bool dark)
    {
        const auto image = env->window->grabWindow();
        const double dpr = image.devicePixelRatio();
        const QColor corner = image.pixelColor(qRound(6 * dpr), image.height() - qRound(6 * dpr));
        if (dark) {
            QVERIFY2(luminance(corner) < 90, qPrintable(corner.name()));
        } else {
            QVERIFY2(luminance(corner) > 200, qPrintable(corner.name()));
        }
    }

    void checkSelectedChip(Env *env, bool dark)
    {
        auto *chip = findItem(env->window->contentItem(), QStringLiteral("chipMe"));
        QVERIFY(chip);
        const auto image = env->window->grabWindow();
        const double dpr = image.devicePixelRatio();
        const auto topLeft = chip->mapToScene(QPointF(0, 0));
        const QColor c = image.pixelColor(qRound((topLeft.x() + 6) * dpr), qRound((topLeft.y() + chip->height() / 2) * dpr));
        const QColor want = dark ? QColor(QStringLiteral("#3B82F6")) : QColor(QStringLiteral("#2563EB"));
        QVERIFY2(qAbs(c.red() - want.red()) < 6 && qAbs(c.green() - want.green()) < 6 && qAbs(c.blue() - want.blue()) < 6, qPrintable(c.name() + QStringLiteral(" vs ") + want.name()));
    }

    static void click(QQuickWindow *window, QQuickItem *item)
    {
        QVERIFY(item);
        const auto center = item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
        QTest::mouseClick(window, Qt::LeftButton, {}, center);
        QTest::qWait(60);
    }

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY(_config.isValid());
        OCC::ConfigFile::setConfDir(_config.path());
        _shots = qEnvironmentVariable("NODRIVE_SCREENSHOT_DIR", QCoreApplication::applicationDirPath() + QStringLiteral("/nodrive-screenshots")) + QStringLiteral("/noagenda");
        initNoAgendaResources();
    }

    void cleanup()
    {
        QGuiApplication::styleHints()->unsetColorScheme();
    }

    void everyQmlFileLoads()
    {
        const auto env = open(QStringLiteral("ali"), false);
        QVERIFY(env->window);
        QCOMPARE(env->window->title(), QStringLiteral("NoAgenda"));
        QVERIFY(env->window->minimumWidth() >= 720);
        QVERIFY(env->window->minimumHeight() >= 520);
    }

    void thePlainEnglishTexts()
    {
        const auto env = open(QStringLiteral("ali"), false);
        show(env.get());
        QVERIFY(hasText(env->window, QStringLiteral("Day")));
        QVERIFY(hasText(env->window, QStringLiteral("Week")));
        QVERIFY(hasText(env->window, QStringLiteral("Month")));
        QVERIFY(hasText(env->window, QStringLiteral("Today")));
        QVERIFY(hasText(env->window, QStringLiteral("All")));
        QVERIFY(hasText(env->window, QStringLiteral("Work")));
        QVERIFY(hasText(env->window, QStringLiteral("Personal")));
        QVERIFY(hasText(env->window, QStringLiteral("Me")));
        QVERIFY(hasText(env->window, QStringLiteral("My agents")));
        QVERIFY(hasText(env->window, QStringLiteral("My team")));
        QVERIFY(hasText(env->window, QStringLiteral("Refresh")));
        QVERIFY(hasText(env->window, QStringLiteral("Sources")));
        QVERIFY(hasText(env->window, QStringLiteral("Due today")));
        QVERIFY(hasText(env->window, QStringLiteral("Self")));
        QVERIFY(hasText(env->window, QStringLiteral("Writer-Bot")));
        QVERIFY(hasText(env->window, QStringLiteral("Dentist")));
        QVERIFY(hasText(env->window, QStringLiteral("Client call")));
    }

    void noTextOfTheWindowEverSaysTheServerSoftwareName()
    {
        for (const char *user : {"ali", "mara"}) {
            const auto env = open(QString::fromLatin1(user), false);
            show(env.get());
            // all pages
            for (const auto &page : {QStringLiteral("agenda"), QStringLiteral("sources")}) {
                env->controller->setPage(page);
                QTest::qWait(100);
                for (const auto &view : {QStringLiteral("day"), QStringLiteral("week"), QStringLiteral("month")}) {
                    env->controller->setViewMode(view);
                    QTest::qWait(100);
                    const auto texts = textsOf(env->window);
                    for (const auto &t : texts) {
                        QVERIFY2(!t.contains(QStringLiteral("nextcloud"), Qt::CaseInsensitive), qPrintable(t));
                    }
                }
            }
            // and every string the controller hands over
            const auto rows = env->controller->rows();
            for (const auto &row : rows) {
                const auto map = row.toMap();
                for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
                    QVERIFY2(!it.value().toString().contains(QStringLiteral("nextcloud"), Qt::CaseInsensitive), qPrintable(it.key()));
                }
            }
        }
    }

    void dayViewShowsMyDayInLightAndDark_data()
    {
        QTest::addColumn<bool>("dark");
        QTest::newRow("light") << false;
        QTest::newRow("dark") << true;
    }

    void dayViewShowsMyDayInLightAndDark()
    {
        QFETCH(bool, dark);
        const auto env = open(QStringLiteral("ali"), dark);
        show(env.get());
        auto *content = env->window->contentItem();
        // the rows of the day
        for (const char *key : {"row-cal:personal/dentist.ics", "row-cal:work/call.ics", "row-gantt:task:101", "row-deck:card:503", "row-cal:personal/td2.ics"}) {
            QVERIFY2(findItem(content, QString::fromLatin1(key)), key);
        }
        // the dentist and the client call are both at 10:00 (the clash of feature 2 is not drawn yet)
        QVERIFY(hasText(env->window, QStringLiteral("10:00")));
        QVERIFY(hasText(env->window, QStringLiteral("Late")));
        QVERIFY(hasText(env->window, QStringLiteral("2 days late")));
        QVERIFY(hasText(env->window, QStringLiteral("In progress")));
        QVERIFY(hasText(env->window, QStringLiteral("No date")));
        checkBackground(env.get(), dark);
        checkSelectedChip(env.get(), dark);
        shoot(env.get(), QStringLiteral("day"), dark);
    }

    void aSupervisedRowShowsItsAgentAndSelfRowsShowSelf()
    {
        const auto env = open(QStringLiteral("ali"), false);
        const auto rows = env->controller->rows();
        QString review;
        QString call;
        for (const auto &r : rows) {
            const auto m = r.toMap();
            if (m.value(QStringLiteral("key")).toString() == QLatin1String("cal:work/agent.ics")) {
                review = m.value(QStringLiteral("agentText")).toString() + QLatin1Char('|') + m.value(QStringLiteral("workKind")).toString();
            }
            if (m.value(QStringLiteral("key")).toString() == QLatin1String("cal:work/call.ics")) {
                call = m.value(QStringLiteral("agentText")).toString() + QLatin1Char('|') + m.value(QStringLiteral("workKind")).toString();
            }
        }
        QCOMPARE(review, QStringLiteral("Writer-Bot|agent"));
        QCOMPARE(call, QStringLiteral("Self|self"));
    }

    void theChipsFilterTheThreeKindsOfWork()
    {
        const auto env = open(QStringLiteral("ali"), false);
        show(env.get());
        auto *content = env->window->contentItem();
        QVERIFY(findItem(content, QStringLiteral("row-cal:work/agent.ics")));
        QVERIFY(findItem(content, QStringLiteral("row-cal:work/call.ics")));
        // My agents off: the supervised rows go, mine stay
        click(env->window, findItem(content, QStringLiteral("chipAgents")));
        QVERIFY(!env->controller->chipAgents());
        QTRY_VERIFY(!findItem(content, QStringLiteral("row-cal:work/agent.ics")));
        QTRY_VERIFY(findItem(content, QStringLiteral("row-cal:work/call.ics")));
        // Me off, My agents on: the other way round
        click(env->window, findItem(content, QStringLiteral("chipAgents")));
        click(env->window, findItem(content, QStringLiteral("chipMe")));
        QVERIFY(env->controller->chipAgents() && !env->controller->chipMe());
        QTRY_VERIFY(findItem(content, QStringLiteral("row-cal:work/agent.ics")));
        QTRY_VERIFY(!findItem(content, QStringLiteral("row-cal:work/call.ics")));
    }

    void allWorkPersonalSwitch()
    {
        const auto env = open(QStringLiteral("ali"), false);
        show(env.get());
        auto *content = env->window->contentItem();
        env->controller->setCategory(QStringLiteral("work"));
        QTRY_VERIFY(findItem(content, QStringLiteral("row-cal:work/call.ics")));
        QTRY_VERIFY(!findItem(content, QStringLiteral("row-cal:personal/dentist.ics")));
        env->controller->setCategory(QStringLiteral("personal"));
        QTRY_VERIFY(!findItem(content, QStringLiteral("row-cal:work/call.ics")));
        QTRY_VERIFY(findItem(content, QStringLiteral("row-cal:personal/dentist.ics")));
        // projects and boards are work
        QVERIFY(!findItem(content, QStringLiteral("row-gantt:task:101")));
        env->controller->setCategory(QStringLiteral("all"));
        QTRY_VERIFY(findItem(content, QStringLiteral("row-cal:work/call.ics")) && findItem(content, QStringLiteral("row-cal:personal/dentist.ics")));
    }

    void switchingToWeekAndMonthWorks_data()
    {
        QTest::addColumn<bool>("dark");
        QTest::newRow("light") << false;
        QTest::newRow("dark") << true;
    }

    void switchingToWeekAndMonthWorks()
    {
        QFETCH(bool, dark);
        const auto env = open(QStringLiteral("ali"), dark);
        show(env.get());
        auto *content = env->window->contentItem();
        click(env->window, findItem(content, QStringLiteral("view-week")));
        QCOMPARE(env->controller->viewMode(), QStringLiteral("week"));
        QVERIFY(env->controller->rangeText().contains(QStringLiteral("2026")));
        // a header for every day of the week
        int dayHeaders = 0;
        for (const auto &r : env->controller->rows()) {
            dayHeaders += r.toMap().value(QStringLiteral("type")).toString() == QLatin1String("day") ? 1 : 0;
        }
        QCOMPARE(dayHeaders, 7);
        shoot(env.get(), QStringLiteral("week"), dark);

        click(env->window, findItem(content, QStringLiteral("view-month")));
        QCOMPARE(env->controller->viewMode(), QStringLiteral("month"));
        QCOMPARE(env->controller->monthGrid().size(), 42);
        QVERIFY(env->controller->rangeText().contains(QStringLiteral("October")) || env->controller->rangeText().contains(QStringLiteral("2026")));
        // only days with items have a header in the month list
        dayHeaders = 0;
        for (const auto &r : env->controller->rows()) {
            dayHeaders += r.toMap().value(QStringLiteral("type")).toString() == QLatin1String("day") ? 1 : 0;
        }
        QVERIFY(dayHeaders > 0 && dayHeaders < 31);
        shoot(env.get(), QStringLiteral("month"), dark);

        click(env->window, findItem(content, QStringLiteral("view-day")));
        QCOMPARE(env->controller->viewMode(), QStringLiteral("day"));
        // Previous / next / today
        click(env->window, findItem(content, QStringLiteral("nextButton")));
        QCOMPARE(env->controller->date(), QDate(2026, 10, 29));
        click(env->window, findItem(content, QStringLiteral("todayButton")));
        QCOMPARE(env->controller->date(), QDate(2026, 10, 28));
        click(env->window, findItem(content, QStringLiteral("previousButton")));
        QCOMPARE(env->controller->date(), QDate(2026, 10, 27));
    }

    void myTeamShowsProblemsOnlyAndTheSwitchShowsAll_data()
    {
        QTest::addColumn<bool>("dark");
        QTest::newRow("light") << false;
        QTest::newRow("dark") << true;
    }

    void myTeamShowsProblemsOnlyAndTheSwitchShowsAll()
    {
        QFETCH(bool, dark);
        const auto env = open(QStringLiteral("mara"), dark);
        show(env.get());
        auto *content = env->window->contentItem();
        QVERIFY(env->controller->hasTeamSection());
        QVERIFY(hasText(env->window, QStringLiteral("My team")));
        // a problem row names the person, the agent and the problem
        auto *late = findItem(content, QStringLiteral("row-gantt:task:103:sara"));
        QVERIFY(late);
        QVERIFY(hasText(env->window, QStringLiteral("Banner design")));
        QVERIFY(hasText(env->window, QStringLiteral("Sara")));
        QVERIFY(hasText(env->window, QStringLiteral("Design-Bot")));
        QVERIFY(hasText(env->window, QStringLiteral("2 days late")));
        // a row without a problem is not there...
        QVERIFY(!findItem(content, QStringLiteral("row-gantt:task:101:ali")));
        shoot(env.get(), QStringLiteral("my-team"), dark);
        // ...until "Show all team tasks"
        click(env->window, findItem(content, QStringLiteral("showAllTeam")));
        QVERIFY(env->controller->showAllTeam());
        QTRY_VERIFY(findItem(content, QStringLiteral("row-gantt:task:101:ali")));
        // the chip hides the whole section
        click(env->window, findItem(content, QStringLiteral("chipTeam")));
        QTRY_VERIFY(!findItem(content, QStringLiteral("row-gantt:task:103:sara")));
    }

    void theDetailPanelHasOnlyOpenIn()
    {
        const auto env = open(QStringLiteral("ali"), false);
        show(env.get());
        auto *content = env->window->contentItem();
        QVERIFY(!findItem(content, QStringLiteral("detailPanel"))->isVisible());
        click(env->window, findItem(content, QStringLiteral("row-gantt:task:101")));
        QCOMPARE(env->controller->selectedKey(), QStringLiteral("gantt:task:101"));
        auto *panel = findItem(content, QStringLiteral("detailPanel"));
        QVERIFY(panel->isVisible());
        QVERIFY(hasText(env->window, QStringLiteral("Write homepage copy")));
        QVERIFY(hasText(env->window, QStringLiteral("Open in NoGantt")));
        QVERIFY(!hasText(env->window, QStringLiteral("Delete")));
        QCOMPARE(env->controller->detail().value(QStringLiteral("agentText")).toString(), QStringLiteral("Writer-Bot"));
        // Open in...: the account's own server, in the default browser
        QUrl opened;
        connect(env->controller.get(), &AgendaController::openUrlRequested, this, [&opened](const QUrl &u) { opened = u; });
        click(env->window, findItem(content, QStringLiteral("openInButton")));
        QCOMPARE(opened.toString(), QStringLiteral("https://drive.example.test/index.php/apps/nc_gantt/?project=1&task=101"));
    }

    void detailInLightAndDark_data()
    {
        QTest::addColumn<bool>("dark");
        QTest::newRow("light") << false;
        QTest::newRow("dark") << true;
    }

    void detailInLightAndDark()
    {
        QFETCH(bool, dark);
        const auto env = open(QStringLiteral("ali"), dark);
        show(env.get());
        env->controller->select(QStringLiteral("gantt:task:108"));
        QTest::qWait(100);
        QVERIFY(hasText(env->window, QStringLiteral("Two agents for Ali: choose one")));
        shoot(env.get(), QStringLiteral("detail"), dark);
    }

    void theKeyboardMovesTheSelection()
    {
        const auto env = open(QStringLiteral("ali"), false);
        show(env.get());
        QVERIFY(env->controller->selectedKey().isEmpty());
        env->controller->moveSelection(1);
        const auto first = env->controller->selectedKey();
        QVERIFY(!first.isEmpty());
        env->controller->moveSelection(1);
        QVERIFY(env->controller->selectedKey() != first);
        env->controller->moveSelection(-1);
        QCOMPARE(env->controller->selectedKey(), first);
        env->controller->clearSelection();
        QVERIFY(env->controller->selectedKey().isEmpty());
    }

    void sourcesScreenWithTheDefaults_data()
    {
        QTest::addColumn<bool>("dark");
        QTest::newRow("light") << false;
        QTest::newRow("dark") << true;
    }

    void sourcesScreenWithTheDefaults()
    {
        QFETCH(bool, dark);
        const auto env = open(QStringLiteral("mara"), dark);
        show(env.get());
        click(env->window, findItem(env->window->contentItem(), QStringLiteral("sourcesButton")));
        QCOMPARE(env->controller->page(), QStringLiteral("sources"));
        QTest::qWait(100);
        auto *page = findItem(env->window->contentItem(), QStringLiteral("sourcesPage"));
        QVERIFY(page && page->isVisible());
        QVERIFY(hasText(env->window, QStringLiteral("Projects (NoGantt)")));
        QVERIFY(hasText(env->window, QStringLiteral("Boards (NoBoard)")));
        QVERIFY(hasText(env->window, QStringLiteral("Website")));
        QVERIFY(hasText(env->window, QStringLiteral("Team board")));
        QVERIFY(hasText(env->window, QStringLiteral("Do + Oversee")));
        QVERIFY(hasText(env->window, QStringLiteral("Working hours")));
        QVERIFY(hasText(env->window, QStringLiteral("Agents")));
        // Changing a setting on the screen changes it for real.
        click(env->window, findItem(page, QStringLiteral("mode-2-hidden")));
        bool hidden = false;
        for (const auto &s : env->agenda->state().sources) {
            if (s.type == SourceEntry::Type::Project && s.key == QLatin1String("2")) {
                hidden = s.mode == QLatin1String("hidden");
            }
        }
        QVERIFY(hidden);
        shoot(env.get(), QStringLiteral("sources"), dark);
    }

    void sourcesOfAliShowCalendarsWithAutoHiddenOnes()
    {
        const auto env = open(QStringLiteral("ali"), false);
        const auto sources = env->controller->sources();
        QStringList names;
        QString deckNote;
        for (const auto &v : sources) {
            const auto m = v.toMap();
            names.append(m.value(QStringLiteral("name")).toString());
            if (m.value(QStringLiteral("key")).toString() == QLatin1String("app-generated--deck--board-10")) {
                deckNote = m.value(QStringLiteral("note")).toString();
                QVERIFY(m.value(QStringLiteral("autoHidden")).toBool());
            }
        }
        QVERIFY(names.contains(QStringLiteral("Personal")));
        QVERIFY(names.contains(QStringLiteral("Team")));
        QVERIFY(deckNote.startsWith(QStringLiteral("Hidden automatically")));
    }

    void theOneTimeQuestionComesOnceAndSaves_data()
    {
        QTest::addColumn<bool>("dark");
        QTest::newRow("light") << false;
        QTest::newRow("dark") << true;
    }

    void theOneTimeQuestionComesOnceAndSaves()
    {
        QFETCH(bool, dark);
        const auto env = open(QStringLiteral("ali"), dark, {}, false);
        show(env.get());
        auto *dialog = findItem(env->window->contentItem(), QStringLiteral("personalDialog"));
        QVERIFY(dialog && dialog->isVisible());
        QVERIFY(hasText(env->window, QStringLiteral("Which of your calendars are personal?")));
        QVERIFY(hasText(env->window, QStringLiteral("Personal events are only ever shown to you. The others count as Work.")));
        QVERIFY(hasText(env->window, QStringLiteral("Save")));
        QVERIFY(hasText(env->window, QStringLiteral("Skip")));
        QVERIFY(findItem(dialog, QStringLiteral("personalCalendar-personal")));
        QVERIFY(findItem(dialog, QStringLiteral("personalCalendar-work")));
        shoot(env.get(), QStringLiteral("personal-question"), dark);
        // Untick "work", save: it counts as Work from now on, and the question is gone for good.
        click(env->window, findItem(dialog, QStringLiteral("personalCalendar-work")));
        click(env->window, findItem(dialog, QStringLiteral("personalSave")));
        QVERIFY(!env->controller->personalQuestionVisible());
        QVERIFY(!dialog->isVisible());
        QVERIFY(!env->agenda->state().build.needsPersonalQuestion);
        QCOMPARE(find(env->agenda->state().items, QStringLiteral("cal:work/call.ics"))->category, Category::Work);
        QCOMPARE(find(env->agenda->state().items, QStringLiteral("cal:personal/dentist.ics"))->category, Category::Personal);
    }

    void skippingTheQuestionKeepsEverythingPersonal()
    {
        const auto env = open(QStringLiteral("ali"), false, {}, false);
        show(env.get());
        auto *dialog = findItem(env->window->contentItem(), QStringLiteral("personalDialog"));
        click(env->window, findItem(dialog, QStringLiteral("personalSkip")));
        QVERIFY(!dialog->isVisible());
        QCOMPARE(find(env->agenda->state().items, QStringLiteral("cal:work/call.ics"))->category, Category::Personal);
    }

    void notLinkedToAPersonInNoGantt_data()
    {
        QTest::addColumn<bool>("dark");
        QTest::newRow("light") << false;
        QTest::newRow("dark") << true;
    }

    void notLinkedToAPersonInNoGantt()
    {
        QFETCH(bool, dark);
        const auto env = open(QStringLiteral("ali"), dark, [](FakeTransport *t) {
            t->answers.removeIf([](const FakeTransport::Answer &a) { return a.path.endsWith(QLatin1String("/my-tasks")); });
            t->routeOcs(QStringLiteral("my-tasks"), QStringLiteral("gantt/my-tasks-ali-unlinked.json"));
            // nothing else to show: no calendars, no board
            t->answers.removeIf([](const FakeTransport::Answer &a) { return a.verb == "PROPFIND" || a.path.contains(QStringLiteral("/apps/deck/")); });
            t->answers.removeIf([](const FakeTransport::Answer &a) { return a.path.contains(QStringLiteral("/projects/")); });
        });
        show(env.get());
        QVERIFY(hasText(env->window, QStringLiteral("You're not linked to a person in NoGantt yet. Ask your project manager to link you.")));
        shoot(env.get(), QStringLiteral("empty-not-linked"), dark);
    }

    void offlineShowsWhatWasSavedAndWhen_data()
    {
        QTest::addColumn<bool>("dark");
        QTest::newRow("light") << false;
        QTest::newRow("dark") << true;
    }

    void offlineShowsWhatWasSavedAndWhen()
    {
        QFETCH(bool, dark);
        const auto env = open(QStringLiteral("ali"), dark);
        env->transport->offline = true;
        env->agenda->refresh(true);
        show(env.get());
        QVERIFY(hasText(env->window, QStringLiteral("can't be reached; showing what was saved at")));
        QVERIFY(hasText(env->window, QStringLiteral("Dentist"))); // the saved rows are still there
        shoot(env.get(), QStringLiteral("offline-banner"), dark);
    }

    void aSingleSourceDownHasItsOwnBanner()
    {
        const auto env = open(QStringLiteral("ali"), false);
        for (auto &a : env->transport->answers) {
            if (a.path.contains(QStringLiteral("/nc_gantt/"))) {
                a.status = 503;
            }
        }
        env->agenda->refresh(true);
        show(env.get());
        QVERIFY(hasText(env->window, QStringLiteral("NoGantt can't be reached; showing what was saved at")));
        QVERIFY(!hasText(env->window, QStringLiteral("NoBoard can't be reached")));
        QVERIFY(findItem(env->window->contentItem(), QStringLiteral("row-gantt:task:101"))); // the old rows stay
    }

    void offlineWithoutACacheSaysSo()
    {
        const auto env = open(QStringLiteral("ali"), false, [](FakeTransport *t) { t->offline = true; });
        env->agenda->setOnline(false);
        show(env.get());
        QVERIFY(hasText(env->window, QStringLiteral("You're offline. NoAgenda will load when NoDrive is connected.")));
    }

    void aSignedOutAccountOffersToLogIn()
    {
        const auto env = open(QStringLiteral("ali"), false);
        env->agenda->setNeedsLogin(true);
        show(env.get());
        QVERIFY(hasText(env->window, QStringLiteral("Sign in to NoDrive to see new changes.")));
        QVERIFY(hasText(env->window, QStringLiteral("Log in")));
        bool login = false;
        connect(env->controller.get(), &AgendaController::loginRequested, this, [&login] { login = true; });
        env->controller->bannerAction(QStringLiteral("login"));
        QVERIFY(login);
    }

    void aMissingOrBrokenRegistryIsExplainedOnTheSourcesScreen_data()
    {
        QTest::addColumn<bool>("dark");
        QTest::newRow("light") << false;
        QTest::newRow("dark") << true;
    }

    void aMissingOrBrokenRegistryIsExplainedOnTheSourcesScreen()
    {
        QFETCH(bool, dark);
        {
            const auto env = open(QStringLiteral("ali"), dark, [](FakeTransport *t) {
                t->answers.removeIf([](const FakeTransport::Answer &a) { return a.path.endsWith(QLatin1String("agent-registry.json")); });
            });
            show(env.get());
            env->controller->setPage(QStringLiteral("sources"));
            QTest::qWait(100);
            QVERIFY(hasText(env->window, QStringLiteral("No agent list found in your NoDrive (NoAgenda/agent-registry.json). Everything shows as Self.")));
            shoot(env.get(), QStringLiteral("registry-missing"), dark);
        }
        {
            const auto env = open(QStringLiteral("ali"), dark, [](FakeTransport *t) {
                t->answers.removeIf([](const FakeTransport::Answer &a) { return a.path.endsWith(QLatin1String("agent-registry.json")); });
                t->route("GET", QStringLiteral("/remote.php/dav/files/ali/NoAgenda/agent-registry.json"), 200, "{ this is not json");
            });
            env->controller->setPage(QStringLiteral("sources"));
            QCOMPARE(env->controller->registryMessage(), QStringLiteral("The agent list in your NoDrive can't be read. Everything shows as Self until it's fixed."));
        }
        {
            const auto env = open(QStringLiteral("ali"), dark, [](FakeTransport *t) {
                t->answers.removeIf([](const FakeTransport::Answer &a) { return a.path.endsWith(QLatin1String("agent-registry.json")); });
                t->route("GET", QStringLiteral("/remote.php/dav/files/ali/NoAgenda/agent-registry.json"), 200,
                    R"({"schema":"nourax-agent-registry","version":1,"agents":[{"id":"AG-1","name":"Ok","person":"ali","status":"active","organisation":"o"},{"id":"AG-2"}]})");
            });
            QCOMPARE(env->controller->registryMessage(), QStringLiteral("1 agent in the list was skipped because it is incomplete."));
        }
    }

    void agentsAreListedWithTheirStatus()
    {
        const auto env = open(QStringLiteral("ali"), false);
        QStringList lines;
        for (const auto &v : env->controller->agents()) {
            lines.append(v.toMap().value(QStringLiteral("name")).toString() + QLatin1Char('=') + v.toMap().value(QStringLiteral("statusText")).toString());
        }
        QCOMPARE(lines, (QStringList{QStringLiteral("Writer-Bot=Active"), QStringLiteral("Research-Bot=Paused"), QStringLiteral("Invoice-Bot=Retired")}));
        QVERIFY(env->controller->registryMessage().isEmpty());
    }

    void theWindowIsShownForAnyOfTheThreeViewsWithoutQmlWarnings()
    {
        // The QML of every page is exercised above; this one makes sure the engine reported no error.
        const auto env = open(QStringLiteral("mara"), true);
        show(env.get());
        for (const auto &view : {QStringLiteral("week"), QStringLiteral("month"), QStringLiteral("day")}) {
            env->controller->setViewMode(view);
            QTest::qWait(60);
        }
        QVERIFY(env->window->isVisible());
    }

    void liveDataFromTheServerTest()
    {
        const auto liveDir = qEnvironmentVariable("NODRIVE_AGENDA_LIVE_DIR");
        if (liveDir.isEmpty()) {
            QSKIP("NODRIVE_AGENDA_LIVE_DIR is not set (the server job of tools/ci/agenda-server.sh sets it)");
        }
        for (const bool dark : {false, true}) {
            auto fake = std::make_unique<FakeTransport>();
            AccountAgenda agenda(std::move(fake), liveDir);
            agenda.setZone(QTimeZone::systemTimeZone());
            agenda.loadFromCache();
            AgendaController controller(&agenda);
            controller.setAccountName(QStringLiteral("Ali (throwaway server)"));
            QGuiApplication::styleHints()->setColorScheme(dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
            QTest::qWait(50);
            controller.agendaStyle()->setDark(dark);
            QString error;
            auto *window = createNoAgendaWindow(&_engine, &controller, &error);
            QVERIFY2(window, qPrintable(error));
            window->resize(1040, 1000);
            window->show();
            QVERIFY(QTest::qWaitForWindowExposed(window));
            QTest::qWait(300);
            const auto image = window->grabWindow();
            QDir().mkpath(_shots);
            QVERIFY(image.save(QStringLiteral("%1/live-day-%2.png").arg(_shots, dark ? QStringLiteral("dark") : QStringLiteral("light"))));
            delete window;
        }
    }
};

int main(int argc, char *argv[])
{
    // The same software renderer everywhere: no GPU is needed to take the screenshots.
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QApplication app(argc, argv);
    TestNoDriveAgendaUi test;
    return QTest::qExec(&test, argc, argv);
}

#include "testnodriveagendaui.moc"
