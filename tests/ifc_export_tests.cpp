// R097.d: the IFC4 export, the helper included (ADR 0171 sections 3.8 and 4, checks G1 to G7 of section 6.3), checked against a
// raw-text scan of the files it writes (tests/ifc_text_scan.py, which shares no code with the library), an independent
// decoder of the package (tests/ifc_export_package.py), numbers computed by hand and the golden exports in
// tests/fixtures/ifc/export. Needs sketchyup-ifc-worker (IFC_WORKER_PATH) and python3; exits 77 (skipped) when the helper is
// not built. IFC_UPDATE_GOLDENS=1 rewrites the golden files.
#include "core/annotations.hpp"
#include "core/assets.hpp"
#include "core/reference_images.hpp"
#include "core/drawing_sheets.hpp"
#include "core/scenes.hpp"
#include "core/sections.hpp"
#include "core/terrain.hpp"
#include "core/material_records.hpp"
#include "io/dxf_import.hpp"
#include "io/gltf_import.hpp"
#include "geometry/curves.hpp"
#include "io/ifc_export.hpp"
#include "io/ifc_worker.hpp"
#include "io/native_format.hpp"
#include "io/obj_import.hpp"
#include "io/texture_image.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtEndian>
#include <bit>
#include <cmath>
#include <iostream>
#include <set>
using namespace sketchy;
namespace {
void check(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}
bool near(double a, double b, double tolerance = 1e-9) { return std::abs(a - b) <= tolerance * std::max(1.0, std::abs(b)); }
QString source(const QString &relative) { return QStringLiteral(SOURCE_DIR "/") + relative; }
QString fixture(const QString &name) { return source("tests/fixtures/ifc/" + name); }
constexpr qint64 stamp = 1767323045; // 2026-01-02T03:04:05 UTC
const char *const identity = "0123456789abcdef0123456789abcdef";

// The helper the tests run: the one built with them, or IFC_TEST_WORKER (a build of the helper without the sanitizers, when the
// application side is run under them, since the helper's environment is scrubbed and IfcParse leaks by design).
QString workerPath() {
    const auto other = qEnvironmentVariable("IFC_TEST_WORKER");
    return other.isEmpty() ? QStringLiteral(IFC_WORKER_PATH) : other;
}

IfcWorkerOptions workerOptions() {
    IfcWorkerOptions options;
    options.workerPath = workerPath();
    options.threads = 1;
    options.deadlineSeconds = 120;
#ifdef IFC_NO_ADDRESS_LIMIT
    options.addressSpaceBytes = 0;
#endif
    return options;
}

QByteArray read(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "read " + path.toStdString());
    return file.readAll();
}
void write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "write " + path.toStdString());
}
QByteArray python(const QStringList &arguments, int expectedExit = 0) {
    QProcess process;
    process.start("python3", QStringList{"-I", "-B"} + arguments); // (no bytecode written into the source tree)
    check(process.waitForFinished(300000), "python timed out");
    check(process.exitCode() == expectedExit, "python " + arguments.join(' ').toStdString() + ": " + process.readAllStandardError().toStdString());
    return process.readAllStandardOutput();
}
QJsonObject scan(const QString &path) {
    return QJsonDocument::fromJson(python({source("tests/ifc_text_scan.py"), path})).object();
}
QJsonObject checkFile(const QString &path) {
    QProcess process;
    process.start(workerPath(), {"--check", path});
    check(process.waitForFinished(300000), "check timed out");
    QJsonObject result;
    for (const auto &line : process.readAllStandardOutput().split('\n'))
        if (line.startsWith("R "))
            result = QJsonDocument::fromJson(line.mid(2)).object();
    check(process.exitCode() == 0 && result["status"] == "ok", "the helper's schema check of " + path.toStdString() + ": " + QJsonDocument(result).toJson(QJsonDocument::Compact).toStdString());
    return result;
}

// ---- Documents -----------------------------------------------------------------------------------------------------
// The same document with a known identity, so that derived GlobalIds and golden files do not depend on chance.
Document withIdentity(const Document &document, const char *id = identity) {
    auto json = QJsonDocument::fromJson(encodeDocument(document)).object();
    json["documentId"] = id;
    return decodeDocument(QJsonDocument(json).toJson(QJsonDocument::Compact));
}

std::shared_ptr<Body> box(Id id, Id parent, const std::string &name, Vec3 origin, Vec3 size) {
    auto body = std::make_shared<Body>();
    body->id = id;
    body->parent = parent;
    body->name = name;
    const auto face = body->surface.addFace({{origin, origin + Vec3{size.x, 0, 0}, origin + Vec3{size.x, size.y, 0}, origin + Vec3{0, size.y, 0}}});
    body->surface.extrude(face, size.z);
    body->topology = Topology::rebuild(body->surface, {});
    return body;
}
// A square plate with a square hole through it: one face with two loops, extruded.
std::shared_ptr<Body> frame(Id id, Id parent, const std::string &name, Vec3 origin) {
    auto body = std::make_shared<Body>();
    body->id = id;
    body->parent = parent;
    body->name = name;
    const auto o = origin;
    const auto face = body->surface.addFace({{o, o + Vec3{4, 0, 0}, o + Vec3{4, 4, 0}, o + Vec3{0, 4, 0}},
                                             {o + Vec3{1, 1, 0}, o + Vec3{1, 3, 0}, o + Vec3{3, 3, 0}, o + Vec3{3, 1, 0}}});
    body->surface.extrude(face, 1);
    body->topology = Topology::rebuild(body->surface, {});
    return body;
}
std::shared_ptr<Body> group(Id id, Id parent, const std::string &name, Transform transform = {}) {
    auto body = std::make_shared<Body>();
    body->id = id;
    body->parent = parent;
    body->name = name;
    body->kind = BodyKind::Group;
    body->transform = transform;
    return body;
}

struct Model {
    Document document;
    std::map<std::string, Id> ids;
};
// The native model of the tests: groups with rigid, scaled and mirrored placements, a material, per-face colours, hidden
// bodies and a hidden tag, classes (valid, odd, a door), properties of every type, two placements of a component, an empty
// group and a body with only wires.
Model nativeModel(bool site = true, const char *crs = "EPSG:32760") {
    Model model;
    Document &doc = model.document;
    Edit edit{"Native model", {}};
    auto add = [&](std::shared_ptr<Body> body, const char *key) {
        model.ids[key] = body->id;
        edit.changes.push_back({body->id, nullptr, body});
        return body;
    };
    {
        auto tag = std::make_shared<TagRecord>();
        tag->id = 1;
        tag->name = "Hidden things";
        tag->visible = false;
        edit.tags.push_back({1, nullptr, tag});
        edit.nextTagFloor = 2;
        auto brick = std::make_shared<MaterialRecord>();
        brick->id = 1;
        brick->name = "Brick";
        brick->color = {0.7f, 0.2f, 0.1f};
        brick->opacity = 0.5f;
        edit.materials.push_back({1, nullptr, brick});
        edit.nextMaterialFloor = 2;
    }
    add(group(1, 0, "House", Transform::translation({10, 20, 0}) * Transform::rotation({0, 0, 1}, std::numbers::pi / 6)), "house");
    add(group(2, 1, "Ground floor", Transform::translation({0, 0, 0.5})), "ground");
    {
        auto wall = add(box(3, 2, "Wall A", {0, 0, 0}, {4, 0.3, 2.5}), "wallA");
        wall->materials = MaterialSides{1, 1};
        wall->properties = {{"Pset_WallCommon.FireRating", std::string("REI30")}, {"Pset_WallCommon.IsExternal", true}, {"Thermal.U", 0.25},
                            {"note", std::string("plain key")}, {"ifc.Step", 267.0}};
    }
    {
        auto wall = add(box(4, 2, "Wall B", {0, 0, 0}, {2, 0.3, 2.5}), "wallB");
        wall->transform = Transform::translation({4, 0, 0});
        // Two faces of their own colour: the first and the last.
        const auto first = wall->surface.faces.begin()->first, last = wall->surface.faces.rbegin()->first;
        wall->faceColors[first] = {1, 0, 0};
        wall->faceColors[last] = {0, 0.5, 1};
    }
    add(box(5, 2, "Hidden slab", {0, 3, 0}, {2, 2, 0.2}), "hidden")->hidden = true;
    add(box(6, 2, "In hidden tag", {3, 3, 0}, {1, 1, 1}), "tagged")->tag = 1;
    {
        auto door = add(box(7, 2, "Door", {6, 0, 0}, {1, 0.1, 2.1}), "door");
        door->properties = {{"ifc.Class", std::string("IfcDoor")}, {"ifc.PredefinedType", std::string("DOOR")}};
    }
    add(box(8, 2, "Odd \u00fc \u4e2d\u6587", {8, 0, 0}, {1, 1, 1}), "odd")->properties = {{"ifc.Class", std::string("IfcFoo")}};
    {
        auto mirror = add(box(9, 2, "Mirror", {0, 0, 0}, {1, 2, 3}), "mirror");
        mirror->transform = Transform::translation({10, 0, 0}) * Transform::scaling({-1, 1, 1});
    }
    add(group(10, 2, "Empty group"), "empty");
    add(frame(42, 2, "Frame with a hole", {20, 20, 0}), "frame");
    // A group with a scale and a child placed inside it: the child's placement is the true one, the scale is baked into its points.
    add(group(44, 0, "Scaled group", Transform::translation({5, 0, 0}) * Transform::scaling({2, 2, 2})), "scaledGroup");
    add(box(45, 44, "Scaled child", {0, 0, 0}, {1, 1, 1}), "scaledChild")->transform = Transform::translation({1, 0, 0});
    // A hidden group hides what is inside it.
    add(group(40, 2, "Hidden group"), "hiddenGroup")->hidden = true;
    add(box(41, 40, "In hidden group", {0, 6, 0}, {1, 1, 1}), "inHidden");
    {
        auto chair = add(box(11, 1, "Chair", {0, 0, 0}, {0.5, 0.5, 0.9}), "chair");
        chair->transform = Transform::translation({2, 5, 0}) * Transform::scaling({1.5, 1.5, 1.5});
    }
    add(box(12, 0, "Loose", {20, 20, 0}, {1, 1, 1}), "loose")->properties = {{"Site.Note", std::string("loose body")}};
    {
        auto wires = std::make_shared<Body>();
        wires->id = 13;
        wires->name = "Wires";
        const auto a = wires->surface.vertex({0, 0, 0}), b = wires->surface.vertex({1, 0, 0}), c = wires->surface.vertex({1, 1, 0});
        wires->surface.wires = {{a, b}, {b, c}};
        wires->topology = Topology::rebuild(wires->surface, {});
        add(wires, "wires");
    }
    // A component of two placements: a stool (a group root and one box).
    {
        auto definition = std::make_shared<ComponentDefinition>();
        definition->id = 1;
        definition->root = 1;
        definition->name = "Stool";
        auto root = group(1, 0, "Stool");
        auto member = box(2, 1, "Seat", {0, 0, 0}, {0.4, 0.4, 0.45});
        definition->members[1] = root;
        definition->members[2] = member;
        definition->nextMemberId = 3;
        edit.definitions.push_back({1, nullptr, definition});
        edit.nextDefinitionFloor = 2;
        Id next = 14;
        const std::pair<Id, Transform> placements[] = {{0, Transform::translation({1, 8, 0})},
                                                       {2, Transform::translation({0.5, 0.5, 0}) * Transform::rotation({0, 0, 1}, 1.0)}};
        int index = 0;
        for (const auto &[parent, transform] : placements) {
            auto instance = std::make_shared<ComponentInstance>();
            instance->definition = 1;
            auto placedRoot = std::make_shared<Body>(*root);
            placedRoot->id = next++;
            placedRoot->parent = parent;
            placedRoot->transform = transform;
            placedRoot->name = index == 0 ? "Stool 1" : "Stool 2";
            auto placedSeat = std::make_shared<Body>(*member);
            placedSeat->id = next++;
            placedSeat->parent = placedRoot->id;
            instance->members[1] = placedRoot->id;
            instance->members[2] = placedSeat->id;
            edit.changes.push_back({placedRoot->id, nullptr, placedRoot});
            edit.changes.push_back({placedSeat->id, nullptr, placedSeat});
            edit.instances.push_back({placedRoot->id, nullptr, instance});
            model.ids[index == 0 ? "stool1" : "stool2"] = placedRoot->id;
            ++index;
        }
        edit.nextIdFloor = 46;
    }
    doc.apply(std::move(edit), doc.revision());
    if (site)
        setSiteOrigin(doc, SiteOrigin{729013.3488297004, 9063992.684697364, 1.3, crs, {"test", ""}});
    return model;
}

// Things the export cannot write: a guide, a saved scene, a section, a drawing sheet, a terrain and a label.
void addUnwritable(Document &doc, Id body, Id edgeBody) {
    // A circle: a disc (one open face) with its analytic record, which the tessellation does not keep.
    doc.addCurve(0, centerCurve(CurveKind::Circle, {}, 2, 0, 2 * std::acos(-1), 24));
    doc.addGuide(body, guideLine({0, 0, 1}, {1, 0, 0}));
    doc.addGuide(body, guidePoint({1, 1, 1}));
    SceneSnapshot snapshot;
    snapshot.camera = SceneCamera{};
    createScene(doc, "Front", snapshot);
    createScene(doc, "Back", snapshot);
    createSection(doc, "Cut", 0, {{1, 0, 0}, -1});
    createDrawingSheet(doc, "Cover", {420, 297});
    TerrainMesh mesh{{{100, 100, 0}, {102, 100, 0}, {102, 102, 1}, {100, 102, 0}}, {{0, 1, 2}, {0, 2, 3}}};
    TerrainRecord record;
    record.generator = "test/terrain-v1";
    createTerrain(doc, mesh, record);
    const auto &surface = doc.bodies().at(edgeBody)->surface;
    Id first = surface.vertices.begin()->first, second = std::next(surface.vertices.begin())->first;
    Id edge = 0;
    for (const auto &[id, e] : doc.bodies().at(edgeBody)->topology.edges)
        if ((e.a == first && e.b == second) || (e.a == second && e.b == first))
            edge = id;
    if (!edge)
        edge = doc.bodies().at(edgeBody)->topology.edges.begin()->first;
    // A reference image and a textured material.
    {
        const auto png = encodeTexturePng(TextureImage(2, 1, {255, 0, 0, 255, 0, 255, 0, 128}));
        const auto asset = createAsset(doc, "Pixels", "image/png", std::make_shared<AssetPayload>(std::vector<std::uint8_t>(png.begin(), png.end())));
        createReferenceImage(doc, ReferenceImage{asset, 4, 2, .6}, Transform::translation({30, 30, 0}));
        Edit edit{"Textured material", {}};
        auto textured = std::make_shared<MaterialRecord>();
        textured->id = doc.nextMaterialId();
        textured->name = "Tiles";
        textured->color = {0.4f, 0.4f, 0.9f};
        textured->asset = asset;
        edit.materials.push_back({textured->id, nullptr, textured});
        edit.nextMaterialFloor = textured->id + 1;
        doc.apply(std::move(edit), doc.revision());
    }
    AnnotationRecord label;
    label.name = "Label";
    label.kind = AnnotationKind::Label;
    label.text = "Label";
    label.anchors = {edgeAnchor(doc, edgeBody, edge, .5)};
    createAnnotation(doc, label);
}

// ---- Export, scan, re-import ------------------------------------------------------------------------------------------
struct Written {
    QString path;
    QByteArray bytes;
    QJsonObject report;
};
Written exportTo(const Document &document, const QString &directory, const QString &name, qint64 time = stamp) {
    IfcExportOptions options;
    options.timestamp = time;
    const auto path = QDir(directory).filePath(name);
    const auto result = exportIfcFile(document, path, options, workerOptions());
    Written written{path, read(path), result.report};
    check(result.bytes == written.bytes.size(), "reported size");
    return written;
}
std::string textOf(const Body &body, const char *key) {
    const auto found = body.properties.find(key);
    return found != body.properties.end() && std::holds_alternative<std::string>(found->second) ? std::get<std::string>(found->second) : std::string();
}

// World-space vertices of a body (positions of its surface vertices), sorted.
std::vector<std::array<double, 3>> worldVertices(const Document &doc, Id id) {
    std::vector<std::array<double, 3>> out;
    const auto world = doc.worldTransform(id);
    for (const auto &[vertex, p] : doc.bodies().at(id)->surface.vertices) {
        const auto w = world.point(p);
        out.push_back({w.x, w.y, w.z});
    }
    std::sort(out.begin(), out.end());
    return out;
}
double worldArea(const Document &doc, Id id) {
    double area = 0;
    for (const auto &t : doc.worldTriangles(id))
        area += 0.5 * length(cross(t.b - t.a, t.c - t.a));
    return area;
}
double worldVolume(const Document &doc, Id id) {
    double volume = 0;
    for (const auto &t : doc.worldTriangles(id))
        volume += dot(t.a, cross(t.b, t.c)) / 6;
    return volume;
}
bool geometric(const Body &b) { return b.kind == BodyKind::Geometry && !b.surface.faces.empty(); }
std::vector<Id> geometryInOrder(const Document &doc) {
    // Pre-order, children by ID, roots by ID: the order the export writes.
    std::map<Id, std::vector<Id>> children;
    std::vector<Id> roots, out;
    for (const auto &[id, b] : doc.bodies())
        (b->parent ? children[b->parent] : roots).push_back(id);
    std::vector<Id> stack(roots.rbegin(), roots.rend());
    while (!stack.empty()) {
        const Id id = stack.back();
        stack.pop_back();
        if (doc.bodies().at(id)->kind == BodyKind::Terrain)
            continue; // terrain is not written, and neither are its tiles
        if (geometric(*doc.bodies().at(id)))
            out.push_back(id);
        if (children.contains(id))
            stack.insert(stack.end(), children[id].rbegin(), children[id].rend());
    }
    return out;
}
std::vector<std::string> pathOf(const Document &doc, Id id) {
    std::vector<std::string> names;
    for (Id at = id; at; at = doc.bodies().at(at)->parent)
        names.insert(names.begin(), doc.bodies().at(at)->name);
    return names;
}
bool effectivelyHidden(const Document &doc, Id id) {
    for (Id at = id; at; at = doc.bodies().at(at)->parent) {
        const auto &b = *doc.bodies().at(at);
        if (b.hidden || (b.tag && !tagVisible(doc.tags(), b.tag)))
            return true;
    }
    return false;
}

// G2 (areas and volumes are measured on the native tessellation, which snaps to a 1e-7 m grid, hence 1e-6 relative): the re-import has the same bodies (by name, in the written order), the same vertices (1e-9 m), areas and volumes, the same
// hierarchy below the made-up site and building, the same properties, materials, hidden state and site origin.
void sameModel(const Document &a, const Document &b, const std::string &label, const std::set<std::string> &skipNames = {}, bool ordered = true) {
    auto first = geometryInOrder(a), second = geometryInOrder(b);
    if (!ordered) {
        // By name, then by the first world vertex.
        const auto key = [](const Document &d, Id id) { return std::make_pair(d.bodies().at(id)->name, worldVertices(d, id)); };
        std::sort(first.begin(), first.end(), [&](Id x, Id y) { return key(a, x) < key(a, y); });
        std::sort(second.begin(), second.end(), [&](Id x, Id y) { return key(b, x) < key(b, y); });
    }
    std::vector<Id> expected;
    for (const Id id : first)
        if (!skipNames.contains(a.bodies().at(id)->name))
            expected.push_back(id);
    check(expected.size() == second.size(), label + ": " + std::to_string(second.size()) + " bodies come back, " + std::to_string(expected.size()) + " expected");
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const Id x = expected[i], y = second[i];
        const auto &bx = *a.bodies().at(x), &by = *b.bodies().at(y);
        const std::string where = label + ": " + bx.name;
        check(bx.name == by.name, where + " name comes back as " + by.name);
        const auto vx = worldVertices(a, x), vy = worldVertices(b, y);
        if (vx.size() != vy.size()) {
            auto dump = [](const std::vector<std::array<double, 3>> &v) {
                std::string out;
                for (const auto &p : v)
                    out += " (" + std::to_string(p[0]) + "," + std::to_string(p[1]) + "," + std::to_string(p[2]) + ")";
                return out;
            };
            std::cerr << where << "\n original:" << dump(vx) << "\n back:" << dump(vy) << "\n faces " << bx.surface.faces.size() << " against " << by.surface.faces.size() << "\n";
        }
        check(vx.size() == vy.size(), where + " vertex count " + std::to_string(vy.size()) + " against " + std::to_string(vx.size()));
        for (std::size_t k = 0; k < vx.size(); ++k)
            for (int c = 0; c < 3; ++c)
                check(std::abs(vx[k][std::size_t(c)] - vy[k][std::size_t(c)]) <= 1e-9, where + " vertex coordinate");
        check(near(worldArea(a, x), worldArea(b, y), 1e-6), where + " area " + std::to_string(worldArea(a, x)) + " against " + std::to_string(worldArea(b, y)));
        check(near(std::abs(worldVolume(a, x)), std::abs(worldVolume(b, y)), 1e-6), where + " volume " + std::to_string(worldVolume(a, x)) + " against " + std::to_string(worldVolume(b, y)));
        // The hierarchy below the made-up levels (or below the IFC levels of an imported model).
        auto px = pathOf(a, x), py = pathOf(b, y);
        px.pop_back();
        py.pop_back();
        if (py.size() >= px.size() && !std::equal(px.begin(), px.end(), py.end() - long(px.size())))
            check(false, where + " ancestors differ");
        // Properties: every property of the original comes back (a key without a dot in the set "SketchyUp").
        for (const auto &[key, value] : bx.properties) {
            if (key.rfind("ifc.", 0) == 0)
                continue;
            const std::string back = key.find('.') == std::string::npos ? "SketchyUp." + key : key;
            const auto found = by.properties.find(back);
            check(found != by.properties.end(), where + " property " + key + " comes back as " + back);
            check(found->second == value, where + " property " + key + " value");
        }
        // (A space comes back on the importer's tag for spaces, which starts hidden: that is the importer's rule, not the file's.)
        bool inSpace = false;
        for (Id at = x; at; at = a.bodies().at(at)->parent)
            inSpace = inSpace || textOf(*a.bodies().at(at), "ifc.Class") == "IfcSpace";
        if (!inSpace)
            check(effectivelyHidden(a, x) == effectivelyHidden(b, y), where + " hidden state");
        // Appearance: the material by name, colour and opacity.
        if (bx.materials.front) {
            const auto &mx = *a.materials().at(bx.materials.front);
            check(by.materials.front != 0, where + " keeps a material");
            const auto &my = *b.materials().at(by.materials.front);
            check(mx.name == my.name && mx.color == my.color && mx.opacity == my.opacity, where + " material " + mx.name + " came back as " + my.name);
        }
    }
    if (a.site()) {
        check(b.site().has_value(), label + ": the site origin comes back");
        check(near(a.site()->easting, b.site()->easting) && near(a.site()->northing, b.site()->northing) && near(a.site()->elevation, b.site()->elevation) &&
                  a.site()->crs == b.site()->crs,
              label + ": site origin values and label");
    }
}

Document reimport(const QString &path, const IfcImportOptions &options = {}) {
    return importIfcFile(path, options, workerOptions()).document;
}

void golden(const Written &written, const QString &name) {
    const auto path = fixture("export/" + name);
    if (qEnvironmentVariableIsSet("IFC_UPDATE_GOLDENS")) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        write(path, written.bytes);
        std::cout << "  rewrote golden " << name.toStdString() << " (" << written.bytes.size() << " bytes)\n";
        return;
    }
    check(QFile::exists(path), "G6: the golden file " + name.toStdString() + " is missing (IFC_UPDATE_GOLDENS=1 writes it)");
    check(read(path) == written.bytes, "G6: the export differs from the golden file " + name.toStdString());
}

// ---- Sections ----------------------------------------------------------------------------------------------------------
void packageAndPlan() {
    auto model = nativeModel();
    auto &doc = model.document;
    addUnwritable(doc, model.ids["wallA"], model.ids["wallB"]);
    const auto stable = withIdentity(doc);
    IfcExportOptions options;
    options.timestamp = stamp;
    const auto a = captureIfcExport(stable, options, "model.ifc"), b = captureIfcExport(stable, options, "model.ifc");
    check(a.package == b.package && a.plan == b.plan, "G3: two captures of one document are byte for byte equal");
    // Independent decoding of the package.
    QTemporaryDir dir;
    write(dir.filePath("p.skifcx"), a.package);
    const auto decoded = QJsonDocument::fromJson(python({source("tests/ifc_export_package.py"), dir.filePath("p.skifcx")})).object();
    const auto manifest = decoded["manifest"].toObject();
    check(manifest["documentId"] == identity && manifest["timestamp"] == "2026-01-02T03:04:05" && manifest["fileName"] == "model.ifc", "the package header, read independently");
    check(manifest["georeference"].toObject()["crs"] == "EPSG:32760", "the package carries the georeference");
    const auto plan = a.plan;
    const auto written = plan["written"].toObject();
    // By hand: groups House, Ground floor, Hidden group and Scaled group (Empty group is not written); bodies A, B, Hidden, Tagged, Door, Odd, Mirror,
    // Frame, In hidden group, Scaled child, Chair, Loose and the two stools' seats; two placements.
    check(written["groups"].toInt() == 4, "G4: groups written");
    check(written["bodies"].toInt() == 15, "G4: bodies written " + QJsonDocument(written).toJson(QJsonDocument::Compact).toStdString());
    check(written["placements"].toInt() == 2, "G4: placements written as copies");
    check(manifest["nodes"].toArray().size() == written["nodes"].toInt(), "the package's nodes are the plan's");
    check(manifest["nodes"].toArray().size() == 3 + 4 + 15 + 2, "node count: project, site, building, 4 groups, 15 bodies (the disc is one), 2 placements");
    {
        // The scaled group's child: its placement is where it stands in the (rigid) frame of the group, not its raw local transform.
        bool found = false;
        for (const auto &n : manifest["nodes"].toArray()) {
            const auto node = n.toObject();
            if (qint64(node["id"].toDouble()) != 45)
                continue;
            const auto placement = node["placement"].toArray();
            check(placement[0].toDouble() == 2 && placement[1].toDouble() == 0 && placement[2].toDouble() == 0, "a child of a scaled group is placed at its true place in the group's frame: " + QJsonDocument(placement).toJson(QJsonDocument::Compact).toStdString());
            found = true;
        }
        check(found, "the scaled child is in the package");
    }
    const auto notWritten = plan["notWritten"].toObject();
    check(notWritten["emptyGroups"].toInt() == 1 && notWritten["bodiesWithoutFaces"].toInt() == 1 && notWritten["looseEdges"].toInt() == 2 && notWritten["curves"].toInt() == 1 &&
              notWritten["guides"].toInt() == 2 && notWritten["scenes"].toInt() == 2 && notWritten["sections"].toInt() == 1 && notWritten["sheets"].toInt() == 1 &&
              notWritten["annotations"].toInt() == 1 && notWritten["terrain"].toInt() == 1 && notWritten["terrainTiles"].toInt() >= 1 &&
              notWritten["referenceImages"].toInt() == 1 && notWritten["textures"].toInt() == 1,
          "G4: every kind that is not written is counted: " + QJsonDocument(notWritten).toJson(QJsonDocument::Compact).toStdString());
    const auto accounting = plan["accounting"].toObject();
    qint64 sum = accounting["written"].toInteger() + accounting["writtenAsCopies"].toInteger() + accounting["notWritten"].toInteger();
    qint64 independent = qint64(stable.bodies().size()) + 2 /*guides*/ + 2 /*wires*/ + 1 /*circle record*/ + 1 /*label*/ + 2 /*scenes*/ + 1 /*section*/ + 1 /*sheet*/ + 1 /*textured material*/;
    check(sum == accounting["items"].toInteger() && sum == independent, "the accounting identity: " + std::to_string(sum) + " against " + std::to_string(independent));
    check(plan["georeference"].toObject()["result"] == "written", "G5: an authority code is written");
    check(plan["spatial"].toObject()["storeysInvented"].toInt() == 0 && plan["spatial"].toObject()["synthesizedSite"].toBool() && plan["spatial"].toObject()["synthesizedBuilding"].toBool(),
          "one site and one building are made up and no storey is invented");
    check(written["hidden"].toInt() == 3, "hidden bodies: the hidden slab, the one in the hidden tag and the one in the hidden group");
    check(written["propertySets"].toInt() == 4 && written["properties"].toInt() == 5 /* wall A: 4 in 3 sets; loose body: 1 in 1 */, "property sets and properties (ifc.* are not written): " + QJsonDocument(written).toJson(QJsonDocument::Compact).toStdString());

    // G5: the CRS label rule.
    auto georef = [&](const char *crs, bool site) {
        auto m = nativeModel(site, crs);
        return planIfcExport(m.document)["georeference"].toObject();
    };
    check(georef("EPSG:32760", true)["result"] == "written", "G5: EPSG:32760 is written");
    check(georef("ESRI:102100", true)["result"] == "written" && georef("OGC:CRS84", true)["result"] == "written", "G5: other authority codes are written");
    for (const char *label : {"Local grid", "epsg 32760", "EPSG:", ":32760", "EPSG:327 60", "1PSG:32760", "EPSG32760", "E:1"}) {
        const auto g = georef(label, true);
        check(g["result"] == "notWritten" && g["reason"] == "crsNotAuthorityCode", std::string("G5: ") + label + " is free text, not written");
    }
    const auto none = georef("", true);
    check(none["result"] == "notWritten" && none["reason"] == "noCrsLabel", "G5: no label, not written");
    const auto absent = georef("", false);
    check(absent["result"] == "notApplicable" && absent["reason"] == "noSiteOrigin", "G5: no site origin, nothing to write");
    check(isAuthorityCode("EPSG:32760") && !isAuthorityCode("Local grid") && !isAuthorityCode(""), "isAuthorityCode");
    {
        auto m = nativeModel(true, "Local grid");
        const auto p = planIfcExport(m.document);
        bool attention = false;
        for (const auto &n : p["notices"].toArray())
            attention = attention || n.toObject()["code"] == "georeferenceNotWritten";
        check(attention && p["attention"].toInt() == 1, "G5: the report says why the georeference was not written, and asks for one look");
        const auto captured = captureIfcExport(m.document, options, "x.ifc");
        write(dir.filePath("q.skifcx"), captured.package);
        const auto q = QJsonDocument::fromJson(python({source("tests/ifc_export_package.py"), dir.filePath("q.skifcx")})).object();
        check(q["manifest"].toObject()["georeference"].isNull(), "G5: a free-text label puts no georeference in the package");
    }

    // G7 and decision 16: kept when valid and unique, derived otherwise, stable across edits and copies.
    {
        auto m = nativeModel(false);
        auto edit = Edit{"GlobalIds", {}};
        const auto kept = derivedIfcGlobalId("some other document", "x", 99);
        auto change = [&](Id id, const char *key, const std::string &value) {
            auto body = std::make_shared<Body>(*m.document.bodies().at(id));
            body->properties[key] = value;
            edit.changes.push_back({id, m.document.bodies().at(id), body});
        };
        change(m.ids["wallA"], "ifc.GlobalId", kept);
        change(m.ids["wallB"], "ifc.GlobalId", kept);   // a copy of an imported body carries the same value
        change(m.ids["door"], "ifc.GlobalId", "short"); // not valid
        change(m.ids["odd"], "ifc.GlobalId", "4zzzzzzzzzzzzzzzzzzzzz"); // first character above 3
        m.document.apply(std::move(edit), m.document.revision());
        const auto base = withIdentity(m.document);
        const auto capturedA = captureIfcExport(base, options, "g.ifc");
        const auto g = capturedA.plan["globalIds"].toObject();
        check(g["kept"].toInt() == 1 && g["replacedDuplicate"].toInt() == 1 && g["replacedInvalid"].toInt() == 2 && g["replaced"].toInt() == 3, "G7: kept and replaced identifiers are counted: " + QJsonDocument(g).toJson(QJsonDocument::Compact).toStdString());
        check(capturedA.plan["attention"].toInt() == 3, "G7: three items need a look");
        check(capturedA.plan["locationTotals"].toObject()["globalIdReplaced"].toInt() == 3 && capturedA.plan["locations"].toArray().size() == 3, "G7: the replaced identifiers are located");
        QString names;
        for (const auto &l : capturedA.plan["locations"].toArray())
            names += l.toObject()["name"].toString() + "/" + l.toObject()["reason"].toString() + " ";
        check(names.contains("Wall B/duplicate") && names.contains("Door/invalid") && names.contains("Odd \u00fc \u4e2d\u6587/invalid") && !names.contains("Wall A"), "G7: the first claimant (the lower ID) keeps the identifier: " + names.toStdString());
        write(dir.filePath("g.skifcx"), capturedA.package);
        const auto decodedG = QJsonDocument::fromJson(python({source("tests/ifc_export_package.py"), dir.filePath("g.skifcx")})).object()["manifest"].toObject();
        std::map<qint64, std::string> guidOf;
        std::set<std::string> all;
        for (const auto &n : decodedG["nodes"].toArray()) {
            guidOf[qint64(n.toObject()["id"].toDouble())] = n.toObject()["guid"].toString().toStdString();
            check(all.insert(n.toObject()["guid"].toString().toStdString()).second, "G7: every GlobalId in the package is unique");
        }
        check(guidOf[m.ids["wallA"]] == kept, "G7: the imported identifier is kept");
        check(guidOf[m.ids["wallB"]] == derivedIfcGlobalId(base.identity(), "body", m.ids["wallB"]), "G7: the replacement is the derived identifier");
        // Stable across an edit that adds a body, across a copy, and different for another document.
        auto edited = base;
        {
            Edit add{"More", {}};
            add.changes.push_back({90, nullptr, box(90, 0, "Late", {50, 0, 0}, {1, 1, 1})});
            add.nextIdFloor = 91;
            edited.apply(std::move(add), edited.revision());
        }
        const auto capturedB = captureIfcExport(edited, options, "g.ifc");
        write(dir.filePath("h.skifcx"), capturedB.package);
        const auto decodedH = QJsonDocument::fromJson(python({source("tests/ifc_export_package.py"), dir.filePath("h.skifcx")})).object()["manifest"].toObject();
        std::map<qint64, std::string> after;
        for (const auto &n : decodedH["nodes"].toArray())
            after[qint64(n.toObject()["id"].toDouble())] = n.toObject()["guid"].toString().toStdString();
        for (const auto &[id, guid] : guidOf)
            check(after[id] == guid, "G7: a GlobalId does not change when another body is added (body " + std::to_string(id) + ")");
        const auto other = withIdentity(m.document, "ffffffffffffffffffffffffffffffff");
        const auto capturedC = captureIfcExport(other, options, "g.ifc");
        check(capturedC.package != capturedA.package, "G7: another document derives other identifiers");
        // The derivation equals an independent one (hashlib).
        const auto expected = QString::fromUtf8(python({"-c",
                                                         "import sys; sys.path.insert(0, sys.argv[1]); import ifc_export_package as p; print(p.derived_global_id(sys.argv[2], sys.argv[3], int(sys.argv[4])))",
                                                         source("tests"), identity, "body", "12345"}))
                                  .trimmed();
        check(derivedIfcGlobalId(identity, "body", 12345).c_str() == expected.toStdString(), "G7: the derivation equals the independent one: " + expected.toStdString());
        check(validIfcGlobalId(expected.toStdString()) && validIfcGlobalId("0123456789012345678901") && validIfcGlobalId("3$$$$$$$$$$$$$$$$$$$$$") && !validIfcGlobalId("") &&
                  !validIfcGlobalId("short") && !validIfcGlobalId("01234567890123456789012") && !validIfcGlobalId("4123456789012345678901") && !validIfcGlobalId("012345678901234567890-"),
              "the shape check");
    }
    // An empty model is refused, with the counts.
    {
        Document empty;
        try {
            captureIfcExport(empty, options, "e.ifc");
            check(false, "an empty model must be refused");
        } catch (const IfcExportError &error) {
            check(error.code() == "emptyModel", "the empty model is refused with its own code");
        }
    }
}


// G1: the scan of a written file against the document and the report.
void scanAgainstDocument(const Document &doc, const Written &written, const std::set<Id> &unwritten) {
    const auto facts = scan(written.path);
    check(facts["schema"] == "IFC4", "G1: IFC4");
    check(facts["units"].toObject()["length"].toDouble() == 1 && facts["units"].toObject()["area"].toDouble() == 1 && facts["units"].toObject()["volume"].toDouble() == 1,
          "G1: metres, square metres, cubic metres");
    checkFile(written.path);
    // Entity counts equal the report's, which the application compared with the document's before it published.
    const auto fileReport = written.report["file"].toObject();
    check(fileReport["entities"].toInt() == facts["entities"].toInt(), "G1: the helper's entity count equals the scan's");
    const auto scanned = facts["classes"].toObject();
    const auto reported = fileReport["classes"].toObject();
    for (auto it = reported.begin(); it != reported.end(); ++it)
        check(scanned[it.key().toUpper()].toInt() == it.value().toInt(), "G1: class " + it.key().toStdString() + " counts agree with the scan");
    const auto planned = written.report["spatial"].toObject();
    check(scanned["IFCSITE"].toInt() == planned["sites"].toInt() && scanned["IFCBUILDING"].toInt() == planned["buildings"].toInt() &&
              scanned["IFCBUILDINGSTOREY"].toInt() == planned["storeys"].toInt() && scanned["IFCSPACE"].toInt() == planned["spaces"].toInt() && scanned["IFCPROJECT"].toInt() == 1,
          "G1: the spatial structure equals the report's");
    // The hierarchy by GlobalId: a body's parent is its nearest written ancestor, or the building.
    const auto parents = facts["parents"].toObject();
    const auto objects = facts["objects"].toObject();
    const std::string project = derivedIfcGlobalId(doc.identity(), "project", 0), building = derivedIfcGlobalId(doc.identity(), "building", 0),
                      site = derivedIfcGlobalId(doc.identity(), "site", 0);
    check(parents[QString::fromStdString(site)] == QString::fromStdString(project) && parents[QString::fromStdString(building)] == QString::fromStdString(site), "G1: project, site, building");
    std::size_t checkedParents = 0;
    for (const auto &[id, body] : doc.bodies()) {
        if (unwritten.contains(id))
            continue;
        const auto guid = QString::fromStdString(derivedIfcGlobalId(doc.identity(), "body", id));
        check(objects.contains(guid), "G1: body " + body->name + " is in the file");
        Id ancestor = body->parent;
        while (ancestor && unwritten.contains(ancestor))
            ancestor = doc.bodies().at(ancestor)->parent;
        const auto expected = ancestor ? derivedIfcGlobalId(doc.identity(), "body", ancestor) : building;
        check(parents[guid].toString().toStdString() == expected, "G1: the parent of " + body->name + " in the file");
        check(objects[guid].toObject()["name"].toString().toStdString() == body->name, "G1: the name of " + body->name);
        // Properties.
        QJsonObject want;
        for (const auto &[key, value] : body->properties) {
            if (key.rfind("ifc.", 0) == 0)
                continue;
            const auto name = QString::fromStdString(key.find('.') == std::string::npos ? "SketchyUp." + key : key);
            if (const auto *flag = std::get_if<bool>(&value))
                want[name] = *flag;
            else if (const auto *number = std::get_if<double>(&value))
                want[name] = *number;
            else
                want[name] = QString::fromStdString(std::get<std::string>(value));
        }
        check(facts["occurrenceProperties"].toObject()[guid].toObject() == want, "G1: the properties of " + body->name);
        // Hidden: on the layer that is off.
        const bool onLayer = facts["layers"].toObject().contains(guid);
        const bool hasShape = geometric(*body);
        if (hasShape)
            check(onLayer == effectivelyHidden(doc, id), "G1: the layer of " + body->name);
        ++checkedParents;
    }
    check(checkedParents > 0, "G1: bodies were compared");
    // The georeference.
    const auto maps = facts["mapConversions"].toArray();
    if (doc.site() && isAuthorityCode(doc.site()->crs)) {
        check(maps.size() == 1, "G1: one map conversion");
        const auto m = maps[0].toObject();
        check(m["eastings"].toDouble() == doc.site()->easting && m["northings"].toDouble() == doc.site()->northing && m["orthogonalHeight"].toDouble() == doc.site()->elevation &&
                  m["xAxisAbscissa"].toDouble() == 1 && m["xAxisOrdinate"].toDouble() == 0 && m["scale"].toDouble() == 1,
              "G1: the map conversion is the site origin, with no rotation and scale 1");
        check(written.report["georeference"].toObject()["result"] == "written", "G1: the report says it was written");
    } else {
        check(maps.isEmpty(), "G5: no map conversion without an authority code");
    }
}

void exportNative() {
    QTemporaryDir dir;
    auto model = nativeModel();
    addUnwritable(model.document, model.ids["wallA"], model.ids["wallB"]);
    const auto doc = withIdentity(model.document);
    const auto stableRevision = doc.revision();
    const auto w = exportTo(doc, dir.path(), "model.ifc");
    std::set<Id> unwritten{model.ids["empty"], model.ids["wires"]};
    for (const auto &[id, body] : doc.bodies())
        if (body->kind == BodyKind::Terrain || body->kind == BodyKind::ReferenceImage || (body->parent && doc.bodies().at(body->parent)->kind == BodyKind::Terrain))
            unwritten.insert(id);
    scanAgainstDocument(doc, w, unwritten);
    const auto facts = scan(w.path);
    const auto classes = facts["classes"].toObject();
    check(classes["IFCELEMENTASSEMBLY"].toInt() == 6 && classes["IFCDOOR"].toInt() == 1 && classes["IFCBUILDINGELEMENTPROXY"].toInt() == 14, "G1: classes: assemblies, the door and proxies");
    check(classes["IFCINDEXEDCOLOURMAP"].toInt() == 1 && classes["IFCMATERIAL"].toInt() == 1 && classes["IFCSURFACESTYLE"].toInt() == 1 && classes["IFCPRESENTATIONLAYERWITHSTYLE"].toInt() == 1, "G1: a colour map, one material, one layer");
    // The report: accounting, notices, fallbacks.
    const auto report = w.report;
    check(report["format"] == "IFC" && report["direction"] == "export" && report["apiVersion"].toInt() == 1, "report shape");
    check(report["target"].toObject()["viewDefinition"] == "none" && report["target"].toObject()["schema"] == "IFC4" && report["target"].toObject()["timestamp"] == "2026-01-02T03:04:05", "no view is claimed and the time is the injected one");
    check(report["target"].toObject()["sha256"].toString() == QString::fromLatin1(QCryptographicHash::hash(w.bytes, QCryptographicHash::Sha256).toHex()), "the report's digest is the file's");
    check(report["accounting"].toObject()["balanced"].toBool(), "G4: balanced");
    const auto fallbacks = report["classFallbacks"].toArray();
    check(fallbacks.size() == 1 && fallbacks[0].toObject()["requested"] == "IfcFoo" && fallbacks[0].toObject()["written"] == "IfcBuildingElementProxy", "the odd class is reported");
    check(QJsonDocument(report).toJson(QJsonDocument::Compact).size() <= 64 * 1024, "the report is within 64 KiB");
    for (const auto &n : report["notices"].toArray())
        check(n.toObject()["message"].toString().toUtf8().size() <= 256, "messages are within 256 bytes");
    // G3: the same file twice; a new time or name changes only the header and the owner history.
    const auto again = exportTo(doc, dir.path(), "again.ifc");
    check(QByteArray(again.bytes).replace("again.ifc", "model.ifc") == w.bytes, "G3: two exports of one document are byte for byte the same");
    const auto later = exportTo(doc, dir.path(), "model2.ifc", stamp + 86400);
    const auto first = QString::fromUtf8(w.bytes).split('\n'), second = QString::fromUtf8(later.bytes).split('\n');
    int different = 0;
    check(first.size() == second.size(), "G3: same number of lines");
    for (int i = 0; i < first.size(); ++i)
        different += first[i] != second[i] ? 1 : 0;
    check(different == 2, "G3: a new time and name change the header's FILE_NAME and the owner history only: " + std::to_string(different));
    check(w.report["file"].toObject()["classes"] == later.report["file"].toObject()["classes"], "same classes");
    // A file name with non-ASCII characters is written in the header with STEP escapes and the file still reads.
    {
        const auto unicode = exportTo(doc, dir.path(), QString::fromUtf8("gr\u00fc\u00dfe \u4e2d.ifc"));
        check(unicode.bytes.contains("FILE_NAME('gr\\S\\") || unicode.bytes.contains("\\X2\\"), "the header's file name is escaped: " + unicode.bytes.left(300).toStdString());
        checkFile(unicode.path);
    }
    // G6.
    golden(w, "native-model.ifc");
    // G2.
    const auto back = reimport(w.path);
    sameModel(doc, back, "native model");
    // The document is not touched.
    check(doc.revision() == stableRevision, "the document is untouched");

    // Destination rules.
    const auto hashBefore = QCryptographicHash::hash(read(w.path), QCryptographicHash::Sha256);
    try {
        exportIfcFile(doc, w.path, {}, workerOptions());
        check(false, "an existing file must not be replaced");
    } catch (const IfcExportError &error) {
        check(error.code() == "destinationExists", "destination exists: " + error.code());
    }
    check(QCryptographicHash::hash(read(w.path), QCryptographicHash::Sha256) == hashBefore, "the existing file is untouched");
    QFile::link(dir.filePath("nowhere"), dir.filePath("dangling.ifc"));
    try {
        exportIfcFile(doc, dir.filePath("dangling.ifc"), {}, workerOptions());
        check(false, "a symbolic link must not be followed");
    } catch (const IfcExportError &error) {
        check(error.code() == "destinationExists" && !QFile::exists(dir.filePath("nowhere")), "a dangling link is refused and not followed");
    }
    try {
        exportIfcFile(doc, dir.filePath("no/such/folder/x.ifc"), {}, workerOptions());
        check(false, "a missing folder");
    } catch (const IfcExportError &error) {
        check(error.code() == "destination", "missing folder: " + error.code());
    }
    {
        auto missing = workerOptions();
        missing.workerPath = dir.filePath("no-such-worker");
        try {
            exportIfcFile(doc, dir.filePath("w.ifc"), {}, missing);
            check(false, "no helper");
        } catch (const IfcExportError &error) {
            check(error.code() == "workerMissing" && !QFile::exists(dir.filePath("w.ifc")), "the helper is missing: nothing else is affected and nothing is written");
        }
    }
    // Cancel: nothing is published and the private directory is gone.
    {
        QTemporaryDir scratch;
        auto cancel = workerOptions();
        cancel.scratchRoot = scratch.path();
        cancel.cancelled = [] { return true; };
        try {
            exportIfcFile(doc, dir.filePath("c.ifc"), {}, cancel);
            check(false, "cancelled");
        } catch (const IfcExportError &error) {
            check(error.code() == "cancelled" && !QFile::exists(dir.filePath("c.ifc")), "cancel writes nothing");
        }
        check(QDir(scratch.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty(), "cancel removes the private directory");
        auto fine = workerOptions();
        fine.scratchRoot = scratch.path();
        QStringList phases;
        fine.progress = [&](const QString &phase, qint64) { phases << phase; };
        exportIfcFile(doc, dir.filePath("d.ifc"), {}, fine);
        check(phases == QStringList({"capture", "build", "check", "publish"}), "the four steps of the progress sheet: " + phases.join(',').toStdString());
        check(QDir(scratch.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty(), "the private directory is removed after an export");
    }
    // A model with nothing to write is refused, with the counts of what was left out.
    {
        Document wiresOnly;
        wiresOnly.addWire(0, {0, 0, 0}, {1, 0, 0});
        try {
            exportIfcFile(wiresOnly, dir.filePath("empty.ifc"), {}, workerOptions());
            check(false, "nothing to export");
        } catch (const IfcExportError &error) {
            check(error.code() == "emptyModel" && !QFile::exists(dir.filePath("empty.ifc")), "nothing to export is refused");
        }
    }
}

// G2 with the other importers' documents, and G4/G6 on their goldens.
void exportImported() {
    QTemporaryDir dir;
    // OBJ: a concave polygon (triangulated by the export, merged back by the import) and a painted, textured quad.
    {
        write(dir.filePath("scene.obj"), "mtllib paint.mtl\no Concave\ng Walls Exterior\nv 0 0 0\nv 2000 0 0\nv 2000 1000 0\nv 1000 1000 0\nv 1000 2000 0\nv 0 2000 0\nf 1 2 3 4 5 6\n"
                                           "o Painted\nusemtl Paint\nv 0 0 500\nv 2000 0 500\nv 2000 3000 500\nv 0 3000 500\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nf 7/1 8/2 9/3 10/4\n");
        write(dir.filePath("paint.mtl"), "newmtl Paint\nKd .25 .5 1\nd .4\nmap_Kd tile.png\n");
        write(dir.filePath("tile.png"), encodeTexturePng(TextureImage(1, 1, {255, 0, 0, 255})));
        const auto doc = withIdentity(loadObj(dir.filePath("scene.obj"), {.001, ObjUpAxis::Z}).document);
        const auto w = exportTo(doc, dir.path(), "obj.ifc");
        check(w.report["notWritten"].toObject()["textures"].toInt() == 1, "G4: the texture is counted: " + QJsonDocument(w.report["notWritten"].toObject()).toJson(QJsonDocument::Compact).toStdString());
        scanAgainstDocument(doc, w, {});
        sameModel(doc, reimport(w.path), "obj");
        golden(w, "obj-concave-paint.ifc");
    }
    // glTF: a rig with two uses of one mesh and one use of another (components and their placements).
    {
        QByteArray buffer;
        auto f32 = [&](float v) { const auto at = buffer.size(); buffer.resize(at + 4); qToLittleEndian<quint32>(std::bit_cast<quint32>(v), buffer.data() + at); };
        auto u16 = [&](quint16 v) { const auto at = buffer.size(); buffer.resize(at + 2); qToLittleEndian<quint16>(v, buffer.data() + at); };
        const float cube[8][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}};
        for (const auto &v : cube)
            for (const float c : v)
                f32(c);
        const quint16 indices[36] = {0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4, 1, 2, 6, 1, 6, 5, 2, 3, 7, 2, 7, 6, 3, 0, 4, 3, 4, 7};
        const auto indexStart = buffer.size();
        for (const auto i : indices)
            u16(i);
        const auto positionLength = indexStart;
        const auto tetraStart = buffer.size();
        for (const float v : {0.f, 0.f, 0.f, 2.f, 0.f, 0.f, 0.f, 2.f, 0.f, 0.f, 0.f, 2.f})
            f32(v);
        const auto tetraIndexStart = buffer.size();
        for (const quint16 i : {0, 2, 1, 0, 1, 3, 0, 3, 2, 1, 2, 3})
            u16(i);
        QJsonObject tree{{"asset", QJsonObject{{"version", "2.0"}}}};
        tree["buffers"] = QJsonArray{QJsonObject{{"byteLength", buffer.size()}, {"uri", "data:application/octet-stream;base64," + QString::fromLatin1(buffer.toBase64())}}};
        tree["bufferViews"] = QJsonArray{QJsonObject{{"buffer", 0}, {"byteOffset", 0}, {"byteLength", positionLength}}, QJsonObject{{"buffer", 0}, {"byteOffset", indexStart}, {"byteLength", 72}},
                                         QJsonObject{{"buffer", 0}, {"byteOffset", tetraStart}, {"byteLength", 48}}, QJsonObject{{"buffer", 0}, {"byteOffset", tetraIndexStart}, {"byteLength", 24}}};
        tree["accessors"] = QJsonArray{QJsonObject{{"bufferView", 0}, {"componentType", 5126}, {"type", "VEC3"}, {"count", 8}, {"min", QJsonArray{0, 0, 0}}, {"max", QJsonArray{1, 1, 1}}},
                                       QJsonObject{{"bufferView", 1}, {"componentType", 5123}, {"type", "SCALAR"}, {"count", 36}},
                                       QJsonObject{{"bufferView", 2}, {"componentType", 5126}, {"type", "VEC3"}, {"count", 4}, {"min", QJsonArray{0, 0, 0}}, {"max", QJsonArray{2, 2, 2}}},
                                       QJsonObject{{"bufferView", 3}, {"componentType", 5123}, {"type", "SCALAR"}, {"count", 12}}};
        tree["materials"] = QJsonArray{QJsonObject{{"name", "Blue"}, {"pbrMetallicRoughness", QJsonObject{{"baseColorFactor", QJsonArray{0.2, 0.4, 0.8, 0.5}}}}, {"alphaMode", "BLEND"}}};
        tree["meshes"] = QJsonArray{QJsonObject{{"name", "Cube"}, {"primitives", QJsonArray{QJsonObject{{"attributes", QJsonObject{{"POSITION", 0}}}, {"indices", 1}, {"material", 0}}}}},
                                    QJsonObject{{"name", "Tetra"}, {"primitives", QJsonArray{QJsonObject{{"attributes", QJsonObject{{"POSITION", 2}}}, {"indices", 3}}}}}};
        const double h = std::sqrt(0.5);
        tree["nodes"] = QJsonArray{QJsonObject{{"name", "Rig"}, {"translation", QJsonArray{5, 6, 7}}, {"rotation", QJsonArray{0, 0, h, h}}, {"children", QJsonArray{1, 2, 3}}},
                                   QJsonObject{{"name", "Left"}, {"mesh", 0}},
                                   QJsonObject{{"name", "Right"}, {"mesh", 0}, {"translation", QJsonArray{3, 0, 0}}, {"scale", QJsonArray{2, 2, 2}}},
                                   QJsonObject{{"name", "Solo"}, {"mesh", 1}, {"translation", QJsonArray{0, 4, 0}}}};
        tree["scenes"] = QJsonArray{QJsonObject{{"nodes", QJsonArray{0}}}};
        tree["scene"] = 0;
        write(dir.filePath("rig.gltf"), QJsonDocument(tree).toJson(QJsonDocument::Compact));
        const auto doc = withIdentity(loadGltf(dir.filePath("rig.gltf")).document);
        check(doc.instances().size() == 3 && doc.definitions().size() == 2, "the glTF fixture has three placements of two meshes: " + std::to_string(doc.instances().size()) + " placements, " + std::to_string(doc.definitions().size()) + " definitions, " + std::to_string(doc.bodies().size()) + " bodies");
        const auto w = exportTo(doc, dir.path(), "gltf.ifc");
        check(w.report["written"].toObject()["placements"].toInt() == 3 && w.report["accounting"].toObject()["writtenAsCopies"].toInt() == 3, "the placements are written as copies and counted");
        scanAgainstDocument(doc, w, {});
        sameModel(doc, reimport(w.path), "gltf");
        golden(w, "gltf-rig.ifc");
    }
    // DXF: wires only, so nothing can be written; with one body added the wires are counted and left out.
    {
        auto dxf = withIdentity(loadDxf(source("tests/fixtures/dxf/ezdxf-r2018.dxf"), {1}, 96).document);
        std::size_t wires = 0;
        for (const auto &[id, body] : dxf.bodies())
            wires += body->surface.wires.size();
        try {
            exportIfcFile(dxf, dir.filePath("dxf.ifc"), {}, workerOptions());
            check(false, "a DXF import has no faces");
        } catch (const IfcExportError &error) {
            check(error.code() == "emptyModel" && !QFile::exists(dir.filePath("dxf.ifc")), "G4: a model of wires only has nothing to export");
            check(error.report()["notWritten"].toObject()["looseEdges"].toInt() == int(wires), "G4: and says how many edges it left out: " + QJsonDocument(error.report()).toJson(QJsonDocument::Compact).toStdString());
        }
        Edit edit{"Block", {}};
        const Id next = dxf.nextId();
        edit.changes.push_back({next, nullptr, box(next, 0, "Block", {0, 0, 0}, {1, 2, 3})});
        edit.nextIdFloor = next + 1;
        dxf.apply(std::move(edit), dxf.revision());
        const auto w = exportTo(dxf, dir.path(), "dxf.ifc");
        check(w.report["notWritten"].toObject()["looseEdges"].toInt() == int(wires) && w.report["written"].toObject()["bodies"].toInt() >= 1, "G4: the edges are counted and the block is written");
    }
    // IFC: the certified architecture file, then the colours sample, then the road (georeferenced).
    for (const auto &[name, goldenName, ordered] : {std::tuple<const char *, const char *, bool>{"bsi-ifc4-arch.ifc", "bsi-ifc4-arch.ifc", false},
                                                    {"rv-tessellation-with-individual-colors.ifc", "rv-individual-colors.ifc", true},
                                                    {"bsi-ifc4-road.ifc", nullptr, false}}) {
        const auto imported = importIfcFile(fixture(name), {}, workerOptions());
        const auto doc = withIdentity(imported.document);
        const auto w = exportTo(doc, dir.path(), QString("ifc-") + name);
        const auto original = scan(fixture(name));
        const auto written = scan(w.path);
        checkFile(w.path);
        // The hierarchy of every exported object is the file's own.
        const auto op = original["parents"].toObject(), wp = written["parents"].toObject();
        int compared = 0;
        for (auto it = wp.begin(); it != wp.end(); ++it) {
            if (!op.contains(it.key()))
                continue;
            check(op[it.key()] == it.value(), std::string("the parent of ") + it.key().toStdString() + " in " + name);
            ++compared;
        }
        check(compared >= 2, std::string("hierarchy compared for ") + name);
        // Every property of the source's occurrences is in the export (type-level ones are added by the import).
        const auto op2 = original["occurrenceProperties"].toObject(), wp2 = written["occurrenceProperties"].toObject();
        for (auto it = op2.begin(); it != op2.end(); ++it) {
            if (!wp2.contains(it.key()))
                continue;
            const auto got = wp2[it.key()].toObject();
            const auto want = it.value().toObject();
            for (auto p = want.begin(); p != want.end(); ++p)
                check(got.contains(p.key()) && (p.value().isDouble() ? near(got[p.key()].toDouble(), p.value().toDouble(), 1e-9) : got[p.key()] == p.value()),
                      std::string("property ") + p.key().toStdString() + " of " + name);
        }
        sameModel(doc, reimport(w.path), name, {}, ordered);
        if (std::string(name) == "bsi-ifc4-road.ifc") {
            check(doc.site().has_value(), "the road has a site origin");
            const auto m = written["mapConversions"].toArray();
            check(m.size() == 1 && m[0].toObject()["eastings"].toDouble() == doc.site()->easting && m[0].toObject()["northings"].toDouble() == doc.site()->northing, "F1/G1: the road's map conversion comes back");
        }
        if (goldenName)
            golden(w, goldenName);
    }
    // Per-face colours come back face by face (areas by colour).
    {
        const auto doc = withIdentity(importIfcFile(fixture("rv-tessellation-with-individual-colors.ifc"), {}, workerOptions()).document);
        const auto w = exportTo(doc, dir.path(), "colors.ifc");
        const auto back = reimport(w.path);
        auto areasByColour = [](const Document &d) {
            std::map<std::array<float, 3>, double> out;
            for (const auto &[id, body] : d.bodies())
                if (geometric(*body))
                    for (const auto &[face, record] : body->surface.faces) {
                        const auto look = surfaceAppearance(d.materials(), *body, face);
                        out[look.color] += body->surface.area(face) * 1.0;
                    }
            return out;
        };
        const auto before = areasByColour(doc), after = areasByColour(back);
        check(before.size() == after.size() && before.size() >= 2, "per-face colours: as many colours come back: " + std::to_string(before.size()) + "/" + std::to_string(after.size()));
        for (const auto &[colour, area] : before)
            check(after.contains(colour) && near(after.at(colour), area, 1e-9), "per-face colours: the area of each colour");
    }
}

// Real documents of every format version: nothing is lost without being counted, and the export is deterministic.
void exportFixtureDocuments() {
    QTemporaryDir dir;
    for (const char *name : {"annotations-v21.sketchyup", "component-parameters-v28.sketchyup", "container-materials-v10.sketchyup", "container-components-v8.sketchyup",
                             "drawing-sheets-v27.sketchyup", "m4-complete-v11.sketchyup", "release-1.0.0-v26.sketchyup", "container-scene-v2.sketchyup", "instances-v25.sketchyup",
                             "container-guides-v5.sketchyup", "container-tags-v9.sketchyup", "container-groups-v6.sketchyup", "model-style-v17.sketchyup"}) {
        const auto doc = loadDocument(source(QString("tests/fixtures/") + name));
        IfcExportOptions options;
        options.timestamp = stamp;
        QJsonObject plan;
        try {
            plan = captureIfcExport(doc, options, "x.ifc").plan;
        } catch (const IfcExportError &error) {
            check(error.code() == "emptyModel", std::string(name) + ": only an empty model is refused: " + error.what());
            continue;
        }
        const auto accounting = plan["accounting"].toObject();
        check(accounting["balanced"].toBool() && accounting["items"].toInteger() == accounting["written"].toInteger() + accounting["writtenAsCopies"].toInteger() + accounting["notWritten"].toInteger(),
              std::string(name) + ": the accounting identity");
        const auto notWritten = plan["notWritten"].toObject();
        check(notWritten["annotations"].toInt() == int(doc.annotations().size()) && notWritten["scenes"].toInt() == int(doc.scenes().size()) && notWritten["sections"].toInt() == int(doc.sections().size()) &&
                  notWritten["sheets"].toInt() == int(doc.drawingSheets().size()),
              std::string(name) + ": annotations, scenes, sections and sheets are counted");
        const auto w = exportTo(doc, dir.path(), QString("fixture-") + name + ".ifc");
        checkFile(w.path);
        check(w.report["written"].toObject()["placements"].toInt() == int(doc.instances().size()) || w.report["notWritten"].toObject()["emptyGroups"].toInt() > 0,
              std::string(name) + ": every placement is written");
        const auto again = exportTo(doc, dir.path(), QString("fixture-again-") + name + ".ifc");
        check(QByteArray(again.bytes).replace(QByteArray("fixture-again-"), QByteArray("fixture-")) == w.bytes, std::string(name) + ": deterministic");
    }
}

// The command line: --export-ifc and its rules (ADR 0123 and 0115: unrelated options reject, nothing is replaced).
void commandLine() {
#ifdef CLI_PATH
    QTemporaryDir dir;
    auto model = nativeModel();
    addUnwritable(model.document, model.ids["wallA"], model.ids["wallB"]);
    const auto doc = withIdentity(model.document);
    const auto native = dir.filePath("model.sketchyup");
    createNativeFile(doc, native);
    auto cli = [&](const QStringList &arguments, bool ok = true, QProcessEnvironment environment = QProcessEnvironment::systemEnvironment()) {
        QProcess process;
        process.setProcessEnvironment(environment);
        process.start(QStringLiteral(CLI_PATH), arguments + QStringList{"--ifc-worker", workerPath()});
        check(process.waitForFinished(300000), "cli timed out");
        const auto out = process.readAllStandardOutput(), errors = process.readAllStandardError();
        check((process.exitCode() == 0) == ok, "cli " + arguments.join(' ').toStdString() + ": " + out.toStdString() + errors.toStdString());
        return QJsonDocument::fromJson(ok ? out : errors).object(); // a failure is reported on the error stream
    };
    const auto out = dir.filePath("export/model.ifc");
    QDir().mkpath(dir.filePath("export"));
    const auto result = cli({"--input", native, "--export-ifc", out, "--ifc-timestamp", "2026-01-02T03:04:05"});
    check(result["status"] == "exported" && result["exportReport"].toObject()["direction"] == "export", "CLI exports");
    check(read(out) == read(fixture("export/native-model.ifc")), "G3/G6: the command line's file is the golden file, byte for byte");
    // The same through SOURCE_DATE_EPOCH and a numeric option.
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("SOURCE_DATE_EPOCH", QString::number(stamp));
    QDir().mkpath(dir.filePath("e2"));
    cli({"--input", native, "--export-ifc", dir.filePath("e2/model.ifc")}, true, environment);
    check(read(dir.filePath("e2/model.ifc")) == read(out), "SOURCE_DATE_EPOCH gives the same file");
    QDir().mkpath(dir.filePath("e3"));
    cli({"--input", native, "--export-ifc", dir.filePath("e3/model.ifc"), "--ifc-timestamp", QString::number(stamp)});
    check(read(dir.filePath("e3/model.ifc")) == read(out), "a numeric timestamp gives the same file");
    environment.insert("SOURCE_DATE_EPOCH", "soon");
    cli({"--input", native, "--export-ifc", dir.filePath("e4.ifc")}, false, environment);
    check(!QFile::exists(dir.filePath("e4.ifc")), "a bad SOURCE_DATE_EPOCH is refused");
    // Rules.
    cli({"--input", native, "--export-ifc", out}, false);                                                // never replaces
    cli({"--export-ifc", dir.filePath("n.ifc")}, false);                                                 // needs --input
    cli({"--input", native, "--export-ifc", dir.filePath("n.ifc"), "--output", dir.filePath("x.sketchyup")}, false); // unrelated option
    cli({"--input", native, "--export-ifc", dir.filePath("n.ifc"), "--export-obj", dir.filePath("obj")}, false);
    cli({"--input", native, "--export-ifc", dir.filePath("n.ifc"), "--ifc-timestamp", "yesterday"}, false);
    cli({"--input", native, "--export-ifc", dir.filePath("n.ifc"), "--ifc-timestamp", "-5"}, false);
    cli({"--input", native, "--export-ifc", dir.filePath("n.ifc"), "--ifc-deadline", "0"}, false);
    cli({"--input", native, "--ifc-timestamp", "5"}, false);                                              // an IFC option alone
    cli({"--import-ifc", fixture("bsi-ifc4-arch.ifc"), "--ifc-timestamp", "5"}, false);                  // import has no timestamp
    cli({"--input", native, "--export-ifc", dir.filePath("n.ifc"), "extra"}, false);                     // named paths only
    check(!QFile::exists(dir.filePath("n.ifc")), "rejected commands publish nothing");
    // A document with nothing to write.
    {
        Document wiresOnly;
        wiresOnly.addWire(0, {0, 0, 0}, {1, 0, 0});
        createNativeFile(wiresOnly, dir.filePath("wires.sketchyup"));
        const auto failed = cli({"--input", dir.filePath("wires.sketchyup"), "--export-ifc", dir.filePath("w.ifc")}, false);
        check(failed["details"].toObject()["ifcCode"] == "emptyModel" && !QFile::exists(dir.filePath("w.ifc")), "CLI reports the empty model with its code: " + QJsonDocument(failed).toJson(QJsonDocument::Compact).toStdString());
    }
#endif
}



// Text the document holds but the helper would refuse (control characters, a line break in a name) is cleaned, not fatal.
void hostileText() {
    QTemporaryDir dir;
    Document doc;
    Edit edit{"Odd text", {}};
    auto body = box(1, 0, "Tab\there \x01 and a\nline break", {0, 0, 0}, {1, 1, 1});
    body->properties = {{"Set\x02.Name\x03", std::string("value with\x01 a control, a tab\t and a\nline break")}, {"Plain\x01", 1.0}};
    edit.changes.push_back({1, nullptr, body});
    edit.nextIdFloor = 2;
    // (The document takes this text today; if it ever refused it, the export would have nothing to clean and this must say so.)
    doc.apply(std::move(edit), doc.revision());
    const auto w = exportTo(withIdentity(doc), dir.path(), "odd.ifc");
    checkFile(w.path);
    const auto facts = scan(w.path);
    const auto guid = QString::fromStdString(derivedIfcGlobalId(identity, "body", 1));
    const auto properties = facts["occurrenceProperties"].toObject()[guid].toObject();
    check(properties.contains("Set .Name ") && properties["Set .Name "].toString() == "value with  a control, a tab\t and a\nline break",
          "control characters become spaces, a tab and a line break stay in a value: " + QJsonDocument(properties).toJson(QJsonDocument::Compact).toStdString());
    check(properties.contains("SketchyUp.Plain "), "control characters in a key become spaces");
    check(facts["objects"].toObject()[guid].toObject()["name"] == "Tab here   and a line break", "control characters in a name become spaces");
}

// A model shaped like an IFC import (spatial classes, a body of its own on a site and a space, a deprecated wall class, a site of
// sites, a building directly in the project): the structure is kept, the class is mapped and reported, nothing is lost.
void exportSpatialModel() {
    QTemporaryDir dir;
    Document doc;
    Edit edit{"IFC-shaped model", {}};
    std::map<std::string, std::string> guids;
    auto tag = [&](const std::shared_ptr<Body> &b, const char *cls, double step, const char *key, bool withGuid = true) {
        b->properties["ifc.Class"] = std::string(cls);
        b->properties["ifc.Step"] = step;
        if (withGuid) {
            guids[key] = derivedIfcGlobalId("elsewhere", key, std::uint64_t(step));
            b->properties["ifc.GlobalId"] = guids[key];
        }
        edit.changes.push_back({b->id, nullptr, b});
        return b;
    };
    tag(group(1, 0, "Project"), "IfcProject", 1, "project")->properties["ifc.LongName"] = std::string("Demo project");
    tag(group(2, 1, "Site"), "IfcSite", 10, "site");
    tag(box(3, 2, "Site", {0, 0, -1}, {50, 50, 1}), "IfcSite", 10, "siteBody", false); // the site's own body (folded into the site)
    tag(group(4, 2, "House"), "IfcBuilding", 11, "house")->properties["ifc.LongName"] = std::string("House long name");
    tag(group(5, 4, "Level 0"), "IfcBuildingStorey", 12, "level0")->properties["ifc.Elevation"] = 0.0;
    {
        auto wall = tag(box(6, 5, "Wall", {0, 0, 0}, {4, 0.2, 2.5}), "IfcWallStandardCase", 13, "wall");
        wall->properties["ifc.PredefinedType"] = std::string("STANDARD");
        wall->properties["Pset_WallCommon.FireRating"] = std::string("REI60");
    }
    tag(group(7, 5, "Room"), "IfcSpace", 14, "room");
    tag(box(8, 7, "Room", {0.2, 0.2, 0}, {3, 3, 2.4}), "IfcSpace", 14, "roomBody", false); // the space's own body
    tag(box(9, 7, "Sofa", {1, 1, 0}, {1.5, 0.8, 0.8}), "IfcFurniture", 15, "sofa");
    tag(group(10, 4, "Level 1"), "IfcBuildingStorey", 16, "level1")->properties["ifc.Elevation"] = 3.0; // nothing in it: not written
    tag(group(12, 2, "Partial site"), "IfcSite", 17, "partial");
    tag(box(13, 12, "Pavement", {60, 0, 0}, {5, 5, 0.1}), "IfcSlab", 18, "pavement");
    tag(group(14, 1, "Direct building"), "IfcBuilding", 19, "direct");
    tag(box(15, 14, "Shed", {80, 0, 0}, {2, 2, 2}), "IfcBuildingElementProxy", 20, "shed");
    {
        auto stray = box(16, 0, "Stray", {-10, 0, 0}, {1, 1, 1});
        edit.changes.push_back({16, nullptr, stray});
    }
    edit.nextIdFloor = 17;
    doc.apply(std::move(edit), doc.revision());
    const auto stable = withIdentity(doc);
    const auto w = exportTo(stable, dir.path(), "structure.ifc");
    const auto facts = scan(w.path);
    checkFile(w.path);
    const auto classes = facts["classes"].toObject();
    check(classes["IFCSITE"].toInt() == 2 && classes["IFCBUILDING"].toInt() == 2 && classes["IFCBUILDINGSTOREY"].toInt() == 1 && classes["IFCSPACE"].toInt() == 1 &&
              classes["IFCWALL"].toInt() == 1 && classes["IFCWALLSTANDARDCASE"].toInt() == 0 && classes["IFCFURNITURE"].toInt() == 1 && classes["IFCSLAB"].toInt() == 1 &&
              classes["IFCBUILDINGELEMENTPROXY"].toInt() == 2 && classes["IFCELEMENTASSEMBLY"].toInt() == 0,
          "the classes: " + QJsonDocument(classes).toJson(QJsonDocument::Compact).toStdString());
    const auto parents = facts["parents"].toObject();
    auto guidOf = [&](const char *key) { return QString::fromStdString(guids.at(key)); };
    auto parent = [&](const QString &guid) { return parents[guid].toString(); };
    check(parent(guidOf("site")) == guidOf("project") && parent(guidOf("partial")) == guidOf("site") && parent(guidOf("house")) == guidOf("site") &&
              parent(guidOf("direct")) == guidOf("project"),
          "the spatial tree: sites in the project, a site in a site, a building in a site and one directly in the project");
    check(parent(guidOf("level0")) == guidOf("house") && parent(guidOf("wall")) == guidOf("level0") && parent(guidOf("room")) == guidOf("level0") && parent(guidOf("sofa")) == guidOf("room"),
          "storey, wall, space and the sofa contained in the space");
    check(parent(guidOf("pavement")) == guidOf("partial") && parent(guidOf("shed")) == guidOf("direct"), "elements in the nearest spatial ancestor");
    check(!parents.contains(guidOf("level1")), "an empty storey is not written");
    {
        // The stray body takes the first building's place; the project's identifier and all kept identifiers are in the file.
        QString stray;
        for (auto it = parents.begin(); it != parents.end(); ++it)
            if (facts["objects"].toObject()[it.key()].toObject()["name"] == "Stray")
                stray = it.value().toString();
        check(stray == guidOf("house"), "a body outside the spatial structure goes into the first building");
        for (const auto &[key, guid] : guids)
            if (key != "siteBody" && key != "roomBody" && key != "level1")
                check(facts["objects"].toObject().contains(QString::fromStdString(guid)), "the imported GlobalId of " + key + " is kept");
    }
    const auto text = QString::fromUtf8(w.bytes);
    check(text.contains(QRegularExpression("IFCSITE\\('" + QRegularExpression::escape(guidOf("site")) + "',#\\d+,'Site',\\$,\\$,#\\d+,#\\d+,\\$,\\.COMPLEX\\.,")), "the site keeps its body and is COMPLEX: composed of sites");
    check(text.contains(QRegularExpression("'Partial site',[^;]*\\.PARTIAL\\.")), "the part of a site is PARTIAL");
    check(text.contains(QRegularExpression("IFCBUILDING\\('" + QRegularExpression::escape(guidOf("house")) + "',#\\d+,'House',\\$,\\$,#\\d+,\\$,'House long name',\\.ELEMENT\\.")), "the building's long name");
    check(text.contains(QRegularExpression("IFCBUILDINGSTOREY\\('" + QRegularExpression::escape(guidOf("level0")) + "'[^;]*\\.ELEMENT\\.,0\\.\\)")), "the storey's elevation");
    check(text.contains(QRegularExpression("IFCPROJECT\\('" + QRegularExpression::escape(guidOf("project")) + "',#\\d+,'Project',\\$,\\$,'Demo project'")), "the project's long name");
    const auto fallbacks = w.report["classFallbacks"].toArray();
    check(fallbacks.size() == 1 && fallbacks[0].toObject()["requested"] == "IfcWallStandardCase" && fallbacks[0].toObject()["written"] == "IfcWall" && fallbacks[0].toObject()["reason"] == "standardCase",
          "the deprecated standard-case class is written as the class it refines, and reported");
    check(facts["occurrenceProperties"].toObject()[guidOf("wall")].toObject()["Pset_WallCommon.FireRating"] == "REI60", "the wall's property");
    check(w.report["accounting"].toObject()["balanced"].toBool() && w.report["notWritten"].toObject()["emptyGroups"].toInt() == 1, "the empty storey is counted");
    check(w.report["globalIds"].toObject()["kept"].toInt() == 11 && w.report["globalIds"].toObject()["replaced"].toInt() == 0, "eleven identifiers are kept: " + QJsonDocument(w.report["globalIds"].toObject()).toJson(QJsonDocument::Compact).toStdString());
    sameModel(stable, reimport(w.path), "structure", {}, false);
    golden(w, "ifc-structure.ifc");
}

// The process runner against helpers that misbehave (scripts that stand in for sketchyup-ifc-worker): deadline, cancel, a flood, a
// helper that ignores SIGTERM, a crash, hostile words, garbage and false claims. Nothing is published and the private directory is
// removed each time.
void runner() {
    QTemporaryDir dir, scratchRoot;
    Document doc;
    {
        Edit edit{"Box", {}};
        edit.changes.push_back({1, nullptr, box(1, 0, "Box", {0, 0, 0}, {1, 1, 1})});
        edit.nextIdFloor = 2;
        doc.apply(std::move(edit), doc.revision());
    }
    const QString real = workerPath();
    int number = 0;
    const auto script = [&](const QByteArray &body) {
        const auto path = dir.filePath(QString("stub%1.sh").arg(++number));
        QFile file(path);
        check(file.open(QIODevice::WriteOnly) && file.write("#!/bin/sh\nREAL='" + real.toUtf8() + "'\n" + body) > 0, "write a stand-in helper");
        file.close();
        QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        return path;
    };
    const auto options = [&](const QString &worker, int deadline = 60) {
        auto o = workerOptions();
        o.workerPath = worker;
        o.deadlineSeconds = deadline;
        o.scratchRoot = scratchRoot.path();
        return o;
    };
    const auto fails = [&](const QString &worker, const char *expected, const char *label, int deadline = 60, std::function<bool()> cancelled = {}, int maxMs = 20000) {
        const auto target = dir.filePath(QString("out%1.ifc").arg(number));
        auto o = options(worker, deadline);
        o.cancelled = cancelled;
        QElapsedTimer clock;
        clock.start();
        try {
            exportIfcFile(doc, target, {}, o);
            check(false, std::string(label) + ": must fail");
        } catch (const IfcExportError &error) {
            check(error.code() == expected, std::string(label) + ": code " + error.code() + " instead of " + expected + " (" + error.what() + ")");
            check(clock.elapsed() < maxMs, std::string(label) + ": took " + std::to_string(clock.elapsed()) + " ms");
            check(QString(error.what()).toUtf8().size() < 1000, std::string(label) + ": the message is short");
            for (const char c : std::string(error.what()))
                check(static_cast<unsigned char>(c) >= 0x20 || c == '\n', std::string(label) + ": no control character in the message");
            check(error.report().isEmpty() || QJsonDocument(error.report()).toJson(QJsonDocument::Compact).size() <= 64 * 1024, std::string(label) + ": the report is bounded");
        }
        check(!QFile::exists(target), std::string(label) + ": nothing is published");
        check(QDir(scratchRoot.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty(), std::string(label) + ": the private directory is removed");
    };
    fails(script("sleep 30\n"), "workerFailed", "deadline", 1, {}, 8000);
    {
        auto calls = std::make_shared<int>(0);
        fails(script("sleep 30\n"), "cancelled", "cancel while the helper runs", 60, [calls] { return ++*calls >= 3; }, 8000);
    }
    fails(script("echo 'P phase read'; head -c 4000000 /dev/zero | tr '\\000' x; sleep 30\n"), "workerFailed", "a flood without a line end", 60, {}, 8000);
    fails(script("trap '' TERM; while :; do :; done\n"), "workerFailed", "a helper that ignores SIGTERM", 1, {}, 8000);
    fails(script("kill -SEGV $$\n"), "workerFailed", "a crash");
    {
        // The log tail in the report names no host path.
        const auto worker = script("echo \"cannot open /home/someone/secret/model.ifc and $0\" >&2; kill -SEGV $$\n");
        try {
            exportIfcFile(doc, dir.filePath("paths.ifc"), {}, options(worker));
            check(false, "must fail");
        } catch (const IfcExportError &error) {
            const auto tail = error.report()["logTail"].toString();
            check(tail.contains("cannot open <path> and <path>") && !tail.contains("/home") && !tail.contains(dir.path()), "the log tail holds no host path: " + tail.toStdString());
        }
    }
    fails(script("printf 'R {\"status\":\"refused\",\"code\":\"a b\\\\nc<script>\",\"message\":\"%s\"}\\n' \"$(head -c 30000 /dev/zero | tr '\\000' y)\"; exit 3\n"), "workerRefused", "hostile words");
    fails(script("echo garbage > \"$4\"; echo 'R {\"status\":\"ok\"}'\n"), "workerOutput", "a file that is not STEP");
    fails(script("echo 'R {\"status\":\"ok\"}'\n"), "workerOutput", "no file at all");
    fails(script("\"$REAL\" \"$@\" | sed 's/\"triangles\":[0-9]*/\"triangles\":1/'; exit ${PIPESTATUS:-0}\n"), "workerMismatch", "a false claim about the triangles");
    fails(script("if [ \"$1\" = --check ]; then echo 'R {\"status\":\"invalid\",\"entities\":1,\"issueCount\":1,\"issues\":[\"#1 IfcFoo: broken\"]}'; exit 3; fi\nexec \"$REAL\" \"$@\"\n"), "workerOutput", "a file that fails the schema check");
    // The check phase itself failing is the helper failing: a crash, a hang, a flood, and a cancel while it runs.
    const QByteArray checkOnly = "if [ \"$1\" = --check ]; then ";
    fails(script(checkOnly + "kill -SEGV $$; fi\nexec \"$REAL\" \"$@\"\n"), "workerFailed", "a crash while checking");
    fails(script(checkOnly + "sleep 30; fi\nexec \"$REAL\" \"$@\"\n"), "workerFailed", "a hang while checking", 1, {}, 12000);
    fails(script(checkOnly + "head -c 4000000 /dev/zero | tr '\\000' x; sleep 30; fi\nexec \"$REAL\" \"$@\"\n"), "workerFailed", "a flood while checking", 60, {}, 12000);
    {
        // The first call is after the capture, the next ones are in the loop of the export (a real run, quick) and then of the check.
        auto calls = std::make_shared<int>(0);
        fails(script(checkOnly + "sleep 30; fi\nexec \"$REAL\" \"$@\"\n"), "cancelled", "cancel while checking", 60,
              [calls] { return ++*calls >= 40; }, 12000);
    }
    // And a good run through a stand-in that only delegates, to show the stand-ins are not what failed above.
    {
        const auto target = dir.filePath("delegated.ifc");
        exportIfcFile(doc, target, {}, options(script("exec \"$REAL\" \"$@\"\n")));
        check(QFile::exists(target), "a delegating stand-in works");
    }
}

// ADR 0171 section 6.4: "Export of the 100,000-face model <= 5 s" is measured on request (IFC_EXPORT_BUDGET=1).
std::shared_ptr<Body> icosahedron(Id id, Vec3 centre, double radius) {
    auto body = std::make_shared<Body>();
    body->id = id;
    body->name = "Ico " + std::to_string(id);
    const double t = (1 + std::sqrt(5.0)) / 2;
    const Vec3 raw[12] = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t}, {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    const int faces[20][3] = {{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
                              {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};
    Vec3 points[12];
    for (int i = 0; i < 12; ++i) {
        const double l = length(raw[i]);
        points[i] = centre + raw[i] * (radius / l);
    }
    for (const auto &f : faces)
        body->surface.addFace({{points[f[0]], points[f[1]], points[f[2]]}});
    body->topology = Topology::rebuild(body->surface, {});
    return body;
}

// ADR 0171 section 6.4: "Export of the 100,000-face model <= 5 s" is measured on request (IFC_EXPORT_BUDGET=1): 5,000 icosahedra
// are 100,000 faces and 60,000 vertices that do not merge, the model the import budget uses.
void budget() {
    if (!qEnvironmentVariableIsSet("IFC_EXPORT_BUDGET"))
        return;
    Document doc;
    Edit edit{"Many icosahedra", {}};
    Id next = 1;
    for (int i = 0; i < 5000; ++i, ++next)
        edit.changes.push_back({next, nullptr, icosahedron(next, {double(i % 100) * 3, double(i / 100) * 3, 0}, 1)});
    edit.nextIdFloor = next;
    doc.apply(std::move(edit), doc.revision());
    QTemporaryDir dir;
    QElapsedTimer clock;
    clock.start();
    const auto stable = withIdentity(doc);
    std::cout << "  budget: prepared in " << clock.elapsed() << " ms\n";
    clock.restart();
    IfcExportOptions options;
    options.timestamp = stamp;
    const auto captured = captureIfcExport(stable, options, "big.ifc");
    std::cout << "  budget: capture " << clock.elapsed() << " ms, package " << captured.package.size() << " bytes\n";
    clock.restart();
    const auto w = exportTo(stable, dir.path(), "big.ifc");
    std::cout << "  budget: " << w.report["written"].toObject()["bodies"].toInt() << " bodies, " << w.report["written"].toObject()["faces"].toInt() << " faces, "
              << w.report["written"].toObject()["triangles"].toInt() << " triangles exported in " << clock.elapsed() << " ms (capture, helper, checks, publication), " << w.bytes.size() << " bytes\n";
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (!QFileInfo(workerPath()).isExecutable()) {
        std::cout << "skipped: the IFC helper is not built\n";
        return 77;
    }
    try {
        for (const auto &[name, run] : std::initializer_list<std::pair<const char *, void (*)()>>{{"package and plan", packageAndPlan},
                                                                                              {"native model", exportNative},
                                                                                              {"ifc-shaped model", exportSpatialModel},
                                                                                              {"hostile text", hostileText},
                                                                                              {"helper that misbehaves", runner},
                                                                                              {"imported documents", exportImported},
                                                                                              {"fixture documents", exportFixtureDocuments},
                                                                                              {"command line", commandLine},
                                                                                              {"budget", budget}}) {
            QElapsedTimer clock;
            clock.start();
            run();
            std::cout << name << ": ok (" << clock.elapsed() << " ms)\n";
        }
    } catch (const std::exception &error) {
        std::cerr << "FAILED: " << error.what() << "\n";
        return 1;
    }
    std::cout << "ifc_export_tests: ok\n";
    return 0;
}
