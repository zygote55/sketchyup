// R094.f (ADR 0166 §§6.2, 6.3): placeLibraryAssembly reuses the in-model definition by
// assembly identity and places, overrides and cuts in one edit; a refusal leaves the
// document unchanged; undo and redo restore each step whole.
#include "automation/commands.hpp"
#include "automation/component_parameter_commands.hpp"
#include "core/component_parameters.hpp"
#include "core/hosted_components.hpp"
#include "geometry/solid.hpp"
#include "io/assembly_source.hpp"
#include "io/component_library.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <cmath>
#include <iostream>
using namespace sketchy;
namespace {
std::size_t checks = 0;
void check(bool value, const std::string &message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
ComponentBundle assembly(const char *slug) {
    QFile file(QString(SKETCHYUP_ASSEMBLY_SOURCES) + "/" + slug + ".json");
    check(file.open(QIODevice::ReadOnly), "Read the assembly source");
    return decodeComponentBundle(buildAssemblyBundle(file.readAll()));
}
struct Wall {
    Document doc{DisplayUnit::Millimeters};
    Id body{}, face{};
    Wall() {
        body = doc.addFace({{{0, 0, 0}, {6, 0, 0}, {6, .2, 0}, {0, .2, 0}}}, "Wall");
        doc.extrude(body, doc.bodies().at(body)->surface.faces.begin()->first, 2.7);
        for (const auto &[id, record] : doc.bodies().at(body)->surface.faces) {
            (void)record;
            if (doc.bodies().at(body)->surface.normal(id).y < -.99)
                face = id;
        }
    }
    double volume() const {
        const auto &b = *doc.bodies().at(body);
        return *analyzeSolidShells(b.surface, b.topology).report.volume;
    }
    AssemblyPlacement at(double x, double z = 1.2) const {
        AssemblyPlacement placement;
        placement.host = AssemblyHostFace{body, face, {x, 0, z}, {1, 0, 0}, 0};
        return placement;
    }
    // The opening's width along the wall, from its cut profile.
    double openingWidth(Id placement) const {
        const auto &opening = doc.hostedComponents().hosts.at(body)->openings.at(placement);
        double low = 1e9, high = -1e9;
        for (const auto &corner : opening.profile.corners) {
            low = std::min(low, corner.point.x);
            high = std::max(high, corner.point.x);
        }
        return high - low;
    }
};
std::string lastLabel(const Document &doc) {
    const auto position = doc.history().position;
    check(position > 0, "History has an applied entry");
    return doc.history(position - 1, 1).entries.at(0).label;
}
QJsonObject batch(const Document &doc, QJsonArray commands, const QString &label = {}) {
    QJsonObject result{{"apiVersion", 1},
                       {"documentId", QString::fromStdString(doc.identity())},
                       {"expectedRevision", QString::number(doc.revision())},
                       {"commands", commands}};
    if (!label.isEmpty())
        result["history"] = QJsonObject{{"label", label}};
    return result;
}
template <class F> void refused(Document &doc, F run, const std::string &message) {
    const auto bytes = encodeContainer(doc);
    const auto history = doc.history().total;
    try {
        run();
    } catch (const std::exception &) {
        check(encodeContainer(doc) == bytes && doc.history().total == history,
              message + " leaves the document and History unchanged");
        return;
    }
    throw std::runtime_error(message + " was not refused");
}
void doorOnWall() {
    const auto door = assembly("interior-door");
    Wall wall;
    auto &doc = wall.doc;
    const auto full = wall.volume();
    const auto history = doc.history().total;
    // A 1000 mm door on the wall: one entry that inserts, overrides and cuts.
    const auto first = placeLibraryAssembly(doc, door, wall.at(1.5), {{"width", "1000 mm"}});
    check(doc.history().total == history + 1 && lastLabel(doc) == "Place Interior door",
          "Placing a door is one History entry, Place Interior door");
    check(!first.reused && doc.definitions().size() == 1, "The first door inserts the bundle");
    check(doc.hostedComponents().attachments.contains(first.placement),
          "The door is attached to the wall in the same edit");
    check(std::abs(wall.openingWidth(first.placement) - 1.0) < 1e-9,
          "The door cuts a 1000 mm opening");
    check(std::abs(full - wall.volume() - 1.0 * 2.1 * .2) < 1e-6,
          "The opening goes through the wall");
    const auto values = componentParameterValues(doc, first.placement);
    for (const auto &entry : values["values"].toArray()) {
        const auto value = entry.toObject();
        check((value["name"] == "width") == (value["source"] == "override"),
              "Only the width is stored as an override");
    }
    // Changing the width re-cuts the opening in one entry (R093, ADR 0166 §6.3).
    executeBatch(doc,
                 batch(doc,
                       {QJsonObject{{"command", "component_parameter.override"},
                                    {"body", QString::number(first.placement)},
                                    {"values", QJsonObject{{"width", 1.1}}}}},
                       "Change Width"));
    check(doc.history().total == history + 2 && lastLabel(doc) == "Change Width",
          "Changing the width is one entry");
    check(std::abs(wall.openingWidth(first.placement) - 1.1) < 1e-9 &&
              std::abs(full - wall.volume() - 1.1 * 2.1 * .2) < 1e-6,
          "Changing the width re-cuts the opening");
    // A second door reuses the definition: every door is one definition with its own values.
    const auto second = placeLibraryAssembly(doc, door, wall.at(4.2));
    check(second.reused && second.definition == first.definition &&
              doc.definitions().size() == 1 && doc.history().total == history + 3,
          "A second door reuses the in-model definition in one entry");
    check(std::abs(wall.openingWidth(second.placement) - .9) < 1e-9,
          "The second door at its defaults cuts a 900 mm opening");
    check(componentParameterValues(doc, second.placement)["values"].toArray().size() == 4,
          "The second door shows the definition's four parameters");
    // Undo removes the second door and its opening whole; redo restores both.
    const auto withBoth = wall.volume();
    const auto records = doc.hostedComponents();
    doc.undo();
    check(!doc.bodies().contains(second.placement) &&
              !doc.hostedComponents().hosts.at(wall.body)->openings.contains(second.placement),
          "Undo removes the placement and its opening together");
    doc.redo();
    check(doc.bodies().contains(second.placement) && doc.hostedComponents() == records &&
              std::abs(wall.volume() - withBoth) < 1e-12,
          "Redo restores the placement and its opening exactly");
    // Undo to the start leaves the wall uncut and no definition.
    doc.undo();
    doc.undo();
    doc.undo();
    check(doc.definitions().empty() && doc.hostedComponents().attachments.empty() &&
              std::abs(wall.volume() - full) < 1e-9,
          "Undo to the start removes the inserted definition and the cut");
}
void refusals() {
    const auto door = assembly("interior-door");
    Wall wall;
    auto &doc = wall.doc;
    refused(doc, [&] { placeLibraryAssembly(doc, door, wall.at(2), {{"width", "2 m"}}); },
            "A width above the maximum");
    bool coded = false;
    try {
        placeLibraryAssembly(doc, door, wall.at(2), {{"width", "2 m"}});
    } catch (const ComponentParameterError &error) {
        coded = error.code() == "PARAMETER_DOMAIN";
    }
    check(coded, "A bounds refusal keeps PARAMETER_DOMAIN");
    refused(doc, [&] { placeLibraryAssembly(doc, door, wall.at(9)); },
            "An anchor outside the host face");
    refused(doc, [&] { placeLibraryAssembly(doc, door, wall.at(.2)); },
            "An opening past the wall's end");
    refused(doc, [&] { placeLibraryAssembly(doc, door, wall.at(2), {{"colour", "red"}}); },
            "An unknown parameter");
    // A refusal after a reuse leaves the earlier placement as it was.
    placeLibraryAssembly(doc, door, wall.at(2));
    refused(doc, [&] { placeLibraryAssembly(doc, door, wall.at(2.1)); },
            "An opening overlapping another");
}
void freePlacement() {
    const auto table = assembly("dining-table");
    Document doc(DisplayUnit::Millimeters);
    AssemblyPlacement placement;
    placement.transform = Transform::translation({2, 3, 0});
    const auto first = placeLibraryAssembly(doc, table, placement,
                                            {{"legs", "tapered"}, {"apron", false}});
    check(lastLabel(doc) == "Place Dining table" &&
              std::abs(doc.worldTransform(first.placement).point({}).x - 2) < 1e-12 &&
              doc.instances().at(first.placement)->overrides.size() == 2,
          "A free assembly is placed at the transform with its values");
    placement.transform = Transform::translation({5, 3, 0});
    const auto second = placeLibraryAssembly(doc, table, placement);
    check(second.reused && second.definition == first.definition &&
              doc.instances().at(second.placement)->overrides.empty(),
          "A second table reuses the definition and stores no overrides at its defaults");
    // A unique copy loses the identity, so a later placement still reuses the original.
    executeBatch(doc, batch(doc, {QJsonObject{{"command", "component.make_unique"},
                                              {"body", QString::number(second.placement)}}}));
    const auto copy = doc.instances().at(second.placement)->definition;
    check(copy != first.definition, "Make unique copies the definition");
    check(findLibraryAssembly(doc, *libraryAssemblyIdentity(table)) == first.definition,
          "Only the library's definition carries the assembly identity");
    const auto third = placeLibraryAssembly(doc, table, placement);
    check(third.reused && third.definition == first.definition,
          "Later placements reuse the library's definition, not the unique copy");
    // A bundle without an identity is inserted as new, independent definitions (ADR 0130).
    auto plain = table;
    auto document = plain.document.readSnapshot();
    setComponentAssembly(document, plain.definition, std::nullopt);
    plain = decodeComponentBundle(
        encodeComponentBundle(document, plain.definition, plain.metadata, plain.thumbnailPng));
    check(!libraryAssemblyIdentity(plain), "The plain bundle has no identity");
    const auto definitions = doc.definitions().size();
    const auto independent = placeLibraryAssembly(doc, plain, placement);
    check(!independent.reused && doc.definitions().size() == definitions + 1,
          "A bundle without an assembly identity inserts its own definition");
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        doorOnWall();
        refusals();
        freePlacement();
        std::cout << "assembly_placement_tests passed " << checks << " checks\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "assembly_placement_tests failed: " << error.what() << '\n';
        return 1;
    }
}
