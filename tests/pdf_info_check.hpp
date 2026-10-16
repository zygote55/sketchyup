#pragma once
// D-036: an independent reader for a PDF's document information dictionary, found the
// way a PDF reader finds it (startxref, the cross-reference table, the trailer's
// /Info reference), with a strict tokenizer for the dictionary itself. It catches a
// dictionary that is not closed by ">>", a key that is not a name, and dates that are
// not "D:YYYYMMDDHHmmSS" with an optional valid UTC offset, on any Qt and without
// Poppler. Also used to build a Qt 6.8.0-shaped Info dictionary for the repair tests.
#include <QByteArray>
#include <QRegularExpression>
#include <QString>
#include <map>
#include <optional>
#include <string>
#include <utility>
namespace sketchy::pdfcheck {
inline bool space(char c) {
    return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f' || c == '\0';
}
inline bool delimiter(char c) {
    return space(c) || c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']' ||
           c == '{' || c == '}' || c == '/' || c == '%';
}
inline void skipSpace(const QByteArray &b, qsizetype &pos) {
    while (pos < b.size()) {
        if (space(b[pos]))
            ++pos;
        else if (b[pos] == '%')
            while (pos < b.size() && b[pos] != '\n' && b[pos] != '\r')
                ++pos;
        else
            break;
    }
}
inline std::optional<qint64> integer(const QByteArray &b, qsizetype &pos) {
    skipSpace(b, pos);
    const auto start = pos;
    qint64 value = 0;
    while (pos < b.size() && pos - start < 12 && b[pos] >= '0' && b[pos] <= '9')
        value = value * 10 + (b[pos++] - '0');
    if (pos == start)
        return std::nullopt;
    return value;
}

// [start of "N G obj", position just after it) of the trailer's /Info object.
inline std::pair<qsizetype, std::string> infoObjectStart(const QByteArray &pdf) {
    const auto startxref = pdf.lastIndexOf("startxref");
    if (startxref < 0)
        return {-1, "No startxref"};
    qsizetype pos = startxref + 9;
    const auto xref = integer(pdf, pos);
    if (!xref || *xref >= pdf.size() || pdf.mid(*xref, 4) != "xref")
        return {-1, "startxref does not point at a cross-reference table"};
    const auto trailer = pdf.lastIndexOf("trailer", startxref);
    const auto infoKey = trailer < 0 ? -1 : pdf.indexOf("/Info", trailer);
    if (infoKey < 0 || infoKey > startxref)
        return {-1, "The trailer has no /Info"};
    pos = infoKey + 5;
    const auto object = integer(pdf, pos);
    const auto generation = integer(pdf, pos);
    skipSpace(pdf, pos);
    if (!object || !generation || pos >= pdf.size() || pdf[pos] != 'R')
        return {-1, "/Info is not an indirect reference"};
    pos = *xref + 4;
    // Walk the subsections until one holds the object.
    while (true) {
        const auto first = integer(pdf, pos);
        const auto count = integer(pdf, pos);
        if (!first || !count)
            return {-1, "The cross-reference table has no entry for /Info"};
        skipSpace(pdf, pos);
        if (*object >= *first && *object < *first + *count) {
            const auto entry = pos + (*object - *first) * 20;
            if (entry + 20 > pdf.size() || pdf[entry + 17] != 'n')
                return {-1, "The /Info cross-reference entry is not in use"};
            auto at = entry;
            const auto offset = integer(pdf, at);
            const QByteArray header =
                QByteArray::number(*object) + ' ' + QByteArray::number(*generation) + " obj";
            if (!offset || *offset >= pdf.size() || pdf.mid(*offset, header.size()) != header)
                return {-1, "The /Info offset does not point at its object"};
            return {*offset + header.size(), {}};
        }
        pos += *count * 20;
    }
}

inline QString textString(const QByteArray &bytes) {
    if (bytes.startsWith("\xfe\xff")) {
        QString text;
        for (qsizetype i = 2; i + 1 < bytes.size(); i += 2)
            text.append(QChar(char16_t((uchar(bytes[i]) << 8) | uchar(bytes[i + 1]))));
        return text;
    }
    return QString::fromLatin1(bytes); // PDFDocEncoding agrees for printable ASCII.
}

struct Info {
    std::map<QByteArray, QByteArray> entries; // Key name -> string bytes or raw token.
    std::string error;
};

// Parses the /Info dictionary strictly; on success, `error` is empty.
inline Info readInfo(const QByteArray &pdf) {
    Info info;
    auto [pos, error] = infoObjectStart(pdf);
    if (pos < 0) {
        info.error = error;
        return info;
    }
    auto fail = [&](const std::string &message) {
        info.error = message + " at byte " + std::to_string(pos);
        return info;
    };
    skipSpace(pdf, pos);
    if (pdf.mid(pos, 2) != "<<")
        return fail("The /Info object is not a dictionary");
    pos += 2;
    while (true) {
        skipSpace(pdf, pos);
        if (pos >= pdf.size())
            return fail("Unterminated /Info dictionary");
        if (pdf.mid(pos, 2) == ">>") {
            pos += 2;
            break;
        }
        if (pdf[pos] != '/')
            return fail("Dictionary key must be a name object");
        const auto keyStart = ++pos;
        while (pos < pdf.size() && !delimiter(pdf[pos]))
            ++pos;
        const auto key = pdf.mid(keyStart, pos - keyStart);
        if (key.isEmpty() || info.entries.contains(key))
            return fail("Empty or repeated /Info key");
        skipSpace(pdf, pos);
        if (pos >= pdf.size())
            return fail("Missing /Info value");
        QByteArray value;
        if (pdf[pos] == '(') {
            int depth = 1;
            ++pos;
            while (pos < pdf.size() && depth > 0) {
                const char c = pdf[pos++];
                if (c == '\\') {
                    if (pos >= pdf.size())
                        break;
                    const char e = pdf[pos++];
                    static const QByteArray from = "nrtbf()\\", to = "\n\r\t\b\f()\\";
                    if (const auto i = from.indexOf(e); i >= 0)
                        value.append(to[i]);
                    else if (e >= '0' && e <= '7') {
                        int code = e - '0';
                        for (int n = 0;
                             n < 2 && pos < pdf.size() && pdf[pos] >= '0' && pdf[pos] <= '7'; ++n)
                            code = code * 8 + (pdf[pos++] - '0');
                        value.append(char(code));
                    } else if (e == '\r' && pos < pdf.size() && pdf[pos] == '\n')
                        ++pos;
                    else if (e != '\n' && e != '\r')
                        value.append(e);
                    continue;
                }
                if (c == '(')
                    ++depth;
                if (c == ')' && --depth == 0)
                    break;
                value.append(c);
            }
            if (depth != 0)
                return fail("Unterminated string in /Info");
        } else if (pdf[pos] == '<' && pdf.mid(pos, 2) != "<<") {
            const auto end = pdf.indexOf('>', pos);
            if (end < 0)
                return fail("Unterminated hex string in /Info");
            value = QByteArray::fromHex(pdf.mid(pos + 1, end - pos - 1));
            pos = end + 1;
        } else if (pdf[pos] == '/' || (pdf[pos] >= '0' && pdf[pos] <= '9') ||
                   QByteArrayView(pdf).sliced(pos).startsWith("true") ||
                   QByteArrayView(pdf).sliced(pos).startsWith("false")) {
            const auto start = pos++;
            while (pos < pdf.size() && !delimiter(pdf[pos]))
                ++pos;
            value = pdf.mid(start, pos - start);
        } else
            return fail("Unsupported /Info value");
        info.entries[key] = value;
    }
    skipSpace(pdf, pos);
    if (pdf.mid(pos, 6) != "endobj")
        return fail("The /Info dictionary is not followed by endobj");
    return info;
}

inline bool validDate(const QByteArray &value) {
    static const QRegularExpression pattern(
        R"(^D:(\d{4})(\d\d)(\d\d)(\d\d)(\d\d)(\d\d)(?:Z(?:00'00'?)?|[+-](\d\d)'(\d\d)'?)?$)");
    const auto m = pattern.match(QString::fromLatin1(value));
    if (!m.hasMatch())
        return false;
    auto in = [&](int group, int low, int high) {
        if (m.captured(group).isEmpty())
            return true;
        const auto v = m.captured(group).toInt();
        return v >= low && v <= high;
    };
    return in(1, 1000, 9999) && in(2, 1, 12) && in(3, 1, 31) && in(4, 0, 23) && in(5, 0, 59) &&
           in(6, 0, 59) && in(7, 0, 23) && in(8, 0, 59);
}

// Empty when the /Info dictionary is well formed with this Title and Creator, a
// "Qt ..." Producer and valid creation and modification dates.
inline std::string infoProblem(const QByteArray &pdf, const QString &title,
                               const QString &creator) {
    const auto info = readInfo(pdf);
    if (!info.error.empty())
        return info.error;
    auto text = [&](const char *key) {
        const auto it = info.entries.find(key);
        return it == info.entries.end() ? QString() : textString(it->second);
    };
    if (text("Title") != title)
        return "Title is \"" + text("Title").toStdString() + "\"";
    if (text("Creator") != creator)
        return "Creator is \"" + text("Creator").toStdString() + "\"";
    if (!text("Producer").startsWith("Qt "))
        return "Producer is \"" + text("Producer").toStdString() + "\"";
    for (const char *key : {"CreationDate", "ModDate"})
        if (!info.entries.contains(key) || !validDate(info.entries.at(key)))
            return std::string(key) + " is \"" + text(key).toStdString() + "\"";
    return {};
}

// A copy of a well-formed Qt PDF whose /Info dictionary has the Qt 6.8.0 defects at
// the same byte length, so the cross-reference table stays exact: the dictionary
// ends "/Trapped /False\n2\n" and both dates read "(D:00000000-1-1-1Z)".
inline QByteArray qt680Info(QByteArray pdf) {
    auto [start, error] = infoObjectStart(pdf);
    if (start < 0)
        return {};
    const auto end = pdf.indexOf("endobj", start);
    // Qt 6.8.2 and later end "/Trapped /False\n>>\n"; a repaired Qt 6.8.0 export ends
    // "/Trapped/False\n>>\n".
    const QByteArray broken = "/Trapped /False\n2\n";
    bool replaced = false;
    for (const QByteArray close : {"/Trapped /False\n>>\n", "/Trapped/False\n>>\n"})
        if (!replaced && end >= close.size() &&
            pdf.mid(end - close.size(), close.size()) == close) {
            const auto padding = QByteArray(close.size() - broken.size(), ' ');
            pdf.replace(end - close.size(), close.size(), padding + broken);
            replaced = true;
        }
    if (!replaced)
        return {};
    static const QRegularExpression date(R"(\(D:[0-9]{14}[^)]*\))");
    auto it = date.globalMatch(QString::fromLatin1(pdf.mid(start, end - start)));
    while (it.hasNext()) {
        const auto m = it.next();
        QByteArray invalid = "(D:00000000-1-1-1Z)";
        if (m.capturedLength() < invalid.size())
            return {};
        invalid.append(QByteArray(m.capturedLength() - invalid.size(), ' '));
        pdf.replace(start + m.capturedStart(), invalid.size(), invalid);
    }
    return pdf;
}
} // namespace sketchy::pdfcheck
