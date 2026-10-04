#include "core/model.hpp"
#include <iostream>
#include <numbers>
using namespace sketchy;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F fn) {
    try {
        fn();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Expected guide rejection");
}
int main() {
    try {
        const auto sill = offsetGuide(guideLine({}, {1, 0, 0}), {0, -1, 0}, .9);
        check(sill.origin == Vec3{0, 0, .9} && sill.direction == Vec3{1, 0, 0}, "0.9 m sill guide");
        const auto sloped =
            angledGuide(DrawingPlane::make({}, {0, -1, 0}, {1, 0, 0}), std::numbers::pi / 4);
        check(std::abs(dot(sloped.direction, Vec3{0, 1, 0})) < tolerance &&
                  std::abs(sloped.direction.x - sloped.direction.z) < tolerance,
              "Protractor guide on wall plane");
        check(measureDistance({1, 2, 3}, {4, 6, 3}) == 5, "Three-dimensional tape distance");
        check(std::abs(measureAngle({}, {1, 0, 0}, {0, 0, 1}, {0, -1, 0}) - std::numbers::pi / 2) <
                  1e-12,
              "Signed wall-plane angle");
        check(measureAngle({}, {1, 0, 0}, {0, -1, 0}, {0, 0, 1}) < 0, "Clockwise signed angle");
        rejects([] { guideLine({}, {}); });
        rejects([] { guidePoint({NAN, 0, 0}); });
        rejects([] { Guide{GuideKind::Point, {}, {1, 0, 0}}.validate(); });
        rejects([] { Guide{GuideKind::Line, {}, {2, 0, 0}}.validate(); });
        rejects([] { offsetGuide(guidePoint({}), {0, 0, 1}, 1); });
        rejects([] { offsetGuide(guideLine({}, {1, 0, 0}), {1, 0, 0}, 1); });
        rejects([] { offsetGuide(guideLine({}, {1, 0, 0}), {0, 0, 1}, 0); });
        rejects([] { angledGuide({}, 7); });
        rejects([] { measureAngle({}, {}, {1, 0, 0}, {0, 0, 1}); });
        rejects([] { measureAngle({}, {1, 0, 1}, {0, 1, 0}, {0, 0, 1}); });
        Document doc;
        auto report = doc.addGuide(0, sill);
        check(report.at(1).guides.created == std::vector<Id>{1}, "Guide creation ID report");
        doc.addGuide(1, sloped);
        doc.addGuide(1, guidePoint({1, 0, .9}));
        const auto guides = doc.bodies().at(1)->guides;
        check(doc.bodies().at(1)->surface.vertices.empty() &&
                  doc.bodies().at(1)->surface.faces.empty() &&
                  doc.bodies().at(1)->topology.edges.empty(),
              "Crossing guides never create model topology");
        doc.insertEdges(1, {0, 0, 0}, {0, -1, 0},
                        polylineEdges({{0, 0, 0}, {2, 0, 0}, {2, 0, 2}, {0, 0, 2}}, true));
        check(doc.bodies().at(1)->surface.faces.size() == 1 &&
                  doc.bodies().at(1)->surface.vertices.size() == 4 &&
                  doc.bodies().at(1)->guides == guides,
              "Guides do not partition a new face");
        const auto surface = doc.bodies().at(1)->surface;
        const auto topology = doc.bodies().at(1)->topology;
        report = doc.eraseGuide(1, 1);
        check(report.at(1).guides.deleted == std::vector<Id>{1} &&
                  doc.bodies().at(1)->surface == surface &&
                  doc.bodies().at(1)->topology == topology,
              "Guide erase preserves model geometry");
        doc.undo();
        check(doc.bodies().at(1)->guides == guides, "Guide erase undo exact");
        doc.addGuide(0, guidePoint({4, 5, 6}));
        const auto revision = doc.revision();
        report = doc.clearGuides();
        check(doc.revision() == revision + 1 && report.size() == 2 &&
                  doc.bodies().at(1)->guides.empty() && doc.bodies().at(2)->guides.empty(),
              "Global guide cleanup is one revision");
        doc.undo();
        check(doc.bodies().at(1)->guides == guides && doc.bodies().at(2)->guides.size() == 1,
              "Global guide cleanup is one undo");
        doc.clearGuides(2);
        check(doc.bodies().at(1)->guides == guides, "Context cleanup preserves other guides");
        doc.addGuide(1, guidePoint({.4, 0, .9}));
        const auto oldId = doc.bodies().at(1)->guides.rbegin()->first;
        report = doc.amendLast(doc.amendmentStamp(), [](Document &candidate) {
            candidate.addGuide(1, guidePoint({.6, 0, .9}));
        });
        check(report.at(1).guides.deleted == std::vector<Id>{oldId} &&
                  doc.bodies().at(1)->guides.rbegin()->first > oldId,
              "Amendment reserves retired guide IDs");
        doc.undo();
        check(doc.bodies().at(1)->guides == guides, "Amended guide remains one undo item");
        const auto body = doc.bodies().at(1);
        auto forged = std::make_shared<Body>(*body);
        forged->guides[oldId] = guidePoint({});
        rejects([&] { doc.apply({"Reused guide", {{1, body, forged}}}, doc.revision()); });
        forged = std::make_shared<Body>(*body);
        forged->guides[body->surface.vertices.begin()->first] = guidePoint({});
        rejects([&] { doc.apply({"Colliding guide", {{1, body, forged}}}, doc.revision()); });
        forged = std::make_shared<Body>(*body);
        for (int i = 0; i < 1025; ++i)
            forged->guides[forged->surface.nextId++] = guidePoint({});
        rejects([&] { doc.apply({"Oversized guides", {{1, body, forged}}}, doc.revision()); });
        check(doc.bodies().at(1) == body, "Invalid guide mutations remain atomic");
        Document curved;
        curved.addCurve(0, centerCurve(CurveKind::Circle, {}, 1, 0, 2 * std::numbers::pi, 12));
        const auto curveBody = curved.bodies().at(1);
        auto collision = std::make_shared<Body>(*curveBody);
        collision->guides[collision->curves.begin()->first] = guidePoint({});
        rejects([&] {
            curved.apply({"Curve ID collision", {{1, curveBody, collision}}}, curved.revision());
        });
        Document oversized;
        std::map<Id, BodyPtr> contexts;
        for (Id id = 1; id <= 11; ++id) {
            auto context = std::make_shared<Body>();
            context->id = id;
            for (Id guideId = 1; guideId <= 1000; ++guideId)
                context->guides.emplace(guideId, guidePoint({}));
            context->surface.nextId = 1001;
            contexts.emplace(id, context);
        }
        rejects([&] { oversized.restore(oversized.identity(), 12, contexts); });
        check(oversized.bodies().empty(),
              "Aggregate guide limit rejects otherwise valid contexts atomically");
        const auto transform = Transform::translation({2, 3, 4}) * Transform::scaling({-2, 3, 1});
        doc.transform(2, transform);
        doc.addGuide(2, guidePoint({1, 2, 3}));
        check(doc.worldTransform(2).point(doc.bodies().at(2)->guides.begin()->second.origin) ==
                  Vec3{0, 9, 7},
              "Guides inherit context transform");
        std::cout << "Guide isolation, offsets, angles, measurement, ID floors, limits, undo and "
                     "contexts passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
