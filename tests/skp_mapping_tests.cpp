// R102.b: the SketchUp import's mapping (ADR 0176 sections 4 and 7), through the real path: the
// helper (sketchyup-skp-worker, via importSkpFile) and the checked decoder. On every fixture in
// tests/fixtures/skp/ (the scaled, mirrored and textured ones included):
//   S1: the native face, hole, edge, definition, instance, material and tag counts equal the package's;
//   S2: the world bounding box, in metres, matches the placement arithmetic within 1e-6 m;
//   S3: the three-instance component is one definition with three instances, and editing the
//       definition changes all three;
//   S4: the hidden tag is hidden, the soft and smooth edges keep their flags, the holed face has two loops;
//   S5: the two-materials faces carry the right materials, the textured face carries a texture and
//       the UV mapping the exporter's formula gives, and a mirrored placement keeps its faces outward.
// Plus: one undo removes the whole import, its label is "Import SketchUp", and the report's
// accounting adds up. With --real and SKETCHYUP_SKP_CORPUS, the owner's file is checked too (its
// counts and its import time); it is never copied into the repository, and the test exits 77
// (skipped) when the variable is unset or the file is absent.
#include "core/components.hpp"
#include "core/model.hpp"
#include "io/skp_package.hpp"
#include "io/skp_worker.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace sketchy;

namespace {

int failures = 0;

void check(bool ok, const std::string &message) {
    if (!ok) {
        std::cerr << "FAIL: " << message << "\n";
        ++failures;
    }
}

bool within(double a, double b, double tolerance = 1e-6) { return std::abs(a - b) <= tolerance; }

QString fixture(const char *name) { return QStringLiteral(SOURCE_DIR "/tests/fixtures/skp/") + QString::fromLatin1(name); }

QString scratchRoot() { return QDir::tempPath(); }

SkpWorkerOptions workerOptions() {
    SkpWorkerOptions options;
    options.workerPath = QStringLiteral(SKP_WORKER_PATH);
    options.scratchRoot = scratchRoot();
    return options;
}

// The package the helper writes for a file, decoded with the checked decoder (the bytes the import reads).
SkpPackage packageOf(const QString &file, const QTemporaryDir &temp) {
    const auto out = temp.path() + "/" + QFileInfo(file).completeBaseName() + ".skskpp";
    QProcess process;
    process.start(QStringLiteral(SKP_WORKER_PATH), {"--import", file, "--output", out});
    check(process.waitForFinished(120000) && process.exitCode() == 0, "the helper's package of " + file.toStdString());
    QFile packageFile(out);
    check(packageFile.open(QIODevice::ReadOnly), "read the package of " + file.toStdString());
    const auto bytes = packageFile.readAll();
    return decodeSkpPackage(std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
}

// The whole import, through the application's runner. A refusal is a failure of the test.
SkpImport importOf(const QString &file) {
    try {
        return importSkpFile(file, workerOptions());
    } catch (const IfcImportError &error) {
        check(false, "the import of " + file.toStdString() + " was refused: " + error.code() + ": " + error.what());
        throw;
    }
}

// The world transform of a package placement: row-major 3x3 in the top-left, translation in values 12 to 14
// (the worker's layout, ADR 0176 section 3). p' = M p + t, the same arithmetic as OpenSKP's transform_point.
std::array<double, 3> placed(const SkpPackageInstance &instance, const std::array<double, 3> &p) {
    const auto &m = instance.transform;
    return {m[0] * p[0] + m[1] * p[1] + m[2] * p[2] + m[12], m[4] * p[0] + m[5] * p[1] + m[6] * p[2] + m[13],
            m[8] * p[0] + m[9] * p[1] + m[10] * p[2] + m[14]};
}

// Every metre-space vertex of the package's placed geometry (the root's, and each placement's, recursively).
void packageWorldPoints(const SkpPackage &package, std::size_t definition, const std::function<std::array<double, 3>(const std::array<double, 3> &)> &map,
                        std::vector<std::array<double, 3>> &out, int depth = 0) {
    if (depth > 32)
        return;
    const auto &def = package.definitions[definition];
    for (const auto &p : def.vertices)
        out.push_back(map(p));
    for (const auto &instance : def.instances) {
        if (instance.definition < 0)
            continue;
        packageWorldPoints(package, std::size_t(instance.definition), [&](const std::array<double, 3> &p) { return map(placed(instance, p)); }, out, depth + 1);
    }
}

// The exporter's UV formula (openskp face_groups.cpp compute_face_uv and face_uv_basis), written out
// independently of the importer, for a point in metres, the stored 3x3 and the texture tile in inches.
std::array<double, 2> exporterUv(const std::array<double, 9> &stored, const std::array<double, 3> &normal, double tileW,
                                 double tileH, const std::array<double, 3> &p) {
    const double inch = 0.0254;
    const double cx = -normal[1], cy = normal[0];
    const double clen = std::sqrt(cx * cx + cy * cy);
    std::array<double, 3> xr{1, 0, 0}, yr{0, 1, 0};
    if (clen >= 1e-3) {
        xr = {cx / clen, cy / clen, 0};
        yr = {normal[1] * xr[2] - normal[2] * xr[1], normal[2] * xr[0] - normal[0] * xr[2], normal[0] * xr[1] - normal[1] * xr[0]};
    } else if (normal[2] < 0) {
        xr = {-1, 0, 0};
    }
    const double px = (p[0] * xr[0] + p[1] * xr[1] + p[2] * xr[2]) / inch;
    const double py = (p[0] * yr[0] + p[1] * yr[1] + p[2] * yr[2]) / inch;
    const auto &a = stored;
    const double det = a[0] * (a[4] * a[8] - a[5] * a[7]) - a[1] * (a[3] * a[8] - a[5] * a[6]) + a[2] * (a[3] * a[7] - a[4] * a[6]);
    const double inv[9] = {(a[4] * a[8] - a[5] * a[7]) / det, (a[2] * a[7] - a[1] * a[8]) / det, (a[1] * a[5] - a[2] * a[4]) / det,
                           (a[5] * a[6] - a[3] * a[8]) / det, (a[0] * a[8] - a[2] * a[6]) / det, (a[2] * a[3] - a[0] * a[5]) / det,
                           (a[3] * a[7] - a[4] * a[6]) / det, (a[1] * a[6] - a[0] * a[7]) / det, (a[0] * a[4] - a[1] * a[3]) / det};
    const double u = px * inv[0] + py * inv[3] + inv[6];
    const double v = px * inv[1] + py * inv[4] + inv[7];
    return {u / tileW, v / tileH};
}

// The canonical geometry: the bodies the document holds once, not as a placed copy. Those are the root's
// geometry and the definitions' own members; a placed copy is a body some instance binding names.
std::vector<const Body *> canonicalBodies(const Document &doc) {
    std::set<Id> placedIds;
    for (const auto &[root, instance] : doc.instances()) {
        (void)root;
        for (const auto &[member, id] : instance->members) {
            (void)member;
            placedIds.insert(id);
        }
    }
    std::vector<const Body *> out;
    for (const auto &[id, body] : doc.bodies())
        if (!placedIds.contains(id))
            out.push_back(body.get());
    for (const auto &[id, definition] : doc.definitions())
        for (const auto &[member, body] : definition->members) {
            (void)member;
            out.push_back(body.get());
        }
    return out;
}

struct Counts {
    long long faces{}, holes{}, edges{}, wires{};
};

Counts countsOf(const std::vector<const Body *> &bodies) {
    Counts c;
    for (const auto *body : bodies) {
        c.faces += static_cast<long long>(body->surface.faces.size());
        for (const auto &[id, face] : body->surface.faces) {
            (void)id;
            c.holes += static_cast<long long>(face.loops.size()) - 1;
        }
        c.edges += static_cast<long long>(body->topology.edges.size());
        c.wires += static_cast<long long>(body->surface.wires.size());
    }
    return c;
}

// The world bounding box (metres) of every body the document places: the scene's bodies, through their transforms.
std::pair<std::array<double, 3>, std::array<double, 3>> worldBox(const Document &doc) {
    std::array<double, 3> lo{1e300, 1e300, 1e300}, hi{-1e300, -1e300, -1e300};
    for (const auto &[id, body] : doc.bodies()) {
        if (body->kind == BodyKind::Group)
            continue;
        const auto world = doc.worldTransform(id);
        for (const auto &[vertex, p] : body->surface.vertices) {
            (void)vertex;
            const auto w = world.point(p);
            const std::array<double, 3> q{w.x, w.y, w.z};
            for (int axis = 0; axis < 3; ++axis) {
                lo[std::size_t(axis)] = std::min(lo[std::size_t(axis)], q[std::size_t(axis)]);
                hi[std::size_t(axis)] = std::max(hi[std::size_t(axis)], q[std::size_t(axis)]);
            }
        }
    }
    return {lo, hi};
}

// The bodies of the faces with a given centroid test, used to find the faces a fixture names by place.
std::optional<Id> faceNearest(const Body &body, const std::array<double, 3> &at) {
    std::optional<Id> best;
    double bestDistance = 1e300;
    for (const auto &[face, record] : body.surface.faces) {
        (void)record;
        std::array<double, 3> centre{};
        const auto &loop = body.surface.faces.at(face).loops.front();
        for (const Id vertex : loop) {
            const auto p = body.surface.vertices.at(vertex);
            centre[0] += p.x / double(loop.size());
            centre[1] += p.y / double(loop.size());
            centre[2] += p.z / double(loop.size());
        }
        const double d = std::hypot(centre[0] - at[0], std::hypot(centre[1] - at[1], centre[2] - at[2]));
        if (d < bestDistance) {
            bestDistance = d;
            best = face;
        }
    }
    return best;
}

std::string materialName(const Document &doc, Id material) {
    if (!material || !doc.materials().contains(material))
        return "";
    return doc.materials().at(material)->name;
}

// S1 for one fixture: the native counts against the package's, and the accounting identity.
void checkCounts(const char *name, const SkpPackage &package, const SkpImport &imported) {
    const auto &doc = imported.document;
    const auto counts = countsOf(canonicalBodies(doc));
    const std::string label = name;
    std::cout << name << ": " << counts.faces << " faces, " << counts.holes << " holes, " << counts.edges << " edges, "
              << doc.definitions().size() << " definitions, " << doc.instances().size() << " instances, "
              << doc.materials().size() << " materials, " << doc.tags().size() << " tags\n";
    // A face the native validator refuses is dropped and counted (ADR 0176 section 4), with its holes:
    // the native faces and holes plus the dropped ones are the package's.
    const auto &report = imported.report["counts"].toObject();
    const long long droppedFaces = static_cast<long long>(imported.report["accounting"].toObject()["faces"].toObject()["omittedByImport"].toDouble(-1));
    const long long droppedHoles = static_cast<long long>(report["invalidHoles"].toDouble(-1));
    check(counts.faces + droppedFaces == package.counts.faces, "S1 " + label + ": faces " + std::to_string(counts.faces) + " plus " + std::to_string(droppedFaces) + " dropped != " + std::to_string(package.counts.faces));
    check(counts.holes + droppedHoles == package.counts.holes, "S1 " + label + ": holes " + std::to_string(counts.holes) + " plus " + std::to_string(droppedHoles) + " dropped != " + std::to_string(package.counts.holes));
    check(counts.faces == report["faces"].toDouble(-1), label + ": the report counts the faces the document holds");
    check(counts.edges == package.counts.edges, "S1 " + label + ": edges " + std::to_string(counts.edges) + " != " + std::to_string(package.counts.edges));
    check(static_cast<long long>(doc.definitions().size()) == package.counts.definitions - 1,
          "S1 " + label + ": definitions " + std::to_string(doc.definitions().size()) + " != the package's " + std::to_string(package.counts.definitions - 1) + " (the root is a group)");
    check(static_cast<long long>(doc.instances().size()) == package.counts.instances, "S1 " + label + ": instances " + std::to_string(doc.instances().size()) + " != " + std::to_string(package.counts.instances));
    check(static_cast<long long>(doc.materials().size()) == package.counts.materials, "S1 " + label + ": materials " + std::to_string(doc.materials().size()) + " != " + std::to_string(package.counts.materials));
    long long untagged = 0;
    for (const auto &tag : package.tags)
        untagged += (tag.name == "Layer0" || tag.name == "Untagged") ? 1 : 0;
    check(static_cast<long long>(doc.tags().size()) + untagged == package.counts.tags, "S1 " + label + ": tags " + std::to_string(doc.tags().size()) + " plus " + std::to_string(untagged) + " untagged != " + std::to_string(package.counts.tags));
    // The report's accounting: counted = imported + omitted, for each kind.
    for (const char *kind : {"faces", "edges", "instances", "materials"}) {
        const auto block = imported.report["accounting"].toObject()[kind].toObject();
        const long long counted = static_cast<long long>(block["counted"].toDouble(-1));
        const long long imp = static_cast<long long>(block["imported"].toDouble(-1));
        const long long omit = static_cast<long long>(block["omitted"].toDouble(-1));
        check(imp + omit == counted, label + ": the accounting for " + kind + " does not add up");
    }
}

// S2: the world bounding box matches the package's own placement arithmetic, in metres.
void checkBox(const char *name, const SkpPackage &package, const SkpImport &imported) {
    const auto [lo, hi] = worldBox(imported.document);
    std::vector<std::array<double, 3>> expected;
    packageWorldPoints(package, 0, [](const std::array<double, 3> &p) { return p; }, expected);
    std::array<double, 3> elo{1e300, 1e300, 1e300}, ehi{-1e300, -1e300, -1e300};
    for (const auto &p : expected)
        for (int axis = 0; axis < 3; ++axis) {
            elo[std::size_t(axis)] = std::min(elo[std::size_t(axis)], p[std::size_t(axis)]);
            ehi[std::size_t(axis)] = std::max(ehi[std::size_t(axis)], p[std::size_t(axis)]);
        }
    for (int axis = 0; axis < 3; ++axis) {
        check(within(lo[std::size_t(axis)], elo[std::size_t(axis)]), std::string("S2 ") + name + ": min axis " + std::to_string(axis));
        check(within(hi[std::size_t(axis)], ehi[std::size_t(axis)]), std::string("S2 ") + name + ": max axis " + std::to_string(axis));
    }
}

// The document as the fixtures' own geometry says it must be (the scaled block, in inches, through the
// parser's own placement, times 0.0254): S2 against the written numbers, not only the package's.
void checkScaledBox(const SkpImport &imported) {
    const double M[9] = {2, 0.5, 0, 0, 1, 0, 0, 0, 1.5};
    const double t[3] = {1, 2, 3};
    std::array<double, 3> lo{1e300, 1e300, 1e300}, hi{-1e300, -1e300, -1e300};
    for (int corner = 0; corner < 8; ++corner) {
        const double p[3] = {double(corner & 1), double((corner >> 1) & 1), double((corner >> 2) & 1)};
        for (int r = 0; r < 3; ++r) {
            const double inches = M[r * 3] * p[0] + M[r * 3 + 1] * p[1] + M[r * 3 + 2] * p[2] + t[r];
            lo[std::size_t(r)] = std::min(lo[std::size_t(r)], inches * 0.0254);
            hi[std::size_t(r)] = std::max(hi[std::size_t(r)], inches * 0.0254);
        }
    }
    const auto [wlo, whi] = worldBox(imported.document);
    for (int axis = 0; axis < 3; ++axis) {
        check(within(wlo[std::size_t(axis)], lo[std::size_t(axis)]), "S2 scaled: min axis " + std::to_string(axis));
        check(within(whi[std::size_t(axis)], hi[std::size_t(axis)]), "S2 scaled: max axis " + std::to_string(axis));
    }
}

void checkUndo(const char *name, SkpImport &imported) {
    auto &doc = imported.document;
    const std::string label = name;
    check(doc.canUndo(), label + ": the import is undoable");
    const auto history = doc.history(0, 10);
    check(!history.entries.empty() && history.entries.front().label == "Import SketchUp", label + ": the history label is \"Import SketchUp\"");
    check(history.entries.size() == 1, label + ": the import is one history entry");
    doc.undo();
    check(doc.bodies().empty() && doc.definitions().empty() && doc.instances().empty() && doc.tags().empty() &&
              doc.materials().empty() && doc.assets().empty(),
          label + ": one undo removes everything");
}

} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const bool real = argc > 1 && std::string(argv[1]) == "--real";
    QTemporaryDir temp;
    check(temp.isValid(), "the temporary directory");

    // A file that is not a SketchUp file is refused before the helper starts.
    {
        const auto notSkp = temp.path() + "/not-a-model.skp";
        QFile file(notSkp);
        check(file.open(QIODevice::WriteOnly), "write the non-skp file");
        file.write("this is not a SketchUp file");
        file.close();
        try {
            (void)importSkpFile(notSkp, workerOptions());
            check(false, "a non-SketchUp file is refused");
        } catch (const IfcImportError &error) {
            check(error.code() == "notSkp", "the non-SketchUp refusal says notSkp (" + error.code() + ")");
        }
    }

    // S1 to S5 and the undo on each fixture.
    const char *fixtures[] = {"box.skp", "face_with_hole.skp", "soft_smooth_edges.skp", "two_materials.skp",
                              "component_instances.skp", "hidden_tag.skp", "nested_component.skp",
                              "scaled_instance.skp", "mirrored_instance.skp", "textured_face.skp",
                              "named_attributes.skp"};
    for (const char *name : fixtures) {
        const QString file = fixture(name);
        const auto package = packageOf(file, temp);
        auto imported = importOf(file);
        checkCounts(name, package, imported);
        checkBox(name, package, imported);
        const auto &doc = imported.document;
        check(doc.displayUnits() == DisplayUnit::Meters, std::string(name) + ": the display units are metres");
        check(imported.report["notCarried"].toArray().size() == 8, std::string(name) + ": the report names what the package does not carry");
        // The root group is named after the file (without its extension).
        bool rootNamed = false;
        for (const auto &[id, body] : doc.bodies())
            rootNamed |= body->kind == BodyKind::Group && body->parent == 0 && body->name == QFileInfo(file).completeBaseName().toStdString();
        check(rootNamed, std::string(name) + ": the root group is named after the file");
        checkUndo(name, imported);
    }

    // S3: component_instances.skp, three placements of one definition; editing it changes all three.
    {
        auto imported = importOf(fixture("component_instances.skp"));
        auto &doc = imported.document;
        check(doc.definitions().size() == 1, "S3: one definition");
        int hidden = 0;
        for (const auto &[root, instance] : doc.instances()) {
            (void)instance;
            hidden += doc.bodies().at(root)->hidden ? 1 : 0;
        }
        check(hidden == 1, "S3: the hidden third placement is hidden");
        check(doc.instances().size() == 3, "S3: three instances");
        if (doc.definitions().size() == 1 && doc.instances().size() == 3) {
            const Id definition = doc.definitions().begin()->first;
            for (const auto &[root, instance] : doc.instances())
                check(instance->definition == definition, "S3: every instance is of the one definition");
            check(doc.definitions().at(definition)->name == "Widget", "S3: the definition is named Widget");
            const auto &members = doc.definitions().at(definition)->members;
            Id geometry = 0;
            for (const auto &[member, body] : members)
                if (member != 1 && !body->surface.faces.empty())
                    geometry = member;
            check(geometry != 0, "S3: the definition holds its geometry");
            const std::array<float, 3> red{1, 0, 0};
            const auto result = editComponentDefinition(doc, definition, [&](Document &draft) {
                draft.paint(geometry, red);
                return ChangeReport{};
            });
            (void)result;
            check(doc.definitions().size() == 1 && doc.definitions().begin()->first == definition, "S3: the edit keeps one definition");
            int painted = 0;
            for (const auto &[root, instance] : doc.instances()) {
                const auto copy = instance->members.at(geometry);
                painted += doc.bodies().at(copy)->color == red ? 1 : 0;
            }
            check(painted == 3, "S3: editing the definition changes all three instances (" + std::to_string(painted) + " of 3)");
        }
        // The edit is its own history entry: one undo takes it back, a second takes the whole import.
        const auto history = doc.history(0, 10);
        check(history.entries.size() == 2 && history.entries.front().label == "Import SketchUp",
              "S3: the import is the first of two history entries");
        doc.undo();
        doc.undo();
        check(doc.bodies().empty() && doc.definitions().empty(), "S3: undo takes back the edit and the import");
    }

    // Names and attributes (ADR 0176 section 4): the placements' names, their attributes as properties.
    {
        auto imported = importOf(fixture("named_attributes.skp"));
        const auto &doc = imported.document;
        const Body *first = nullptr, *second = nullptr;
        for (const auto &[id, body] : doc.bodies()) {
            (void)id;
            if (body->kind == BodyKind::Group && body->name == "Chair A")
                first = body.get();
            if (body->kind == BodyKind::Group && body->name == "Chair B")
                second = body.get();
        }
        check(first && second, "names: the two placements are named Chair A and Chair B");
        if (first) {
            const auto text = [&](const char *key) -> std::string {
                const auto found = first->properties.find(key);
                return found != first->properties.end() && std::holds_alternative<std::string>(found->second)
                           ? std::get<std::string>(found->second) : std::string("(missing)");
            };
            const auto number = [&](const char *key) -> double {
                const auto found = first->properties.find(key);
                return found != first->properties.end() && std::holds_alternative<double>(found->second)
                           ? std::get<double>(found->second) : -1e300;
            };
            check(text("skp.attributes.Material") == "Oak", "attributes: the string Material is Oak");
            check(number("skp.attributes.Count") == 3.0, "attributes: the integer Count is 3");
            check(number("skp.attributes.Price") == 9.5, "attributes: the float Price is 9.5");
            const auto fire = first->properties.find("skp.attributes.Fire");
            check(fire != first->properties.end() && std::holds_alternative<bool>(fire->second) && std::get<bool>(fire->second),
                  "attributes: the boolean Fire is true");
            check(!first->properties.contains("skp.attributes.Span"), "attributes: a Length is not imported");
        }
        if (second)
            check(second->properties.empty(), "an instance with no attributes has no properties");
        bool named = false;
        for (const auto &[id, definition] : doc.definitions())
            named |= definition->name == "Chair" && id != 0;
        check(named, "names: the definition is named Chair");
        checkUndo("named_attributes.skp", imported);
    }

    // S4: the hidden tag, the soft and smooth edges, the holed face.
    {
        auto imported = importOf(fixture("hidden_tag.skp"));
        bool hiddenTag = false;
        for (const auto &[id, tag] : imported.document.tags()) {
            (void)id;
            hiddenTag |= tag->name == "Off" && !tag->visible;
        }
        check(hiddenTag, "S4: the hidden layer is a hidden tag named Off");
        checkUndo("hidden_tag.skp (S4)", imported);
    }
    {
        auto imported = importOf(fixture("soft_smooth_edges.skp"));
        bool soft = false, smooth = false;
        for (const auto *body : canonicalBodies(imported.document))
            for (const auto &[edge, look] : body->edgeAppearances) {
                (void)edge;
                soft |= look.soft;
                smooth |= look.smooth;
            }
        check(soft && smooth, "S4: the soft and the smooth edges keep their flags");
        checkUndo("soft_smooth_edges.skp (S4)", imported);
    }
    {
        auto imported = importOf(fixture("face_with_hole.skp"));
        const auto counts = countsOf(canonicalBodies(imported.document));
        check(counts.faces == 1 && counts.holes == 1, "S4: one face with a hole (two loops)");
        checkUndo("face_with_hole.skp (S4)", imported);
    }

    // S5: the two materials on the right faces; the textured face's mapping; the mirrored placement's outward faces.
    {
        auto imported = importOf(fixture("two_materials.skp"));
        const auto &doc = imported.document;
        const auto bodies = canonicalBodies(doc);
        int checked = 0;
        for (const auto *body : bodies) {
            for (const auto &[face, sides] : body->faceMaterials) {
                const auto &loop = body->surface.faces.at(face).loops.front();
                double x = 0;
                for (const Id vertex : loop)
                    x += body->surface.vertices.at(vertex).x / double(loop.size());
                const std::string expected = x < 0.5 * 0.0254 * 0 + 1.0 * 0.0254 ? "Red" : "Blue";
                check(materialName(doc, sides.front) == expected, "S5: the face at x=" + std::to_string(x) + " is " + expected);
                ++checked;
            }
        }
        check(checked == 2, "S5: both painted faces carry a material (" + std::to_string(checked) + ")");
        checkUndo("two_materials.skp (S5)", imported);
    }
    {
        const auto package = packageOf(fixture("textured_face.skp"), temp);
        auto imported = importOf(fixture("textured_face.skp"));
        const auto &doc = imported.document;
        const auto bodies = canonicalBodies(doc);
        bool textured = false;
        for (const auto *body : bodies) {
            for (const auto &[face, sides] : body->faceMaterials) {
                const auto material = doc.materials().at(sides.front);
                check(material && material->asset != 0, "S5: the textured face's material has an image asset");
                if (material && material->asset)
                    check(doc.assets().at(material->asset)->mediaType == "image/png", "S5: the texture is kept as PNG");
                const auto mapping = body->faceTextureMappings.find(face);
                check(mapping != body->faceTextureMappings.end() && mapping->second.front.has_value(), "S5: the textured face has a texture mapping");
                if (mapping != body->faceTextureMappings.end() && mapping->second.front) {
                    // The stored transform, and the exporter's formula on it: the mapping must give the same UV.
                    const auto &texture = package.textures.at(0);
                    const auto stored = package.definitions[0].faces[0].uvFront;
                    const std::array<double, 3> normal{0, 0, 1};
                    for (const std::array<double, 3> p : {std::array<double, 3>{0, 0, 0}, {4 * 0.0254, 0, 0}, {0, 4 * 0.0254, 0}, {1 * 0.0254, 2 * 0.0254, 0}}) {
                        const auto want = exporterUv(stored, normal, texture.width, texture.height, p);
                        const auto got = mapping->second.front->coordinates({p[0], p[1], p[2]});
                        check(within(got.u, want[0], 1e-9) && within(got.v, want[1], 1e-9),
                              "S5: the mapping at (" + std::to_string(p[0]) + ", " + std::to_string(p[1]) + ") is the exporter's UV");
                    }
                    textured = true;
                }
            }
        }
        check(textured, "S5: the textured face is imported with its mapping");
        checkUndo("textured_face.skp (S5)", imported);
    }
    {
        // A mirrored placement keeps its faces outward: the block's faces point away from its centre, and the
        // placement (a mirror in X) maps those normals outward too (the normal transforms as a normal).
        auto imported = importOf(fixture("mirrored_instance.skp"));
        const auto &doc = imported.document;
        bool mirrored = false;
        for (const auto &[root, instance] : doc.instances()) {
            const auto world = doc.worldTransform(root);
            mirrored |= world.determinant() < 0;
            const auto inverse = world.inverse();
            const auto blockCentre = world.point({0.5 * 0.0254, 0.5 * 0.0254, 0.5 * 0.0254});
            for (const auto &[member, copy] : instance->members) {
                (void)member;
                const auto body = doc.bodies().find(copy);
                if (body == doc.bodies().end() || body->second->surface.faces.empty())
                    continue;
                for (const auto &[face, record] : body->second->surface.faces) {
                    (void)record;
                    const Vec3 local = normalized(body->second->surface.normal(face));
                    std::array<double, 3> centreLocal{};
                    const auto &loop = body->second->surface.faces.at(face).loops.front();
                    for (const Id vertex : loop)
                        centreLocal[0] += body->second->surface.vertices.at(vertex).x / double(loop.size());
                    (void)centreLocal;
                    // Local: the block's centre is (0.5, 0.5, 0.5) inches, so the face's own centroid, pointed from it.
                    Vec3 centroid{};
                    for (const Id vertex : loop)
                        centroid = centroid + body->second->surface.vertices.at(vertex) * (1.0 / double(loop.size()));
                    const Vec3 centre{0.5 * 0.0254, 0.5 * 0.0254, 0.5 * 0.0254};
                    check(dot(local, centroid - centre) > 0, "S5: a mirrored block's face points outward (local)");
                    // The world normal: the inverse transpose of the placement applied to the local normal.
                    const Vec3 worldNormal{inverse.m[0] * local.x + inverse.m[1] * local.y + inverse.m[2] * local.z,
                                           inverse.m[4] * local.x + inverse.m[5] * local.y + inverse.m[6] * local.z,
                                           inverse.m[8] * local.x + inverse.m[9] * local.y + inverse.m[10] * local.z};
                    const Vec3 worldCentroid = world.point(centroid);
                    check(dot(normalized(worldNormal), worldCentroid - blockCentre) > 0, "S5: a mirrored block's face points outward (world)");
                }
            }
        }
        check(mirrored, "S5: the mirrored placement has a negative determinant");
        checkUndo("mirrored_instance.skp (S5)", imported);
    }
    {
        auto imported = importOf(fixture("scaled_instance.skp"));
        checkScaledBox(imported);
        checkUndo("scaled_instance.skp (S2)", imported);
    }

    if (real) {
        const QString corpus = qEnvironmentVariable("SKETCHYUP_SKP_CORPUS");
        const QString file = corpus.isEmpty() ? QString() : corpus + QStringLiteral("/BanyanHarbor.skp");
        if (file.isEmpty() || !QFile::exists(file)) {
            std::cout << "SKETCHYUP_SKP_CORPUS does not hold the owner's file; skipped\n";
            return 77;
        }
        const auto package = packageOf(file, temp);
        QElapsedTimer clock;
        clock.start();
        auto imported = importOf(file);
        const auto elapsed = clock.elapsed();
        checkCounts("BanyanHarbor.skp", package, imported);
        checkBox("BanyanHarbor.skp", package, imported);
        // The one face the native validator refuses (its outline touches itself): 158 kept, 1 dropped.
        const auto &counts = imported.report["counts"].toObject();
        check(counts["faces"].toInt() == 158 && counts["holes"].toInt() == 4 && counts["invalidHoles"].toInt() == 1,
              "the owner's file: 158 faces and 4 holes kept, the one refused face's hole counted");
        check(imported.report["accounting"].toObject()["faces"].toObject()["omittedByImport"].toInt() == 1,
              "the owner's file: one face is dropped by the native validator");
        check(counts["materials"].toInt() == 18, "the owner's file: 18 materials");
        check(imported.document.definitions().size() == 1, "the owner's file: one component definition (Teddy)");
        check(imported.document.instances().size() == 1, "the owner's file: one instance");
        std::cout << "owner's file: import " << elapsed << " ms, counts " << QJsonDocument(counts).toJson(QJsonDocument::Compact).toStdString() << "\n";
        const auto [boxLo, boxHi] = worldBox(imported.document);
        std::cout << "owner's file: world box " << (boxHi[0] - boxLo[0]) << " x " << (boxHi[1] - boxLo[1]) << " x " << (boxHi[2] - boxLo[2]) << " m\n";
        std::cout << "owner's file: notices " << QJsonDocument(imported.report["notices"].toObject()).toJson(QJsonDocument::Compact).toStdString() << "\n";
        std::cout << "owner's file: locations " << QJsonDocument(imported.report["locations"].toObject()).toJson(QJsonDocument::Compact).toStdString() << "\n";
        checkUndo("BanyanHarbor.skp", imported);
    }

    if (failures) {
        std::cerr << failures << " failures\n";
        return 1;
    }
    std::cout << "skp mapping tests ok\n";
    return 0;
}
