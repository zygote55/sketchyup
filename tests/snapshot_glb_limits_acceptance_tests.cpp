// R112 (1.8.0): the read-snapshot budget (512 MiB) and the GLB visible-face cap (600,000) at the
// published limits, through a catalog import, save and reopen, and a GLB export with everything
// visible. The base has about 85,000 faces in 26 single-placement components, and two saved scenes.
// The catalog has two entries of 207,000 smooth-edged faces in all: entry A holds definition B
// placed twice, and a sphere C; entry D is a sphere. Both entries are imported without exploding
// anything. The model must validate, save and reopen byte for byte, export to GLB, and keep its
// scene records. Its visible triangle count must equal the sum of its triangulated faces. Boundary
// checks: the budget helper accepts the charge and refuses one byte less; the GLB cap accepts the
// visible count and refuses one face less, by name, and a refused export changes nothing; hiding
// bodies lowers the visible count. It is a slow core test (ADR 0170), and the fast tier runs it only
// when the core group is selected.
#include "core/components.hpp"
#include "core/document_limits.hpp"
#include "core/groups.hpp"
#include "core/scenes.hpp"
#include "core/selection.hpp"
#include "geometry/topology.hpp"
#include "integrations/glb_export.hpp"
#include "integrations/render_snapshot.hpp"
#include "io/catalog_insert.hpp"
#include "io/catalog_io.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <map>
#include <memory>
#include <numbers>
#include <set>
#include <stdexcept>
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
double peakMiB() {
    rusage usage{};
    check(getrusage(RUSAGE_SELF, &usage) == 0, "Read peak RSS");
    return usage.ru_maxrss / 1024.0;
}
// The message of the operation's exception, or empty when it does not throw.
template <class F> std::string refusal(F operation) {
    try {
        operation();
    } catch (const std::exception &error) {
        return error.what();
    }
    return {};
}
// Every edge smooth and soft, as an imported smooth mesh stores them: one appearance record each.
void smoothEdges(Body &body) {
    for (const auto &[edge, record] : body.topology.edges) {
        (void)record;
        body.edgeAppearances[edge] = EdgeAppearance{false, true, true};
    }
}
// A prism of `sides` sides, 1 m high: 2 * sides vertices, 3 * sides edges and sides + 2 faces.
std::shared_ptr<Body> prism(Id id, int sides, double x) {
    auto body = std::make_shared<Body>();
    body->id = id;
    body->name = "Prism " + std::to_string(id);
    auto &surface = body->surface;
    std::vector<Id> bottom, top;
    for (int i = 0; i < sides; ++i) {
        const auto angle = 2 * std::numbers::pi * i / sides;
        const Vec3 p{x + std::cos(angle), 400 + std::sin(angle), 0};
        bottom.push_back(surface.nextId++);
        surface.vertices.emplace(bottom.back(), p);
        top.push_back(surface.nextId++);
        surface.vertices.emplace(top.back(), p + Vec3{0, 0, 1});
    }
    surface.addFaceIds({bottom});
    std::vector<Id> lid(top.rbegin(), top.rend());
    surface.addFaceIds({lid});
    for (int i = 0; i < sides; ++i) {
        const int next = (i + 1) % sides;
        surface.addFaceIds({{bottom[size_t(i)], bottom[size_t(next)], top[size_t(next)],
                             top[size_t(i)]}});
    }
    body->topology = Topology::rebuild(body->surface, body->topology);
    smoothEdges(*body);
    return body;
}
// A closed triangulated sphere: 2 * sides * rings faces, 2 + sides * rings vertices, 1.5 edges a face.
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
// The faces, and the triangles, of every body in the document (placements included).
struct Counts {
    size_t faces{}, triangles{};
};
Counts countAll(const Document &doc) {
    Counts counts;
    for (const auto &[id, body] : doc.bodies()) {
        (void)id;
        for (const auto &[face, record] : body->surface.faces) {
            (void)record;
            ++counts.faces;
            counts.triangles += body->surface.triangulate(face).size();
        }
    }
    return counts;
}
// The faces and triangles of the bodies the render snapshot shows.
Counts countVisible(const Document &doc, const RenderSnapshot &snapshot) {
    Counts counts;
    for (const auto &[id, body] : doc.bodies()) {
        if (!snapshot.visible(id))
            continue;
        for (const auto &[face, record] : body->surface.faces) {
            (void)record;
            ++counts.faces;
            counts.triangles += body->surface.triangulate(face).size();
        }
    }
    return counts;
}
// The base: 26 prisms of 3,267 sides (3,269 faces each, 84,994 in all), each a single-placement
// component, with two saved scenes. The first placement's id is returned for the scenes to name.
Document base(Id &firstInstance) {
    std::map<Id, BodyPtr> bodies;
    for (Id id = 1; id <= 26; ++id)
        bodies.emplace(id, prism(id, 3267, 3.0 * double(id)));
    Document doc;
    doc.restore("00000000000000000000000000000ab1", 27, std::move(bodies));
    for (Id id = 1; id <= 26; ++id) {
        const auto component = createComponent(doc, createGroup(doc, {id}, "Prism " + std::to_string(id)),
                                               "Prism " + std::to_string(id));
        if (id == 1)
            firstInstance = component.instance;
    }
    SceneSnapshot plan, elevation;
    plan.camera = SceneCamera{};
    plan.visibility = SceneVisibility{{{firstInstance, true}}, {}, {}, false};
    elevation = plan;
    elevation.camera->pitch = 0;
    elevation.camera->yaw = 90;
    (void)createScene(doc, "Plan", plan);
    (void)createScene(doc, "Elevation", elevation);
    // Reload, so the import starts with an empty history, as a file opens.
    return decodeContainer(encodeContainer(doc));
}
// The catalog's source: entry A is a group of two placements of definition B (50,000 faces each)
// and a sphere C (50,000 faces), so its definition nests B. Entry D is a sphere of 57,000 faces.
// The catalog holds A and D, and the closure of A brings B in with it. Returns the catalog bytes.
QByteArray catalog() {
    std::map<Id, BodyPtr> bodies;
    bodies.emplace(1, sphere(1, {0, 0, 0}, 200, 125));    // B
    bodies.emplace(2, sphere(2, {60, 0, 0}, 200, 125));   // C
    bodies.emplace(3, sphere(3, {120, 0, 0}, 190, 150));  // D
    Document source;
    source.restore("00000000000000000000000000000ab2", 4, std::move(bodies));
    const auto definitionB = createComponent(source, createGroup(source, {1}, "Sphere B"), "Sphere B");
    const auto second = placeComponent(source, definitionB.definition,
                                       Transform::translation({0, 100, 0}), 0, "Sphere B");
    const auto groupA = createGroup(source, {definitionB.instance, second.instance, 2}, "Entry A");
    const auto entryA = createComponent(source, groupA, "Entry A");
    const auto entryD = createComponent(source, createGroup(source, {3}, "Entry D"), "Entry D");
    CatalogExportRequest request;
    request.name = "entries";
    request.description = "R112 acceptance entries";
    request.version = "1.0.0";
    request.licenseIdentifier = "LicenseRef-Unspecified";
    request.components = {entryA.definition, entryD.definition};
    return exportCatalog(source, request);
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        const auto started = Clock::now();
        check(Document::readSnapshotByteLimit == 512u * 1024 * 1024,
              "The read-snapshot budget is 512 MiB (R112)");
        Id firstInstance = 0;
        Document model = base(firstInstance);
        check(model.materializedTotals().faces == 84994 && model.definitions().size() == 26 &&
                  model.scenes().size() == 2,
              "The base holds 84,994 faces in 26 components and two scenes");
        const auto scenesBefore = model.scenes();
        const auto built = Clock::now();

        // The catalog: entries A and D. A's closure brings B in, so three definitions are added.
        const auto package = decodeCatalog(catalog());
        const auto imported =
            importCatalogEntries(model, package, QStringList{"Entry A", "Entry D"});
        check(model.definitions().size() == 29,
              "Entry A, its nested definition B and entry D are imported: 29 definitions");
        const auto totals = model.materializedTotals();
        check(totals.faces == 291994,
              "The model holds 291,994 materialized faces after the import: " +
                  std::to_string(totals.faces));
        const auto imports = Clock::now();

        // Save, reopen, re-save: byte for byte. The reopened model keeps its totals and scenes.
        const auto saved = encodeContainer(model);
        const auto reopened = decodeContainer(saved);
        check(reopened.materializedTotals() == totals && reopened.definitions().size() == 29,
              "The imported model reopens unchanged");
        check(encodeContainer(reopened) == saved,
              "The imported model re-saves byte for byte");
        for (const auto &[id, scene] : scenesBefore)
            check(model.scenes().contains(id) && *model.scenes().at(id) == *scene &&
                      reopened.scenes().contains(id) && *reopened.scenes().at(id) == *scene,
                  "The import leaves every scene record unchanged");
        const auto reloaded = Clock::now();

        // The snapshot budget: the charge is within it, and one byte less refuses the same charge.
        const auto charge = model.readSnapshotBytes();
        check(withinReadSnapshotBudget(model, charge) &&
                  !withinReadSnapshotBudget(model, charge - 1),
              "The budget accepts the charge and refuses one byte less");
        check(charge <= Document::readSnapshotByteLimit,
              "The imported model is within the 512 MiB read-snapshot budget");

        // GLB at the cap: everything visible. The visible triangle count is the sum of the
        // triangulated faces, and the visible face count is the one the cap counts.
        const auto everything = countAll(model);
        const auto snapshot = RenderSnapshot::capture(model);
        const auto scene = exportGlb(snapshot);
        check(everything.faces == 291994 && everything.faces <= glbVisibleFaceLimit,
              "Every face is visible and within the 600,000-face cap");
        check(scene.manifest["visibleTriangles"].toDouble() == double(everything.triangles),
              "The visible triangle count equals the sum of the triangulated faces: " +
                  std::to_string(everything.triangles));
        check(scene.manifest["hiddenFacesOmitted"].toDouble() == 0 &&
                  scene.manifest["hiddenBodiesOmitted"].toDouble() == 0,
              "Nothing is hidden in the export");
        const auto exported = Clock::now();

        // The cap accepts its visible count and refuses one face less, by name. The refusal
        // changes nothing: the model saves to the same bytes afterwards.
        check(refusal([&] { (void)exportGlb(snapshot, everything.faces); }).empty(),
              "The export is accepted at its visible face count");
        check(refusal([&] { (void)exportGlb(snapshot, everything.faces - 1); }) ==
                  "GLB has too many visible faces",
              "One face under the visible count is refused by name");
        check(encodeContainer(model) == saved, "A refused export changes nothing");

        // Hiding a body lowers the visible count, and the cap follows the visible count.
        SelectionSet hidden;
        hidden.insert({firstInstance, SelectionKind::Body, 0});
        const auto hiddenSnapshot = RenderSnapshot::capture(model, {}, hidden);
        const auto shown = countVisible(model, hiddenSnapshot);
        check(shown.faces < everything.faces && shown.faces > 0,
              "Hiding the first base component lowers the visible count");
        const auto hiddenScene = exportGlb(hiddenSnapshot, shown.faces);
        check(hiddenScene.manifest["visibleTriangles"].toDouble() == double(shown.triangles),
              "The hidden export's triangles are the visible triangles");
        check(refusal([&] { (void)exportGlb(hiddenSnapshot, shown.faces - 1); }) ==
                  "GLB has too many visible faces",
              "The cap counts visible faces, so one under the hidden count is refused");

        rusage usage{};
        getrusage(RUSAGE_SELF, &usage);
        std::printf("R112 acceptance: %zu faces in %zu definitions; charge %.1f MiB of the 512 MiB "
                    "budget; peak RSS %.0f MiB; build %.1f s, import %.1f s, save and reopen "
                    "%.1f s, export %.1f s, total %.1f s\n",
                    size_t(totals.faces), model.definitions().size(),
                    double(charge) / (1024 * 1024), peakMiB(), seconds(started, built),
                    seconds(built, imports), seconds(imports, reloaded),
                    seconds(reloaded, exported), seconds(started, Clock::now()));
        std::cout << "Snapshot and GLB limits acceptance: the at-limit model imports, saves, "
                     "reopens and exports; the budget and the cap refuse one unit over\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
