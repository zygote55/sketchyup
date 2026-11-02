// R097.b: the application's conversion of an IFC conversion package into a document (ADR 0171 section 3 and
// 4). The packages are built by hand (tests/ifc_package_fixtures.hpp), so no worker is involved: this
// is the contract between the worker and the application, and runs everywhere.
#include "ifc_package_fixtures.hpp"
#include "io/document_io.hpp"
#include "io/ifc_import.hpp"
#include "io/native_limits.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <set>
using namespace sketchy;
using namespace ifc_fixture;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void near(double a, double b, double tol, const char *message) {
    if (!(std::abs(a - b) <= tol))
        throw std::runtime_error(std::string(message) + ": " + std::to_string(a) + " vs " + std::to_string(b));
}
IfcImport import(const IfcPackage &package, IfcSpaces spaces = IfcSpaces::Hidden) {
    IfcImportOptions options;
    options.spaces = spaces;
    IfcRunFacts facts;
    facts.sha256 = "00";
    facts.bytes = 1;
    return importIfcPackage(package, options, facts);
}
template <class F> void refused(const char *code, F operation) {
    try {
        operation();
    } catch (const IfcImportError &error) {
        if (error.code() != code)
            throw std::runtime_error(std::string("expected ") + code + ", got " + error.code() + ": " + error.what());
        return;
    }
    throw std::runtime_error(std::string("must be refused with ") + code);
}
const Body *byStep(const Document &document, double step) {
    for (const auto &[id, body] : document.bodies()) {
        const auto found = body->properties.find("ifc.Step");
        if (found != body->properties.end() && std::get<double>(found->second) == step)
            return body.get();
    }
    return nullptr;
}
std::size_t bodiesWithClass(const Document &document, const char *cls) {
    std::size_t n = 0;
    for (const auto &[id, body] : document.bodies()) {
        const auto found = body->properties.find("ifc.Class");
        if (found != body->properties.end() && std::get<std::string>(found->second) == cls)
            ++n;
    }
    return n;
}
// Volume and area of a body in its own frame (a closed outward triangulation).
std::pair<double, double> volumeArea(const Body &body) {
    double volume = 0, area = 0;
    for (const auto &[id, face] : body.surface.faces) {
        for (const auto &t : body.surface.triangulate(id)) {
            area += 0.5 * length(cross(t.b - t.a, t.c - t.a));
            volume += dot(t.a, cross(t.b, t.c)) / 6;
        }
    }
    return {volume, area};
}
IfcPackageMesh quadMesh(std::vector<Vec3> v, std::vector<std::array<std::uint32_t, 3>> t) {
    IfcPackageMesh mesh;
    mesh.vertices = std::move(v);
    mesh.triangles = std::move(t);
    mesh.materials.assign(mesh.triangles.size(), IfcPackageMesh::noMaterial);
    return mesh;
}
// R098.j: a grid of triangles whose heights differ from cell to cell, so no two neighbours are coplanar: a curved
// surface, where every triangle is its own face. Each mesh is moved by `m` so the bodies do not overlap.
IfcPackageMesh roughGrid(std::uint32_t n, std::uint32_t m) {
    IfcPackageMesh mesh;
    for (std::uint32_t j = 0; j <= n; ++j)
        for (std::uint32_t i = 0; i <= n; ++i)
            mesh.vertices.push_back({10.0 * m + i, double(j), 0.05 * std::sin(0.9 * i + m) * std::cos(0.7 * j)});
    const auto at = [n](std::uint32_t i, std::uint32_t j) { return j * (n + 1) + i; };
    for (std::uint32_t j = 0; j < n; ++j)
        for (std::uint32_t i = 0; i < n; ++i) {
            mesh.triangles.push_back({at(i, j), at(i + 1, j), at(i + 1, j + 1)});
            mesh.triangles.push_back({at(i, j), at(i + 1, j + 1), at(i, j + 1)});
        }
    mesh.materials.assign(mesh.triangles.size(), IfcPackageMesh::noMaterial);
    return mesh;
}

// The document's encoding with its random identity fixed, and its SHA-256: the imported model, byte for byte.
std::string encodingHash(const Document &document) {
    auto json = QJsonDocument::fromJson(encodeDocument(document)).object();
    json["documentId"] = "00000000000000000000000000000001";
    return QCryptographicHash::hash(QJsonDocument(json).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex().toStdString();
}

IfcPackage withMesh(IfcPackageMesh mesh) {
    IfcPackage p;
    p.meshes = {std::move(mesh)};
    p.nodes = {node(1, "IfcProject", "P", -1, IfcNodeRole::Project), node(2, "IfcBuildingElementProxy", "Body", 0, IfcNodeRole::Element, 0)};
    return p;
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        // ---- The sample package: tree, tags, shared geometry, properties, one edit ------------------
        {
            const auto result = import(samplePackage());
            const auto &doc = result.document;
            check(doc.bodies().size() > 0, "bodies imported");
            const auto *a = byStep(doc, 10), *b = byStep(doc, 11), *space = byStep(doc, 12), *storey = byStep(doc, 3);
            check(a && b && space && storey, "every node has a body with its step");
            // Two walls share one mesh: one component definition, two placements.
            check(doc.definitions().size() == 1 && doc.instances().size() == 2, "a shared mesh is one definition with two placements");
            check(a->kind == BodyKind::Group && b->kind == BodyKind::Group, "a shared-mesh element is a group holding its placement");
            // Tags: one per class family; the space tag starts hidden.
            std::map<std::string, bool> tagVisible;
            for (const auto &[id, tag] : doc.tags())
                tagVisible[tag->name] = tag->visible;
            check(tagVisible.size() == 2 && tagVisible.at("IFC Wall") && !tagVisible.at("IFC Space"), "tags: IFC Wall visible, IFC Space hidden");
            check(a->tag == b->tag && a->tag != space->tag && storey->tag == 0, "tags per class; spatial groups are untagged");
            check(std::get<std::string>(a->properties.at("Pset_WallCommon.FireRating")) == "EI60" &&
                      std::get<bool>(a->properties.at("Pset_WallCommon.LoadBearing")) &&
                      std::get<double>(a->properties.at("Qto_WallBaseQuantities.Length")) == 4.5,
                  "properties keep their types");
            check(std::get<std::string>(a->properties.at("ifc.Class")) == "IfcWall", "ifc.Class");
            // Placement: the storey is 3 m up, the second wall 5 m along its storey.
            near(storey->transform.m[14], 3, 1e-12, "storey translation (column-major)");
            near(doc.worldTransform(b->id).m[12], 5, 1e-12, "wall B world x");
            near(doc.worldTransform(b->id).m[14], 3, 1e-12, "wall B world z");
            // One edit: a single undo empties the document.
            check(doc.history().total == 1, "the import is one history entry");
            Document undone = result.document;
            undone.undo();
            check(undone.bodies().empty() && undone.tags().empty() && undone.materials().empty(), "one undo removes everything");
            check(result.report["format"] == "IFC" && result.report["apiVersion"] == 1 && result.report["direction"] == "import", "report header");
            check(result.report["georeference"].toObject()["mode"] == "keepAxes", "project axes are kept");
            // Full serialization validates and survives save and reopen.
            const auto container = encodeContainer(doc);
            check(encodeContainer(decodeContainer(container)) == container, "save and reopen is bit-exact");
        }

        // ---- Georeferencing (ADR 0171 section 3.6): the worker's block is data ----------------------------------------
        {
            const std::string digest(64, 'a');
            const auto block = [](const QString &result, double e, double n, double h, double rotationDegrees, double applied) {
                return QJsonObject{{"mode", "x"},
                                   {"result", result},
                                   {"mapConversion", QJsonObject{{"eastings", e},
                                                                 {"northings", n},
                                                                 {"orthogonalHeight", h},
                                                                 {"xAxisAbscissa", std::cos(rotationDegrees * M_PI / 180)},
                                                                 {"xAxisOrdinate", std::sin(rotationDegrees * M_PI / 180)},
                                                                 {"rotationDegrees", rotationDegrees},
                                                                 {"scale", 0.001}}},
                                   {"crs", QJsonObject{{"name", "EPSG:32633"}, {"mapUnit", "METRE"}, {"mapUnitMetres", 1}, {"mapUnitGiven", true}}},
                                   {"siteOrigin", QJsonObject{{"easting", e}, {"northing", n}, {"elevation", h}, {"crs", "EPSG:32633"}}},
                                   {"rotationAppliedDegrees", applied}};
            };
            const auto withBlock = [&](QJsonObject georeference, IfcGeoreference mode, const std::array<double, 16> &root = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}) {
                auto package = samplePackage();
                if (!georeference.isEmpty())
                    package.report["georeference"] = georeference;
                package.nodes[0].transform = root;
                IfcImportOptions options;
                options.georeference = mode;
                IfcRunFacts facts;
                facts.sha256 = QString::fromStdString(digest);
                facts.bytes = 1;
                return importIfcPackage(package, options, facts);
            };
            const auto version = [](const Document &document) { return QJsonDocument::fromJson(encodeDocument(document)).object()["version"].toInt(); };
            const auto codes = [](const IfcImport &imported) {
                std::set<QString> out;
                for (const auto &n : imported.report["notices"].toArray())
                    out.insert(n.toObject()["code"].toString());
                return out;
            };
            // No rotation: the site origin is set from the conversion's own numbers.
            {
                const auto set = withBlock(block("siteOrigin", 500000.5, 4000000.25, 12.5, 0, 0), IfcGeoreference::KeepAxes);
                check(set.document.site().has_value(), "georeference: the site origin is set");
                const auto &site = *set.document.site();
                check(site.easting == 500000.5 && site.northing == 4000000.25 && site.elevation == 12.5 && site.crs == "EPSG:32633" &&
                          site.established.source == "IfcMapConversion" && site.established.sha256 == digest,
                      "georeference: the origin, label and provenance");
                check(version(set.document) == 29, "georeference: schema 29");
                check(decodeDocument(encodeDocument(set.document)).site() == site, "georeference: the origin survives encode and decode");
                const auto report = set.report["georeference"].toObject();
                check(report["siteOriginSet"].toBool() && report["documentSchema"] == 29 && report["siteOrigin"].toObject()["source"] == "IfcMapConversion" &&
                          codes(set).contains("siteOriginSet"),
                      "georeference: the report says so");
                Document undone = set.document;
                undone.undo();
                check(!undone.site().has_value() && undone.bodies().empty(), "georeference: one undo removes the origin too");
            }
            // No block (or none): nothing is set and the schema stays low.
            {
                const auto none = withBlock({}, IfcGeoreference::Grid);
                check(!none.document.site() && version(none.document) < 29 && none.report["georeference"].toObject()["result"] == "none" &&
                          none.report["georeference"].toObject()["mode"] == "grid",
                      "georeference: a package without a block sets nothing");
                const auto kept = withBlock(QJsonObject{{"result", "notRepresentable"}, {"mode", "keepAxes"}}, IfcGeoreference::KeepAxes);
                check(!kept.document.site() && !kept.report["georeference"].toObject()["siteOriginSet"].toBool(), "georeference: not representable sets nothing");
            }
            // Grid: the root carries the rotation, and the application checks it against the formula.
            {
                const double theta = 53.13010235415598;
                const std::array<double, 16> turn{0.6, -0.8, 0, 0, 0.8, 0.6, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
                const auto grid = withBlock(block("gridAligned", 1000.5, 2000.25, 3.5, theta, theta), IfcGeoreference::Grid, turn);
                check(grid.document.site().has_value() && codes(grid).contains("siteOriginSet"), "georeference: grid sets the origin");
                const auto *b = byStep(grid.document, 11);
                // Wall B is 5 m along its storey (x), 3 m up: turned by (0.6, 0.8) it is at (3, 4, 3).
                near(grid.document.worldTransform(b->id).m[12], 3.0, 1e-12, "grid: wall B world x");
                near(grid.document.worldTransform(b->id).m[13], 4.0, 1e-12, "grid: wall B world y");
                near(grid.document.worldTransform(b->id).m[14], 3.0, 1e-12, "grid: wall B world z");
                near(grid.report["georeference"].toObject()["rotationAppliedDegrees"].toDouble(), theta, 1e-12, "grid: the rotation applied");
                refused("georeferenceMismatch", [&] { (void)withBlock(block("gridAligned", 1000.5, 2000.25, 3.5, theta, theta), IfcGeoreference::KeepAxes, turn); });
                refused("georeferenceMismatch", [&] { (void)withBlock(block("gridAligned", 1000.5, 2000.25, 3.5, theta, theta), IfcGeoreference::Grid); });
                refused("georeferenceMismatch", [&] { (void)withBlock(block("gridAligned", 1000.5, 2000.25, 3.5, theta, theta + 1), IfcGeoreference::Grid, turn); });
                refused("georeferenceMismatch", [&] { (void)withBlock(block("siteOrigin", 1000.5, 2000.25, 3.5, 0, 0), IfcGeoreference::Grid, turn); });
            }
            // A worker that contradicts itself fails the import rather than guessing.
            {
                auto moved = block("siteOrigin", 1000.5, 2000.25, 3.5, 0, 0);
                moved["siteOrigin"] = QJsonObject{{"easting", 1001.5}, {"northing", 2000.25}, {"elevation", 3.5}, {"crs", "EPSG:32633"}};
                refused("georeferenceMismatch", [&] { (void)withBlock(moved, IfcGeoreference::KeepAxes); });
                auto bare = block("siteOrigin", 1000.5, 2000.25, 3.5, 0, 0);
                bare.remove("mapConversion");
                refused("georeferenceMismatch", [&] { (void)withBlock(bare, IfcGeoreference::KeepAxes); });
                refused("georeferenceMismatch", [&] { (void)withBlock(block("siteOrigin", 2e8, 0, 0, 0, 0), IfcGeoreference::KeepAxes); });
                refused("georeferenceMismatch", [&] { (void)withBlock(block("siteOrigin", 1000.5, 2000.25, 3.5, 0, 90), IfcGeoreference::KeepAxes); });
                // The formula itself is checked, not only the worker's agreement with itself: a rotated conversion that was not
                // turned, a turn for a conversion that has none, reasons beside an origin, a scale that is not the project unit.
                refused("georeferenceMismatch", [&] { (void)withBlock(block("siteOrigin", 1000.5, 2000.25, 3.5, 90, 0), IfcGeoreference::KeepAxes); });
                refused("georeferenceMismatch", [&] { (void)withBlock(block("siteOrigin", 1000.5, 2000.25, 3.5, 90, 0), IfcGeoreference::Grid); });
                refused("georeferenceMismatch", [&] { (void)withBlock(block("gridAligned", 1000.5, 2000.25, 3.5, 0, 0), IfcGeoreference::Grid); });
                auto reasoned = block("siteOrigin", 1000.5, 2000.25, 3.5, 0, 0);
                reasoned["reasons"] = QJsonArray{QJsonObject{{"code", "mapUnit"}, {"message", "x"}}};
                refused("georeferenceMismatch", [&] { (void)withBlock(reasoned, IfcGeoreference::KeepAxes); });
                auto scaled = block("siteOrigin", 1000.5, 2000.25, 3.5, 0, 0);
                auto conversion = scaled["mapConversion"].toObject();
                conversion["scale"] = 1.0;
                scaled["mapConversion"] = conversion;
                refused("georeferenceMismatch", [&] { (void)withBlock(scaled, IfcGeoreference::KeepAxes); });
                auto feet = block("siteOrigin", 1000.5, 2000.25, 3.5, 0, 0);
                auto unit = feet["crs"].toObject();
                unit["mapUnitMetres"] = 0.3048;
                feet["crs"] = unit;
                refused("georeferenceMismatch", [&] { (void)withBlock(feet, IfcGeoreference::KeepAxes); });
                auto skewed = block("gridAligned", 1000.5, 2000.25, 3.5, 90, 90);
                auto axis = skewed["mapConversion"].toObject();
                axis["rotationDegrees"] = 80.0;
                skewed["mapConversion"] = axis;
                skewed["rotationAppliedDegrees"] = 80.0;
                refused("georeferenceMismatch", [&] { (void)withBlock(skewed, IfcGeoreference::Grid, {0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}); });
            }
            // Hostile text and sizes are bounded: a label of control characters and 10,000 bytes, 100 reasons.
            {
                auto labelled = block("siteOrigin", 1, 2, 3, 0, 0);
                auto name = labelled["crs"].toObject();
                name["name"] = QString("A\tB\n") + QString(10000, 'x');
                labelled["crs"] = name;
                const auto set = withBlock(labelled, IfcGeoreference::KeepAxes);
                const auto &label = set.document.site()->crs;
                check(label.size() <= 64 && std::none_of(label.begin(), label.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20; }) && label.rfind("A B ", 0) == 0,
                      "georeference: the label is plain text of at most 64 bytes");
                auto hostile = block("notRepresentable", 1, 2, 3, 0, 0);
                auto crs = hostile["crs"].toObject();
                crs["description"] = QString(10000, 'd');
                hostile["crs"] = crs;
                QJsonArray reasons;
                for (int i = 0; i < 100; ++i)
                    reasons.append(QJsonObject{{"code", QString(500, 'c')}, {"message", QString(5000, 'm')}});
                hostile["reasons"] = reasons;
                hostile["reason"] = QString(5000, 'r');
                hostile["extra"] = "ignored";
                const auto result = withBlock(hostile, IfcGeoreference::KeepAxes);
                check(!result.document.site(), "georeference: reasons mean no origin");
                const auto report = result.report["georeference"].toObject();
                check(report["reasons"].toArray().size() == 8 && report["reasons"].toArray()[0].toObject()["message"].toString().size() <= 240 &&
                          report["reasons"].toArray()[0].toObject()["code"].toString().size() <= 64 && report["reason"].toString().size() <= 240 &&
                          report["crs"].toObject()["description"].toString().size() <= 120 && !report.contains("extra"),
                      "georeference: the worker's block is rebuilt from known fields, bounded");
                check(QJsonDocument(result.report).toJson(QJsonDocument::Compact).size() <= 64 * 1024, "georeference: the report stays within 64 KiB");
                const auto strange = withBlock(QJsonObject{{"result", "somethingElse"}}, IfcGeoreference::KeepAxes);
                check(!strange.document.site() && strange.report["georeference"].toObject()["result"] == "notRepresentable", "georeference: an unknown result sets nothing");
            }
        }

        // ---- Spaces -------------------------------------------------------------------------------
        {
            const auto shown = import(samplePackage(), IfcSpaces::Shown);
            bool visible = false;
            for (const auto &[id, tag] : shown.document.tags())
                if (tag->name == "IFC Space")
                    visible = tag->visible;
            check(visible, "spaces shown: the tag is visible");
            const auto skipped = import(samplePackage(), IfcSpaces::Skip);
            check(byStep(skipped.document, 12) == nullptr, "spaces skipped: no space body");
            check(skipped.report["notices"].toArray().size() >= 1, "spaces skipped: reported");
            for (const auto &[id, tag] : skipped.document.tags())
                check(tag->name != "IFC Space", "spaces skipped: no space tag");
        }

        // ---- The planar patch merge ---------------------------------------------------------------
        {
            // A unit cube: 12 triangles become 6 faces, 12 edges; volume and area are exact.
            const auto result = import(withMesh(cubeMesh(1)));
            const auto *body = byStep(result.document, 2);
            check(body && body->surface.faces.size() == 6, "a cube's side is one face, not two triangles");
            const auto [volume, area] = volumeArea(*body);
            near(volume, 1, 1e-12, "cube volume");
            near(area, 6, 1e-12, "cube area");
            const auto geometry = result.report["geometry"].toObject();
            check(geometry["mergedFaces"].toInt() == 6 && geometry["sourceTriangles"].toInt() == 12 && geometry["nonPlanarFaces"].toInt() == 0,
                  "merge counts in the report");
            // No feature edges: every interior edge is soft and smooth; with the cube's twelve edges
            // listed as feature edges they stay visible.
            std::size_t soft = 0;
            for (const auto &[edge, appearance] : body->edgeAppearances)
                soft += appearance.soft && appearance.smooth;
            check(soft == 12, "without feature edges every cube edge is soft");
            auto marked = withMesh(cubeMesh(1));
            for (std::uint32_t i = 0; i < 8; ++i)
                for (std::uint32_t bit : {1u, 2u, 4u})
                    if (!(i & bit))
                        marked.meshes[0].featureEdges.push_back({i, i | bit});
            const auto kept = import(marked);
            check(byStep(kept.document, 2)->edgeAppearances.empty(), "feature edges stay visible");
        }
        {
            // A square with a square hole, as eight triangles in one plane: one face with two loops.
            const std::vector<Vec3> v{{0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {0, 4, 0}, {1, 1, 0}, {3, 1, 0}, {3, 3, 0}, {1, 3, 0}};
            const auto mesh = quadMesh(v, {{0, 1, 5}, {0, 5, 4}, {1, 2, 6}, {1, 6, 5}, {2, 3, 7}, {2, 7, 6}, {3, 0, 4}, {3, 4, 7}});
            const auto result = import(withMesh(mesh));
            const auto *body = byStep(result.document, 2);
            check(body && body->surface.faces.size() == 1 && body->surface.faces.begin()->second.loops.size() == 2,
                  "a plane with a hole is one face with a hole");
            near(result.document.worldArea(body->id, body->surface.faces.begin()->first), 12, 1e-9, "area with the hole subtracted");
        }
        {
            // Two coplanar triangles whose shared edge is bent by 1e-5 m stay triangles and are counted.
            const auto mesh = quadMesh({{0, 0, 0}, {1, 0, 0}, {1, 1, 1e-5}, {0, 1, 0}}, {{0, 1, 2}, {0, 2, 3}});
            const auto result = import(withMesh(mesh));
            const auto *body = byStep(result.document, 2);
            check(body->surface.faces.size() == 2, "a bent quad stays two triangles");
            check(result.report["geometry"].toObject()["nonPlanarFaces"].toInt() >= 1, "and is counted");
        }
        {
            // Vertices within 1e-8 m weld; a triangle that collapses is dropped and counted.
            const auto mesh = quadMesh({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1e-8, 0, 0}}, {{0, 1, 2}, {3, 1, 2}, {0, 3, 1}});
            const auto result = import(withMesh(mesh));
            const auto *body = byStep(result.document, 2);
            check(body->surface.vertices.size() == 3, "near vertices weld");
            check(result.report["geometry"].toObject()["degenerateTriangles"].toInt() == 1, "the collapsed triangle is counted");
        }
        {
            // Colours: a face with its own colour keeps it, and does not merge with its neighbours.
            auto mesh = cubeMesh(1);
            mesh.colors.assign(mesh.triangles.size(), std::nullopt);
            mesh.colors[0] = std::array<float, 3>{1, 0, 0};
            mesh.colors[1] = std::array<float, 3>{1, 0, 0};
            const auto result = import(withMesh(mesh));
            const auto *body = byStep(result.document, 2);
            check(body->faceColors.size() == 1 && body->faceColors.begin()->second[0] == 1.0f, "a coloured side keeps its colour");
            check(body->surface.faces.size() == 6, "the coloured pair still merges");
        }
        {
            // Two materials: the body's default is the common one; the other side keeps its own.
            auto p = withMesh(cubeMesh(1, 0));
            p.materials = {{"Red", {1, 0, 0}, 1}, {"Blue", {0, 0, 1}, 0.5f}};
            p.meshes[0].materials[0] = p.meshes[0].materials[1] = 1;
            const auto result = import(p);
            const auto *body = byStep(result.document, 2);
            check(result.document.materials().size() == 2, "both materials exist");
            check(body->materials.front != 0 && body->faceMaterials.size() == 1, "default material plus one override");
            const auto &override = body->faceMaterials.begin()->second;
            check(result.document.materials().at(override.front)->name == "Blue" &&
                      std::abs(result.document.materials().at(override.front)->opacity - 0.5f) < 1e-6,
                  "the overriding side is the blue, half-transparent one");
        }

        // ---- Properties and their caps ------------------------------------------------------------
        {
            auto p = samplePackage();
            auto &wall = p.nodes[3];
            wall.properties.clear();
            wall.provenance.clear();
            for (int i = 0; i < 20; ++i)
                wall.provenance.emplace_back("ifc.P" + std::to_string(i), double(i));
            for (int i = 0; i < 200; ++i)
                wall.properties.push_back({"Pset_" + std::to_string(i % 3), "N" + std::to_string(i), 0, double(i)});
            wall.properties.push_back({"Pset_Long", std::string(300, 'k'), 0, std::string(3000, 't')});
            const auto result = import(p);
            const auto *body = byStep(result.document, 10);
            // The wall has no ifc.Step any more; find it by the first provenance key.
            const Body *wallBody = nullptr;
            for (const auto &[id, candidate] : result.document.bodies())
                if (candidate->properties.contains("ifc.P0"))
                    wallBody = candidate.get();
            check(body == nullptr && wallBody != nullptr, "wall found by provenance");
            check(wallBody->properties.size() == 128, "at most 128 properties per body");
            std::size_t provenance = 0;
            for (const auto &[key, value] : wallBody->properties)
                provenance += key.rfind("ifc.", 0) == 0;
            check(provenance == 16, "provenance takes at most 16 slots");
            const auto dropped = result.report["properties"].toObject()["dropped"].toObject();
            check(dropped["ifc"].toInt() == 4, "dropped provenance is counted");
            long long total = 0;
            for (auto it = dropped.begin(); it != dropped.end(); ++it)
                total += it.value().toInt();
            check(total == 4 + (201 - 112), "every dropped property is counted by set");
            // Order: occurrence properties in the order given; the later ones are the ones dropped.
            check(wallBody->properties.contains("Pset_0.N0") && !wallBody->properties.contains("Pset_0.N198"), "the first properties are kept");
        }
        {
            // Long keys and texts are shortened, with counts.
            auto p = samplePackage();
            p.nodes[3].properties = {{"Pset_Long", std::string(300, 'k'), 0, std::string(3000, 't')}};
            const auto result = import(p);
            const auto *wall = byStep(result.document, 10);
            bool key = false, text = false;
            for (const auto &[k, v] : wall->properties) {
                if (k.size() == 128 && k.rfind("Pset_Long.", 0) == 0)
                    key = true;
                if (const auto *t = std::get_if<std::string>(&v))
                    if (t->size() == 2048)
                        text = true;
            }
            check(key && text, "a 300-byte key and a 3,000-byte text are shortened");
        }
        {
            // The aggregate cap: 8 MiB of property text drops type-level before occurrence-level, quantities
            // before properties, deterministically, and counts the drops.
            IfcPackage p;
            p.nodes.push_back(node(1, "IfcProject", "P", -1, IfcNodeRole::Project));
            for (int i = 0; i < 70; ++i) {
                auto n = node(10 + i, "IfcGroup", "g", 0, IfcNodeRole::Spatial);
                // The worker writes occurrence properties, then type properties (62 of the latter fit the 128).
                for (int k = 0; k < 64; ++k)
                    n.properties.push_back({"Pset", "occ" + std::to_string(k), 0, std::string(1000, 'a')});
                for (int k = 0; k < 64; ++k)
                    n.properties.push_back({"Qto", "typ" + std::to_string(k), 2, std::string(1000, 'b')});
                p.nodes.push_back(std::move(n));
            }
            const auto result = import(p);
            std::size_t bytes = 0, typeKept = 0, occurrenceKept = 0;
            for (const auto &[id, body] : result.document.bodies())
                for (const auto &[k, v] : body->properties)
                    if (k.rfind("ifc.", 0) != 0) {
                        bytes += k.size() + std::get<std::string>(v).size();
                        (k.find("typ") != std::string::npos ? typeKept : occurrenceKept)++;
                    }
            check(bytes <= 8 * 1024 * 1024, "property text stays within 8 MiB");
            check(typeKept < occurrenceKept && occurrenceKept == 70 * 64, "type-level properties are dropped first");
            check(result.report["properties"].toObject()["droppedTotal"].toInt() > 0, "the drops are counted");
            const auto again = import(p);
            // The same package converts to the same bodies with the same properties (compared by content: the ids of a
            // fresh document do not enter into it).
            const auto fingerprint = [](const Document &document) {
                std::vector<std::string> lines;
                for (const auto &[id, body] : document.bodies()) {
                    std::string line = body->name + "|" + std::to_string(body->properties.size());
                    for (const auto &[k, v] : body->properties)
                        line += "|" + k + "=" + (std::get_if<std::string>(&v) ? *std::get_if<std::string>(&v) : std::string("<non-text>"));
                    lines.push_back(std::move(line));
                }
                std::sort(lines.begin(), lines.end());
                return lines;
            };
            check(fingerprint(again.document) == fingerprint(result.document), "the same package converts to the same bodies and properties");
        }
        {
            // The model chunk (96 MiB less a 2 MiB margin since R110, which raised it from 32 MiB): provenance
            // over the chunk gives way, as properties do, so the document fits. 6,600 nodes of 16 KB of
            // provenance are about 105 MB, and the import keeps the chunk within its bound.
            IfcPackage p;
            p.nodes.push_back(node(1, "IfcProject", "P", -1, IfcNodeRole::Project));
            for (int i = 0; i < 6600; ++i) {
                auto n = node(10 + i, "IfcGroup", "g", 0, IfcNodeRole::Spatial);
                for (int k = 0; k < 16; ++k)
                    n.provenance.emplace_back("ifc.X" + std::to_string(k), std::string(1000, 'p'));
                p.nodes.push_back(std::move(n));
            }
            const auto trimmed = import(p);
            check(encodeDocument(trimmed.document).size() <= qsizetype(NativeLimits::modelBytes), "the provenance-heavy model fits the model chunk");
            IfcPackage q;
            q.nodes.push_back(node(1, "IfcProject", "P", -1, IfcNodeRole::Project));
            for (int i = 0; i < 6000; ++i) {
                auto n = node(10 + i, "IfcGroup", "g", 0, IfcNodeRole::Spatial);
                for (int k = 0; k < 16; ++k)
                    n.provenance.emplace_back("ifc.X" + std::to_string(k), std::string(1000, 'p'));
                for (int k = 0; k < 4; ++k)
                    n.properties.push_back({"Pset", "p" + std::to_string(k), 0, std::string(1100, 'q')});
                q.nodes.push_back(std::move(n));
            }
            const auto result = import(q);
            check(encodeDocument(result.document).size() <= qsizetype(NativeLimits::modelBytes), "the document fits the model chunk");
            check(result.report["properties"].toObject()["droppedTotal"].toInt() > 0, "properties gave way to the chunk");
        }

        // ---- Materials, names, shared geometry, limits --------------------------------------------
        {
            auto p = withMesh(cubeMesh(1, 0));
            p.materials.clear();
            for (int i = 0; i < 1100; ++i)
                p.materials.push_back({"M", {0.5f, 0.5f, 0.5f}, 1});
            // Each triangle its own material index: 12 triangles, indices spread over the table.
            for (std::size_t t = 0; t < p.meshes[0].materials.size(); ++t)
                p.meshes[0].materials[t] = std::uint32_t(1090 + t % 10);
            const auto result = import(p);
            check(result.document.materials().size() <= 10, "only the used materials are created");
            std::set<std::string> names;
            for (const auto &[id, m] : result.document.materials())
                names.insert(m->name);
            check(names.size() == result.document.materials().size(), "material names are unique");
        }
        {
            IfcPackage p;
            p.nodes.push_back(node(1, "IfcProject", "P", -1, IfcNodeRole::Project));
            p.nodes.push_back(node(2, "IfcWall", "tab\tname\nbreak", 0, IfcNodeRole::Element));
            p.nodes.push_back(node(3, "IfcWall", std::string(300, 'n').c_str(), 0, IfcNodeRole::Element));
            const auto result = import(p);
            check(byStep(result.document, 2)->name == "tab name break", "control characters become spaces");
            check(byStep(result.document, 3)->name.size() == 256, "names are shortened to 256 bytes");
        }
        {
            // More than 1,024 shared meshes: the rest are expanded and reported.
            IfcPackage p;
            p.nodes.push_back(node(1, "IfcProject", "P", -1, IfcNodeRole::Project));
            for (int m = 0; m < 1030; ++m) {
                p.meshes.push_back(cubeMesh(1 + m * 1e-3));
                for (int k = 0; k < 2; ++k)
                    p.nodes.push_back(node(10 + unsigned(m * 2 + k), "IfcBuildingElementProxy", "e", 0, IfcNodeRole::Element, m));
            }
            const auto result = import(p);
            check(result.document.definitions().size() == 1024, "at most 1,024 component definitions");
            bool noted = false;
            for (const auto &n : result.report["notices"].toArray())
                noted = noted || n.toObject()["code"] == "definitionsOverLimit";
            check(noted, "the expansion is reported");
        }
        {
            // The document limits: the 10,001st unique body is not imported, the rest are, and it is said.
            IfcPackage p;
            p.meshes = {cubeMesh(1)};
            p.nodes.push_back(node(1, "IfcProject", "P", -1, IfcNodeRole::Project));
            for (unsigned i = 0; i < 10001; ++i) {
                auto n = node(10 + i, "IfcBuildingElementProxy", "e", 0, IfcNodeRole::Element, i == 0 ? 0 : -1);
                p.nodes.push_back(n);
            }
            const auto result = import(p);
            check(result.document.bodies().size() == 10000, "10,000 bodies fit, the 10,001st does not");
            const auto elements = result.report["elements"].toObject()["notImported"].toArray();
            check(elements.size() == 1 && elements[0].toObject()["reason"].toString().startsWith("documentLimit:") &&
                      elements[0].toObject()["count"].toInt() == 2,
                  "the elements beyond the limit are reported with the limit named");
        }
        {
            // A body over the per-body limit (100,000 faces) is not imported; the rest are. 100,001 triangles
            // that share no plane (each tilted differently) cannot merge.
            IfcPackageMesh big;
            for (std::uint32_t i = 0; i < 100001; ++i) {
                const double x = double(i % 300) * 3, y = double(i / 300) * 3, tilt = 1e-3 + 1e-6 * i;
                big.vertices.push_back({x, y, 0});
                big.vertices.push_back({x + 1, y, tilt});
                big.vertices.push_back({x, y + 1, 0});
                big.triangles.push_back({3 * i, 3 * i + 1, 3 * i + 2});
            }
            big.materials.assign(big.triangles.size(), IfcPackageMesh::noMaterial);
            IfcPackage p;
            p.meshes = {big, cubeMesh(1)};
            p.nodes.push_back(node(1, "IfcProject", "P", -1, IfcNodeRole::Project));
            p.nodes.push_back(node(2, "IfcBuildingElementProxy", "big", 0, IfcNodeRole::Element, 0));
            p.nodes.push_back(node(3, "IfcBuildingElementProxy", "cube", 0, IfcNodeRole::Element, 1));
            const auto result = import(p);
            check(byStep(result.document, 2) == nullptr && byStep(result.document, 3) != nullptr, "the body over the limit is left out, the cube stays");
            const auto elements = result.report["elements"].toObject()["notImported"].toArray();
            check(elements.size() == 1 && elements[0].toObject()["reason"] == "bodyLimit", "and is reported with the limit");
        }

        {
            // R098.j: the output of the conversion is pinned. Thirty-one curved bodies of 9,800 faces: the document takes
            // the first ones up to its 300,000 unique faces (R110), and the rest are refused with the limit named. The sample package (merged
            // faces, edges, materials, colours and a shared mesh) must come out the same too.
            IfcPackage rough;
            rough.nodes.push_back(node(1, "IfcProject", "P", -1, IfcNodeRole::Project));
            for (std::uint32_t m = 0; m < 31; ++m) {
                rough.meshes.push_back(roughGrid(70, m));
                rough.nodes.push_back(node(2 + m, "IfcBuildingElementProxy", "rough", 0, IfcNodeRole::Element, m));
            }
            const auto roughResult = import(rough);
            std::int64_t accepted = 0;
            for (std::uint32_t m = 0; m < 31; ++m)
                accepted += byStep(roughResult.document, 2 + m) != nullptr;
            const auto refusals = roughResult.report["elements"].toObject()["notImported"].toArray();
            check(accepted == 30 && refusals.size() == 1 && refusals[0].toObject()["reason"].toString() == "documentLimit:faces" &&
                      refusals[0].toObject()["count"].toInteger() == 1,
                  "thirty bodies (294,000 faces) fit, and the one after them is refused with the face limit named");
            const std::string roughHash = encodingHash(roughResult.document);
            const std::string sampleHash = encodingHash(import(samplePackage()).document);
            const std::string pinnedRough = "db291571713ba7ed1aaf345d9c45c41902d87dfb186ca20f61cc4080a337bb46",
                              pinnedSample = "5b7ea80f1a6205a09e04013afa5bacb3d491fedc26173e3566916b5648df7f8c";
            if (pinnedRough != roughHash || pinnedSample != sampleHash)
                throw std::runtime_error("pinned encodings differ: rough " + roughHash + ", sample " + sampleHash);
        }

        // ---- Refusals and transforms --------------------------------------------------------------
        refused("nothingToImport", [&] { import(IfcPackage{}); });
        {
            auto p = samplePackage();
            p.nodes[3].transform = translation(2e6, 0, 0);
            refused("farFromOrigin", [&] { import(p); });
        }
        {
            // A rotation about Z by 90 degrees and a translation, row-major as the package states it.
            IfcPackage p = withMesh(cubeMesh(1));
            p.nodes[1].transform = {0, -1, 0, 5, 1, 0, 0, 6, 0, 0, 1, 7, 0, 0, 0, 1};
            const auto result = import(p);
            const auto *body = byStep(result.document, 2);
            const auto triangles = result.document.worldTriangles(body->id);
            double lo[3] = {1e9, 1e9, 1e9}, hi[3] = {-1e9, -1e9, -1e9};
            for (const auto &t : triangles)
                for (const auto point : {t.a, t.b, t.c}) {
                    lo[0] = std::min(lo[0], point.x), hi[0] = std::max(hi[0], point.x);
                    lo[1] = std::min(lo[1], point.y), hi[1] = std::max(hi[1], point.y);
                    lo[2] = std::min(lo[2], point.z), hi[2] = std::max(hi[2], point.z);
                }
            // x' = -y + 5, y' = x + 6, z' = z + 7 maps [0,1]^3 to x in [4,5], y in [6,7], z in [7,8].
            near(lo[0], 4, 1e-9, "world x min");
            near(hi[0], 5, 1e-9, "world x max");
            near(lo[1], 6, 1e-9, "world y min");
            near(hi[1], 7, 1e-9, "world y max");
            near(lo[2], 7, 1e-9, "world z min");
            near(hi[2], 8, 1e-9, "world z max");
        }
        {
            // Hidden elements (an opening, a layer that is off) keep their tag and are hidden.
            IfcPackage p = withMesh(cubeMesh(1));
            p.nodes[1].role = IfcNodeRole::Opening;
            p.nodes[1].className = "IfcOpeningElement";
            p.nodes[1].hidden = true;
            const auto result = import(p);
            const auto *body = byStep(result.document, 2);
            check(body->hidden && result.document.tags().at(body->tag)->name == "IFC Opening" && result.document.tags().at(body->tag)->visible,
                  "an opening not subtracted is hidden on the visible tag IFC Opening");
        }
        {
            // Tag names: one per class family.
            check(ifcTagName("IfcWall") == "IFC Wall" && ifcTagName("IfcWallStandardCase") == "IFC Wall", "wall tag");
            check(ifcTagName("IfcBuildingElementProxy") == "IFC Building Element Proxy", "proxy tag");
            check(ifcTagName("IfcHVACVirtualElement") == "IFC HVAC Virtual Element", "acronym tag");
            // More than 128 classes: the rest share IFC Other.
            IfcPackage p;
            p.nodes.push_back(node(1, "IfcProject", "P", -1, IfcNodeRole::Project));
            for (int i = 0; i < 140; ++i)
                p.nodes.push_back(node(10 + unsigned(i), ("IfcThing" + std::string(1, char('a' + i % 26)) + std::to_string(i)).c_str(), "x", 0, IfcNodeRole::Element));
            // A space and an opening after the family tags ran out keep their own tags (and do not recurse).
            p.nodes.push_back(node(900, "IfcSpace", "room", 0, IfcNodeRole::Space));
            p.nodes.push_back(node(901, "IfcOpeningElement", "hole", 0, IfcNodeRole::Opening));
            for (int i = 0; i < 12; ++i)
                p.nodes.push_back(node(1000 + unsigned(i), ("IfcMore" + std::to_string(i)).c_str(), "y", 0, IfcNodeRole::Element));
            const auto result = import(p);
            std::set<std::string> names;
            for (const auto &[id, tag] : result.document.tags())
                names.insert(tag->name);
            check(names.contains("IFC Other") && names.size() <= 131, "class tags are bounded; the rest are IFC Other");
            check(names.contains("IFC Space") && names.contains("IFC Opening"), "and the space and opening tags are their own");
        }
        {
            // The report is bounded: 64 KiB, 64 kinds, 32 locations per kind, 256-byte messages.
            auto p = samplePackage();
            QJsonArray locations;
            for (int i = 0; i < 40; ++i)
                locations.append(QJsonObject{{"kind", "notImported"}, {"step", i}, {"globalId", "g"}, {"message", QString(400, 'm')}});
            QJsonArray notices;
            for (int i = 0; i < 100; ++i)
                notices.append(QJsonObject{{"code", QString("code%1").arg(i)}, {"count", 1}});
            p.report["locations"] = locations;
            p.report["notices"] = notices;
            const auto result = import(p);
            check(result.report["locations"].toArray().size() == 32, "locations: 32 per kind");
            check(result.report["notices"].toArray().size() <= 64, "at most 64 kinds of notice");
            for (const auto &entry : result.report["locations"].toArray())
                check(entry.toObject()["message"].toString().toUtf8().size() <= 256, "messages: 256 bytes");
            check(QJsonDocument(result.report).toJson(QJsonDocument::Compact).size() <= 64 * 1024, "report: 64 KiB");
            long long total = 0;
            for (const auto &n : result.report["notices"].toArray())
                total += n.toObject()["count"].toInteger();
            check(total >= 100, "counts stay complete when kinds are merged");
        }
        {
            // A hostile report (a helper's words are data): thousands of kinds, long names, a huge excluded list.
            auto p = samplePackage();
            QJsonObject omitted, imported, losses, totals;
            for (int i = 0; i < 3000; ++i) {
                omitted[QString("Ifc%1").arg(i, 6, 10, QChar('0')) + QString(300, 'k')] = 1;
                imported[QString("IfcClass%1").arg(i)] = 2;
                losses[QString("loss%1").arg(i)] = 3;
                totals[QString("kind%1").arg(i)] = 4;
            }
            QJsonArray notImported, excluded;
            for (int i = 0; i < 3000; ++i) {
                notImported.append(QJsonObject{{"class", QString("IfcC%1").arg(i)}, {"reason", QString(500, 'r')}, {"count", 5}});
                excluded.append(QJsonObject{{"step", i}, {"reason", "crash"}});
            }
            p.report["omittedEntities"] = omitted;
            p.report["losses"] = losses;
            p.report["locationTotals"] = totals;
            auto elements = p.report["elements"].toObject();
            elements["imported"] = imported;
            elements["notImported"] = notImported;
            p.report["elements"] = elements;
            p.report["excluded"] = excluded;
            const auto result = import(p);
            check(QJsonDocument(result.report).toJson(QJsonDocument::Compact).size() <= 64 * 1024, "a hostile report stays within 64 KiB");
            check(result.report["omittedEntities"].toObject().size() <= 64 && result.report["losses"].toObject().size() <= 64, "tables of counts: 64 keys");
            check(result.report["elements"].toObject()["notImported"].toArray().size() <= 64, "lists of kinds: 64 entries");
            long long total = 0;
            for (const auto &v : result.report["omittedEntities"].toObject())
                total += v.toInteger();
            check(total == 3000, "and the counts stay complete under 'other'");
            long long lossTotal = 0, kindTotal = 0;
            for (const auto &v : result.report["losses"].toObject())
                lossTotal += v.toInteger();
            for (const auto &v : result.report["locationTotals"].toObject())
                kindTotal += v.toInteger();
            check(lossTotal == 9000 && kindTotal == 12000, "also for losses and location totals");
        }
        std::cout << "ifc_convert ok\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
