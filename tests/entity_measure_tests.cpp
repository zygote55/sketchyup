#include "core/components.hpp"
#include "core/entity_measure.hpp"
#include "core/groups.hpp"
#include <iostream>
#include <numbers>
using namespace sketchy;
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
bool near(Vec3 a, Vec3 b) { return length(a - b) < tolerance; }
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
        const auto id = doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}});
        doc.extrude(id, 5, 4);
        const auto parent = createGroup(doc, {id}, "Assembly");
        doc.transform(parent, Transform::translation({10, 20, 30}) *
                                  Transform::rotation({0, 0, 1}, std::numbers::pi / 2) *
                                  Transform::scaling({-2, 3, .5}));
        const auto stamp = doc.saveStamp();
        auto measured = measureEntity(doc, {id, SelectionKind::Body, 0});
        check(near(measured.local.bounds->dimensions(), {2, 3, 4}) &&
                  near(measured.world.bounds->dimensions(), {9, 4, 2}),
              "Local and world bounds distinguish rotated mirrored nonuniform frames");
        check(std::abs(measured.local.area - 52) < tolerance &&
                  std::abs(measured.world.area - 124) < tolerance && measured.world.volume &&
                  std::abs(*measured.world.volume - 72) < tolerance &&
                  std::abs(*measured.local.volume - 24) < tolerance,
              "Affine areas and positive volumes follow transformed geometry");
        check(std::abs(measured.local.length - 36) < tolerance &&
                  std::abs(measured.world.length - 60) < tolerance,
              "Edge length counts unique topology edges once");
        check(near(measured.worldOrigin, {10, 20, 30}) && near(measured.parentOrigin, {}),
              "World and parent origin fields have explicit meanings");
        check(doc.isCurrentSnapshot(stamp), "Measurement queries do not edit the document");
        auto groupMeasure = measureEntity(doc, {parent, SelectionKind::Body, 0});
        check(groupMeasure.records == 2 && groupMeasure.solidBody == id &&
                  groupMeasure.local.volume &&
                  std::abs(*groupMeasure.local.volume - 24) < tolerance,
              "Single-geometry groups report descendant bounds and solid volume");
        const auto faceMeasure = measureEntity(doc, {id, SelectionKind::Face, 5});
        check(std::abs(faceMeasure.local.area - 6) < tolerance &&
                  std::abs(faceMeasure.world.area - 36) < tolerance && !faceMeasure.world.volume &&
                  faceMeasure.edges == 4,
              "Selected faces report boundary length and area without fabricated volume");
        const auto edge = doc.bodies().at(id)->topology.edges.begin()->first;
        const auto edgeMeasure = measureEntity(doc, {id, SelectionKind::Edge, edge});
        check(edgeMeasure.edges == 1 && edgeMeasure.vertices == 2 && edgeMeasure.world.area == 0,
              "Selected edge measurements exclude neighboring geometry");
        const auto beforePosition = doc.worldTransform(id);
        positionEntity(doc, id, {1, 2, 3}, false);
        check(near(doc.bodies().at(id)->transform.point({}), {1, 2, 3}),
              "Parent-frame position edits actual placement");
        doc.undo();
        check(doc.worldTransform(id) == beforePosition, "Position edit has one-step undo");
        dimensionEntity(doc, id, {4, 6, 8}, false);
        measured = measureEntity(doc, {id, SelectionKind::Body, 0});
        check(near(measured.local.bounds->dimensions(), {2, 3, 4}),
              "Context-local geometry dimensions remain intrinsic after placement scaling");
        check(near(measured.parent.bounds->dimensions(), {4, 6, 8}) &&
                  near(measured.world.bounds->dimensions(), {18, 8, 4}),
              "Parent dimension edit reads back the requested size and scales world geometry");
        const auto resizedRevision = doc.revision();
        dimensionEntity(doc, id, {4, 6, 8}, false);
        check(doc.revision() == resizedRevision, "Re-entering dimensions is an exact no-op");
        doc.undo();
        dimensionEntity(doc, parent, {18, 8, 4}, true);
        groupMeasure = measureEntity(doc, {parent, SelectionKind::Body, 0});
        check(near(groupMeasure.world.bounds->dimensions(), {18, 8, 4}),
              "World dimension edit matches desired AABB");
        doc.undo();
        doc.transform(id, Transform::rotation({0, 0, 1}, .37), parent);
        dimensionEntity(doc, id, {7, 8, 9}, false);
        measured = measureEntity(doc, {id, SelectionKind::Body, 0});
        check(near(measured.parent.bounds->dimensions(), {7, 8, 9}),
              "Rotated member dimensions read back exactly in the parent frame");
        positionEntity(doc, id, {2, 4, 6}, true);
        check(near(doc.worldTransform(id).point({}), {2, 4, 6}),
              "World origin edit survives a mirrored nonuniform parent");
        setEntityProperties(
            doc, id, {{"role", std::string("wall")}, {"thickness", .2}, {"structural", true}});
        check(doc.bodies().at(id)->properties.size() == 3, "Semantic fields retain typed values");
        const auto snapshot = doc.bodies();
        rejects([&] { setEntityProperties(doc, id, {{"bad", std::string(2049, 'x')}}); },
                "Oversized metadata rejects");
        check(doc.bodies() == snapshot, "Rejected property edit is atomic");
        setEntityState(doc, id, {}, true);
        rejects([&] { positionEntity(doc, id, {1, 2, 3}, true); },
                "Entity position respects locks");
        rejects([&] { dimensionEntity(doc, id, {8, 9, 10}, true); },
                "Entity dimension respects locks");
        Document planar;
        const auto sheet = planar.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}});
        rejects([&] { dimensionEntity(planar, sheet, {2, 3, 1}, false); },
                "Planar dimensions cannot invent thickness");
        const auto guide = planar.bodies().at(sheet)->surface.nextId;
        planar.addGuide(sheet, guideLine({0, 0, 0}, {1, 0, 0}));
        const auto guideMeasure = measureEntity(planar, {sheet, SelectionKind::Guide, guide});
        check(guideMeasure.world.infiniteLength && !guideMeasure.world.bounds,
              "Infinite guides have neither a fabricated length nor finite bounds");
        const auto component = createComponent(planar, sheet, "Panel");
        const auto member = component.movedGeometry.at(sheet);
        const auto peer = placeComponent(planar, component.definition).instance;
        editComponentDefinition(planar, component.definition, [&](Document &draft) {
            return setEntityProperties(draft, member, {{"role", std::string("shared")}});
        });
        check(planar.bodies()
                  .at(planar.instances().at(peer)->members.at(member))
                  ->properties.contains("role"),
              "Semantic fields use explicit shared component publication");
        std::cout << "Entity frame measurements, bounds, position/dimension edits, typed metadata "
                     "and scope passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
