/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * NoDrive problem reports: the scrubber removes secrets and keeps file names.
 * Fixtures: test/nodrive/problemreports/<name>.txt with <name>.expected.txt (all values are made up).
 */

#include "gui/nourax/problemreportscrubber.h"

#include <QDir>
#include <QFile>
#include <QRandomGenerator>
#include <QTest>

using OCC::Nourax::ProblemReportScrubber;

namespace {

QString fixtureDir()
{
    return QString::fromUtf8(NODRIVE_FIXTURE_DIR);
}

QString readText(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(file.readAll()).replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
}

QStringList secrets()
{
    return readText(fixtureDir() + QStringLiteral("/secrets.txt")).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

QString scrubFixture(const QString &name)
{
    const auto input = readText(fixtureDir() + QLatin1Char('/') + name + QStringLiteral(".txt"));
    if (name.startsWith(QLatin1String("cfg-"))) {
        return QString::fromUtf8(ProblemReportScrubber::scrubConfig(input.toUtf8()));
    }
    return ProblemReportScrubber::scrubText(input);
}

// A random string of letters and digits that has an upper-case letter, a lower-case letter and a digit.
QString randomAlnum(QRandomGenerator &rng, int length)
{
    static const QString upper = QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
    static const QString lower = QStringLiteral("abcdefghijklmnopqrstuvwxyz");
    static const QString digits = QStringLiteral("0123456789");
    const QString all = upper + lower + digits;
    QString s;
    s.reserve(length);
    s += upper.at(int(rng.bounded(upper.size())));
    s += lower.at(int(rng.bounded(lower.size())));
    s += digits.at(int(rng.bounded(digits.size())));
    while (s.size() < length) {
        s += all.at(int(rng.bounded(all.size())));
    }
    // shuffle
    for (int i = s.size() - 1; i > 0; --i) {
        const auto j = int(rng.bounded(i + 1));
        const auto tmp = s.at(i);
        s[i] = s.at(j);
        s[j] = tmp;
    }
    return s;
}

QString randomWord(QRandomGenerator &rng, int length)
{
    QString s;
    for (int i = 0; i < length; ++i) {
        s += QChar('a' + int(rng.bounded(26)));
    }
    return s;
}

} // namespace

class TestNoDriveProblemReportScrubber : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void fixtures_data()
    {
        QTest::addColumn<QString>("name");
        const QDir dir(fixtureDir());
        const auto files = dir.entryList(QStringList{ QStringLiteral("*.expected.txt") }, QDir::Files, QDir::Name);
        QVERIFY(files.size() >= 10);
        for (const auto &file : files) {
            auto name = file;
            name.chop(QStringLiteral(".expected.txt").size());
            QTest::newRow(qPrintable(name)) << name;
        }
    }

    void fixtures()
    {
        QFETCH(QString, name);
        const auto expected = readText(fixtureDir() + QLatin1Char('/') + name + QStringLiteral(".expected.txt"));
        QVERIFY(!expected.isEmpty());
        QCOMPARE(scrubFixture(name), expected);
    }

    void noSecretSurvivesInAnyFixture()
    {
        const auto planted = secrets();
        QVERIFY(planted.size() > 20);
        const QDir dir(fixtureDir());
        const auto files = dir.entryList(QStringList{ QStringLiteral("*.txt") }, QDir::Files, QDir::Name);
        int checked = 0;
        for (const auto &file : files) {
            if (file == QLatin1String("secrets.txt") || file.endsWith(QLatin1String(".expected.txt"))) {
                continue;
            }
            auto name = file;
            name.chop(4);
            const auto output = scrubFixture(name);
            for (const auto &secret : planted) {
                QVERIFY2(!output.contains(secret), qPrintable(QStringLiteral("%1 leaks %2").arg(name, secret)));
            }
            ++checked;
        }
        QVERIFY(checked >= 10);
    }

    void fileNamesAndPlainFactsAreKept()
    {
        const auto text = QStringLiteral("Uploading /Photos/Holiday 2026/IMG_0001.jpg (etag \"7d6c5f0a1b2c3d4e5f60718293a4b5c6\")\n");
        QCOMPARE(ProblemReportScrubber::scrubText(text), text);
        const auto keep = scrubFixture(QStringLiteral("09-keep"));
        QVERIFY(keep.contains(QStringLiteral("IMG_0001.jpg")));
        QVERIFY(keep.contains(QStringLiteral("Company_Documents_2026_Quarter_Three_Final")));
        QVERIFY(keep.contains(QStringLiteral("da39a3ee5e6b4b0d3255bfef95601890afd80709")));
    }

    void scrubbingTwiceChangesNothing()
    {
        const QDir dir(fixtureDir());
        const auto files = dir.entryList(QStringList{ QStringLiteral("*.expected.txt") }, QDir::Files, QDir::Name);
        for (const auto &file : files) {
            if (file.startsWith(QLatin1String("cfg-"))) {
                continue;
            }
            const auto once = readText(fixtureDir() + QLatin1Char('/') + file);
            QCOMPARE(ProblemReportScrubber::scrubText(once), once);
        }
    }

    void arguments()
    {
        const QStringList args{
            QStringLiteral("/usr/bin/nodrive"), QStringLiteral("--apppassword"), QStringLiteral("aB3dE-fG5hI-jK7lM-nO9pQ-rS1tU"),
            QStringLiteral("--userid"), QStringLiteral("jane.doe@example.com"), QStringLiteral("--serverurl"),
            QStringLiteral("https://drive.example.org")
        };
        const auto out = ProblemReportScrubber::scrubArguments(args);
        QVERIFY(!out.contains(QStringLiteral("aB3dE")));
        QVERIFY(!out.contains(QStringLiteral("jane.doe@example.com")));
        QVERIFY(out.contains(QStringLiteral("--apppassword <removed>")));
        QVERIFY(out.contains(QStringLiteral("<email>")));
        QVERIFY(out.contains(QStringLiteral("https://drive.example.org")));

        const auto joined = ProblemReportScrubber::scrubArguments({ QStringLiteral("nodrive"), QStringLiteral("--apppassword=SecretValue123") });
        QVERIFY(!joined.contains(QStringLiteral("SecretValue123")));
    }

    void configKeepsPlainSettings()
    {
        const auto out = QString::fromUtf8(ProblemReportScrubber::scrubConfig(
            "[General]\nconfirmExternalStorage=true\n[Proxy]\npass=hunter22\n"));
        QVERIFY(out.contains(QStringLiteral("confirmExternalStorage=true")));
        QVERIFY(out.contains(QStringLiteral("pass=<removed>")));
        QVERIFY(!out.contains(QStringLiteral("hunter22")));
    }

    // 200 random secrets of each kind, none may survive.
    void randomSecretsNeverSurvive()
    {
        QRandomGenerator rng(20261028);
        for (int i = 0; i < 200; ++i) {
            const auto appPassword = randomAlnum(rng, 72);
            const QString group = randomAlnum(rng, 5) + QLatin1Char('-') + randomAlnum(rng, 5) + QLatin1Char('-') + randomAlnum(rng, 5)
                + QLatin1Char('-') + randomAlnum(rng, 5) + QLatin1Char('-') + randomAlnum(rng, 5);
            const auto bearer = randomAlnum(rng, 40);
            const auto basic = randomAlnum(rng, 24);
            const auto cookie = randomAlnum(rng, 32);
            const auto query = randomAlnum(rng, 20);
            const auto longBare = randomAlnum(rng, 48);
            const auto flowToken = randomAlnum(rng, 128);
            const QString email = randomWord(rng, 6 + int(rng.bounded(6))) + QLatin1Char('.') + randomWord(rng, 5) + QStringLiteral("@")
                + randomWord(rng, 5 + int(rng.bounded(5))) + QStringLiteral(".example.com");
            const QString words = randomWord(rng, 6) + QLatin1Char(' ') + randomWord(rng, 7) + QLatin1Char(' ') + randomWord(rng, 5);

            const QList<QPair<QString, QString>> cases{
                { QStringLiteral("{\"appPassword\":\"%1\"}").arg(appPassword), appPassword },
                { QStringLiteral("New app password %1 created").arg(group), group },
                { QStringLiteral("Authorization: Bearer %1").arg(bearer), bearer },
                { QStringLiteral("Header: { Authorization: Basic %1, Accept: */*, }").arg(basic), basic },
                { QStringLiteral("Cookie: oc_sessionPassphrase=%1; other=1").arg(cookie), cookie },
                { QStringLiteral("Set-Cookie: nc_session_id=%1; path=/; HttpOnly").arg(cookie), cookie },
                { QStringLiteral("GET https://host.example.org/x?token=%1&fileid=7").arg(query), query },
                { QStringLiteral("GET https://host.example.org/x?code=%1&state=ok").arg(query), query },
                { QStringLiteral("session value %1 stored").arg(longBare), longBare },
                { QStringLiteral("nodrive --apppassword %1").arg(appPassword), appPassword },
                { QStringLiteral("login https://host.example.org/login/v2/flow/%1").arg(flowToken), flowToken },
                { QStringLiteral("Account of %1 added").arg(email), email },
                { QStringLiteral("/remote.php/dav/files/%1/Documents").arg(email), email },
                { QStringLiteral("{\"mnemonic\":\"%1\"}").arg(words), words },
            };
            for (const auto &c : cases) {
                const auto out = ProblemReportScrubber::scrubText(c.first);
                QVERIFY2(!out.contains(c.second), qPrintable(QStringLiteral("leak in '%1' -> '%2'").arg(c.first, out)));
            }
        }
    }
};

QTEST_APPLESS_MAIN(TestNoDriveProblemReportScrubber)
#include "testnodriveproblemreportscrubber.moc"
