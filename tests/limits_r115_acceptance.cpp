// R115 at-limit acceptance (manual). Not a CTest test: it is built with EXCLUDE_FROM_ALL and run on the
// GPU PC, one case per process, under the caps in docs/verification/R115-raised-limits.md. Models are
// synthetic. Every step prints one JSON line (case, step, seconds, peak RSS, outcome, detail) and the
// process exits 0 only when every step in the case passed.
//
//   limits_r115_acceptance <case> <workdir> [--smoke]
//
// Cases: unique, materialized, one-body, chunk, textures, export-glb, export-obj, export-stl,
// export-dxf, import-obj, import-stl, import-gltf. --smoke divides every size by 100 (or uses a small
// fixed one), so that the code paths run on the development laptop. At-limit sizes never run there.
#include "core/components.hpp"
#include "core/document_limits.hpp"
#include "core/face_textures.hpp"
#include "core/groups.hpp"
#include "core/materials.hpp"
#include "core/assets.hpp"
#include "core/model.hpp"
#include "core/texture_mapping.hpp"
#include "geometry/topology.hpp"
#include "integrations/glb_export.hpp"
#include "integrations/render_snapshot.hpp"
#include "io/assets.hpp"
#include "io/document_io.hpp"
#include "io/dxf_export.hpp"
#include "io/dxf_import.hpp"
#include "io/gltf_import.hpp"
#include "io/obj_export.hpp"
#include "io/obj_import.hpp"
#include "io/stl_export.hpp"
#include "io/stl_import.hpp"
#include <QBuffer>
#include <QtGui/QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QtEndian>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <numbers>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using namespace sketchy;

namespace {

using Clock = std::chrono::steady_clock;

bool smoke = false;
int failures = 0;
std::string caseName;

// Peak and current resident memory of this process, in MiB, from /proc/self/status.
double statusMiB(const char *key) {
    std::ifstream in("/proc/self/status");
    std::string line;
    while (std::getline(in, line))
        if (line.rfind(key, 0) == 0)
            return std::stoll(line.substr(std::strlen(key))) / 1024.0;
    return -1;
}

std::string jsonEscape(const std::string &text) {
    std::string out;
    for (const char c : text) {
        if (c == '"' || c == '\\')
            out += '\\';
        out += (c == '\n' || c == '\r') ? ' ' : c;
    }
    return out;
}

std::string toText(const std::string &value) { return value; }
std::string toText(const char *value) { return value; }
std::string toText(const QString &value) { return value.toStdString(); }

void report(const std::string &name, double seconds, bool ok, const std::string &detail) {
    std::cout << "{\"case\":\"" << caseName << "\",\"step\":\"" << jsonEscape(name)
              << "\",\"seconds\":" << seconds << ",\"peakRssMiB\":" << statusMiB("VmHWM:")
              << ",\"rssMiB\":" << statusMiB("VmRSS:") << ",\"ok\":" << (ok ? "true" : "false")
              << ",\"detail\":\"" << jsonEscape(detail) << "\"}" << std::endl;
    if (!ok)
        ++failures;
}

// Runs one step. A thrown error is a failed step, recorded with its message (a product refusal is
// an outcome too: `expectRefusal` marks a step whose refusal is the expected result).
template <class F> bool step(const std::string &name, F operation, std::string *detail = nullptr) {
    const auto started = Clock::now();
    std::string text;
    bool ok = true;
    try {
        text = toText(operation());
    } catch (const std::exception &error) {
        ok = false;
        text = error.what();
    }
    const double seconds = std::chrono::duration<double>(Clock::now() - started).count();
    report(name, seconds, ok, text);
    if (detail)
        *detail = text;
    return ok;
}

void require(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}

std::string number(qsizetype value) { return std::to_string(value); }

// A closed, triangulated bipyramid: 2 * sides faces, sides + 2 vertices, 3 * sides edges.
std::shared_ptr<Body> bipyramid(Id id, Vec3 centre, int sides, double radius = 20, double height = 10) {
    auto body = std::make_shared<Body>();
    body->id = id;
    body->name = "Bipyramid " + std::to_string(id);
    auto &surface = body->surface;
    auto add = [&](Vec3 point) {
        const Id vertex = surface.nextId++;
        surface.vertices.emplace(vertex, centre + point);
        return vertex;
    };
    const Id apexTop = add({0, 0, height});
    const Id apexBottom = add({0, 0, -height});
    std::vector<Id> ringIds(static_cast<std::size_t>(sides));
    for (int j = 0; j < sides; ++j) {
        const double phi = 2 * std::numbers::pi * j / sides;
        ringIds[size_t(j)] = add({radius * std::cos(phi), radius * std::sin(phi), 0});
    }
    for (int j = 0; j < sides; ++j) {
        const size_t next = size_t((j + 1) % sides);
        surface.addFaceIds({{apexTop, ringIds[size_t(j)], ringIds[next]}});
        surface.addFaceIds({{apexBottom, ringIds[next], ringIds[size_t(j)]}});
    }
    body->topology = Topology::rebuild(body->surface, body->topology);
    return body;
}

// A flat triangulated grid in the XY plane, n * n cells: 2 n * n faces and about 1.5 edges per face. DXF
// export takes only planar geometry.
std::shared_ptr<Body> flatGrid(Id id, Vec3 origin, int n) {
    auto body = std::make_shared<Body>();
    body->id = id;
    body->name = "Flat grid " + std::to_string(id);
    auto &surface = body->surface;
    const int side = n + 1;
    std::vector<Id> vertex(size_t(side) * size_t(side));
    for (int j = 0; j < side; ++j)
        for (int i = 0; i < side; ++i) {
            const Id v = surface.nextId++;
            surface.vertices.emplace(v, origin + Vec3{0.001 * i, 0.001 * j, 0});
            vertex[size_t(j) * size_t(side) + size_t(i)] = v;
        }
    auto at = [&](int i, int j) { return vertex[size_t(j) * size_t(side) + size_t(i)]; };
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            surface.addFaceIds({{at(i, j), at(i + 1, j), at(i + 1, j + 1)}});
            surface.addFaceIds({{at(i, j), at(i + 1, j + 1), at(i, j + 1)}});
        }
    body->topology = Topology::rebuild(body->surface, body->topology);
    return body;
}


// A document of the given bodies, restored as one file would be (no history).
Document documentOf(const std::map<Id, BodyPtr> &bodies) {
    Document doc;
    const Id next = bodies.empty() ? 1 : bodies.rbegin()->first + 1;
    doc.restore("00000000000000000000000000000115", next, bodies);
    return doc;
}

std::size_t facesOf(const Document &doc) {
    std::size_t faces = 0;
    for (const auto &[id, body] : doc.bodies())
        faces += body->surface.faces.size();
    return faces;
}

// Unique family (R114): distinct 200-face bipyramids, 2,400,000 faces in 12,000 bodies.
std::map<Id, BodyPtr> uniqueBodies() {
    const int bodies = smoke ? 120 : 12000, sides = 100;
    std::map<Id, BodyPtr> records;
    // Near the origin: binary STL refuses coordinates whose float precision is over one micrometre (about 8 m).
    for (int i = 0; i < bodies; ++i) {
        const Vec3 centre{0.005 * (i % 110), 0.005 * (i / 110), 0};
        records.emplace(Id(i + 1), bipyramid(Id(i + 1), centre, sides, 0.004, 0.002));
    }
    return records;
}

// Materialized family: a definition of 800,000 faces, placed six times: 4,800,000 placed faces.
Document materializedDocument(int &definitionOut, std::vector<Id> &placementRoots) {
    const int sides = smoke ? 4000 : 400000;
    Document doc = documentOf({{1, bipyramid(1, {0, 0, 0}, sides, 1, 0.5)}});
    const Id group = createGroup(doc, {1}, "Definition");
    const auto component = createComponent(doc, group, "Big");
    definitionOut = int(component.definition);
    placementRoots.push_back(component.instance);
    for (int i = 1; i < 6; ++i) {
        const auto placed = placeComponent(doc, component.definition,
                                           Transform::translation({2.5 * i, 0, 0}));
        placementRoots.push_back(placed.instance);
    }
    return doc;
}

// Planar family for the DXF export: one flat grid of 798,848 faces (400,689 vertices) placed six times. The
// unique geometry stays inside the unique limits (R115 review: six separate grids have 2,404,134 vertices, over
// 2,400,000), and the placed model has 4,793,088 faces, within the materialized limits.
Document flatGridDocument(int &definitionOut, std::vector<Id> &placementRoots) {
    const int n = smoke ? 20 : 632;
    Document doc = documentOf({{1, flatGrid(1, {0, 0, 0}, n)}});
    const Id group = createGroup(doc, {1}, "Definition");
    const auto component = createComponent(doc, group, "Grid");
    definitionOut = int(component.definition);
    placementRoots.push_back(component.instance);
    for (int i = 1; i < 6; ++i) {
        const auto placed = placeComponent(doc, component.definition,
                                           Transform::translation({1.0 * i, 0, 0}));
        placementRoots.push_back(placed.instance);
    }
    return doc;
}

// Model JSON size of the chunk a container writes (the `model` chunk of the manifest).
qsizetype modelChunkBytes(const Document &doc) {
    const auto container = encodeContainer(doc);
    const auto manifestLength = qFromLittleEndian<quint32>(container.constData() + 12);
    const auto manifest = QJsonDocument::fromJson(container.mid(16, manifestLength)).object();
    for (const auto value : manifest["chunks"].toArray())
        if (value.toObject()["kind"].toString() == "document")
            return value.toObject()["bytes"].toString().toLongLong();
    return -1;
}

// ---- Cases -------------------------------------------------------------------------------------

void unique(const QString &dir) {
    Document doc;
    std::size_t expectedFaces = 0;
    step("build 12,000 distinct 200-face bodies", [&] {
        doc = documentOf(uniqueBodies());
        const auto totals = doc.materializedTotals();
        require(smoke || (totals.faces == 2400000 && doc.bodies().size() == 12000),
                "fixture is 2,400,000 faces in 12,000 bodies, got " + number(qsizetype(totals.faces)));
        expectedFaces = facesOf(doc);
        return "faces " + number(qsizetype(expectedFaces)) + ", bodies " + number(qsizetype(doc.bodies().size()));
    });
    const QString file = dir + "/unique.sketchyup";
    step("save", [&] {
        saveDocument(doc, file);
        return "file bytes " + number(QFileInfo(file).size());
    });
    doc = Document();
    Document reopened;
    step("open", [&] {
        reopened = loadDocument(file);
        require(smoke || facesOf(reopened) == expectedFaces, "reopened faces differ");
        return "faces " + number(qsizetype(facesOf(reopened)));
    });
    const Id body = 1;
    step("move one body", [&] {
        reopened.move(body, {5, 0, 0});
        return "history bytes " + number(qsizetype(reopened.historyBytes()));
    });
    step("undo", [&] {
        reopened.undo();
        require(reopened.canRedo(), "undo left nothing to redo");
        return "";
    });
    step("redo", [&] {
        reopened.redo();
        return "";
    });
}

void materialized(const QString &dir) {
    int definition = 0;
    std::vector<Id> placements;
    Document doc;
    step("build a 800,000-face definition placed six times", [&] {
        doc = materializedDocument(definition, placements);
        const auto totals = doc.materializedTotals();
        require(smoke || totals.faces == 4800000, "placed faces are " + number(qsizetype(totals.faces)));
        return "placed faces " + number(qsizetype(totals.faces)) + ", placements " +
               number(qsizetype(doc.instances().size()));
    });
    const QString file = dir + "/materialized.sketchyup";
    step("save", [&] {
        saveDocument(doc, file);
        return "file bytes " + number(QFileInfo(file).size());
    });
    Document reopened;
    step("open", [&] {
        reopened = loadDocument(file);
        return "placed faces " + number(qsizetype(reopened.materializedTotals().faces));
    });
    const Id root = placements.at(2);
    step("move one placement", [&] {
        reopened.move(root, {2, 0, 0});
        return "";
    });
    step("undo the placement move", [&] {
        reopened.undo();
        return "";
    });
    step("redo the placement move", [&] {
        reopened.redo();
        return "";
    });
    // D-104: a definition edit charges the records of every placement it regenerates. Its history
    // charge and the RSS growth are recorded side by side.
    const auto historyBefore = reopened.historyBytes();
    const double rssBefore = statusMiB("VmRSS:");
    std::string charge;
    step("edit the definition (move one member)", [&] {
        const auto &record = *reopened.definitions().at(Id(definition));
        Id member = 0;
        for (const auto &[id, body] : record.members)
            if (id != record.root) {
                member = id;
                break;
            }
        require(member != 0, "definition has no member");
        editComponentDefinition(reopened, Id(definition), [&](Document &draft) {
            draft.move(member, {1, 0, 0});
            return ChangeReport{};
        });
        const auto added = reopened.historyBytes() - historyBefore;
        charge = "history charge " + number(qsizetype(added)) + " bytes, RSS growth " +
                 std::to_string(statusMiB("VmRSS:") - rssBefore) + " MiB";
        return charge;
    });
    step("undo the definition edit", [&] {
        reopened.undo();
        return "";
    });
    step("redo the definition edit", [&] {
        reopened.redo();
        return "";
    });
}

void oneBody(const QString &dir) {
    const int sides = smoke ? 4000 : 400000;
    Document doc = documentOf({{1, bipyramid(1, {0, 0, 0}, sides, 1, 0.5)}});
    step("fixture: one body of 800,000 faces", [&] {
        require(smoke || facesOf(doc) == 800000, "faces " + number(qsizetype(facesOf(doc))));
        return "faces " + number(qsizetype(facesOf(doc)));
    });
    step("move the body", [&] {
        doc.move(1, {5, 0, 0});
        return "history bytes " + number(qsizetype(doc.historyBytes()));
    });
    step("undo", [&] {
        doc.undo();
        return "";
    });
    step("redo", [&] {
        doc.redo();
        return "";
    });
    (void)dir;
}

// D-099: a model chunk near 768 MiB with imported UVs. Every face carries a front and a back mapping.
void chunk(const QString &dir) {
    const int bodies = smoke ? 5 : 595, sides = 1000;
    std::map<Id, BodyPtr> records;
    std::mt19937_64 random(115);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    auto precise = [&] { return unit(random) * 1e-3 + 1.0 / 3.0 * unit(random); };
    for (int i = 0; i < bodies; ++i) {
        auto body = bipyramid(Id(i + 1), {0.5 * (i % 50), 0.5 * (i / 50), 0}, sides, 0.2, 0.1);
        for (const auto &[face, record] : body->surface.faces) {
            // Full-precision values for each face, both sides, as imported UVs carry them.
            TextureMappingSides mapping;
            mapping.front = TextureMapping{{precise(), precise(), precise()}, {1 + precise() * 1e-6, precise() * 1e-6, precise() * 1e-6}, {precise() * 1e-6, 1 + precise() * 1e-6, precise() * 1e-6}, {precise(), precise()}};
            mapping.back = TextureMapping{{precise(), precise(), precise()}, {1 + precise() * 1e-6, precise() * 1e-6, precise() * 1e-6}, {precise() * 1e-6, 1 + precise() * 1e-6, precise() * 1e-6}, {precise(), precise()}};
            body->faceTextureMappings[face] = mapping;
        }
        records.emplace(Id(i + 1), std::move(body));
    }
    Document doc;
    step("build textured bodies", [&] {
        doc = documentOf(records);
        return "faces " + number(qsizetype(facesOf(doc))) + ", model chunk bytes " +
               number(modelChunkBytes(doc));
    });
    const QString file = dir + "/chunk.sketchyup";
    step("save (model chunk near 768 MiB)", [&] {
        saveDocument(doc, file);
        return "file bytes " + number(QFileInfo(file).size()) + ", model chunk bytes " +
               number(modelChunkBytes(doc));
    });
    Document reopened;
    step("open", [&] {
        reopened = loadDocument(file);
        require(smoke || facesOf(reopened) == facesOf(doc), "reopened faces differ");
        return "faces " + number(qsizetype(facesOf(reopened)));
    });
}

// R114 textures family: 32 distinct 4096-square PNGs (about 14.6 MB each) and one 64 MiB asset.
void textures(const QString &dir) {
    const int images = smoke ? 2 : 32;
    Document doc = documentOf({});
    step("build the textured quads", [&] {
        std::size_t stored = 0;
        for (int i = 0; i < images; ++i) {
            QImage image(smoke ? 256 : 4096, smoke ? 256 : 4096, QImage::Format_RGB888);
            std::uint32_t seed = 0x9e3779b9u * std::uint32_t(i + 1);
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x) {
                    seed = seed * 1664525u + 1013904223u;
                    image.setPixelColor(x, y, QColor((seed >> 30) * 85, (seed >> 24 & 3) * 85, (seed >> 16 & 3) * 85));
                }
            QByteArray png;
            QBuffer buffer(&png);
            buffer.open(QIODevice::WriteOnly);
            require(image.save(&buffer, "PNG"), "encode texture " + std::to_string(i));
            stored += size_t(png.size());
            const auto asset = createAsset(doc, "Texture " + std::to_string(i), "image/png", assetPayload(png));
            const auto body = createMaterial(doc, "Swatch " + std::to_string(i), {1, 1, 1}, 1, asset);
            (void)body;
        }
        auto big = QByteArray(smoke ? 1024 * 1024 : 64 * 1024 * 1024, 'x');
        createAsset(doc, "Opaque 64 MiB", "application/octet-stream", assetPayload(big));
        return "stored asset bytes " + number(qsizetype(stored + size_t(big.size())));
    });
    const QString file = dir + "/textures.sketchyup";
    step("save", [&] {
        saveDocument(doc, file);
        return "file bytes " + number(QFileInfo(file).size()) + " (expected viewport placeholders: " +
               std::to_string(images + 1 - 2) + " of " + std::to_string(images + 1) +
               "; not measured in the viewport here)";
    });
    step("open", [&] {
        Document reopened = loadDocument(file);
        require(reopened.assets().size() == size_t(images + 1), "asset count differs");
        return "assets " + number(qsizetype(reopened.assets().size()));
    });
}

// Exports and imports at the limits. The model is the materialized family (4,800,000 placed faces) for
// the exports, and the unique family (2,400,000 faces) for the imports.
void exports(const std::string &format, const QString &dir) {
    int definition = 0;
    std::vector<Id> placements;
    Document doc;
    if (format == "dxf") {
        step("build one flat grid (798,848 faces) placed six times (4,793,088 placed faces)", [&] {
            doc = flatGridDocument(definition, placements);
            return "placed faces " + number(qsizetype(doc.materializedTotals().faces)) + ", placed edges " +
                   number(qsizetype(doc.materializedTotals().edges));
        });
    } else {
        step("build the materialized model", [&] {
            doc = materializedDocument(definition, placements);
            return "placed faces " + number(qsizetype(doc.materializedTotals().faces));
        });
    }
    if (format == "glb") {
        GlbExport scene;
        step("export GLB (4,800,000 visible faces)", [&] {
            scene = exportGlb(RenderSnapshot::capture(doc));
            return "glb bytes " + number(scene.glb.size()) + ", manifest bytes " +
                   number(QJsonDocument(scene.manifest).toJson(QJsonDocument::Compact).size());
        });
        step("write GLB export", [&] {
            writeGlbExport(scene, dir + "/glb");
            return dir + "/glb/scene.glb";
        });
    } else if (format == "obj") {
        step("export OBJ", [&] {
            const auto exported = exportObj(doc, ObjImportOptions{1, ObjUpAxis::Z});
            require(!exported.obj.isEmpty(), "empty OBJ");
            QFile file(dir + "/materialized.obj");
            require(file.open(QIODevice::WriteOnly) && file.write(exported.obj) == exported.obj.size(),
                    "write OBJ");
            return "obj bytes " + number(exported.obj.size());
        });
    } else if (format == "stl") {
        step("export binary STL", [&] {
            const auto exported = exportStl(doc, StlCoordinateOptions{1, StlUpAxis::Z}, StlEncoding::Binary);
            writeStlExport(exported, dir + "/materialized.stl");
            return "stl bytes " + number(exported.bytes.size());
        });
    } else if (format == "dxf") {
        step("export DXF", [&] {
            const auto exported = exportDxf(doc, 1.0);
            writeDxfExport(exported, dir + "/materialized.dxf");
            return "dxf bytes " + number(QFileInfo(dir + "/materialized.dxf").size());
        });
    }
}

// Imports at the unique limit: write the 2,400,000-face model in each format, then read it back.
void imports(const std::string &format, const QString &dir) {
    Document doc = documentOf(uniqueBodies());
    if (format == "obj") {
        step("write OBJ of the unique model", [&] {
            const auto exported = exportObj(doc, ObjImportOptions{1, ObjUpAxis::Z});
            QFile file(dir + "/unique.obj");
            require(file.open(QIODevice::WriteOnly) && file.write(exported.obj) == exported.obj.size(),
                    "write OBJ");
            return "obj bytes " + number(exported.obj.size());
        });
        step("import OBJ at the unique limit", [&] {
            const auto imported = loadObj(dir + "/unique.obj", ObjImportOptions{1, ObjUpAxis::Z});
            require(smoke || facesOf(imported.document) == 2400000,
                    "faces " + number(qsizetype(facesOf(imported.document))));
            return "faces " + number(qsizetype(facesOf(imported.document))) + ", bodies " +
                   number(qsizetype(imported.document.bodies().size()));
        });
    } else if (format == "stl") {
        step("write binary STL of the unique model", [&] {
            const auto exported = exportStl(doc, StlCoordinateOptions{1, StlUpAxis::Z}, StlEncoding::Binary);
            writeStlExport(exported, dir + "/unique.stl");
            return "stl bytes " + number(exported.bytes.size());
        });
        step("import binary STL at the unique limit", [&] {
            const auto imported = loadStl(dir + "/unique.stl", StlCoordinateOptions{1, StlUpAxis::Z}, StlRepairOptions{});
            require(smoke || facesOf(imported.document) == 2400000,
                    "facets " + number(qsizetype(facesOf(imported.document))));
            return "facets " + number(qsizetype(facesOf(imported.document))) + ", bodies " +
                   number(qsizetype(imported.document.bodies().size()));
        });
    } else if (format == "gltf") {
        GlbExport scene;
        step("write GLB of the unique model", [&] {
            scene = exportGlb(RenderSnapshot::capture(doc));
            writeGlbExport(scene, dir + "/unique-glb");
            return "glb bytes " + number(scene.glb.size());
        });
        step("import glTF/GLB at the unique limit (D-103: undo budget)", [&] {
            const auto imported = loadGltf(dir + "/unique-glb/scene.glb");
            require(smoke || facesOf(imported.document) == 2400000,
                    "faces " + number(qsizetype(facesOf(imported.document))));
            return "faces " + number(qsizetype(facesOf(imported.document))) + ", history bytes " +
                   number(qsizetype(imported.document.historyBytes()));
        });
    }
}

} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc < 3) {
        std::cerr << "usage: limits_r115_acceptance <case> <workdir> [--smoke]\n";
        return 2;
    }
    caseName = argv[1];
    const QString dir = QString::fromLocal8Bit(argv[2]);
    smoke = argc > 3 && std::string(argv[3]) == "--smoke";
    QDir().mkpath(dir);
    try {
        if (caseName == "unique") unique(dir);
        else if (caseName == "materialized") materialized(dir);
        else if (caseName == "one-body") oneBody(dir);
        else if (caseName == "chunk") chunk(dir);
        else if (caseName == "textures") textures(dir);
        else if (caseName == "export-glb") exports("glb", dir);
        else if (caseName == "export-obj") exports("obj", dir);
        else if (caseName == "export-stl") exports("stl", dir);
        else if (caseName == "export-dxf") exports("dxf", dir);
        else if (caseName == "import-obj") imports("obj", dir);
        else if (caseName == "import-stl") imports("stl", dir);
        else if (caseName == "import-gltf") imports("gltf", dir);
        else {
            std::cerr << "unknown case " << caseName << "\n";
            return 2;
        }
    } catch (const std::exception &error) {
        report("case aborted", 0, false, error.what());
    }
    return failures == 0 ? 0 : 1;
}
