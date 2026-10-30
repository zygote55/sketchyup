#include "io/ifc_import.hpp"
#include "core/document_limits.hpp"
#include "io/native_limits.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>
namespace sketchy {
namespace {
using Value = std::variant<bool, double, std::string>;
constexpr double planeTolerance = tolerance; // 1e-7 m: the native planarity bound
constexpr std::size_t reservedProvenance = 16;
constexpr std::size_t propertyAggregateBytes = 8 * 1024 * 1024;
constexpr std::size_t modelMargin = 2 * 1024 * 1024;
constexpr std::size_t maxFamilyTags = 128;

std::string clipUtf8(const std::string &text, std::size_t bytes) {
    if (text.size() <= bytes)
        return text;
    std::size_t end = bytes;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80)
        --end;
    return text.substr(0, end);
}
std::string plainText(const std::string &text, std::size_t bytes) {
    std::string result = text;
    for (auto &c : result)
        if (static_cast<unsigned char>(c) < 32 || c == 127)
            c = ' ';
    return clipUtf8(result, bytes);
}
std::size_t valueBytes(const Value &value) {
    if (const auto *text = std::get_if<std::string>(&value))
        return text->size();
    return 8;
}

// ---- Geometry -------------------------------------------------------------------------------

struct GridKey {
    std::int64_t x, y, z;
    bool operator==(const GridKey &) const = default;
};
struct GridHash {
    std::size_t operator()(const GridKey &k) const {
        // Unsigned arithmetic: the cells of a model tens of kilometres across would overflow a signed product.
        return std::size_t(std::uint64_t(k.x) * 73856093ULL) ^ std::size_t(std::uint64_t(k.y) * 19349663ULL) ^
               std::size_t(std::uint64_t(k.z) * 83492791ULL);
    }
};
struct Welder {
    std::unordered_map<GridKey, std::vector<Id>, GridHash> cells;
    Surface &surface;
    explicit Welder(Surface &s) : surface(s) {}
    static GridKey cell(Vec3 p) {
        return {std::llround(p.x / tolerance), std::llround(p.y / tolerance), std::llround(p.z / tolerance)};
    }
    Id vertex(Vec3 p) {
        checkPoint(p);
        const auto base = cell(p);
        for (std::int64_t dx = -1; dx <= 1; ++dx)
            for (std::int64_t dy = -1; dy <= 1; ++dy)
                for (std::int64_t dz = -1; dz <= 1; ++dz) {
                    const auto found = cells.find({base.x + dx, base.y + dy, base.z + dz});
                    if (found == cells.end())
                        continue;
                    for (const auto id : found->second)
                        if (length(surface.vertices.at(id) - p) < tolerance)
                            return id;
                }
        const Id id = surface.nextId++;
        surface.vertices.emplace(id, p);
        cells[base].push_back(id);
        return id;
    }
};

struct GeometryStats {
    std::size_t sourceTriangles{}, degenerate{}, nonPlanar{}, mergedFaces{}, faces{}, vertices{}, edges{},
        mergeFallbacks{};
    bool tooLarge{};
    bool empty{};
};
struct MeshGeometry {
    std::shared_ptr<Body> body; // geometry only; ids, name, tag, transform and properties are the node's
    GeometryStats stats;
    Vec3 lo{}, hi{};
    // R098.j: the feature edges, the edge appearances and the materials are made by finishMeshGeometry, when the
    // document accepts the body. They count against no limit, and a body the document refuses does not need them.
    bool finished{};
    std::vector<Id> vertexOf;  // mesh vertex -> welded vertex
    std::map<Id, Id> desired; // face -> material (0: none); colours are kept in body.faceColors
};
struct TriRecord {
    Id a, b, c;
    Vec3 n;
    std::uint32_t material;
    std::optional<std::array<float, 3>> color;
};

// The planar-patch merge (ADR 0171 section 3.3): coplanar triangles that share an edge and a look become
// one polygon face, with holes where the outline requires; everything else stays triangles.
MeshGeometry buildMeshGeometry(const IfcPackageMesh &mesh, const std::function<Id(std::uint32_t)> &nativeMaterial) {
    MeshGeometry result;
    result.body = std::make_shared<Body>();
    Body &body = *result.body;
    Surface &surface = body.surface;
    Welder welder(surface);
    std::vector<Id> vertexOf(mesh.vertices.size());
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i)
        vertexOf[i] = welder.vertex(mesh.vertices[i]);
    result.stats.sourceTriangles = mesh.triangles.size();
    std::vector<TriRecord> triangles;
    triangles.reserve(mesh.triangles.size());
    for (std::size_t t = 0; t < mesh.triangles.size(); ++t) {
        const auto &source = mesh.triangles[t];
        const Id a = vertexOf[source[0]], b = vertexOf[source[1]], c = vertexOf[source[2]];
        if (a == b || b == c || a == c) {
            ++result.stats.degenerate;
            continue;
        }
        const auto pa = surface.vertices.at(a), pb = surface.vertices.at(b), pc = surface.vertices.at(c);
        const auto cr = cross(pb - pa, pc - pa);
        const double twiceArea = length(cr);
        // Faces below 1e-12 m^2 are below what the tessellator keeps (and about a millionth of a metre).
        if (!(twiceArea >= 2e-12)) {
            ++result.stats.degenerate;
            continue;
        }
        TriRecord record{a, b, c, cr * (1 / twiceArea), mesh.materials[t], std::nullopt};
        if (!mesh.colors.empty())
            record.color = mesh.colors[t];
        triangles.push_back(record);
    }
    if (triangles.empty()) {
        result.stats.empty = true;
        return result;
    }
    // Directed edge -> triangles that run along it.
    std::map<std::pair<Id, Id>, std::vector<std::uint32_t>> directed;
    for (std::uint32_t t = 0; t < triangles.size(); ++t) {
        const auto &r = triangles[t];
        directed[{r.a, r.b}].push_back(t);
        directed[{r.b, r.c}].push_back(t);
        directed[{r.c, r.a}].push_back(t);
    }
    const auto sameLook = [&](const TriRecord &x, const TriRecord &y) {
        return x.material == y.material && x.color == y.color;
    };
    constexpr std::uint32_t unvisited = 0xFFFFFFFFu;
    std::vector<std::uint32_t> region(triangles.size(), unvisited);
    std::vector<std::vector<std::uint32_t>> regions;
    std::set<std::uint32_t> nonPlanarTriangles;
    for (std::uint32_t seed = 0; seed < triangles.size(); ++seed) {
        if (region[seed] != unvisited)
            continue;
        const auto id = std::uint32_t(regions.size());
        regions.emplace_back();
        std::deque<std::uint32_t> queue{seed};
        region[seed] = id;
        const auto &first = triangles[seed];
        const Vec3 origin = surface.vertices.at(first.a);
        while (!queue.empty()) {
            const auto t = queue.front();
            queue.pop_front();
            regions[id].push_back(t);
            const auto &r = triangles[t];
            const std::array<std::pair<Id, Id>, 3> edges{{{r.a, r.b}, {r.b, r.c}, {r.c, r.a}}};
            for (const auto &[u, v] : edges) {
                const auto here = directed.find({u, v});
                const auto there = directed.find({v, u});
                if (here == directed.end() || there == directed.end() || here->second.size() != 1 ||
                    there->second.size() != 1)
                    continue; // a boundary or a non-manifold edge: no merge across it
                const auto q = there->second.front();
                if (region[q] != unvisited || !sameLook(r, triangles[q]))
                    continue;
                const auto &other = triangles[q];
                const double cosine = dot(first.n, other.n);
                bool flat = cosine >= 1 - 1e-9;
                if (flat)
                    for (const Id vertexId : {other.a, other.b, other.c})
                        if (std::abs(dot(surface.vertices.at(vertexId) - origin, first.n)) > planeTolerance * 0.5)
                            flat = false;
                if (flat) {
                    region[q] = id;
                    queue.push_back(q);
                } else if (cosine >= 1 - 1e-4) {
                    nonPlanarTriangles.insert(q);
                }
            }
        }
    }
    result.stats.nonPlanar = nonPlanarTriangles.size();
    std::map<Id, Id> desired; // face -> material (0: none); colours are kept in body.faceColors
    const auto sideFor = [&](const TriRecord &r, Id face) {
        if (r.color) {
            body.faceColors[face] = *r.color;
            desired[face] = 0;
        } else {
            desired[face] = nativeMaterial(r.material);
        }
    };
    const auto addTriangle = [&](std::uint32_t t) {
        const auto &r = triangles[t];
        const Id face = surface.nextId++;
        surface.faces[face] = Face{face, {{r.a, r.b, r.c}}};
        sideFor(r, face);
    };
    for (const auto &members : regions) {
        if (members.size() == 1) {
            addTriangle(members.front());
            continue;
        }
        // Boundary: directed edges whose reverse is not run by a triangle of the same region.
        const auto &first = triangles[members.front()];
        std::map<Id, std::vector<Id>> next;
        std::set<std::pair<Id, Id>> inRegion;
        for (const auto t : members) {
            const auto &r = triangles[t];
            inRegion.insert({r.a, r.b});
            inRegion.insert({r.b, r.c});
            inRegion.insert({r.c, r.a});
        }
        bool pinched = false;
        for (const auto t : members) {
            const auto &r = triangles[t];
            const std::array<std::pair<Id, Id>, 3> edges{{{r.a, r.b}, {r.b, r.c}, {r.c, r.a}}};
            for (const auto &[u, v] : edges)
                if (!inRegion.contains({v, u})) {
                    auto &list = next[u];
                    list.push_back(v);
                    if (list.size() > 1)
                        pinched = true;
                }
        }
        std::vector<std::vector<Id>> loops;
        bool ok = !pinched && !next.empty();
        std::set<Id> used;
        for (const auto &[start, ignored] : next) {
            if (!ok || used.contains(start))
                continue;
            std::vector<Id> loop;
            Id at = start;
            while (true) {
                if (!used.insert(at).second) {
                    ok = ok && at == start;
                    break;
                }
                loop.push_back(at);
                const auto step = next.find(at);
                if (step == next.end()) {
                    ok = false;
                    break;
                }
                at = step->second.front();
                if (loop.size() > surface.vertices.size()) {
                    ok = false;
                    break;
                }
            }
            if (loop.size() < 3)
                ok = false;
            loops.push_back(std::move(loop));
        }
        std::vector<std::vector<Id>> outer, holes;
        if (ok)
            for (auto &loop : loops) {
                Vec3 sum{};
                const auto o = surface.vertices.at(loop[0]);
                for (std::size_t i = 0; i < loop.size(); ++i)
                    sum = sum + cross(surface.vertices.at(loop[i]) - o, surface.vertices.at(loop[(i + 1) % loop.size()]) - o);
                (dot(sum, first.n) > 0 ? outer : holes).push_back(std::move(loop));
            }
        if (ok && outer.size() == 1) {
            std::vector<std::vector<Id>> face{outer.front()};
            for (auto &hole : holes)
                face.push_back(std::move(hole));
            const Id id = surface.nextId;
            surface.faces[id] = Face{id, std::move(face)};
            try {
                (void)surface.triangulate(id);
                ++surface.nextId;
                sideFor(first, id);
                ++result.stats.mergedFaces;
                continue;
            } catch (const std::exception &) {
                surface.faces.erase(id);
            }
        }
        ++result.stats.mergeFallbacks;
        for (const auto t : members)
            addTriangle(t);
    }
    result.stats.faces = surface.faces.size();
    result.stats.vertices = surface.vertices.size();
    if (surface.faces.size() > 100000 || surface.vertices.size() > 100000) {
        result.stats.tooLarge = true;
        return result;
    }
    body.topology = Topology::rebuild(surface, {});
    result.stats.edges = body.topology.edges.size();
    bool first = true;
    for (const auto &[id, p] : surface.vertices) {
        if (first) {
            result.lo = result.hi = p;
            first = false;
        }
        result.lo = {std::min(result.lo.x, p.x), std::min(result.lo.y, p.y), std::min(result.lo.z, p.z)};
        result.hi = {std::max(result.hi.x, p.x), std::max(result.hi.y, p.y), std::max(result.hi.z, p.z)};
    }
    result.vertexOf = std::move(vertexOf);
    result.desired = std::move(desired);
    return result;
}

// R098.j: the rest of what a body needs (ADR 0171 section 3.3), made once, when the document accepts it. The
// topology and its counts were made by buildMeshGeometry. These maps are the bulk of the work for a body the
// document refuses, and no limit counts them.
void finishMeshGeometry(MeshGeometry &result, const IfcPackageMesh &mesh) {
    if (result.finished)
        return;
    Body &body = *result.body;
    Surface &surface = body.surface;
    // Edges: the worker's feature edges stay visible; the interior of a surface becomes soft and smooth.
    std::set<std::pair<Id, Id>> feature;
    for (const auto &edge : mesh.featureEdges) {
        Id a = result.vertexOf[edge[0]], b = result.vertexOf[edge[1]];
        if (a == b)
            continue;
        feature.insert({std::min(a, b), std::max(a, b)});
    }
    const auto adjacency = body.topology.adjacency(surface);
    for (const auto &[edge, incidence] : adjacency.edgeFaces) {
        if (incidence.size() != 2)
            continue;
        const auto &record = body.topology.edges.at(edge);
        if (record.wire)
            continue;
        if (!feature.contains({std::min(record.a, record.b), std::max(record.a, record.b)}))
            body.edgeAppearances[edge] = {false, true, true};
    }
    // The body's default material is the most used one; a face that differs (including a face with
    // none, or with a colour of its own) keeps an explicit assignment.
    std::map<Id, std::size_t> counts;
    for (const auto &[face, material] : result.desired)
        ++counts[material];
    Id best = 0;
    std::size_t bestCount = 0;
    for (const auto &[material, count] : counts)
        if (count > bestCount) {
            best = material;
            bestCount = count;
        }
    body.materials = MaterialSides{best, best};
    for (const auto &[face, material] : result.desired)
        if (material != best)
            body.faceMaterials[face] = MaterialSides{material, material};
    result.finished = true;
}

// ---- Tags ----------------------------------------------------------------------------------------

} // namespace
std::string ifcTagName(const std::string &className) {
    std::string name = className;
    if (name.rfind("Ifc", 0) == 0)
        name = name.substr(3);
    for (const char *suffix : {"StandardCase", "ElementedCase"})
        if (name.size() > std::strlen(suffix) && name.compare(name.size() - std::strlen(suffix), std::strlen(suffix), suffix) == 0)
            name.resize(name.size() - std::strlen(suffix));
    std::string spaced;
    for (std::size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        if (i > 0 && std::isupper(static_cast<unsigned char>(c)) &&
            (std::islower(static_cast<unsigned char>(name[i - 1])) ||
             (i + 1 < name.size() && std::islower(static_cast<unsigned char>(name[i + 1])))))
            spaced += ' ';
        spaced += c;
    }
    return "IFC " + (spaced.empty() ? std::string("Other") : spaced);
}
// R098.b (ADR 0172 section 4): the STEP package's classes (IfcStepSolid, IfcStepAssembly, ...)
// take the "Ifc" and "Step" prefixes off, are spaced and carried on the tag family STEP.
std::string stepTagName(const std::string &className) {
    std::string name = className;
    if (name.rfind("IfcStep", 0) == 0)
        name = name.substr(7);
    std::string spaced;
    for (std::size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        if (i > 0 && std::isupper(static_cast<unsigned char>(c)) &&
            (std::islower(static_cast<unsigned char>(name[i - 1])) ||
             (i + 1 < name.size() && std::islower(static_cast<unsigned char>(name[i + 1])))))
            spaced += ' ';
        spaced += c;
    }
    return "STEP " + (spaced.empty() ? std::string("Other") : spaced);
}
namespace {

const std::map<std::string, std::string> &noticeTexts() {
    static const std::map<std::string, std::string> texts{
        {"unitsRescaled", "The geometry library did not read the project's length unit (the file has more than one context, for example an IfcProjectLibrary); its numbers were scaled by the project's own unit."},
        {"unitFactorSnapped", "A unit conversion factor within 1e-6 of a canonical value was replaced by it."},
        {"unitFactorAsWritten", "A unit conversion factor that matches no canonical value is used as written; the unit is not exact."},
        {"lengthUnitOverrideIgnored", "The file declares a length unit, so the length unit override was ignored."},
        {"siteOriginSet", "The document site origin was set from the file's map conversion; the document is saved in schema 29 and opens only in SketchyUp 1.3.0 or later."},
        {"georeferenceRotationKept", "The map conversion rotates the project axes; they are kept and no site origin is set. The grid mode rotates the model to the map grid instead."},
        {"georeferenceGridAligned", "The model was rotated about the project origin by the map conversion's rotation, so that +X is east; walls that were on the project axes are no longer on the model axes."},
        {"georeferenceNotRepresentable", "The map conversion cannot be held by a translation-only site origin (see the reasons); the project axes are kept and no site origin is set."},
        {"georeferenceEpsets", "Property sets named ePSet_MapConversion or ePSet_ProjectedCRS were read as ordinary properties; they are not standardised and are not used for georeferencing."},
        {"georeferenceMapUnitAssumed", "The CRS gives no map unit; the metre was assumed, as the conversion's scale is the project unit in metres."},
        {"trueNorthRecorded", "The context's true north is recorded as ifc.TrueNorth; the solar settings are not changed."},
        {"materialLayeringDescriptive", "Material layer sets, constituent sets and profile sets are described in ifc.Material and ifc.MaterialLayers only; the geometry is not split into layers."},
        {"uncontainedElements", "Elements that no spatial container holds were placed under the project."},
        {"invalidGlobalId", "A GlobalId is not 22 characters of the IFC alphabet; it is kept as text."},
        {"duplicateGlobalId", "A GlobalId occurs more than once; each keeps its own ifc.GlobalId and the first one is used for the hierarchy."},
        {"layerHidden", "Bodies on a presentation layer that is off or frozen were imported hidden."},
        {"latin1Fallback", "Text that was not valid UTF-8 was read as ISO 8859-1."},
        {"faceColorsIncomplete", "An indexed colour map did not cover every triangle of its face set; the rest keep the body's material."},
        {"contextFallback", "The representation context query matched nothing; the library's default selection was used."},
        {"kernelUnavailable", "The OpenCASCADE geometry kernel is unavailable; only already-tessellated items could be read."},
        {"groupDropped", "A group whose placement cannot be composed was not imported; what it holds keeps its true position."},
        {"placementMismatch", "The library's placement of an element differs from the one composed from the file; the library's was used."},
        {"parentSingular", "A node's parent has a singular placement; the node keeps its world placement."},
        {"unnamedPropertySet", "A property set without a name is recorded under its class name."},
        {"parseWarnings", "The parser reported problems in the file, for example references to instances that do not exist; the kernel messages list them."},
        {"duplicateMaterialNamesRenamed", "Material names that repeat after shortening received a suffix."},
        {"uncutRerunFailed", "Converting a host uncut after an untrusted cut also failed."},
        {"restarted", "The worker was restarted after it stopped on an element; the element was left out."},
        {"propertyKeyShortened", "A property key longer than 128 bytes was shortened."},
        {"propertyTextShortened", "A property text longer than 2,048 bytes was shortened."},
        {"propertyKeyCollision", "Two properties had the same key after shortening; the first was kept."},
        {"propertiesDropped", "Properties beyond the per-body or aggregate limits were dropped."},
        {"containmentCycle", "Relationships that made an element its own ancestor were not followed; every element keeps the place it was first given."},
        {"propertyNotFinite", "A property whose number is infinite or not a number was left out."},
        {"propertyTextClipped", "A property text longer than 32 KiB was shortened."},
        {"propertiesOmitted", "Properties beyond 4,096 on one element, or beyond 8 MiB of text in all, were left out by the helper."},
        {"materialsOverLimit", "More than 1,024 materials: the rest keep the body's own colour."},
        {"tagsOverLimit", "More than 128 class tags: the rest are on the format's Other tag."},
        // R098.b (ADR 0172 section 4): the STEP import's notices.
        {"nameTruncated", "A name longer than 2,048 bytes was truncated."},
        {"defaultMaterial", "A product without a style was given the DefaultMaterial (0.7 grey)."},
        {"colorAlphaDropped", "A colour with an opacity that is not 1 was imported opaque."},
        {"invisibleShapes", "A shape the file marks invisible was imported hidden."},
        {"degeneratePlacement", "A placement the kernel could not apply was replaced by the identity; the shape keeps its own frame."},
        {"freeFaces", "Surfaces that no solid bounds (faces without volume) were not imported; each is counted and located."},
        {"metadataTruncated", "A name or property longer than its cap was truncated."},
        {"mirrorVariantOmitted", "A mirror instance's second mesh variant was left out of the package by the budget; the instance keeps the first."},
        {"noSolids", "A shape transferred but held no solid; it was not converted."},
        {"treeTooDeep", "The assembly is nested deeper than the document keeps (128 levels, the file's root included). The parts and assemblies below are not imported; each is counted and located."},
        {"pmiNotImported", "PMI and GD&T (dimensions, tolerances, datums and annotations) are not imported; each entity is counted and located."},
        {"triangleBudgetApplied", "The conversion stopped at the triangle budget; the rest of the file is not converted."},
        {"triangleCapApplied", "A shape reached the 100,000-triangle cap and was tessellated with the deflection relaxed until it fit."},
        {"spacesSkipped", "Spaces were not imported (skipped on request)."},
        {"definitionsOverLimit", "More shared geometries than the 1,024 component definitions allow: the rest are expanded."},
    };
    return texts;
}


// The report's bounds (ADR 0171 section 4): 64 KiB, 64 kinds, 32 locations per kind, 256-byte messages.
// Counts stay complete; only the listed examples are cut.
QString clipMessage(const QString &text) {
    const auto bytes = text.toUtf8();
    if (bytes.size() <= 256)
        return text;
    return QString::fromUtf8(clipUtf8(bytes.toStdString(), 253).c_str()) + QStringLiteral("...");
}
void boundList(QJsonObject &report, const char *key, int limit) {
    auto list = report[key].toArray();
    if (list.size() <= limit)
        return;
    QJsonArray kept;
    long long rest = 0;
    for (int i = 0; i < list.size(); ++i) {
        if (i < limit - 1)
            kept.append(list[i]);
        else
            rest += list[i].toObject()["count"].toInteger(1);
    }
    kept.append(QJsonObject{{"code", "other"}, {"count", qint64(rest)}, {"message", "Further kinds, counted together."}});
    report[key] = kept;
}
QString clipKey(const QString &key) {
    const auto bytes = key.toUtf8();
    return bytes.size() <= 128 ? key : QString::fromUtf8(clipUtf8(bytes.toStdString(), 128).c_str());
}
// A table of counts keyed by names from the file (classes, reasons): at most `limit` keys of at most 128 bytes, the
// rest summed under "other". Totals stay complete.
void boundCounts(QJsonObject &parent, const char *key, int limit = 64) {
    if (!parent[key].isObject())
        return;
    const auto object = parent[key].toObject();
    QJsonObject kept;
    long long rest = 0;
    int n = 0;
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (n < limit - 1 && clipKey(it.key()) == it.key()) {
            kept[it.key()] = it.value();
            ++n;
        } else {
            rest += it.value().toInteger(0);
        }
    }
    if (rest > 0)
        kept["other"] = kept["other"].toInteger(0) + qint64(rest);
    parent[key] = kept;
}
// A list of {class, reason, count}-like entries: at most `limit`, the rest as one entry with the counts added.
void boundEntries(QJsonObject &parent, const char *key, int limit = 64) {
    auto list = parent[key].toArray();
    QJsonArray kept;
    long long rest = 0;
    for (int i = 0; i < list.size(); ++i) {
        auto entry = list[i].toObject();
        for (const char *field : {"class", "reason", "unit"})
            if (entry[field].isString())
                entry[field] = clipKey(entry[field].toString());
        if (i < limit - 1)
            kept.append(entry);
        else
            rest += entry["count"].toInteger(1);
    }
    if (rest > 0)
        kept.append(QJsonObject{{"class", "other"}, {"reason", "other"}, {"count", qint64(rest)}});
    parent[key] = kept;
}
} // namespace
void boundIfcReport(QJsonObject &report) {
    boundList(report, "notices", 64);
    boundCounts(report, "omittedEntities");
    boundCounts(report, "losses");
    boundCounts(report, "locationTotals");
    boundEntries(report, "excluded");
    boundEntries(report, "unitsUnconverted");
    for (const char *section : {"elements", "geometry", "properties"}) {
        if (!report[section].isObject())
            continue;
        auto object = report[section].toObject();
        for (const auto &name : object.keys())
            if (object[name].isObject())
                boundCounts(object, name.toUtf8().constData());
        boundEntries(object, "notImported");
        report[section] = object;
    }
    for (const char *list : {"notices", "kernelMessages"}) {
        auto entries = report[list].toArray();
        for (int i = 0; i < entries.size(); ++i) {
            auto entry = entries[i].toObject();
            entry["message"] = clipMessage(entry["message"].toString());
            entries[i] = entry;
        }
        report[list] = entries;
    }
    // Locations: at most 32 per kind, 64 kinds.
    auto locations = report["locations"].toArray();
    std::map<QString, int> perKind;
    QJsonArray kept;
    for (const auto &entry : locations) {
        auto location = entry.toObject();
        const auto kind = location["kind"].toString();
        if (perKind.size() >= 64 && !perKind.contains(kind))
            continue;
        if (++perKind[kind] > 32)
            continue;
        location["message"] = clipMessage(location["message"].toString());
        kept.append(location);
    }
    report["locations"] = kept;
    boundList(report, "kernelMessages", 64);
    const auto size = [&] { return QJsonDocument(report).toJson(QJsonDocument::Compact).size(); };
    // Still over 64 KiB: shorten the examples before anything that counts.
    for (const int perKindLimit : {8, 2, 0}) {
        if (size() <= 64 * 1024)
            break;
        std::map<QString, int> seen;
        QJsonArray fewer;
        for (const auto &entry : report["locations"].toArray())
            if (++seen[entry.toObject()["kind"].toString()] <= perKindLimit)
                fewer.append(entry);
        report["locations"] = fewer;
    }
    if (size() > 64 * 1024) {
        auto messages = report["kernelMessages"].toArray();
        for (int i = 0; i < messages.size(); ++i) {
            auto entry = messages[i].toObject();
            entry["message"] = "";
            messages[i] = entry;
        }
        report["kernelMessages"] = messages;
    }
}
namespace {
// ---- The converter --------------------------------------------------------------------------------

struct PropertyPlan {
    // Per node: which entries were dropped by the aggregate or chunk caps.
    std::size_t aggregateBudget{propertyAggregateBytes};
};

struct NodeProps {
    std::vector<std::pair<std::string, Value>> provenance;
    struct Entry {
        std::string key, set;
        int origin{};
        Value value;
        std::size_t bytes{};
        bool dropped{};
    };
    std::vector<Entry> entries;
};

struct Counters {
    LimitCounts unique, materialized;
};

class Converter {
  public:
    Converter(const IfcPackage &p, const IfcImportOptions &o, const IfcRunFacts &f, const IfcConversionHooks &h)
        : pkg(p), options(o), facts(f), hooks(h), step(p.engine["kernel"].toString() == QLatin1String("step")) {}
    IfcImport run();

  private:
    const IfcPackage &pkg;
    IfcImportOptions options;
    IfcRunFacts facts;
    IfcConversionHooks hooks;
    // R098.b: the package the worker's STEP import wrote (engine.kernel "step"): the bodies take
    // the tag family STEP and the edit is labelled Import STEP (ADR 0172 section 4).
    bool step;

    // Persistent across attempts.
    std::map<std::string, Id> tagIds;
    std::size_t familyTags{};
    std::vector<std::shared_ptr<TagRecord>> tags;
    std::map<std::uint32_t, Id> nativeMaterials;
    std::set<std::string> materialNames;
    std::vector<std::shared_ptr<MaterialRecord>> materials;
    std::map<std::size_t, MeshGeometry> geometry;
    std::map<std::string, long long> notices;
    std::map<std::string, std::size_t> propertiesDropped;
    std::size_t materialsOverLimit{};

    // Per attempt.
    struct Attempt {
        Document document;
        long long bodies{}, geometryBodies{}, groups{}, definitions{}, instances{}, skipped{};
        std::size_t vertices{}, faces{}, edges{}, sourceTriangles{}, degenerate{}, nonPlanar{}, mergedFaces{},
            mergeFallbacks{};
        std::map<std::string, long long> skippedReasons;
        std::vector<std::tuple<std::string, std::uint32_t, std::string>> skippedWhere; // class, step, reason
        std::map<std::string, long long> byRole;
        std::size_t propertyBytes{}, propertyCount{};
        std::map<std::string, long long> notImportedByClass;
        std::size_t encodedBytes{};
        std::size_t droppedForSize{};
        QJsonObject georeference; // the report's block, rebuilt from the worker's by evaluateGeoreference
    };

    void note(const std::string &code, long long count = 1) { notices[code] += count; }
    Id tagFor(const IfcPackageNode &node);
    Id materialFor(std::uint32_t index);
    MeshGeometry &geometryFor(std::size_t mesh);
    std::vector<NodeProps> planProperties(std::size_t aggregateBudget);
    Attempt build(std::size_t aggregateBudget);
    // The worker's georeference block, checked: the report's block, and the site origin the document gets (if any).
    std::pair<QJsonObject, std::optional<SiteOrigin>> evaluateGeoreference();
    QJsonObject makeReport(const Attempt &attempt, bool spacesSkipped);
};

Id Converter::tagFor(const IfcPackageNode &node) {
    std::string name;
    bool visible = true, family = false;
    switch (node.role) {
    case IfcNodeRole::Space:
        name = "IFC Space";
        visible = options.spaces == IfcSpaces::Shown;
        break;
    case IfcNodeRole::Opening:
        name = "IFC Opening";
        break;
    case IfcNodeRole::Element:
    case IfcNodeRole::Assembly:
        name = step ? stepTagName(node.className) : ifcTagName(node.className);
        family = true;
        break;
    default:
        return 0;
    }
    if (const auto found = tagIds.find(name); found != tagIds.end())
        return found->second;
    // At most 128 class-family tags; the rest share the format's Other. The space and opening tags are not among them.
    const char *other = step ? "STEP Other" : "IFC Other";
    if (family && familyTags >= maxFamilyTags) {
        note("tagsOverLimit");
        name = other;
        if (const auto found = tagIds.find(name); found != tagIds.end())
            return found->second;
    }
    auto tag = std::make_shared<TagRecord>();
    tag->id = Id(tags.size()) + 1;
    tag->name = name;
    tag->visible = visible;
    tags.push_back(tag);
    tagIds[name] = tag->id;
    if (family && name != other)
        ++familyTags;
    return tag->id;
}

Id Converter::materialFor(std::uint32_t index) {
    if (index == IfcPackageMesh::noMaterial || index >= pkg.materials.size())
        return 0;
    if (const auto found = nativeMaterials.find(index); found != nativeMaterials.end())
        return found->second;
    if (materials.size() >= 1024) {
        nativeMaterials[index] = 0;
        ++materialsOverLimit;
        return 0;
    }
    const auto &source = pkg.materials[index];
    auto record = std::make_shared<MaterialRecord>();
    record->id = Id(materials.size()) + 1;
    std::string name = plainText(source.name, 200);
    if (name.empty())
        name = "IFC material " + std::to_string(record->id);
    const std::string original = name;
    for (std::size_t suffix = 1; !materialNames.insert(name).second; ++suffix)
        name = original + " (IFC " + std::to_string(suffix) + ")";
    if (name != original)
        note("duplicateMaterialNamesRenamed");
    record->name = name;
    record->color = source.color;
    record->opacity = source.opacity;
    materials.push_back(record);
    nativeMaterials[index] = record->id;
    return record->id;
}

MeshGeometry &Converter::geometryFor(std::size_t mesh) {
    const auto found = geometry.find(mesh);
    if (found != geometry.end())
        return found->second;
    auto built = buildMeshGeometry(pkg.meshes[mesh], [&](std::uint32_t index) { return materialFor(index); });
    return geometry.emplace(mesh, std::move(built)).first->second;
}

// Provenance first (at most 16 slots), then occurrence, then type properties; per body at most 128.
std::vector<NodeProps> Converter::planProperties(std::size_t aggregateBudget) {
    std::vector<NodeProps> plans(pkg.nodes.size());
    std::size_t total = 0;
    for (std::size_t i = 0; i < pkg.nodes.size(); ++i) {
        const auto &node = pkg.nodes[i];
        auto &plan = plans[i];
        std::set<std::string> keys;
        std::size_t kept = 0;
        for (const auto &[key, value] : node.provenance) {
            if (plan.provenance.size() >= reservedProvenance) {
                ++propertiesDropped["ifc"];
                continue;
            }
            Value stored = value;
            if (auto *text = std::get_if<std::string>(&stored))
                if (text->size() > 2048) {
                    *text = clipUtf8(*text, 2048);
                    note("propertyTextShortened");
                }
            keys.insert(key);
            plan.provenance.emplace_back(key, std::move(stored));
            total += key.size() + valueBytes(plan.provenance.back().second) + 16;
            ++kept;
        }
        for (const auto &p : node.properties) {
            std::string key = p.set.empty() ? p.name : p.set + "." + p.name;
            if (key.size() > 128) {
                key = clipUtf8(key, 128);
                note("propertyKeyShortened");
            }
            if (key.empty()) {
                ++propertiesDropped[p.set];
                continue;
            }
            if (!keys.insert(key).second) {
                note("propertyKeyCollision");
                continue;
            }
            if (kept >= 128) {
                ++propertiesDropped[p.set];
                continue;
            }
            Value stored = p.value;
            if (auto *text = std::get_if<std::string>(&stored))
                if (text->size() > 2048) {
                    *text = clipUtf8(*text, 2048);
                    note("propertyTextShortened");
                }
            NodeProps::Entry entry;
            entry.key = std::move(key);
            entry.set = p.set;
            entry.origin = p.origin;
            entry.bytes = entry.key.size() + valueBytes(stored) + 16;
            entry.value = std::move(stored);
            total += entry.bytes;
            plan.entries.push_back(std::move(entry));
            ++kept;
        }
    }
    // Aggregate cap: type-level before occurrence-level, quantities before property sets, last nodes first.
    for (const int origin : {3, 2, 1, 0}) {
        for (std::size_t i = plans.size(); i-- > 0 && total > aggregateBudget;)
            for (auto it = plans[i].entries.rbegin(); it != plans[i].entries.rend() && total > aggregateBudget; ++it)
                if (!it->dropped && it->origin == origin) {
                    it->dropped = true;
                    total -= it->bytes;
                    ++propertiesDropped[it->set];
                }
    }
    return plans;
}

namespace {
std::optional<double> finiteNumber(const QJsonValue &value) {
    if (!value.isDouble())
        return std::nullopt;
    const double number = value.toDouble();
    if (!std::isfinite(number))
        return std::nullopt;
    return number;
}
bool near(double a, double b, double bound) { return std::abs(a - b) <= bound; }
} // namespace

// ADR 0171 section 3.6. The worker decides what a map conversion means; the application rebuilds the report block from
// known fields only (it is data), and sets a site origin only when everything the worker says agrees with itself: the
// origin is the conversion's own numbers, and the package's roots carry exactly the rotation the worker says it applied.
std::pair<QJsonObject, std::optional<SiteOrigin>> Converter::evaluateGeoreference() {
    const QJsonObject w = pkg.report["georeference"].toObject();
    const bool grid = options.georeference == IfcGeoreference::Grid;
    QJsonObject block;
    block["mode"] = grid ? "grid" : "keepAxes";
    QString result = w["result"].toString();
    static const std::set<QString> results{"none", "siteOrigin", "gridAligned", "notRepresentable"};
    if (!results.contains(result)) {
        // A package with no block (an older or hand-built one) says nothing about georeferencing.
        result = w.isEmpty() ? QStringLiteral("none") : QStringLiteral("notRepresentable");
    }
    block["result"] = result;
    for (const char *count : {"mapConversionsInFile", "mapConversionsOnModelContext", "modelContexts", "otherCoordinateOperations", "epsets"})
        if (w[count].isDouble())
            block[count] = std::clamp<qint64>(w[count].toInteger(0), 0, 1000000000);
    if (w["reason"].isString())
        block["reason"] = QString::fromStdString(plainText(w["reason"].toString().toStdString(), 240));
    const QJsonObject conversion = w["mapConversion"].toObject();
    std::optional<double> easting, northing, height, rotation, scale;
    if (!conversion.isEmpty()) {
        QJsonObject kept;
        for (const char *field : {"eastings", "northings", "orthogonalHeight", "xAxisAbscissa", "xAxisOrdinate", "rotationDegrees", "scale"})
            if (const auto value = finiteNumber(conversion[field]))
                kept[field] = *value;
        if (conversion["step"].isDouble())
            kept["step"] = conversion["step"].toInteger(0);
        if (conversion["factors"].isArray()) {
            QJsonArray factors;
            const auto given = conversion["factors"].toArray();
            for (qsizetype i = 0; i < std::min<qsizetype>(3, given.size()); ++i)
                if (const auto value = finiteNumber(given[i]))
                    factors.append(*value);
            kept["factors"] = factors;
        }
        easting = finiteNumber(conversion["eastings"]);
        northing = finiteNumber(conversion["northings"]);
        height = finiteNumber(conversion["orthogonalHeight"]);
        rotation = finiteNumber(conversion["rotationDegrees"]);
        scale = finiteNumber(conversion["scale"]);
        block["mapConversion"] = kept;
    }
    const QJsonObject crs = w["crs"].toObject();
    std::string crsLabel;
    if (!crs.isEmpty()) {
        QJsonObject kept;
        const std::pair<const char *, std::size_t> texts[]{{"name", 64}, {"description", 120}, {"geodeticDatum", 64}, {"mapProjection", 64}, {"mapZone", 64}, {"mapUnit", 48}};
        for (const auto &[field, bytes] : texts)
            if (crs[field].isString())
                kept[field] = QString::fromStdString(plainText(crs[field].toString().toStdString(), bytes));
        if (const auto metres = finiteNumber(crs["mapUnitMetres"]))
            kept["mapUnitMetres"] = *metres;
        if (crs["mapUnitGiven"].isBool())
            kept["mapUnitGiven"] = crs["mapUnitGiven"].toBool();
        crsLabel = plainText(crs["name"].toString().toStdString(), siteCrsBytes);
        block["crs"] = kept;
    }
    QJsonArray reasons;
    const auto reasonList = w["reasons"].toArray();
    for (qsizetype i = 0; i < std::min<qsizetype>(8, reasonList.size()); ++i) {
        const auto reason = reasonList[i].toObject();
        reasons.append(QJsonObject{{"code", QString::fromStdString(plainText(reason["code"].toString().toStdString(), 64))},
                                   {"message", QString::fromStdString(plainText(reason["message"].toString().toStdString(), 240))}});
    }
    if (!reasons.isEmpty())
        block["reasons"] = reasons;
    block["siteOriginSet"] = false;
    if (result != "siteOrigin" && result != "gridAligned")
        return {block, std::nullopt};

    // The worker says a site origin is set. Everything it rests on is checked here.
    const auto fail = [&](const std::string &why) -> std::pair<QJsonObject, std::optional<SiteOrigin>> {
        throw IfcImportError("georeferenceMismatch", "The helper's georeference does not agree with itself: " + why);
    };
    const QJsonObject proposed = w["siteOrigin"].toObject();
    if (!easting || !northing || !height || !rotation || !scale)
        return fail("a site origin without a complete map conversion");
    const auto pe = finiteNumber(proposed["easting"]), pn = finiteNumber(proposed["northing"]), ph = finiteNumber(proposed["elevation"]);
    if (!pe || !pn || !ph || !near(*pe, *easting, 1e-9) || !near(*pn, *northing, 1e-9) || !near(*ph, *height, 1e-9))
        return fail("the site origin is not the conversion's eastings, northings and height");
    // The rest of the formula (3.6): a site origin needs a conversion with nothing in the way, in metres, whose scale is the
    // project unit; and it is turned exactly when the conversion's X axis is not along +X.
    if (!reasons.isEmpty())
        return fail("a site origin together with the reasons it cannot be held");
    const auto abscissa = finiteNumber(conversion["xAxisAbscissa"]), ordinate = finiteNumber(conversion["xAxisOrdinate"]);
    const double metresPerUnit = pkg.units["metresPerUnit"].toDouble(0);
    if (!abscissa || !ordinate || !(metresPerUnit > 0) || !near(*scale, metresPerUnit, 1e-9 * metresPerUnit))
        return fail("the conversion's axis or scale is not the project unit in metres");
    if (crs["mapUnitMetres"].isDouble() && !near(crs["mapUnitMetres"].toDouble(), 1.0, 1e-9))
        return fail("the CRS's map unit is not the metre");
    const bool rotated = !(*abscissa > 0 && std::abs(*ordinate) <= 1e-12 * *abscissa);
    if (rotated != (result == "gridAligned"))
        return fail(rotated ? "a rotated conversion without the rotation of the model" : "a rotation of the model for a conversion that has none");
    const double axisDegrees = std::atan2(*ordinate, *abscissa) * 180.0 / 3.14159265358979323846;
    if (!near(*rotation, axisDegrees, 1e-9))
        return fail("the conversion's rotation is not its X axis direction");
    const double applied = finiteNumber(w["rotationAppliedDegrees"]).value_or(std::nan(""));
    const double expected = rotated ? axisDegrees : 0.0;
    if (!std::isfinite(applied) || !near(applied, expected, 1e-9))
        return fail("the rotation applied is not the conversion's");
    if (result == "gridAligned" && !grid)
        return fail("the model was rotated though only the project axes were asked for");
    const double radians = expected * 3.14159265358979323846 / 180.0;
    const double c = std::cos(radians), s = std::sin(radians);
    for (const auto &node : pkg.nodes) {
        if (node.parent >= 0)
            continue;
        const auto &t = node.transform;
        const bool exact = near(t[0], c, 1e-9) && near(t[1], -s, 1e-9) && near(t[4], s, 1e-9) && near(t[5], c, 1e-9) && near(t[10], 1, 1e-9) &&
                           near(t[2], 0, 1e-9) && near(t[6], 0, 1e-9) && near(t[8], 0, 1e-9) && near(t[9], 0, 1e-9) &&
                           near(t[3], 0, 1e-9) && near(t[7], 0, 1e-9) && near(t[11], 0, 1e-9);
        if (!exact)
            return fail("a root node does not carry the rotation of the site origin");
    }
    std::string digest = facts.sha256.toStdString();
    if (digest.size() != 64 || !std::all_of(digest.begin(), digest.end(), [](char ch) { return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'); }))
        digest.clear();
    SiteOrigin origin{*pe, *pn, *ph, crsLabel, {"IfcMapConversion", digest}};
    try {
        validateSiteOrigin(origin);
    } catch (const std::exception &error) {
        return fail(error.what());
    }
    block["siteOriginSet"] = true;
    block["siteOrigin"] = QJsonObject{{"easting", origin.easting},
                                      {"northing", origin.northing},
                                      {"elevation", origin.elevation},
                                      {"crs", QString::fromStdString(origin.crs)},
                                      {"source", QString::fromStdString(origin.established.source)},
                                      {"sha256", QString::fromStdString(origin.established.sha256)}};
    block["rotationAppliedDegrees"] = expected;
    block["documentSchema"] = 29;
    block["requires"] = "SketchyUp 1.3.0 or later";
    return {block, origin};
}

Converter::Attempt Converter::build(std::size_t aggregateBudget) {
    Attempt attempt;
    propertiesDropped.clear();
    const auto plans = planProperties(aggregateBudget);
    Edit edit{step ? "Import STEP" : "Import IFC", {}};
    {
        auto [block, origin] = evaluateGeoreference();
        attempt.georeference = std::move(block);
        if (origin) {
            edit.site = std::pair{std::optional<SiteOrigin>{}, std::optional<SiteOrigin>{*origin}};
            note("siteOriginSet");
        }
    }
    const std::size_t count = pkg.nodes.size();
    std::vector<Id> container(count, 0);
    std::vector<bool> skip(count, false);
    std::vector<Transform> world(count);
    Id nextBody = 1, nextDefinition = 1;

    // Which meshes are shared: used by more than one node that will be placed.
    std::map<std::size_t, std::size_t> uses;
    for (const auto &node : pkg.nodes)
        if (node.mesh >= 0 && !(node.role == IfcNodeRole::Space && options.spaces == IfcSpaces::Skip))
            ++uses[std::size_t(node.mesh)];
    std::vector<std::pair<std::size_t, std::size_t>> shareable;
    for (const auto &[mesh, n] : uses)
        if (n > 1)
            shareable.emplace_back(n, mesh);
    std::sort(shareable.begin(), shareable.end(), [](auto a, auto b) { return a.first != b.first ? a.first > b.first : a.second < b.second; });
    std::map<std::size_t, DefinitionPtr> definitions;
    std::size_t expanded = 0;
    for (std::size_t i = 0; i < shareable.size(); ++i)
        if (i >= 1024)
            ++expanded;
    if (expanded)
        note("definitionsOverLimit");

    Counters counters;
    const auto tryAdd = [&](const LimitCounts &addUnique, const LimitCounts &addMaterialized, std::string &exceeded) {
        LimitCounts u = counters.unique, m = counters.materialized;
        const auto plus = [](LimitCounts &a, const LimitCounts &b) {
            a.placements += b.placements;
            a.bodies += b.bodies;
            a.vertices += b.vertices;
            a.faces += b.faces;
            a.wires += b.wires;
            a.edges += b.edges;
        };
        plus(u, addUnique);
        plus(m, addMaterialized);
        auto bad = exceededLimit(u, LimitClass::UniqueGeometry);
        if (bad.empty())
            bad = exceededLimit(m, LimitClass::Placement);
        if (!bad.empty()) {
            exceeded = std::string(bad);
            return false;
        }
        counters.unique = u;
        counters.materialized = m;
        return true;
    };
    const auto transformOf = [&](const IfcPackageNode &node) {
        Transform t;
        // The package is row-major with the translation in the last column; the native matrix is
        // column-major.
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                t.m[std::size_t(c * 4 + r)] = node.transform[std::size_t(r * 4 + c)];
        t.validate();
        return t;
    };
    const auto skipNode = [&](std::size_t index, const std::string &reason) {
        skip[index] = true;
        ++attempt.skipped;
        ++attempt.skippedReasons[reason];
        const auto &node = pkg.nodes[index];
        ++attempt.notImportedByClass[node.className + "|" + reason];
        if (attempt.skippedWhere.size() < 4096)
            attempt.skippedWhere.emplace_back(node.className, node.step, reason);
    };
    std::vector<bool> holdsNodes(count, false);
    for (const auto &node : pkg.nodes)
        if (node.parent >= 0)
            holdsNodes[std::size_t(node.parent)] = true;
    std::set<std::size_t> sharedMeshes;
    for (std::size_t i = 0; i < shareable.size() && i < 1024; ++i)
        sharedMeshes.insert(shareable[i].second);
    double extent = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (hooks.cancelled && (i % 128 == 0) && hooks.cancelled())
            throw IfcImportError("cancelled", "The import was cancelled.");
        const auto &node = pkg.nodes[i];
        if (node.parent >= 0 && skip[std::size_t(node.parent)]) {
            skipNode(i, "parentNotImported");
            continue;
        }
        if (node.role == IfcNodeRole::Space && options.spaces == IfcSpaces::Skip) {
            skipNode(i, "spacesSkipped");
            note("spacesSkipped");
            continue;
        }
        Transform local;
        try {
            local = transformOf(node);
        } catch (const std::exception &) {
            skipNode(i, "badTransform");
            continue;
        }
        Transform parentWorld;
        if (node.parent >= 0)
            parentWorld = world[std::size_t(node.parent)];
        try {
            world[i] = parentWorld * local;
        } catch (const std::exception &) {
            skipNode(i, "badTransform");
            continue;
        }
        const bool hasMesh = node.mesh >= 0;
        const auto meshIndex = std::size_t(std::max<std::int64_t>(node.mesh, 0));
        MeshGeometry *mesh = nullptr;
        if (hasMesh) {
            mesh = &geometryFor(meshIndex);
            if (mesh->stats.empty) {
                // Everything collapsed: the element has no usable geometry (it stays a group when it holds others).
                attempt.degenerate += mesh->stats.degenerate;
            } else if (mesh->stats.tooLarge) {
                skipNode(i, "bodyLimit");
                continue;
            }
        }
        const bool geometric = hasMesh && mesh && !mesh->stats.empty;
        // World extent of the mesh (its bounding box corners).
        if (geometric) {
            for (int corner = 0; corner < 8; ++corner) {
                const Vec3 c{(corner & 1) ? mesh->hi.x : mesh->lo.x, (corner & 2) ? mesh->hi.y : mesh->lo.y,
                             (corner & 4) ? mesh->hi.z : mesh->lo.z};
                const auto w = world[i].point(c);
                extent = std::max({extent, std::abs(w.x), std::abs(w.y), std::abs(w.z)});
            }
        }
        extent = std::max({extent, std::abs(world[i].m[12]), std::abs(world[i].m[13]), std::abs(world[i].m[14])});
        if (extent > coordinateLimit)
            throw IfcImportError("farFromOrigin", "The model extends " + std::to_string(std::llround(extent)) +
                                                      " m from the origin; coordinates beyond 1,000,000 m are not imported.");
        const bool hasKids = holdsNodes[i];
        const bool shared = geometric && sharedMeshes.contains(meshIndex);
        const auto &stats = geometric ? mesh->stats : GeometryStats{};
        LimitCounts addUnique{}, addMaterialized{};
        addUnique.bodies = 1;
        addMaterialized.bodies = 1;
        const bool separateGeometry = geometric && (hasKids || shared);
        if (geometric) {
            if (shared) {
                const bool first = !definitions.contains(meshIndex);
                if (first) {
                    addUnique.bodies += 2;
                    addUnique.vertices += stats.vertices;
                    addUnique.faces += stats.faces;
                    addUnique.edges += stats.edges;
                }
                addMaterialized.bodies += 2;
                addMaterialized.placements = 1;
                addMaterialized.vertices += stats.vertices;
                addMaterialized.faces += stats.faces;
                addMaterialized.edges += stats.edges;
            } else {
                if (separateGeometry) {
                    addUnique.bodies += 1;
                    addMaterialized.bodies += 1;
                }
                addUnique.vertices += stats.vertices;
                addUnique.faces += stats.faces;
                addUnique.edges += stats.edges;
                addMaterialized.vertices += stats.vertices;
                addMaterialized.faces += stats.faces;
                addMaterialized.edges += stats.edges;
            }
        }
        std::string exceeded;
        if (!tryAdd(addUnique, addMaterialized, exceeded)) {
            skipNode(i, "documentLimit:" + exceeded);
            continue;
        }
        if (geometric)
            finishMeshGeometry(*mesh, pkg.meshes[meshIndex]);

        const Id tag = tagFor(node);
        const std::string name = plainText(node.name, 256);
        auto stamp = [&](Body &body, bool full) {
            body.name = name.empty() ? node.className : name;
            body.tag = tag;
            body.hidden = node.hidden;
            if (full) {
                for (const auto &[key, value] : plans[i].provenance)
                    body.properties.emplace(key, value);
                for (const auto &entry : plans[i].entries)
                    if (!entry.dropped)
                        body.properties.emplace(entry.key, entry.value);
            }
        };
        if (!geometric || separateGeometry) {
            // A group: the node's own body.
            auto group = std::make_shared<Body>();
            group->id = nextBody++;
            group->kind = BodyKind::Group;
            group->parent = node.parent >= 0 ? container[std::size_t(node.parent)] : 0;
            group->transform = local;
            stamp(*group, true);
            for (const auto &[key, value] : plans[i].provenance)
                attempt.propertyBytes += key.size() + valueBytes(value) + 16;
            for (const auto &entry : plans[i].entries)
                if (!entry.dropped)
                    attempt.propertyBytes += entry.bytes;
            container[i] = group->id;
            edit.changes.push_back({group->id, nullptr, group});
            ++attempt.groups;
            ++attempt.bodies;
            if (geometric && shared) {
                // One component definition per shared mesh, one placement per use.
                DefinitionPtr definition;
                if (const auto found = definitions.find(meshIndex); found != definitions.end()) {
                    definition = found->second;
                } else {
                    auto d = std::make_shared<ComponentDefinition>();
                    d->id = nextDefinition++;
                    d->root = 1;
                    // A STEP instance keeps its product's name (ADR 0172); an IFC definition keeps the class name.
                    d->name = plainText(step && !node.name.empty() ? node.name : (node.className + " " + std::to_string(node.step)), 200);
                    auto root = std::make_shared<Body>();
                    root->id = 1;
                    root->kind = BodyKind::Group;
                    root->name = d->name;
                    d->members[1] = root;
                    auto member = std::make_shared<Body>(*mesh->body);
                    member->id = 2;
                    member->parent = 1;
                    member->name = "Geometry";
                    // A STEP instance keeps the part's provenance on the canonical member (ADR 0172 section 4):
                    // the instance's placed bodies are its exact copies, so the keys ride there.
                    if (step) {
                        for (const auto &[key, value] : plans[i].provenance)
                            member->properties.emplace(key, value);
                    }
                    d->members[2] = member;
                    d->nextMemberId = 3;
                    definition = d;
                    definitions[meshIndex] = definition;
                    edit.definitions.push_back({d->id, nullptr, definition});
                    ++attempt.definitions;
                }
                auto instance = std::make_shared<ComponentInstance>();
                instance->definition = definition->id;
                for (const auto &[memberId, canonical] : definition->members) {
                    auto placed = std::make_shared<Body>(*canonical);
                    placed->id = nextBody++;
                    placed->parent = memberId == definition->root ? group->id : instance->members.at(canonical->parent);
                    instance->members[memberId] = placed->id;
                    edit.changes.push_back({placed->id, nullptr, placed});
                    ++attempt.bodies;
                }
                edit.instances.push_back({instance->members.at(definition->root), nullptr, instance});
                ++attempt.instances;
            } else if (geometric) {
                auto body = std::make_shared<Body>(*mesh->body);
                body->id = nextBody++;
                body->parent = group->id;
                body->name = group->name;
                body->tag = tag;
                body->hidden = false;
                for (const auto &[key, value] : plans[i].provenance)
                    if (key == "ifc.Class" || key == "ifc.Step" || key == "ifc.Representation" || key == "ifc.Conversion")
                        body->properties.emplace(key, value);
                edit.changes.push_back({body->id, nullptr, body});
                ++attempt.bodies;
                ++attempt.geometryBodies;
            }
        } else {
            // A single geometry body carries the node's identity, properties and placement.
            auto body = std::make_shared<Body>(*mesh->body);
            body->id = nextBody++;
            body->parent = node.parent >= 0 ? container[std::size_t(node.parent)] : 0;
            body->transform = local;
            stamp(*body, true);
            for (const auto &[key, value] : plans[i].provenance)
                attempt.propertyBytes += key.size() + valueBytes(value) + 16;
            for (const auto &entry : plans[i].entries)
                if (!entry.dropped)
                    attempt.propertyBytes += entry.bytes;
            container[i] = body->id;
            edit.changes.push_back({body->id, nullptr, body});
            ++attempt.bodies;
            ++attempt.geometryBodies;
        }
        if (geometric) {
            attempt.sourceTriangles += stats.sourceTriangles;
            attempt.degenerate += stats.degenerate;
            attempt.nonPlanar += stats.nonPlanar;
            attempt.mergedFaces += stats.mergedFaces;
            attempt.mergeFallbacks += stats.mergeFallbacks;
        }
        ++attempt.byRole[ifcNodeRoleName(node.role)];
    }
    // Tags and materials that exist.
    for (const auto &tag : tags)
        edit.tags.push_back({tag->id, nullptr, tag});
    for (const auto &material : materials)
        edit.materials.push_back({material->id, nullptr, material});
    edit.nextIdFloor = nextBody;
    edit.nextTagFloor = Id(tags.size()) + 1;
    edit.nextMaterialFloor = Id(materials.size()) + 1;
    edit.nextDefinitionFloor = nextDefinition;
    attempt.document.apply(std::move(edit), attempt.document.revision());
    attempt.vertices = counters.unique.vertices;
    attempt.faces = counters.unique.faces;
    attempt.edges = counters.unique.edges;
    return attempt;
}

QJsonObject Converter::makeReport(const Attempt &attempt, bool spacesSkipped) {
    (void)spacesSkipped;
    const auto &w = pkg.report;
    QJsonObject report;
    report["apiVersion"] = 1;
    report["format"] = "IFC";
    report["direction"] = "import";
    QJsonObject engine = pkg.engine;
    for (auto it = facts.run.begin(); it != facts.run.end(); ++it)
        engine[it.key()] = it.value();
    report["engine"] = engine;
    QJsonObject source = pkg.source;
    source["sha256"] = facts.sha256;
    source["bytes"] = facts.bytes;
    report["source"] = source;
    report["units"] = pkg.units;
    report["georeference"] = attempt.georeference;
    report["spatial"] = w["spatial"];
    // Elements.
    QJsonObject elements = w["elements"].toObject();
    QJsonArray notImported = elements["notImported"].toArray();
    std::map<std::pair<std::string, std::string>, long long> extra;
    for (const auto &[key, count] : attempt.notImportedByClass) {
        const auto bar = key.find('|');
        extra[{key.substr(0, bar), key.substr(bar + 1)}] += count;
    }
    for (const auto &[key, count] : extra)
        notImported.append(QJsonObject{{"class", QString::fromStdString(key.first)},
                                       {"reason", QString::fromStdString(key.second)},
                                       {"count", qint64(count)}});
    elements["notImported"] = notImported;
    report["elements"] = elements;
    // Geometry.
    QJsonObject geometryReport = w["geometry"].toObject();
    geometryReport["bodies"] = qint64(attempt.geometryBodies);
    geometryReport["groups"] = qint64(attempt.groups);
    geometryReport["vertices"] = qint64(attempt.vertices);
    geometryReport["faces"] = qint64(attempt.faces);
    geometryReport["edges"] = qint64(attempt.edges);
    geometryReport["definitions"] = qint64(attempt.definitions);
    geometryReport["instances"] = qint64(attempt.instances);
    geometryReport["sourceTriangles"] = qint64(attempt.sourceTriangles);
    geometryReport["mergedFaces"] = qint64(attempt.mergedFaces);
    geometryReport["nonPlanarFaces"] = qint64(attempt.nonPlanar);
    geometryReport["degenerateTriangles"] = qint64(attempt.degenerate);
    geometryReport["mergeFallbacks"] = qint64(attempt.mergeFallbacks);
    report["geometry"] = geometryReport;
    // Properties.
    QJsonObject properties;
    const auto workerProperties = w["properties"].toObject();
    const auto counts = workerProperties["properties"].toObject();
    properties["psets"] = counts["psets"];
    properties["properties"] = counts["properties"];
    properties["quantities"] = counts["quantities"];
    properties["typeOverridden"] = counts["typeOverridden"];
    properties["omitted"] = workerProperties["propertiesOmitted"];
    properties["unmapped"] = workerProperties["propertiesUnmapped"];
    QJsonObject dropped;
    long long droppedTotal = 0;
    for (const auto &[set, n] : propertiesDropped) {
        dropped[QString::fromStdString(set)] = qint64(n);
        droppedTotal += qint64(n);
    }
    properties["dropped"] = dropped;
    properties["droppedTotal"] = qint64(droppedTotal);
    properties["bytes"] = qint64(attempt.propertyBytes);
    report["properties"] = properties;
    QJsonObject materialsReport = w["materials"].toObject();
    materialsReport["materials"] = qint64(materials.size());
    materialsReport["materialsOverLimit"] = qint64(materialsOverLimit);
    report["materials"] = materialsReport;
    QJsonObject accounting = w["accounting"].toObject();
    report["accounting"] = accounting;
    report["omittedEntities"] = w["omittedEntities"];
    report["losses"] = w["losses"];
    report["kernelMessages"] = w["kernelMessages"];
    report["unitsUnconverted"] = w["unitsUnconverted"];
    // Notices: the worker's and the conversion's, with their texts.
    std::map<std::string, long long> all = notices;
    for (const auto &entry : w["notices"].toArray()) {
        const auto o = entry.toObject();
        all[o["code"].toString().toStdString()] += o["count"].toInteger();
    }
    if (droppedTotal)
        all["propertiesDropped"] += droppedTotal;
    QJsonArray noticeList;
    for (const auto &[code, n] : all) {
        const auto found = noticeTexts().find(code);
        noticeList.append(QJsonObject{{"code", QString::fromStdString(code)},
                                      {"count", qint64(n)},
                                      {"message", QString::fromStdString(found == noticeTexts().end() ? "See the code." : found->second)}});
    }
    report["notices"] = noticeList;
    // Locations: the worker's, then what the conversion skipped.
    QJsonArray locations = w["locations"].toArray();
    QJsonObject totals = w["locationTotals"].toObject();
    std::map<std::string, int> perKind;
    for (const auto &entry : locations)
        ++perKind[entry.toObject()["kind"].toString().toStdString()];
    for (const auto &[cls, step, reason] : attempt.skippedWhere) {
        totals["skipped"] = totals["skipped"].toInt() + 1;
        if (perKind["skipped"] >= 32)
            continue;
        ++perKind["skipped"];
        locations.append(QJsonObject{{"kind", "skipped"},
                                     {"step", qint64(step)},
                                     {"globalId", ""},
                                     {"message", QString::fromStdString(cls + ": " + reason)}});
    }
    report["locations"] = locations;
    report["locationTotals"] = totals;
    report["excludedSteps"] = w["excludedSteps"];
    report["precision"] = w["precision"];
    return report;
}

IfcImport Converter::run() {
    if (pkg.nodes.empty())
        throw IfcImportError("nothingToImport", "The package holds nothing to import.");
    std::size_t budget = propertyAggregateBytes;
    Attempt attempt;
    for (int round = 0; round < 3; ++round) {
        notices.clear();
        try {
            attempt = build(budget);
        } catch (const IfcImportError &) {
            throw;
        } catch (const std::exception &error) {
            // The document refused the edit (a limit or a validation the conversion did not foresee).
            const std::string message = error.what();
            throw IfcImportError(message.find("limit") != std::string::npos || message.find("exceeds") != std::string::npos ? "documentLimit"
                                                                                                                         : "validation",
                                 "The imported model was refused by the document: " + message);
        }
        // The whole serialization is validated, and its size checked against the model chunk.
        QByteArray bytes;
        try {
            bytes = encodeDocument(attempt.document);
        } catch (const std::exception &error) {
            throw IfcImportError("modelTooLarge", std::string("The imported model cannot be serialized: ") + error.what());
        }
        attempt.encodedBytes = std::size_t(bytes.size());
        const std::size_t limit = NativeLimits::modelBytes - modelMargin;
        if (attempt.encodedBytes <= limit) {
            try {
                (void)decodeDocument(bytes);
            } catch (const std::exception &error) {
                throw IfcImportError("validation", std::string("The imported model failed native validation: ") + error.what());
            }
            IfcImport result;
            result.report = makeReport(attempt, false);
            boundIfcReport(result.report);
            result.document = std::move(attempt.document);
            return result;
        }
        const std::size_t excess = attempt.encodedBytes - limit + (1024 * 1024);
        if (attempt.propertyBytes <= excess)
            throw IfcImportError("modelTooLarge",
                                 "The geometry and provenance alone exceed the model chunk (" + std::to_string(attempt.encodedBytes) + " bytes).");
        budget = attempt.propertyBytes - excess;
        note("propertiesDropped");
    }
    throw IfcImportError("modelTooLarge", "The imported model does not fit the model chunk.");
}

} // namespace
IfcImport importIfcPackage(const IfcPackage &package, const IfcImportOptions &options, const IfcRunFacts &facts,
                           const IfcConversionHooks &hooks) {
    return Converter(package, options, facts, hooks).run();
}
} // namespace sketchy
