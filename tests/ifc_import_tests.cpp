// R097.b: the whole import, the helper included, checked against a raw-text scan of the files
// (tests/ifc_text_scan.py, which shares no code with the library) and against hand-computed values
// (ADR 0171 section 6.3: A1-A5, B1-B5, C1, C4-C6, D1, D3, E1-E7, H1). Needs sketchyup-ifc-worker
// (IFC_WORKER_PATH) and python3; exits 77 (skipped) when the helper is not built.
#include "io/ifc_worker.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>
#include <set>
using namespace sketchy;
namespace {
void check(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}
bool near(double a, double b, double tolerance = 1e-9) {
    return std::abs(a - b) <= tolerance * std::max(1.0, std::abs(b));
}
QString fixture(const QString &name) { return QStringLiteral(SOURCE_DIR "/tests/fixtures/ifc/") + name; }
IfcWorkerOptions workerOptions(int threads = 1) {
    IfcWorkerOptions options;
    options.workerPath = QStringLiteral(IFC_WORKER_PATH);
    options.threads = threads;
    options.deadlineSeconds = 120;
#ifdef IFC_NO_ADDRESS_LIMIT
    options.addressSpaceBytes = 0;
#endif
    return options;
}
QJsonObject scan(const QString &name) {
    QProcess process;
    process.start("python3", {"-I", QStringLiteral(SOURCE_DIR "/tests/ifc_text_scan.py"), fixture(name)});
    check(process.waitForFinished(60000) && process.exitCode() == 0, "raw-text scan of " + name.toStdString() + ": " + process.readAllStandardError().toStdString());
    return QJsonDocument::fromJson(process.readAllStandardOutput()).object();
}
// A stable identity so two imports can be compared byte for byte.
QByteArray stableEncoding(const Document &document) {
    auto json = QJsonDocument::fromJson(encodeDocument(document)).object();
    json["documentId"] = "00000000000000000000000000000001";
    return QJsonDocument(json).toJson(QJsonDocument::Compact);
}
std::string stringProperty(const Body &body, const char *key) {
    const auto found = body.properties.find(key);
    if (found == body.properties.end())
        return {};
    if (const auto *text = std::get_if<std::string>(&found->second))
        return *text;
    return {};
}
// Volume of a body in world space (signed triangles; an outward-wound solid is positive).
double worldVolume(const Document &document, Id body) {
    double volume = 0;
    for (const auto &t : document.worldTriangles(body))
        volume += dot(t.a, cross(t.b, t.c)) / 6;
    return volume;
}
const Body *byName(const Document &document, const std::string &name) {
    const Body *found = nullptr;
    for (const auto &[id, body] : document.bodies())
        if (body->name == name && body->kind == BodyKind::Geometry)
            found = body.get();
    if (!found)
        for (const auto &[id, body] : document.bodies())
            if (body->name == name)
                found = body.get();
    check(found != nullptr, "no body named " + name);
    return found;
}
// The nearest ancestor (or the body itself) that carries an ifc.GlobalId.
std::string guidOf(const Document &document, const Body &body) { return stringProperty(body, "ifc.GlobalId"); }

struct Imported {
    IfcImport result;
    QJsonObject facts;
};
Imported importFile(const QString &name, int threads = 1, IfcSpaces spaces = IfcSpaces::Hidden) {
    IfcImportOptions options;
    options.spaces = spaces;
    Imported imported{importIfcFile(fixture(name), options, workerOptions(threads)), scan(name)};
    return imported;
}
void commonChecks(const QString &name, const Imported &imported) {
    const auto &doc = imported.result.document;
    const auto &report = imported.result.report;
    const std::string label = name.toStdString();
    // E1: validated as a whole; save and reopen is bit-exact.
    const auto container = encodeContainer(doc);
    check(encodeContainer(decodeContainer(container)) == container, label + ": save and reopen");
    (void)decodeDocument(encodeDocument(doc));
    // E5: one history entry, one undo removes everything.
    check(doc.history().total == 1, label + ": one history entry");
    Document undone = doc;
    undone.undo();
    check(undone.bodies().empty() && undone.tags().empty() && undone.materials().empty() && undone.definitions().empty(), label + ": one undo");
    // D1: the accounting identity, with the scan's total.
    const auto accounting = report["accounting"].toObject();
    check(accounting["entities"].toInt() == imported.facts["entities"].toInt(), label + ": entities equal the scan's");
    check(accounting["converted"].toInt() + accounting["supporting"].toInt() + accounting["omitted"].toInt() == accounting["entities"].toInt(),
          label + ": converted + supporting + omitted = entities");
    // I2: the report's bounds.
    check(QJsonDocument(report).toJson(QJsonDocument::Compact).size() <= 64 * 1024, label + ": report within 64 KiB");
    for (const auto *list : {"notices", "kernelMessages", "locations"})
        for (const auto &entry : report[list].toArray())
            check(entry.toObject()["message"].toString().toUtf8().size() <= 256, label + ": message within 256 bytes");
    // E6 and C4: every node body resolves to its source entity; B5: tags per class.
    const auto objects = imported.facts["objects"].toObject();
    std::map<std::string, const Body *> byGuid;
    for (const auto &[id, body] : doc.bodies()) {
        const auto guid = stringProperty(*body, "ifc.GlobalId");
        if (guid.empty())
            continue;
        byGuid.emplace(guid, body.get());
        bool duplicate = false;
        for (const auto &d : imported.facts["duplicateGuids"].toArray())
            duplicate = duplicate || d.toString().toStdString() == guid;
        const auto cls = stringProperty(*body, "ifc.Class");
        const auto it = objects.find(QString::fromStdString(guid));
        if (it != objects.end()) {
            check(QString::fromStdString(cls).toUpper() == it.value().toObject()["class"].toString(), label + ": ifc.Class of " + guid);
            // (Two elements may share a GlobalId, and the scan keeps one of them.)
            if (!duplicate)
                check(std::get<double>(body->properties.at("ifc.Step")) == it.value().toObject()["step"].toDouble(), label + ": ifc.Step of " + guid);
        }
        if (body->kind == BodyKind::Geometry || body->kind == BodyKind::Group) {
            const bool infrastructure = cls == "IfcRoad" || cls == "IfcRoadPart";
            if (cls != "IfcProject" && cls != "IfcSite" && cls != "IfcBuilding" && cls != "IfcBuildingStorey" && !infrastructure) {
                check(body->tag != 0, label + ": " + cls + " has a tag");
                const auto &tag = doc.tags().at(body->tag)->name;
                const bool space = cls == "IfcSpace" || cls == "IfcSpatialZone";
                check(tag == (space ? "IFC Space" : cls == "IfcOpeningElement" ? "IFC Opening" : ifcTagName(cls)), label + ": tag of " + cls + " is " + tag);
            } else {
                check(body->tag == 0, label + ": spatial groups are untagged");
            }
        }
    }
    // B1: the group tree equals the scan's, by GlobalId.
    const auto parents = imported.facts["parents"].toObject();
    std::string projectGuid;
    for (auto it = objects.begin(); it != objects.end(); ++it)
        if (it.value().toObject()["class"] == "IFCPROJECT")
            projectGuid = it.key().toStdString();
    for (const auto &[guid, body] : byGuid) {
        std::string expected;
        const auto found = parents.find(QString::fromStdString(guid));
        if (found != parents.end())
            expected = found.value().toString().toStdString();
        std::string actual;
        for (Id up = body->parent; up;) {
            const auto &ancestor = *doc.bodies().at(up);
            actual = stringProperty(ancestor, "ifc.GlobalId");
            if (!actual.empty())
                break;
            up = ancestor.parent;
        }
        if (stringProperty(*body, "ifc.Class") == "IfcProject")
            continue;
        // An object nothing contains or aggregates goes under the project.
        if (expected.empty())
            expected = projectGuid;
        check(actual == expected, label + ": parent of " + guid + " is " + actual + ", the scan says " + expected);
    }
    // C1: every property the file defines for a node equals the scan's (type-level where not overridden).
    const auto occurrence = imported.facts["occurrenceProperties"].toObject();
    const auto typed = imported.facts["typeProperties"].toObject();
    for (const auto &[guid, body] : byGuid) {
        QJsonObject want = typed[QString::fromStdString(guid)].toObject()["properties"].toObject();
        const auto own = occurrence[QString::fromStdString(guid)].toObject();
        for (auto it = own.begin(); it != own.end(); ++it)
            want[it.key()] = it.value();
        for (auto it = want.begin(); it != want.end(); ++it) {
            const auto key = it.key().toStdString();
            const auto found = body->properties.find(key);
            check(found != body->properties.end(), label + ": " + guid + " misses " + key);
            if (it.value().isString())
                check(std::get<std::string>(found->second) == it.value().toString().toStdString(), label + ": " + key + " text");
            else if (it.value().isBool())
                check(std::get<bool>(found->second) == it.value().toBool(), label + ": " + key + " flag");
            else
                check(near(std::get<double>(found->second), it.value().toDouble()), label + ": " + key + " number");
        }
    }
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (!QFileInfo(QStringLiteral(IFC_WORKER_PATH)).isExecutable()) {
        std::cerr << "sketchyup-ifc-worker is not built: skipped\n";
        return 77;
    }
    try {
        // ---- The certification set and the originals: the common checks ---------------------------
        const QStringList all{"bsi-ifc2x3-arch.ifc", "bsi-ifc4-arch.ifc", "bsi-ifc43-arch.ifc", "bsi-ifc4-landscaping.ifc",
                              "bsi-ifc4-road.ifc", "rv-wall-with-opening-and-window.ifc", "rv-basin-tessellation.ifc",
                              "rv-column-straight-rectangle-tessellation.ifc", "rv-tessellated-item.ifc",
                              "rv-tessellation-with-individual-colors.ifc", "tree-cases.ifc", "psets-types-escapes.ifc",
                              "units-own.ifc", "extrusion-profiles.ifc", "clipped-wall.ifc", "mapped-items.ifc",
                              "polygonal-face-set.ifc", "unsupported-all.ifc", "ifc43-infrastructure.ifc", "openings.ifc",
                              "units-mm.ifc", "units-m.ifc", "units-ft.ifc", "units-mm-projectlibrary.ifc", "units-inch-rounded.ifc"};
        std::map<QString, Imported> imports;
        for (const auto &name : all) {
            auto first = importFile(name);
            commonChecks(name, first);
            // H1: a second import is identical (single-threaded, stable identity).
            auto second = importFile(name);
            check(stableEncoding(first.result.document) == stableEncoding(second.result.document), name.toStdString() + ": H1 two imports are identical");
            imports.emplace(name, std::move(first));
            std::cerr << name.toStdString() << " ok\n";
        }
        // The default of four threads converts too (the order of the library's output may differ; the document may not).
        {
            auto threaded = importFile("bsi-ifc4-arch.ifc", 4);
            commonChecks("bsi-ifc4-arch.ifc", threaded);
            check(stableEncoding(threaded.result.document) == stableEncoding(imports.at("bsi-ifc4-arch.ifc").result.document), "four threads give the same document");
        }

        // ---- Values past the package's bounds: kept within what the decoder accepts, and said ---------------
        {
            auto hostile = importFile("limits-hostile.ifc");
            const auto *wall = byName(hostile.result.document, "W");
            check(wall != nullptr, "limits: the wall is imported");
            std::size_t pCount = 0;
            for (const auto &[k, v] : wall->properties)
                pCount += k.find("Pset_Hostile") != std::string::npos;
            check(pCount > 0 && pCount <= 128, "limits: the properties that fit the application's 128 a body are kept (" + std::to_string(pCount) + ")");
            bool huge = false;
            for (const auto &[k, v] : wall->properties)
                huge = huge || k.find("Huge") != std::string::npos;
            check(!huge, "limits: a length that is not finite is left out");
            std::set<QString> codes;
            for (const auto &n : hostile.result.report["notices"].toArray())
                codes.insert(n.toObject()["code"].toString());
            check(codes.contains("propertyNotFinite") && codes.contains("propertyTextClipped") && codes.contains("propertiesOmitted"), "limits: and the notices say so");
        }
        {
            const auto &doc = imports.at("units-inch-rounded.ifc").result.document;
            check(near(worldVolume(doc, byName(doc, "W")->id), 1.016 * 0.2032 * 2.032), "a nine-digit inch imports at the snapped size");
        }

        // ---- Hand-computed volumes (E2, E3), units (A1) ------------------------------------------
        {
            const auto &doc = imports.at("rv-wall-with-opening-and-window.ifc").result.document;
            check(near(worldVolume(doc, byName(doc, "Wall for Test Example")->id), 1.5), "E3: the wall with its opening subtracted is 1.5 m3");
            check(near(worldVolume(doc, byName(doc, "Window for Test Example")->id), 0.2), "the window is 0.2 m3");
            // The window sits 1 m along the wall, 0.05 m in and 0.5 m up.
            const auto world = doc.worldTransform(byName(doc, "Window for Test Example")->id);
            check(near(world.m[12], 1.0) && near(world.m[13], 0.05) && near(world.m[14], 0.5), "F1: the window's world placement");
        }
        {
            const auto &doc = imports.at("extrusion-profiles.ifc").result.document;
            check(near(worldVolume(doc, byName(doc, "rectangle")->id), 6.0), "E2: rectangle profile 6 m3");
            const double analytic = std::acos(-1.0) * 0.25 * 2;
            const double circle = worldVolume(doc, byName(doc, "circle")->id);
            check(circle <= analytic && circle >= 0.995 * analytic, "E2: circle within the deflection of the analytic volume");
            check(near(worldVolume(doc, byName(doc, "l-shape")->id), 2.5), "E2: L profile 2.5 m3");
            check(near(worldVolume(doc, byName(doc, "with-voids")->id), 12.0), "E2: profile with voids 12 m3");
            // E4: a profile's side is one face: the rectangle's box has 6 faces, the L has 8 (6 sides + 2 caps).
            check(byName(doc, "rectangle")->surface.faces.size() == 6, "E4: a box is 6 faces");
            check(byName(doc, "l-shape")->surface.faces.size() == 8, "E4: the L prism is 8 faces");
            check(byName(doc, "with-voids")->surface.faces.size() == 10 && byName(doc, "with-voids")->surface.faces.begin() != byName(doc, "with-voids")->surface.faces.end(),
                  "E4: a prism with a hole is 4 + 4 sides and 2 caps with holes");
            bool hole = false;
            for (const auto &[id, face] : byName(doc, "with-voids")->surface.faces)
                hole = hole || face.loops.size() == 2;
            check(hole, "E4: its caps carry the hole as a loop");
        }
        check(near(worldVolume(imports.at("clipped-wall.ifc").result.document, byName(imports.at("clipped-wall.ifc").result.document, "clipped")->id), 1.6), "boolean clipping 1.6 m3");
        {
            const auto &doc = imports.at("polygonal-face-set.ifc").result.document;
            check(near(worldVolume(doc, byName(doc, "polygonal cube")->id), 1.0) && near(worldVolume(doc, byName(doc, "plate with hole")->id), 0.3), "polygonal face sets");
            check(byName(doc, "polygonal cube")->surface.faces.size() == 6, "a polygonal cube is 6 faces");
        }
        {
            const auto &doc = imports.at("mapped-items.ifc").result.document;
            check(doc.definitions().size() == 1 && doc.instances().size() == 3, "mapped items: one component definition, three placements");
            double total = 0;
            for (const auto &[id, body] : doc.bodies())
                if (body->kind == BodyKind::Geometry)
                    total += worldVolume(doc, id);
            check(near(total, 3.0), "and three cubic metres in all");
        }
        {
            const auto &doc = imports.at("units-ft.ifc").result.document;
            for (const auto &[id, body] : doc.bodies())
                if (body->kind == BodyKind::Geometry)
                    check(near(worldVolume(doc, id), 169.901079552), "A1: the foot file's volume");
        }
        {
            const auto &doc = imports.at("units-mm-projectlibrary.ifc").result.document;
            for (const auto &[id, body] : doc.bodies())
                if (body->kind == BodyKind::Geometry)
                    check(near(worldVolume(doc, id), 6.0), "F1: the project-library file imports at the right size");
        }
        // E7: where the file records NetVolume, the mesh agrees within 1e-3 relative.
        {
            const auto &doc = imports.at("bsi-ifc4-arch.ifc").result.document;
            int checked = 0;
            for (const auto &[id, body] : doc.bodies()) {
                const auto net = body->properties.find("Qto_WallBaseQuantities.NetVolume");
                const auto any = net != body->properties.end() ? net : body->properties.find("Qto_SlabBaseQuantities.NetVolume");
                if (any == body->properties.end() || body->kind != BodyKind::Geometry)
                    continue;
                const double want = std::get<double>(any->second);
                check(std::abs(worldVolume(doc, id) - want) <= 1e-3 * want, "E7: " + body->name + " volume vs its NetVolume");
                ++checked;
            }
            check(checked >= 4, "E7: NetVolume compared on " + std::to_string(checked) + " elements");
        }
        // C6: the per-face colours of the individual-colours file.
        {
            const auto &doc = imports.at("rv-tessellation-with-individual-colors.ifc").result.document;
            const Body *body = nullptr;
            for (const auto &[id, candidate] : doc.bodies())
                if (candidate->kind == BodyKind::Geometry)
                    body = candidate.get();
            check(body != nullptr, "the coloured body exists");
            std::set<std::array<float, 3>> colours;
            for (const auto &[face, colour] : body->faceColors)
                colours.insert(colour);
            check(colours == std::set<std::array<float, 3>>{{1, 0, 0}, {0, 0.5f, 0}, {1, 1, 0}}, "C6: the face colours are the file's three");
            check(body->faceColors.size() < body->surface.faces.size() || body->surface.faces.size() > 0, "and the triangle without an index has none");
        }
        // B1/B6: the tree cases.
        {
            const auto &doc = imports.at("tree-cases.ifc").result.document;
            const auto *l1 = byName(doc, "L1");
            check(l1->hidden && stringProperty(*l1, "ifc.Layer") == "Hidden Layer", "B7: a layer that is off imports hidden");
            const auto *space = byName(doc, "SP1");
            check(doc.tags().at(space->tag)->name == "IFC Space" && !doc.tags().at(space->tag)->visible, "B5: spaces are on a tag that starts hidden");
            bool cycle = false;
            for (const auto &[id, body] : doc.bodies())
                cycle = cycle || body->name == "C1" || body->name == "C2";
            check(!cycle, "D3: a cycle's elements are not imported");
            const auto notices = imports.at("tree-cases.ifc").result.report["notices"].toArray();
            std::set<QString> codes;
            for (const auto &n : notices)
                codes.insert(n.toObject()["code"].toString());
            for (const auto &code : {"duplicateGlobalId", "invalidGlobalId", "uncontainedElements", "layerHidden"})
                check(codes.contains(code), std::string("B6: notice ") + code);
        }
        // D2/D3: unsupported content.
        {
            const auto &report = imports.at("unsupported-all.ifc").result.report;
            std::map<std::string, int> reasons;
            for (const auto &entry : report["elements"].toObject()["notImported"].toArray())
                reasons[entry.toObject()["class"].toString().toStdString() + ":" + entry.toObject()["reason"].toString().toStdString()] += entry.toObject()["count"].toInt();
            check(reasons["IfcWall:gridPlacement"] == 1 && reasons["IfcWall:placementCycle"] == 2 && reasons["IfcWall:noGeometry"] == 1 &&
                      reasons["IfcWall:noRepresentation"] == 1 && reasons["IfcVirtualElement:unsupportedClass"] == 1 && reasons["IfcAnnotation:unsupportedClass"] == 1,
                  "D2/D3: every unsupported thing is reported with its reason");
            std::size_t geometric = 0;
            for (const auto &[id, body] : imports.at("unsupported-all.ifc").result.document.bodies())
                geometric += body->kind == BodyKind::Geometry;
            check(geometric == 1, "D3: nothing but the good wall is imported (none at the origin)");
        }
        // A5 and C2: properties with their own units, escapes.
        {
            const auto &doc = imports.at("psets-types-escapes.ifc").result.document;
            const auto *w = byName(doc, "W");
            check(std::get<std::string>(w->properties.at("Pset_WallCommon.Cafe")) == "caf\xC3\xA9", "C2: \\X2\\ decoded");
            check(near(std::get<double>(w->properties.at("Pset_WallCommon.OwnUnit")), 0.254), "A5: own unit");
            const auto &own = imports.at("units-own.ifc").result.document;
            const auto *ow = byName(own, "W");
            check(near(std::get<double>(ow->properties.at("Qto_Own.Area")), 9.290304) && near(std::get<double>(ow->properties.at("Qto_Own.Volume")), 0.28316846592),
                  "A5: conversion-based area and volume units convert");
        }

        // ---- Refusals and the override (A3, A4, F3) ---------------------------------------------------
        const auto refusal = [&](const QString &name, const std::string &code, const QString &unit = {}) {
            IfcWorkerOptions worker = workerOptions();
            worker.lengthUnit = unit;
            try {
                (void)importIfcFile(fixture(name), {}, worker);
            } catch (const IfcImportError &error) {
                check(error.code() == code, name.toStdString() + ": refused with " + error.code() + ", expected " + code);
                return;
            }
            throw std::runtime_error(name.toStdString() + " must be refused");
        };
        refusal("units-missing.ifc", "noLengthUnit");
        refusal("units-furlong.ifc", "unsupportedLengthUnit");
        refusal("far-from-origin.ifc", "farFromOrigin");
        {
            IfcWorkerOptions worker = workerOptions();
            worker.lengthUnit = "ft";
            const auto imported = importIfcFile(fixture("units-missing.ifc"), {}, worker);
            check(imported.report["units"].toObject()["override"].toBool(), "A3: the override is reported");
            for (const auto &[id, body] : imported.document.bodies())
                if (body->kind == BodyKind::Geometry)
                    check(near(worldVolume(imported.document, id), 6 * 0.028316846592), "A3: with the override the file imports in feet");
        }
        // ---- The command line (ADR 0171 section 5; the rules of ADR 0115 and 0123) --------------------------------------
#ifdef CLI_PATH
        {
            QTemporaryDir dir;
            check(dir.isValid(), "CLI scratch");
            const auto cli = QString::fromUtf8(CLI_PATH);
            const auto helper = QStringLiteral(IFC_WORKER_PATH);
            const auto source = fixture("rv-wall-with-opening-and-window.ifc");
            QFile original(source);
            check(original.open(QIODevice::ReadOnly), "read the source");
            const auto sourceBytes = original.readAll();
            original.close();
            const auto invoke = [&](QStringList arguments, bool success = true) {
                QProcess process;
                process.start(cli, arguments);
                check(process.waitForStarted(10000) && process.waitForFinished(120000) && process.exitStatus() == QProcess::NormalExit &&
                          (process.exitCode() == 0) == success,
                      "IFC CLI result: " + arguments.join(' ').toStdString());
                const auto output = success ? process.readAllStandardOutput() : process.readAllStandardError();
                const auto parsed = QJsonDocument::fromJson(output);
                check(parsed.isObject(), "IFC CLI returns structured JSON");
                return parsed.object();
            };
            const auto destination = dir.filePath("converted.sketchyup");
            const auto reported = invoke({"--import-ifc", source, "--ifc-worker", helper});
            check(reported["status"] == "imported" && reported["importReport"].toObject()["format"] == "IFC" &&
                      !reported.contains("nativeFile") && !QFileInfo::exists(destination),
                  "CLI: the report without an output creates no file");
            const auto saved = invoke({"--import-ifc", source, "--ifc-worker", helper, "--output", destination, "--ifc-deflection", "2", "--ifc-spaces", "shown"});
            check(saved["nativeFile"].toObject()["status"] == "created" &&
                      saved["importReport"].toObject()["engine"].toObject()["linearDeflectionMetres"].toDouble() == 0.002,
                  "CLI: a new native file, the deflection passed on");
            const auto copy = encodeDocument(loadDocument(destination));
            check(!loadDocument(destination).bodies().empty(), "the saved document holds the import");
            invoke({"--import-ifc", source, "--ifc-worker", helper, "--output", destination}, false);
            invoke({"--import-ifc", source, "--ifc-worker", helper, "--output", source}, false);
            const auto link = dir.filePath("existing-link");
            check(QFile::link(source, link), "symlink fixture");
            invoke({"--import-ifc", source, "--ifc-worker", helper, "--output", link}, false);
            check(encodeDocument(loadDocument(destination)) == copy, "CLI: a rejected output replacement preserves the file");
            // Option isolation: unrelated options reject before anything is published.
            invoke({"--import-ifc", source, "--ifc-worker", helper, "--input", destination}, false);
            invoke({"--import-ifc", source, "--ifc-worker", helper, "--import-obj", source}, false);
            invoke({"--import-ifc", source, "--ifc-worker", helper, "--format-capabilities"}, false);
            invoke({"--import-ifc", source, "--ifc-worker", helper, "extra"}, false);
            invoke({"--ifc-deflection", "2", "--input", destination}, false);
            invoke({"--import-ifc", source, "--ifc-worker", helper, "--ifc-deflection", "0"}, false);
            invoke({"--import-ifc", source, "--ifc-worker", helper, "--ifc-spaces", "maybe"}, false);
            invoke({"--import-ifc", source, "--ifc-worker", helper, "--ifc-length-unit", "furlong"}, false);
            const auto failure = invoke({"--import-ifc", fixture("units-missing.ifc"), "--ifc-worker", helper}, false);
            check(failure["details"].toObject()["ifcCode"] == "noLengthUnit", "CLI: a refusal names its code");
            invoke({"--import-ifc", fixture("units-missing.ifc"), "--ifc-worker", helper, "--ifc-length-unit", "m"});
            const auto missing = invoke({"--import-ifc", source, "--ifc-worker", "/nonexistent/helper"}, false);
            check(missing["details"].toObject()["ifcCode"] == "workerMissing", "CLI: a missing helper is reported");
            QFile after(source);
            check(after.open(QIODevice::ReadOnly) && after.readAll() == sourceBytes, "CLI: the source stays byte exact");
        }
#endif
        std::cout << "ifc_import ok\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
