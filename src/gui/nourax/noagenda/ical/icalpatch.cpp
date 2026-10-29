/*
 * SPDX-FileCopyrightText: 2026 Nourax
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "icalpatch.h"

#include "icaltext.h"

namespace OCC::Nourax::Agenda {

namespace {

const auto kAgentProperty = QStringLiteral("X-NOURAX-AGENT-ID");

QString stamp(const QDateTime &time)
{
    return time.toUTC().toString(QStringLiteral("yyyyMMdd'T'HHmmss'Z'"));
}

QStringList splitKeepEnds(const QString &text)
{
    QStringList out;
    int start = 0;
    for (int i = 0; i < text.size(); ++i) {
        if (text.at(i) == QLatin1Char('\n')) {
            out.append(text.mid(start, i - start + 1));
            start = i + 1;
        }
    }
    if (start < text.size()) {
        out.append(text.mid(start));
    }
    return out;
}

QString stripEol(QString line)
{
    while (line.endsWith(QLatin1Char('\n')) || line.endsWith(QLatin1Char('\r'))) {
        line.chop(1);
    }
    return line;
}

/// The position of the first colon outside double quotes (the end of the name and parameters).
int colonOf(const QString &logical)
{
    bool quoted = false;
    for (int i = 0; i < logical.size(); ++i) {
        const QChar c = logical.at(i);
        if (c == QLatin1Char('"')) {
            quoted = !quoted;
        } else if (c == QLatin1Char(':') && !quoted) {
            return i;
        }
    }
    return -1;
}

struct Entry
{
    QString raw; ///< the physical lines, with their line endings, exactly as they were
    QString logical; ///< unfolded, without line ending
    QString name; ///< upper case; BEGIN and END for component lines
    QString head; ///< name and parameters, as written
    QString value; ///< after the first colon outside quotes

    void analyse()
    {
        logical.clear();
        const auto lines = splitKeepEnds(raw);
        for (int i = 0; i < lines.size(); ++i) {
            auto line = stripEol(lines.at(i));
            if (i > 0 && !line.isEmpty()) {
                line.remove(0, 1); // the folding space or tab
            }
            logical += line;
        }
        const int colon = colonOf(logical);
        head = colon >= 0 ? logical.left(colon) : logical;
        value = colon >= 0 ? logical.mid(colon + 1) : QString();
        const int semi = head.indexOf(QLatin1Char(';'));
        name = (semi >= 0 ? head.left(semi) : head).trimmed().toUpper();
    }
};

QList<Entry> parseEntries(const QString &text)
{
    QList<Entry> entries;
    const auto lines = splitKeepEnds(text);
    for (const auto &line : lines) {
        if (!entries.isEmpty() && (line.startsWith(QLatin1Char(' ')) || line.startsWith(QLatin1Char('\t')))) {
            entries.last().raw += line;
            continue;
        }
        Entry e;
        e.raw = line;
        entries.append(e);
    }
    for (auto &e : entries) {
        e.analyse();
    }
    return entries;
}

class Doc
{
public:
    Doc(const QString &text, const QString &componentType)
        : _entries(parseEntries(text))
        , _type(componentType)
    {
        _eol = text.contains(QLatin1String("\r\n")) || !text.contains(QLatin1Char('\n')) ? QStringLiteral("\r\n") : QStringLiteral("\n");
        locate();
    }

    [[nodiscard]] bool found() const { return _count >= 1 && _begin >= 0 && _end > _begin; }
    [[nodiscard]] int count() const { return _count; }
    [[nodiscard]] QString eol() const { return _eol; }

    [[nodiscard]] QString text() const
    {
        QString out;
        for (const auto &e : _entries) {
            out += e.raw;
        }
        return out;
    }

    [[nodiscard]] QList<int> props(const QString &name) const
    {
        QList<int> out;
        int nested = 0;
        for (int i = _begin + 1; i < _end; ++i) {
            const auto &e = _entries.at(i);
            if (e.name == QLatin1String("BEGIN")) {
                ++nested;
                continue;
            }
            if (e.name == QLatin1String("END")) {
                --nested;
                continue;
            }
            if (nested == 0 && e.name == name) {
                out.append(i);
            }
        }
        return out;
    }

    [[nodiscard]] bool has(const QString &name) const { return !props(name).isEmpty(); }
    [[nodiscard]] const Entry &entry(int index) const { return _entries.at(index); }

    void removeAll(const QString &name)
    {
        const auto indexes = props(name);
        for (int i = indexes.size() - 1; i >= 0; --i) {
            _entries.removeAt(indexes.at(i));
        }
        locate();
    }

    /// Replaces the first property of this name in place (the others are removed), or adds it before END.
    void set(const QString &name, const QString &logical)
    {
        const auto indexes = props(name);
        Entry e;
        e.raw = ICalPatch::foldLine(logical, _eol);
        e.analyse();
        if (indexes.isEmpty()) {
            if (_end > 0 && !_entries.at(_end - 1).raw.endsWith(QLatin1Char('\n'))) {
                _entries[_end - 1].raw += _eol;
            }
            _entries.insert(_end, e);
        } else {
            _entries[indexes.first()] = e;
            for (int i = indexes.size() - 1; i >= 1; --i) {
                _entries.removeAt(indexes.at(i));
            }
        }
        locate();
    }

    void replaceEntry(int index, const QString &logical)
    {
        Entry e;
        e.raw = ICalPatch::foldLine(logical, _eol);
        e.analyse();
        _entries[index] = e;
    }

    void removeEntry(int index)
    {
        _entries.removeAt(index);
        locate();
    }

private:
    void locate()
    {
        _begin = -1;
        _end = -1;
        _count = 0;
        int depth = 0;
        for (int i = 0; i < _entries.size(); ++i) {
            const auto &e = _entries.at(i);
            if (e.name == QLatin1String("BEGIN")) {
                if (depth == 1 && e.value.trimmed().toUpper() == _type) {
                    ++_count;
                    if (_count == 1) {
                        _begin = i;
                    }
                }
                ++depth;
            } else if (e.name == QLatin1String("END")) {
                --depth;
                if (depth == 1 && _begin >= 0 && _end < 0 && i > _begin && e.value.trimmed().toUpper() == _type) {
                    _end = i;
                }
            }
        }
    }

    QList<Entry> _entries;
    QString _type;
    QString _eol;
    int _begin = -1;
    int _end = -1;
    int _count = 0;
};

QString dateLine(const QString &name, const ICalDateValue &value)
{
    if (value.dateOnly) {
        return name + QStringLiteral(";VALUE=DATE:") + value.date.toString(QStringLiteral("yyyyMMdd"));
    }
    return name + QLatin1Char(':') + stamp(value.utc);
}

void setDate(Doc &doc, const QString &name, const ICalDateValue &value)
{
    switch (value.mode) {
    case ICalDateValue::Mode::Keep:
        return;
    case ICalDateValue::Mode::Remove:
        doc.removeAll(name);
        return;
    case ICalDateValue::Mode::Set:
        doc.set(name, dateLine(name, value));
        return;
    }
}

void setText(Doc &doc, const QString &name, const QString &value)
{
    if (name == QLatin1String("DESCRIPTION") && value.isEmpty()) {
        doc.removeAll(name);
        return;
    }
    QString head = name;
    const auto indexes = doc.props(name);
    if (!indexes.isEmpty()) {
        head = doc.entry(indexes.first()).head; // keeps its parameters (for example LANGUAGE)
    }
    doc.set(name, head + QLatin1Char(':') + ICalPatch::escapeText(value));
}

QStringList splitCategories(const QString &value)
{
    QStringList out;
    QString current;
    for (int i = 0; i < value.size(); ++i) {
        const QChar c = value.at(i);
        if (c == QLatin1Char('\\') && i + 1 < value.size()) {
            current.append(c);
            current.append(value.at(i + 1));
            ++i;
        } else if (c == QLatin1Char(',')) {
            out.append(ICalText::unescape(current));
            current.clear();
        } else {
            current.append(c);
        }
    }
    out.append(ICalText::unescape(current));
    return out;
}

QString joinCategories(const QStringList &values)
{
    QStringList parts;
    for (const auto &v : values) {
        parts.append(ICalPatch::escapeText(v));
    }
    return parts.join(QLatin1Char(','));
}

void editCategories(Doc &doc, const QStringList &remove, const QString &add)
{
    bool addPresent = false;
    auto indexes = doc.props(QStringLiteral("CATEGORIES"));
    for (int n = indexes.size() - 1; n >= 0; --n) {
        const int index = indexes.at(n);
        const auto &e = doc.entry(index);
        const auto values = splitCategories(e.value);
        QStringList kept;
        for (const auto &v : values) {
            if (!add.isEmpty() && v == add) {
                addPresent = true;
                kept.append(v);
            } else if (remove.contains(v)) {
                continue;
            } else {
                kept.append(v);
            }
        }
        if (kept == values) {
            continue; // untouched, byte for byte
        }
        if (kept.isEmpty() || (kept.size() == 1 && kept.first().isEmpty())) {
            doc.removeEntry(index);
        } else {
            doc.replaceEntry(index, e.head + QLatin1Char(':') + joinCategories(kept));
        }
    }
    if (add.isEmpty() || addPresent) {
        return;
    }
    indexes = doc.props(QStringLiteral("CATEGORIES"));
    if (indexes.isEmpty()) {
        doc.set(QStringLiteral("CATEGORIES"), QStringLiteral("CATEGORIES:") + ICalPatch::escapeText(add));
        return;
    }
    const auto &first = doc.entry(indexes.first());
    auto values = splitCategories(first.value);
    values.append(add);
    doc.replaceEntry(indexes.first(), first.head + QLatin1Char(':') + joinCategories(values));
}

bool repeatsIn(const Doc &doc)
{
    return doc.has(QStringLiteral("RRULE")) || doc.has(QStringLiteral("RDATE")) || doc.has(QStringLiteral("RECURRENCE-ID")) || doc.has(QStringLiteral("EXRULE"));
}

void applyFields(Doc &doc, const ICalEdit &edit, bool creating)
{
    const auto now = edit.now.isValid() ? edit.now : QDateTime::currentDateTimeUtc();
    // The time stamps.
    doc.set(QStringLiteral("DTSTAMP"), QStringLiteral("DTSTAMP:") + stamp(now));
    if (creating) {
        doc.set(QStringLiteral("CREATED"), QStringLiteral("CREATED:") + stamp(now));
    }
    doc.set(QStringLiteral("LAST-MODIFIED"), QStringLiteral("LAST-MODIFIED:") + stamp(now));
    if (creating) {
        doc.set(QStringLiteral("SEQUENCE"), QStringLiteral("SEQUENCE:0"));
    } else {
        int sequence = 0;
        const auto indexes = doc.props(QStringLiteral("SEQUENCE"));
        if (!indexes.isEmpty()) {
            sequence = doc.entry(indexes.first()).value.trimmed().toInt();
        }
        doc.set(QStringLiteral("SEQUENCE"), QStringLiteral("SEQUENCE:") + QString::number(sequence + 1));
    }

    if (edit.summary) {
        setText(doc, QStringLiteral("SUMMARY"), *edit.summary);
    }
    if (edit.description) {
        setText(doc, QStringLiteral("DESCRIPTION"), *edit.description);
    }
    setDate(doc, QStringLiteral("DTSTART"), edit.start);
    if (edit.end.mode == ICalDateValue::Mode::Set) {
        doc.removeAll(QStringLiteral("DURATION"));
    }
    setDate(doc, QStringLiteral("DTEND"), edit.end);
    if (edit.due.mode == ICalDateValue::Mode::Set) {
        doc.removeAll(QStringLiteral("DURATION"));
    }
    setDate(doc, QStringLiteral("DUE"), edit.due);

    if (edit.done) {
        if (*edit.done) {
            doc.set(QStringLiteral("STATUS"), QStringLiteral("STATUS:COMPLETED"));
            doc.set(QStringLiteral("COMPLETED"), QStringLiteral("COMPLETED:") + stamp(now));
            doc.set(QStringLiteral("PERCENT-COMPLETE"), QStringLiteral("PERCENT-COMPLETE:100"));
        } else {
            doc.set(QStringLiteral("STATUS"), QStringLiteral("STATUS:NEEDS-ACTION"));
            doc.removeAll(QStringLiteral("COMPLETED"));
            doc.removeAll(QStringLiteral("PERCENT-COMPLETE"));
        }
    }

    if (edit.changeAgent) {
        if (edit.agentId.isEmpty()) {
            doc.removeAll(kAgentProperty);
        } else {
            doc.set(kAgentProperty, kAgentProperty + QLatin1Char(':') + ICalPatch::escapeText(edit.agentId));
        }
        editCategories(doc, edit.removeCategories, edit.addCategory);
    }
}

} // namespace

QString ICalPatch::escapeText(const QString &value)
{
    QString out;
    out.reserve(value.size() + 8);
    for (int i = 0; i < value.size(); ++i) {
        const QChar c = value.at(i);
        if (c == QLatin1Char('\\')) {
            out += QLatin1String("\\\\");
        } else if (c == QLatin1Char(';')) {
            out += QLatin1String("\\;");
        } else if (c == QLatin1Char(',')) {
            out += QLatin1String("\\,");
        } else if (c == QLatin1Char('\r')) {
            if (i + 1 < value.size() && value.at(i + 1) == QLatin1Char('\n')) {
                ++i;
            }
            out += QLatin1String("\\n");
        } else if (c == QLatin1Char('\n')) {
            out += QLatin1String("\\n");
        } else {
            out += c;
        }
    }
    return out;
}

QString ICalPatch::foldLine(const QString &logical, const QString &eol)
{
    const QByteArray utf8 = logical.toUtf8();
    QString out;
    int i = 0;
    bool first = true;
    if (utf8.isEmpty()) {
        return eol;
    }
    while (i < utf8.size()) {
        const int limit = first ? 75 : 74; // a continuation line starts with one space
        int take = static_cast<int>(qMin<qsizetype>(limit, utf8.size() - i));
        while (take > 1 && i + take < utf8.size() && (static_cast<unsigned char>(utf8.at(i + take)) & 0xC0) == 0x80) {
            --take;
        }
        out += QString::fromUtf8(utf8.mid(i, take));
        i += take;
        out += eol;
        if (i < utf8.size()) {
            out += QLatin1Char(' ');
        }
        first = false;
    }
    return out;
}

bool ICalPatch::isRepeating(const QString &text)
{
    for (const auto *type : {"VEVENT", "VTODO"}) {
        Doc doc(text, QString::fromLatin1(type));
        if (doc.found() && repeatsIn(doc)) {
            return true;
        }
    }
    return false;
}

ICalPatchResult ICalPatch::apply(const QString &text, const ICalEdit &edit)
{
    ICalPatchResult result;
    Doc doc(text, edit.isTodo ? QStringLiteral("VTODO") : QStringLiteral("VEVENT"));
    if (!doc.found()) {
        result.error = QStringLiteral("no-component");
        return result;
    }
    if (doc.count() > 1) {
        result.error = QStringLiteral("several-components");
        return result;
    }
    if (repeatsIn(doc)) {
        result.error = QStringLiteral("repeats");
        return result;
    }
    applyFields(doc, edit, false);
    result.ok = true;
    result.text = doc.text();
    return result;
}

ICalPatchResult ICalPatch::create(const QString &uid, const ICalEdit &edit)
{
    ICalPatchResult result;
    const QString eol = QStringLiteral("\r\n");
    const QString type = edit.isTodo ? QStringLiteral("VTODO") : QStringLiteral("VEVENT");
    QString skeleton;
    skeleton += QStringLiteral("BEGIN:VCALENDAR") + eol;
    skeleton += QStringLiteral("VERSION:2.0") + eol;
    skeleton += QStringLiteral("PRODID:-//Nourax//NoAgenda//EN") + eol;
    skeleton += QStringLiteral("CALSCALE:GREGORIAN") + eol;
    skeleton += QStringLiteral("BEGIN:") + type + eol;
    skeleton += foldLine(QStringLiteral("UID:") + escapeText(uid), eol);
    skeleton += QStringLiteral("END:") + type + eol;
    skeleton += QStringLiteral("END:VCALENDAR") + eol;

    ICalEdit full = edit;
    if (!edit.isTodo) {
        if (edit.start.mode != ICalDateValue::Mode::Set) {
            result.error = QStringLiteral("no-start");
            return result;
        }
        if (edit.end.mode != ICalDateValue::Mode::Set) {
            full.end = edit.start.dateOnly ? ICalDateValue::ofDate(edit.start.date.addDays(1)) : ICalDateValue::ofUtc(edit.start.utc.addSecs(3600));
        }
    }
    if (!full.summary) {
        full.summary = QString();
    }

    Doc doc(skeleton, type);
    if (!doc.found()) {
        result.error = QStringLiteral("no-component");
        return result;
    }
    applyFields(doc, full, true);
    if (edit.isTodo && !edit.done) {
        doc.set(QStringLiteral("STATUS"), QStringLiteral("STATUS:NEEDS-ACTION"));
    } else if (!edit.isTodo) {
        doc.set(QStringLiteral("STATUS"), QStringLiteral("STATUS:CONFIRMED"));
    }
    result.ok = true;
    result.text = doc.text();
    return result;
}

} // namespace OCC::Nourax::Agenda
