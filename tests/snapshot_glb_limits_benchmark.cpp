// R112 (1.8.0) software measurement for the read-snapshot budget and the GLB visible-face cap
// (ADR 0005, ADR 0032, R112; method of R082w v3 and unique_limits_benchmark). Core and I/O only,
// synthetic documents only, never a timing gate. Each mode prints key: value lines.
//   charge <shape>            build the shape in this process and print its counts, its
//                             readSnapshotBytes() with the 64 MiB of assets, and its encoded chunk
//   write <shape> <file>      the same shape, saved as a container for the command-line steps
//   shapes                    the shape names and what they hold
// A shape is a set of component definitions, each placed one or more times. Every definition is
// placed at least once, because createComponent places the group it makes.
#include "core/assets.hpp"
#include "core/components.hpp"
#include "core/document_limits.hpp"
#include "core/groups.hpp"
#include "geometry/topology.hpp"
#include "integrations/glb_export.hpp"
#include "integrations/render_snapshot.hpp"
#include "io/document_io.hpp"
#include <QJsonDocument>
#include <unistd.h>
#include <QCoreApplication>
#include <QFile>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <numbers>
#include <set>
#include <stdexcept>
#include <utility>
#include <string>
#include <sys/resource.h>
#include <vector>
using namespace sketchy;
namespace {
using Clock = std::chrono::steady_clock;
void check(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}
double seconds(Clock::time_point from, Clock::time_point to) {
    return std::chrono::duration<double>(to - from).count();
}
// Resident set right now (not the peak), from /proc/self/statm.
double currentMiB() {
    QFile status("/proc/self/statm");
    check(status.open(QIODevice::ReadOnly), "Read current RSS");
    const auto columns = status.readAll().simplified().split(' ');
    check(columns.size() > 1, "Current RSS has resident pages");
    return double(columns[1].toLongLong()) * double(::sysconf(_SC_PAGESIZE)) / 1048576.0;
}
double peakMiB() {
    rusage usage{};
    check(getrusage(RUSAGE_SELF, &usage) == 0, "Read peak RSS");
    return usage.ru_maxrss / 1024.0;
}
// Every edge smooth and soft, as an imported smooth mesh stores them: one appearance record each.
void smoothEdges(Body &body) {
    for (const auto &[edge, record] : body.topology.edges) {
        (void)record;
        body.edgeAppearances[edge] = EdgeAppearance{false, true, true};
    }
}
// A closed triangulated sphere: 2 * sides * rings faces, 2 + sides * rings vertices, 3 edges per
// two faces (1.5 per face). The sphere of unique_limits_acceptance_tests, smooth-edged.
std::shared_ptr<Body> sphere(Id id, Vec3 centre, int sides, int rings) {
    const double radius = 20;
    auto body = std::make_shared<Body>();
    body->id = id;
    body->name = "Sphere " + std::to_string(id);
    auto &surface = body->surface;
    auto add = [&](Vec3 point) {
        const Id vertex = surface.nextId++;
        surface.vertices.emplace(vertex, centre + point);
        return vertex;
    };
    const Id north = add({0, 0, radius});
    const std::vector<Id> row(static_cast<size_t>(sides));
    std::vector<std::vector<Id>> ring(static_cast<size_t>(rings), row);
    for (int r = 0; r < rings; ++r) {
        const double theta = std::numbers::pi * (r + 1) / (rings + 1);
        for (int j = 0; j < sides; ++j) {
            const double phi = 2 * std::numbers::pi * j / sides;
            ring[size_t(r)][size_t(j)] =
                add({radius * std::sin(theta) * std::cos(phi),
                     radius * std::sin(theta) * std::sin(phi), radius * std::cos(theta)});
        }
    }
    const Id south = add({0, 0, -radius});
    for (int j = 0; j < sides; ++j) {
        const int next = (j + 1) % sides;
        surface.addFaceIds({{north, ring.front()[size_t(j)], ring.front()[size_t(next)]}});
    }
    for (int r = 0; r + 1 < rings; ++r)
        for (int j = 0; j < sides; ++j) {
            const int next = (j + 1) % sides;
            const auto &a = ring[size_t(r)], &b = ring[size_t(r + 1)];
            surface.addFaceIds({{a[size_t(j)], b[size_t(j)], b[size_t(next)]}});
            surface.addFaceIds({{a[size_t(j)], b[size_t(next)], a[size_t(next)]}});
        }
    for (int j = 0; j < sides; ++j) {
        const int next = (j + 1) % sides;
        surface.addFaceIds({{south, ring.back()[size_t(next)], ring.back()[size_t(j)]}});
    }
    body->topology = Topology::rebuild(body->surface, body->topology);
    smoothEdges(*body);
    return body;
}
// A zigzag strip of `triangles` triangles: triangles + 2 vertices, 2 * triangles + 1 edges.
// Vertices and faces go in directly, as the sphere does, because Surface::vertex scans.
std::shared_ptr<Body> strip(Id id, size_t triangles, Vec3 offset) {
    auto body = std::make_shared<Body>();
    body->id = id;
    body->name = "Strip " + std::to_string(id);
    auto &surface = body->surface;
    std::vector<Id> vertex;
    for (size_t k = 0; k < triangles + 2; ++k) {
        const Id v = surface.nextId++;
        surface.vertices.emplace(v, offset + Vec3{0.001 * double(k), (k % 2) * 0.001, 0});
        vertex.push_back(v);
    }
    for (size_t k = 0; k < triangles; ++k)
        surface.addFaceIds({{vertex[k], vertex[k + 1], vertex[k + 2]}});
    body->topology = Topology::rebuild(body->surface, body->topology);
    smoothEdges(*body);
    return body;
}
// A definition: its bodies (one or two strips, or one sphere), and how many placements it has.
struct Def {
    enum class Kind { Strip, Sphere } kind;
    size_t strips{};     // Strip: triangles per strip, one strip per body
    size_t count{1};     // Strip: bodies in the definition
    int sides{}, rings{}; // Sphere
    int placements{1};
};
struct Shape {
    const char *name;
    const char *what;
    std::vector<Def> defs;
};
// The shapes. `spheres-at-cap` puts 600,000 materialized faces in 300,000 unique faces of closed
// spheres: 2 * 50,000 faces per 100,000 faces, edges at 1.5 per face. `strips-at-limits` is the
// densest the published limits admit: strips have one vertex per face and two edges per face, so
// the materialized 900,000 edges bind first, and a small sphere is placed 100 times to fill the
// materialized edges that the 300,000 unique strip faces leave (see R112 in ADR 0005).
const std::vector<Shape> &shapes() {
    static const std::vector<Shape> all{
        {"spheres-at-cap", "three 100,000-face spheres, each placed twice: 300,000 unique faces, 600,000 materialized",
         {{Def::Kind::Sphere, 0, 1, 200, 250, 2},
          {Def::Kind::Sphere, 0, 1, 200, 250, 2},
          {Def::Kind::Sphere, 0, 1, 200, 250, 2}}},
        {"two-spheres-twice", "two 100,000-face spheres, each placed twice: 200,000 unique faces, 400,000 materialized (a catalog import's base)",
         {{Def::Kind::Sphere, 0, 1, 200, 250, 2},
          {Def::Kind::Sphere, 0, 1, 200, 250, 2}}},
        {"one-sphere", "one 100,000-face sphere, placed once (the entry a catalog import adds)",
         {{Def::Kind::Sphere, 0, 1, 200, 250, 1}}},
        {"strips-at-limits", "strips: two of 99,998 triangles and one of 98,004, each its own definition (one edit each, so the 192 MiB edit budget holds), and a 2,000-face sphere placed 100 times",
         {{Def::Kind::Strip, 99998, 1, 0, 0, 1},
          {Def::Kind::Strip, 99998, 1, 0, 0, 1},
          {Def::Kind::Strip, 98004, 1, 0, 0, 1},
          {Def::Kind::Sphere, 0, 1, 50, 20, 100}}},
    };
    return all;
}
const Shape &shapeNamed(const std::string &name) {
    for (const auto &shape : shapes())
        if (name == shape.name)
            return shape;
    throw std::runtime_error("Unknown shape: " + name);
}
// What the faces of every body carry besides their geometry. `paint` gives each face a colour
// (84 charged bytes a face). `textures` maps the faces of the definitions named by textureDefs (one
// front and one back mapping, 264 charged bytes a materialized face and 528 encoded bytes a
// unique face), so that each placement of a textured definition is textured too.
struct Variant {
    bool paint{};
    // (definition index into Shape::defs, faces textured in each of its bodies; 0 = all)
    std::vector<std::pair<size_t, size_t>> textured;
};
void decorate(Body &body, const Variant &variant, size_t definition) {
    if (variant.paint)
        for (const auto &[face, record] : body.surface.faces) {
            (void)record;
            body.faceColors[face] = {0.5f, 0.5f, 0.5f};
        }
    for (const auto &[index, limit] : variant.textured) {
        if (index != definition)
            continue;
        size_t count = 0;
        for (const auto &[face, record] : body.surface.faces) {
            (void)record;
            if (limit && count++ >= limit)
                break;
            TextureMapping mapping;
            mapping.origin = {0, 0, 0};
            body.faceTextureMappings[face] = TextureMappingSides{mapping, mapping};
        }
    }
}
// Builds the shape. Bodies are restored in one step, then grouped into components, placed, and the
// document is reloaded so that its history starts empty, as a file opens (the R110 method).
// `singlePlacement` places each definition once, which is the base of a catalog import.
Document build(const Shape &shape, const Variant &variant = {}, bool singlePlacement = false) {
    std::map<Id, BodyPtr> bodies;
    Id next = 1;
    std::vector<std::vector<Id>> groups;
    for (const auto &def : shape.defs) {
        std::vector<Id> members;
        const size_t count = def.kind == Def::Kind::Strip ? def.count : 1;
        for (size_t c = 0; c < count; ++c) {
            const Id id = next++;
            const Vec3 offset{double(c) * 1000.0, 0, 0};
            auto body = def.kind == Def::Kind::Strip ? strip(id, def.strips, offset)
                                                     : sphere(id, offset, def.sides, def.rings);
            decorate(*body, variant, size_t(&def - shape.defs.data()));
            bodies.emplace(id, body);
            members.push_back(id);
        }
        groups.push_back(members);
    }
    Document doc;
    doc.restore("000000000000000000000000000008a0", next, std::move(bodies));
    std::vector<Id> definitions;
    for (size_t d = 0; d < shape.defs.size(); ++d) {
        const std::set<Id> parts(groups[d].begin(), groups[d].end());
        const auto group = createGroup(doc, parts, "Part " + std::to_string(d));
        const auto component = createComponent(doc, group, "Part " + std::to_string(d));
        definitions.push_back(component.definition);
    }
    for (size_t d = 0; d < shape.defs.size(); ++d)
        for (int p = 1; !singlePlacement && p < shape.defs[d].placements; ++p)
            (void)placeComponent(doc, definitions[d],
                                 Transform::translation({0, 1000.0 * double(p + 1), 0}));
    for (size_t d = 0; d < definitions.size(); ++d)
        std::printf("definition %zu: %s, %d placements\n", size_t(definitions[d]),
                    ("Part " + std::to_string(d)).c_str(),
                    singlePlacement ? 1 : shape.defs[d].placements);
    // Reload, so the history of building does not charge the snapshot or the next edit.
    return decodeContainer(encodeContainer(doc));
}
// Four assets of 16 MiB, the 64 MiB total of assetTotalLimit, filled with a fixed pattern.
void addAssets(Document &doc) {
    for (int a = 0; a < 4; ++a) {
        std::vector<std::uint8_t> bytes(AssetPayload::limit);
        for (size_t i = 0; i < bytes.size(); ++i)
            bytes[i] = std::uint8_t((i * 131 + size_t(a) * 17) & 0xff);
        (void)createAsset(doc, "Benchmark asset " + std::to_string(a), "application/octet-stream",
                          std::make_shared<const AssetPayload>(bytes));
    }
}
void report(const Shape &shape, const Document &doc, double builtSeconds) {
    const auto totals = doc.materializedTotals();
    const auto stored = componentGeometry(doc.definitions()).stored;
    std::printf("shape: %s (%s)\n", shape.name, shape.what);
    std::printf("definitions: %zu; materialized bodies: %zu\n", doc.definitions().size(),
                size_t(totals.records));
    std::printf("materialized: %zu vertices, %zu faces, %zu edges, %zu wires\n",
                size_t(totals.vertices), size_t(totals.faces), size_t(totals.edges),
                size_t(totals.wires));
    std::printf("definition members (unique): %zu vertices, %zu faces, %zu edges, %zu wires\n",
                size_t(stored.vertices), size_t(stored.faces), size_t(stored.edges),
                size_t(stored.wires));
    const auto charge = doc.readSnapshotBytes();
    std::printf("readSnapshotBytes without assets: %zu (%.1f MiB)\n",
                charge, double(charge) / 1048576.0);
    const auto chunk = encodeDocument(doc, AssetStorage::External).size();
    std::printf("document chunk: %.1f MiB (bound 96 MiB)\n", double(chunk) / 1048576.0);
    std::printf("built in %.1f s; peak RSS %.0f MiB\n", builtSeconds, peakMiB());
}
} // namespace
// A variant from its command-line form: "plain", or a comma list of "paint" and "tex=<def>:<faces>"
// (faces 0 = every face of that definition's bodies).
Variant variantNamed(const std::string &text) {
    Variant variant;
    if (text == "plain" || text.empty())
        return variant;
    size_t start = 0;
    while (start <= text.size()) {
        const auto comma = text.find(',', start);
        const auto token = text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        if (token == "paint")
            variant.paint = true;
        else if (token.rfind("tex=", 0) == 0) {
            const auto colon = token.find(':');
            check(colon != std::string::npos, "tex=<def>:<faces>");
            variant.textured.emplace_back(std::stoul(token.substr(4, colon - 4)),
                                          std::stoul(token.substr(colon + 1)));
        } else
            throw std::runtime_error("Unknown variant token: " + token);
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
    return variant;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        const std::string mode = argc > 1 ? argv[1] : "";
        // export <file>: the GLB export of a saved model, with the resident set before and after it.
        if (mode == "export" && argc == 3) {
            QFile input(argv[2]);
            check(input.open(QIODevice::ReadOnly), "Open the container");
            const auto loadStarted = Clock::now();
            Document doc = decodeContainer(input.readAll());
            std::printf("loaded in %.1f s; resident %.0f MiB; peak %.0f MiB\n",
                        seconds(loadStarted, Clock::now()), currentMiB(), peakMiB());
            const auto snapshot = RenderSnapshot::capture(doc);
            std::printf("snapshot taken; resident %.0f MiB\n", currentMiB());
            const auto exportStarted = Clock::now();
            const auto scene = exportGlb(snapshot);
            std::printf("exported %zu bytes of GLB and a %.1f MiB manifest in %.1f s; resident %.0f MiB; "
                        "peak %.0f MiB\n",
                        size_t(scene.glb.size()), double(QJsonDocument(scene.manifest).toJson().size()) / 1048576.0,
                        seconds(exportStarted, Clock::now()), currentMiB(), peakMiB());
            return 0;
        }
        if (mode == "shapes") {
            for (const auto &shape : shapes())
                std::printf("%s: %s\n", shape.name, shape.what);
            return 0;
        }
        // charge <shape> [variant]; write <shape> <file> [variant]; base <shape> <file>.
        if ((mode == "charge" && (argc == 3 || argc == 4)) ||
            (mode == "write" && (argc == 4 || argc == 5)) || (mode == "base" && argc == 4)) {
            const auto &shape = shapeNamed(argv[2]);
            const bool writes = mode != "charge";
            const std::string variantText = mode == "write" && argc == 5   ? argv[4]
                                            : mode == "charge" && argc == 4 ? argv[3]
                                                                            : "plain";
            const auto started = Clock::now();
            Document doc = build(shape, variantNamed(variantText), mode == "base");
            const auto built = seconds(started, Clock::now());
            const auto geometry = doc.readSnapshotBytes();
            std::printf("shape: %s; variant: %s\n", shape.name, variantText.c_str());
            report(shape, doc, built);
            std::printf("readSnapshotBytes, geometry only: %zu (%.1f MiB)\n", geometry,
                        double(geometry) / 1048576.0);
            addAssets(doc);
            const auto charge = doc.readSnapshotBytes();
            std::printf("readSnapshotBytes with 64 MiB of assets: %zu (%.1f MiB; budget %zu MiB; "
                        "%s)\n",
                        charge, double(charge) / 1048576.0, Document::readSnapshotByteLimit / 1048576,
                        charge <= Document::readSnapshotByteLimit ? "within" : "OVER");
            std::printf("peak RSS: %.0f MiB\n", peakMiB());
            if (writes) {
                const auto container = encodeContainer(doc);
                QFile out(argv[3]);
                check(out.open(QIODevice::WriteOnly) && out.write(container) == container.size(),
                      "Write the container");
                std::printf("container written: %.1f MiB\n", double(container.size()) / 1048576.0);
            }
            return 0;
        }
        std::cerr << "usage: snapshot_glb_limits_benchmark shapes | charge <shape> [variant] | "
                     "write <shape> <file> [variant] | base <shape> <file>\n";
        return 2;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
