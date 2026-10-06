#include "core/shading_normals.hpp"
#include <algorithm>
#include <numeric>
#include <set>
namespace sketchy {
namespace {
struct Fan {
    std::vector<size_t> parents, ranks;
    size_t add() {
        const auto id = parents.size();
        parents.push_back(id);
        ranks.push_back(0);
        return id;
    }
    size_t root(size_t id) {
        while (parents[id] != id) {
            parents[id] = parents[parents[id]];
            id = parents[id];
        }
        return id;
    }
    void join(size_t a, size_t b) {
        a = root(a);
        b = root(b);
        if (a == b)
            return;
        if (ranks[a] < ranks[b])
            std::swap(a, b);
        parents[b] = a;
        if (ranks[a] == ranks[b])
            ++ranks[a];
    }
};
struct Sum {
    long double x{}, y{}, z{}, weight{};
    void add(Vec3 n, double area) {
        x += static_cast<long double>(n.x) * area;
        y += static_cast<long double>(n.y) * area;
        z += static_cast<long double>(n.z) * area;
        weight += area;
    }
    Vec3 normal(Vec3 fallback) const {
        const auto magnitude = std::sqrt(x * x + y * y + z * z);
        if (magnitude <= weight * 1e-12L)
            return fallback;
        return {double(x / magnitude), double(y / magnitude), double(z / magnitude)};
    }
};
} // namespace
ShadingNormals::Key ShadingNormals::FaceNormals::key(Vec3 point) const {
    // The same basis and quantization as Surface::triangulate. Lookup is local
    // to one face: coincident coordinates never connect unrelated identities.
    const auto relative = point - origin;
    return {std::llround(dot(relative, u) * 1e7), std::llround(dot(relative, v) * 1e7)};
}
ShadingNormals::ShadingNormals(const Body &body) {
    const auto &surface = body.surface;
    if (surface.faces.size() > 100000 || body.topology.edges.size() > Topology::edgeLimit)
        throw std::runtime_error("Shading geometry exceeds supported bounds");
    size_t references = 0;
    for (const auto &[id, face] : surface.faces)
        for (const auto &loop : face.loops) {
            if (loop.size() > 10000 || loop.size() > 2000000 - references)
                throw std::runtime_error("Shading exceeds two million face corners");
            references += loop.size();
        }
    const bool smoothing = std::any_of(body.edgeAppearances.begin(), body.edgeAppearances.end(),
                                       [](const auto &entry) { return entry.second.smooth; });
    std::map<Id, double> areas;
    for (const auto &[id, face] : surface.faces) {
        auto &data = faces_[id];
        data.normal = surface.normal(id);
        if (!smoothing)
            continue;
        data.origin = surface.vertices.at(face.loops.front().front());
        data.u = normalized(surface.vertices.at(face.loops.front()[1]) - data.origin);
        data.v = cross(data.normal, data.u);
        double area = 0;
        for (size_t loopIndex = 0; loopIndex < face.loops.size(); ++loopIndex) {
            const auto &loop = face.loops[loopIndex];
            Vec3 vector{};
            const auto origin = surface.vertices.at(loop.front());
            for (size_t i = 0; i < loop.size(); ++i)
                vector = vector + cross(surface.vertices.at(loop[i]) - origin,
                                        surface.vertices.at(loop[(i + 1) % loop.size()]) - origin);
            const auto loopArea = std::abs(dot(vector, data.normal)) * .5;
            area += loopIndex == 0 ? loopArea : -loopArea;
        }
        if (!std::isfinite(area) || area <= 0)
            throw std::runtime_error("Shading face has invalid net area");
        areas[id] = area;
    }
    if (!smoothing)
        return;
    const auto adjacency = body.topology.adjacency(surface);
    using Corner = std::pair<Id, Id>; // face, authoritative vertex
    std::map<Corner, size_t> nodes;
    Fan fan;
    auto node = [&](Id face, Id vertex) {
        auto [it, added] = nodes.try_emplace({face, vertex}, fan.parents.size());
        if (added)
            fan.add();
        return it->second;
    };
    for (const auto &[id, style] : body.edgeAppearances) {
        if (!style.smooth)
            continue;
        const auto &edge = body.topology.edges.at(id);
        const auto &incident = adjacency.edgeFaces.at(id);
        if (incident.size() != 2 || incident[0].face == incident[1].face ||
            incident[0].reversed == incident[1].reversed)
            continue;
        for (auto vertex : {edge.a, edge.b})
            fan.join(node(incident[0].face, vertex), node(incident[1].face, vertex));
    }
    std::vector<Sum> sums(fan.parents.size());
    for (const auto &[corner, index] : nodes)
        sums[fan.root(index)].add(faces_.at(corner.first).normal, areas.at(corner.first));
    for (const auto &[corner, index] : nodes) {
        auto &face = faces_.at(corner.first);
        face.corners[corner.second] = sums[fan.root(index)].normal(face.normal);
    }
    for (auto &[id, data] : faces_) {
        if (data.corners.empty())
            continue;
        std::set<Key> seen;
        for (const auto &loop : surface.faces.at(id).loops)
            for (auto vertex : loop) {
                const auto projected = data.key(surface.vertices.at(vertex));
                if (!seen.insert(projected).second) {
                    data.projected[projected] = data.normal;
                    continue; // Quantized aliases never pick an arbitrary corner.
                }
                data.projected[projected] = corner(id, vertex);
            }
    }
}
Vec3 ShadingNormals::corner(Id face, Id vertex) const {
    const auto &data = faces_.at(face);
    const auto found = data.corners.find(vertex);
    return found == data.corners.end() ? data.normal : found->second;
}
std::array<Vec3, 3> ShadingNormals::triangle(const Triangle &triangle) const {
    const auto &data = faces_.at(triangle.face);
    auto at = [&](Vec3 point) {
        if (data.projected.empty())
            return data.normal;
        const auto found = data.projected.find(data.key(point));
        return found == data.projected.end() ? data.normal : found->second;
    };
    return {at(triangle.a), at(triangle.b), at(triangle.c)};
}
} // namespace sketchy
