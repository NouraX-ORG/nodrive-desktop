/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "problemreportscrubber.h"

#include <QRegularExpression>

#include <functional>

namespace OCC::Nourax {

namespace {

using Re = QRegularExpression;
using MatchFn = std::function<QString(const QRegularExpressionMatch &)>;

QString replaceAll(const QString &in, const Re &re, const MatchFn &fn)
{
    QString out;
    out.reserve(in.size());
    qsizetype last = 0;
    auto it = re.globalMatch(in);
    while (it.hasNext()) {
        const auto m = it.next();
        out += QStringView(in).mid(last, m.capturedStart() - last);
        out += fn(m);
        last = m.capturedEnd();
    }
    out += QStringView(in).mid(last);
    return out;
}

QString removedMarker()
{
    return QStringLiteral("<removed>");
}

// 1. PEM private keys (also when a log printed them as one JSON line with "\n" inside).
const Re &pemFull()
{
    static const Re re(QStringLiteral("-----BEGIN [A-Z0-9 ]*PRIVATE KEY( BLOCK)?-----.*?-----END [A-Z0-9 ]*PRIVATE KEY( BLOCK)?-----"),
        Re::DotMatchesEverythingOption);
    return re;
}

// A PEM header whose end was cut off by a log limit: remove the header and the key text after it.
const Re &pemHeaderOnly()
{
    static const Re re(QStringLiteral("-----BEGIN [A-Z0-9 ]*PRIVATE KEY( BLOCK)?-----(?:\\\\[nr]|[A-Za-z0-9+/=])*"));
    return re;
}

// 2/7. Secret JSON fields or key=value pairs.
const Re &secretFields()
{
    static const Re re(QStringLiteral(
        "(?<![A-Za-z0-9_])([\"']?)"
        "(privateKey|private_key|mnemonic|metadataKey|metadata_key|encryptedMetadataKey|sessionPassphrase|passphrase|secret"
        "|clientSecret|client_secret|password|passwd|appPassword|app_password|apppassword|token|accessToken|access_token"
        "|refreshToken|refresh_token|api_key|apikey)"
        "\\1(\\s*[:=]\\s*)(\"(?:[^\"\\\\]|\\\\.)*\"|'[^']*'|[^\\s,;&\"'}\\]<]+)"),
        Re::CaseInsensitiveOption);
    return re;
}

// 3/5. Authorization, Cookie and Set-Cookie values, up to the next ", Header-Name: ", "}" or the end of the line.
const Re &headerValues()
{
    static const Re re(QStringLiteral(
        "\\b((?:Proxy-)?Authorization|Set-Cookie|Cookie)\\b(\"?\\s*[:=]\\s*\"?)"
        "((?:(?!, [A-Za-z][A-Za-z0-9-]*: )[^\\r\\n}\"'])*)"),
        Re::CaseInsensitiveOption);
    return re;
}

// 4. Basic and Bearer values.
const Re &basicBearer()
{
    static const Re re(QStringLiteral("\\b(Basic|Bearer)\\s+([A-Za-z0-9._~+/=-]{8,})"), Re::CaseInsensitiveOption);
    return re;
}

// 5. Session and instance cookies by name, wherever they appear.
const Re &cookieNames()
{
    static const Re re(QStringLiteral(
        "(?<![A-Za-z0-9_])((?:oc_sessionPassphrase|nc_session_id|nc_username|nc_token|nc_sameSiteCookie[A-Za-z]*"
        "|__Host-[A-Za-z0-9_.-]+|__Secure-[A-Za-z0-9_.-]+|oc[a-z0-9]{10,14}))=([^;\\s,\"'<]+)"));
    return re;
}

// 6. URL query (and form body) values.
const Re &queryValues()
{
    static const Re re(QStringLiteral(
        "((?:(?<=[?&\\[(\"'])|^)(?:token|password|pass|secret|key|sig|code|state))=([^&\\s\"'<>#]+)"),
        Re::CaseInsensitiveOption);
    return re;
}

// 6b. user:password@ inside a URL.
const Re &urlUserInfo()
{
    static const Re re(QStringLiteral("(://)[^/\\s:@\"',]+:[^/\\s@\"',]+@"));
    return re;
}

// 8. The command line.
const Re &appPasswordArgument()
{
    static const Re re(QStringLiteral("(--apppassword[\"']?(?:\\s*[=,]\\s*|\\s+))(\"[^\"]*\"|'[^']*'|[^\\s,)]+)"),
        Re::CaseInsensitiveOption);
    return re;
}

const Re &jwt()
{
    static const Re re(QStringLiteral("eyJ[A-Za-z0-9_-]{8,}\\.[A-Za-z0-9_-]{8,}\\.[A-Za-z0-9_-]*"));
    return re;
}

// 10. Email addresses, also as %40 in URLs and as user ids inside DAV paths.
const Re &email()
{
    static const Re re(QStringLiteral("[A-Za-z0-9._%+-]+(?:@|%40)[A-Za-z0-9.-]+\\.[A-Za-z]{2,}"));
    return re;
}

// 9. Long secrets: 32 or more letters and digits with an upper-case letter, a lower-case letter and a digit.
const Re &longRun()
{
    static const Re re(QStringLiteral("(?<![A-Za-z0-9])[A-Za-z0-9]{32,}(?![A-Za-z0-9])"));
    return re;
}

const Re &groupedSecret()
{
    static const Re re(QStringLiteral("(?<![A-Za-z0-9-])[A-Za-z0-9]{5}(?:-[A-Za-z0-9]{5}){4}(?![A-Za-z0-9-])"));
    return re;
}

bool hasMixedClasses(const QString &s)
{
    bool upper = false;
    bool lower = false;
    bool digit = false;
    for (const auto c : s) {
        const auto u = c.unicode();
        upper = upper || (u >= 'A' && u <= 'Z');
        lower = lower || (u >= 'a' && u <= 'z');
        digit = digit || (u >= '0' && u <= '9');
    }
    return upper && lower && digit;
}

// "Basic configuration" is prose, "Basic YWRtaW46cGFzc3dvcmQ=" is a credential.
bool looksLikeBasicCredential(const QString &token)
{
    if (token.size() >= 16) {
        return true;
    }
    bool upper = false;
    bool lower = false;
    for (const auto c : token) {
        const auto u = c.unicode();
        if ((u >= '0' && u <= '9') || u == '=' || u == '+' || u == '/') {
            return true;
        }
        upper = upper || (u >= 'A' && u <= 'Z');
        lower = lower || (u >= 'a' && u <= 'z');
    }
    return upper && lower;
}

bool configKeyIsSecret(const QString &key)
{
    const auto k = key.toLower();
    static const QStringList parts{
        QStringLiteral("pass"), QStringLiteral("token"), QStringLiteral("secret"),
        QStringLiteral("privatekey"), QStringLiteral("mnemonic"), QStringLiteral("cookie")
    };
    for (const auto &p : parts) {
        if (k.contains(p)) {
            return true;
        }
    }
    return false;
}

} // namespace

QString ProblemReportScrubber::scrubLine(const QString &line)
{
    QString s = line;
    const bool hasEquals = s.contains(QLatin1Char('='));
    const bool hasColon = s.contains(QLatin1Char(':'));

    if (s.contains(QStringLiteral("uthorization"), Qt::CaseInsensitive) || s.contains(QStringLiteral("ookie"), Qt::CaseInsensitive)) {
        s = replaceAll(s, headerValues(), [](const QRegularExpressionMatch &m) -> QString {
            return m.captured(1) + m.captured(2) + removedMarker();
        });
    }
    if (hasColon || hasEquals) {
        s = replaceAll(s, secretFields(), [](const QRegularExpressionMatch &m) -> QString {
            const auto quote = m.captured(1);
            const auto value = m.captured(4);
            QString replacement = removedMarker();
            if (value.startsWith(QLatin1Char('"'))) {
                replacement = QStringLiteral("\"<removed>\"");
            } else if (value.startsWith(QLatin1Char('\''))) {
                replacement = QStringLiteral("'<removed>'");
            }
            return quote + m.captured(2) + quote + m.captured(3) + replacement;
        });
    }
    if (s.contains(QStringLiteral("asic"), Qt::CaseInsensitive) || s.contains(QStringLiteral("earer"), Qt::CaseInsensitive)) {
        s = replaceAll(s, basicBearer(), [](const QRegularExpressionMatch &m) -> QString {
            const auto scheme = m.captured(1);
            if (scheme.compare(QStringLiteral("basic"), Qt::CaseInsensitive) == 0 && !looksLikeBasicCredential(m.captured(2))) {
                return m.captured(0);
            }
            return QString(scheme + QLatin1Char(' ') + removedMarker());
        });
    }
    if (hasEquals) {
        s = replaceAll(s, cookieNames(), [](const QRegularExpressionMatch &m) -> QString {
            return m.captured(1) + QLatin1Char('=') + removedMarker();
        });
        s = replaceAll(s, queryValues(), [](const QRegularExpressionMatch &m) -> QString {
            return m.captured(1) + QLatin1Char('=') + removedMarker();
        });
    }
    if (s.contains(QStringLiteral("://"))) {
        s = replaceAll(s, urlUserInfo(), [](const QRegularExpressionMatch &m) -> QString {
            return m.captured(1) + removedMarker() + QLatin1Char('@');
        });
    }
    if (s.contains(QStringLiteral("apppassword"), Qt::CaseInsensitive)) {
        s = replaceAll(s, appPasswordArgument(), [](const QRegularExpressionMatch &m) -> QString {
            return m.captured(1) + removedMarker();
        });
    }
    if (s.contains(QStringLiteral("eyJ"))) {
        s = replaceAll(s, jwt(), [](const QRegularExpressionMatch &) -> QString {
            return QStringLiteral("<secret>");
        });
    }
    if (s.contains(QLatin1Char('@')) || s.contains(QStringLiteral("%40"))) {
        s = replaceAll(s, email(), [](const QRegularExpressionMatch &) -> QString {
            return QStringLiteral("<email>");
        });
    }
    if (s.size() >= 32) {
        s = replaceAll(s, longRun(), [](const QRegularExpressionMatch &m) -> QString {
            return hasMixedClasses(m.captured(0)) ? QStringLiteral("<secret>") : m.captured(0);
        });
    }
    if (s.size() >= 29 && s.contains(QLatin1Char('-'))) {
        s = replaceAll(s, groupedSecret(), [](const QRegularExpressionMatch &) -> QString {
            return QStringLiteral("<secret>");
        });
    }
    return s;
}

QString ProblemReportScrubber::scrubText(const QString &text)
{
    QString input = text;
    if (input.contains(QStringLiteral("PRIVATE KEY"))) {
        input = replaceAll(input, pemFull(), [](const QRegularExpressionMatch &) -> QString {
            return QStringLiteral("<private key removed>");
        });
        input = replaceAll(input, pemHeaderOnly(), [](const QRegularExpressionMatch &) -> QString {
            return QStringLiteral("<private key removed>");
        });
    }

    QString out;
    out.reserve(input.size());
    qsizetype pos = 0;
    while (pos < input.size()) {
        const auto end = input.indexOf(QLatin1Char('\n'), pos);
        const auto lineEnd = end < 0 ? input.size() : end;
        out += scrubLine(input.mid(pos, lineEnd - pos));
        if (end >= 0) {
            out += QLatin1Char('\n');
        }
        pos = lineEnd + 1;
    }
    return out;
}

QByteArray ProblemReportScrubber::scrubConfig(const QByteArray &ini)
{
    const auto text = QString::fromUtf8(ini);
    QString out;
    out.reserve(text.size());
    qsizetype pos = 0;
    while (pos < text.size()) {
        const auto end = text.indexOf(QLatin1Char('\n'), pos);
        const auto lineEnd = end < 0 ? text.size() : end;
        auto line = text.mid(pos, lineEnd - pos);
        const auto eq = line.indexOf(QLatin1Char('='));
        if (eq > 0 && configKeyIsSecret(line.left(eq))) {
            const bool trailingCr = line.endsWith(QLatin1Char('\r'));
            line = line.left(eq + 1) + removedMarker();
            if (trailingCr) {
                line += QLatin1Char('\r');
            }
        }
        out += line;
        if (end >= 0) {
            out += QLatin1Char('\n');
        }
        pos = lineEnd + 1;
    }
    return scrubText(out).toUtf8();
}

QString ProblemReportScrubber::scrubArguments(const QStringList &arguments)
{
    QStringList cleaned;
    cleaned.reserve(arguments.size());
    for (qsizetype i = 0; i < arguments.size(); ++i) {
        const auto &arg = arguments.at(i);
        if (arg.compare(QStringLiteral("--apppassword"), Qt::CaseInsensitive) == 0) {
            cleaned << arg;
            if (i + 1 < arguments.size()) {
                cleaned << removedMarker();
                ++i;
            }
        } else if (arg.startsWith(QStringLiteral("--apppassword="), Qt::CaseInsensitive)) {
            cleaned << QStringLiteral("--apppassword=") + removedMarker();
        } else {
            cleaned << arg;
        }
    }
    return scrubText(cleaned.join(QLatin1Char(' ')));
}

} // namespace OCC::Nourax
