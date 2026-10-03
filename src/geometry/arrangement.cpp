#include "geometry/arrangement.hpp"
#include <clipper2/clipper.h>
#include <algorithm>
#include <limits>
namespace sketchy {
std::map<Id, std::vector<Id>> EdgeIdentityIndex::reconcile(const Surface &surface) {
    surface.validate();
    std::map<Key, Id> nextRecords;
    auto allocator = next_;
    for (const auto &edge : surface.edges()) {
        Key key{std::min(edge.a, edge.b), std::max(edge.a, edge.b)};
        if (auto found = records_.find(key); found != records_.end())
            nextRecords.emplace(key, found->second);
        else {
            if (allocator == std::numeric_limits<Id>::max())
                throw std::runtime_error("Edge identity space exhausted");
            nextRecords.emplace(key, allocator++);
        }
    }
    std::map<Id, std::array<Vec3, 2>> nextGeometry;
    std::map<Id, std::vector<Id>> descendants;
    for (const auto &[key, id] : nextRecords)
        nextGeometry[id] = {surface.vertices.at(key[0]), surface.vertices.at(key[1])};
    for (const auto &[oldId, segment] : geometry_) {
        auto &out = descendants[oldId];
        if (nextGeometry.contains(oldId)) { out.push_back(oldId); continue; }
        const auto delta = segment[1] - segment[0];
        const auto size = length(delta);
        if (size < tolerance) continue;
        const auto direction = delta * (1 / size);
        for (const auto &[id, candidate] : nextGeometry) {
            auto a = candidate[0] - segment[0], b = candidate[1] - segment[0];
            if (length(cross(a, direction)) > tolerance ||
                length(cross(b, direction)) > tolerance) continue;
            auto low = dot(a, direction), high = dot(b, direction);
            if (low > high) std::swap(low, high);
            if (std::min(size, high) - std::max(0.0, low) > tolerance) out.push_back(id);
        }
    }
    records_ = std::move(nextRecords);
    geometry_ = std::move(nextGeometry);
    next_ = allocator;
    return descendants;
}
FacePartition partitionFace(const Surface &source, Id face, Vec3 point, Vec3 planeNormal) {
    using namespace Clipper2Lib;
    source.validate();
    checkPoint(point);
    if (source.faces.size() != 1 || !source.wires.empty())
        throw std::runtime_error("Partition experiment requires one isolated face");
    const auto n = source.normal(face);
    const auto cutter = normalized(planeNormal);
    const auto projected = cutter - n * dot(cutter, n);
    if (length(projected) < 1e-8)
        throw std::runtime_error("Parallel or near-parallel cutting plane");
    const auto v = normalized(projected), u = cross(v, n);
    const auto faceOrigin = source.vertices.at(source.faces.at(face).loops[0][0]);
    const auto origin = faceOrigin + projected *
        (dot(cutter, point - faceOrigin) / dot(projected, projected));
    checkPoint(origin);
    constexpr double scale = 1e7;
    Paths64 paths;
    int64_t extent = 1;
    for (const auto &loop : source.faces.at(face).loops) {
        Path64 path;
        for (auto id : loop) {
            const auto d = source.vertices.at(id) - origin;
            const auto x = std::llround(dot(d, u) * scale);
            const auto y = std::llround(dot(d, v) * scale);
            extent = std::max(extent, std::max<int64_t>(std::abs(x), std::abs(y)) + 1);
            path.emplace_back(x, y);
        }
        if (IsPositive(path) != paths.empty()) std::reverse(path.begin(), path.end());
        paths.push_back(std::move(path));
    }
    std::vector<std::vector<std::vector<Vec3>>> loops;
    auto collect = [&](auto &&self, const PolyPath64 &node) -> void {
        for (const auto &child : node) {
            if (child->IsHole()) { self(self, *child); continue; }
            std::vector<std::vector<Vec3>> faceLoops;
            auto convert = [&](const Path64 &path) {
                std::vector<Vec3> points;
                for (auto p : path)
                    points.push_back(origin + u * (double(p.x) / scale) + v * (double(p.y) / scale));
                faceLoops.push_back(std::move(points));
            };
            convert(child->Polygon());
            for (const auto &hole : *child) convert(hole->Polygon());
            loops.push_back(std::move(faceLoops));
            self(self, *child);
        }
    };
    size_t firstCount = 0;
    for (int side : {1, -1}) {
        const int64_t bottom = side == 1 ? 0 : -extent;
        const int64_t top = side == 1 ? extent : 0;
        Clipper64 clipper;
        clipper.AddSubject(paths);
        clipper.AddClip({{{-extent, bottom}, {extent, bottom}, {extent, top}, {-extent, top}}});
        PolyTree64 tree;
        if (!clipper.Execute(ClipType::Intersection, FillRule::NonZero, tree))
            throw std::runtime_error("Planar partition failed");
        collect(collect, tree);
        if (side == 1) firstCount = loops.size();
    }
    if (firstCount == 0 || firstCount == loops.size())
        return {source, {{face, {face}}}}; // Tangent/outside cut is a true no-op.
    FacePartition result{source, {{face, {}}}};
    result.surface.faces.erase(face);
    for (const auto &outline : loops)
        result.descendants.at(face).push_back(result.surface.addFace(outline));
    double area = 0;
    for (const auto &[id, record] : result.surface.faces) area += result.surface.area(id);
    const auto expected = source.area(face);
    if (std::abs(area - expected) > std::max(1e-8, expected * 1e-8))
        throw std::runtime_error("Partition failed area conservation");
    result.surface.validate();
    return result;
}
} // namespace sketchy
