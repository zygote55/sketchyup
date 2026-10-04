#include "core/transform_selection.hpp"
#include <algorithm>
#include <iostream>
#include <numbers>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F call) {
    try {
        call();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Expected transform rejection");
}
bool near(Vec3 a, Vec3 b) { return length(a - b) <= tolerance; }
Document square() {
    Document doc;
    doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}}});
    return doc;
}
Id topFace(const Surface &surface) {
    for (const auto &[id, face] : surface.faces)
        if (surface.normal(id).z > .99)
            return id;
    throw std::runtime_error("No top face");
}
int main() {
    try {
        const auto rotate = Transform::rotation({0, 0, 1}, std::numbers::pi / 2);
        check(near(rotate.point({1, 0, 0}), {0, 1, 0}),
              "Rotation follows right-hand axis convention");
        rejects([&] { Transform::rotation({}, 1); });
        rejects([&] { Transform::rotation({1, 0, 0}, INFINITY); });
        auto doc = square();
        const auto source = doc.bodies().at(1);
        const auto face = source->surface.faces.begin()->first;
        const TransformTargets selectedFace{{1, TransformKind::Face, face}};
        const auto report = transformSelected(doc, selectedFace, rotate, {1, 1, 0});
        check(near(doc.bodies().at(1)->surface.vertices.at(1), {2, 0, 0}),
              "Face rotates around explicit pivot");
        check(doc.bodies().at(1)->topology.edges == source->topology.edges &&
                  report.changes.at(1).vertices.modified.size() == 4,
              "Face rotation preserves attached topology IDs and reports changed vertices");
        doc.undo();
        check(*doc.bodies().at(1) == *source, "Face transform is one exact undo step");
        transformSelected(doc, selectedFace, Transform::scaling({-1, 2, 1}), {1, 1, 0});
        check(doc.bodies().at(1)->surface.normal(face).z > .99 &&
                  std::abs(doc.bodies().at(1)->surface.area(face) - 8) < tolerance,
              "Raw mirror/nonuniform scale keeps face orientation and expected area");
        doc.undo();
        const auto beforeNoop = doc.revision();
        check(transformSelected(doc, selectedFace, {}).changes.empty() &&
                  doc.revision() == beforeNoop,
              "Identity transform creates no history item");
        rejects([&] { transformSelected(doc, selectedFace, Transform::scaling({0, 1, 1})); });
        rejects([&] { transformSelected(doc, {{1, TransformKind::Face, 999}}, rotate); });
        rejects([&] { transformSelected(doc, {{1, TransformKind::Context, 1}}, rotate); });
        rejects([&] { transformSelected(doc, {}, rotate); });
        // Shared cap vertices carry every attached wall without separating geometry.
        auto box = square();
        box.pushPull(1, face, 2);
        const auto originalBox = box.bodies().at(1);
        const auto top = topFace(originalBox->surface);
        const auto revision = box.revision();
        transformSelected(box, {{1, TransformKind::Face, top}}, Transform::translation({.5, 0, 1}));
        check(box.revision() == revision + 1 &&
                  box.bodies().at(1)->surface.faces == originalBox->surface.faces,
              "Moving a connected cap changes attached walls without replacing their IDs");
        box.bodies().at(1)->surface.validate();
        for (auto edge : box.bodies().at(1)->surface.edges())
            check(edge.faces.size() == 2, "Connected move stays closed");
        box.undo();
        const auto beforeFailure = box.bodies().at(1);
        const auto corner = beforeFailure->surface.faces.at(top).loops[0][0];
        rejects([&] {
            transformSelected(box, {{1, TransformKind::Vertex, corner}},
                              Transform::translation({0, 0, 1}));
        });
        check(box.bodies().at(1) == beforeFailure, "A nonplanar corner move rejects atomically");
        // Loose edge endpoints and individual vertices can be edited without detaching shared IDs.
        Document wire;
        wire.addWire(0, {0, 0, 0}, {1, 0, 0});
        const auto edge = wire.bodies().at(1)->topology.edges.begin()->first;
        const auto endpoint = wire.bodies().at(1)->topology.edges.at(edge).b;
        transformSelected(wire, {{1, TransformKind::Vertex, endpoint}},
                          Transform::translation({0, 1, 0}));
        check(near(wire.bodies().at(1)->surface.vertices.at(endpoint), {1, 1, 0}),
              "Individual vertex move");
        const auto wireBefore = wire.bodies().at(1);
        rejects([&] {
            transformSelected(wire, {{1, TransformKind::Vertex, endpoint}},
                              Transform::translation({-1, -1, 0}));
        });
        check(wire.bodies().at(1) == wireBefore,
              "Collapsed wire rejects without consuming geometry");
        transformSelected(wire,
                          {{1, TransformKind::Edge, edge}, {1, TransformKind::Vertex, endpoint}},
                          Transform::translation({0, 0, 1}));
        check(near(wire.bodies().at(1)->surface.vertices.at(endpoint), {1, 1, 1}),
              "Overlapping targets transform a shared vertex once");
        // Local frames follow a rotated/scaled context; world frames do not.
        auto framed = square();
        framed.transform(1, Transform::translation({4, 2, 0}) * rotate *
                                Transform::scaling({2, 1, 1}));
        const auto framedRevision = framed.revision();
        transformSelected(framed, selectedFace, {});
        check(framed.revision() == framedRevision,
              "Identity transform in a rotated context does not introduce roundoff edits");
        const auto originalPoint = framed.worldTransform(1).point({0, 0, 0});
        transformSelected(framed, selectedFace, Transform::translation({1, 0, 0}), {},
                          TransformSpace::Local);
        check(near(framed.worldTransform(1).point(framed.bodies().at(1)->surface.vertices.at(1)),
                   originalPoint + Vec3{0, 2, 0}),
              "Local translation follows nonuniform context axes");
        framed.undo();
        transformSelected(framed, selectedFace, Transform::translation({1, 0, 0}));
        check(near(framed.worldTransform(1).point(framed.bodies().at(1)->surface.vertices.at(1)),
                   originalPoint + Vec3{1, 0, 0}),
              "World translation remains in world axes");
        // A whole parent subsumes explicitly selected descendants; copies get new context IDs.
        auto hierarchy = square();
        hierarchy.addWire(0, {0, 0, 0}, {1, 0, 0});
        hierarchy.transform(1, Transform::translation({3, 0, 0}));
        hierarchy.transform(2, Transform::translation({0, 2, 0}), 1);
        const auto childPoint = hierarchy.worldTransform(2).point({});
        transformSelected(hierarchy,
                          {{1, TransformKind::Context, 0}, {2, TransformKind::Context, 0}},
                          Transform::translation({1, 0, 0}));
        check(near(hierarchy.worldTransform(2).point({}), childPoint + Vec3{1, 0, 0}),
              "Selected child never transforms twice");
        hierarchy.undo();
        const auto oldParent = hierarchy.bodies().at(1), oldChild = hierarchy.bodies().at(2);
        const auto copied =
            transformSelected(hierarchy, {{1, TransformKind::Context, 0}},
                              Transform::translation({0, 0, 3}), {}, TransformSpace::World, true);
        const auto parentCopy = copied.copies.at(1), childCopy = copied.copies.at(2);
        check(hierarchy.bodies().at(parentCopy)->surface == oldParent->surface &&
                  hierarchy.bodies().at(childCopy)->parent == parentCopy &&
                  near(hierarchy.worldTransform(childCopy).point({}), childPoint + Vec3{0, 0, 3}) &&
                  hierarchy.bodies().at(1) == oldParent && hierarchy.bodies().at(2) == oldChild,
              "Hierarchy copies preserve local IDs, remap parents and leave sources unchanged");
        hierarchy.undo();
        check(hierarchy.bodies().size() == 2, "One undo removes the complete copied subtree");
        const auto again = transformSelected(hierarchy, {{1, TransformKind::Context, 0}}, {}, {},
                                             TransformSpace::World, true);
        check(again.copies.at(1) > childCopy,
              "Copied context identities are never reused after undo");
        // Raw copies stay in their editing context, with fresh vertices and topology IDs.
        auto partial = square();
        const auto old = partial.bodies().at(1);
        const auto copy =
            transformSelected(partial, selectedFace, Transform::translation({3, 0, 0}), {},
                              TransformSpace::World, true);
        const auto &mapping = copy.geometryCopies.at(1);
        check(partial.bodies().size() == 1 &&
                  partial.bodies().at(1)->surface.faces.at(face) == old->surface.faces.at(face) &&
                  partial.bodies().at(1)->surface.faces.contains(mapping.faces.at(face)) &&
                  near(partial.bodies().at(1)->surface.vertices.at(mapping.vertices.at(1)),
                       {3, 0, 0}),
              "Partial face copy stays in context with explicit new entity identities");
        check(copy.changes.at(1).faces.descendants.at(face) ==
                  std::vector<Id>{face, mapping.faces.at(face)},
              "Copy lineage includes the original and copied face");
        const auto edgeCopy = transformSelected(wire, {{1, TransformKind::Edge, edge}}, rotate, {},
                                                TransformSpace::World, true);
        check(wire.bodies().size() == 1 && wire.bodies().at(1)->surface.wires.size() == 2 &&
                  edgeCopy.geometryCopies.at(1).edges.at(edge) != edge,
              "Edge copy is an independent wire in the active context");
        rejects([&] {
            transformSelected(wire, {{1, TransformKind::Vertex, endpoint}}, {}, {},
                              TransformSpace::World, true);
        });
        // Analytic constructions follow a complete transform; a partial edit retires its curve.
        Document curved;
        curved.addCurve(0, centerCurve(CurveKind::Circle, {}, 1, 0, 2 * std::numbers::pi, 24));
        curved.addGuide(1, guideLine({0, 0, 2}, {1, 0, 0}));
        const auto curveId = curved.bodies().at(1)->curves.begin()->first;
        const auto guideId = curved.bodies().at(1)->guides.begin()->first;
        const auto circleFace = curved.bodies().at(1)->surface.faces.begin()->first;
        transformSelected(
            curved, {{1, TransformKind::Face, circleFace}, {1, TransformKind::Guide, guideId}},
            rotate * Transform::scaling({2, 1, 1}));
        check(near(curved.bodies().at(1)->curves.at(curveId).xAxis, {0, 2, 0}) &&
                  near(curved.bodies().at(1)->guides.at(guideId).direction, {0, 1, 0}),
              "Affine curve frame and guide direction follow the selected geometry");
        const auto vertex = curved.bodies().at(1)->surface.vertices.begin()->first;
        transformSelected(curved, {{1, TransformKind::Vertex, vertex}},
                          Transform::translation({.01, 0, 0}));
        check(curved.bodies().at(1)->curves.empty(),
              "Partial curve edit retires analytic provenance");
        curved.undo();
        check(curved.bodies().at(1)->curves.contains(curveId),
              "Undo restores retired curve provenance");
        const auto curveCopy = transformSelected(
            curved, {{1, TransformKind::Face, circleFace}, {1, TransformKind::Guide, guideId}},
            Transform::translation({5, 0, 0}), {}, TransformSpace::World, true);
        const auto &constructionCopies = curveCopy.geometryCopies.at(1);
        check(
            curved.bodies().at(1)->curves.size() == 2 &&
                near(curved.bodies().at(1)->curves.at(constructionCopies.curves.at(curveId)).center,
                     {5, 0, 0}) &&
                near(curved.bodies().at(1)->guides.at(constructionCopies.guides.at(guideId)).origin,
                     {5, 0, 2}),
            "Raw copies preserve complete analytic curves and construction guides with new IDs");
        curved.undo();
        check(curved.bodies().at(1)->curves.size() == 1 &&
                  curved.bodies().at(1)->guides.size() == 1,
              "One undo removes all copied construction records");
        // Multi-context validation is staged: a later invalid target cannot leave an early edit.
        partial.addFace({{{0, 0, 3}, {2, 0, 3}, {2, 2, 3}, {0, 2, 3}}});
        const auto id = partial.bodies().rbegin()->first;
        const auto firstBody = partial.bodies().at(1), secondBody = partial.bodies().at(id);
        const auto next = partial.nextId();
        rejects([&] {
            transformSelected(partial,
                              {{1, TransformKind::Face, face}, {id, TransformKind::Vertex, 1}},
                              Transform::translation({0, 0, 1}));
        });
        check(partial.bodies().at(1) == firstBody && partial.bodies().at(id) == secondBody &&
                  partial.nextId() == next,
              "Mixed transform failure preserves all bodies and allocation state");
        std::cout << "Scoped transforms: pivots, mirrors, local/world frames, shared topology, "
                     "copies, hierarchy, curves, guides and atomic rejection passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
