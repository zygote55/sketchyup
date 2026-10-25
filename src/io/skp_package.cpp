#include "io/skp_package.hpp"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonValue>
#include <QtEndian>
#include <cmath>
#include <cstring>
#include <set>
#include <stdexcept>

namespace sketchy {
namespace {
const QByteArray magic("SKSKPP01", 8);
constexpr std::uint32_t none = 0xFFFFFFFFu;

[[noreturn]] void fail(const std::string &message) {
    throw std::runtime_error("Invalid .skp conversion package: " + message);
}
void require(bool ok, const std::string &message) {
    if (!ok)
        fail(message);
}
double finite(const QJsonValue &value, const char *what) {
    if (!value.isDouble() || !std::isfinite(value.toDouble()))
        fail(std::string(what) + " must be a finite number");
    return value.toDouble();
}
std::int64_t integer(const QJsonValue &value, std::int64_t low, std::int64_t high,
                     const char *what) {
    const double v = finite(value, what);
    if (v != std::floor(v) || v < double(low) || v > double(high))
        fail(std::string(what) + " is out of range");
    return std::int64_t(v);
}
std::string text(const QJsonValue &value, std::size_t limit, const char *what) {
    if (!value.isString())
        fail(std::string(what) + " must be a string");
    const auto bytes = value.toString().toUtf8();
    if (std::size_t(bytes.size()) > limit)
        fail(std::string(what) + " is too long");
    return bytes.toStdString();
}
std::optional<std::string> optionalText(const QJsonValue &value, std::size_t limit,
                                        const char *what) {
    if (value.isNull())
        return std::nullopt;
    return text(value, limit, what);
}
QJsonObject object(const QJsonValue &value, const char *what) {
    if (!value.isObject())
        fail(std::string(what) + " must be an object");
    return value.toObject();
}
QJsonArray array(const QJsonValue &value, std::size_t limit, const char *what) {
    if (!value.isArray())
        fail(std::string(what) + " must be an array");
    const auto result = value.toArray();
    if (std::size_t(result.size()) > limit)
        fail(std::string(what) + " has too many entries");
    return result;
}
void onlyKeys(const QJsonObject &o, std::initializer_list<const char *> keys, const char *what) {
    std::set<QString> allowed;
    for (const auto *key : keys)
        allowed.insert(QString::fromLatin1(key));
    for (auto it = o.begin(); it != o.end(); ++it)
        if (!allowed.contains(it.key()))
            fail(std::string(what) + " has an unknown member");
    for (const auto *key : keys)
        if (!o.contains(QString::fromLatin1(key)))
            fail(std::string(what) + " is missing a member");
}
// As onlyKeys, but `optional` members are allowed to be absent (the rest stay required).
void onlyKeysOr(const QJsonObject &o, std::initializer_list<const char *> keys,
                std::initializer_list<const char *> optional, const char *what) {
    std::set<QString> allowed;
    for (const auto *key : keys)
        allowed.insert(QString::fromLatin1(key));
    for (const auto *key : optional)
        allowed.insert(QString::fromLatin1(key));
    for (auto it = o.begin(); it != o.end(); ++it)
        if (!allowed.contains(it.key()))
            fail(std::string(what) + " has an unknown member");
    for (const auto *key : keys)
        if (!o.contains(QString::fromLatin1(key)))
            fail(std::string(what) + " is missing a member");
}
std::uint32_t readU32(const std::uint8_t *&p, const std::uint8_t *end) {
    require(p + 4 <= end, "a chunk ends mid-field");
    const std::uint32_t v = std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
                          (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
    p += 4;
    return v;
}
std::uint64_t readU64(const std::uint8_t *&p, const std::uint8_t *end) {
    require(p + 8 <= end, "a chunk ends mid-field");
    std::uint64_t v = 0;
    for (int i = 7; i >= 0; --i)
        v = (v << 8) | p[i];
    p += 8;
    return v;
}
double readF64(const std::uint8_t *&p, const std::uint8_t *end) {
    const std::uint64_t bits = readU64(p, end);
    double v;
    std::memcpy(&v, &bits, sizeof v);
    return v;
}
std::uint8_t readFlag(const std::uint8_t *&p, const std::uint8_t *end) {
    require(p + 1 <= end, "a chunk ends mid-flag");
    const std::uint8_t v = *p;
    ++p;
    return v;
}
void coordinate(double v, const char *what) {
    require(std::isfinite(v) && std::abs(v) <= 1e6,
            std::string(what) + " is not finite or beyond 1e6 m");
}
// The image magic: the package holds the bytes as stored (R102.a; R102.b re-encodes to
// PNG / JPEG at the mapping stage and applies the 4096 x 4096 cap there).
bool knownImageMagic(const std::vector<std::uint8_t> &bytes) {
    if (bytes.size() >= 4 && bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G')
        return true;  // PNG
    if (bytes.size() >= 3 && bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF)
        return true;  // JPEG
    if (bytes.size() >= 2 && bytes[0] == 'B' && bytes[1] == 'M')
        return true;  // BMP
    return false;
}

}  // namespace

SkpPackage decodeSkpPackage(const std::vector<std::uint8_t> &bytes,
                           SkpPackageDecodeOptions options) {
    const std::uint64_t size = bytes.size();
    if (size < SkpPackageLimits::headerBytes || std::memcmp(bytes.data(), "SKSKPP01", 8) != 0)
        fail("bad header");
    const auto *data = bytes.data();
    const std::uint32_t manifestBytes = qFromLittleEndian<quint32>(data + 8);
    const std::uint32_t reserved = qFromLittleEndian<quint32>(data + 12);
    const std::uint64_t geometryBytes = qFromLittleEndian<quint64>(data + 16);
    const std::uint64_t textureBytes = qFromLittleEndian<quint64>(data + 24);
    require(reserved == 0, "reserved header word is not zero");
    require(std::uint64_t(manifestBytes) <= SkpPackageLimits::manifestBytes,
            "manifest exceeds 32 MiB");
    require(geometryBytes <= SkpPackageLimits::geometryBytes, "geometry chunk exceeds 256 MiB");
    require(textureBytes <= SkpPackageLimits::textureChunkBytes, "texture chunk exceeds 256 MiB");
    require(size == SkpPackageLimits::headerBytes + std::uint64_t(manifestBytes) + geometryBytes +
                      textureBytes,
            "length disagrees with the header");
    const auto *manifestData = data + SkpPackageLimits::headerBytes;
    const auto *geometryData = manifestData + manifestBytes;
    const auto *textureData = geometryData + geometryBytes;

    QJsonParseError parse;
    const auto json = QJsonDocument::fromJson(
        QByteArray(reinterpret_cast<const char *>(manifestData), int(manifestBytes)), &parse);
    require(parse.error == QJsonParseError::NoError && json.isObject(), "manifest is not a JSON object");
    const auto manifest = json.object();
    onlyKeys(manifest,
              {"format", "version", "geometrySha256", "textureSha256", "source", "formatVersion",
               "container", "units", "unitScale", "report", "definitions", "tags", "materials",
               "textures", "counts"},
              "manifest");
    require(text(manifest["format"], 64, "format") == "sketchyup-skp-package", "unknown format");
    require(integer(manifest["version"], 0, 1000, "version") == 1, "unsupported version");

    SkpPackage package;
    package.formatVersion = text(manifest["formatVersion"], SkpPackageLimits::nameBytes, "formatVersion");
    const auto container = text(manifest["container"], 16, "container");
    require(container == "vff" || container == "mfc", "container must be vff or mfc");
    package.container = container;
    package.units = optionalText(manifest["units"], SkpPackageLimits::nameBytes, "units").value_or("");
    const double unitScale = finite(manifest["unitScale"], "unitScale");
    require(unitScale > 0 && unitScale <= 1e6, "unitScale is outside 0..1e6");
    package.unitScale = unitScale;
    const auto source = object(manifest["source"], "source");
    onlyKeys(source, {"name", "size", "sha256"}, "source");
    package.sourceName = text(source["name"], SkpPackageLimits::nameBytes, "source name");
    package.sourceSize = std::uint64_t(integer(source["size"], 0, std::uint64_t{8} * 1024 * 1024 * 1024,
                                              "source size"));
    package.sourceSha256 = text(source["sha256"], 64, "source sha256");

    // The report block (the IFC report shape): bounded, then copied through.
    const auto report = object(manifest["report"], "report");
    onlyKeysOr(report, {"apiVersion", "counts", "notices", "locations", "locationTotals",
                       "omitted", "source"},
               {"bounding", "boundingInches"},
               "report");
    require(integer(report["apiVersion"], 1, 1, "report apiVersion") == 1,
            "unsupported report apiVersion");
    // The bounding boxes, when the model had a vertex: bounding (metres) and boundingInches
    // (the parser's raw inches) as a pair, min <= max on every axis.
    const bool hasMetres = report.contains("bounding");
    const bool hasInches = report.contains("boundingInches");
    require(hasMetres == hasInches, "bounding and boundingInches must come as a pair");
    if (hasMetres) {
        const auto bounding = object(report["bounding"], "bounding");
        onlyKeys(bounding, {"min", "max"}, "bounding");
        const auto min = array(bounding["min"], 3, "bounding min");
        const auto max = array(bounding["max"], 3, "bounding max");
        require(min.size() == 3 && max.size() == 3, "bounding box has too few axes");
        for (int axis = 0; axis < 3; ++axis) {
            const double lo = finite(min[axis], "bounding min");
            const double hi = finite(max[axis], "bounding max");
            require(lo <= hi, "bounding min exceeds max");
            package.boundingMin[axis] = lo;
            package.boundingMax[axis] = hi;
        }
        const auto inches = object(report["boundingInches"], "boundingInches");
        onlyKeys(inches, {"min", "max"}, "boundingInches");
        const auto iMin = array(inches["min"], 3, "boundingInches min");
        const auto iMax = array(inches["max"], 3, "boundingInches max");
        require(iMin.size() == 3 && iMax.size() == 3, "boundingInches has too few axes");
        for (int axis = 0; axis < 3; ++axis) {
            const double lo = finite(iMin[axis], "boundingInches min");
            const double hi = finite(iMax[axis], "boundingInches max");
            require(lo <= hi, "boundingInches min exceeds max");
            package.boundingMinInches[axis] = lo;
            package.boundingMaxInches[axis] = hi;
        }
        package.hasBounding = true;
    }
    const auto counts = object(report["counts"], "report counts");
    onlyKeys(counts,
              {"definitions", "vertices", "edges", "faces", "holes", "instances", "materials",
               "tags", "uvTransforms", "textures"},
              "report counts");
    auto countOf = [&](const char *key, std::int64_t high) {
        return integer(counts[QString::fromLatin1(key)], 0, high, key);
    };
    package.counts.definitions = countOf("definitions", std::int64_t(SkpPackageLimits::definitions));
    package.counts.vertices = countOf("vertices", std::int64_t(SkpPackageLimits::vertices));
    package.counts.edges = countOf("edges", std::int64_t(SkpPackageLimits::edges));
    package.counts.faces = countOf("faces", std::int64_t(SkpPackageLimits::faces));
    package.counts.holes = countOf("holes", std::int64_t(SkpPackageLimits::faces) * 8);
    package.counts.instances = countOf("instances", std::int64_t(SkpPackageLimits::instances));
    package.counts.materials = countOf("materials", std::int64_t(SkpPackageLimits::materials));
    package.counts.tags = countOf("tags", std::int64_t(SkpPackageLimits::tags));
    package.counts.uvTransforms =
        countOf("uvTransforms", std::int64_t(SkpPackageLimits::faces) * 2);
    package.counts.textures = countOf("textures", std::int64_t(SkpPackageLimits::textures));
    for (const auto &entry : array(report["notices"], SkpPackageLimits::noticeCodes, "notices")) {
        const auto n = object(entry, "notice");
        onlyKeys(n, {"code", "count"}, "notice");
        package.notices[text(n["code"], SkpPackageLimits::stringBytes, "notice code")] =
            integer(n["count"], 1, 100000000, "notice count");
    }
    for (const auto &entry :
         array(report["locations"], SkpPackageLimits::locationsPerKind * 8, "locations")) {
        const auto l = object(entry, "location");
        onlyKeys(l, {"kind", "definition", "message"}, "location");
        SkpPackageLocation place;
        place.kind = text(l["kind"], 64, "location kind");
        place.definition = text(l["definition"], 64, "location definition");
        place.message = text(l["message"], 256, "location message");
        package.locations.push_back(std::move(place));
    }
    const auto totals = object(report["locationTotals"], "location totals");
    std::size_t storedKinds = 0;
    for (auto it = totals.begin(); it != totals.end(); ++it) {
        require(++storedKinds <= 8, "too many location kinds");
        package.locationTotals[it.key().toStdString()] =
            int(integer(it.value(), 0, 1000000, "location total"));
    }
    const auto omittedEntries = object(report["omitted"], "report omitted");
    std::size_t omittedKinds = 0;
    for (auto it = omittedEntries.begin(); it != omittedEntries.end(); ++it) {
        require(++omittedKinds <= 16, "too many omitted kinds");
        package.omitted[it.key().toStdString()] =
            integer(it.value(), 0, 100000000, "omitted count");
    }
    const auto sourceEntries = object(report["source"], "report source");
    std::size_t sourceKinds = 0;
    for (auto it = sourceEntries.begin(); it != sourceEntries.end(); ++it) {
        require(++sourceKinds <= 8, "too many source kinds");
        package.sourceTotals[it.key().toStdString()] =
            integer(it.value(), 0, 100000000, "source count");
    }
    // The accounting identity: imported + omitted = the file's count, for every kind the report
    // carries a total for.
    const auto identityOf = [](long long imported, long long omitted, long long source) {
        return imported >= 0 && omitted >= 0 && imported + omitted == source;
    };
    if (package.sourceTotals.count("edges"))
        require(identityOf(package.counts.edges, package.omitted["edge"], package.sourceTotals["edges"]),
                "the edge accounting does not add up");
    if (package.sourceTotals.count("faces"))
        require(identityOf(package.counts.faces, package.omitted["face"], package.sourceTotals["faces"]),
                "the face accounting does not add up");
    if (package.sourceTotals.count("instances"))
        require(identityOf(package.counts.instances, package.omitted["instance"],
                          package.sourceTotals["instances"]),
                "the instance accounting does not add up");
    if (package.sourceTotals.count("materials"))
        require(identityOf(package.counts.materials, package.omitted["material"],
                          package.sourceTotals["materials"]),
                "the material accounting does not add up");
    // The stored locations never exceed the totals.
    for (const auto &place : package.locations)
        require(package.locationTotals[place.kind] >= 1, "a location kind has no total");
    std::map<std::string, int> storedPerKind;
    for (const auto &place : package.locations)
        require(++storedPerKind[place.kind] <= SkpPackageLimits::locationsPerKind,
                "a location kind has more than 32 stored locations");

    // The manifest's counts agree with its arrays.
    const auto textureEntries = array(manifest["textures"], SkpPackageLimits::textures, "textures");
    require(std::size_t(package.counts.textures) == textureEntries.size(),
            "texture count disagrees with the manifest");
    const auto materialEntries =
        array(manifest["materials"], SkpPackageLimits::materials, "materials");
    require(std::size_t(package.counts.materials) == materialEntries.size(),
            "material count disagrees with the manifest");
    const auto tagEntries = array(manifest["tags"], SkpPackageLimits::tags, "tags");
    require(std::size_t(package.counts.tags) == tagEntries.size(),
            "tag count disagrees with the manifest");
    const auto definitionEntries =
        array(manifest["definitions"], SkpPackageLimits::definitions, "definitions");
    require(std::size_t(package.counts.definitions) == definitionEntries.size(),
            "definition count disagrees with the manifest");
    const auto manifestCounts = object(manifest["counts"], "counts");
    onlyKeys(manifestCounts,
              {"definitions", "vertices", "edges", "faces", "holes", "instances", "materials",
               "tags", "uvTransforms", "textures"},
              "counts");
    require(manifestCounts["definitions"].toDouble() == double(package.counts.definitions) &&
                manifestCounts["vertices"].toDouble() == double(package.counts.vertices),
            "counts disagree with the report");

    // Textures: read from the chunk, in the manifest's order, every image checked.
    const auto *textureCursor = textureData;
    const auto *textureEnd = textureData + textureBytes;
    for (const auto &entry : textureEntries) {
        const auto t = object(entry, "texture");
        onlyKeys(t, {"filename", "width", "height", "bytes"}, "texture");
        const auto imageBytes = integer(t["bytes"], 0, SkpPackageLimits::perTextureBytes,
                                       "texture bytes");
        require(std::size_t(imageBytes) <= std::size_t(textureEnd - textureCursor),
                "texture chunk is shorter than its images");
        SkpPackageTexture texture;
        texture.filename = text(t["filename"], SkpPackageLimits::nameBytes, "texture filename");
        texture.width = finite(t["width"], "texture width");
        texture.height = finite(t["height"], "texture height");
        require(texture.width >= 0 && texture.width <= 1e6 && texture.height >= 0 &&
                    texture.height <= 1e6,
                "texture size is out of range");
        texture.bytes.assign(textureCursor, textureCursor + std::size_t(imageBytes));
        textureCursor += imageBytes;
        if (options.checkTextureSize)
            require(knownImageMagic(texture.bytes), "texture is not PNG, JPEG or BMP bytes");
        package.textures.push_back(std::move(texture));
    }
    require(textureCursor == textureEnd, "texture chunk length disagrees with its images");

    // Materials.
    for (const auto &entry : materialEntries) {
        const auto m = object(entry, "material");
        onlyKeys(m, {"name", "color", "opacity", "texture"}, "material");
        SkpPackageMaterial material;
        material.name = text(m["name"], SkpPackageLimits::nameBytes, "material name");
        const auto color = array(m["color"], 4, "material colour");
        require(color.size() == 4, "material colour needs four components");
        for (int i = 0; i < 4; ++i) {
            const double v = finite(color[i], "material colour");
            require(v >= 0 && v <= 255, "material colour is outside 0..255");
            material.color[std::size_t(i)] = std::uint8_t(v);
        }
        const double opacity = finite(m["opacity"], "material opacity");
        require(opacity >= 0 && opacity <= 1, "material opacity is outside 0..1");
        material.opacity = opacity;
        material.texture = integer(m["texture"], -1, std::int64_t(package.textures.size()),
                                   "material texture index");
        package.materials.push_back(std::move(material));
    }

    // Tags.
    for (const auto &entry : tagEntries) {
        const auto t = object(entry, "tag");
        onlyKeys(t, {"name", "color", "hidden"}, "tag");
        SkpPackageTag tag;
        tag.name = text(t["name"], SkpPackageLimits::nameBytes, "tag name");
        const auto color = array(t["color"], 3, "tag colour");
        require(color.size() == 3, "tag colour needs three components");
        for (int i = 0; i < 3; ++i) {
            const double v = finite(color[i], "tag colour");
            require(v >= 0 && v <= 255, "tag colour is outside 0..255");
            tag.color[std::size_t(i)] = std::uint8_t(v);
        }
        require(t["hidden"].isBool(), "tag hidden flag must be a boolean");
        tag.hidden = t["hidden"].toBool();
        package.tags.push_back(std::move(tag));
    }

    // The digest of the two chunks.
    const auto geometryDigest = text(manifest["geometrySha256"], 64, "geometrySha256");
    const auto textureDigest = text(manifest["textureSha256"], 64, "textureSha256");
    if (options.checkDigest) {
        require(QCryptographicHash::hash(
                    QByteArray(reinterpret_cast<const char *>(geometryData), int(geometryBytes)),
                    QCryptographicHash::Sha256)
                    .toHex()
                    .toStdString() == geometryDigest,
                "geometry chunk SHA-256 does not match");
        require(QCryptographicHash::hash(
                    QByteArray(reinterpret_cast<const char *>(textureData), int(textureBytes)),
                    QCryptographicHash::Sha256)
                    .toHex()
                    .toStdString() == textureDigest,
                "texture chunk SHA-256 does not match");
    }

    // The geometry chunk: self-describing, walked with every count checked.
    const auto *cursor = geometryData;
    const auto *end = geometryData + geometryBytes;
    long long verticesTotal = 0, edgesTotal = 0, facesTotal = 0, instancesTotal = 0,
        holesTotal = 0, uvTotal = 0;
    for (std::size_t i = 0; i < definitionEntries.size(); ++i) {
        const auto d = object(definitionEntries[i], "definition");
        onlyKeys(d, {"name", "guid", "vertices", "edges", "faces", "instances"}, "definition");
        const auto expectedVertices =
            std::uint64_t(integer(d["vertices"], 0, SkpPackageLimits::vertices, "definition vertices"));
        const auto expectedEdges =
            std::uint64_t(integer(d["edges"], 0, SkpPackageLimits::edges, "definition edges"));
        const auto expectedFaces =
            std::uint64_t(integer(d["faces"], 0, SkpPackageLimits::faces, "definition faces"));
        const auto expectedInstances = std::uint64_t(
            integer(d["instances"], 0, SkpPackageLimits::instances, "definition instances"));

        SkpPackageDefinition definition;
        definition.name = text(d["name"], SkpPackageLimits::nameBytes, "definition name");
        definition.guid = text(d["guid"], SkpPackageLimits::stringBytes, "definition guid");

        const auto vertexCount = readU32(cursor, end);
        require(vertexCount == expectedVertices, "vertex count disagrees with the manifest");
        verticesTotal += vertexCount;
        require(std::uint64_t(verticesTotal) <= SkpPackageLimits::vertices,
                "vertices exceed the limit");
        definition.vertices.reserve(vertexCount);
        for (std::uint64_t v = 0; v < vertexCount; ++v) {
            std::array<double, 3> point{readF64(cursor, end), readF64(cursor, end),
                                       readF64(cursor, end)};
            for (const auto p : point)
                coordinate(p, "vertex coordinate");
            definition.vertices.push_back(point);
        }

        const auto edgeCount = readU32(cursor, end);
        require(edgeCount == expectedEdges, "edge count disagrees with the manifest");
        edgesTotal += edgeCount;
        require(std::uint64_t(edgesTotal) <= SkpPackageLimits::edges, "edges exceed the limit");
        definition.edges.reserve(edgeCount);
        for (std::uint64_t e = 0; e < edgeCount; ++e) {
            SkpPackageEdge edge;
            edge.v1 = readU32(cursor, end);
            edge.v2 = readU32(cursor, end);
            require(edge.v1 < vertexCount && edge.v2 < vertexCount,
                    "edge index is out of range");
            const auto flags = readFlag(cursor, end);
            edge.soft = flags & 0x01;
            edge.smooth = flags & 0x02;
            edge.hidden = flags & 0x04;
            edge.wire = flags & 0x08;
            definition.edges.push_back(edge);
        }

        const auto faceCount = readU32(cursor, end);
        require(faceCount == expectedFaces, "face count disagrees with the manifest");
        facesTotal += faceCount;
        require(std::uint64_t(facesTotal) <= SkpPackageLimits::faces, "faces exceed the limit");
        definition.faces.reserve(faceCount);
        for (std::uint64_t f = 0; f < faceCount; ++f) {
            SkpPackageFace face;
            const auto flags = readFlag(cursor, end);
            face.hidden = flags & 0x01;
            face.hasUvFront = flags & 0x02;
            face.hasUvBack = flags & 0x04;
            face.uvFrontProjected = flags & 0x08;
            face.uvBackProjected = flags & 0x10;
            face.materialFront = static_cast<long long>(readU32(cursor, end));
            face.materialFront = face.materialFront == none ? -1 : face.materialFront;
            require(face.materialFront == -1 || face.materialFront  < static_cast<long long>(package.materials.size()),
                    "face material index is out of range");
            face.materialBack = static_cast<long long>(readU32(cursor, end));
            face.materialBack = face.materialBack == none ? -1 : face.materialBack;
            require(face.materialBack == -1 || face.materialBack  < static_cast<long long>(package.materials.size()),
                    "face material index is out of range");
            face.tag = static_cast<long long>(readU32(cursor, end));
            face.tag = face.tag == none ? -1 : face.tag;
            require(face.tag == -1 || face.tag  < static_cast<long long>(package.tags.size()),
                    "face tag index is out of range");
            const auto loopCount = readU32(cursor, end);
            require(loopCount >= 1 && loopCount <= SkpPackageLimits::loopsPerFace,
                    "face loop count is out of range");
            for (std::uint64_t l = 0; l < loopCount; ++l) {
                const auto loopSize = readU32(cursor, end);
                require(loopSize >= 3 && loopSize <= SkpPackageLimits::loopVertices,
                        "face loop is out of range");
                if (l > 0)
                    holesTotal++;
                std::vector<std::uint32_t> loop;
                loop.reserve(loopSize);
                for (std::uint64_t v = 0; v < loopSize; ++v) {
                    const auto index = readU32(cursor, end);
                    require(index < vertexCount, "face loop index is out of range");
                    loop.push_back(index);
                }
                face.loops.push_back(std::move(loop));
            }
            if (face.hasUvFront) {
                uvTotal++;
                for (auto &v : face.uvFront) {
                    v = readF64(cursor, end);
                    coordinate(v, "UV transform value");
                }
            }
            if (face.hasUvBack) {
                uvTotal++;
                for (auto &v : face.uvBack) {
                    v = readF64(cursor, end);
                    coordinate(v, "UV transform value");
                }
            }
            definition.faces.push_back(std::move(face));
        }

        const auto instanceCount = readU32(cursor, end);
        require(instanceCount == expectedInstances, "instance count disagrees with the manifest");
        instancesTotal += instanceCount;
        require(std::uint64_t(instancesTotal) <= SkpPackageLimits::instances,
                "instances exceed the limit");
        definition.instances.reserve(instanceCount);
        for (std::uint64_t n = 0; n < instanceCount; ++n) {
            SkpPackageInstance instance;
            instance.definition = static_cast<long long>(readU32(cursor, end));
            instance.definition = instance.definition == none ? -1 : instance.definition;
            require(instance.definition == -1 ||
                        instance.definition  < static_cast<long long>(definitionEntries.size()),
                    "instance definition index is out of range");
            const auto flags = readFlag(cursor, end);
            instance.hidden = flags & 0x01;
            instance.tag = static_cast<long long>(readU32(cursor, end));
            instance.tag = instance.tag == none ? -1 : instance.tag;
            require(instance.tag == -1 || instance.tag  < static_cast<long long>(package.tags.size()),
                    "instance tag index is out of range");
            instance.material = static_cast<long long>(readU32(cursor, end));
            instance.material = instance.material == none ? -1 : instance.material;
            require(instance.material == -1 ||
                        instance.material  < static_cast<long long>(package.materials.size()),
                    "instance material index is out of range");
            for (auto &v : instance.transform) {
                v = readF64(cursor, end);
                coordinate(v, "instance transform value");
            }
            require(instance.transform[15] == 1.0, "instance transform w is not 1");
            definition.instances.push_back(std::move(instance));
        }

        package.definitions.push_back(std::move(definition));
    }
    require(cursor == end, "geometry chunk length disagrees with its definitions");
    require(verticesTotal == package.counts.vertices && edgesTotal == package.counts.edges &&
                facesTotal == package.counts.faces && instancesTotal == package.counts.instances,
            "counts disagree with the chunk");
    require(holesTotal == package.counts.holes, "hole count disagrees with the manifest");
    require(uvTotal == package.counts.uvTransforms, "uv count disagrees with the manifest");
    return package;
}

}  // namespace sketchy
