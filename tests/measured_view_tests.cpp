#include "geometry/measured_view.hpp"
#include <iostream>
#include <limits>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void near(double a, double b) {
    check(std::isfinite(a) && std::abs(a - b) < 1e-7, "Independent measured projection oracle");
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Invalid measured view must reject");
}
int main() {
    try {
        MeasuredPage page;
        page.widthMm = 200;
        page.heightMm = 100;
        page.scaleDenominator = 50;
        page.marginMm = 0;
        const auto start = page.project({0, 0, 0}), end = page.project({1, 0, 0});
        near(end.x - start.x, 20);
        near(end.y, 50);
        const std::vector<MeasuredEdge> line{{{-2, 0, 0}, {2, 0, 0}, 7}};
        const std::vector<MeasuredTriangle> front{{{{-1, -1, 1}, {1, -1, 1}, {0, 1, 1}}}};
        auto result = measuredHiddenLines(page, line, front);
        check(result.lines.size() == 2 && result.hiddenIntervals == 1,
              "Foreground triangle splits hidden interval");
        near(result.lines[0].a.x, 60);
        near(result.lines[0].b.x, 90);
        near(result.lines[1].a.x, 110);
        near(result.lines[1].b.x, 140);
        check(result.lines[0].source == 7, "Line provenance retained");
        page.includeHidden = true;
        result = measuredHiddenLines(page, line, front);
        check(result.lines.size() == 3 && result.lines[1].hidden,
              "Optional hidden interval retained for dashed presentation");
        near(result.lines[1].b.x - result.lines[1].a.x, 20);
        auto behind = front;
        for (auto &p : behind[0].vertices)
            p.z = -1;
        check(measuredHiddenLines(page, line, behind).lines.size() == 1,
              "Geometry behind line cannot occlude it");
        auto coplanar = front;
        for (auto &p : coplanar[0].vertices)
            p.z = 0;
        check(measuredHiddenLines(page, line, coplanar).lines.size() == 1,
              "Coplanar face edges remain visible");
        auto duplicate = front;
        duplicate.push_back(front[0]);
        result = measuredHiddenLines(page, line, duplicate);
        check(result.lines.size() == 3 && result.hiddenIntervals == 1,
              "Overlapping occluders union once");
        auto reverse = front;
        std::swap(reverse[0].vertices[0], reverse[0].vertices[2]);
        check(measuredHiddenLines(page, line, reverse).lines.size() == 3,
              "Occlusion is independent of triangle winding");
        page.includeHidden = false;
        page.marginMm = 10;
        result = measuredHiddenLines(page, {{{-100, 0, 0}, {100, 0, 0}, 1}}, {});
        near(result.lines[0].a.x, 10);
        near(result.lines[0].b.x, 190);
        result = measuredHiddenLines(page, {{{0, 0, 0}, {0, 0, 1}, 1}}, {});
        check(result.lines.empty() && result.collapsedEdges == 1,
              "View-normal edges collapse in projection");
        page.right = {0, 1, 0};
        page.up = {0, 0, 1};
        page.towardEye = {1, 0, 0};
        page.validate();
        near(page.project({0, 1, 0}).x - page.project({0, 0, 0}).x, 20);
        near(page.project({0, 0, 1}).y, 30);
        auto bad = page;
        bad.scaleDenominator = 0;
        rejects([&] { measuredHiddenLines(bad, line, {}); });
        bad = page;
        bad.up = bad.right;
        rejects([&] { measuredHiddenLines(bad, line, {}); });
        bad = page;
        bad.marginMm = 50;
        rejects([&] { measuredHiddenLines(bad, line, {}); });
        rejects([&] { measuredHiddenLines(page, std::vector<MeasuredEdge>(20001), {}); });
        page.right = {1, 0, 0};
        page.up = {0, 1, 0};
        page.towardEye = {0, 0, 1};
        // A depth crossing creates an additional cut inside the projected triangle.
        result = measuredHiddenLines(page, {{{-2, 0, -1}, {2, 0, 1}, 1}}, coplanar);
        check(result.lines.size() == 2, "Depth interpolation clips only the behind portion");
        near(result.lines[0].b.x, 90);
        near(result.lines[1].a.x, 100 - 4e-6);
        std::cout << "Measured millimetres, orthographic frames, margins, hidden intervals and "
                     "depth clipping passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
