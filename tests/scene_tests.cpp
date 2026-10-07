#include "core/groups.hpp"
#include "core/model.hpp"
#include <iostream>
#include <numbers>
using namespace sketchy;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void near(Vec3 a, Vec3 b, const char *message) { check(length(a - b) < 1e-9, message); }
template <class F> void rejects(F fn) {
    bool rejected = false;
    try {
        fn();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected, "Invalid scene operation must reject");
}
int main() {
    try {
        check(std::abs(meters(12, "ft") - 3.6576) < 1e-12, "Feet to meters");
        check(meters(1000, "mm") == 1, "Millimeters to meters");
        check(std::abs(radians(180, "deg") - std::numbers::pi) < 1e-12, "Degrees to radians");
        rejects([] { meters(INFINITY, "m"); });
        rejects([] { radians(NAN, "rad"); });
        rejects([] { meters(1, "yards"); });
        Document doc;
        auto root = doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}}});
        auto child = doc.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
        const auto local = Transform::translation({10, 0, 0}) * Transform::scaling({-2, 3, 1});
        doc.transform(root, local);
        doc.transform(child, Transform::translation({1, 2, 1}), root);
        auto world = doc.worldTransform(child);
        near(world.point({0, 0, 0}), {8, 6, 1}, "Nested mirrored world transform");
        near(world.inverse().point(world.point({.3, .8, .7})), {.3, .8, .7},
             "Affine inverse roundtrip");
        check(std::abs(doc.worldArea(child, doc.bodies().at(child)->surface.faces.begin()->first) -
                       6) < 1e-9,
              "World face area respects nonuniform scale");
        check(world.determinant() < 0, "Mirror sign retained");
        for (const auto &triangle : doc.worldTriangles(child))
            check(triangle.a.x >= 6 && triangle.a.x <= 8, "Rendered geometry uses world transform");
        const auto revision = doc.revision();
        rejects([&] { doc.transform(root, local, child); });
        check(doc.revision() == revision && doc.bodies().at(root)->parent == 0,
              "Cycle failure atomic");
        rejects([&] { doc.transform(child, {}, 999); });
        rejects([&] { doc.transform(root, Transform::translation({coordinateLimit, 0, 0})); });
        rejects([&] { doc.erase(root); });
        check(doc.revision() == revision,
              "Invalid parent, bounds and deletion leave scene unchanged");
        auto invalid = Transform{};
        invalid.m[0] = 0;
        rejects([&] { doc.transform(child, invalid, root); });
        doc.move(child, {2, 0, 0});
        near(doc.worldTransform(child).point({0, 0, 0}), {10, 6, 1},
             "World move under mirrored parent");
        doc.undo();
        near(doc.worldTransform(child).point({0, 0, 0}), {8, 6, 1},
             "Transform undo restores hierarchy");
        doc.redo();
        near(doc.worldTransform(child).point({0, 0, 0}), {10, 6, 1}, "Transform redo");
        auto old = doc.bodies().at(child);
        auto body = std::make_shared<Body>(*old);
        body->properties = {{"label", std::string("Window")}, {"width", 1.2}, {"locked", false}};
        doc.apply({"Properties", {{child, old, body}}}, doc.revision());
        body->properties["width"] = 4.0;
        check(std::get<double>(doc.bodies().at(child)->properties.at("width")) == 1.2,
              "Properties frozen with authoritative records");
        Document nestedBounds;
        const auto nestedLeaf =
            nestedBounds.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
        const auto emptyParent = createGroup(nestedBounds, {nestedLeaf});
        const auto boundedScene = nestedBounds.bodies();
        const auto boundedRevision = nestedBounds.revision();
        rejects([&] {
            nestedBounds.transform(emptyParent, Transform::translation({coordinateLimit, 0, 0}));
        });
        check(nestedBounds.bodies() == boundedScene && nestedBounds.revision() == boundedRevision,
              "An empty parent's transform still validates unchanged descendants' world bounds");
        nestedBounds.transform(emptyParent, Transform::translation({10, 0, 0}));
        near(nestedBounds.worldTransform(nestedLeaf).point({0, 0, 0}), {10, 0, 0},
             "A valid parent transform updates descendant world geometry");
        nestedBounds.undo();
        near(nestedBounds.worldTransform(nestedLeaf).point({0, 0, 0}), {},
             "Parent transform undo restores descendant world geometry");
        Document identities;
        auto identityBody = identities.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
        auto identityFace = identities.bodies().at(identityBody)->surface.faces.begin()->first;
        identities.extrude(identityBody, identityFace, 1);
        auto retiredFloor = identities.bodies().at(identityBody)->surface.nextId;
        identities.undo();
        identities.undo();
        identities.redo();
        check(identities.bodies().at(identityBody)->surface.nextId >= retiredFloor,
              "Undo/redo of context creation preserves retired surface IDs");
        identities.extrude(identityBody, identityFace, 2);
        for (const auto &[id, face] : identities.bodies().at(identityBody)->surface.faces)
            check(id == identityFace || id >= retiredFloor,
                  "New faces cannot reuse IDs from undone extrusion");
        identities.undo();
        auto identityBefore = identities.bodies().at(identityBody);
        auto reuse = std::make_shared<Body>(*identityBefore);
        reuse->surface.nextId = 6;
        reuse->surface.extrude(identityFace, 3);
        rejects([&] {
            identities.apply({"Invalid reuse", {{identityBody, identityBefore, reuse}}},
                             identities.revision());
        });
        Document bounded;
        auto boundedId = bounded.addFace({{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}});
        auto beforeProperties = bounded.bodies().at(boundedId);
        auto payload = std::make_shared<Body>(*beforeProperties);
        for (int i = 0; i < 128; ++i)
            payload->properties["property" + std::to_string(i)] = std::string(2048, 'x');
        bounded.apply({"Bounded metadata", {{boundedId, beforeProperties, payload}}},
                      bounded.revision());
        for (int i = 0; i < 160; ++i)
            bounded.paint(boundedId, {float(i % 10) / 10, .2f, .3f});
        check(bounded.historyBytes() <= Document::historyLimit,
              "History evicts to its allocation budget");
        auto finalBody = *bounded.bodies().at(boundedId);
        int undoCount = 0;
        while (bounded.canUndo()) {
            bounded.undo();
            ++undoCount;
        }
        check(undoCount > 0 && undoCount < 162, "Budget eviction removes oldest undo entries");
        while (bounded.canRedo())
            bounded.redo();
        check(*bounded.bodies().at(boundedId) == finalBody,
              "Evicted history still redoes retained edits exactly");
        bounded.undo();
        bounded.paint(boundedId, {.4f, .5f, .6f});
        check(!bounded.canRedo() && bounded.historyBytes() <= Document::historyLimit,
              "New edit releases redo branch within budget");
        Document exhausted;
        exhausted.restore(exhausted.identity(), 1, {}, UINT64_MAX);
        rejects([&] { exhausted.addFace({{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}}); });
        check(exhausted.bodies().empty(), "Revision exhaustion rejects before mutation");
        std::cout << "Scene records: units, nested/mirrored transforms, properties, atomic "
                     "validation and history passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
