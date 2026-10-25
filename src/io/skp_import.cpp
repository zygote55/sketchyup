#include "io/skp_import.hpp"

#include "core/asset_records.hpp"
#include "core/component_records.hpp"
#include "core/document_limits.hpp"
#include "core/material_records.hpp"
#include "core/tag_records.hpp"
#include "core/texture_mapping.hpp"
#include "geometry/surface.hpp"
#include "geometry/topology.hpp"
#include "io/texture_image.hpp"

#include <QBuffer>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

namespace sketchy {
namespace {

constexpr std::size_t nameBytes = 1024;     // the native cap on body, definition and tag names (model.cpp, component_records.cpp, tag_records.cpp); ADR 0176 section 4 said 2,048
constexpr std::size_t materialNameBytes = 1024;  // the native material cap
constexpr std::size_t materialLimit = 1024;      // validateMaterialRecords
constexpr std::size_t maxNesting = 64;          // a placement's depth (a guard against cycles)
constexpr std::size_t locationsPerKind = 32;    // ADR 0176 section 5
constexpr std::size_t textureDimensionLimit = 4096;
constexpr double metresPerInch = 0.0254;
constexpr double tileEpsilon = 1e-9;
constexpr double vertical = 1e-3;  // the exporter's face-frame tolerance (face_groups.cpp)

using PairKey = std::pair<std::uint32_t, std::uint32_t>;

PairKey pairKey(std::uint32_t a, std::uint32_t b) { return a < b ? PairKey{a, b} : PairKey{b, a}; }

// A UTF-8 string cut to at most `bytes` bytes, never inside a code point.
std::string clipUtf8(const std::string &text, std::size_t bytes) {
    if (text.size() <= bytes)
        return text;
    std::size_t cut = bytes;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80)
        --cut;
    return text.substr(0, cut);
}

// A name from the file: control characters become spaces, and the text is clipped.
std::string plainText(const std::string &text, std::size_t bytes) {
    std::string out = text;
    for (auto &c : out)
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f)
            c = ' ';
    return clipUtf8(out, bytes);
}

Vec3 point(const std::array<double, 3> &p) { return {p[0], p[1], p[2]}; }

// SketchUp's placement, as the native transform (column-major, the translation in m[12..14]).
// The package keeps the 3x3 row-major in the top-left and the translation in the bottom row (values
// 12 to 14), so the 3x3 is transposed into the native columns. Returns false when it is not a
// usable transform (non-finite, or singular).
bool placementOf(const SkpPackageInstance &instance, Transform &out) {
    Transform t;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            t.m[std::size_t(c * 4 + r)] = instance.transform[std::size_t(r * 4 + c)];
    for (int r = 0; r < 3; ++r)
        t.m[std::size_t(12 + r)] = instance.transform[std::size_t(12 + r)];
    t.m[3] = t.m[7] = t.m[11] = 0;
    t.m[15] = 1;
    try {
        t.validate();
        if (std::abs(t.determinant()) < 1e-12)
            return false;
    } catch (const std::exception &) {
        return false;
    }
    out = t;
    return true;
}

// The exporter's face-frame basis (openskp face_groups.cpp, face_uv_basis) for a unit normal.
std::pair<Vec3, Vec3> faceUvBasis(Vec3 n) {
    const double cx = -n.y, cy = n.x;
    const double clen = std::sqrt(cx * cx + cy * cy);
    if (clen < vertical)
        return {Vec3{n.z >= 0 ? 1.0 : -1.0, 0, 0}, Vec3{0, 1, 0}};
    const Vec3 xr{cx / clen, cy / clen, 0};
    const Vec3 yr{n.y * xr.z - n.z * xr.y, n.z * xr.x - n.x * xr.z, n.x * xr.y - n.y * xr.x};
    return {xr, yr};
}

// The inverse of a row-major 3x3 (the exporter's invert_3x3), or nothing when it is singular.
std::optional<std::array<double, 9>> invert3x3(const std::array<double, 9> &m) {
    const double a = m[0], b = m[1], c = m[2], d = m[3], e = m[4], f = m[5], g = m[6], h = m[7], i = m[8];
    const double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (!std::isfinite(det) || std::abs(det) < 1e-12)
        return std::nullopt;
    const double k = 1.0 / det;
    return std::array<double, 9>{(e * i - f * h) * k, (c * h - b * i) * k, (b * f - c * e) * k,
                                 (f * g - d * i) * k, (a * i - c * g) * k, (c * d - a * f) * k,
                                 (d * h - e * g) * k, (b * g - a * h) * k, (a * e - b * d) * k};
}

// The UV mapping of one face side. The stored 3x3 maps texture space to the face's inches (the
// exporter's compute_face_uv): u = (x * inv[0] + y * inv[3] + inv[6]) / tileW, and v likewise
// with tileH. Each texture repeat is tileW by tileH inches, so the native gradients are repeats per
// metre. Returns nothing for a perspective matrix (the third column is not (0, 0, 1)).
std::optional<TextureMapping> storedMapping(const std::array<double, 9> &stored, Vec3 normal, double tileW,
                                            double tileH) {
    if (std::abs(stored[2]) > 1e-9 || std::abs(stored[5]) > 1e-9 || std::abs(stored[8] - 1.0) > 1e-9)
        return std::nullopt;
    const auto inv = invert3x3(stored);
    if (!inv)
        return std::nullopt;
    const auto [xr, yr] = faceUvBasis(normal);
    TextureMapping mapping;
    const Vec3 uDirection = xr * (*inv)[0] + yr * (*inv)[3];
    const Vec3 vDirection = xr * (*inv)[1] + yr * (*inv)[4];
    mapping.uGradient = uDirection * (1.0 / (metresPerInch * tileW));
    mapping.vGradient = vDirection * (1.0 / (metresPerInch * tileH));
    mapping.offset = {(*inv)[6] / tileW, (*inv)[7] / tileH};
    return mapping;
}

// The default projection (no stored transform): one tile per texture size, from the face frame.
TextureMapping defaultMapping(Vec3 normal, double tileW, double tileH) {
    const auto [xr, yr] = faceUvBasis(normal);
    TextureMapping mapping;
    mapping.uGradient = xr * (1.0 / (metresPerInch * tileW));
    mapping.vGradient = yr * (1.0 / (metresPerInch * tileH));
    return mapping;
}

// A loop as the exporter reads it (openskp's loop_vertices): a vertex repeated in a row is one vertex, and
// a closing vertex repeated at the end is dropped. The native validator refuses a repeated loop vertex.
std::vector<std::uint32_t> cleanLoop(const std::vector<std::uint32_t> &loop) {
    std::vector<std::uint32_t> out;
    for (const auto v : loop)
        if (out.empty() || out.back() != v)
            out.push_back(v);
    while (out.size() > 1 && out.front() == out.back())
        out.pop_back();
    return out;
}

// The layers SketchUp's default layer goes by: they import as the untagged tag.
bool isDefaultLayer(const std::string &name) { return name == "Layer0" || name == "Untagged"; }

// The display units of the document, from the file's unit system ("" when the file records none).
DisplayUnit displayUnitsOf(const std::string &units, bool &known) {
    std::string folded;
    for (const char c : units)
        folded.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    known = true;
    if (folded.find("milli") != std::string::npos)
        return DisplayUnit::Millimeters;
    if (folded.find("centi") != std::string::npos || folded.find("meter") != std::string::npos ||
        folded.find("metre") != std::string::npos)
        return DisplayUnit::Meters;
    if (folded.find("inch") != std::string::npos || folded.find("foot") != std::string::npos ||
        folded.find("feet") != std::string::npos)
        return DisplayUnit::FeetInches;
    known = folded.empty();
    return DisplayUnit::Meters;
}

// What a definition's canonical geometry costs when it is placed, for the materialized caps.
struct Cost {
    std::size_t bodies{}, vertices{}, faces{}, edges{}, wires{};
};

// One canonical definition: its members (member 1 is the root), its nested placements
// (references) and the native counts of its own geometry.
struct Canonical {
    std::map<Id, BodyPtr> members;  // member id -> the body (member 1 is the root)
    std::map<Id, Id> references;    // a nested placement's member -> the definition it places
    Id nextMember{2};
    Cost own;                       // this definition's own geometry, not its nested placements
};

// The instance's attributes as the body's properties skp.<dictionary>.<key> (ADR 0176 section 4): a string,
// a number or a boolean. The native caps hold: 128 properties a body, keys of at most 128 bytes and strings
// of at most 2,048 bytes; what does not fit is dropped and counted (propertiesDropped).
void addAttributeProperties(std::map<std::string, std::variant<bool, double, std::string>> &properties,
                            const SkpPackageInstance &instance, std::map<std::string, long long> &notices) {
    for (const auto &attribute : instance.attributes) {
        std::string key = "skp." + attribute.dictionary + "." + attribute.key;
        if (key.size() > 128 || properties.size() >= 128) {
            notices["propertiesDropped"] += 1;
            continue;
        }
        std::variant<bool, double, std::string> value;
        switch (attribute.kind) {
        case SkpPackageAttribute::Kind::String:
            value = std::string(clipUtf8(attribute.text, 2048));
            break;
        case SkpPackageAttribute::Kind::Number:
            value = attribute.number;
            break;
        case SkpPackageAttribute::Kind::Boolean:
            value = attribute.flag;
            break;
        }
        properties.emplace(std::move(key), std::move(value));
    }
}

// One body under construction: the faces with one tag and one visibility.
struct Bucket {
    Body body;
    std::map<std::uint32_t, Id> vertexIds;  // package vertex -> the body's vertex
    std::map<Id, std::uint32_t> packageOf;  // the reverse
    std::set<PairKey> edgeKeys;             // the package edges this body's faces use
};

class Converter {
  public:
    Converter(const SkpPackage &package, const SkpConversionHooks &hooks) : pkg(package), hooks(hooks) {
        // The one undoable entry of the whole import (ADR 0176 section 4).
        edit.label = "Import SketchUp";
    }
    SkpImport run();

  private:
    void checkCancel() const {
        if (hooks.cancelled && hooks.cancelled())
            throw IfcImportError("cancelled", "The import was cancelled.");
    }
    void note(const std::string &code, long long count = 1) { notices[code] += count; }
    // A body or definition name capped at the native 1,024 bytes; a name that was cut is a truncatedNames
    // notice (ADR 0176 section 4).
    std::string capName(const std::string &text) {
        if (text.size() > nameBytes)
            note("truncatedNames");
        return plainText(text, nameBytes);
    }
    void locate(const std::string &kind, const std::string &definition, const std::string &message);
    void omit(const std::string &kind, long long count = 1) { omitted[kind] += count; }

    void buildTags();
    void buildMaterials();
    Id textureAsset(long long index);
    Id tagOf(long long index) const;
    Id materialOf(long long index) const;

    Canonical buildDefinition(std::size_t d);
    bool faceIsValid(const SkpPackageDefinition &definition, const std::vector<std::vector<std::uint32_t>> &loops, std::string &problem) const;
    Id vertexOf(Bucket &bucket, const SkpPackageDefinition &definition, std::uint32_t index) const;
    Cost costOf(std::size_t d, std::size_t depth, bool &tooDeep) const;
    void place(std::size_t d, Id rootId, std::size_t depth);
    void placeTopLevel(std::size_t k, Id parent);

    const SkpPackage &pkg;
    SkpConversionHooks hooks;
    Edit edit;
    Id nextBody{1}, nextTag{1}, nextMaterial{1}, nextAsset{1};
    std::vector<Canonical> canonical;
    std::vector<TagPtr> tagRecords;
    std::map<std::size_t, Id> tagIds;  // package layer index -> the native tag (0: untagged)
    std::vector<MaterialPtr> materialRecords;
    std::vector<Id> materialIds;       // package material index -> the native material (0: none)
    std::vector<std::optional<std::pair<double, double>>> textureTiles;  // per package material
    std::map<long long, Id> assetIds;  // package texture index -> the asset (0: refused)
    std::size_t assetBytes{};
    AssetRecords assetRecords;
    std::map<std::string, long long> notices;
    std::map<std::string, long long> omitted;
    std::map<std::string, std::vector<std::string>> locations;
    std::map<std::string, long long> locationTotals;
    LimitCounts unique;        // the canonical geometry the document will hold
    LimitCounts materialized;  // every placed copy
    long long importedFaces{}, importedHoles{}, importedEdges{}, importedWires{}, importedInstances{},
        importedDefinitions{}, placements{}, invalidFaces{}, invalidHoles{}, degenerateEdges{};
    long long nativeEdges{};   // the topology edges of every body (a shared edge counts per body)
};

void Converter::locate(const std::string &kind, const std::string &definition, const std::string &message) {
    auto &list = locations[kind];
    locationTotals[kind]++;
    if (list.size() < locationsPerKind) {
        const std::string text = clipUtf8(definition + ": " + message, 256);
        list.push_back(text);
    }
}

void Converter::buildTags() {
    std::set<std::string> names;
    for (std::size_t i = 0; i < pkg.tags.size(); ++i) {
        const auto &tag = pkg.tags[i];
        const std::string name = plainText(tag.name, nameBytes);
        if (isDefaultLayer(name)) {
            // The default layer is the untagged tag, which has no colour, visibility or name of its own.
            tagIds[i] = 0;
            if (tag.hidden)
                note("untaggedHidden");
            continue;
        }
        auto record = std::make_shared<TagRecord>();
        record->id = nextTag++;
        std::string unique = name.empty() ? "Layer " + std::to_string(record->id) : name;
        const std::string original = unique;
        for (std::size_t suffix = 2; !names.insert(unique).second; ++suffix)
            unique = original + " (" + std::to_string(suffix) + ")";
        if (unique != name)
            note("duplicateTagNamesRenamed");
        if (name != tag.name)
            note("truncatedNames");
        record->name = unique;
        record->visible = !tag.hidden;
        tagRecords.push_back(record);
        tagIds[i] = record->id;
    }
}

Id Converter::tagOf(long long index) const {
    if (index < 0)
        return 0;
    const auto it = tagIds.find(std::size_t(index));
    return it == tagIds.end() ? 0 : it->second;
}

Id Converter::materialOf(long long index) const {
    if (index < 0 || std::size_t(index) >= materialIds.size())
        return 0;
    return materialIds[std::size_t(index)];
}

// The texture as a native asset: PNG and JPEG are kept as they are when the native decoder reads
// them; any other image (or a PNG it refuses, such as 16 bits per channel) is decoded here and
// re-encoded as PNG. Images beyond 4096 by 4096 pixels are refused.
Id Converter::textureAsset(long long index) {
    if (const auto found = assetIds.find(index); found != assetIds.end())
        return found->second;
    Id result = 0;
    const auto &texture = pkg.textures[std::size_t(index)];
    std::string mediaType;
    std::vector<std::uint8_t> bytes;
    const auto &source = texture.bytes;
    const bool png = source.size() > 8 && source[0] == 0x89 && source[1] == 'P' && source[2] == 'N' && source[3] == 'G';
    const bool jpeg = source.size() > 3 && source[0] == 0xFF && source[1] == 0xD8 && source[2] == 0xFF;
    bool kept = false;
    if (source.empty()) {
        note("textureMissing");
    } else {
        if (png || jpeg) {
            AssetRecord probe;
            probe.mediaType = png ? "image/png" : "image/jpeg";
            probe.payload = std::make_shared<AssetPayload>(source);
            const auto status = decodeTextureImage(probe);
            if (status.status == TextureImageStatus::Ready) {
                mediaType = probe.mediaType;
                bytes = source;
                kept = true;
            } else if (status.status == TextureImageStatus::TooLarge) {
                note("textureTooLarge");
                assetIds[index] = 0;
                return 0;
            }
        }
        if (!kept) {
            QByteArray data(reinterpret_cast<const char *>(source.data()), qsizetype(source.size()));
            QBuffer buffer(&data);
            buffer.open(QIODevice::ReadOnly);
            QImageReader reader(&buffer);
            const QSize size = reader.size();
            if (!size.isValid() || size.width() <= 0 || size.height() <= 0) {
                note("textureUnreadable");
            } else if (size.width() > int(textureDimensionLimit) || size.height() > int(textureDimensionLimit)) {
                note("textureTooLarge");
            } else {
                QImage image = reader.read();
                if (image.isNull()) {
                    note("textureUnreadable");
                } else {
                    image = image.convertToFormat(QImage::Format_RGBA8888);
                    const int width = image.width(), height = image.height();
                    std::vector<std::uint8_t> rgba(std::size_t(width) * std::size_t(height) * 4);
                    for (int row = 0; row < height; ++row)
                        std::memcpy(rgba.data() + std::size_t(row) * std::size_t(width) * 4, image.constScanLine(row),
                                    std::size_t(width) * 4);
                    const QByteArray png8 = encodeTexturePng(TextureImage(width, height, std::move(rgba)));
                    bytes.assign(png8.begin(), png8.end());
                    mediaType = "image/png";
                    kept = true;
                }
            }
        }
    }
    if (kept) {
        if (assetBytes + bytes.size() > assetTotalLimit) {
            note("texturesOverLimit");
        } else {
            auto record = std::make_shared<AssetRecord>();
            record->id = nextAsset++;
            record->name = plainText(texture.filename, materialNameBytes);
            record->mediaType = mediaType;
            record->payload = std::make_shared<AssetPayload>(bytes);
            assetBytes += bytes.size();
            assetRecords[record->id] = record;
            result = record->id;
        }
    }
    assetIds[index] = result;
    return result;
}

void Converter::buildMaterials() {
    materialIds.assign(pkg.materials.size(), 0);
    textureTiles.assign(pkg.materials.size(), std::nullopt);
    std::set<std::string> names;
    for (std::size_t i = 0; i < pkg.materials.size(); ++i) {
        checkCancel();
        if (materialRecords.size() >= materialLimit) {
            note("materialsOverLimit");
            omit("material");
            continue;
        }
        const auto &source = pkg.materials[i];
        auto record = std::make_shared<MaterialRecord>();
        record->id = nextMaterial++;
        std::string name = plainText(source.name, materialNameBytes);
        if (name.empty())
            name = "SketchUp material " + std::to_string(record->id);
        if (name != source.name)
            note("truncatedNames");
        const std::string original = name;
        for (std::size_t suffix = 2; !names.insert(name).second; ++suffix)
            name = original + " (" + std::to_string(suffix) + ")";
        if (name != original)
            note("duplicateMaterialNamesRenamed");
        record->name = name;
        for (int c = 0; c < 3; ++c)
            record->color[std::size_t(c)] = source.color[std::size_t(c)] / 255.0f;
        record->opacity = float(std::clamp(source.opacity, 0.0, 1.0));
        if (source.texture >= 0 && std::size_t(source.texture) < pkg.textures.size()) {
            const Id asset = textureAsset(source.texture);
            if (asset) {
                record->asset = asset;
                const auto &texture = pkg.textures[std::size_t(source.texture)];
                const double tileW = texture.width > tileEpsilon ? texture.width : 1.0;
                const double tileH = texture.height > tileEpsilon ? texture.height : 1.0;
                textureTiles[i] = std::make_pair(tileW, tileH);
            }
        }
        materialRecords.push_back(record);
        materialIds[i] = record->id;
    }
}

bool Converter::faceIsValid(const SkpPackageDefinition &definition, const std::vector<std::vector<std::uint32_t>> &loops, std::string &problem) const {
    // The native validator on a scratch surface with this face alone. Its vertices keep the package's
    // identities (no merging by distance, unlike Surface::addFace), so the check is the one the body gets.
    Surface scratch;
    std::map<std::uint32_t, Id> ids;
    std::vector<std::vector<Id>> ringIds;
    try {
        for (const auto &loop : loops) {
            ringIds.emplace_back();
            for (const auto index : loop) {
                if (index >= definition.vertices.size()) {
                    problem = "a loop names a vertex the definition does not have";
                    return false;
                }
                auto found = ids.find(index);
                if (found == ids.end()) {
                    const Vec3 p = point(definition.vertices[index]);
                    checkPoint(p);
                    found = ids.emplace(index, scratch.nextId++).first;
                    scratch.vertices[found->second] = p;
                }
                ringIds.back().push_back(found->second);
            }
        }
        (void)scratch.addFaceIds(std::move(ringIds));
        return true;
    } catch (const std::exception &error) {
        problem = error.what();
        return false;
    }
}

Id Converter::vertexOf(Bucket &bucket, const SkpPackageDefinition &definition, std::uint32_t index) const {
    if (const auto found = bucket.vertexIds.find(index); found != bucket.vertexIds.end())
        return found->second;
    const Id id = bucket.body.surface.nextId++;
    bucket.body.surface.vertices[id] = point(definition.vertices[index]);
    bucket.vertexIds[index] = id;
    bucket.packageOf[id] = index;
    return id;
}

// The cost of materializing definition d once under an existing root: its own bodies and geometry,
// and each nested placement's cost. A cycle (or a nesting beyond the limit) is too deep.
Cost Converter::costOf(std::size_t d, std::size_t depth, bool &tooDeep) const {
    Cost total = canonical[d].own;
    if (depth > maxNesting) {
        tooDeep = true;
        return total;
    }
    for (const auto &[member, target] : canonical[d].references) {
        (void)member;
        if (target == 0 || std::size_t(target) >= canonical.size()) {
            tooDeep = true;
            continue;
        }
        const Cost nested = costOf(std::size_t(target), depth + 1, tooDeep);
        total.bodies += nested.bodies + 1;
        total.vertices += nested.vertices;
        total.faces += nested.faces;
        total.edges += nested.edges;
        total.wires += nested.wires;
    }
    return total;
}

Canonical Converter::buildDefinition(std::size_t d) {
    checkCancel();
    const auto &definition = pkg.definitions[d];
    Canonical canon;
    auto root = std::make_shared<Body>();
    root->id = 1;
    root->kind = BodyKind::Group;
    root->name = capName(definition.name);
    canon.members[1] = root;

    std::map<PairKey, const SkpPackageEdge *> edgeOf;
    for (const auto &edge : definition.edges) {
        if (edge.v1 == edge.v2 || edge.v1 >= definition.vertices.size() || edge.v2 >= definition.vertices.size()) {
            omit("edge");
            ++degenerateEdges;
            continue;
        }
        edgeOf.emplace(pairKey(edge.v1, edge.v2), &edge);
    }

    std::map<std::pair<Id, bool>, Bucket> buckets;
    std::set<PairKey> faceEdges;
    std::size_t count = 0;
    for (const auto &face : definition.faces) {
        if (++count % 256 == 0)
            checkCancel();
        std::vector<std::vector<std::uint32_t>> loops;
        for (const auto &loop : face.loops)
            loops.push_back(cleanLoop(loop));
        if (loops.empty()) {
            omit("face");
            continue;
        }
        std::string problem;
        if (std::any_of(loops.begin(), loops.end(), [](const auto &loop) { return loop.size() < 3; }))
            problem = "a loop has fewer than three vertices";
        if (!problem.empty() || !faceIsValid(definition, loops, problem)) {
            ++invalidFaces;
            if (loops.size() > 1)
                invalidHoles += static_cast<long long>(loops.size() - 1);
            locate("invalidFace", definition.name, "A face the native validator rejects is dropped: " + problem + ".");
            continue;
        }
        const Id tag = tagOf(face.tag);
        auto found = buckets.find({tag, face.hidden});
        if (found == buckets.end()) {
            Bucket fresh;
            fresh.body.name = "Geometry";
            fresh.body.tag = tag;
            fresh.body.hidden = face.hidden;
            found = buckets.emplace(std::make_pair(tag, face.hidden), std::move(fresh)).first;
        }
        Bucket &bucket = found->second;
        // The new vertices and edges this face adds to its body, against the body and the document caps.
        std::set<std::uint32_t> newVertices;
        std::set<PairKey> newEdges;
        for (const auto &loop : loops)
            for (std::size_t i = 0; i < loop.size(); ++i) {
                if (!bucket.vertexIds.contains(loop[i]))
                    newVertices.insert(loop[i]);
                const auto key = pairKey(loop[i], loop[(i + 1) % loop.size()]);
                if (!bucket.edgeKeys.contains(key))
                    newEdges.insert(key);
            }
        const std::size_t bodyFaces = bucket.body.surface.faces.size() + 1;
        const std::size_t bodyVertices = bucket.body.surface.vertices.size() + newVertices.size();
        const std::size_t bodyEdges = bucket.edgeKeys.size() + newEdges.size();
        LimitCounts trial = unique;
        trial.bodies += bucket.body.surface.faces.empty() ? 1 : 0;
        trial.faces += 1;
        trial.vertices += newVertices.size();
        trial.edges += newEdges.size();
        if (bodyFaces > 100000 || bodyVertices > 100000 || bodyEdges > Topology::edgeLimit) {
            note("bodyLimit");
            omit("face");
            continue;
        }
        if (!exceededLimit(trial, LimitClass::UniqueGeometry).empty()) {
            note("documentLimit");
            omit("face");
            continue;
        }
        unique = trial;
        std::vector<std::vector<Id>> ringIds;
        for (const auto &loop : loops) {
            ringIds.emplace_back();
            for (const auto index : loop)
                ringIds.back().push_back(vertexOf(bucket, definition, index));
        }
        const Id faceId = bucket.body.surface.addFaceIds(std::move(ringIds));
        for (const auto &loop : loops)
            for (std::size_t i = 0; i < loop.size(); ++i) {
                const auto key = pairKey(loop[i], loop[(i + 1) % loop.size()]);
                bucket.edgeKeys.insert(key);
                faceEdges.insert(key);
            }

        ++importedFaces;
        if (loops.size() > 1)
            importedHoles += static_cast<long long>(loops.size() - 1);

        MaterialSides sides;
        sides.front = materialOf(face.materialFront);
        sides.back = materialOf(face.materialBack);
        if (sides.front || sides.back)
            bucket.body.faceMaterials[faceId] = sides;

        // The face's own normal: the exporter's basis is taken from the face's normal, which the loops
        // give (the outer loop's right-hand normal).
        const Vec3 raw = bucket.body.surface.normal(faceId);
        const double magnitude = length(raw);
        TextureMappingSides mappings;
        if (magnitude > tileEpsilon) {
            const Vec3 normal = raw * (1.0 / magnitude);
            const auto side = [&](long long index, bool hasStored, const std::array<double, 9> &stored) -> std::optional<TextureMapping> {
                const Id material = materialOf(index);
                if (!material || index < 0 || std::size_t(index) >= textureTiles.size() || !textureTiles[std::size_t(index)])
                    return std::nullopt;
                const auto [tileW, tileH] = *textureTiles[std::size_t(index)];
                if (hasStored) {
                    if (const auto mapping = storedMapping(stored, normal, tileW, tileH))
                        return mapping;
                    note("uvPerspective");
                }
                return defaultMapping(normal, tileW, tileH);
            };
            mappings.front = side(face.materialFront, face.hasUvFront, face.uvFront);
            mappings.back = side(face.materialBack, face.hasUvBack, face.uvBack);
        }
        if (mappings.front || mappings.back)
            bucket.body.faceTextureMappings[faceId] = mappings;
    }

    // Each body: its topology, and the flags of its edges.
    const auto finish = [&](Bucket &bucket) {
        bucket.body.topology = Topology::rebuild(bucket.body.surface, {});
        for (const auto &[id, record] : bucket.body.topology.edges) {
            nativeEdges += 1;
            const auto a = bucket.packageOf.find(record.a), b = bucket.packageOf.find(record.b);
            if (a == bucket.packageOf.end() || b == bucket.packageOf.end())
                continue;
            const auto flags = edgeOf.find(pairKey(a->second, b->second));
            if (flags == edgeOf.end())
                continue;
            const EdgeAppearance look{flags->second->hidden, flags->second->soft, flags->second->smooth};
            if (look.hidden || look.soft || look.smooth)
                bucket.body.edgeAppearances[id] = look;
        }
    };
    for (auto &[key, bucket] : buckets) {
        (void)key;
        finish(bucket);
        bucket.body.surface.validate();
        auto body = std::make_shared<Body>(std::move(bucket.body));
        body->id = canon.nextMember++;
        body->parent = 1;
        canon.own.bodies += 1;
        canon.own.vertices += body->surface.vertices.size();
        canon.own.faces += body->surface.faces.size();
        canon.own.edges += body->topology.edges.size();
        canon.members[body->id] = body;
    }

    // Loose edges (no kept face uses them) are wires, in one body of their own.
    std::vector<std::pair<PairKey, const SkpPackageEdge *>> wires;
    for (const auto &[key, edge] : edgeOf)
        if (!faceEdges.contains(key))
            wires.emplace_back(key, edge);
    if (!wires.empty()) {
        Bucket wire;
        wire.body.name = "Wires";
        for (const auto &[key, edge] : wires) {
            (void)edge;
            const Id a = vertexOf(wire, definition, key.first);
            const Id b = vertexOf(wire, definition, key.second);
            wire.body.surface.wires.push_back({a, b});
        }
        finish(wire);
        wire.body.surface.validate();
        auto body = std::make_shared<Body>(std::move(wire.body));
        body->id = canon.nextMember++;
        body->parent = 1;
        canon.own.bodies += 1;
        canon.own.wires += body->topology.edges.size();
        canon.members[body->id] = body;
        importedWires += static_cast<long long>(wires.size());
    }
    importedEdges += static_cast<long long>(edgeOf.size());

    // The root has no nested placements of its own: its instances are placed at the top level.
    if (d != 0) {
        for (const auto &instance : definition.instances) {
            Transform placement;
            if (instance.definition < 0 || std::size_t(instance.definition) >= pkg.definitions.size() ||
                instance.definition == 0 || std::size_t(instance.definition) == d || !placementOf(instance, placement)) {
                omit("instance");
                continue;
            }
            auto node = std::make_shared<Body>();
            node->id = canon.nextMember++;
            node->kind = BodyKind::Group;
            node->parent = 1;
            node->name = capName(instance.name.empty() ? pkg.definitions[std::size_t(instance.definition)].name : instance.name);
            addAttributeProperties(node->properties, instance, notices);
            node->transform = placement;
            node->tag = tagOf(instance.tag);
            node->hidden = instance.hidden;
            if (const Id material = materialOf(instance.material)) {
                node->materials.front = material;
                node->materials.back = material;
            }
            canon.members[node->id] = node;
            canon.references[node->id] = Id(instance.definition);
            ++importedInstances;
        }
    }
    return canon;
}

// Materializes definition `d` under the placement root `rootId`, which is a body already in the
// edit (its transform, name, tag and visibility are the placement's). Every member gets a copy and
// every nested placement gets its own binding, as the native projection requires.
void Converter::place(std::size_t d, Id rootId, std::size_t depth) {
    const Canonical &canon = canonical[d];
    auto binding = std::make_shared<ComponentInstance>();
    binding->definition = Id(d);
    binding->members[1] = rootId;
    for (const auto &[member, body] : canon.members) {
        (void)body;
        if (member != 1)
            binding->members[member] = nextBody++;
    }
    for (const auto &[member, prototype] : canon.members) {
        if (member == 1)
            continue;
        auto body = std::make_shared<Body>(*prototype);
        body->id = binding->members.at(member);
        body->parent = binding->members.at(prototype->parent);
        if (const auto reference = canon.references.find(member); reference != canon.references.end())
            place(std::size_t(reference->second), body->id, depth + 1);
        edit.changes.push_back({body->id, nullptr, body});
    }
    edit.instances.push_back({rootId, nullptr, binding});
    ++placements;
}

// One top-level instance of the root: its group carries the placement, then the definition is placed under it.
void Converter::placeTopLevel(std::size_t k, Id parent) {
    const auto &instance = pkg.definitions[0].instances[k];
    Transform placement;
    if (instance.definition < 0 || std::size_t(instance.definition) >= pkg.definitions.size() ||
        instance.definition == 0 || !placementOf(instance, placement)) {
        omit("instance");
        return;
    }
    const std::size_t d = std::size_t(instance.definition);
    bool tooDeep = false;
    const Cost cost = costOf(d, 1, tooDeep);
    LimitCounts add;
    add.placements = 1;
    add.bodies = cost.bodies + 1;
    add.vertices = cost.vertices;
    add.faces = cost.faces;
    add.edges = cost.edges;
    add.wires = cost.wires;
    LimitCounts trial = materialized;
    trial.placements += add.placements;
    trial.bodies += add.bodies;
    trial.vertices += add.vertices;
    trial.faces += add.faces;
    trial.edges += add.edges;
    trial.wires += add.wires;
    if (tooDeep || !exceededLimit(trial, LimitClass::Placement).empty()) {
        note(tooDeep ? "nestingTooDeep" : "placementLimit");
        omit("instance");
        return;
    }
    materialized = trial;
    auto group = std::make_shared<Body>();
    group->id = nextBody++;
    group->kind = BodyKind::Group;
    group->parent = parent;
    group->name = capName(instance.name.empty() ? pkg.definitions[d].name : instance.name);
    addAttributeProperties(group->properties, instance, notices);
    group->transform = placement;
    group->tag = tagOf(instance.tag);
    group->hidden = instance.hidden;
    if (const Id material = materialOf(instance.material)) {
        group->materials.front = material;
        group->materials.back = material;
    }
    edit.changes.push_back({group->id, nullptr, group});
    ++importedInstances;
    place(d, group->id, 1);
}

SkpImport Converter::run() {
    checkCancel();
    bool knownUnits = false;
    const DisplayUnit units = displayUnitsOf(pkg.units, knownUnits);
    if (!knownUnits)
        note("unitsUnknown");

    buildTags();
    buildMaterials();

    // Every definition's canonical geometry: the root (0) first, then the file's definitions in order.
    canonical.resize(pkg.definitions.size());
    for (std::size_t d = 0; d < pkg.definitions.size(); ++d)
        canonical[d] = buildDefinition(d);

    // The root: a top-level group named after the file, holding the root's geometry and its placements.
    std::string base = pkg.sourceName;
    const auto slash = base.find_last_of("/\\");
    if (slash != std::string::npos)
        base = base.substr(slash + 1);
    if (base.size() > 4 && base.substr(base.size() - 4) == ".skp")
        base = base.substr(0, base.size() - 4);
    base = capName(base);
    if (base.empty())
        base = "SketchUp model";
    auto rootGroup = std::make_shared<Body>();
    rootGroup->id = nextBody++;
    rootGroup->kind = BodyKind::Group;
    rootGroup->name = base;
    edit.changes.push_back({rootGroup->id, nullptr, rootGroup});
    for (const auto &[member, prototype] : canonical[0].members) {
        if (member == 1)
            continue;
        auto body = std::make_shared<Body>(*prototype);
        body->id = nextBody++;
        body->parent = rootGroup->id;
        materialized.bodies += 1;
        materialized.vertices += body->surface.vertices.size();
        materialized.faces += body->surface.faces.size();
        materialized.edges += body->topology.edges.size();
        materialized.wires += body->surface.wires.size();
        edit.changes.push_back({body->id, nullptr, body});
    }
    for (std::size_t k = 0; k < pkg.definitions[0].instances.size(); ++k) {
        checkCancel();
        placeTopLevel(k, rootGroup->id);
    }

    // The definitions: every file definition, as a component definition with its canonical members.
    for (std::size_t d = 1; d < pkg.definitions.size(); ++d) {
        const auto &canon = canonical[d];
        auto definition = std::make_shared<ComponentDefinition>();
        definition->id = Id(d);
        definition->root = 1;
        definition->name = capName(pkg.definitions[d].name);
        definition->members = canon.members;
        definition->references = canon.references;
        definition->nextMemberId = canon.nextMember;
        edit.definitions.push_back({definition->id, nullptr, definition});
        ++importedDefinitions;
    }

    for (const auto &tag : tagRecords)
        edit.tags.push_back({tag->id, nullptr, tag});
    for (const auto &material : materialRecords)
        edit.materials.push_back({material->id, nullptr, material});
    for (const auto &[id, asset] : assetRecords)
        edit.assets.push_back({id, nullptr, asset});
    edit.nextIdFloor = nextBody;
    edit.nextTagFloor = nextTag;
    edit.nextMaterialFloor = nextMaterial;
    edit.nextAssetFloor = nextAsset;
    edit.nextDefinitionFloor = Id(pkg.definitions.size());
    if (units != DisplayUnit::Meters)
        edit.displayUnits = std::pair{DisplayUnit::Meters, units};

    SkpImport result{Document(DisplayUnit::Meters), {}};
    checkCancel();
    result.document.apply(std::move(edit), result.document.revision());

    // The accounting identity (ADR 0176 section 5): counted = imported + omitted, where the omitted is
    // what the helper left out (the package's own identity) and what this conversion refused.
    const auto &counts = pkg.counts;
    const auto sourceTotal = [&](const char *key) -> long long {
        const auto it = pkg.sourceTotals.find(key);
        return it == pkg.sourceTotals.end() ? 0 : it->second;
    };
    QJsonObject accounting;
    const auto account = [&](const char *kind, long long kept, long long imported) {
        const long long total = sourceTotal(kind);
        const long long byWorker = total - kept;
        const long long byImport = kept - imported;
        accounting[kind] = QJsonObject{{"counted", total},
                                       {"imported", imported},
                                       {"omitted", byWorker + byImport},
                                       {"omittedByWorker", byWorker},
                                       {"omittedByImport", byImport}};
    };
    account("faces", counts.faces, importedFaces);
    account("edges", counts.edges, importedEdges);
    account("instances", counts.instances, importedInstances);
    account("materials", counts.materials, static_cast<long long>(materialRecords.size()));

    // What the SketchUp file holds that the package does not carry (ADR 0176 section 5): counted as none.
    QJsonArray notCarried{"scenes", "styles", "sectionPlanes", "texts", "dimensions", "camera", "constructionLines",
                          "constructionPoints"};
    QJsonObject notes;
    for (const auto &[code, count] : notices)
        notes[QString::fromStdString(code)] = qint64(count);
    QJsonObject where;
    for (const auto &[kind, messages] : locations) {
        QJsonArray list;
        for (const auto &message : messages)
            list.append(QString::fromStdString(message));
        where[QString::fromStdString(kind)] = QJsonObject{{"total", qint64(locationTotals[kind])}, {"samples", list}};
    }
    QJsonObject sourceTotals;
    for (const auto &[key, value] : pkg.sourceTotals)
        sourceTotals[QString::fromStdString(key)] = value;
    QJsonObject omittedWorker;
    for (const auto &[key, value] : pkg.omitted)
        omittedWorker[QString::fromStdString(key)] = value;
    result.report = QJsonObject{
        {"apiVersion", 1},
        {"format", "SketchUp"},
        {"direction", "import"},
        {"formatVersion", QString::fromStdString(pkg.formatVersion)},
        {"container", QString::fromStdString(pkg.container)},
        {"units", QString::fromStdString(pkg.units)},
        {"source", QJsonObject{{"name", QString::fromStdString(pkg.sourceName)},
                               {"sha256", QString::fromStdString(pkg.sourceSha256)},
                               {"bytes", qint64(pkg.sourceSize)}}},
        {"counts", QJsonObject{{"definitions", importedDefinitions},
                               {"placements", placements},
                               {"faces", importedFaces},
                               {"holes", importedHoles},
                               {"invalidHoles", invalidHoles},
                               {"edges", nativeEdges},
                               {"wires", importedWires},
                               {"instances", importedInstances},
                               {"materials", qint64(materialRecords.size())},
                               {"tags", qint64(tagRecords.size())},
                               {"textures", qint64(assetRecords.size())}}},
        {"accounting", accounting},
        {"sourceTotals", sourceTotals},
        {"omittedByWorker", omittedWorker},
        {"notices", notes},
        {"notCarried", notCarried},
        {"locations", where},
    };
    return result;
}

} // namespace

SkpImport importSkpPackage(const SkpPackage &package, const SkpImportOptions &, const SkpConversionHooks &hooks) {
    if (package.definitions.empty())
        throw IfcImportError("package", "The SketchUp package has no root definition.");
    Converter converter(package, hooks);
    return converter.run();
}

} // namespace sketchy
