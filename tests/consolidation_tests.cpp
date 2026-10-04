#include "core/appearance.hpp"
#include "core/consolidation.hpp"
#include "core/groups.hpp"
#include <iostream>
using namespace sketchy;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
Id square(Document &doc, double x) {
    return doc.addFace({{{x, 0, 0}, {x + 1, 0, 0}, {x + 1, 1, 0}, {x, 1, 0}}});
}
void adjacentColors() {
    Document doc;
    auto a = square(doc, 0), b = square(doc, 1);
    const std::array<float, 3> red{.9f, .1f, .2f}, blue{.1f, .2f, .9f};
    doc.paint(a, red);
    doc.paint(b, blue);
    auto before = doc.bodies();
    const auto revision = doc.revision();
    const auto result = consolidateContext(doc);
    const auto &body = *doc.bodies().at(a);
    check(result.destination == a && doc.bodies().size() == 1 &&
              body.surface.vertices.size() == 6 && body.topology.edges.size() == 7 &&
              body.surface.faces.size() == 2,
          "Adjacent records weld their seam");
    check(faceColor(body, 5) == red && faceColor(body, result.transfers.at(b).faces.at(5)) == blue,
          "Different record colors survive consolidation");
    check(result.transfers.at(a).vertices.at(2) == result.transfers.at(b).vertices.at(1),
          "Transferred vertex IDs identify the welded seam");
    check(doc.revision() == revision + 1, "One merge publishes one edit");
    const auto after = doc.bodies();
    doc.undo();
    for (const auto &[id, original] : before) {
        auto expected = *original;
        expected.surface.nextId = doc.bodies().at(id)->surface.nextId;
        expected.topology.nextId = doc.bodies().at(id)->topology.nextId;
        check(*doc.bodies().at(id) == expected, "Undo restores source records with safe ID floors");
    }
    doc.redo();
    for (const auto &[id, merged] : after)
        check(*doc.bodies().at(id) == *merged, "Redo restores the merged record exactly");
    doc.insertEdges(a, {}, {0, 0, 1}, {{Vec3{0, .5, 0}, Vec3{2, .5, 0}}});
    check(doc.bodies().at(a)->surface.faces.size() == 4,
          "A line crosses the former record boundary in one raw context");
    size_t reds = 0, blues = 0;
    for (const auto &[id, face] : doc.bodies().at(a)->surface.faces) {
        reds += faceColor(*doc.bodies().at(a), id) == red;
        blues += faceColor(*doc.bodies().at(a), id) == blue;
    }
    check(reds == 2 && blues == 2, "Subsequent splitting retains both colors");
}
void framesAndIsolation() {
    Document doc;
    const auto a = square(doc, 0), b = square(doc, 0), hidden = square(doc, 7);
    doc.transform(a, Transform::translation({3, 4, 0}));
    doc.transform(b, Transform::translation({5, 4, 0}) * Transform::scaling({-1, 1, 1}));
    const auto child = square(doc, 9);
    doc.transform(child, Transform::translation({0, 0, 3}), b);
    const auto group = createGroup(doc, {child});
    setEntityState(doc, group, {}, true);
    setEntityState(doc, hidden, true, {});
    const auto childFrame = doc.worldTransform(child);
    const auto protectedChild = doc.bodies().at(child);
    const auto protectedGroup = doc.bodies().at(group);
    const auto protectedHidden = doc.bodies().at(hidden);
    const auto result = consolidateContext(doc);
    check(doc.bodies().at(a)->surface.vertices.size() == 6, "Mirrored transformed face welds");
    check(doc.bodies().at(b)->surface.vertices.empty() && doc.bodies().at(b)->parent == 0,
          "An emptied source frame with children survives");
    check(doc.bodies().at(child) == protectedChild && doc.bodies().at(group) == protectedGroup &&
              doc.worldTransform(child) == childFrame && doc.bodies().at(hidden) == protectedHidden,
          "Nested locked groups and hidden records are isolated");
    check(doc.bodies().at(a)->surface.normal(result.transfers.at(b).faces.at(5)).z > .99,
          "Mirrored transfer preserves face orientation");
    auto snapshot = doc.bodies();
    bool rejected = false;
    try {
        consolidateContext(doc, 0, std::set<Id>{a, hidden});
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    check(rejected && doc.bodies() == snapshot, "Explicit hidden member rejects atomically");
}
void analyticGeometry() {
    Document doc;
    const auto a = square(doc, 0);
    const auto b = square(doc, 4);
    auto curve = centerCurve(CurveKind::Circle, DrawingPlane::make({7, 0, 0}, {0, 0, 1}, {1, 0, 0}),
                             1, 0, 2 * 3.14159265358979323846, 12);
    doc.addCurve(b, curve);
    doc.addGuide(b, guideLine({7, 0, 0}, {1, 1, 0}));
    doc.transform(b, Transform::translation({0, 3, 0}) * Transform::scaling({-2, 1, 1}));
    const auto source = doc.bodies().at(b);
    const auto frame = doc.worldTransform(b);
    const auto result = consolidateContext(doc);
    const auto &target = *doc.bodies().at(a);
    const auto curveId = source->curves.begin()->first;
    const auto &copy = target.curves.at(result.transfers.at(b).curves.at(curveId));
    check(length(copy.point(.4) - frame.point(source->curves.at(curveId).point(.4))) < tolerance,
          "Affine analytic curve placement survives merging");
    validateCurves(target.curves, target.surface, target.topology);
    const auto guideId = source->guides.begin()->first;
    check(length(target.guides.at(result.transfers.at(b).guides.at(guideId)).origin -
                 frame.point(source->guides.at(guideId).origin)) < tolerance,
          "Guide placement survives merging");
}
void groupDestination() {
    Document doc;
    auto a = square(doc, 0), b = square(doc, 1), outside = square(doc, 10);
    auto group = createGroup(doc, {a, b});
    const auto protectedOutside = doc.bodies().at(outside);
    const auto result = consolidateContext(doc, group);
    check(result.destination == group && doc.bodies().size() == 2 &&
              doc.bodies().at(group)->surface.faces.size() == 2 &&
              doc.bodies().at(outside) == protectedOutside,
          "An active group owns consolidated geometry without touching its neighbors");
}
void atomicLimits() {
    Document doc;
    const auto a = square(doc, 0), b = square(doc, 2);
    for (auto id : {a, b}) {
        const auto before = doc.bodies().at(id);
        auto after = std::make_shared<Body>(*before);
        for (int i = 0; i < 600; ++i)
            after->guides[after->surface.nextId++] = guidePoint({double(i), 0, 0});
        doc.apply({"Guide budget fixture", {{id, before, after}}}, doc.revision());
    }
    const auto before = doc.bodies();
    const auto revision = doc.revision();
    bool rejected = false;
    try {
        consolidateContext(doc);
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    check(rejected && doc.bodies() == before && doc.revision() == revision,
          "Combined per-context guide budget rejects without publishing or retiring IDs");
    Document scaled;
    square(scaled, 0);
    square(scaled, 10000);
    scaled.transform(1, Transform::scaling({.001, .001, .001}));
    const auto stable = scaled.bodies();
    rejected = false;
    try {
        consolidateContext(scaled);
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    check(rejected && scaled.bodies() == stable,
          "Unrepresentable destination coordinates reject without moving source records");
}
} // namespace
int main() {
    try {
        adjacentColors();
        framesAndIsolation();
        analyticGeometry();
        groupDestination();
        atomicLimits();
        std::cout << "Context consolidation, colors, world frames, analytic geometry and isolation "
                     "passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
