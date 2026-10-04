#include "core/model.hpp"
#include "geometry/drawing.hpp"
#include <iostream>
#include <numbers>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F fn) {
    try {
        fn();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Expected rejection");
}
int main() {
    try {
        for (auto normal : {Vec3{0, 0, 1}, Vec3{0, 1, 1}, Vec3{1, 2, 3}, Vec3{0, 0, -1}}) {
            const auto plane = DrawingPlane::make({3, 4, 5}, normal, {1, 0, 0});
            auto rectangle = rectangleOutline(plane, 2, 3);
            Surface s;
            auto face = s.addFace({rectangle});
            check(std::abs(s.area(face) - 6) < 1e-6, "Tilted rectangle area");
            for (auto point : rectangle)
                check(std::abs(plane.coordinates(point).z) < tolerance,
                      "Rectangle plane membership");
            for (unsigned sides : {3u, 4u, 6u, 24u, 256u}) {
                Surface polygon;
                const auto id = polygon.addFace({polygonOutline(plane, 2, sides)});
                const auto expected = sides * 2 * std::sin(2 * std::numbers::pi / sides);
                check(std::abs(polygon.area(id) - expected) < 2e-6, "Regular polygon area");
            }
            Document drawing;
            drawing.insertEdges(0, plane.origin, plane.normal, polylineEdges(rectangle, true));
            check(drawing.bodies().at(1)->surface.faces.size() == 1,
                  "Closed outline forms engine face");
            auto original = drawing.bodies().at(1);
            drawing.insertEdges(1, plane.origin, plane.normal,
                                {{{plane.point(1, 0), plane.point(1, 3)}}});
            check(drawing.bodies().at(1)->surface.faces.size() == 2,
                  "Plane-aligned line splits existing face");
            drawing.undo();
            check(drawing.bodies().at(1)->surface.faces == original->surface.faces,
                  "Drawing undo restores exact face records");
        }
        for (auto magnitude : {1e-200, 1e-12, 1e12, 1e200}) {
            const auto scaled = DrawingPlane::make({}, {0, 0, magnitude}, {magnitude, 0, 0});
            check(scaled.normal == Vec3{0, 0, 1} && scaled.xAxis == Vec3{1, 0, 0},
                  "Plane directions are independent of their scale");
        }
        auto plane = DrawingPlane::make({}, {0, 0, 1}, {1, 1, 0});
        auto outline = rectangleOutline(plane, 3, 2);
        Surface rotated;
        const auto id = rotated.addFace({outline});
        check(std::abs(rotated.area(id) - 6) < 1e-6, "Rotated baseline rectangle");
        auto sampled = polylineEdges({{0, 0, 0}, {0, 0, 0}, {1, 0, 0}, {2, 1, 0}}, false);
        check(sampled.size() == 2, "Coincident consecutive samples are coalesced");
        rejects([&] { DrawingPlane::make({}, {0, 0, 1}, {0, 0, 2}); });
        rejects([&] { rectangleOutline(plane, 0, 1); });
        rejects([&] { polygonOutline(plane, 1, 2); });
        rejects([&] { polygonOutline(plane, 1, 257); });
        rejects([&] { polygonOutline(plane, 1e-6, 256); });
        rejects([&] { polylineEdges(std::vector<Vec3>(513), false); });
        rejects([&] { polylineEdges({{0, 0, 0}, {0, 0, 0}}, false); });
        Document malformed;
        const auto before = malformed.revision();
        rejects([&] {
            malformed.insertEdges(0, {0, 0, 0}, {0, 0, 1},
                                  polylineEdges({{0, 0, 0}, {1, 0, .1}}, false));
        });
        check(malformed.revision() == before && malformed.bodies().empty(),
              "Off-plane polyline rejects atomically");
        std::cout << "Arbitrary-plane rectangles, regular polygons, sampled polylines and "
                     "rejection tests passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
