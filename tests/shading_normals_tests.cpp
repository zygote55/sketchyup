#include "core/shading_normals.hpp"
#include <algorithm>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void near(Vec3 a, Vec3 b, const char *message) { check(length(a - b) < 2e-8, message); }
Body perpendicular(double scale = 1) {
    Body body;
    body.id = 1;
    body.surface.addFace({{{0, 0, 0}, {2 * scale, 0, 0}, {2 * scale, scale, 0}, {0, scale, 0}}});
    body.surface.addFace(
        {{{2 * scale, 0, 0}, {0, 0, 0}, {0, 0, 3 * scale}, {2 * scale, 0, 3 * scale}}});
    body.topology = Topology::rebuild(body.surface, {});
    return body;
}
Id seam(const Body &body) {
    const auto adjacency = body.topology.adjacency(body.surface);
    for (const auto &[id, incident] : adjacency.edgeFaces)
        if (incident.size() == 2)
            return id;
    throw std::runtime_error("Missing seam");
}
void weighted() {
    for (const auto scale : {1., .0001, 1000.}) {
        auto body = perpendicular(scale);
        const auto edge = seam(body);
        body.edgeAppearances[edge] = {true, true, false};
        ShadingNormals hard(body);
        for (const auto &[face, record] : body.surface.faces)
            near(hard.corner(face, body.topology.edges.at(edge).a), body.surface.normal(face),
                 "Hidden/soft alone retain hard normals");
        body.edgeAppearances[edge].smooth = true;
        const auto before = body;
        const ShadingNormals smooth(body);
        const auto expected = normalized({0, 3, 1});
        for (const auto &[face, record] : body.surface.faces) {
            for (auto vertex : record.loops.front()) {
                const auto p = body.surface.vertices.at(vertex);
                const bool shared = p.y == 0 && p.z == 0;
                near(smooth.corner(face, vertex), shared ? expected : body.surface.normal(face),
                     "Only shared fan corners get independent area-weighted normal");
            }
            for (auto triangle : body.surface.triangulate(face)) {
                const auto normals = smooth.triangle(triangle);
                const std::array<Vec3, 3> points{triangle.a, triangle.b, triangle.c};
                for (size_t i = 0; i < 3; ++i) {
                    const bool shared =
                        std::abs(points[i].y) < tolerance && std::abs(points[i].z) < tolerance;
                    near(normals[i], shared ? expected : body.surface.normal(face),
                         "Quantized triangulation corners use the same fan");
                }
            }
        }
        check(body == before, "Derived shading leaves all body data unchanged");
    }
}
void cubeFans() {
    Body body;
    body.id = 1;
    const auto face = body.surface.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
    body.surface.extrude(face, 1);
    body.topology = Topology::rebuild(body.surface, {});
    for (const auto &[id, edge] : body.topology.edges)
        body.edgeAppearances[id].smooth = true;
    const auto original = body;
    const ShadingNormals normals(body);
    for (const auto &[faceId, record] : body.surface.faces)
        for (auto vertex : record.loops.front())
            near(normals.corner(faceId, vertex),
                 normalized(body.surface.vertices.at(vertex) - Vec3{.5, .5, .5}),
                 "Three-face fans join transitively at all eight cube corners");
    check(body == original, "Smooth cube retains original topology and geometry");
    // One marked edge connects its two faces, but the third face at the same
    // authoritative vertex still has a hard corner.
    body.edgeAppearances.clear();
    const auto edge = body.topology.edges.begin()->first;
    body.edgeAppearances[edge].smooth = true;
    const auto adjacency = body.topology.adjacency(body.surface);
    const auto &incident = adjacency.edgeFaces.at(edge);
    const auto a = incident[0].face, b = incident[1].face;
    const auto endpoint = body.topology.edges.at(edge).a;
    const auto expected = normalized(body.surface.normal(a) + body.surface.normal(b));
    ShadingNormals partial(body);
    for (const auto &[faceId, record] : body.surface.faces)
        if (std::find(record.loops.front().begin(), record.loops.front().end(), endpoint) !=
            record.loops.front().end())
            near(partial.corner(faceId, endpoint),
                 faceId == a || faceId == b ? expected : body.surface.normal(faceId),
                 "Hard seam remains separate at a shared vertex");
}
void boundaries() {
    auto body = perpendicular();
    const auto edge = seam(body);
    body.edgeAppearances[edge].smooth = true;
    const auto face = body.surface.faces.rbegin()->first;
    std::reverse(body.surface.faces.at(face).loops[0].begin(),
                 body.surface.faces.at(face).loops[0].end());
    ShadingNormals inconsistent(body);
    for (const auto &[id, record] : body.surface.faces)
        near(inconsistent.corner(id, body.topology.edges.at(edge).a), body.surface.normal(id),
             "Inconsistent winding does not smooth");
    body = perpendicular();
    body.surface.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 0, -1}, {0, 0, -1}}});
    body.topology = Topology::rebuild(body.surface, body.topology);
    body.edgeAppearances[edge].smooth = true;
    ShadingNormals nonmanifold(body);
    for (const auto &[id, record] : body.surface.faces)
        near(nonmanifold.corner(id, body.topology.edges.at(edge).a), body.surface.normal(id),
             "Radial nonmanifold edge never guesses a face pairing");
    body = perpendicular();
    body.edgeAppearances[edge].smooth = true;
    // Separate native identities at exactly the same coordinates remain flat.
    std::vector<Id> duplicate;
    const auto originalFace = body.surface.faces.begin()->first;
    for (auto vertex : body.surface.faces.at(originalFace).loops.front()) {
        const auto id = body.surface.nextId++;
        body.surface.vertices[id] = body.surface.vertices.at(vertex);
        duplicate.push_back(id);
    }
    const auto disconnected = body.surface.addFaceIds({duplicate});
    body.topology = Topology::rebuild(body.surface, body.topology);
    ShadingNormals distinct(body);
    for (auto vertex : duplicate)
        near(distinct.corner(disconnected, vertex), {0, 0, 1},
             "Coincident disconnected identities never share a fan");
    Body cancel;
    cancel.surface.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
    auto reversed = cancel.surface.faces.begin()->second.loops;
    std::reverse(reversed[0].begin(), reversed[0].end());
    cancel.surface.addFaceIds(reversed);
    cancel.topology = Topology::rebuild(cancel.surface, {});
    for (const auto &[id, record] : cancel.topology.edges)
        cancel.edgeAppearances[id].smooth = true;
    ShadingNormals zero(cancel);
    for (const auto &[id, record] : cancel.surface.faces)
        for (auto vertex : record.loops.front())
            near(zero.corner(id, vertex), cancel.surface.normal(id),
                 "Cancelled fan safely falls back to each face normal");
}
void holesAndPlacement() {
    Body body;
    body.surface.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}},
                          {{.5, .5, 0}, {1, .5, 0}, {1, 1, 0}, {.5, 1, 0}}});
    body.surface.addFace({{{2, 0, 0}, {0, 0, 0}, {0, 0, 1}, {2, 0, 1}}});
    body.topology = Topology::rebuild(body.surface, {});
    body.edgeAppearances[seam(body)].smooth = true;
    const auto expected = normalized({0, 2, 3.75});
    const auto first = body.surface.faces.begin()->first;
    near(ShadingNormals(body).corner(first, 1), expected,
         "Hole area is removed from face weight independent of loop winding");
    const auto transform =
        Transform::translation({500000, -400000, 300000}) * Transform::rotation({1, 2, 3}, .716);
    for (auto &[id, point] : body.surface.vertices)
        point = transform.point(point);
    const ShadingNormals oblique(body);
    size_t sharedCorners = 0;
    for (const auto &triangle : body.surface.triangles()) {
        const auto normals = oblique.triangle(triangle);
        const std::array<Vec3, 3> points{triangle.a, triangle.b, triangle.c};
        for (size_t i = 0; i < 3; ++i)
            if (length(points[i] - transform.point({0, 0, 0})) < 2e-7 ||
                length(points[i] - transform.point({2, 0, 0})) < 2e-7) {
                near(normals[i], transform.vector(expected),
                     "Far oblique triangulation retains shared quantized corner normals");
                ++sharedCorners;
            }
    }
    check(sharedCorners >= 4, "Both oblique faces exercise smooth triangle corners");
}
} // namespace
int main() {
    try {
        weighted();
        cubeFans();
        boundaries();
        holesAndPlacement();
        std::cout << "Area-weighted smooth fans, hard boundaries and quantized corners passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
