#include "geometry/constraints.hpp"
#include <algorithm>
#include <iostream>
using namespace sketchy;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Expected rejection");
}
InferenceCamera camera() {
    return {{.1, 0, 0, 0, 0, .1, 0, 0, 0, 0, -.1, 0, 0, 0, 0, 1},
            {10, 0, 0, 0, 0, 10, 0, 0, 0, 0, -10, 0, 0, 0, 0, 1},
            1000,
            800};
}
int main() {
    try {
        const auto cam = camera();
        const DirectionConstraint red{DirectionKind::RedAxis, {1, 2, 0}, {1, 0, 0}};
        const auto candidate = projectDirection(red, cam, 700, 323);
        check(candidate && length(candidate->point - Vec3{4, 2, 0}) < tolerance &&
                  std::abs(candidate->pixels - 3) < 1e-8,
              "Projected axis acquired in logical pixels");
        const auto directions = directionCandidates(cam, 700, 323, {1, 2, 0}, {});
        check(!directions.empty() && directions[0].constraint.kind == DirectionKind::RedAxis,
              "Axis ranking");
        const auto coincident = directionCandidates(
            cam, 700, 323, {1, 2, 0}, {}, {{DirectionKind::Parallel, {1, 2, 0}, {-1, 0, 0}}});
        check(coincident.size() == 2 && coincident[0].constraint.kind == DirectionKind::RedAxis,
              "Coincident opposite line representations retain canonical axis priority");
        check(!projectDirection({DirectionKind::BlueAxis, {0, 0, 0}, {0, 0, 1}}, cam, 500, 400),
              "End-on direction requires orbit or numeric input");
        auto point = constrainedLength(red, {1, 2, 0}, {4, 2, 0}, 5);
        check(point == Vec3{6, 2, 0}, "Typed length follows locked axis");
        point = constrainedLength({DirectionKind::FromPoint, {0, 1, 0}, {1, 0, 0}}, {0, 0, 0},
                                  {2, 1, 0}, std::sqrt(5.));
        check(length(point - Vec3{2, 1, 0}) < tolerance, "Length reaches a reference line");
        rejects([&] {
            constrainedLength({DirectionKind::FromPoint, {0, 2, 0}, {1, 0, 0}}, {}, {1, 2, 0}, 1);
        });
        DirectionLocks locks;
        locks.toggle(red);
        check(locks.current() == red, "Arrow establishes persistent world direction");
        locks.hold({DirectionKind::GreenAxis, {1, 2, 0}, {0, 1, 0}});
        check(locks.holding() && locks.current()->kind == DirectionKind::GreenAxis,
              "Shift temporary lock");
        locks.release();
        check(locks.current() == red, "Shift release restores persistent lock");
        locks.toggle(red);
        check(!locks.current(), "Same arrow releases axis");
        Document guides;
        guides.addGuide(0, guideLine({}, {1, 1, 0}));
        guides.transform(1, Transform::scaling({-2, 3, 1}));
        InferenceCandidate guideReference;
        guideReference.kind = InferenceKind::OnGuide;
        guideReference.body = 1;
        guideReference.entity = 1;
        guideReference.entityType = InferenceEntity::Guide;
        const auto guideDirections = edgeDirections(guides, guideReference, {0, 1, 0}, {});
        check(guideDirections.size() == 2 && guideDirections[0].entityType == InferenceEntity::Guide &&
              length(cross(guideDirections[0].direction, Vec3{-2,3,0})) < tolerance,
              "Guide references use transformed world directions and typed identities");
        auto guideLock = guideDirections[0];
        locks.toggle(guideLock);
        auto edgeLock = guideLock;
        edgeLock.entityType = InferenceEntity::Edge;
        locks.toggle(edgeLock);
        check(locks.current() && locks.current()->entityType == InferenceEntity::Edge,
              "Equal edge/guide numbers do not toggle the wrong source off");
        locks.clear();
        Document doc;
        doc.addWire(0, {0, 0, 0}, {2, 2, 0});
        InferenceCandidate edge;
        edge.kind = InferenceKind::OnEdge;
        edge.body = 1;
        edge.entity = doc.bodies().at(1)->topology.edges.begin()->first;
        edge.entityType = InferenceEntity::Edge;
        edge.point = {1, 1, 0};
        auto references = edgeDirections(doc, edge, {0, 1, 0}, {});
        check(references.size() == 2 &&
                  std::abs(dot(references[0].direction, references[1].direction)) < 1e-10,
              "Parallel and perpendicular references");
        doc = Document();
        doc.addCurve(0, centerCurve(CurveKind::Circle, {}, 2, 0, 2 * std::acos(-1), 24));
        edge.entity = doc.bodies().at(1)->curves.begin()->second.edges[0].edge;
        edge.point = {2, 0, 0};
        references = edgeDirections(doc, edge, {4, 0, 0}, {});
        size_t tangents = 0;
        for (const auto &reference : references)
            if (reference.kind == DirectionKind::Tangent) {
                ++tangents;
                const auto t = -dot(reference.origin, reference.direction);
                const auto contact = reference.origin + reference.direction * t;
                check(std::abs(length(contact) - 2) < tolerance &&
                          std::abs(dot(contact, reference.direction)) < tolerance,
                      "External analytic tangent constraint");
            }
        check(tangents == 2, "External point has two circle tangents");
        references = edgeDirections(doc, edge, {2, 0, 0}, {});
        tangents = 0;
        for (const auto &reference : references)
            if (reference.kind == DirectionKind::Tangent) {
                ++tangents;
                check(std::abs(reference.direction.x) < tolerance, "Tangent at analytic endpoint");
            }
        check(tangents == 1, "Endpoint has one tangent direction");
        references = edgeDirections(doc, edge, {0, 0, 0}, {});
        check(std::none_of(references.begin(), references.end(),
                           [](const auto &r) { return r.kind == DirectionKind::Tangent; }),
              "Interior point has no tangent");
        const auto from = directionCandidates(cam, 700, 320, {0, 0, 0}, {}, {}, Vec3{1, 2, 0});
        check(std::any_of(from.begin(), from.end(),
                          [](const auto &r) {
                              return r.constraint.kind == DirectionKind::FromPoint &&
                                     length(r.point - Vec3{4, 2, 0}) < tolerance;
                          }),
              "Armed reference supplies from-point direction");
        // Perspective projection must produce a world point on the line, even
        // when its endpoints have different clip-space w coordinates.
        auto perspective = cam;
        perspective.clipFromWorld = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, -1, 0, 0, -.2, 0};
        const DirectionConstraint sloped{DirectionKind::Parallel, {0, 0, -2}, {1, 0, -1}};
        const auto expected = Vec3{2, 0, -4};
        const auto screen = perspective.project(expected);
        const auto projected = projectDirection(sloped, perspective, screen->x, screen->y + 7);
        check(projected && length(projected->point - expected) < tolerance &&
                  std::abs(projected->pixels - 7) < 1e-8,
              "Perspective-correct direction projection");
        auto invalidCamera = cam;
        invalidCamera.width = 0;
        rejects([&] { projectDirection(red, invalidCamera, 0, 0); });
        rejects([&] { projectDirection(red, cam, NAN, 0); });
        const auto tilted = DrawingPlane::make({}, {1, 0, 1}, {0, 1, 0});
        const auto tiltedDirections = directionCandidates(cam, 500, 320, {}, tilted);
        check(tiltedDirections.size() == 1 &&
                  tiltedDirections[0].constraint.kind == DirectionKind::GreenAxis,
              "Tilted plane excludes incompatible world axes");
        doc.transform(1, Transform::scaling({-2, 3, 1}));
        references = edgeDirections(doc, edge, {-8, 0, 0}, {});
        tangents = 0;
        for (const auto &reference : references)
            if (reference.kind == DirectionKind::Tangent) {
                ++tangents;
                const auto localDirection =
                    doc.worldTransform(1).inverse().vector(reference.direction);
                const auto localAnchor = Vec3{4, 0, 0};
                const auto t =
                    -dot(localAnchor, localDirection) / dot(localDirection, localDirection);
                const auto contact = localAnchor + localDirection * t;
                check(std::abs(length(contact) - 2) < tolerance,
                      "Mirrored nonuniform transform preserves analytic tangency");
            }
        check(tangents == 2, "Transformed ellipse has two external tangents");
        Document arc;
        arc.addCurve(0, centerCurve(CurveKind::Arc, {}, 2, 0, std::acos(-1) * .5, 12));
        edge.entity = arc.bodies().at(1)->curves.begin()->second.edges[0].edge;
        references = edgeDirections(arc, edge, {4, 0, 0}, {});
        check(std::count_if(references.begin(), references.end(),
                            [](const auto &r) { return r.kind == DirectionKind::Tangent; }) == 1,
              "Arc excludes tangent contacts outside its sweep");
        std::cout << "Axis, parallel/perpendicular, analytic tangent, reference projection, length "
                     "and lock state tests passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
