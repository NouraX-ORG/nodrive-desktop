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

#include "nodrive/noagenda/edittestsupport.h"

#include "gui/nourax/noagenda/ui/agendacontroller.h"
#include "gui/nourax/noagenda/ui/noagendalauncher.h"
#include "gui/nourax/noagenda/ui/noagendawindow.h"

#include "configfile.h"
#include "theme.h"

#include <QApplication>
#include <QGuiApplication>
#include <QImage>
#include <QLocale>
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
    int _opened = 0;
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
        // Every window gets its own cache folder: the settings (chips, answers) must not leak from one test to the next.
        env->agenda = std::make_unique<AccountAgenda>(std::move(fake), _config.path() + QStringLiteral("/noagenda/") + user + QLatin1Char('-') + QString::number(++_opened));
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

    // ---- helpers of the edit step ------------------------------------------------------------------------------------

    static int countShown(QQuickItem *root, const QString &objectName)
    {
        if (!root || !root->isVisible()) {
            return 0;
        }
        int n = root->objectName() == objectName ? 1 : 0;
        const auto children = root->childItems();
        for (auto *child : children) {
            n += countShown(child, objectName);
        }
        return n;
    }

    static bool shown(QQuickItem *root, const QString &objectName)
    {
        return countShown(root, objectName) > 0;
    }

    static void darkRows()
    {
        QTest::addColumn<bool>("dark");
        QTest::newRow("light") << false;
        QTest::newRow("dark") << true;
    }

    static void prepend(FakeTransport *t, const QByteArray &verb, const QString &path, int status, const QByteArray &body, const QMap<QByteArray, QByteArray> &headers = {})
    {
        t->answers.prepend({verb, path, QByteArray(), status, body, headers});
    }

    static QByteArray ics(const QString &uid, const QString &summary)
    {
        return (QStringLiteral("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VEVENT\r\nUID:") + uid + QStringLiteral("\r\nSUMMARY:") + summary
            + QStringLiteral("\r\nDTSTART:20261028T100000Z\r\nDTEND:20261028T110000Z\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n")).toUtf8();
    }

    /// The NoGantt tasks of the fixtures, with the total work of the task "Landing page build" filled in as the server does.
    static QByteArray ganttTasksWithWork()
    {
        auto tasks = fixtureJson(QStringLiteral("gantt/tasks-1.json")).toArray();
        for (int i = 0; i < tasks.size(); ++i) {
            auto t = tasks.at(i).toObject();
            if (t.value(QStringLiteral("id")).toInt() == 111) {
                t.insert(QStringLiteral("work"), 0.75);
                tasks.replace(i, t);
            }
        }
        return ocsBody(tasks);
    }

    static void routeGanttAgentEdit(FakeTransport *t)
    {
        prepend(t, "GET", QStringLiteral("/ocs/v2.php/apps/nc_gantt/api/v1/projects/1/tasks"), 200, ganttTasksWithWork());
        QJsonObject saved;
        for (const auto &v : fixtureJson(QStringLiteral("gantt/tasks-1.json")).toArray()) {
            if (v.toObject().value(QStringLiteral("id")).toInt() == 111) {
                saved = v.toObject();
            }
        }
        saved.insert(QStringLiteral("work"), 0.75);
        saved.insert(QStringLiteral("updated_at"), QStringLiteral("2026-10-28 12:00:00"));
        saved.insert(QStringLiteral("resources"), QJsonArray{QJsonObject{{QStringLiteral("resource_id"), 1}, {QStringLiteral("work"), 6}}, QJsonObject{{QStringLiteral("resource_id"), 11}, {QStringLiteral("work"), QJsonValue(QJsonValue::Null)}}});
        prepend(t, "PUT", QStringLiteral("/ocs/v2.php/apps/nc_gantt/api/v1/tasks/111"), 200,
            ocsBody(QJsonObject{{QStringLiteral("task"), saved}, {QStringLiteral("propagated"), QJsonArray()}, {QStringLiteral("op_id"), QJsonValue(QJsonValue::Null)}, {QStringLiteral("deck"), QJsonObject()}}));
    }

    static QString allText(QQuickWindow *window)
    {
        return textsOf(window).join(QStringLiteral("\n"));
    }

    void selectAndWait(Env *env, const QString &key)
    {
        env->controller->select(key);
        QTest::qWait(120);
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
        QTRY_VERIFY(hasText(env->window, QStringLiteral("Day")));
        QTRY_VERIFY(hasText(env->window, QStringLiteral("Week")));
        QTRY_VERIFY(hasText(env->window, QStringLiteral("Month")));
        QTRY_VERIFY(hasText(env->window, QStringLiteral("Today")));
        QTRY_VERIFY(hasText(env->window, QStringLiteral("All")));
        QTRY_VERIFY(hasText(env->window, QStringLiteral("Work")));
        QTRY_VERIFY(hasText(env->window, QStringLiteral("Personal")));
        QTRY_VERIFY(hasText(env->window, QStringLiteral("Me")));
        QTRY_VERIFY(hasText(env->window, QStringLiteral("My agents")));
        QTRY_VERIFY(hasText(env->window, QStringLiteral("My team")));
        QTRY_VERIFY(hasText(env->window, QStringLiteral("Refresh")));
        QTRY_VERIFY(hasText(env->window, QStringLiteral("Sources")));
        QTRY_VERIFY(hasText(env->window, QStringLiteral("Due today")));
        QTRY_VERIFY(hasText(env->window, QStringLiteral("Self")));
        QTRY_VERIFY(hasText(env->window, QStringLiteral("Writer-Bot")));
        QTRY_VERIFY(hasText(env->window, QStringLiteral("Dentist")));
        QTRY_VERIFY(hasText(env->window, QStringLiteral("Client call")));
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
        QTRY_VERIFY(hasText(env->window, QStringLiteral("10:00")));
        QTRY_VERIFY(hasText(env->window, QStringLiteral("Late")));
        QTRY_VERIFY(hasText(env->window, QStringLiteral("2 days late")));
        QTRY_VERIFY(hasText(env->window, QStringLiteral("In progress")));
        QTRY_VERIFY(hasText(env->window, QStringLiteral("No date")));
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
        QTRY_VERIFY(hasText(env->window, QStringLiteral("My team")));
        // a problem row names the person, the agent and the problem
        auto *late = findItem(content, QStringLiteral("row-gantt:task:103:sara"));
        QVERIFY(late);
        QTRY_VERIFY(hasText(env->window, QStringLiteral("Banner design")));
        QTRY_VERIFY(hasText(env->window, QStringLiteral("Sara")));
        QTRY_VERIFY(hasText(env->window, QStringLiteral("Design-Bot")));
        QTRY_VERIFY(hasText(env->window, QStringLiteral("2 days late")));
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
        // A click on a row near the top selects it (rows further down are below the window).
        click(env->window, findItem(content, QStringLiteral("row-cal:personal/dentist.ics")));
        QCOMPARE(env->controller->selectedKey(), QStringLiteral("cal:personal/dentist.ics"));
        QTRY_VERIFY(hasText(env->window, QStringLiteral("Open in NoCalendar")));
        env->controller->select(QStringLiteral("gantt:task:101"));
        auto *panel = findItem(content, QStringLiteral("detailPanel"));
        QVERIFY(panel->isVisible());
        QTRY_VERIFY(hasText(env->window, QStringLiteral("Write homepage copy")));
        QTRY_VERIFY(hasText(env->window, QStringLiteral("Open in NoGantt")));
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
        QTRY_VERIFY(hasText(env->window, QStringLiteral("Two agents for Ali: choose one")));
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
        QTRY_VERIFY(hasText(env->window, QStringLiteral("Projects (NoGantt)")));
        QTRY_VERIFY(hasText(env->window, QStringLiteral("Boards (NoBoard)")));
        QTRY_VERIFY(hasText(env->window, QStringLiteral("Website")));
        QTRY_VERIFY(hasText(env->window, QStringLiteral("Team board")));
        QTRY_VERIFY(hasText(env->window, QStringLiteral("Do + Oversee")));
        QTRY_VERIFY(hasText(env->window, QStringLiteral("Working hours")));
        QTRY_VERIFY(hasText(env->window, QStringLiteral("Agents")));
        // Changing a setting on the screen changes it for real.
        click(env->window, findItem(page, QStringLiteral("mode-2-hidden")));
        bool hidden = false;
        for (const auto &s : env->agenda->state().sources) {
            if (s.type == SourceEntry::Type::Project && s.key == QLatin1String("2")) {
                hidden = s.mode == QLatin1String("hidden");
            }
        }
        QVERIFY(hidden);
        // The working hours can be changed in half hours.
        QCOMPARE(env->controller->hours().value(QStringLiteral("from")).toString(), QStringLiteral("09:00"));
        click(env->window, findItem(page, QStringLiteral("hoursFromLater")));
        QCOMPARE(env->controller->hours().value(QStringLiteral("from")).toString(), QStringLiteral("09:30"));
        QVERIFY(env->controller->hours().value(QStringLiteral("overridden")).toBool());
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
        QTRY_VERIFY(hasText(env->window, QStringLiteral("Which of your calendars are personal?")));
        QTRY_VERIFY(hasText(env->window, QStringLiteral("Personal events are only ever shown to you. The others count as Work.")));
        QTRY_VERIFY(hasText(env->window, QStringLiteral("Save")));
        QTRY_VERIFY(hasText(env->window, QStringLiteral("Skip")));
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
        QTRY_VERIFY(hasText(env->window, QStringLiteral("You're not linked to a person in NoGantt yet. Ask your project manager to link you.")));
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
        QTRY_VERIFY(hasText(env->window, QStringLiteral("can't be reached; showing what was saved at")));
        QTRY_VERIFY(hasText(env->window, QStringLiteral("Dentist"))); // the saved rows are still there
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
        QTRY_VERIFY(hasText(env->window, QStringLiteral("NoGantt can't be reached; showing what was saved at")));
        QVERIFY(!hasText(env->window, QStringLiteral("NoBoard can't be reached")));
        QVERIFY(findItem(env->window->contentItem(), QStringLiteral("row-gantt:task:101"))); // the old rows stay
    }

    void aRefusingAppOffersToBeOpenedInsteadOfAskingToSignIn()
    {
        const auto env = open(QStringLiteral("ali"), false);
        for (auto &a : env->transport->answers) {
            if (a.path.contains(QStringLiteral("/nc_gantt/"))) {
                a.status = 403;
            }
        }
        env->agenda->refresh(true);
        show(env.get());
        QTRY_VERIFY(hasText(env->window, QStringLiteral("NoGantt didn't accept NoDrive's sign-in on your server. Showing what was saved at")));
        QTRY_VERIFY(hasText(env->window, QStringLiteral("Open in NoGantt")));
        QVERIFY(!hasText(env->window, QStringLiteral("Sign in to NoDrive to see new changes.")));
        QVERIFY(!hasText(env->window, QStringLiteral("NoBoard didn't accept")));
        QVERIFY(findItem(env->window->contentItem(), QStringLiteral("row-gantt:task:101"))); // the old rows stay
        QUrl opened;
        connect(env->controller.get(), &AgendaController::openUrlRequested, this, [&opened](const QUrl &url) { opened = url; });
        env->controller->bannerAction(QStringLiteral("openGantt"));
        QCOMPARE(opened.toString(), QStringLiteral("https://drive.example.test/index.php/apps/nc_gantt/"));
    }

    void aProblemWithMyAgentsHasItsOwnGroupAndIsNotMyWork()
    {
        const auto env = open(QStringLiteral("ali"), false);
        bool group = false;
        bool problem = false;
        for (const auto &r : env->controller->rows()) {
            const auto m = r.toMap();
            group = group || (m.value(QStringLiteral("type")).toString() == QLatin1String("group") && m.value(QStringLiteral("text")).toString() == QLatin1String("Problems with your agents"));
            problem = problem || m.value(QStringLiteral("key")).toString() == QLatin1String("gantt:noperson:106:AG-0001");
        }
        QVERIFY(group);
        QVERIFY(problem);
        // The row is not at home in "No date" (it is not Ali's work): it stays out of the list even when that is open.
        env->controller->toggleNoDate();
        int occurrences = 0;
        for (const auto &r : env->controller->rows()) {
            occurrences += r.toMap().value(QStringLiteral("key")).toString() == QLatin1String("gantt:noperson:106:AG-0001") ? 1 : 0;
        }
        QCOMPARE(occurrences, 1);
    }

    void offlineWithoutACacheSaysSo()
    {
        const auto env = open(QStringLiteral("ali"), false, [](FakeTransport *t) { t->offline = true; });
        env->agenda->setOnline(false);
        show(env.get());
        QTRY_VERIFY(hasText(env->window, QStringLiteral("You're offline. NoAgenda will load when NoDrive is connected.")));
    }

    void aSignedOutAccountOffersToLogIn()
    {
        const auto env = open(QStringLiteral("ali"), false);
        env->agenda->setNeedsLogin(true);
        show(env.get());
        QTRY_VERIFY(hasText(env->window, QStringLiteral("Sign in to NoDrive to see new changes.")));
        QTRY_VERIFY(hasText(env->window, QStringLiteral("Log in")));
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
            QTRY_VERIFY(hasText(env->window, QStringLiteral("No agent list found in your NoDrive (NoAgenda/agent-registry.json). Everything shows as Self.")));
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

    // ---- the edit step: conflicts, workload and the editor ---------------------------------------------------------------

    void theDayShowsTheClashAndTheTooFullDay_data() { darkRows(); }

    void theDayShowsTheClashAndTheTooFullDay()
    {
        QFETCH(bool, dark);
        const auto env = open(QStringLiteral("ali"), dark);
        show(env.get());
        auto *content = env->window->contentItem();
        QTRY_VERIFY(shown(content, QStringLiteral("workloadBar")));
        QTRY_VERIFY(hasText(env->window, QStringLiteral("h planned /")));
        bool overbooked = false;
        for (const auto &r : env->controller->rows()) {
            const auto m = r.toMap();
            overbooked = overbooked || (m.value(QStringLiteral("type")).toString() == QLatin1String("workload") && m.value(QStringLiteral("overbooked")).toBool());
        }
        QVERIFY(overbooked);
        // The dentist and the client call are both at 10:00: both rows carry the clash mark.
        QTRY_VERIFY(countShown(content, QStringLiteral("badgeClash")) >= 2);
        shoot(env.get(), QStringLiteral("day-clash-overbooked"), dark);
        checkBackground(env.get(), dark);
    }

    void theConflictsFilterShowsOnlyRowsWithAConflict()
    {
        const auto env = open(QStringLiteral("ali"), false);
        show(env.get());
        auto *content = env->window->contentItem();
        QVERIFY(findItem(content, QStringLiteral("row-cal:personal/dentist.ics")));
        QVERIFY(findItem(content, QStringLiteral("row-deck:card:503")));
        env->controller->setConflictsOnly(true);
        QTRY_VERIFY(!findItem(content, QStringLiteral("row-deck:card:503")));
        QVERIFY(findItem(content, QStringLiteral("row-cal:personal/dentist.ics")));
        QVERIFY(findItem(content, QStringLiteral("row-cal:work/call.ics")));
        env->controller->setConflictsOnly(false);
    }

    void editingTheAgentOfATaskOffersSelfAndMyAgentsAndSendsTheNewList_data() { darkRows(); }

    void editingTheAgentOfATaskOffersSelfAndMyAgentsAndSendsTheNewList()
    {
        QFETCH(bool, dark);
        const auto env = open(QStringLiteral("ali"), dark, [](FakeTransport *t) { routeGanttAgentEdit(t); });
        show(env.get());
        auto *content = env->window->contentItem();
        selectAndWait(env.get(), QStringLiteral("gantt:task:111"));
        auto *picker = findItem(content, QStringLiteral("agentPicker"));
        QVERIFY(picker && picker->isVisible());
        QVERIFY(shown(content, QStringLiteral("agentChoice-SELF")));
        QVERIFY(shown(content, QStringLiteral("agentChoice-AG-0001")));
        QVERIFY(shown(content, QStringLiteral("agentChoice-AG-0003"))); // paused: shown greyed
        QVERIFY(!findItem(content, QStringLiteral("agentChoice-AG-0004"))); // retired: not listed
        QVERIFY(!findItem(content, QStringLiteral("agentChoice-AG-0002"))); // somebody else's agent
        QTRY_VERIFY(hasText(env->window, QStringLiteral("Paused")));
        shoot(env.get(), QStringLiteral("edit-agent-gantt"), dark);

        click(env->window, findItem(content, QStringLiteral("agentChoice-AG-0001")));
        QTRY_VERIFY(hasText(env->window, QStringLiteral("Saved.")));
        bool found = false;
        for (const auto &r : env->transport->requests) {
            if (r.verb == "PUT" && r.url.path() == QStringLiteral("/ocs/v2.php/apps/nc_gantt/api/v1/tasks/111")) {
                const auto resources = parseObject(r.body).value(QStringLiteral("resources")).toArray();
                QCOMPARE(resources.size(), 2);
                QCOMPARE(resources.at(0).toObject().value(QStringLiteral("resource_id")).toInt(), 1);
                QCOMPARE(resources.at(0).toObject().value(QStringLiteral("work")).toDouble(), 6.0); // mine is untouched
                QCOMPARE(resources.at(1).toObject().value(QStringLiteral("resource_id")).toInt(), 11);
                found = true;
            }
        }
        QVERIFY(found);
        QCOMPARE(env->agenda->edit()->queue().pendingCount(), 0);
    }

    void anAgentThatIsNotInTheProjectYetAsksTheProjectManager_data() { darkRows(); }

    void anAgentThatIsNotInTheProjectYetAsksTheProjectManager()
    {
        QFETCH(bool, dark);
        const auto env = open(QStringLiteral("ali"), dark, [](FakeTransport *t) {
            QJsonArray visible;
            for (const auto &r : fixtureJson(QStringLiteral("gantt/resources.json")).toArray()) {
                if (r.toObject().value(QStringLiteral("id")).toInt() != 11) {
                    visible.append(r);
                }
            }
            prepend(t, "GET", QStringLiteral("/ocs/v2.php/apps/nc_gantt/api/v1/resources"), 200, ocsBody(visible));
        });
        show(env.get());
        auto *content = env->window->contentItem();
        selectAndWait(env.get(), QStringLiteral("gantt:task:111"));
        QTRY_VERIFY(shown(content, QStringLiteral("agentPicker")));
        QTRY_VERIFY(hasText(env->window, QStringLiteral("Ask your project manager to add")));
        QVERIFY(hasText(env->window, QStringLiteral("Writer-Bot")));
        QVERIFY(shown(content, QStringLiteral("openInButton")));
        QVERIFY(hasText(env->window, QStringLiteral("Open in NoGantt")));
        shoot(env.get(), QStringLiteral("edit-agent-not-in-project"), dark);
        // Choosing it sends nothing.
        env->transport->requests.clear();
        click(env->window, findItem(content, QStringLiteral("agentChoice-AG-0001")));
        QTest::qWait(100);
        for (const auto &r : env->transport->requests) {
            QVERIFY2(r.verb != "PUT", "nothing may be sent for an agent that is not in the project");
        }
    }

    void aNewEventHasSelfPreselectedAndNeedsAnAgent_data() { darkRows(); }

    void aNewEventHasSelfPreselectedAndNeedsAnAgent()
    {
        QFETCH(bool, dark);
        const auto env = open(QStringLiteral("ali"), dark);
        show(env.get());
        auto *content = env->window->contentItem();
        click(env->window, findItem(content, QStringLiteral("newButton")));
        QVERIFY(shown(content, QStringLiteral("newMenu")));
        click(env->window, findItem(content, QStringLiteral("newEventItem")));
        QTRY_VERIFY(shown(content, QStringLiteral("newItemDialog")));
        QVERIFY(hasText(env->window, QStringLiteral("New event")));
        QVERIFY(hasText(env->window, QStringLiteral("Agent")));
        QVERIFY(shown(content, QStringLiteral("newAgentPicker")));
        QVERIFY(shown(content, QStringLiteral("agentChoice-SELF")));
        QVERIFY(shown(content, QStringLiteral("agentChoice-AG-0001")));
        QVERIFY(hasText(env->window, QStringLiteral("Self")));
        // The date of the day in view is already in the form.
        auto *startDate = findItem(content, QStringLiteral("fieldStartDate"));
        QVERIFY(startDate);
        QCOMPARE(startDate->property("text").toString(), QStringLiteral("2026-10-28"));
        findItem(content, QStringLiteral("fieldTitle"))->setProperty("text", QStringLiteral("Planning with Sara"));
        QTest::qWait(60);
        shoot(env.get(), QStringLiteral("new-event"), dark);

        // Self is preselected: the new event has neither an agent id nor an agent tag.
        click(env->window, findItem(content, QStringLiteral("newSaveButton")));
        QTest::qWait(100);
        const auto puts = env->transport->of("PUT");
        QCOMPARE(puts.size(), 1);
        QVERIFY(puts.first().url.path().startsWith(QStringLiteral("/remote.php/dav/calendars/ali/")));
        QVERIFY(puts.first().url.path().endsWith(QStringLiteral(".ics")));
        QCOMPARE(ScriptedTransport::header(puts.first(), "If-None-Match"), QByteArrayLiteral("*"));
        const QString body = QString::fromUtf8(puts.first().body);
        QVERIFY(body.contains(QStringLiteral("SUMMARY:Planning with Sara")));
        QVERIFY(body.contains(QStringLiteral("DTSTART:20261028T090000Z")));
        QVERIFY(!body.contains(QStringLiteral("X-NOURAX-AGENT-ID")));
        QVERIFY(!body.contains(QStringLiteral("CATEGORIES")));
    }

    void aNewItemNeedsATitleAndAnAgent()
    {
        const auto env = open(QStringLiteral("ali"), false);
        auto *edit = env->controller->editController();
        QVariantMap values{{QStringLiteral("kind"), QStringLiteral("event")}, {QStringLiteral("calendarUri"), QStringLiteral("work")}, {QStringLiteral("title"), QStringLiteral("x")},
            {QStringLiteral("startDate"), QStringLiteral("2026-10-29")}, {QStringLiteral("startTime"), QStringLiteral("09:00")}, {QStringLiteral("endDate"), QStringLiteral("2026-10-29")}, {QStringLiteral("endTime"), QStringLiteral("10:00")}};
        QCOMPARE(edit->create(values), QStringLiteral("Choose who does it: Self or one of your agents."));
        values.insert(QStringLiteral("agentId"), QStringLiteral("SELF"));
        values.insert(QStringLiteral("title"), QStringLiteral("  "));
        QCOMPARE(edit->create(values), QStringLiteral("Give it a title."));
        values.insert(QStringLiteral("title"), QStringLiteral("x"));
        values.insert(QStringLiteral("endTime"), QStringLiteral("08:00"));
        QCOMPARE(edit->create(values), QStringLiteral("The end can't be before the start."));
        values.insert(QStringLiteral("endTime"), QStringLiteral("10:00"));
        values.insert(QStringLiteral("startDate"), QStringLiteral("29/10/2026"));
        QVERIFY(edit->create(values).contains(QStringLiteral("2026-11-04")));
        // Nobody else's agent, and not a paused one.
        values.insert(QStringLiteral("startDate"), QStringLiteral("2026-10-29"));
        values.insert(QStringLiteral("agentId"), QStringLiteral("AG-0002"));
        QVERIFY(!edit->create(values).isEmpty());
        values.insert(QStringLiteral("agentId"), QStringLiteral("AG-0003"));
        QVERIFY(!edit->create(values).isEmpty());
        QCOMPARE(env->transport->count("PUT"), 0);
        // A new to-do with one of my agents writes the id and the tag.
        QVariantMap todo{{QStringLiteral("kind"), QStringLiteral("todo")}, {QStringLiteral("calendarUri"), QStringLiteral("personal")}, {QStringLiteral("title"), QStringLiteral("Call the bank")},
            {QStringLiteral("dueDate"), QStringLiteral("2026-10-30")}, {QStringLiteral("agentId"), QStringLiteral("AG-0001")}};
        QCOMPARE(edit->create(todo), QString());
        QTest::qWait(60);
        const auto puts = env->transport->of("PUT");
        QCOMPARE(puts.size(), 1);
        const QString body = QString::fromUtf8(puts.first().body);
        QVERIFY(body.contains(QStringLiteral("BEGIN:VTODO")));
        QVERIFY(body.contains(QStringLiteral("DUE;VALUE=DATE:20261030")));
        QVERIFY(body.contains(QStringLiteral("X-NOURAX-AGENT-ID:AG-0001")));
        QVERIFY(body.contains(QStringLiteral("CATEGORIES:agent:AG-0001")));
    }

    void anEditThatCollidesIsKeptAsNotSentAndCanBeAppliedAgain_data() { darkRows(); }

    void anEditThatCollidesIsKeptAsNotSentAndCanBeAppliedAgain()
    {
        QFETCH(bool, dark);
        const QString path = QStringLiteral("/remote.php/dav/calendars/ali/personal/dentist.ics");
        const auto env = open(QStringLiteral("ali"), dark, [&](FakeTransport *t) {
            // the dentist's appointment was changed in the web page meanwhile: another ETag
            prepend(t, "GET", path, 200, ics(QStringLiteral("ci-dentist"), QStringLiteral("Dentist at 10")), {{QByteArrayLiteral("etag"), QByteArrayLiteral("\"e9\"")}});
        });
        show(env.get());
        auto *content = env->window->contentItem();
        selectAndWait(env.get(), QStringLiteral("cal:personal/dentist.ics"));
        click(env->window, findItem(content, QStringLiteral("editButton")));
        QVERIFY(shown(content, QStringLiteral("editor")));
        findItem(content, QStringLiteral("fieldTitle"))->setProperty("text", QStringLiteral("Dentist (moved)"));
        click(env->window, findItem(content, QStringLiteral("saveButton")));
        auto *panel = findItem(content, QStringLiteral("notSentPanel"));
        QTRY_VERIFY(panel && panel->isVisible());
        QTRY_VERIFY(hasText(env->window, QStringLiteral("Not sent")));
        QTRY_VERIFY(hasText(env->window, QStringLiteral("This changed somewhere else while you were editing. NoAgenda kept the other change.")));
        QVERIFY(hasText(env->window, QStringLiteral("Apply my change again")));
        QVERIFY(hasText(env->window, QStringLiteral("Keep the other change")));
        QCOMPARE(env->transport->count("PUT"), 0); // the other change wins: nothing was written
        QCOMPARE(env->agenda->edit()->queue().conflicts().size(), 1);
        shoot(env.get(), QStringLiteral("edit-conflict"), dark);

        // "Apply my change again": one fresh read and one write with the new ETag.
        prepend(env->transport, "PUT", path, 204, QByteArray(), {{QByteArrayLiteral("etag"), QByteArrayLiteral("\"e10\"")}});
        env->transport->requests.clear();
        click(env->window, findItem(content, QStringLiteral("applyAgainButton")));
        QTRY_VERIFY(!panel->isVisible());
        const auto puts = env->transport->of("PUT");
        QCOMPARE(puts.size(), 1);
        QCOMPARE(ScriptedTransport::header(puts.first(), "If-Match"), QByteArrayLiteral("\"e9\""));
        QVERIFY(QString::fromUtf8(puts.first().body).contains(QStringLiteral("SUMMARY:Dentist (moved)")));
        QCOMPARE(env->agenda->edit()->queue().conflicts().size(), 0);
    }

    void keepingTheOtherChangeDropsTheEdit()
    {
        const QString path = QStringLiteral("/remote.php/dav/calendars/ali/personal/dentist.ics");
        const auto env = open(QStringLiteral("ali"), false, [&](FakeTransport *t) {
            prepend(t, "GET", path, 200, ics(QStringLiteral("ci-dentist"), QStringLiteral("Dentist at 10")), {{QByteArrayLiteral("etag"), QByteArrayLiteral("\"e9\"")}});
        });
        show(env.get());
        auto *content = env->window->contentItem();
        selectAndWait(env.get(), QStringLiteral("cal:personal/dentist.ics"));
        click(env->window, findItem(content, QStringLiteral("editButton")));
        findItem(content, QStringLiteral("fieldTitle"))->setProperty("text", QStringLiteral("Mine"));
        click(env->window, findItem(content, QStringLiteral("saveButton")));
        auto *panel = findItem(content, QStringLiteral("notSentPanel"));
        QTRY_VERIFY(panel && panel->isVisible());
        click(env->window, findItem(content, QStringLiteral("keepOtherButton")));
        QTRY_VERIFY(!panel->isVisible());
        QCOMPARE(env->transport->count("PUT"), 0);
        QCOMPARE(env->agenda->edit()->queue().conflicts().size(), 0);
    }

    void aChangeMadeOfflineWaitsAndShowsTheClock_data() { darkRows(); }

    void aChangeMadeOfflineWaitsAndShowsTheClock()
    {
        QFETCH(bool, dark);
        const auto env = open(QStringLiteral("ali"), dark);
        show(env.get());
        auto *content = env->window->contentItem();
        selectAndWait(env.get(), QStringLiteral("cal:personal/dentist.ics"));
        click(env->window, findItem(content, QStringLiteral("editButton")));
        findItem(content, QStringLiteral("fieldTitle"))->setProperty("text", QStringLiteral("Dentist (new time)"));
        env->transport->offline = true; // the network goes away; NoDrive itself still thinks it is connected
        click(env->window, findItem(content, QStringLiteral("saveButton")));
        QTRY_VERIFY(hasText(env->window, QStringLiteral("Waiting to send 1 change")));
        QTRY_VERIFY(shown(content, QStringLiteral("badgePending")));
        QTRY_VERIFY(hasText(env->window, QStringLiteral("Dentist (new time)"))); // drawn at once
        QCOMPARE(env->agenda->edit()->queue().pendingCount(), 1);
        QVERIFY(env->agenda->edit()->queue().conflicts().isEmpty());
        shoot(env.get(), QStringLiteral("pending-offline"), dark);
    }

    void deletingNeedsTheConfirmationAndMovesToTheTrashBin_data() { darkRows(); }

    void deletingNeedsTheConfirmationAndMovesToTheTrashBin()
    {
        QFETCH(bool, dark);
        const QString path = QStringLiteral("/remote.php/dav/calendars/ali/personal/dentist.ics");
        const auto env = open(QStringLiteral("ali"), dark, [&](FakeTransport *t) { prepend(t, "DELETE", path, 204, QByteArray()); });
        show(env.get());
        auto *content = env->window->contentItem();
        selectAndWait(env.get(), QStringLiteral("cal:personal/dentist.ics"));
        click(env->window, findItem(content, QStringLiteral("deleteButton")));
        QTRY_VERIFY(shown(content, QStringLiteral("confirmDeleteDialog")));
        QVERIFY(hasText(env->window, QStringLiteral("Delete this event? It moves to the calendar's trash bin.")));
        shoot(env.get(), QStringLiteral("delete-confirm"), dark);
        // Cancel: nothing is sent.
        click(env->window, findItem(content, QStringLiteral("cancelDeleteButton")));
        QVERIFY(!shown(content, QStringLiteral("confirmDeleteDialog")));
        QCOMPARE(env->transport->count("DELETE"), 0);
        // Delete: one DELETE with If-Match.
        click(env->window, findItem(content, QStringLiteral("deleteButton")));
        click(env->window, findItem(content, QStringLiteral("confirmDeleteButton")));
        QTRY_COMPARE(env->transport->count("DELETE"), 1);
        QCOMPARE(ScriptedTransport::header(env->transport->of("DELETE").first(), "If-Match"), QByteArrayLiteral("\"e1\""));
        QTRY_VERIFY(!findItem(content, QStringLiteral("row-cal:personal/dentist.ics")));
    }

    void aRepeatingEventOnlyOffersTheCalendarPage_data() { darkRows(); }

    void aRepeatingEventOnlyOffersTheCalendarPage()
    {
        QFETCH(bool, dark);
        const auto env = open(QStringLiteral("ali"), dark);
        show(env.get());
        auto *content = env->window->contentItem();
        selectAndWait(env.get(), QStringLiteral("cal:work/standup.ics#20261028T093000Z"));
        QTRY_VERIFY(hasText(env->window, QStringLiteral("This repeats. Change it in NoCalendar.")));
        QVERIFY(!shown(content, QStringLiteral("editButton")));
        QVERIFY(!shown(content, QStringLiteral("deleteButton")));
        QVERIFY(!shown(content, QStringLiteral("agentPicker")));
        QVERIFY(shown(content, QStringLiteral("openInButton")));
        QVERIFY(hasText(env->window, QStringLiteral("Open in NoCalendar")));
        shoot(env.get(), QStringLiteral("repeating-locked"), dark);
        QCOMPARE(env->transport->count("PUT"), 0);
    }

    void aProjectIViewOnlyIsLockedWithAPlainReason_data() { darkRows(); }

    void aProjectIViewOnlyIsLockedWithAPlainReason()
    {
        QFETCH(bool, dark);
        const auto env = open(QStringLiteral("ali"), dark, [](FakeTransport *t) {
            auto projects = fixtureJson(QStringLiteral("gantt/projects-ali.json")).toArray();
            auto first = projects.at(0).toObject();
            first.insert(QStringLiteral("permission"), QStringLiteral("viewer"));
            projects.replace(0, first);
            prepend(t, "GET", QStringLiteral("/ocs/v2.php/apps/nc_gantt/api/v1/projects"), 200, ocsBody(projects));
        });
        show(env.get());
        auto *content = env->window->contentItem();
        selectAndWait(env.get(), QStringLiteral("gantt:task:111"));
        QTRY_VERIFY(hasText(env->window, QStringLiteral("You can view this project only. Ask the project manager for editor rights.")));
        QVERIFY(!shown(content, QStringLiteral("editButton")));
        QVERIFY(!shown(content, QStringLiteral("doneButton")));
        QVERIFY(shown(content, QStringLiteral("openInButton")));
        shoot(env.get(), QStringLiteral("viewer-locked"), dark);
    }

    void aCardIsNeverDeletedAndOffersItsDueDate()
    {
        const auto env = open(QStringLiteral("ali"), false);
        show(env.get());
        auto *content = env->window->contentItem();
        selectAndWait(env.get(), QStringLiteral("deck:card:503"));
        QVERIFY(shown(content, QStringLiteral("editButton")));
        QVERIFY(shown(content, QStringLiteral("doneButton")));
        QVERIFY(!shown(content, QStringLiteral("deleteButton")));
        QVERIFY(hasText(env->window, QStringLiteral("Open in NoBoard")));
    }

    void noTextOfTheEditorSaysTheServerSoftwareName()
    {
        const auto env = open(QStringLiteral("ali"), false);
        show(env.get());
        auto *content = env->window->contentItem();
        for (const auto &key : {QStringLiteral("cal:personal/dentist.ics"), QStringLiteral("gantt:task:111"), QStringLiteral("deck:card:503")}) {
            selectAndWait(env.get(), key);
            if (auto *edit = findItem(content, QStringLiteral("editButton")); edit && edit->isVisible()) {
                click(env->window, edit);
            }
            QVERIFY2(!allText(env->window).contains(QStringLiteral("nextcloud"), Qt::CaseInsensitive), qPrintable(key));
        }
        click(env->window, findItem(content, QStringLiteral("newButton")));
        click(env->window, findItem(content, QStringLiteral("newEventItem")));
        QTRY_VERIFY(shown(content, QStringLiteral("newItemDialog")));
        QVERIFY(!allText(env->window).contains(QStringLiteral("nextcloud"), Qt::CaseInsensitive));
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
    // The screenshots look the same everywhere: 24 hour clock and English names.
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedKingdom));
    TestNoDriveAgendaUi test;
    return QTest::qExec(&test, argc, argv);
}

#include "testnodriveagendaui.moc"
