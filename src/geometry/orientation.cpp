#include "geometry/orientation.hpp"
#include <algorithm>
namespace sketchy {
namespace {
[[noreturn]] void fail(const char *code, const char *message, Id face = 0, Id edge = 0) {
    throw OrientationError(code, message, face, edge);
}
void validate(const Surface &surface, const Topology &topology) {
    if (surface.faces.empty() || surface.faces.size() > 16384 || surface.vertices.size() > 65536 ||
        topology.edges.size() > 131072 || surface.wires.size() > 131072)
        fail("ORIENTATION_LIMIT", "Face orientation requires 1–16384 faces and bounded topology");
    size_t uses{}, work{};
    for (const auto &[id, face] : surface.faces) {
        size_t boundary{};
        for (const auto &loop : face.loops) {
            if (loop.size() > 4096 || boundary > 4096 - loop.size())
                fail("ORIENTATION_LIMIT", "A face exceeds 4096 boundary vertices", id);
            boundary += loop.size();
        }
        uses += boundary;
        if (uses > 131072 || boundary * boundary > 4000000 - work)
            fail("ORIENTATION_LIMIT", "Face orientation exceeds its validation work budget", id);
        work += boundary * boundary;
    }
    surface.validate();
    topology.validate(surface);
}
FaceOrientationResult apply(const Surface &surface, const Topology &topology,
                            const std::map<Id, bool> &parity) {
    FaceOrientationResult result{surface, {}, {}};
    for (auto [id, reversed] : parity) {
        result.connected.push_back(id);
        if (reversed) {
            result.reversed.push_back(id);
            for (auto &loop : result.surface.faces.at(id).loops)
                std::reverse(loop.begin(), loop.end());
        }
    }
    // Reversal changes incidence directions, never authoritative edge records.
    topology.validate(result.surface);
    return result;
}
} // namespace
FaceOrientationResult reverseFaces(const Surface &surface, const Topology &topology,
                                   const std::set<Id> &faces) {
    validate(surface, topology);
    if (faces.empty() || faces.size() > 16384)
        fail("ORIENTATION_SELECTION", "Choose 1–16384 faces to reverse");
    std::map<Id, bool> parity;
    for (auto id : faces) {
        if (!surface.faces.contains(id))
            fail("ORIENTATION_SELECTION", "Selected face does not exist", id);
        parity[id] = true;
    }
    return apply(surface, topology, parity);
}
FaceOrientationResult orientFaces(const Surface &surface, const Topology &topology, Id seed) {
    validate(surface, topology);
    if (!surface.faces.contains(seed))
        fail("ORIENTATION_SELECTION", "Choose an existing reference face", seed);
    const auto adjacency = topology.adjacency(surface);
    std::map<Id, bool> parity{{seed, false}};
    std::vector<Id> pending{seed};
    for (size_t index = 0; index < pending.size(); ++index) {
        const auto face = pending[index];
        for (const auto &loop : adjacency.faceLoops.at(face))
            for (const auto &edge : loop) {
                const auto &incident = adjacency.edgeFaces.at(edge.edge);
                if (incident.size() > 2)
                    fail("ORIENTATION_NON_MANIFOLD", "Connected component has a non-manifold edge",
                         face, edge.edge);
                if (incident.size() != 2)
                    continue;
                const auto &a = incident[0], &b = incident[1];
                const auto next = a.face == face ? b.face : a.face;
                if (next == face)
                    fail("ORIENTATION_NON_MANIFOLD", "A face uses the same boundary edge twice",
                         face, edge.edge);
                const bool reversed = parity.at(face) != (a.reversed == b.reversed);
                const auto [found, inserted] = parity.emplace(next, reversed);
                if (inserted)
                    pending.push_back(next);
                else if (found->second != reversed)
                    fail("ORIENTATION_CONFLICT", "Connected faces have contradictory orientation",
                         next, edge.edge);
            }
    }
    return apply(surface, topology, parity);
}
} // namespace sketchy
