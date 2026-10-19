#include "io/dxf_source.hpp"
#include "io/terrain_source.hpp"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QStringDecoder>
#include <algorithm>
#include <numbers>
#include <set>
namespace sketchy {
namespace {
constexpr double pi = std::numbers::pi;
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
struct Tag {
    int code;
    QString value;
};
using Tags = std::vector<Tag>;
struct Record {
    QString type;
    Tags tags;
};
std::optional<QString> field(const Tags &tags, int code) {
    std::optional<QString> result;
    for (const auto &tag : tags)
        if (tag.code == code) {
            require(!result, "Duplicate singleton DXF group code");
            result = tag.value;
        }
    return result;
}
double numeric(const QString &value) {
    bool ok{};
    const auto n = value.toDouble(&ok);
    require(ok && std::isfinite(n), "DXF number must be finite");
    return n;
}
double number(const Tags &tags, int code, std::optional<double> fallback = {}) {
    const auto text = field(tags, code);
    if (text)
        return numeric(*text);
    require(fallback.has_value(), "Missing DXF coordinate or required group code");
    return *fallback;
}
int integer(const Tags &tags, int code, int fallback) {
    const auto text = field(tags, code);
    if (!text)
        return fallback;
    bool ok{};
    const int value = text->toInt(&ok);
    require(ok, "DXF integer is invalid");
    return value;
}
QString name(QString value) {
    require(!value.isEmpty() && value.toUtf8().size() <= 256 && !value.contains(QChar(0)),
            "DXF name exceeds supported bounds");
    return value;
}
std::vector<Record> records(const Tags &tags) {
    std::vector<Record> result;
    for (const auto &tag : tags) {
        if (tag.code == 0) {
            require(result.size() < 100000, "DXF section exceeds record count");
            require(tag.value.size() <= 64, "DXF record type exceeds bounds");
            result.push_back({tag.value, {}});
        } else {
            require(!result.empty(), "DXF section requires a record marker");
            result.back().tags.push_back(tag);
        }
    }
    return result;
}
struct Reader {
    DxfSource result;
    std::map<QString, Tags> sections;
    QJsonObject omitted, losses;
    size_t segments{};
    double scale{};
    void loss(const QString &key, int amount = 1) { losses[key] = losses[key].toInt() + amount; }
    void skip(const QString &type, const QString &reason) {
        const auto key = type + ":" + reason;
        require(omitted.contains(key) || omitted.size() < 128,
                "Too many distinct unsupported DXF entities");
        omitted[key] = omitted[key].toInt() + 1;
    }
    Vec3 point(const Tags &tags, int x = 10, int y = 20, int z = 30) {
        const Vec3 p{number(tags, x) * scale, number(tags, y) * scale, number(tags, z, 0) * scale};
        checkPoint(p);
        return p;
    }
    bool planar(const Tags &tags) {
        return number(tags, 210, 0) == 0 && number(tags, 220, 0) == 0 &&
               number(tags, 230, 1) == 1 && number(tags, 39, 0) == 0;
    }
    void segment(DxfEntity &entity, Vec3 start, Vec3 end, double bulge = 0) {
        require(++segments <= 100000, "DXF exceeds 100000 expanded segments");
        require(length(end - start) >= tolerance, "DXF has a collapsed segment");
        DxfSegment s{start, end, {}, 0};
        if (bulge != 0) {
            require(std::abs(bulge) <= 1e6, "DXF bulge exceeds supported range");
            const auto chord = end - start;
            const auto distance = length(chord);
            const auto offset = distance * (1 - bulge * bulge) / (4 * bulge);
            s.center =
                (start + end) * .5 + Vec3{-chord.y / distance, chord.x / distance, 0} * offset;
            checkPoint(s.center);
            s.sweep = 4 * std::atan(bulge);
            const auto radius = length(start - s.center);
            require(radius >= tolerance && radius <= coordinateLimit &&
                        std::abs(s.center.x) + radius <= coordinateLimit &&
                        std::abs(s.center.y) + radius <= coordinateLimit,
                    "DXF arc exceeds supported extents");
        }
        entity.segments.push_back(s);
    }
    void entity(const Record &record, const std::vector<Record> &all, size_t &index) {
        const auto &t = record.tags;
        const auto &type = record.type;
        DxfEntity e{type, name(field(t, 8).value_or("0")), {}, false};
        if (!result.layers.contains(e.layer)) {
            require(result.layers.size() < 1024, "DXF exceeds 1024 layers");
            result.layers[e.layer] = {e.layer, false, false};
            loss("undeclaredLayers");
        }
        std::vector<Record> vertices;
        if (type == "POLYLINE") {
            while (index + 1 < all.size() && all[index + 1].type == "VERTEX")
                vertices.push_back(all[++index]);
            require(index + 1 < all.size() && all[index + 1].type == "SEQEND",
                    "DXF POLYLINE lacks SEQEND");
            ++index;
        }
        if (integer(t, 67, 0) != 0 || field(t, 410).value_or("Model") != "Model") {
            skip(type, "paperSpace");
            return;
        }
        if (type != "LINE" && type != "ARC" && type != "CIRCLE" && type != "LWPOLYLINE" &&
            type != "POLYLINE") {
            skip(type, "unsupportedType");
            return;
        }
        if (!planar(t)) {
            skip(type, "extrusionOrThickness");
            return;
        }
        if (field(t, 62) || field(t, 420) || field(t, 6) || field(t, 370))
            loss("entityAppearanceOmitted");
        if (std::any_of(t.begin(), t.end(),
                        [](const Tag &tag) { return tag.code == 102 || tag.code == 1001; }))
            loss("applicationMetadataOmitted");
        if (type == "LINE") {
            const auto a = point(t), b = point(t, 11, 21, 31);
            if (a.z != 0 || b.z != 0) {
                skip(type, "nonXY");
                return;
            }
            segment(e, a, b);
        } else if (type == "ARC" || type == "CIRCLE") {
            const auto center = point(t);
            if (center.z != 0) {
                skip(type, "nonXY");
                return;
            }
            const auto radius = number(t, 40) * scale;
            require(radius >= tolerance && std::abs(center.x) + radius <= coordinateLimit &&
                        std::abs(center.y) + radius <= coordinateLimit,
                    "DXF arc radius/extents invalid");
            double start{}, sweep = 2 * pi;
            if (type == "ARC") {
                const auto a = number(t, 50), b = number(t, 51);
                require(std::abs(a) <= 1e9 && std::abs(b) <= 1e9, "DXF angle out of bounds");
                start = std::fmod(a, 360) * pi / 180;
                double degrees = std::fmod(b - a, 360);
                if (degrees < 0)
                    degrees += 360;
                require(degrees > 0, "DXF ARC has zero sweep");
                sweep = degrees * pi / 180;
            }
            const auto at = [&](double angle) {
                return center + Vec3{std::cos(angle) * radius, std::sin(angle) * radius, 0};
            };
            require(++segments <= 100000, "DXF exceeds 100000 segments");
            e.segments.push_back({at(start), at(start + sweep), center, sweep});
            e.closed = type == "CIRCLE";
        } else {
            const int flags = integer(t, 70, 0);
            if (flags < 0 || (flags & ~129)) {
                skip(type, "polylineFlags");
                return;
            }
            e.closed = flags & 1;
            bool unsupported =
                number(t, 38, 0) != 0 || number(t, 30, 0) != 0 || number(t, 43, 0) != 0;
            struct Vertex {
                Vec3 p;
                double bulge{};
            };
            std::vector<Vertex> points;
            if (type == "LWPOLYLINE") {
                std::optional<Tags> current;
                auto add = [&] {
                    if (!current)
                        return;
                    const auto &v = *current;
                    unsupported |= number(v, 40, 0) != 0 || number(v, 41, 0) != 0;
                    points.push_back({point(v), number(v, 42, 0)});
                };
                for (const auto &tag : t) {
                    if (tag.code == 10) {
                        add();
                        current = Tags{tag};
                    } else if (tag.code == 20 || tag.code == 40 || tag.code == 41 ||
                               tag.code == 42 || tag.code == 91) {
                        require(current.has_value(), "DXF polyline vertex fields precede X");
                        current->push_back(tag);
                    }
                }
                add();
                require(integer(t, 90, -1) == int(points.size()),
                        "DXF polyline vertex count mismatch");
            } else {
                unsupported |= number(t, 40, 0) != 0 || number(t, 41, 0) != 0;
                for (const auto &v : vertices) {
                    unsupported |= integer(v.tags, 70, 0) != 0 || number(v.tags, 40, 0) != 0 ||
                                   number(v.tags, 41, 0) != 0;
                    points.push_back({point(v.tags), number(v.tags, 42, 0)});
                }
            }
            require(points.size() >= 2 && points.size() <= 100000,
                    "DXF polyline vertex count out of bounds");
            for (const auto &p : points)
                unsupported |= p.p.z != 0;
            if (unsupported) {
                skip(type, "widthElevationOrVertexFlags");
                return;
            }
            for (size_t i = 0; i + 1 < points.size(); ++i)
                segment(e, points[i].p, points[i + 1].p, points[i].bulge);
            if (e.closed)
                segment(e, points.back().p, points.front().p, points.back().bulge);
        }
        result.entities.push_back(std::move(e));
    }
};
// The bounded tokenizer and the header stage shared by the planar reader and the terrain
// mode: byte, line, pair and section budgets, version, units and the LAYER table.
struct Prepared {
    QString version;
    int units{};
};
Prepared prepare(Reader &reader, const QByteArray &bytes, std::optional<double> metresPerUnit) {
    require(!bytes.isEmpty() && bytes.size() <= 64 * 1024 * 1024 && !bytes.contains('\0'),
            "DXF requires bounded ASCII/UTF-8 text, not binary data");
    require(bytes.count('\n') <= 1000001, "DXF exceeds line budget");
    QStringDecoder decoder(QStringDecoder::Utf8);
    QString text = decoder(bytes);
    require(!decoder.hasError(), "DXF text must be valid UTF-8");
    if (text.startsWith(QChar(0xfeff)))
        text.remove(0, 1);
    const auto lines = text.split('\n');
    require(lines.size() <= 1000002, "DXF exceeds line budget");
    Tags tags;
    for (qsizetype i = 0; i < lines.size(); i += 2) {
        if (i == lines.size() - 1 && lines[i].trimmed().isEmpty())
            break;
        require(i + 1 < lines.size() && lines[i].size() <= 4096 && lines[i + 1].size() <= 4096,
                "DXF requires bounded group-code/value pairs");
        bool ok{};
        const int code = lines[i].trimmed().toInt(&ok);
        require(ok && code >= 0 && code <= 1071, "Invalid DXF group code");
        const auto value = lines[i + 1].trimmed();
        if (code != 999)
            tags.push_back({code, value});
    }
    std::optional<QString> section;
    bool ended = false;
    for (size_t i = 0; i < tags.size(); ++i) {
        const auto &tag = tags[i];
        require(!ended, "Data follows DXF EOF");
        if (tag.code == 0 && tag.value == "SECTION") {
            require(!section && i + 1 < tags.size() && tags[i + 1].code == 2,
                    "Invalid nested DXF SECTION");
            section = name(tags[++i].value);
            require(!reader.sections.contains(*section) && reader.sections.size() < 16,
                    "Duplicate or excessive DXF sections");
            reader.sections[*section] = {};
        } else if (tag.code == 0 && tag.value == "ENDSEC") {
            require(section.has_value(), "Unexpected DXF ENDSEC");
            section.reset();
        } else if (tag.code == 0 && tag.value == "EOF") {
            require(!section, "DXF EOF inside section");
            ended = true;
        } else {
            require(section.has_value(), "DXF data outside section");
            reader.sections[*section].push_back(tag);
        }
    }
    require(ended && !section && reader.sections.contains("ENTITIES"), "Incomplete DXF document");
    std::map<QString, Tags> header;
    QString variable;
    for (const auto &tag : reader.sections["HEADER"]) {
        if (tag.code == 9) {
            variable = tag.value;
            require(!header.contains(variable) && header.size() < 4096,
                    "Duplicate or excessive DXF header variables");
            header[variable] = {};
        } else {
            require(!variable.isEmpty(), "DXF header value lacks variable");
            header[variable].push_back(tag);
        }
    }
    Prepared prepared;
    prepared.version = field(header["$ACADVER"], 1).value_or("");
    const std::set<QString> versions{"AC1009", "AC1015", "AC1018", "AC1021",
                                     "AC1024", "AC1027", "AC1032"};
    require(versions.contains(prepared.version),
            "DXF version must be R12 or supported 2000..2018 ASCII format");
    prepared.units = integer(header["$INSUNITS"], 70, 0);
    const auto units = prepared.units;
    const std::map<int, double> scales{{1, .0254}, {2, .3048}, {4, .001}, {5, .01}, {6, 1}};
    if (metresPerUnit) {
        reader.scale = *metresPerUnit;
        require(std::isfinite(reader.scale) && reader.scale >= 1e-6 && reader.scale <= 1e6,
                "DXF unit override out of bounds");
        if (!scales.contains(units) || scales.at(units) != reader.scale)
            reader.loss("unitsOverridden");
    } else {
        require(scales.contains(units),
                "DXF units are missing, unitless or unsupported; choose explicit units");
        reader.scale = scales.at(units);
    }
    if (reader.sections.contains("TABLES")) {
        for (const auto &r : records(reader.sections["TABLES"]))
            if (r.type == "LAYER") {
                const auto layer = name(field(r.tags, 2).value_or(""));
                require(!reader.result.layers.contains(layer) && reader.result.layers.size() < 1024,
                        "Duplicate or excessive DXF layers");
                const int flags = integer(r.tags, 70, 0), color = integer(r.tags, 62, 7);
                reader.result.layers[layer] = {layer, (flags & 1) != 0 || color < 0,
                                               (flags & 4) != 0};
                reader.loss("layerAppearanceOmitted");
            }
    }
    for (const auto &[key, value] : reader.sections)
        if (key != "HEADER" && key != "TABLES" && key != "ENTITIES")
            reader.loss("sectionOmitted:" + key, int(value.size()));
    return prepared;
}
} // namespace
DxfSource parseDxf(const QByteArray &bytes, DxfOptions options) {
    Reader reader;
    const auto prepared = prepare(reader, bytes, options.metresPerUnit);
    const auto &version = prepared.version;
    const auto units = prepared.units;
    const auto entities = records(reader.sections.at("ENTITIES"));
    for (size_t i = 0; i < entities.size(); ++i)
        reader.entity(entities[i], entities, i);
    reader.result.metresPerUnit = reader.scale;
    reader.result.report = {
        {"apiVersion", 1},
        {"format", "dxf"},
        {"version", version},
        {"metresPerUnit", reader.scale},
        {"headerUnits", units},
        {"entities", qint64(reader.result.entities.size())},
        {"segments", qint64(reader.segments)},
        {"layers", qint64(reader.result.layers.size())},
        {"omittedEntities", reader.omitted},
        {"losses", reader.losses},
        {"bytes", qint64(bytes.size())},
        {"sha256",
         QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())}};
    return std::move(reader.result);
}
// R095.c terrain mode (ADR 0168 section 1). Same tokenizer, budgets and unit rules as the
// planar reader; coordinates stay in source units and elevations are kept.
TerrainSourceSet parseDxfTerrain(const QByteArray &bytes, const DxfTerrainOptions &options) {
    Reader reader;
    const auto prepared = prepare(
        reader, bytes,
        options.unit ? std::optional<double>(options.unit->metresPerUnit) : std::nullopt);
    require(std::isfinite(options.chordTolerance) && options.chordTolerance >= 1e-6 &&
                options.chordTolerance <= 100,
            "Chord tolerance must be 1e-6..100 m");
    TerrainSourceSet set;
    set.format = "dxf";
    if (options.unit)
        set.unit = *options.unit;
    else {
        const std::map<int, QString> names{{1, "in"}, {2, "ft"}, {4, "mm"}, {5, "cm"}, {6, "m"}};
        set.unit = *terrainUnit(names.at(prepared.units));
    }
    const double scale = set.unit.metresPerUnit;
    const double bound = TerrainSourceLimits::coordinate / scale;
    const double chord = options.chordTolerance / scale; // source units
    std::map<QString, std::uint32_t> layerIndex;
    auto layerOf = [&](const QString &layer) {
        const auto found = layerIndex.find(layer);
        if (found != layerIndex.end())
            return found->second;
        require(set.layers.size() < TerrainSourceLimits::layers, "DXF exceeds 1024 layers");
        const auto index = std::uint32_t(set.layers.size());
        set.layers.push_back({layer, 0, 0, 0, 0, 0});
        layerIndex[layer] = index;
        return index;
    };
    for (const auto &[layer, record] : reader.result.layers)
        layerOf(layer);
    QJsonObject omitted;
    auto skip = [&](const QString &type, const QString &reason) {
        const auto key = type + ":" + reason;
        require(omitted.contains(key) || omitted.size() < 128,
                "Too many distinct unsupported DXF entities");
        omitted[key] = omitted[key].toInt() + 1;
        set.problems.add(TerrainReport::Severity::Notice, "ignored-entities", 1,
                         "Entities that are not terrain sources were ignored; see "
                         "omittedEntities for the types.");
    };
    std::size_t vertices = 0;
    std::uint64_t arcs = 0;
    auto coordinate = [&](const Tags &tags, int code, std::optional<double> fallback = {}) {
        const auto value = number(tags, code, fallback);
        require(std::abs(value) <= bound, "DXF coordinate is beyond 100,000 km");
        return value;
    };
    const auto entities = records(reader.sections.at("ENTITIES"));
    std::uint64_t ordinal = 0;
    for (size_t index = 0; index < entities.size(); ++index) {
        const auto &record = entities[index];
        const auto &t = record.tags;
        const auto &type = record.type;
        ++ordinal;
        std::vector<Record> vertexRecords;
        if (type == "POLYLINE") {
            while (index + 1 < entities.size() && entities[index + 1].type == "VERTEX")
                vertexRecords.push_back(entities[++index]);
            require(index + 1 < entities.size() && entities[index + 1].type == "SEQEND",
                    "DXF POLYLINE lacks SEQEND");
            ++index;
            require(vertexRecords.size() <= 100000, "DXF polyline vertex count out of bounds");
        }
        if (integer(t, 67, 0) != 0 || field(t, 410).value_or("Model") != "Model") {
            skip(type, "paperSpace");
            continue;
        }
        if (type != "LINE" && type != "LWPOLYLINE" && type != "POLYLINE") {
            skip(type, "notTerrain");
            continue;
        }
        if (number(t, 210, 0) != 0 || number(t, 220, 0) != 0 || number(t, 230, 1) != 1) {
            skip(type, "extrusion");
            continue;
        }
        TerrainSourcePolyline polyline;
        polyline.ref = {TerrainSourceRef::Kind::Entity, ordinal};
        if (const auto handle = field(t, 5)) {
            bool ok{};
            const auto value = handle->toULongLong(&ok, 16);
            if (ok && !handle->isEmpty() && handle->size() <= 16)
                polyline.ref = {TerrainSourceRef::Kind::Handle, value};
        }
        polyline.layer = layerOf(name(field(t, 8).value_or("0")));
        struct Vertex {
            TerrainVec3 p;
            double bulge{};
        };
        std::vector<Vertex> points;
        if (type == "LINE") {
            const TerrainVec3 a{coordinate(t, 10), coordinate(t, 20), coordinate(t, 30, 0)};
            const TerrainVec3 b{coordinate(t, 11), coordinate(t, 21), coordinate(t, 31, 0)};
            if (a.z != b.z) {
                skip(type, "varyingZ");
                continue;
            }
            points = {{a, 0}, {b, 0}};
        } else if (type == "LWPOLYLINE") {
            const int flags = integer(t, 70, 0);
            if (flags < 0 || (flags & ~129)) {
                skip(type, "polylineFlags");
                continue;
            }
            polyline.closed = flags & 1;
            const double elevation = coordinate(t, 38, 0);
            std::optional<Tags> current;
            auto add = [&] {
                if (!current)
                    return;
                const auto &v = *current;
                points.push_back(
                    {{coordinate(v, 10), coordinate(v, 20), elevation}, number(v, 42, 0)});
            };
            for (const auto &tag : t) {
                if (tag.code == 10) {
                    add();
                    current = Tags{tag};
                } else if (tag.code == 20 || tag.code == 40 || tag.code == 41 || tag.code == 42 ||
                           tag.code == 91) {
                    require(current.has_value(), "DXF polyline vertex fields precede X");
                    current->push_back(tag);
                }
            }
            add();
            require(integer(t, 90, -1) == int(points.size()), "DXF polyline vertex count mismatch");
        } else {
            const int flags = integer(t, 70, 0);
            if (flags < 0 || (flags & (16 | 64)) || (flags & ~(1 | 2 | 4 | 8 | 32 | 128))) {
                skip(type, "meshOrFlags");
                continue;
            }
            polyline.closed = flags & 1;
            const bool threeD = flags & 8;
            const double elevation = coordinate(t, 30, 0);
            bool unsupported = false;
            for (const auto &v : vertexRecords) {
                const int vertexFlags = integer(v.tags, 70, 0);
                if (vertexFlags < 0 || (vertexFlags & (64 | 128))) {
                    unsupported = true;
                    break;
                }
                if (vertexFlags & 16) // spline frame control point, not on the curve
                    continue;
                double z = coordinate(v.tags, 30, 0);
                if (!threeD && z == 0)
                    z = elevation;
                points.push_back({{coordinate(v.tags, 10), coordinate(v.tags, 20), z},
                                  threeD ? 0 : number(v.tags, 42, 0)});
            }
            if (unsupported) {
                skip(type, "meshOrFlags");
                continue;
            }
        }
        require(points.size() >= 2 && points.size() <= 100000,
                "DXF polyline vertex count out of bounds");
        // Bulges become chords within the chord tolerance; Z varies linearly along the arc.
        const std::size_t count = points.size();
        const std::size_t spans = polyline.closed ? count : count - 1;
        polyline.points.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            polyline.points.push_back(points[i].p);
            require(++vertices <= TerrainSourceLimits::vertices,
                    "DXF terrain exceeds 1,000,000 vertices");
            const double bulge = points[i].bulge;
            if (i >= spans || bulge == 0)
                continue;
            require(std::isfinite(bulge) && std::abs(bulge) <= 1e6,
                    "DXF bulge exceeds supported range");
            const auto a = points[i].p, b = points[(i + 1) % count].p;
            const double dx = b.x - a.x, dy = b.y - a.y, distance = std::hypot(dx, dy);
            if (!(distance > 0))
                continue;
            const double sweep = 4 * std::atan(bulge);
            const double radius = distance / (2 * std::abs(std::sin(sweep / 2)));
            const double offset = distance * (1 - bulge * bulge) / (4 * bulge);
            const double cx = (a.x + b.x) / 2 - dy / distance * offset;
            const double cy = (a.y + b.y) / 2 + dx / distance * offset;
            require(std::isfinite(radius) && std::isfinite(cx) && std::isfinite(cy) &&
                        std::abs(cx) + radius <= bound && std::abs(cy) + radius <= bound,
                    "DXF arc exceeds supported extents");
            const double step =
                chord >= radius ? pi : 2 * std::acos(std::max(-1.0, 1 - chord / radius));
            const auto pieces = std::size_t(
                std::clamp(std::ceil(std::abs(sweep) / std::max(step, 1e-9)), 1.0, 4096.0));
            const double start = std::atan2(a.y - cy, a.x - cx);
            for (std::size_t k = 1; k < pieces; ++k) {
                const double f = double(k) / double(pieces);
                polyline.points.push_back({cx + radius * std::cos(start + sweep * f),
                                           cy + radius * std::sin(start + sweep * f),
                                           a.z + (b.z - a.z) * f});
                require(++vertices <= TerrainSourceLimits::vertices,
                        "DXF terrain exceeds 1,000,000 vertices");
            }
            ++arcs;
            set.problems.locate(TerrainReport::Severity::Notice, "arcs-segmented",
                                "Arc segments (bulges) were replaced by chords within the chord "
                                "tolerance.",
                                {polyline.ref.text(), {}, std::array<double, 2>{a.x, a.y}});
        }
        auto &layer = set.layers[polyline.layer];
        ++layer.polylines;
        layer.closed += polyline.closed;
        const bool constant =
            std::all_of(polyline.points.begin(), polyline.points.end(),
                        [&](const TerrainVec3 &p) { return p.z == polyline.points.front().z; });
        ++(constant ? layer.constantZ : layer.varyingZ);
        set.polylines.push_back(std::move(polyline));
    }
    set.sha256 =
        QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    set.report = {{"apiVersion", 1},
                  {"format", "dxf"},
                  {"mode", "terrain"},
                  {"version", prepared.version},
                  {"units", set.unit.name},
                  {"metresPerUnit", scale},
                  {"metresPerUnitExact", set.unit.exact},
                  {"headerUnits", prepared.units},
                  {"entities", qint64(ordinal)},
                  {"polylines", qint64(set.polylines.size())},
                  {"vertices", qint64(vertices)},
                  {"arcsSegmented", qint64(arcs)},
                  {"chordToleranceMetres", options.chordTolerance},
                  {"layers", qint64(set.layers.size())},
                  {"omittedEntities", omitted},
                  {"losses", reader.losses},
                  {"bytes", qint64(bytes.size())},
                  {"sha256", set.sha256}};
    return set;
}
} // namespace sketchy
