#include "geometry/offset.hpp"
#include <algorithm>
#include <clipper2/clipper.h>
#include <tuple>
namespace sketchy {
namespace {
using namespace Clipper2Lib;
constexpr double scale = 1e8;
constexpr size_t inputLimit = 1024, loopLimit = 64, outputLimit = 4096, regionLimit = 256;
[[noreturn]] void fail(const char *code, const char *message) { throw OffsetError(code, message); }
OffsetResult build(const Surface &source, Id faceId, double distance) {
    if (!std::isfinite(distance) || std::abs(distance) > coordinateLimit ||
        (distance != 0 && std::abs(distance) < tolerance))
        fail("INVALID_OFFSET_DISTANCE",
             "Use zero or a finite offset of at least 0.0000001 m, within the coordinate limit");
    const auto it = source.faces.find(faceId);
    if (it == source.faces.end() || faceId == 0 || it->second.id != faceId)
        fail("INVALID_OFFSET_FACE", "Select an existing planar face to offset");
    const auto &face = it->second;
    size_t count{};
    if (face.loops.empty() || face.loops.size() > loopLimit)
        fail("OFFSET_LIMIT", "Offset accepts a face with at most 64 boundary loops");
    for (const auto &loop : face.loops) {
        if (loop.size() < 3)
            fail("INVALID_OFFSET_FACE", "Offset needs closed nondegenerate loops");
        count += loop.size();
        if (count > inputLimit)
            fail("OFFSET_LIMIT", "Offset accepts at most 1024 input vertices");
        for (auto id : loop)
            if (id == 0 || !source.vertices.contains(id))
                fail("INVALID_OFFSET_FACE", "Offset boundary references an invalid vertex");
    }
    // Surface validation rejects crossings, touching holes and invalid nesting.
    (void)source.triangulate(faceId);
    OffsetResult result;
    result.inputHoles = face.loops.size() - 1;
    if (distance == 0) {
        OffsetRegion original;
        for (const auto &loop : face.loops) {
            original.loops.emplace_back();
            for (auto id : loop)
                original.loops.back().push_back(source.vertices.at(id));
        }
        result.regions.push_back(std::move(original));
        result.outputHoles = result.inputHoles;
        return result;
    }
    const auto normal = source.normal(faceId);
    const auto axis = std::abs(normal.x) < .8 ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
    const auto u = normalized(cross(axis, normal)), v = cross(normal, u);
    auto low = source.vertices.at(face.loops.front().front()), high = low;
    for (const auto &loop : face.loops)
        for (auto id : loop) {
            const auto p = source.vertices.at(id);
            low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
            high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
        }
    // Anchor on the plane, near its bounding-box center, to bound integer coordinates.
    const auto anchor = source.vertices.at(face.loops.front().front());
    const auto midpoint = (low + high) * .5;
    const auto origin = midpoint - normal * dot(midpoint - anchor, normal);
    Paths64 input;
    for (const auto &loop : face.loops) {
        Path64 path;
        for (auto id : loop) {
            const auto p = source.vertices.at(id) - origin;
            path.emplace_back(std::llround(dot(p, u) * scale), std::llround(dot(p, v) * scale));
        }
        path = TrimCollinear(path);
        if (path.size() < 3)
            fail("OFFSET_BELOW_TOLERANCE", "A boundary collapses at offset precision");
        if (IsPositive(path) != input.empty())
            std::reverse(path.begin(), path.end());
        input.push_back(std::move(path));
    }
    ClipperOffset offset(4.0);
    offset.AddPaths(input, JoinType::Miter, EndType::Polygon);
    PolyTree64 tree;
    offset.Execute(distance * scale, tree);
    if (offset.ErrorCode())
        fail("OFFSET_FAILED", "The offset could not be resolved within numeric bounds");
    size_t outputCount{};
    auto decode = [&](Path64 path) {
        path = TrimCollinear(path);
        outputCount += path.size();
        if (path.size() < 3)
            fail("OFFSET_BELOW_TOLERANCE", "A surviving offset boundary is degenerate");
        if (outputCount > outputLimit)
            fail("OFFSET_LIMIT", "Offset exceeds 4096 output vertices");
        std::rotate(path.begin(),
                    std::min_element(
                        path.begin(), path.end(),
                        [](auto a, auto b) { return std::tie(a.x, a.y) < std::tie(b.x, b.y); }),
                    path.end());
        std::vector<Vec3> points;
        for (auto p : path) {
            const auto world = origin + u * (double(p.x) / scale) + v * (double(p.y) / scale);
            try {
                checkPoint(world);
            } catch (const std::exception &) {
                fail("OFFSET_RANGE", "The offset would exceed the model coordinate range");
            }
            points.push_back(world);
        }
        for (size_t i = 0; i < points.size(); ++i)
            if (length(points[i] - points[(i + 1) % points.size()]) < tolerance)
                fail(
                    "OFFSET_BELOW_TOLERANCE",
                    "A surviving offset edge is below modeling tolerance; choose another distance");
        return points;
    };
    auto less = [](const std::vector<Vec3> &a, const std::vector<Vec3> &b) {
        return std::lexicographical_compare(
            a.begin(), a.end(), b.begin(), b.end(),
            [](auto p, auto q) { return std::tie(p.x, p.y, p.z) < std::tie(q.x, q.y, q.z); });
    };
    auto visit = [&](auto &&self, const PolyPath64 &parent, unsigned depth) -> void {
        if (depth > loopLimit)
            fail("OFFSET_LIMIT", "Offset containment exceeds its nesting budget");
        for (const auto &node : parent) {
            if (!node->IsHole()) {
                if (result.regions.size() >= regionLimit)
                    fail("OFFSET_LIMIT", "Offset exceeds 256 surviving regions");
                OffsetRegion region;
                region.loops.push_back(decode(node->Polygon()));
                for (const auto &hole : *node) {
                    if (!hole->IsHole())
                        fail("OFFSET_FAILED", "Invalid offset containment");
                    region.loops.push_back(decode(hole->Polygon()));
                    ++result.outputHoles;
                }
                std::sort(region.loops.begin() + 1, region.loops.end(), less);
                // Revalidate in the native geometry kernel; no sliver is silently discarded.
                Surface checked;
                try {
                    checked.addFace(region.loops);
                    checked.validate();
                } catch (const std::exception &) {
                    fail("OFFSET_BELOW_TOLERANCE", "Offset boundaries cannot form a valid native "
                                                   "face; choose another distance");
                }
                result.regions.push_back(std::move(region));
            }
            self(self, *node, depth + 1);
        }
    };
    visit(visit, tree, 0);
    std::sort(result.regions.begin(), result.regions.end(),
              [&](const auto &a, const auto &b) { return less(a.loops.front(), b.loops.front()); });
    return result;
}
} // namespace
OffsetResult offsetFaceRegion(const Surface &source, Id face, double distance) {
    try {
        return build(source, face, distance);
    } catch (const OffsetError &) {
        throw;
    } catch (const std::exception &) {
        fail("INVALID_OFFSET_FACE",
             "Offset needs valid planar, nonintersecting boundaries with strictly contained holes");
    }
}
} // namespace sketchy
