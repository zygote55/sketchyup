#include "io/terrain_source.hpp"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStringDecoder>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>

namespace sketchy {
namespace {
void require(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}
// Cuts on a UTF-8 code point boundary.
std::string cut(const std::string &text, std::size_t bytes) {
    if (text.size() <= bytes)
        return text;
    std::size_t end = bytes;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80)
        --end;
    return text.substr(0, end);
}
std::string bounded(const std::string &message) {
    return cut(message, TerrainReportBounds::messageBytes);
}
} // namespace

const std::vector<TerrainUnit> &terrainUnits() {
    static const std::vector<TerrainUnit> units{
        {"mm", .001, "0.001"},   {"cm", .01, "0.01"},
        {"m", 1, "1"},           {"in", .0254, "0.0254"},
        {"ft", .3048, "0.3048"}, {"usft", usSurveyFootMetres, "1200/3937"}};
    return units;
}
std::optional<TerrainUnit> terrainUnit(const QString &name) {
    for (const auto &unit : terrainUnits())
        if (unit.name == name)
            return unit;
    return std::nullopt;
}
QString terrainUnitNames() { return "mm, cm, m, in, ft or usft"; }

std::string TerrainSourceRef::text() const {
    switch (kind) {
    case Kind::Handle: {
        char hex[24];
        std::snprintf(hex, sizeof hex, "%llX", static_cast<unsigned long long>(value));
        return std::string("handle ") + hex;
    }
    case Kind::Entity:
        return "entity " + std::to_string(value);
    case Kind::Line:
        return "line " + std::to_string(value);
    }
    return {};
}

std::string terrainQuote(const std::string &text) {
    std::string clean;
    clean.reserve(std::min<std::size_t>(text.size(), TerrainReportBounds::quotedBytes + 4));
    for (const char c : text) {
        if (clean.size() >= TerrainReportBounds::quotedBytes + 4)
            break;
        clean.push_back(static_cast<unsigned char>(c) < 0x20 || c == 0x7f ? '?' : c);
    }
    const bool longer = text.size() > TerrainReportBounds::quotedBytes;
    auto quoted = cut(clean, TerrainReportBounds::quotedBytes);
    return "'" + quoted + (longer ? "…'" : "'");
}

TerrainReport::Kind *TerrainReport::find(Severity severity, const std::string &kind,
                                         const std::string &message) {
    for (auto &entry : kinds_)
        if (entry.severity == severity && entry.name == kind)
            return &entry;
    if (kinds_.size() >= TerrainReportBounds::kinds) {
        if (std::find(omittedNames_.begin(), omittedNames_.end(), kind) == omittedNames_.end())
            omittedNames_.push_back(kind);
        return nullptr;
    }
    kinds_.push_back({severity, kind, bounded(message), 0, {}});
    return &kinds_.back();
}
void TerrainReport::add(Severity severity, const std::string &kind, std::uint64_t count,
                        const std::string &message) {
    auto *entry = find(severity, kind, message);
    if (!entry) {
        omittedOccurrences_ += count;
        // A refusal is never lost, even when its kind cannot be listed.
        if (severity == Severity::Refusal)
            omittedRefusal_ = true;
        return;
    }
    entry->count = count > std::numeric_limits<std::uint64_t>::max() - entry->count
                       ? std::numeric_limits<std::uint64_t>::max()
                       : entry->count + count;
}
void TerrainReport::locate(Severity severity, const std::string &kind, const std::string &message,
                           TerrainReportLocation location, bool counted) {
    if (counted)
        add(severity, kind, 1, message);
    auto *entry = find(severity, kind, message);
    if (!entry || entry->locations.size() >= TerrainReportBounds::locationsPerKind)
        return;
    location.reference = cut(location.reference, 128);
    location.reference2 = cut(location.reference2, 128);
    location.detail = bounded(location.detail);
    entry->locations.push_back(std::move(location));
}
bool TerrainReport::refused() const {
    return omittedRefusal_ || std::any_of(kinds_.begin(), kinds_.end(), [](const Kind &kind) {
               return kind.severity == Severity::Refusal && kind.count > 0;
           });
}
bool TerrainReport::hasNotices() const {
    return std::any_of(kinds_.begin(), kinds_.end(), [](const Kind &kind) {
        return kind.severity == Severity::Notice && kind.count > 0;
    });
}
std::uint64_t TerrainReport::count(const std::string &kind) const {
    std::uint64_t total = 0;
    for (const auto &entry : kinds_)
        if (entry.name == kind)
            total += entry.count;
    return total;
}
QJsonObject TerrainReport::json(QJsonObject envelope, std::size_t budget) const {
    auto build = [&](std::size_t cap) {
        QJsonArray refusals, notices;
        bool thinned = false;
        for (const auto &kind : kinds_) {
            if (!kind.count)
                continue;
            QJsonArray locations;
            const auto listed = std::min(cap, kind.locations.size());
            thinned = thinned || listed < kind.locations.size();
            for (std::size_t i = 0; i < listed; ++i) {
                const auto &location = kind.locations[i];
                QJsonObject entry{{"reference", QString::fromStdString(location.reference)}};
                if (!location.reference2.empty())
                    entry["reference2"] = QString::fromStdString(location.reference2);
                if (location.xy)
                    entry["xy"] = QJsonArray{(*location.xy)[0], (*location.xy)[1]};
                if (!location.detail.empty())
                    entry["detail"] = QString::fromStdString(location.detail);
                locations.append(entry);
            }
            QJsonObject entry{{"kind", QString::fromStdString(kind.name)},
                              {"count", double(kind.count)},
                              {"message", QString::fromStdString(kind.message)},
                              {"locations", locations},
                              {"locationsOmitted", double(kind.count - listed)}};
            (kind.severity == Severity::Refusal ? refusals : notices).append(entry);
        }
        auto result = envelope;
        result["refusals"] = refusals;
        result["notices"] = notices;
        result["kindsOmitted"] = double(omittedNames_.size());
        result["occurrencesInOmittedKinds"] = double(omittedOccurrences_);
        result["locationsPerKind"] = double(cap);
        result["locationsThinned"] = thinned;
        return result;
    };
    for (const std::size_t cap : {std::size_t(32), std::size_t(16), std::size_t(8), std::size_t(4),
                                  std::size_t(2), std::size_t(1), std::size_t(0)}) {
        auto result = build(cap);
        if (std::size_t(QJsonDocument(result).toJson(QJsonDocument::Compact).size()) <= budget)
            return result;
    }
    return build(0);
}

// ---- Point files ---------------------------------------------------------------------------
std::optional<std::vector<TerrainColumn>> terrainColumnPreset(const QString &name) {
    using C = TerrainColumn;
    if (name == "PNEZD")
        return std::vector{C::Point, C::Northing, C::Easting, C::Elevation, C::Description};
    if (name == "PENZD")
        return std::vector{C::Point, C::Easting, C::Northing, C::Elevation, C::Description};
    if (name == "ENZ" || name == "XYZ")
        return std::vector{C::Easting, C::Northing, C::Elevation};
    return std::nullopt;
}
std::optional<TerrainColumn> terrainColumn(const QString &name) {
    static const std::map<QString, TerrainColumn> names{{"point", TerrainColumn::Point},
                                                        {"easting", TerrainColumn::Easting},
                                                        {"northing", TerrainColumn::Northing},
                                                        {"elevation", TerrainColumn::Elevation},
                                                        {"description", TerrainColumn::Description},
                                                        {"ignore", TerrainColumn::Ignore}};
    const auto found = names.find(name);
    return found == names.end() ? std::nullopt : std::optional(found->second);
}
const char *terrainColumnName(TerrainColumn column) {
    switch (column) {
    case TerrainColumn::Point:
        return "point";
    case TerrainColumn::Easting:
        return "easting";
    case TerrainColumn::Northing:
        return "northing";
    case TerrainColumn::Elevation:
        return "elevation";
    case TerrainColumn::Description:
        return "description";
    case TerrainColumn::Ignore:
        return "ignore";
    }
    return "ignore";
}
std::optional<TerrainDelimiter> terrainDelimiter(const QString &name) {
    if (name == "comma")
        return TerrainDelimiter::Comma;
    if (name == "tab")
        return TerrainDelimiter::Tab;
    if (name == "semicolon")
        return TerrainDelimiter::Semicolon;
    if (name == "whitespace")
        return TerrainDelimiter::Whitespace;
    return std::nullopt;
}
const char *terrainDelimiterName(TerrainDelimiter delimiter) {
    switch (delimiter) {
    case TerrainDelimiter::Comma:
        return "comma";
    case TerrainDelimiter::Tab:
        return "tab";
    case TerrainDelimiter::Semicolon:
        return "semicolon";
    case TerrainDelimiter::Whitespace:
        return "whitespace";
    }
    return "comma";
}
namespace {
QStringList fields(const QString &line, TerrainDelimiter delimiter) {
    if (delimiter == TerrainDelimiter::Whitespace)
        return line.simplified().split(' ', Qt::SkipEmptyParts);
    const QChar separator = delimiter == TerrainDelimiter::Comma ? QChar(',')
                            : delimiter == TerrainDelimiter::Tab ? QChar('\t')
                                                                 : QChar(';');
    auto parts = line.split(separator);
    for (auto &part : parts)
        part = part.trimmed();
    return parts;
}
std::optional<double> numeric(const QString &text) {
    bool ok{};
    const auto value = text.toDouble(&ok);
    if (!ok)
        return std::nullopt;
    return value;
}
} // namespace
TerrainSourceSet parseTerrainPoints(const QByteArray &bytes, const TerrainPointOptions &options) {
    require(!bytes.isEmpty() && bytes.size() <= TerrainSourceLimits::bytes,
            "A point file must be 1 byte to 64 MiB");
    require(!bytes.contains('\0'), "A point file must be text, not binary data");
    require(options.unit.metresPerUnit > 0 && std::isfinite(options.unit.metresPerUnit),
            "Point files have no units; choose " + terrainUnitNames().toStdString());
    QStringDecoder decoder(QStringDecoder::Utf8);
    QString text = decoder(bytes);
    require(!decoder.hasError(), "A point file must be valid UTF-8");
    if (text.startsWith(QChar(0xfeff)))
        text.remove(0, 1);
    auto lines = text.split('\n');
    if (!lines.isEmpty() && lines.back().isEmpty())
        lines.removeLast();
    require(std::size_t(lines.size()) <= TerrainSourceLimits::pointLines,
            "A point file has at most 1,000,000 lines");
    TerrainSourceSet set;
    set.format = "points";
    set.unit = options.unit;
    const double scale = options.unit.metresPerUnit;
    std::vector<qsizetype> data; // indices of non-blank, non-comment lines
    std::uint64_t blank = 0, comments = 0;
    for (qsizetype i = 0; i < lines.size(); ++i) {
        auto &line = lines[i];
        if (line.endsWith('\r'))
            line.chop(1);
        require(std::size_t(line.toUtf8().size()) <= TerrainSourceLimits::lineBytes,
                "Point file line " + std::to_string(i + 1) + " exceeds 4096 bytes");
        const auto trimmed = line.trimmed();
        if (trimmed.isEmpty())
            ++blank;
        else if (trimmed.startsWith('#'))
            ++comments;
        else
            data.push_back(i);
    }
    require(!data.empty(), "The point file has no data lines");
    // Delimiter: the first of comma, tab and semicolon that most of the first 20 data lines
    // contain, otherwise whitespace.
    auto delimiter = options.delimiter;
    const bool delimiterDetected = !delimiter;
    if (!delimiter) {
        const std::size_t sample = std::min<std::size_t>(data.size(), 20);
        for (const auto [candidate, separator] :
             {std::pair{TerrainDelimiter::Comma, QChar(',')},
              std::pair{TerrainDelimiter::Tab, QChar('\t')},
              std::pair{TerrainDelimiter::Semicolon, QChar(';')}}) {
            std::size_t containing = 0;
            for (std::size_t i = 0; i < sample; ++i)
                containing += lines[data[i]].contains(separator);
            if (2 * containing > sample) {
                delimiter = candidate;
                break;
            }
        }
        if (!delimiter)
            delimiter = TerrainDelimiter::Whitespace;
    }
    // Header and columns.
    const auto first = fields(lines[data.front()], *delimiter);
    auto columns = options.columns;
    QString preset = options.preset;
    auto numericFields = [&](const QStringList &values) {
        return std::count_if(values.begin(), values.end(),
                             [](const QString &value) { return numeric(value).has_value(); });
    };
    bool header{};
    const bool headerDetected = !options.header;
    if (options.header)
        header = *options.header;
    else if (columns) {
        for (std::size_t c = 0; c < columns->size(); ++c) {
            const auto column = (*columns)[c];
            if ((column == TerrainColumn::Easting || column == TerrainColumn::Northing ||
                 column == TerrainColumn::Elevation) &&
                (qsizetype(c) >= first.size() || !numeric(first[qsizetype(c)])))
                header = true;
        }
    } else
        header = numericFields(first) < 3;
    if (header)
        data.erase(data.begin());
    require(!data.empty(), "The point file has a header but no data lines");
    if (!columns) {
        const auto sample = fields(lines[data.front()], *delimiter);
        require(sample.size() >= 3, "Point lines need at least three columns; line " +
                                        std::to_string(data.front() + 1) + " has " +
                                        std::to_string(sample.size()));
        preset = sample.size() == 3 ? "ENZ" : "PNEZD";
        columns = terrainColumnPreset(preset);
    }
    int easting = -1, northing = -1, elevation = -1, description = -1;
    for (std::size_t c = 0; c < columns->size(); ++c) {
        auto set = [&](int &slot) {
            require(slot < 0, "Each of easting, northing, elevation and description is one column");
            slot = int(c);
        };
        switch ((*columns)[c]) {
        case TerrainColumn::Easting:
            set(easting);
            break;
        case TerrainColumn::Northing:
            set(northing);
            break;
        case TerrainColumn::Elevation:
            set(elevation);
            break;
        case TerrainColumn::Description:
            set(description);
            break;
        default:
            break;
        }
    }
    require(easting >= 0 && northing >= 0 && elevation >= 0 && columns->size() <= 64,
            "Point columns need one easting, one northing and one elevation (at most 64 columns)");
    const int required = std::max({easting, northing, elevation}) + 1;
    std::map<QString, std::uint32_t> codes;
    auto codeOf = [&](const QString &code) {
        const auto found = codes.find(code);
        if (found != codes.end())
            return found->second;
        require(set.layers.size() < TerrainSourceLimits::layers,
                "A point file has at most 1024 description codes");
        const auto index = std::uint32_t(set.layers.size());
        set.layers.push_back({code, 0, 0, 0, 0, 0});
        codes[code] = index;
        return index;
    };
    const double bound = TerrainSourceLimits::coordinate / scale;
    using Severity = TerrainReport::Severity;
    set.points.reserve(data.size());
    for (const auto index : data) {
        const std::uint64_t lineNumber = std::uint64_t(index) + 1;
        const TerrainSourceRef ref{TerrainSourceRef::Kind::Line, lineNumber};
        auto values = fields(lines[index], *delimiter);
        if (values.size() < required) {
            set.problems.locate(Severity::Refusal, "missing-columns",
                                "Lines without the easting, northing and elevation columns.",
                                {ref.text(),
                                 {},
                                 std::nullopt,
                                 "has " + std::to_string(values.size()) + " of " +
                                     std::to_string(required) + " columns"});
            continue;
        }
        double xyz[3]{};
        bool good = true;
        const int indices[3]{easting, northing, elevation};
        for (int k = 0; k < 3; ++k) {
            const auto &value = values[indices[k]];
            const auto number = numeric(value);
            const auto column = std::to_string(indices[k] + 1);
            if (!number) {
                set.problems.locate(Severity::Refusal, "non-numeric",
                                    "Values that are not numbers.",
                                    {ref.text(),
                                     {},
                                     std::nullopt,
                                     "column " + column + " " + terrainQuote(value.toStdString()) +
                                         " is not a number"});
                good = false;
            } else if (!std::isfinite(*number)) {
                set.problems.locate(Severity::Refusal, "non-finite",
                                    "Values that are infinite or not a number.",
                                    {ref.text(),
                                     {},
                                     std::nullopt,
                                     "column " + column + " " + terrainQuote(value.toStdString()) +
                                         " is not finite"});
                good = false;
            } else if (std::abs(*number) > bound) {
                set.problems.locate(Severity::Refusal, "coordinate-out-of-range",
                                    "Values beyond 100,000 km after the unit is applied.",
                                    {ref.text(),
                                     {},
                                     std::nullopt,
                                     "column " + column + " " + terrainQuote(value.toStdString())});
                good = false;
            } else
                xyz[k] = *number;
        }
        if (!good)
            continue;
        QString code;
        if (description >= 0 && description < values.size()) {
            auto text = values[description];
            if (description == values.size() - 1 || *delimiter == TerrainDelimiter::Whitespace) {
                QStringList rest = values.mid(description);
                text = rest.join(' ');
            }
            const auto token = text.simplified().section(' ', 0, 0);
            if (token.toUtf8().size() > qsizetype(TerrainReportBounds::quotedBytes)) {
                set.problems.locate(
                    Severity::Refusal, "code-too-long", "Description codes longer than 64 bytes.",
                    {ref.text(), {}, std::nullopt, "code " + terrainQuote(token.toStdString())});
                continue;
            }
            code = token;
        }
        const auto layer = codeOf(code);
        ++set.layers[layer].points;
        set.points.push_back({ref, layer, {xyz[0], xyz[1], xyz[2]}});
    }
    set.sha256 =
        QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    QJsonArray columnNames;
    for (const auto column : *columns)
        columnNames.append(terrainColumnName(column));
    set.report = {{"apiVersion", 1},
                  {"format", "points"},
                  {"units", set.unit.name},
                  {"metresPerUnit", scale},
                  {"metresPerUnitExact", set.unit.exact},
                  {"lines", qint64(lines.size())},
                  {"dataLines", qint64(data.size())},
                  {"points", qint64(set.points.size())},
                  {"blankLines", double(blank)},
                  {"commentLines", double(comments)},
                  {"header", header},
                  {"headerDetected", headerDetected},
                  {"delimiter", terrainDelimiterName(*delimiter)},
                  {"delimiterDetected", delimiterDetected},
                  {"columns", columnNames},
                  {"columnsPreset", preset.isEmpty() ? QJsonValue() : QJsonValue(preset)},
                  {"codes", qint64(set.layers.size())},
                  {"bytes", qint64(bytes.size())},
                  {"sha256", set.sha256}};
    return set;
}

} // namespace sketchy
