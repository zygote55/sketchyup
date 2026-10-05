#include "geometry/boolean.hpp"
#include <algorithm>
#include <limits>
#include <manifold/manifold.h>
#include <numeric>
#include <set>
#include <tuple>
namespace sketchy {
namespace {
using Mesh = manifold::MeshGL64;
using Index = uint64_t;
using Pair = std::pair<Index, Index>;
constexpr size_t inputVertices = 4096, inputFaces = 2048, triangleLimit = 8192;
constexpr size_t outputVertices = 16384, outputTriangles = 32768;
[[noreturn]] void fail(const char *code, const std::string &message) {
    throw BooleanError(code, message);
}
void validateInput(const Surface &s, int operand) {
    if (s.vertices.size() > inputVertices || s.faces.size() > inputFaces)
        throw BooleanError("BOOLEAN_LIMIT", "Solid operand exceeds the bounded adapter size",
                           operand);
    try {
        s.validate();
        const auto report = inspectSolid(s, Topology::rebuild(s, {}));
        if (report.status != "solid")
            throw BooleanError("BOOLEAN_INVALID_SOLID", "Solid operand: " + report.status, operand,
                               report);
    } catch (const BooleanError &) {
        throw;
    } catch (const std::exception &e) {
        throw BooleanError("BOOLEAN_INVALID_SOLID", e.what(), operand);
    }
}
Vec3 originFor(const Surface &a, const Surface &b) {
    auto low = a.vertices.begin()->second, high = low;
    for (auto s : {&a, &b})
        for (const auto &[id, p] : s->vertices) {
            low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
            high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
        }
    return (low + high) * .5;
}
struct Frame {
    Vec3 origin, u, v, normal;
    Vec3 localVector(Vec3 p) const { return {dot(p, u), dot(p, v), dot(p, normal)}; }
    Vec3 local(Vec3 p) const { return localVector(p - origin); }
    Vec3 world(Vec3 p) const { return origin + u * p.x + v * p.y + normal * p.z; }
    Vec3 rounded(Vec3 p) const {
        const auto q = local(p);
        constexpr auto grid = tolerance / 8;
        return {std::round(q.x / grid) * grid, std::round(q.y / grid) * grid,
                std::round(q.z / grid) * grid};
    }
};
Frame frameFor(const Surface &a, const Surface &b) {
    const auto &face = a.faces.begin()->second;
    const auto normal = a.normal(face.id);
    const auto edge = a.vertices.at(face.loops[0][1]) - a.vertices.at(face.loops[0][0]);
    const auto u = normalized(edge - normal * dot(edge, normal));
    return {originFor(a, b), u, normalized(cross(normal, u)), normal};
}
Mesh inputMesh(const Surface &s, const Frame &frame, uint32_t original, unsigned operand,
               bool &reversed) {
    Mesh mesh;
    std::map<Id, Index> indices;
    for (const auto &[id, p] : s.vertices) {
        indices[id] = mesh.NumVert();
        const auto q = frame.rounded(p);
        mesh.vertProperties.insert(mesh.vertProperties.end(), {q.x, q.y, q.z});
    }
    long double signedVolume{};
    size_t searchBudget = 4000000;
    for (const auto &[face, record] : s.faces) {
        std::set<Id> corners;
        for (const auto &loop : record.loops)
            corners.insert(loop.begin(), loop.end());
        for (const auto &triangle : s.triangulate(face)) {
            if (mesh.NumTri() >= triangleLimit)
                fail("BOOLEAN_LIMIT", "Solid tessellation exceeds the triangle limit");
            std::array<Vec3, 3> exact;
            unsigned corner{};
            for (auto p : {triangle.a, triangle.b, triangle.c}) {
                Id match{};
                for (auto id : corners) {
                    if (!searchBudget--)
                        fail("BOOLEAN_LIMIT", "Solid tessellation mapping exceeds its work limit");
                    if (length(s.vertices.at(id) - p) <= tolerance) {
                        if (match)
                            fail("BOOLEAN_TESSELLATION",
                                 "Triangle corner has ambiguous native identity");
                        match = id;
                    }
                }
                if (!match)
                    fail("BOOLEAN_TESSELLATION",
                         "Triangle corner cannot recover its native identity");
                exact[corner++] = frame.rounded(s.vertices.at(match));
                mesh.triVerts.push_back(indices.at(match));
            }
            signedVolume += static_cast<long double>(dot(exact[0], cross(exact[1], exact[2])));
            mesh.faceID.push_back(face);
        }
    }
    // Native solids admit either global winding; Manifold requires outward input.
    reversed = signedVolume < 0;
    if (reversed)
        for (size_t i = 0; i < mesh.triVerts.size(); i += 3)
            std::swap(mesh.triVerts[i + 1], mesh.triVerts[i + 2]);
    mesh.runIndex = {0, mesh.triVerts.size()};
    mesh.runOriginalID = {original};
    // Absorb roundoff below native resolution without collapsing native-sized
    // features. Every reconstructed boundary is still validated at native tolerance.
    mesh.tolerance = tolerance / 8;
    manifold::Manifold check(mesh);
    if (check.Status() != manifold::Manifold::Error::NoError)
        throw BooleanError("BOOLEAN_TESSELLATION",
                           "Native solid tessellation is not a closed manifold: " +
                               std::to_string(int(check.Status())),
                           operand);
    return mesh;
}
struct Group {
    unsigned operand;
    Id face;
    bool reversed;
    auto operator<=>(const Group &) const = default;
};
Vec3 point(const Mesh &mesh, Index i) {
    if (i >= mesh.NumVert())
        fail("BOOLEAN_OUTPUT", "Boolean result has an invalid vertex index");
    return {mesh.vertProperties[i * mesh.numProp], mesh.vertProperties[i * mesh.numProp + 1],
            mesh.vertProperties[i * mesh.numProp + 2]};
}
// A loop containment test in the face plane; boundary-touching/ambiguous holes
// are rejected by native face validation after reconstruction.
bool contains(const Surface &s, const std::vector<Id> &loop, Vec3 p, Vec3 u, Vec3 v) {
    bool inside{};
    for (size_t i = 0; i < loop.size(); ++i) {
        const auto a = s.vertices.at(loop[i]) - p;
        const auto b = s.vertices.at(loop[(i + 1) % loop.size()]) - p;
        const auto ax = dot(a, u), ay = dot(a, v), bx = dot(b, u), by = dot(b, v);
        if ((ay > 0) != (by > 0) && ax + (bx - ax) * (-ay) / (by - ay) > 0)
            inside = !inside;
    }
    return inside;
}
BooleanPart restore(const manifold::Manifold &solid, const Surface &a, const Surface &b,
                    const Frame &frame, uint32_t firstId, const std::array<bool, 2> &reversed) {
    const auto mesh = solid.GetMeshGL64();
    if (mesh.NumVert() > outputVertices || mesh.NumTri() > outputTriangles)
        fail("BOOLEAN_LIMIT", "Boolean output exceeds the native reconstruction limit");
    if (mesh.numProp != 3 || mesh.faceID.size() != mesh.NumTri() ||
        mesh.runIndex.size() != mesh.runOriginalID.size() + 1 ||
        mesh.mergeFromVert.size() != mesh.mergeToVert.size())
        fail("BOOLEAN_PROVENANCE", "Boolean output is missing its native face provenance");
    std::vector<Index> roots(mesh.NumVert());
    std::iota(roots.begin(), roots.end(), 0);
    auto root = [&](Index id) {
        if (id >= roots.size())
            fail("BOOLEAN_OUTPUT", "Boolean output has an invalid merge index");
        while (roots[id] != id) {
            roots[id] = roots[roots[id]];
            id = roots[id];
        }
        return id;
    };
    for (size_t i = 0; i < mesh.mergeFromVert.size(); ++i) {
        const auto from = root(mesh.mergeFromVert[i]), to = root(mesh.mergeToVert[i]);
        if (length(point(mesh, from) - point(mesh, to)) > tolerance)
            fail("BOOLEAN_OUTPUT", "Boolean output merge exceeds native tolerance");
        roots[std::max(from, to)] = std::min(from, to);
    }
    // Collapse only connected sub-resolution output edges, never merely nearby
    // vertices on separate sheets. Provenance constraints can retain tiny adapter
    // slivers even after Simplify. Bound every cluster's displacement explicitly.
    for (Index tri = 0; tri < mesh.NumTri(); ++tri)
        for (size_t j = 0; j < 3; ++j) {
            const auto x = root(mesh.triVerts[tri * 3 + j]);
            const auto y = root(mesh.triVerts[tri * 3 + (j + 1) % 3]);
            if (length(point(mesh, x) - point(mesh, y)) <= tolerance / 4)
                roots[std::max(x, y)] = std::min(x, y);
        }
    for (Index i = 0; i < mesh.NumVert(); ++i)
        if (length(point(mesh, i) - point(mesh, root(i))) > tolerance / 4)
            fail("BOOLEAN_PRECISION", "Output edge normalization exceeds its displacement budget");
    std::map<Group, std::map<Pair, int>> boundaries;
    size_t run{};
    for (Index tri = 0; tri < mesh.NumTri(); ++tri) {
        while (run + 1 < mesh.runOriginalID.size() && tri * 3 >= mesh.runIndex[run + 1])
            ++run;
        if (mesh.runOriginalID.empty() || mesh.runOriginalID[run] < firstId ||
            mesh.runOriginalID[run] > firstId + 1)
            fail("BOOLEAN_PROVENANCE", "Boolean result references an unknown source operand");
        const unsigned operand = mesh.runOriginalID[run] - firstId;
        const auto &source = operand ? b : a;
        const auto face = mesh.faceID[tri];
        if (!source.faces.contains(face))
            fail("BOOLEAN_PROVENANCE", "Boolean result references an unknown source face");
        std::array<Index, 3> ids;
        for (size_t j = 0; j < 3; ++j)
            ids[j] = root(mesh.triVerts[tri * 3 + j]);
        if (ids[0] == ids[1] || ids[1] == ids[2] || ids[2] == ids[0])
            continue;
        const auto crossNormal = cross(point(mesh, ids[1]) - point(mesh, ids[0]),
                                       point(mesh, ids[2]) - point(mesh, ids[0]));
        const auto sourceNormal = frame.localVector(source.normal(face));
        const auto size = length(crossNormal);
        if (size <= tolerance * tolerance)
            fail("BOOLEAN_OUTPUT", "Boolean triangle is below native area tolerance");
        // Tiny, skinny tessellation triangles have ill-conditioned normals after
        // roundoff normalization. Test distance to the actual source plane.
        const auto planeOrigin = frame.local(source.vertices.at(source.faces.at(face).loops[0][0]));
        for (auto index : ids)
            if (std::abs(dot(point(mesh, index) - planeOrigin, sourceNormal)) > tolerance)
                fail("BOOLEAN_PROVENANCE", "Boolean triangle left its source face plane");
        const auto longest = std::max({length(point(mesh, ids[1]) - point(mesh, ids[0])),
                                       length(point(mesh, ids[2]) - point(mesh, ids[1])),
                                       length(point(mesh, ids[0]) - point(mesh, ids[2]))});
        if (size > longest * tolerance * 4 &&
            (dot(crossNormal, sourceNormal) < 0) != reversed[operand])
            fail("BOOLEAN_PROVENANCE", "Boolean triangle has inconsistent source orientation");
        auto &edges = boundaries[{operand, face, reversed[operand]}];
        for (size_t j = 0; j < 3; ++j) {
            const auto x = ids[j], y = ids[(j + 1) % 3];
            edges[{std::min(x, y), std::max(x, y)}] += x < y ? 1 : -1;
        }
    }
    BooleanPart part;
    std::map<Index, Id> vertices;
    auto vertex = [&](Index index) {
        if (!vertices.contains(index)) {
            const auto p = frame.world(point(mesh, index));
            checkPoint(p);
            const Id id = part.surface.nextId++;
            part.surface.vertices[id] = p;
            vertices[index] = id;
        }
        return vertices.at(index);
    };
    size_t workBudget = 4000000;
    for (const auto &[group, edges] : boundaries) {
        const auto &source = group.operand ? b : a;
        const auto normal = source.normal(group.face) * (group.reversed ? -1 : 1);
        std::set<Pair> remaining;
        std::map<Index, std::vector<Index>> outgoing;
        for (const auto &[edge, count] : edges) {
            if (!count)
                continue;
            if (std::abs(count) != 1)
                fail("BOOLEAN_OUTPUT", "Boolean face has overlapping boundary incidence");
            const auto from = count > 0 ? edge.first : edge.second;
            const auto to = count > 0 ? edge.second : edge.first;
            remaining.emplace(from, to);
            outgoing[from].push_back(to);
        }
        std::map<Pair, Pair> next;
        std::set<Pair> claimed;
        for (auto edge : remaining) {
            const auto incoming = point(mesh, edge.second) - point(mesh, edge.first);
            std::optional<Pair> choice;
            double best = -4;
            for (auto target : outgoing[edge.second]) {
                if (!workBudget--)
                    fail("BOOLEAN_LIMIT", "Boolean boundary tracing exceeds its work limit");
                const auto direction = point(mesh, target) - point(mesh, edge.second);
                const auto angle =
                    std::atan2(dot(frame.localVector(normal), cross(incoming, direction)),
                               dot(incoming, direction));
                if (choice && std::abs(angle - best) < 1e-12)
                    fail("BOOLEAN_OUTPUT", "Boolean boundary has ambiguous coincident directions");
                if (angle > best) {
                    best = angle;
                    choice = Pair{edge.second, target};
                }
            }
            if (!choice || !claimed.insert(*choice).second)
                fail("BOOLEAN_OUTPUT", "Boolean boundary does not have a closed planar pairing");
            next[edge] = *choice;
        }
        std::vector<std::vector<Id>> loops;
        while (!remaining.empty()) {
            const auto start = *remaining.begin();
            auto current = start;
            std::vector<Id> loop;
            do {
                if (!remaining.erase(current))
                    fail("BOOLEAN_OUTPUT", "Boolean boundary reuses a directed edge");
                loop.push_back(vertex(current.first));
                current = next.at(current);
            } while (current != start);
            if (loop.size() < 3)
                fail("BOOLEAN_OUTPUT", "Boolean face boundary is below native tolerance");
            // Provenance seams can retain a zero-width out-and-back spike.
            // Remove only a backtrack within 25 nm of the remaining segment;
            // shared boundary vertices are propagated across faces below.
            bool changed = true;
            while (changed && loop.size() > 3) {
                changed = false;
                for (size_t j = 0; j < loop.size(); ++j) {
                    if (!workBudget--)
                        fail("BOOLEAN_LIMIT", "Boolean spike normalization exceeds its work limit");
                    const auto a =
                        part.surface.vertices.at(loop[(j + loop.size() - 1) % loop.size()]);
                    const auto b = part.surface.vertices.at(loop[j]);
                    const auto c = part.surface.vertices.at(loop[(j + 1) % loop.size()]);
                    const auto d = c - a;
                    const auto size = length(d);
                    if (size <= tolerance)
                        continue;
                    const auto along = dot(b - a, d) / size;
                    if ((along < 0 || along > size) &&
                        length(cross(b - a, d)) / size <= tolerance / 4) {
                        loop.erase(loop.begin() + j);
                        changed = true;
                        break;
                    }
                }
            }
            loops.push_back(std::move(loop));
        }
        const auto axis = std::abs(normal.x) < .8 ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
        const auto u = normalized(cross(normal, axis)), v = cross(normal, u);
        std::vector<size_t> outers, holes;
        for (size_t i = 0; i < loops.size(); ++i) {
            double signedArea{};
            const auto o = part.surface.vertices.at(loops[i][0]);
            for (size_t j = 0; j < loops[i].size(); ++j)
                signedArea +=
                    dot(normal,
                        cross(part.surface.vertices.at(loops[i][j]) - o,
                              part.surface.vertices.at(loops[i][(j + 1) % loops[i].size()]) - o));
            if (std::abs(signedArea) <= 2 * tolerance * tolerance)
                fail("BOOLEAN_OUTPUT", "Boolean boundary area is below native tolerance");
            (signedArea > 0 ? outers : holes).push_back(i);
        }
        std::map<size_t, std::vector<size_t>> owned;
        for (auto hole : holes) {
            std::optional<size_t> owner;
            for (auto outer : outers) {
                if (workBudget < loops[outer].size())
                    fail("BOOLEAN_LIMIT", "Boolean hole classification exceeds its work limit");
                workBudget -= loops[outer].size();
                if (contains(part.surface, loops[outer], part.surface.vertices.at(loops[hole][0]),
                             u, v)) {
                    if (owner)
                        fail("BOOLEAN_OUTPUT", "Boolean hole has ambiguous outer boundary");
                    owner = outer;
                }
            }
            if (!owner)
                fail("BOOLEAN_OUTPUT", "Boolean hole has no outer boundary");
            owned[*owner].push_back(hole);
        }
        for (auto outer : outers) {
            std::vector<std::vector<Id>> faceLoops{loops[outer]};
            for (auto hole : owned[outer])
                faceLoops.push_back(loops[hole]);
            const auto face = part.surface.addFaceIds(std::move(faceLoops));
            part.sources[face] = {group.operand, group.face, group.reversed};
        }
    }
    std::set<Id> used;
    for (const auto &[id, face] : part.surface.faces)
        for (const auto &loop : face.loops)
            used.insert(loop.begin(), loop.end());
    std::erase_if(part.surface.vertices,
                  [&](const auto &entry) { return !used.contains(entry.first); });
    for (auto &[id, face] : part.surface.faces)
        for (auto &loop : face.loops) {
            std::vector<Id> split;
            for (size_t j = 0; j < loop.size(); ++j) {
                const auto a = part.surface.vertices.at(loop[j]);
                const auto b = part.surface.vertices.at(loop[(j + 1) % loop.size()]);
                const auto d = b - a;
                const auto size = length(d);
                if (size < tolerance)
                    fail("BOOLEAN_OUTPUT", "Boolean edge is below native tolerance");
                std::vector<std::pair<double, Id>> interior;
                for (const auto &[vertex, p] : part.surface.vertices) {
                    if (!workBudget--)
                        fail("BOOLEAN_LIMIT", "Boolean boundary splitting exceeds its work limit");
                    const auto along = dot(p - a, d) / size;
                    if (along > tolerance / 4 && along < size - tolerance / 4 &&
                        length(cross(p - a, d)) / size <= tolerance / 4)
                        interior.emplace_back(along, vertex);
                }
                std::sort(interior.begin(), interior.end());
                split.push_back(loop[j]);
                for (auto [along, vertex] : interior)
                    split.push_back(vertex);
            }
            loop = std::move(split);
        }
    part.surface.validate();
    const auto report = inspectSolid(part.surface, Topology::rebuild(part.surface, {}));
    if (report.status != "solid")
        throw BooleanError("BOOLEAN_OUTPUT", "Reconstructed Boolean result: " + report.status, -1,
                           report);
    part.volume = *report.volume;
    double area{};
    for (const auto &[id, face] : part.surface.faces)
        area += part.surface.area(id);
    if (std::abs(part.volume - solid.Volume()) > std::max(area * tolerance * 4, part.volume * 1e-9))
        fail("BOOLEAN_OUTPUT", "Reconstructed Boolean volume differs from the adapter result");
    return part;
}
} // namespace
BooleanResult booleanSolids(const Surface &a, const Surface &b, BooleanOperation operation) {
    validateInput(a, 0);
    validateInput(b, 1);
    const auto frame = frameFor(a, b);
    const auto first = manifold::Manifold::ReserveIDs(2);
    std::array<bool, 2> reversed{};
    const manifold::Manifold left(inputMesh(a, frame, first, 0, reversed[0]));
    const manifold::Manifold right(inputMesh(b, frame, first + 1, 1, reversed[1]));
    if (operation == BooleanOperation::Subtract)
        reversed[1] = !reversed[1];
    manifold::OpType op;
    switch (operation) {
    case BooleanOperation::Union:
        op = manifold::OpType::Add;
        break;
    case BooleanOperation::Subtract:
        op = manifold::OpType::Subtract;
        break;
    case BooleanOperation::Intersect:
        op = manifold::OpType::Intersect;
        break;
    default:
        fail("BOOLEAN_OPERATION", "Unknown solid Boolean operation");
    }
    if (std::max(left.GetTolerance(), right.GetTolerance()) > tolerance / 4)
        fail("BOOLEAN_PRECISION", "Operand extent exceeds the native Boolean precision budget");
    if (size_t(left.NumTri()) * size_t(right.NumTri()) > 4000000)
        fail("BOOLEAN_LIMIT", "Boolean operand pair exceeds the bounded triangle work budget");
    const auto result = left.Boolean(right, op).Simplify(tolerance / 4);
    if (result.Status() != manifold::Manifold::Error::NoError)
        fail("BOOLEAN_ADAPTER",
             "Solid Boolean adapter status " + std::to_string(int(result.Status())));
    BooleanResult out;
    if (result.IsEmpty())
        return out;
    if (result.NumTri() > outputTriangles || result.NumVert() > outputVertices)
        fail("BOOLEAN_LIMIT", "Boolean result exceeds the bounded native output size");
    const auto shells = result.Decompose();
    if (shells.size() > 64)
        fail("BOOLEAN_LIMIT", "Boolean result exceeds 64 disconnected shells");
    for (const auto &shell : shells)
        if (shell.Volume() < 0)
            fail("BOOLEAN_CAVITY", "Enclosed cavity shells require native containment support");
    try {
        for (const auto &shell : shells)
            if (shell.Volume() != 0)
                out.parts.push_back(restore(shell, a, b, frame, first, reversed));
    } catch (const BooleanError &) {
        throw;
    } catch (const std::exception &e) {
        fail("BOOLEAN_OUTPUT", e.what());
    }
    // Stable part ordering uses world bounds, independent of adapter global IDs.
    auto key = [](const BooleanPart &part) {
        auto low = part.surface.vertices.begin()->second;
        for (const auto &[id, p] : part.surface.vertices)
            low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
        return std::tuple{low.x, low.y, low.z, part.volume};
    };
    std::sort(out.parts.begin(), out.parts.end(),
              [&](const auto &x, const auto &y) { return key(x) < key(y); });
    for (const auto &part : out.parts)
        out.volume += part.volume;
    return out;
}
} // namespace sketchy
