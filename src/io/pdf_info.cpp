#include "io/pdf_info.hpp"
#include <optional>
#include <utility>
namespace sketchy {
namespace {
bool pdfSpace(char c) {
    return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f' || c == '\0';
}
// Skips white space, then reads an unsigned decimal integer at `pos`.
std::optional<qint64> unsignedNumber(QByteArrayView bytes, qsizetype &pos) {
    while (pos < bytes.size() && pdfSpace(bytes[pos]))
        ++pos;
    const auto start = pos;
    qint64 value = 0;
    while (pos < bytes.size() && pos - start < 12 && bytes[pos] >= '0' && bytes[pos] <= '9')
        value = value * 10 + (bytes[pos++] - '0');
    if (pos == start)
        return std::nullopt;
    return value;
}
// The trailer's /Info object as [start of "N 0 obj", start of its "endobj"), found
// through the last cross-reference table the way a reader finds it. Only the
// classic single-section table QPdfWriter writes is recognised.
std::optional<std::pair<qsizetype, qsizetype>> infoObject(QByteArrayView pdf) {
    const auto startxref = pdf.lastIndexOf(QByteArrayView("startxref"));
    if (startxref < 0)
        return std::nullopt;
    const auto trailer = pdf.first(startxref).lastIndexOf(QByteArrayView("trailer"));
    if (trailer < 0)
        return std::nullopt;
    auto pos = pdf.first(startxref).indexOf(QByteArrayView("/Info"), trailer);
    if (pos < 0)
        return std::nullopt;
    pos += 5;
    const auto object = unsignedNumber(pdf, pos);
    pos = startxref + 9;
    const auto xref = unsignedNumber(pdf, pos);
    if (!object || !xref || *xref >= pdf.size() || !pdf.sliced(*xref).startsWith("xref"))
        return std::nullopt;
    pos = *xref + 4;
    const auto first = unsignedNumber(pdf, pos);
    const auto count = unsignedNumber(pdf, pos);
    if (!first || !count || *object < *first || *object >= *first + *count)
        return std::nullopt;
    while (pos < pdf.size() && pdfSpace(pdf[pos]))
        ++pos;
    // Each cross-reference entry is exactly 20 bytes: "oooooooooo ggggg n\r\n".
    const auto entry = pos + (*object - *first) * 20;
    if (entry < pos || entry + 20 > pdf.size() || pdf[entry + 17] != 'n')
        return std::nullopt;
    auto at = entry;
    const auto offset = unsignedNumber(pdf, at);
    const QByteArray header = QByteArray::number(*object) + " 0 obj";
    if (!offset || at != entry + 10 || *offset >= pdf.size() ||
        !pdf.sliced(*offset).startsWith(header))
        return std::nullopt;
    const auto end = pdf.indexOf(QByteArrayView("endobj"), *offset);
    if (end < 0)
        return std::nullopt;
    return std::pair{qsizetype(*offset), end};
}
} // namespace

bool repairQtPdfInfo(QByteArray &pdf, const QDateTime &created) {
    const auto range = infoObject(pdf);
    if (!range)
        return false;
    const auto [begin, end] = *range;
    bool changed = false;
    // Qt 6.8.0 and 6.8.1 end the dictionary with "/Trapped /False\n2\n" before "endobj".
    // PDF names are self-delimiting, so dropping the space leaves room for ">>".
    const QByteArrayView brokenClose = "/Trapped /False\n2\n";
    const QByteArrayView fixedClose = "/Trapped/False\n>>\n";
    Q_ASSERT(brokenClose.size() == fixedClose.size());
    const auto close = end - brokenClose.size();
    if (close > begin && QByteArrayView(pdf).sliced(close, brokenClose.size()) == brokenClose) {
        pdf.replace(close, fixedClose.size(), fixedClose);
        changed = true;
    }
    // An invalid QDateTime (Qt 6.8.0 without a known system time zone) prints as this.
    const QByteArrayView invalidDate = "(D:00000000-1-1-1Z)";
    const auto utc = created.toUTC();
    const QByteArray date =
        "(D:" + utc.toString(QStringLiteral("yyyyMMddHHmmss")).toLatin1() + "Z)";
    if (!utc.isValid() || date.size() != invalidDate.size())
        return changed;
    for (auto at = pdf.indexOf(invalidDate, begin); at >= 0 && at + invalidDate.size() <= end;
         at = pdf.indexOf(invalidDate, at + invalidDate.size())) {
        pdf.replace(at, date.size(), date);
        changed = true;
    }
    return changed;
}
} // namespace sketchy
