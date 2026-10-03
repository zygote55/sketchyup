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
