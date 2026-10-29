// R098.c (ADR 0172 section 7: E3, E5 and E8, and section 2's refusals): the STEP export through the helper, checked by the STEP
// import (R098.b) on the files it writes. E5: a round trip, for AP214 and for AP203, of a group holding one component instanced
// three times at different placements, named bodies, a face of its own colour, a body on a tag and a hidden body keeps the
// geometry (volume and bounding box), the names, the colours, the layer, the hidden state and one definition with three
// instances. E3: the file declares millimetres, and a 1 m cube (1000 mm) re-imports as 1 m. The refusals: AP242, an empty
// model, an existing destination. E8: the messages and the report stay within their bounds.
// Needs sketchyup-ifc-worker (IFC_WORKER_PATH); exits 77 (skipped) when the helper is not built.
#include "core/component_records.hpp"
#include "core/material_records.hpp"
#include "core/tag_records.hpp"
#include "geometry/surface.hpp"
#include "io/document_io.hpp"
#include "io/ifc_worker.hpp"
#include "io/native_limits.hpp"
#include "io/step_export.hpp"
#include "io/step_worker.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <numbers>
#include <set>
#include <string>
#include <sys/resource.h>

using namespace sketchy;
namespace {

void check(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}

bool near(double a, double b, double tolerance) { return std::abs(a - b) <= tolerance; }

QString helperPath() { return QStringLiteral(IFC_WORKER_PATH); }

IfcWorkerOptions exportOptions() {
    IfcWorkerOptions options;
    options.workerPath = helperPath();
    options.threads = 1;
    options.deadlineSeconds = 120;
#ifdef IFC_NO_ADDRESS_LIMIT
    options.addressSpaceBytes = 0;
#endif
    return options;
}

StepWorkerOptions importOptions() {
    StepWorkerOptions options;
    options.workerPath = helperPath();
    options.deadlineSeconds = 120;
#ifdef IFC_NO_ADDRESS_LIMIT
    options.addressSpaceBytes = 0;
#endif
    return options;
}

// A box of `size` at `origin`: one face extruded (as the IFC export test builds its boxes).
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

std::shared_ptr<Body> group(Id id, Id parent, const std::string &name, Transform transform = {}) {
    auto body = std::make_shared<Body>();
    body->id = id;
    body->parent = parent;
    body->name = name;
    body->kind = BodyKind::Group;
    body->transform = transform;
    return body;
}

// The model of E5, in metres (the native unit). Ids: 1 the group "Assembly", 2 the component definition's root "Bolt" (also
// its definition), 3 its member "Shank" (10 x 20 x 30 mm), the instances (the placed copies) from 10; top-level bodies "Slab",
// "Painted", "Tagged" and "Hidden slab" are 1 m cubes at x = 0, 2, 4 and 6.
Document roundTripModel() {
    Document doc;
    Edit edit{"Round-trip model", {}};
    {
        auto tag = std::make_shared<TagRecord>();
        tag->id = 1;
        tag->name = "Walls";
        edit.tags.push_back({1, nullptr, tag});
        edit.nextTagFloor = 2;
    }
    edit.changes.push_back({1, nullptr, group(1, 0, "Assembly")});
    auto definition = std::make_shared<ComponentDefinition>();
    definition->id = 1;
    definition->root = 2;
    definition->name = "Bolt";
    auto root = group(2, 0, "Bolt");
    auto member = box(3, 2, "Shank", {0, 0, 0}, {0.010, 0.020, 0.030});
    definition->members[2] = root;
    definition->members[3] = member;
    definition->nextMemberId = 4;
    edit.definitions.push_back({1, nullptr, definition});
    edit.nextDefinitionFloor = 2;
    Id next = 10;
    const Transform placements[3] = {Transform::translation({10, 0, 0}), Transform::translation({10, 5, 0}),
                                     Transform::translation({10, 0, 5})};
    for (const Transform &placement : placements) {
        auto instance = std::make_shared<ComponentInstance>();
        instance->definition = 1;
        auto placedRoot = std::make_shared<Body>(*root);
        placedRoot->id = next++;
        placedRoot->parent = 1;
        placedRoot->transform = placement;
        auto placedMember = std::make_shared<Body>(*member);
        placedMember->id = next++;
        placedMember->parent = placedRoot->id;
        instance->members[2] = placedRoot->id;
        instance->members[3] = placedMember->id;
        edit.changes.push_back({placedRoot->id, nullptr, placedRoot});
        edit.changes.push_back({placedMember->id, nullptr, placedMember});
        edit.instances.push_back({placedRoot->id, nullptr, instance});
    }
    edit.nextIdFloor = 20;

    auto slab = box(20, 0, "Slab", {0, 0, 0}, {1, 1, 1});
    slab->color = {0.2f, 0.6f, 0.3f};
    edit.changes.push_back({20, nullptr, slab});
    auto painted = box(21, 0, "Painted", {2, 0, 0}, {1, 1, 1});
    painted->faceColors[painted->surface.faces.begin()->first] = {1.0f, 0.0f, 0.0f};
    edit.changes.push_back({21, nullptr, painted});
    auto tagged = box(22, 0, "Tagged", {4, 0, 0}, {1, 1, 1});
    tagged->tag = 1;
    edit.changes.push_back({22, nullptr, tagged});
    auto hidden = box(23, 0, "Hidden slab", {6, 0, 0}, {1, 1, 1});
    hidden->hidden = true;
    edit.changes.push_back({23, nullptr, hidden});
    doc.apply(std::move(edit), doc.revision());
    return doc;
}

std::vector<Triangle> worldTriangles(const Document &doc, Id id) { return doc.worldTriangles(id); }

double volumeOf(const Document &doc, Id id) {
    double volume = 0;
    for (const auto &t : worldTriangles(doc, id))
        volume += dot(t.a, cross(t.b, t.c)) / 6;
    return volume;
}

struct Bounds {
    Vec3 low{1e300, 1e300, 1e300}, high{-1e300, -1e300, -1e300};
};

Bounds boundsOf(const Document &doc, Id id) {
    Bounds out;
    for (const auto &t : worldTriangles(doc, id))
        for (const Vec3 p : {t.a, t.b, t.c}) {
            out.low = {std::min(out.low.x, p.x), std::min(out.low.y, p.y), std::min(out.low.z, p.z)};
            out.high = {std::max(out.high.x, p.x), std::max(out.high.y, p.y), std::max(out.high.z, p.z)};
        }
    return out;
}

// The geometric bodies of a document with the given name.
std::vector<Id> bodiesNamed(const Document &doc, const std::string &name) {
    std::vector<Id> out;
    for (const auto &[id, body] : doc.bodies())
        if (body->name == name && body->kind == BodyKind::Geometry && !body->surface.faces.empty())
            out.push_back(id);
    return out;
}

bool hiddenBody(const Document &doc, Id id) {
    const Body &b = *doc.bodies().at(id);
    return b.hidden || (b.tag && !tagVisible(doc.tags(), b.tag));
}

std::string layerOf(const Body &b) {
    if (const auto found = b.properties.find("step.Layer"); found != b.properties.end())
        if (const auto *text = std::get_if<std::string>(&found->second))
            return *text;
    return {};
}

bool colourNear(const std::array<float, 3> &a, const std::array<float, 3> &b) {
    return near(a[0], b[0], 1e-3) && near(a[1], b[1], 1e-3) && near(a[2], b[2], 1e-3);
}

QByteArray read(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "read " + path.toStdString());
    return file.readAll();
}

// E5 for one schema: the round trip of `doc` through the export and the import.
void roundTrip(const Document &doc, const char *schema, const QString &directory) {
    const QString path = QDir(directory).filePath(QStringLiteral("roundtrip-") + schema + ".step");
    StepExportOptions options;
    options.schema = QString::fromLatin1(schema);
    const auto exported = exportStepFile(doc, path, options, exportOptions());
    check(exported.bytes > 0 && QFileInfo(path).size() == exported.bytes, std::string("export ") + schema + ": the file is written");
    const QByteArray text = read(path);
    check(text.startsWith("ISO-10303-21;") && text.contains("END-ISO-10303-21;"), std::string("export ") + schema + ": a STEP file");
    check(text.contains(schema == std::string("ap203") ? "CONFIG_CONTROL_DESIGN" : "AUTOMOTIVE_DESIGN"), std::string("export ") + schema + ": the schema");
    check(text.contains(".MILLI."), std::string("export ") + schema + ": E3, the file declares millimetres");
    check(exported.report["target"].toObject()["units"].toString() == "millimetre", std::string("export ") + schema + ": the report says millimetres");

    const auto imported = importStepFile(path, importOptions());
    const Document &re = imported.document;
    const std::string label = std::string("round trip ") + schema;

    // E7 (ADR 0172 section 7): the imported document validates as a whole (a save and reopen is bit-exact),
    // one undo removes the whole import, and every geometry body carries the file's own provenance: the
    // step.Source digest is the digest the report names for this file.
    const QByteArray saved = encodeDocument(re);
    check(encodeDocument(decodeDocument(saved)) == saved, label + ": E7, the import validates and reopens bit-exact");
    Document undone = re;
    undone.undo();
    check(undone.bodies().empty(), label + ": E7, one undo removes the whole import");
    const std::string digest = imported.report["source"].toObject()["sha256"].toString().toStdString();
    int geometryBodies = 0, provenanceBodies = 0;
    for (const auto &[id, body] : re.bodies()) {
        if (body->surface.faces.empty())
            continue;
        ++geometryBodies;
        const auto source = body->properties.find("step.Source");
        if (source != body->properties.end() && std::get_if<std::string>(&source->second) &&
            *std::get_if<std::string>(&source->second) == digest)
            ++provenanceBodies;
    }
    check(geometryBodies > 0 && provenanceBodies == geometryBodies,
          label + ": E7, every geometry body names the file's digest (" + std::to_string(provenanceBodies) + " of " +
              std::to_string(geometryBodies) + ")");

    // Geometry and names of the top-level bodies: a 1000 mm cube keeps its volume and bounds (E3: it re-imports as 1 m).
    for (const char *name : {"Slab", "Painted", "Tagged", "Hidden slab"}) {
        const auto original = bodiesNamed(doc, name);
        check(original.size() == 1, label + ": the original " + name);
        const auto found = bodiesNamed(re, name);
        check(found.size() == 1, label + ": the name " + name + " survives");
        check(near(volumeOf(re, found[0]), 1.0, 1e-6), label + ": the volume of " + name + " is " + std::to_string(volumeOf(re, found[0])));
        const Bounds a = boundsOf(doc, original[0]), b = boundsOf(re, found[0]);
        for (int k = 0; k < 3; ++k) {
            const double lo = k == 0 ? a.low.x : k == 1 ? a.low.y : a.low.z;
            const double hi = k == 0 ? a.high.x : k == 1 ? a.high.y : a.high.z;
            const double rlo = k == 0 ? b.low.x : k == 1 ? b.low.y : b.low.z;
            const double rhi = k == 0 ? b.high.x : k == 1 ? b.high.y : b.high.z;
            check(near(lo, rlo, 1e-6) && near(hi, rhi, 1e-6), label + ": the bounds of " + name);
        }
    }
    check(near(boundsOf(re, bodiesNamed(re, "Slab")[0]).high.x - boundsOf(re, bodiesNamed(re, "Slab")[0]).low.x, 1.0, 1e-6),
          label + ": E3, a 1 m cube (1000 mm in the file) is 1 m after the round trip");

    // Colours: the cube's colour on every face; the painted face's own colour.
    {
        const Body &slab = *re.bodies().at(bodiesNamed(re, "Slab")[0]);
        check(!slab.faceColors.empty(), label + ": the slab has its colour");
        for (const auto &[face, colour] : slab.faceColors)
            check(colourNear(colour, {0.2f, 0.6f, 0.3f}), label + ": the slab's colour");
        const Body &painted = *re.bodies().at(bodiesNamed(re, "Painted")[0]);
        bool red = false;
        for (const auto &[face, colour] : painted.faceColors)
            red = red || colourNear(colour, {1.0f, 0.0f, 0.0f});
        check(red, label + ": the painted face keeps its colour");
    }
    // The layer and the hidden state.
    {
        const Id tagged = bodiesNamed(re, "Tagged")[0];
        check(layerOf(*re.bodies().at(tagged)) == "Walls", label + ": the tag becomes the layer step.Layer");
        check(hiddenBody(re, bodiesNamed(re, "Hidden slab")[0]), label + ": the hidden body stays hidden");
        check(!hiddenBody(re, tagged), label + ": a visible body stays visible");
    }
    // The assembly and the instances: one definition of the part with three instances, at the three placements.
    {
        check(re.definitions().size() == 1, label + ": one component definition (got " + std::to_string(re.definitions().size()) + ")");
        check(re.instances().size() == 3, label + ": three instances of it (got " + std::to_string(re.instances().size()) + ")");
        std::set<std::array<long long, 3>> placements;
        std::size_t parts = 0;
        for (const auto &[root, instance] : re.instances()) {
            const Vec3 at = re.worldTransform(root).point({0, 0, 0});
            placements.insert({std::llround(at.x), std::llround(at.y), std::llround(at.z)});
            for (const auto &[member, scene] : instance->members) {
                if (!re.bodies().at(scene)->surface.faces.empty()) {
                    ++parts;
                    check(near(volumeOf(re, scene), 6e-6, 1e-9), label + ": the instance's volume is the part's (10 x 20 x 30 mm)");
                }
            }
        }
        check(placements.size() == 3, label + ": the three instances are at three placements");
        check(parts == 3, label + ": each instance holds the part's geometry");
        bool assembly = false;
        for (const auto &[id, body] : re.bodies())
            assembly = assembly || (body->kind == BodyKind::Group && body->name == "Assembly");
        check(assembly, label + ": the group Assembly survives");
    }
}

// The refusals of section 2 and E8's bounds.
void refusals(const Document &doc, const QString &directory) {
    // AP242 is refused in the application, before the helper runs.
    {
        StepExportOptions options;
        options.schema = "ap242";
        bool refused = false;
        try {
            exportStepFile(doc, QDir(directory).filePath("refused-ap242.step"), options, exportOptions());
        } catch (const StepExportError &error) {
            refused = error.code() == "schema";
            check(std::string(error.what()).size() <= 256, "E8: the AP242 message is at most 256 bytes");
        }
        check(refused, "refusal: AP242 is refused with the schema code");
        check(!QFileInfo(QDir(directory).filePath("refused-ap242.step")).exists(), "refusal: AP242 writes nothing");
    }
    // The helper refuses AP242 by its own check too (the application's test is not the only one).
    {
        QProcess process;
        process.start(helperPath(), {"--export-step", "/nonexistent-package", "--output", QDir(directory).filePath("helper-ap242.step"), "--step-schema", "ap242"});
        check(process.waitForFinished(60000), "helper: the AP242 refusal ran");
        const QByteArray line = process.readAllStandardOutput();
        check(line.contains("\"code\":\"schema\"") && process.exitCode() == 3, "helper: AP242 is refused with exit 3");
    }
    // An empty model: a group with nothing in it.
    {
        Document empty;
        Edit edit{"Empty", {}};
        edit.changes.push_back({1, nullptr, group(1, 0, "Nothing")});
        edit.nextIdFloor = 2;
        empty.apply(std::move(edit), empty.revision());
        bool refused = false;
        try {
            StepExportOptions options;
            exportStepFile(empty, QDir(directory).filePath("empty.step"), options, exportOptions());
        } catch (const StepExportError &error) {
            refused = error.code() == "emptyModel";
        }
        check(refused, "refusal: an empty model is refused");
        check(!QFileInfo(QDir(directory).filePath("empty.step")).exists(), "refusal: an empty model writes nothing");
    }
    // An existing destination is never replaced.
    {
        const QString path = QDir(directory).filePath("existing.step");
        QFile file(path);
        check(file.open(QIODevice::WriteOnly) && file.write("keep me") == 7, "the existing file is written");
        file.close();
        bool refused = false;
        try {
            StepExportOptions options;
            exportStepFile(doc, path, options, exportOptions());
        } catch (const StepExportError &error) {
            refused = error.code() == "destinationExists";
            check(std::string(error.what()).size() <= 256, "E8: the destination message is at most 256 bytes");
        }
        check(refused, "refusal: an existing destination is refused");
        check(read(path) == "keep me", "refusal: the existing file is untouched");
    }
    // E8: a successful export's report is within 64 KiB.
    {
        StepExportOptions options;
        const auto exported = exportStepFile(doc, QDir(directory).filePath("report.step"), options, exportOptions());
        check(QJsonDocument(exported.report).toJson(QJsonDocument::Compact).size() <= 64 * 1024, "E8: the report is at most 64 KiB");
    }
}

// The budget (ADR 0172 section 6): a generated model of 1,000,000 triangles (ten bodies, each a thousand planar 102-gons of
// 100 triangles), exported and timed. The 5 s target and the 256 MiB output cap are reported as measured; nothing is assumed.
// A planar regular polygon of `sides` vertices, radius 0.5 m, at height z.
std::vector<Vec3> regularLoop(int sides, double z) {
    std::vector<Vec3> loop;
    for (int k = 0; k < sides; ++k) {
        const double angle = 2 * std::numbers::pi * k / sides;
        loop.push_back({0.5 * std::cos(angle), 0.5 * std::sin(angle), z});
    }
    return loop;
}

// Body `index` of the budget style: `facesPerBody` 102-gons, then one `tailSides`-gon when tailSides is over 2 (R098.h: the
// tail fills a body to the per-body vertex limit).
std::shared_ptr<Body> budgetBody(Id id, int index, int facesPerBody, int tailSides) {
    constexpr int sides = 102;
    auto body = std::make_shared<Body>();
    body->id = id;
    body->name = "Budget " + std::to_string(index);
    for (int f = 0; f < facesPerBody; ++f)
        body->surface.addFace({regularLoop(sides, f * 0.002 + index * 3.0)});
    if (tailSides > 2)
        body->surface.addFace({regularLoop(tailSides, facesPerBody * 0.002 + index * 3.0)});
    body->topology = Topology::rebuild(body->surface, {});
    return body;
}

// A planar grid of `columns` x `rows` quads, each split into two triangle faces: 2 x columns x rows faces and about 1.5 edges
// per face. The vertices and faces are entered directly: Surface::vertex searches every vertex and addFace copies the surface,
// which is quadratic at this size.
std::shared_ptr<Body> gridBody(Id id, int columns, int rows) {
    auto body = std::make_shared<Body>();
    body->id = id;
    body->name = "Grid";
    constexpr double pitch = 0.01;
    Surface &surface = body->surface;
    const int width = rows + 1;
    std::vector<Id> corner;
    corner.reserve(std::size_t(columns + 1) * width);
    for (int i = 0; i <= columns; ++i)
        for (int j = 0; j <= rows; ++j) {
            corner.push_back(surface.nextId++);
            surface.vertices.emplace(corner.back(), Vec3{i * pitch, j * pitch, 0.0});
        }
    auto at = [&](int i, int j) { return corner[std::size_t(i) * width + j]; };
    for (int i = 0; i < columns; ++i)
        for (int j = 0; j < rows; ++j) {
            surface.addFaceIds({{at(i, j), at(i + 1, j), at(i + 1, j + 1)}});
            surface.addFaceIds({{at(i, j), at(i + 1, j + 1), at(i, j + 1)}});
        }
    body->topology = Topology::rebuild(body->surface, {});
    return body;
}

Document budgetModel(int bodies, int facesPerBody) {
    Document doc;
    Edit edit{"Budget model", {}};
    Id next = 1;
    for (int b = 0; b < bodies; ++b) {
        auto body = budgetBody(next++, b, facesPerBody, 0);
        edit.changes.push_back({body->id, nullptr, body});
    }
    edit.nextIdFloor = next;
    doc.apply(std::move(edit), doc.revision());
    return doc;
}

// The R098.h model: the largest model a document holds (src/core/document_limits.hpp). A definition is one body under a group,
// from `makeMember`; it is placed `placements` times, so the materialized counts are placements x the body's. The unique
// geometry is one body (the unique limits count the definition's members once). One edit per placement: one edit of the whole
// model is refused by the 64 MiB history budget (src/core/model.cpp).
// A single triangle of 10 mm: the smallest member, for the placement and body limits.
std::shared_ptr<Body> triangleBody(Id id) {
    auto body = std::make_shared<Body>();
    body->id = id;
    body->name = "Triangle";
    Surface &surface = body->surface;
    const Id a = surface.nextId++, b = surface.nextId++, c = surface.nextId++;
    surface.vertices.emplace(a, Vec3{0.0, 0.0, 0.0});
    surface.vertices.emplace(b, Vec3{0.01, 0.0, 0.0});
    surface.vertices.emplace(c, Vec3{0.0, 0.01, 0.0});
    surface.addFaceIds({{a, b, c}});
    body->topology = Topology::rebuild(body->surface, {});
    return body;
}

// `bodies` separate closed pentagonal bipyramids (7 vertices, 10 triangle faces each), one body per record: the unique limits
// for bodies (10,000), vertices (70,000) and faces (100,000) at once. Each is a solid when exported.
Document solidsModel(int bodies, int perEdit) {
    Document doc;
    Id next = 1;
    for (int b = 0; b < bodies;) {
        Edit edit{"Solids", {}};
        for (int k = 0; k < perEdit && b < bodies; ++k, ++b) {
            auto body = std::make_shared<Body>();
            body->id = next++;
            body->name = "Solid " + std::to_string(b);
            Surface &surface = body->surface;
            const Id top = surface.nextId++, bottom = surface.nextId++;
            surface.vertices.emplace(top, Vec3{2.0 * b, 0.0, 0.5});
            surface.vertices.emplace(bottom, Vec3{2.0 * b, 0.0, -0.5});
            Id ring[5];
            for (int r = 0; r < 5; ++r) {
                ring[r] = surface.nextId++;
                const double angle = 2 * std::numbers::pi * r / 5;
                surface.vertices.emplace(ring[r], Vec3{2.0 * b + 0.5 * std::cos(angle), 0.5 * std::sin(angle), 0.0});
            }
            for (int r = 0; r < 5; ++r) {
                surface.addFaceIds({{top, ring[r], ring[(r + 1) % 5]}});
                surface.addFaceIds({{bottom, ring[(r + 1) % 5], ring[r]}});
            }
            body->topology = Topology::rebuild(body->surface, {});
            edit.changes.push_back({body->id, nullptr, body});
        }
        edit.nextIdFloor = next;
        doc.apply(std::move(edit), doc.revision());
    }
    return doc;
}

using MemberFactory = std::function<std::shared_ptr<Body>(Id)>;
// `perEdit` placements per edit (one for the large members; the history budget counts each edit).
Document largestModel(int placements, const MemberFactory &makeMember, int perEdit) {
    Document doc;
    Edit definitionEdit{"Largest definition", {}};
    auto definition = std::make_shared<ComponentDefinition>();
    definition->id = 1;
    definition->root = 2;
    definition->name = "Largest";
    auto root = group(2, 0, "Largest");
    auto member = makeMember(3);
    member->parent = 2;
    definition->members[2] = root;
    definition->members[3] = member;
    definition->nextMemberId = 4;
    definitionEdit.definitions.push_back({1, nullptr, definition});
    definitionEdit.nextDefinitionFloor = 2;
    definitionEdit.nextIdFloor = 10;
    doc.apply(std::move(definitionEdit), doc.revision());
    Id next = 10;
    for (int p = 0; p < placements;) {
        Edit edit{"Largest placement", {}};
        for (int k = 0; k < perEdit && p < placements; ++k, ++p) {
            auto instance = std::make_shared<ComponentInstance>();
            instance->definition = 1;
            auto placedRoot = std::make_shared<Body>(*root);
            placedRoot->id = next++;
            placedRoot->transform = Transform::translation({2.0 * p, 0, 0});
            auto placedMember = std::make_shared<Body>(*member);
            placedMember->id = next++;
            placedMember->parent = placedRoot->id;
            instance->members[2] = placedRoot->id;
            instance->members[3] = placedMember->id;
            edit.changes.push_back({placedRoot->id, nullptr, placedRoot});
            edit.changes.push_back({placedMember->id, nullptr, placedMember});
            edit.instances.push_back({placedRoot->id, nullptr, instance});
        }
        edit.nextIdFloor = next;
        doc.apply(std::move(edit), doc.revision());
    }
    return doc;
}

// R098.h (ADR 0172 section 6, owner decision 13): the largest model a document holds, exported through exportStepFile with the
// helper's default limits (4 GiB address space, 120 s deadline, four threads), which is what the application and sketchyup-cli
// pass. The model is largestModel(PLACEMENTS, the member). Each run's helper peak is the largest child's resident size
// (getrusage RUSAGE_CHILDREN), so after run n it is the peak of the largest of the n helpers so far.
void largest(const QString &directory, const std::function<Document()> &build, const std::string &shape, int runs) {
    QElapsedTimer clock;
    clock.start();
    const Document doc = build();
    const qint64 built = clock.elapsed();
    std::size_t vertices = 0, faces = 0, edges = 0, triangles = 0;
    for (const auto &[id, body] : doc.bodies()) {
        (void)id;
        vertices += body->surface.vertices.size();
        faces += body->surface.faces.size();
        edges += body->surface.edges().size();
        for (const auto &[faceId, face] : body->surface.faces) {
            (void)face;
            triangles += body->surface.triangulateExact(faceId).size();
        }
    }
    std::cout << "model: " << shape << ", built in " << built << " ms: " << vertices
              << " materialized vertices (limit 600,000), " << faces << " faces (600,000), " << edges << " edges (900,000), "
              << triangles << " triangles (materialized)" << std::endl;
    try {
        std::cout << "native: " << encodeDocument(doc).size() << " bytes (the file limit is " << NativeLimits::fileBytes << ")"
                  << std::endl;
    } catch (const std::exception &problem) {
        std::cout << "native: not encoded: " << problem.what() << std::endl;
    }
    for (int run = 1; run <= runs; ++run) {
        IfcWorkerOptions options;
        options.workerPath = helperPath();
        const QString path = QDir(directory).filePath(QStringLiteral("largest-%1.step").arg(run));
        QElapsedTimer timer;
        timer.start();
        const auto exported = exportStepFile(doc, path, StepExportOptions{}, options);
        const qint64 milliseconds = timer.elapsed();
        rusage children{};
        getrusage(RUSAGE_CHILDREN, &children);
        std::cout << "run " << run << ": export " << milliseconds << " ms, " << exported.bytes << " bytes, helper peak "
                  << children.ru_maxrss / 1024 << " MiB (largest child so far)" << std::endl;
        if (run == 1)
            std::cout << "report: " << QJsonDocument(exported.report).toJson(QJsonDocument::Compact).left(2400).toStdString()
                      << std::endl;
        QFile::remove(path);
    }
    rusage self{};
    getrusage(RUSAGE_SELF, &self);
    std::cout << "this process peak " << self.ru_maxrss / 1024 << " MiB" << std::endl;
}

void budget(const QString &directory, int bodies, int facesPerBody) {
    QElapsedTimer clock;
    clock.start();
    const Document doc = budgetModel(bodies, facesPerBody);
    const qint64 built = clock.elapsed();
    clock.restart();
    IfcWorkerOptions options = exportOptions();
    QElapsedTimer phases;
    phases.start();
    options.progress = [&phases](const QString &phase, qint64) {
        std::cout << "  phase " << phase.toStdString() << " at " << phases.elapsed() << " ms" << std::endl;
    };
    const auto exported = exportStepFile(doc, QDir(directory).filePath("budget.step"), StepExportOptions{}, options);
    const qint64 milliseconds = clock.elapsed();
    const long long triangles = 1LL * bodies * facesPerBody * 100;
    std::cout << "budget: " << triangles << " triangles, " << exported.bytes << " bytes, export " << milliseconds
              << " ms (model built in " << built << " ms; target 5000 ms, cap 256 MiB)" << '\n';
}

} // namespace

int main(int argc, char **argv) {
    if (!QFileInfo(helperPath()).isExecutable()) {
        std::cout << "skipped: the helper is not built" << '\n';
        return 77;
    }
    try {
        QTemporaryDir directory;
        check(directory.isValid(), "a temporary directory");
        if (argc > 1 && std::string(argv[1]) == "--budget") {
            // ifc_step_export_tests --budget [BODIES FACES]: the default is 2 x 100 (20,000 triangles, seconds); the
            // 1,000,000-triangle model is 20 500 (its model takes hours to build through the document API, see ADR 0172).
            budget(directory.path(), argc > 2 ? std::stoi(argv[2]) : 2, argc > 3 ? std::stoi(argv[3]) : 100);
            return 0;
        }
        if (argc > 1 && std::string(argv[1]) == "--largest") {
            // ifc_step_export_tests --largest [PLACEMENTS FACES TAIL RUNS]: the R098.h measurement. The default is the largest model
            // the document holds in this style: six placements of a definition of 980 102-gons and one 40-gon (600,000 materialized
            // vertices, 100,000 unique), five exports.
            const int placements = argc > 2 ? std::stoi(argv[2]) : 6;
            const int faces = argc > 3 ? std::stoi(argv[3]) : 980;
            const int tail = argc > 4 ? std::stoi(argv[4]) : 40;
            const std::string shape = std::to_string(placements) + " placements of 102-gons: " + std::to_string(faces) + " 102-gons and one " +
                                      std::to_string(tail) + "-gon (100,000 vertices)";
            largest(directory.path(), [&] { return largestModel(placements, [&](Id id) { return budgetBody(id, 0, faces, tail); }, 1); },
                    shape, argc > 5 ? std::stoi(argv[5]) : 5);
            return 0;
        }
        if (argc > 1 && std::string(argv[1]) == "--largest-placements") {
            // ifc_step_export_tests --largest-placements [PLACEMENTS RUNS]: the R098.h placement worst case, 20,000 placements of
            // a triangle under a group: 40,000 materialized bodies, 60,000 vertices, the placement and body limits, five exports.
            const int placements = argc > 2 ? std::stoi(argv[2]) : 20000;
            largest(directory.path(), [&] { return largestModel(placements, [](Id id) { return triangleBody(id); }, 1000); },
                    std::to_string(placements) + " placements of one triangle under a group (two bodies each)",
                    argc > 3 ? std::stoi(argv[3]) : 5);
            return 0;
        }
        if (argc > 1 && std::string(argv[1]) == "--largest-grid") {
            // ifc_step_export_tests --largest-grid [PLACEMENTS COLUMNS ROWS RUNS]: the R098.h export worst case within the unique
            // limits, a grid of 200 x 250 quads (100,000 triangle faces, 50,451 vertices, 150,450 edges), five exports.
            const int placements = argc > 2 ? std::stoi(argv[2]) : 5;
            const int columns = argc > 3 ? std::stoi(argv[3]) : 200;
            const int rows = argc > 4 ? std::stoi(argv[4]) : 250;
            const std::string shape = std::to_string(placements) + " placements of a " + std::to_string(columns) + " x " +
                                      std::to_string(rows) + " grid of triangle faces (100,000 faces)";
            largest(directory.path(), [&] { return largestModel(placements, [&](Id id) { return gridBody(id, columns, rows); }, 1); },
                    shape, argc > 5 ? std::stoi(argv[5]) : 5);
            return 0;
        }
        if (argc > 1 && std::string(argv[1]) == "--largest-solids") {
            // ifc_step_export_tests --largest-solids [BODIES RUNS]: the R098.h body worst case, 10,000 separate closed solids of
            // 10 triangles each (100,000 unique faces, 70,000 vertices, 10,000 bodies): the unique limits, five exports.
            const int bodies = argc > 2 ? std::stoi(argv[2]) : 10000;
            largest(directory.path(), [&] { return solidsModel(bodies, 500); },
                    std::to_string(bodies) + " separate closed pentagonal bipyramids (7 vertices and 10 faces each)",
                    argc > 3 ? std::stoi(argv[3]) : 5);
            return 0;
        }
        const Document doc = roundTripModel();
        roundTrip(doc, "ap214", directory.path());
        roundTrip(doc, "ap203", directory.path());
        refusals(doc, directory.path());
        std::cout << "ifc_step_export: all checks passed" << '\n';
        return 0;
    } catch (const std::exception &problem) {
        std::cerr << "ifc_step_export: " << problem.what() << '\n';
        return 1;
    }
}
