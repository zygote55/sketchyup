#include "core/appearance.hpp"
#include "core/components.hpp"
#include "core/consolidation.hpp"
#include "core/groups.hpp"
#include "core/materials.hpp"
#include "core/transform_selection.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
using namespace sketchy;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F operation, const char *message) {
    bool rejected = false;
    try {
        operation();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected, message);
}
int main() {
    try {
        Document doc;
        const auto body = doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}}});
        const auto face = doc.bodies().at(body)->surface.faces.begin()->first;
        const auto red = createMaterial(doc, "Red", {1, 0, 0});
        const auto glass = createMaterial(doc, "Glass", {.2f, .4f, .8f}, .3f);
        const auto geometry = doc.bodies();
        editMaterial(doc, glass, {}, {}, .4f);
        check(doc.bodies() == geometry && doc.materials().at(glass)->opacity == .4f,
              "Swatch edits keep geometry records unchanged");
        doc.undo();
        check(doc.materials().at(glass)->opacity == .3f, "Material edits undo");
        assignMaterial(doc, body, face, red, true, false);
        assignMaterial(doc, body, face, glass, false, true);
        check(surfaceAppearance(doc.materials(), *doc.bodies().at(body), face).material == red &&
                  surfaceAppearance(doc.materials(), *doc.bodies().at(body), face, true).opacity ==
                      .3f,
              "Front and back have independent named color/opacity");
        const auto state = doc.saveStamp();
        rejects([&] { eraseMaterial(doc, red); }, "Used material deletion rejects");
        rejects([&] { assignMaterial(doc, body, Id{999}, red); }, "Missing face rejects");
        rejects([&] { assignMaterial(doc, body, face, 999); }, "Missing material rejects");
        rejects([&] { editMaterial(doc, red, {}, {}, -1); }, "Invalid opacity rejects");
        rejects([&] { editMaterial(doc, red, {}, {}, std::numeric_limits<float>::quiet_NaN()); },
                "NaN opacity rejects");
        rejects([&] { createMaterial(doc, "Red", {0, 0, 0}); }, "Duplicate swatch names reject");
        check(doc.isCurrentSnapshot(state), "Rejected material operations are atomic");
        doc.insertEdges(body, {}, {0, 0, 1}, {{{1, 0, 0}, {1, 2, 0}}});
        check(doc.bodies().at(body)->surface.faces.size() == 2, "Fixture split");
        for (const auto &[id, record] : doc.bodies().at(body)->surface.faces)
            check(faceMaterials(*doc.bodies().at(body), id) == MaterialSides{red, glass},
                  "Face splits inherit both material identities");
        const auto split = doc.bodies().at(body);
        const auto second = split->surface.faces.rbegin()->first;
        assignMaterial(doc, body, second, glass, true, false);
        Id boundary = 0;
        const auto adjacency =
            doc.bodies().at(body)->topology.adjacency(doc.bodies().at(body)->surface);
        for (const auto &[edge, faces] : adjacency.edgeFaces)
            if (faces.size() == 2)
                boundary = edge;
        rejects([&] { doc.eraseEdge(body, boundary); },
                "Merge cannot discard differing front materials");
        doc.undo();
        doc.eraseEdge(body, boundary);
        check(doc.bodies().at(body)->surface.faces.size() == 1 &&
                  faceMaterials(*doc.bodies().at(body),
                                doc.bodies().at(body)->surface.faces.begin()->first) ==
                      MaterialSides{red, glass},
              "Equal material boundaries merge without losing either side");
        doc.extrude(body, doc.bodies().at(body)->surface.faces.begin()->first, 2);
        for (const auto &[id, record] : doc.bodies().at(body)->surface.faces)
            check(faceMaterials(*doc.bodies().at(body), id) == MaterialSides{red, glass},
                  "Extruded faces inherit source materials");
        const auto beforeTransform = doc.bodies().at(body)->faceMaterials;
        transformSelected(doc, {{body, TransformKind::Context, 0}},
                          Transform::scaling({-2, 3, .5}));
        check(doc.bodies().at(body)->faceMaterials == beforeTransform,
              "Mirrored nonuniform transform preserves assignment identity");
        const auto component = createComponent(doc, body, "Coated block");
        const auto member = component.movedGeometry.at(body);
        const auto peer =
            placeComponent(doc, component.definition, Transform::translation({10, 0, 0})).instance;
        editComponentDefinition(doc, component.definition, [&](Document &draft) {
            return assignMaterial(draft, member, {}, glass, true, false);
        });
        const auto peerMember = doc.instances().at(peer)->members.at(member);
        check(doc.bodies().at(peerMember)->materials.front == glass &&
                  std::all_of(doc.bodies().at(peerMember)->surface.faces.begin(),
                              doc.bodies().at(peerMember)->surface.faces.end(),
                              [&](const auto &entry) {
                                  return faceMaterials(*doc.bodies().at(peerMember), entry.first) ==
                                         MaterialSides{glass, glass};
                              }),
              "Shared material assignments propagate without changing the other side");
        const auto beforeTable = doc.materials();
        rejects(
            [&] {
                editComponentDefinition(doc, component.definition, [&](Document &draft) {
                    createMaterial(draft, "Escaping swatch", {0, 0, 0});
                    return ChangeReport{};
                });
            },
            "Shared geometry scope cannot mutate global swatch table");
        check(doc.materials() == beforeTable,
              "Rejected shared table edit leaves swatches unchanged");
        makeComponentUnique(doc, peer);
        editComponentDefinition(doc, component.definition, [&](Document &draft) {
            return assignMaterial(draft, member, {}, red);
        });
        check(doc.bodies().at(peerMember)->materials.front == glass,
              "Make-unique isolates material assignments");
        Document unplaced;
        unplaced.restore(doc.identity(), 1, {}, 0, doc.definitions(), {}, doc.nextDefinitionId(),
                         doc.tags(), doc.nextTagId(), doc.materials(), doc.nextMaterialId());
        rejects([&] { eraseMaterial(unplaced, red); },
                "Unplaced component definitions retain authoritative material references");
        const auto locked = doc.bodies().at(member);
        setEntityState(doc, body, {}, true);
        rejects([&] { assignMaterial(doc, member, {}, glass); },
                "Locked context blocks assignment");
        check(doc.bodies().at(member) == locked, "Rejected locked assignment preserves record");
        Document copied;
        const auto panel = copied.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
        const auto paint = createMaterial(copied, "Coating", {.8f, .2f, .1f}, .7f);
        assignMaterial(copied, panel, Id{5}, paint, false, true);
        const auto copies =
            transformSelected(copied, {{panel, TransformKind::Face, 5}},
                              Transform::translation({2, 0, 0}), {}, TransformSpace::World, true);
        const auto copiedFace = copies.geometryCopies.at(panel).faces.at(5);
        check(faceMaterials(*copied.bodies().at(panel), copiedFace) == MaterialSides{0, paint},
              "Raw face copies preserve both material sides");
        Selection selected;
        selected.apply(copied, {{panel, SelectionKind::Face, copiedFace}}, SelectionMode::Replace);
        const auto group = groupSelected(copied, selected);
        const auto moved = group.movedGeometry.at(panel);
        check(faceMaterials(*copied.bodies().at(moved), copiedFace) == MaterialSides{0, paint} &&
                  !copied.bodies().at(panel)->faceMaterials.contains(copiedFace),
              "Grouping transfers assignments and prunes retired source face references");
        copied.pushPull(moved, copiedFace, 1);
        for (const auto &[id, record] : copied.bodies().at(moved)->surface.faces)
            check(faceMaterials(*copied.bodies().at(moved), id) == MaterialSides{0, paint},
                  "Push/pull propagates both source sides to new faces");
        const auto beforePaint = *copied.bodies().at(moved);
        copied.paint(moved, {0, 0, 1});
        check(copied.bodies().at(moved)->materials == MaterialSides{} &&
                  copied.bodies().at(moved)->faceMaterials.empty(),
              "Legacy solid-color paint explicitly replaces named assignments");
        copied.undo();
        check(*copied.bodies().at(moved) == beforePaint,
              "Color paint undo restores named assignments");
        Document merged;
        const auto left = merged.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
        const auto right = merged.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
        merged.transform(right, Transform::translation({2, 0, 0}) * Transform::scaling({-1, 1, 1}));
        const auto coating = createMaterial(merged, "Opposite coating", {0, 1, 0}, .6f);
        assignMaterial(merged, left, {}, coating, true, false);
        assignMaterial(merged, right, {}, coating, false, true);
        const auto consolidated = consolidateContext(merged);
        const auto transferred = consolidated.transfers.at(right).faces.at(5);
        check(faceMaterials(*merged.bodies().at(left), 5) == MaterialSides{coating, 0} &&
                  faceMaterials(*merged.bodies().at(left), transferred) ==
                      MaterialSides{0, coating} &&
                  merged.bodies().at(left)->surface.normal(transferred).z > .99,
              "Mirrored consolidation preserves oriented front/back ownership from both records");
        Document history;
        const auto retired = createMaterial(history, "Transient", {0, 0, 0});
        history.undo();
        check(createMaterial(history, "Fresh", {1, 1, 1}) > retired,
              "Undo never reuses material identities");
        const auto fresh = history.materials().begin()->first;
        editMaterial(history, fresh, "Renamed", {}, {});
        const auto stamp = history.amendmentStamp();
        history.amendLast(stamp,
                          [&](Document &draft) { editMaterial(draft, fresh, "Final", {}, {}); });
        check(history.materials().at(fresh)->name == "Final",
              "Material amendment publishes final edit");
        history.undo();
        check(history.materials().at(fresh)->name == "Fresh", "Amended material edit has one undo");
        auto mutableRecord = std::make_shared<MaterialRecord>(
            MaterialRecord{history.nextMaterialId(), "Frozen", {0, 0, 1}, .5f});
        Edit frozen{"External record", {}};
        frozen.materials.push_back({mutableRecord->id, nullptr, mutableRecord});
        history.apply(frozen, history.revision());
        mutableRecord->opacity = 0;
        check(history.materials().at(mutableRecord->id)->opacity == .5f,
              "Published material records do not retain mutable aliases");
        std::cout
            << "Material records, front/back lineage, opacity, scope, locks and history passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
