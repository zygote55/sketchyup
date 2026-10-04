#include "core/model.hpp"
#include <iostream>
#include <numbers>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
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
int main() {
    try {
        const auto pi = std::numbers::pi;
        for (auto n : {Vec3{0, 0, 1}, Vec3{0, 1, 1}, Vec3{1, 2, 3}, Vec3{0, 0, -1}}) {
            const auto p = DrawingPlane::make({3, 4, 5}, n, {1, 0, 0});
            for (unsigned count : {3u, 6u, 24u, 256u}) {
                auto c = centerCurve(CurveKind::Circle, p, 2, 0, 2 * pi, count);
                Document doc;
                const auto report = doc.addCurve(0, c);
                const auto b = doc.bodies().at(1);
                check(b->curves.size() == 1 && b->topology.edges.size() == count &&
                          b->surface.faces.size() == 1,
                      "Circle topology and metadata");
                check(report.at(1).curves.created.size() == 1, "Curve creation report");
                const auto id = b->curves.begin()->first;
                for (const auto &[v, point] : b->surface.vertices)
                    check(std::abs(length(point - p.origin) - 2) < tolerance,
                          "Circle sampled radius");
                const auto edge = b->curves.at(id).edges.front().edge;
                doc.splitEdge(1, edge, .5);
                check(doc.bodies().at(1)->curves.at(id).edges.size() == count + 1,
                      "Split preserves curve with new associations");
                validateCurves(doc.bodies().at(1)->curves, doc.bodies().at(1)->surface,
                               doc.bodies().at(1)->topology);
                doc.undo();
                check(doc.bodies().at(1)->curves == b->curves, "Split undo restores curve");
                auto changes = doc.eraseEdge(1, edge);
                check(doc.bodies().at(1)->curves.empty() &&
                          changes.at(1).curves.deleted == std::vector<Id>{id},
                      "Broken outline retires metadata");
                doc.undo();
                check(doc.bodies().at(1)->curves == b->curves, "Erase undo restores metadata");
                doc.undo();
                check(doc.bodies().empty(), "Curve creation one undo");
                doc.redo();
                check(doc.bodies().at(1)->curves == b->curves, "Curve redo exact");
            }
            for (auto sweep : {pi / 2, -pi / 2, 1.5 * pi}) {
                auto arc = centerCurve(CurveKind::Arc, p, 2, 0, sweep, 24);
                Document doc;
                doc.addCurve(0, arc);
                check(doc.bodies().at(1)->surface.wires.size() == 24 &&
                          doc.bodies().at(1)->surface.faces.empty(),
                      "Arc open wire");
                check(std::abs(dot(arc.tangent(0), arc.point(0) - arc.center)) < tolerance,
                      "Arc tangent radial orthogonality");
                arc.kind = CurveKind::Pie;
                Document pie;
                pie.addCurve(0, arc);
                const auto &s = pie.bodies().at(1)->surface;
                const auto expected = 24 * 2 * std::abs(std::sin(sweep / 24));
                check(s.faces.size() == 1 &&
                          std::abs(s.area(s.faces.begin()->first) - expected) < 1e-5,
                      "Pie sector sampled area");
            }
            for (auto bulge : {-.5, .5, 2., -2.}) {
                auto arc = twoPointArc(p.point(-1, 0), p.point(1, 0), p.normal, bulge, 24);
                check(length(arc.point(0) - p.point(-1, 0)) < tolerance &&
                          length(arc.point(arc.sweepAngle) - p.point(1, 0)) < tolerance,
                      "Two-point endpoints");
                check(length(arc.point(arc.sweepAngle / 2) - p.point(0, bulge)) < tolerance,
                      "Signed bulge midpoint");
            }
            for (auto endAngle : {pi / 2, 1.5 * pi}) {
                auto arc = threePointArc(
                    p.point(2, 0), p.point(2 * std::cos(endAngle / 2), 2 * std::sin(endAngle / 2)),
                    p.point(2 * std::cos(endAngle), 2 * std::sin(endAngle)), 24);
                check(length(arc.center - p.origin) < tolerance &&
                          std::abs(arc.radius - 2) < tolerance &&
                          std::abs(arc.sweepAngle - endAngle) < tolerance,
                      "Three-point major/minor constraints");
            }
        }
        Document doc;
        auto c = centerCurve(CurveKind::Circle, {}, 2, 0, 2 * pi, 24);
        doc.addCurve(0, c);
        const auto body = doc.bodies().at(1);
        const auto revision = doc.revision();
        rejects([&] {
            auto bad = c;
            bad.radius = 0;
            doc.addCurve(1, bad);
        });
        rejects([&] {
            auto bad = c;
            bad.segments = 257;
            doc.addCurve(1, bad);
        });
        rejects([&] {
            auto bad = c;
            bad.kind = CurveKind::Arc;
            doc.addCurve(1, bad);
        });
        rejects([&] {
            auto bad = c;
            bad.xAxis = bad.yAxis;
            doc.addCurve(1, bad);
        });
        rejects([&] { threePointArc({0, 0, 0}, {1, 0, 0}, {2, 0, 0}, 24); });
        rejects([&] { twoPointArc({0, 0, 0}, {1, 0, 1}, {0, 0, 1}, 1, 24); });
        check(doc.revision() == revision && doc.bodies().at(1) == body,
              "Invalid curves reject atomically");
        auto forged = std::make_shared<Body>(*body);
        auto record = forged->curves.begin()->second;
        forged->curves.clear();
        forged->curves.emplace(1, record);
        rejects([&] { doc.apply({"Reused ID", {{1, body, forged}}}, doc.revision()); });
        std::cout
            << "Curve constraints, planes, segmentation, association, retirement and undo passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
