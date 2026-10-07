#include "geometry/measured_view.hpp"
#include <algorithm>
#include <limits>
namespace sketchy {
namespace {
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
double cross2(Vec3 a, Vec3 b) { return a.x * b.y - a.y * b.x; }
bool positive(double a, double b, double &low, double &high, double threshold = 0) {
    a -= threshold;
    b -= threshold;
    if (a >= 0 && b >= 0)
        return true;
    if (a < 0 && b < 0)
        return false;
    const auto t = a / (a - b);
    if (a < 0)
        low = std::max(low, t);
    else
        high = std::min(high, t);
    return low < high;
}
struct Bounds {
    double left, right, top, bottom;
};
Bounds bounds(const std::vector<Vec3> &points) {
    Bounds b{points[0].x, points[0].x, points[0].y, points[0].y};
    for (auto p : points) {
        b.left = std::min(b.left, p.x);
        b.right = std::max(b.right, p.x);
        b.top = std::min(b.top, p.y);
        b.bottom = std::max(b.bottom, p.y);
    }
    return b;
}
struct Occluder {
    std::array<Vec3, 3> vertices;
    double area;
    Bounds box;
    std::array<double, 3> weights(Vec3 p) const {
        const auto delta = p - vertices[0];
        const auto b = cross2(delta, vertices[2] - vertices[0]) / area,
                   c = cross2(vertices[1] - vertices[0], delta) / area;
        return {1 - b - c, b, c};
    }
};
} // namespace
void MeasuredPage::validate() const {
    for (auto v : {widthMm, heightMm, marginMm, scaleDenominator})
        require(std::isfinite(v), "Measured page settings must be finite");
    require(widthMm >= 10 && widthMm <= 2000 && heightMm >= 10 && heightMm <= 2000 &&
                marginMm >= 0 && marginMm * 2 < std::min(widthMm, heightMm),
            "Measured page dimensions/margins out of bounds");
    require(scaleDenominator >= .001 && scaleDenominator <= 1e9,
            "Measured scale denominator out of bounds");
    checkPoint(origin);
    for (auto axis : {right, up, towardEye})
        require(std::isfinite(length(axis)) && std::abs(length(axis) - 1) < 1e-9,
                "Measured frame axes must be unit vectors");
    require(std::abs(dot(right, up)) < 1e-9 && length(cross(right, up) - towardEye) < 1e-9,
            "Measured view frame must be orthonormal and right handed");
}
Vec3 MeasuredPage::project(Vec3 world) const {
    checkPoint(world);
    const auto relative = world - origin;
    Vec3 result{widthMm / 2 + dot(relative, right) * 1000 / scaleDenominator,
                heightMm / 2 - dot(relative, up) * 1000 / scaleDenominator,
                dot(relative, towardEye)};
    require(std::isfinite(result.x) && std::isfinite(result.y) && std::isfinite(result.z) &&
                std::abs(result.x) <= 1e12 && std::abs(result.y) <= 1e12,
            "Measured projection exceeds numeric bounds");
    return result;
}
MeasuredLines measuredHiddenLines(const MeasuredPage &page, const std::vector<MeasuredEdge> &edges,
                                  const std::vector<MeasuredTriangle> &triangles) {
    page.validate();
    require(edges.size() <= 20000 && triangles.size() <= 20000,
            "Measured view exceeds 20000 input edges or occluders");
    constexpr int gridSize = 32;
    std::array<std::vector<size_t>, gridSize * gridSize> grid;
    std::vector<Occluder> occluders;
    size_t insertions{}, visits{}, checks{};
    auto cell = [&](double p, double span) {
        return std::clamp(int(std::floor(std::clamp(p / span, 0., 1.) * gridSize)), 0,
                          gridSize - 1);
    };
    auto cells = [&](Bounds box, auto visitor) {
        if (box.right < 0 || box.bottom < 0 || box.left > page.widthMm || box.top > page.heightMm)
            return;
        for (int y = cell(box.top, page.heightMm); y <= cell(box.bottom, page.heightMm); ++y)
            for (int x = cell(box.left, page.widthMm); x <= cell(box.right, page.widthMm); ++x)
                visitor(size_t(y * gridSize + x));
    };
    for (const auto &triangle : triangles) {
        const std::array<Vec3, 3> p{page.project(triangle.vertices[0]),
                                    page.project(triangle.vertices[1]),
                                    page.project(triangle.vertices[2])};
        const auto area = cross2(p[1] - p[0], p[2] - p[0]);
        if (std::abs(area) < 1e-12)
            continue;
        const auto box = bounds({p[0], p[1], p[2]});
        const auto id = occluders.size();
        occluders.push_back({p, area, box});
        cells(box, [&](size_t cell) {
            require(++insertions <= 1000000, "Measured occlusion grid exceeds one million entries");
            grid[cell].push_back(id);
        });
    }
    MeasuredLines result;
    result.inputEdges = edges.size();
    result.occluders = occluders.size();
    std::vector<size_t> stamps(occluders.size(), std::numeric_limits<size_t>::max());
    for (size_t index = 0; index < edges.size(); ++index) {
        const auto &edge = edges[index];
        const auto a = page.project(edge.a), b = page.project(edge.b), delta = b - a;
        if (std::hypot(delta.x, delta.y) < 1e-9) {
            ++result.collapsedEdges;
            continue;
        }
        double low = 0, high = 1;
        if (!positive(a.x - page.marginMm, b.x - page.marginMm, low, high) ||
            !positive(page.widthMm - page.marginMm - a.x, page.widthMm - page.marginMm - b.x, low,
                      high) ||
            !positive(a.y - page.marginMm, b.y - page.marginMm, low, high) ||
            !positive(page.heightMm - page.marginMm - a.y, page.heightMm - page.marginMm - b.y, low,
                      high))
            continue;
        std::vector<std::pair<double, double>> hidden;
        const auto lineBounds = bounds({a + delta * low, a + delta * high});
        cells(lineBounds, [&](size_t cell) {
            for (const auto id : grid[cell]) {
                require(++visits <= 20000000, "Measured grid traversal exceeds work budget");
                if (stamps[id] == index)
                    continue;
                stamps[id] = index;
                require(++checks <= 5000000,
                        "Measured occlusion exceeds five million candidate checks");
                const auto &triangle = occluders[id];
                const auto &box = triangle.box;
                if (lineBounds.left > box.right || lineBounds.right < box.left ||
                    lineBounds.top > box.bottom || lineBounds.bottom < box.top)
                    continue;
                const auto wa = triangle.weights(a), wb = triangle.weights(b);
                double from = low, to = high;
                bool inside = true;
                for (size_t i = 0; i < 3; ++i)
                    if (!positive(wa[i], wb[i], from, to)) {
                        inside = false;
                        break;
                    }
                if (!inside)
                    continue;
                double za{}, zb{};
                for (size_t i = 0; i < 3; ++i) {
                    za += wa[i] * triangle.vertices[i].z;
                    zb += wb[i] * triangle.vertices[i].z;
                }
                if (positive(za - a.z, zb - b.z, from, to, tolerance) && to - from > 1e-12)
                    hidden.emplace_back(from, to);
            }
        });
        std::sort(hidden.begin(), hidden.end());
        std::vector<std::pair<double, double>> united;
        for (const auto interval : hidden) {
            if (united.empty() || interval.first > united.back().second + 1e-12)
                united.push_back(interval);
            else
                united.back().second = std::max(united.back().second, interval.second);
        }
        result.hiddenIntervals += united.size();
        auto emit = [&](double from, double to, bool isHidden) {
            if (to - from <= 1e-12)
                return;
            require(result.lines.size() < 100000, "Measured output exceeds 100000 segments");
            result.lines.push_back({a + delta * from, a + delta * to, edge.source, isHidden});
        };
        double cursor = low;
        for (const auto &[from, to] : united) {
            emit(cursor, from, false);
            if (page.includeHidden)
                emit(from, to, true);
            cursor = to;
        }
        emit(cursor, high, false);
    }
    result.candidateChecks = checks;
    return result;
}
} // namespace sketchy
