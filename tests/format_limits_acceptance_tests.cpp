// R113 (D-102, 1.9.0): the file formats' count limits at the published limits. A model at the
// materialized limits (600,000 faces, 900,000 edges, placed through one shared definition) exports
// to OBJ, to STL in both encodings, to GLB, and, as a planar model near the edge limit, to DXF. A
// file at the unique limit (300,000 faces) imports from OBJ, STL and glTF, and a DXF polyline of
// 250,000 vertices imports from DXF; every face is kept, the meshes are split into bodies of at most
// 100,000 faces or vertices, and the documents save and reopen. One past a limit is refused by name. The models are synthetic. This is a slow core
// test (ADR 0170): it needs about a minute of I/O and under 4 GiB.
#include "core/components.hpp"
#include "core/document_limits.hpp"
#include "core/groups.hpp"
#include "core/materials.hpp"
#include "core/transform.hpp"
#include "geometry/topology.hpp"
#include "integrations/glb_export.hpp"
#include "integrations/render_snapshot.hpp"
#include "io/document_io.hpp"
#include "io/dxf_export.hpp"
#include "io/dxf_import.hpp"
#include "io/gltf_import.hpp"
#include "io/obj_export.hpp"
#include "io/obj_import.hpp"
#include "io/stl_export.hpp"
#include "io/stl_import.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtEndian>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <numbers>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
using namespace sketchy;
namespace {
using Clock = std::chrono::steady_clock;
void check(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}
double seconds(Clock::time_point from) {
    return std::chrono::duration<double>(Clock::now() - from).count();
}
// The message of the operation's exception, or empty when it does not throw.
std::string refusal(const std::function<void()> &operation) {
    try {
        operation();
    } catch (const std::exception &error) {
        return error.what();
    }
    return {};
}
// A closed triangulated sphere: 2 * sides * rings triangles, 2 + sides * rings vertices, and
// 3 edges for every 2 triangles (R112's sphere, with a radius and an id for each body).
BodyPtr sphere(Id id, Vec3 centre, double radius, int sides, int rings) {
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
    std::vector<std::vector<Id>> ring(static_cast<size_t>(rings),
                                      std::vector<Id>(static_cast<size_t>(sides)));
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
    return body;
}
// A flat grid of cols x rows cells in the plane z = 0: 2 * cols * rows triangles and
// (cols + 1) * (rows + 1) vertices, with the cells `cell` metres wide.
BodyPtr grid(Id id, int cols, int rows, double cell) {
    auto body = std::make_shared<Body>();
    body->id = id;
    body->name = "Grid " + std::to_string(id);
    auto &surface = body->surface;
    std::vector<Id> vertices;
    for (int j = 0; j <= rows; ++j)
        for (int i = 0; i <= cols; ++i) {
            const Id vertex = surface.nextId++;
            surface.vertices.emplace(vertex, Vec3{i * cell, j * cell, 0});
            vertices.push_back(vertex);
        }
    const auto at = [&](int i, int j) { return vertices[size_t(j * (cols + 1) + i)]; };
    for (int j = 0; j < rows; ++j)
        for (int i = 0; i < cols; ++i) {
            surface.addFaceIds({{at(i, j), at(i + 1, j), at(i + 1, j + 1)}});
            surface.addFaceIds({{at(i, j), at(i + 1, j + 1), at(i, j + 1)}});
        }
    body->topology = Topology::rebuild(body->surface, body->topology);
    return body;
}
// The faces and the edges of every body in the document (placements included).
struct Totals {
    std::size_t faces{}, edges{};
};
Totals totalsOf(const Document &doc) {
    Totals totals;
    for (const auto &[id, body] : doc.bodies()) {
        (void)id;
        totals.faces += body->surface.faces.size();
        totals.edges += body->topology.edges.size();
    }
    return totals;
}
// The model at the materialized limits: one definition of two closed spheres of 75,000 faces each
// (150,000 faces, 225,000 edges, unique), placed four times. The placements are 3 m apart, so the
// coordinates stay within a float's micrometre for the binary STL.
Document materializedModel(bool differentSides = false) {
    std::map<Id, BodyPtr> bodies;
    bodies.emplace(1, sphere(1, {0, 0, 0}, .5, 250, 150));
    bodies.emplace(2, sphere(2, {1.2, 0, 0}, .5, 250, 150));
    Document doc;
    doc.restore("00000000000000000000000000000c01", 3, std::move(bodies));
    if (differentSides) {
        const auto front = createMaterial(doc, "Front", {1, 0, 0}),
                   back = createMaterial(doc, "Back", {0, 0, 1});
        for (const Id body : {Id(1), Id(2)}) {
            assignMaterial(doc, body, {}, front, true, false);
            assignMaterial(doc, body, {}, back, false, true);
        }
    }
    const auto group = createGroup(doc, {1, 2}, "Pair");
    const auto component = createComponent(doc, group, "Pair");
    for (int k = 1; k < 4; ++k)
        (void)placeComponent(doc, component.definition,
                             Transform::translation({3.0 * k, 0, 0}));
    // Reload, so the export starts from a file as a user's file does.
    return decodeContainer(encodeContainer(doc));
}
// The planar model near the edge limit: 29 placements of a 101 x 101 cell grid, 591,658 faces and
// 893,345 edges. Planar, so the DXF export takes it.
Document planarModel() {
    Document doc;
    std::map<Id, BodyPtr> bodies;
    bodies.emplace(1, grid(1, 101, 101, 1.0));
    doc.restore("00000000000000000000000000000c02", 2, std::move(bodies));
    const auto group = createGroup(doc, {1}, "Sheet");
    const auto component = createComponent(doc, group, "Sheet");
    for (int k = 1; k < 29; ++k)
        (void)placeComponent(doc, component.definition,
                             Transform::translation({110.0 * k, 0, 0}));
    return decodeContainer(encodeContainer(doc));
}
// A grid of cols x rows cells as OBJ text in the XY plane (Z up), 2 * cols * rows faces.
void writeObj(const QString &path, int cols, int rows) {
    QByteArray text = "# synthetic grid\n";
    for (int j = 0; j <= rows; ++j)
        for (int i = 0; i <= cols; ++i)
            text += "v " + QByteArray::number(i) + " " + QByteArray::number(j) + " 0\n";
    const auto index = [&](int i, int j) { return QByteArray::number(j * (cols + 1) + i + 1); };
    for (int j = 0; j < rows; ++j)
        for (int i = 0; i < cols; ++i) {
            text += "f " + index(i, j) + " " + index(i + 1, j) + " " + index(i + 1, j + 1) + "\n";
            text += "f " + index(i, j) + " " + index(i + 1, j + 1) + " " + index(i, j + 1) + "\n";
        }
    QFile file(path);
    check(file.open(QIODevice::WriteOnly), "Write the OBJ grid");
    file.write(text);
}
// The triangles of a grid as binary STL, from the same grid as writeObj. `facets` keeps only the
// first facets of the grid (0 keeps them all), so a file can hold exactly one past a limit.
void writeStlBinary(const QString &path, int cols, int rows, std::uint32_t facets = 0) {
    const std::uint32_t count = std::uint32_t(2 * cols * rows);
    QByteArray bytes(84 + 50 * qsizetype(count), 0);
    std::memcpy(bytes.data() + 80, &count, 4);
    qsizetype at = 84;
    const auto put = [&](float x, float y, float z) {
        const float values[3] = {x, y, z};
        for (const float v : values) {
            const auto bits = qToLittleEndian<quint32>(std::bit_cast<quint32>(v));
            std::memcpy(bytes.data() + at, &bits, 4);
            at += 4;
        }
    };
    for (int j = 0; j < rows; ++j)
        for (int i = 0; i < cols; ++i)
            for (int t = 0; t < 2; ++t) {
                at += 12; // the normal is recomputed from the vertices
                const float x = float(i), y = float(j);
                if (t == 0) {
                    put(x, y, 0);
                    put(x + 1, y, 0);
                    put(x + 1, y + 1, 0);
                } else {
                    put(x, y, 0);
                    put(x + 1, y + 1, 0);
                    put(x, y + 1, 0);
                }
                at += 2;
            }
    if (facets) {
        bytes.resize(qsizetype(84 + 50 * std::uint64_t(facets)));
        std::memcpy(bytes.data() + 80, &facets, 4);
    }
    QFile file(path);
    check(file.open(QIODevice::WriteOnly), "Write the STL grid");
    file.write(bytes);
}
// A glTF 2.0 binary with one mesh per grid, each placed by `placements` nodes, 3 m apart along x.
QByteArray glb(const std::vector<std::pair<int, int>> &grids, int placements = 1) {
    QByteArray binary;
    QJsonArray accessors, views, nodes, meshes, scene;
    const auto align = [&] {
        while (binary.size() % 4)
            binary.append('\0');
    };
    for (std::size_t m = 0; m < grids.size(); ++m) {
        const auto [cols, rows] = grids[m];
        std::vector<float> positions;
        for (int j = 0; j <= rows; ++j)
            for (int i = 0; i <= cols; ++i) {
                positions.push_back(float(i));
                positions.push_back(float(j));
                positions.push_back(0);
            }
        std::vector<std::uint32_t> indices;
        const auto at = [&](int i, int j) { return std::uint32_t(j * (cols + 1) + i); };
        for (int j = 0; j < rows; ++j)
            for (int i = 0; i < cols; ++i) {
                indices.insert(indices.end(), {at(i, j), at(i + 1, j), at(i + 1, j + 1)});
                indices.insert(indices.end(), {at(i, j), at(i + 1, j + 1), at(i, j + 1)});
            }
        align();
        const auto positionOffset = binary.size();
        for (const float v : positions) {
            const auto bits = qToLittleEndian<quint32>(std::bit_cast<quint32>(v));
            binary.append(reinterpret_cast<const char *>(&bits), 4);
        }
        views.append(QJsonObject{{"buffer", 0},
                                 {"byteOffset", int(positionOffset)},
                                 {"byteLength", int(binary.size() - positionOffset)}});
        const int positionView = int(views.size()) - 1;
        align();
        const auto indexOffset = binary.size();
        for (const auto v : indices) {
            const auto bits = qToLittleEndian<quint32>(v);
            binary.append(reinterpret_cast<const char *>(&bits), 4);
        }
        views.append(QJsonObject{{"buffer", 0},
                                 {"byteOffset", int(indexOffset)},
                                 {"byteLength", int(binary.size() - indexOffset)}});
        const int indexView = int(views.size()) - 1;
        accessors.append(QJsonObject{{"bufferView", positionView},
                                     {"componentType", 5126},
                                     {"count", int(positions.size() / 3)},
                                     {"type", "VEC3"}});
        const int positionAccessor = int(accessors.size()) - 1;
        accessors.append(QJsonObject{{"bufferView", indexView},
                                     {"componentType", 5125},
                                     {"count", int(indices.size())},
                                     {"type", "SCALAR"}});
        const int indexAccessor = int(accessors.size()) - 1;
        meshes.append(QJsonObject{
            {"primitives", QJsonArray{QJsonObject{
                               {"attributes", QJsonObject{{"POSITION", positionAccessor}}},
                               {"indices", indexAccessor},
                               {"mode", 4}}}}});
        for (int p = 0; p < placements; ++p) {
            nodes.append(QJsonObject{{"mesh", int(m)},
                                     {"translation", QJsonArray{3.0 * double(int(m) * placements + p), 0, 0}}});
            scene.append(int(nodes.size()) - 1);
        }
    }
    align();
    QJsonObject json{{"asset", QJsonObject{{"version", "2.0"}}},
                     {"scene", 0},
                     {"scenes", QJsonArray{QJsonObject{{"nodes", scene}}}},
                     {"nodes", nodes},
                     {"meshes", meshes},
                     {"accessors", accessors},
                     {"bufferViews", views},
                     {"buffers", QJsonArray{QJsonObject{{"byteLength", int(binary.size())}}}}};
    QByteArray text = QJsonDocument(json).toJson(QJsonDocument::Compact);
    while (text.size() % 4)
        text.append(' ');
    QByteArray file;
    const auto word = [&](quint32 value) {
        const auto little = qToLittleEndian<quint32>(value);
        file.append(reinterpret_cast<const char *>(&little), 4);
    };
    word(0x46546C67); // "glTF"
    word(2);
    word(quint32(12 + 8 + text.size() + 8 + binary.size()));
    word(quint32(text.size()));
    word(0x4E4F534A); // "JSON"
    file += text;
    word(quint32(binary.size()));
    word(0x004E4942); // "BIN"
    file += binary;
    return file;
}
void writeFile(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(),
          "Write a synthetic file");
}
// The faces of an imported document.
std::size_t facesOf(const Document &doc) {
    std::size_t faces = 0;
    for (const auto &[id, body] : doc.bodies()) {
        (void)id;
        faces += body->surface.faces.size();
    }
    return faces;
}
// The bodies that hold faces, and the bodies that hold wires.
std::size_t geometryBodies(const Document &doc) {
    std::size_t bodies = 0;
    for (const auto &[id, body] : doc.bodies()) {
        (void)id;
        bodies += !body->surface.faces.empty();
    }
    return bodies;
}
std::size_t wireBodies(const Document &doc) {
    std::size_t bodies = 0;
    for (const auto &[id, body] : doc.bodies()) {
        (void)id;
        bodies += !body->surface.wires.empty();
    }
    return bodies;
}
void exportsAtMaterializedLimits() {
    const auto started = Clock::now();
    const auto model = materializedModel();
    const auto totals = totalsOf(model);
    std::cout << "materialized model: " << totals.faces << " faces, " << totals.edges
              << " edges\n";
    check(totals.faces == 600000, "The model holds the materialized face limit");
    check(totals.edges == 900000, "The model holds the materialized edge limit");
    const auto obj = exportObj(model, ObjImportOptions{1.0, ObjUpAxis::Z});
    check(obj.obj.count("\nf ") == qsizetype(totals.faces),
          "OBJ writes every face at the materialized limit");
    std::cout << "OBJ: " << obj.obj.size() << " bytes, " << obj.obj.count("\nf ") << " faces ("
              << seconds(started) << " s)\n";
    const auto binary =
        exportStl(model, StlCoordinateOptions{1.0, StlUpAxis::Z}, StlEncoding::Binary);
    check(binary.bytes.size() == qsizetype(84 + 50 * totals.faces),
          "Binary STL is 84 + 50 bytes per facet at the materialized limit");
    check(qFromLittleEndian<quint32>(binary.bytes.constData() + 80) == totals.faces,
          "Binary STL counts every facet");
    const auto ascii =
        exportStl(model, StlCoordinateOptions{1.0, StlUpAxis::Z}, StlEncoding::Ascii);
    check(ascii.bytes.count("facet normal") == qsizetype(totals.faces),
          "ASCII STL writes every facet at the materialized limit");
    std::cout << "STL: binary " << binary.bytes.size() << " bytes, ASCII " << ascii.bytes.size()
              << " bytes (" << seconds(started) << " s)\n";
    const auto scene = exportGlb(RenderSnapshot::capture(model));
    check(scene.manifest["visibleTriangles"].toInteger() == qint64(totals.faces),
          "GLB writes the materialized triangles");
    std::cout << "GLB: " << scene.glb.size() << " bytes (" << seconds(started) << " s)\n";
    // Different front and back appearances write each triangle twice: 1,200,000 here, over the
    // 1,000,000 that 1.8.0 refused, from a document within every published limit.
    const auto paired = exportGlb(RenderSnapshot::capture(materializedModel(true)));
    check(paired.manifest["visibleTriangles"].toInteger() == 2 * qint64(totals.faces),
          "GLB writes both sides of a model with different front and back appearances");
    std::cout << "GLB, both sides: " << paired.glb.size() << " bytes (" << seconds(started)
              << " s)\n";
    // A model over the materialized limit cannot be made: the document refuses it at the edit, so
    // the export's own check is not reachable from a valid model. The one-past refusals are tested
    // on the imports.
}
// The segments a DXF file writes: one per LINE, and one per step of each LWPOLYLINE (a closed one
// adds its closing step). The chains partition the edges, so the total is the model's edge count.
std::size_t dxfSegments(const QByteArray &bytes) {
    std::size_t segments = std::size_t(bytes.count("\nLINE\n"));
    qsizetype at = 0;
    while ((at = bytes.indexOf("\nLWPOLYLINE\n", at)) >= 0) {
        at += 12;
        const auto count = bytes.indexOf("\n90\n", at);
        const auto flag = bytes.indexOf("\n70\n", count < 0 ? at : count);
        check(count >= 0 && flag >= 0, "A DXF polyline states its vertex count and flags");
        const auto line = [&](qsizetype from) {
            return bytes.mid(from, bytes.indexOf('\n', from) - from);
        };
        const auto vertices = line(count + 4).toULongLong();
        const bool closed = line(flag + 4).toInt() & 1;
        segments += vertices - 1 + (closed ? 1 : 0);
    }
    return segments;
}
void dxfAtTheEdgeLimit() {
    const auto started = Clock::now();
    const auto model = planarModel();
    const auto totals = totalsOf(model);
    std::cout << "planar model: " << totals.faces << " faces, " << totals.edges << " edges\n";
    check(totals.edges > 800000 && totals.edges <= DocumentLimits::materialized.edges,
          "The planar model is near the materialized edge limit");
    const auto dxf = exportDxf(model, 1.0);
    const auto segments = dxfSegments(dxf.bytes);
    check(segments == totals.edges, "DXF writes every edge at the materialized limit");
    std::cout << "DXF: " << dxf.bytes.size() << " bytes, " << segments << " segments ("
              << seconds(started) << " s)\n";
}
void importsAtUniqueLimits(const QString &dir) {
    const auto started = Clock::now();
    // OBJ: 300,000 triangles, split into three bodies of 100,000 faces each.
    const auto objPath = dir + "/unique.obj";
    writeObj(objPath, 300, 500);
    auto obj = loadObj(objPath, ObjImportOptions{1.0, ObjUpAxis::Z});
    check(facesOf(obj.document) == 300000, "OBJ keeps every face at the unique limit");
    const auto objSplits = obj.report["bodySplits"].toArray();
    check(objSplits.size() == 1 && objSplits[0].toObject()["bodies"].toInt() == 3,
          "OBJ splits 300,000 faces into three bodies");
    const auto saved = dir + "/unique-obj.sketchy";
    saveDocument(obj.document, saved);
    const auto reopened = loadDocument(saved);
    check(facesOf(reopened) == 300000 && geometryBodies(reopened) == 3,
          "OBJ at the unique limit saves and reopens with every face");
    std::cout << "OBJ import: 300,000 faces in 3 bodies, saved and reopened (" << seconds(started)
              << " s)\n";
    // STL: 300,000 facets, welded exactly, and split the same way.
    const auto stlPath = dir + "/unique.stl";
    writeStlBinary(stlPath, 300, 500);
    StlRepairOptions repairs;
    repairs.weld = StlWeld::Exact;
    auto stl = loadStl(stlPath, StlCoordinateOptions{1.0, StlUpAxis::Z}, repairs);
    check(facesOf(stl.document) == 300000, "STL keeps every facet at the unique limit");
    check(stl.report["bodySplits"].toArray().size() == 1 &&
              geometryBodies(stl.document) == 3,
          "STL splits 300,000 facets into three bodies");
    std::cout << "STL import: 300,000 facets in 3 bodies (" << seconds(started) << " s)\n";
    // glTF: two meshes of 150,000 triangles each, one definition apiece, placed once each.
    const auto glbPath = dir + "/unique.glb";
    writeFile(glbPath, glb({{300, 250}, {300, 250}}));
    const auto gltf = loadGltf(glbPath);
    // Each mesh is used by one node, so each is a group of loose bodies and no definition (R119).
    check(gltf.report["triangles"].toInteger() == 300000 && facesOf(gltf.document) == 300000 &&
              gltf.document.definitions().empty() && gltf.report["meshGroups"].toInteger() == 2,
          "glTF keeps every triangle at the unique limit");
    std::cout << "glTF import: 300,000 triangles in two groups (" << seconds(started)
              << " s)\n";
    // DXF: one polyline of 250,000 vertices, which splits into three bodies of vertices.
    QByteArray entities = "0\nLWPOLYLINE\n8\nSheet\n90\n250000\n70\n0\n";
    for (int i = 0; i < 250000; ++i)
        entities += "10\n" + QByteArray::number(i) + "\n20\n" +
                    QByteArray::number(i % 2) + "\n";
    const QByteArray dxf = "0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1032\n9\n$INSUNITS\n70\n6\n"
                           "0\nENDSEC\n0\nSECTION\n2\nENTITIES\n" +
                           entities + "0\nENDSEC\n0\nEOF\n";
    const auto dxfPath = dir + "/unique.dxf";
    writeFile(dxfPath, dxf);
    const auto planar = loadDxf(dxfPath);
    std::size_t wires = 0;
    for (const auto &[id, body] : planar.document.bodies()) {
        (void)id;
        wires += body->surface.wires.size();
    }
    check(wires == 249999, "DXF keeps every segment of the polyline");
    check(planar.report["bodySplits"].toArray().size() == 1 && wireBodies(planar.document) == 3,
          "DXF splits the polyline into three bodies of at most 100,000 vertices");
    std::cout << "DXF import: 249,999 segments (" << seconds(started) << " s)\n";
}
void refusalsByName(const QString &dir) {
    // One past the unique face limit: OBJ refuses by name before it builds a body.
    const auto objPath = dir + "/over.obj";
    // 300 x 501 cells: 300,600 faces but only 151,102 vertices, so the face limit is the one passed.
    writeObj(objPath, 300, 501);
    const auto objRefusal =
        refusal([&] { (void)loadObj(objPath, ObjImportOptions{1.0, ObjUpAxis::Z}); });
    check(objRefusal.find("faces would be 300,001 of 300,000") != std::string::npos,
          "OBJ over the unique face limit is refused by name: " + objRefusal);
    // One past the unique facet limit: binary STL refuses by name.
    const auto stlPath = dir + "/over.stl";
    writeStlBinary(stlPath, 1, 150001, 300001);
    const auto stlRefusal = refusal([&] {
        (void)loadStl(stlPath, StlCoordinateOptions{1.0, StlUpAxis::Z}, StlRepairOptions{});
    });
    check(stlRefusal.find("facets would be 300,001 of 300,000") != std::string::npos,
          "STL over the unique facet limit is refused by name: " + stlRefusal);
    // A mesh used by one node is a group of loose bodies (R119), so the definition limit does not govern
    // it: 200,400 faces import, where the R110 definition bound of 200,000 once refused them.
    const auto singlePath = dir + "/single-use.glb";
    // 300 x 334 cells: 200,400 faces but 100,835 vertices.
    writeFile(singlePath, glb({{300, 334}}));
    const auto single = loadGltf(singlePath);
    check(single.document.definitions().empty() && facesOf(single.document) == 200400,
          "A glTF mesh used by one node is loose bodies, not a definition, so 200,400 faces import");
    // A mesh used by two nodes is one definition. The definition limit (800,000 faces, R115) refuses one
    // past it by name, and the mesh is not split to pass it.
    const auto glbPath = dir + "/over-definition.glb";
    // 300 x 1,335 cells: 801,000 faces but 402,136 vertices, so the face limit is the one passed.
    writeFile(glbPath, glb({{300, 1335}}, 2));
    const auto definitionRefusal = refusal([&] { (void)loadGltf(glbPath); });
    check(definitionRefusal.find("faces would be 801,000 of 800,000") != std::string::npos,
          "A glTF definition over 800,000 faces is refused by name: " + definitionRefusal);
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir temp;
        check(temp.isValid(), "A temporary directory");
        const QString dir = temp.path();
        exportsAtMaterializedLimits();
        dxfAtTheEdgeLimit();
        importsAtUniqueLimits(dir);
        refusalsByName(dir);
    } catch (const std::exception &error) {
        std::cerr << "format_limits_acceptance: " << error.what() << "\n";
        return 1;
    }
    std::cout << "format limits acceptance passed\n";
    return 0;
}
