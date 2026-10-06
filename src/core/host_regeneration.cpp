#include "core/host_regeneration.hpp"
#include "core/appearance.hpp"
#include "core/edge_appearance.hpp"
#include "core/face_textures.hpp"
#include <algorithm>
#include <set>
namespace sketchy {
namespace {
constexpr size_t openingLimit = 16;
[[noreturn]] void fail(const char *code, const char *message) { throw OpeningError(code, message); }
Id allocate(Id &next) {
    if (!next || next == UINT64_MAX)
        fail("HOST_LIMIT", "Hosted geometry identity space is exhausted");
    return next++;
}
void preflight(const Surface &surface, const std::map<Id, OpeningProfile> &profiles) {
    if (surface.faces.size() > 1000 || surface.vertices.size() > 10000 ||
        profiles.size() > openingLimit)
        fail("HOST_LIMIT", "Host regeneration accepts at most 16 bounded openings");
    size_t corners = 0;
    for (const auto &[id, face] : surface.faces) {
        if (face.loops.size() > 64)
            fail("HOST_LIMIT", "Host regeneration exceeds the boundary loop limit");
        for (const auto &loop : face.loops) {
            if (loop.size() > 4096 || loop.size() > 32000 - corners)
                fail("HOST_LIMIT", "Host regeneration exceeds the corner budget");
            corners += loop.size();
        }
    }
    for (const auto &[owner, profile] : profiles) {
        if (!owner || !profile.face || !surface.faces.contains(profile.face) ||
            profile.corners.size() < 3 || profile.corners.size() > 256)
            fail("HOST_RECORDS", "An opening needs a host face and 3 to 256 source corners");
        std::set<Id> keys;
        for (const auto &corner : profile.corners) {
            if (!corner.key || !keys.insert(corner.key).second)
                fail("HOST_RECORDS", "Opening source corners require unique stable identities");
            checkPoint(corner.point);
        }
    }
}
struct Draft {
    Surface surface;
    std::map<Id, Id> originalToTemporary, temporaryToOriginal;
    HostOpenings openings;
};
Draft build(const Surface &uncut, const std::map<Id, OpeningProfile> &profiles) {
    preflight(uncut, profiles);
    uncut.validate();
    Draft draft;
    // Temporary dense identities keep validation independent of exhausted live
    // allocator floors. No retired/live identity is consumed by triangulation.
    for (const auto &[id, point] : uncut.vertices) {
        const auto temporary = allocate(draft.surface.nextId);
        draft.surface.vertices[temporary] = point;
        draft.originalToTemporary[id] = temporary;
        draft.temporaryToOriginal[temporary] = id;
    }
    for (const auto &[id, face] : uncut.faces) {
        const auto temporary = allocate(draft.surface.nextId);
        auto loops = face.loops;
        for (auto &loop : loops)
            for (auto &vertex : loop)
                vertex = draft.originalToTemporary.at(vertex);
        draft.surface.faces[temporary] = {temporary, std::move(loops)};
        draft.originalToTemporary[id] = temporary;
        draft.temporaryToOriginal[temporary] = id;
    }
    for (const auto &wire : uncut.wires)
        draft.surface.wires.push_back(
            {draft.originalToTemporary.at(wire[0]), draft.originalToTemporary.at(wire[1])});
    for (const auto &[owner, profile] : profiles) {
        std::vector<Vec3> points;
        for (const auto &corner : profile.corners)
            points.push_back(corner.point);
        const auto entry = draft.originalToTemporary.at(profile.face);
        auto cut = cutHostedOpening(draft.surface, entry, points);
        if (!draft.temporaryToOriginal.contains(cut.exit))
            fail("OPENING_OBSTRUCTED", "An opening cannot exit through another opening's reveal");
        HostOpening opening{profile, draft.temporaryToOriginal.at(cut.exit), {}, {}};
        const auto &surface = cut.edit.surface;
        const auto &front = surface.faces.at(entry).loops.back();
        const auto normal = surface.normal(entry);
        const auto origin = surface.vertices.at(surface.faces.at(entry).loops.front().front());
        std::map<Id, Id> sourceKeys;
        for (const auto &corner : profile.corners) {
            const auto projected = corner.point - normal * dot(corner.point - origin, normal);
            Id nearest = 0;
            double distance = 2 * tolerance;
            for (auto vertex : front) {
                const auto candidate = length(surface.vertices.at(vertex) - projected);
                if (candidate < distance) {
                    distance = candidate;
                    nearest = vertex;
                }
            }
            if (!nearest || !sourceKeys.emplace(nearest, corner.key).second)
                fail("HOST_RECORDS", "Opening corner correspondence is ambiguous");
        }
        for (auto face : cut.jambs) {
            const auto &quad = surface.faces.at(face).loops.front();
            const auto a = sourceKeys.at(quad[0]), b = sourceKeys.at(quad[1]);
            opening.vertices[a] = {quad[0], quad[3]};
            opening.vertices[b] = {quad[1], quad[2]};
            opening.jambs[{std::min(a, b), std::max(a, b)}] = face;
        }
        if (opening.vertices.size() != profile.corners.size() ||
            opening.jambs.size() != profile.corners.size())
            fail("HOST_RECORDS",
                 "Opening outline did not produce a complete native correspondence");
        draft.openings[owner] = std::move(opening);
        draft.surface = std::move(cut.edit.surface);
    }
    return draft;
}
Surface remap(const Surface &surface, const std::map<Id, Id> &identities, Id next) {
    Surface result;
    result.nextId = next;
    std::set<Id> used;
    for (const auto &[temporary, id] : identities)
        if (!id || id >= next || !used.insert(id).second)
            fail("HOST_RECORDS", "Hosted geometry identities collide or exceed their allocator");
    for (const auto &[id, point] : surface.vertices)
        result.vertices.emplace(identities.at(id), point);
    for (const auto &[id, face] : surface.faces) {
        auto loops = face.loops;
        for (auto &loop : loops)
            for (auto &vertex : loop)
                vertex = identities.at(vertex);
        const auto target = identities.at(id);
        result.faces.emplace(target, Face{target, std::move(loops)});
    }
    for (const auto &wire : surface.wires)
        result.wires.push_back({identities.at(wire[0]), identities.at(wire[1])});
    return result;
}
RegeneratedHost regenerate(const Surface &uncut, const Body &current, const HostOpenings &previous,
                           const std::map<Id, OpeningProfile> &requested) {
    if (previous.size() > openingLimit || !uncut.nextId || uncut.nextId > current.surface.nextId)
        fail("HOST_RECORDS", "Invalid uncut host allocator or opening count");
    std::map<Id, OpeningProfile> oldProfiles;
    for (const auto &[owner, opening] : previous) {
        if (opening.vertices.size() > 256 || opening.jambs.size() > 256)
            fail("HOST_LIMIT", "Stored opening exceeds the correspondence limit");
        oldProfiles[owner] = opening.profile;
    }
    // Validate the requested bounds before performing old-state reconstruction.
    preflight(uncut, requested);
    const auto old = build(uncut, oldProfiles);
    auto identities = old.temporaryToOriginal;
    for (const auto &[owner, generated] : old.openings) {
        const auto &stored = previous.at(owner);
        if (stored.exit != generated.exit || stored.vertices.size() != generated.vertices.size() ||
            stored.jambs.size() != generated.jambs.size())
            fail("HOST_RECORDS", "Stored opening does not match its source outline");
        for (const auto &[key, pair] : generated.vertices) {
            if (!stored.vertices.contains(key))
                fail("HOST_RECORDS", "Stored opening is missing a source corner");
            identities[pair[0]] = stored.vertices.at(key)[0];
            identities[pair[1]] = stored.vertices.at(key)[1];
        }
        for (const auto &[key, face] : generated.jambs) {
            if (!stored.jambs.contains(key))
                fail("HOST_RECORDS", "Stored opening is missing a source edge");
            identities[face] = stored.jambs.at(key);
        }
    }
    if (remap(old.surface, identities, current.surface.nextId) != current.surface)
        fail("HOST_CHANGED",
             "Host geometry changed independently; detach openings before editing it");
    current.topology.validate(current.surface);
    validateEdgeAppearances(current);
    auto draft = build(uncut, requested);
    identities = draft.temporaryToOriginal;
    Id next = current.surface.nextId;
    for (auto &[owner, opening] : draft.openings) {
        const auto prior = previous.find(owner);
        for (auto &[key, pair] : opening.vertices) {
            const bool reuse = prior != previous.end() && prior->second.vertices.contains(key);
            const auto targets = reuse ? prior->second.vertices.at(key)
                                       : std::array<Id, 2>{allocate(next), allocate(next)};
            identities[pair[0]] = targets[0];
            identities[pair[1]] = targets[1];
            pair = targets;
        }
        for (auto &[key, face] : opening.jambs) {
            const bool reuse = prior != previous.end() && prior->second.jambs.contains(key);
            const auto target = reuse ? prior->second.jambs.at(key) : allocate(next);
            identities[face] = target;
            face = target;
        }
    }
    RegeneratedHost result{current, std::move(draft.openings), {}};
    result.body.surface = remap(draft.surface, identities, next);
    result.body.topology =
        Topology::rebuild(result.body.surface, current.topology, current.topology.nextId);
    std::erase_if(result.body.faceColors, [&](const auto &entry) {
        return !result.body.surface.faces.contains(entry.first);
    });
    std::erase_if(result.body.faceMaterials, [&](const auto &entry) {
        return !result.body.surface.faces.contains(entry.first);
    });
    std::erase_if(result.body.faceTextureMappings, [&](const auto &entry) {
        return !result.body.surface.faces.contains(entry.first);
    });
    std::erase_if(result.body.edgeAppearances, [&](const auto &entry) {
        return !result.body.topology.edges.contains(entry.first);
    });
    for (const auto &[owner, opening] : result.openings) {
        auto &descendants = result.faceDescendants[opening.profile.face];
        if (descendants.empty())
            descendants.push_back(opening.profile.face);
        for (const auto &[key, face] : opening.jambs) {
            descendants.push_back(face);
            if (current.surface.faces.contains(face))
                continue;
            setFaceTextureMappings(result.body, face,
                                   faceTextureMappings(current, opening.profile.face));
            const auto color = faceColor(current, opening.profile.face);
            const auto materials = faceMaterials(current, opening.profile.face);
            if (color != result.body.color)
                result.body.faceColors[face] = color;
            if (materials != result.body.materials)
                result.body.faceMaterials[face] = materials;
        }
    }
    size_t curveBudget = 1000000;
    for (auto it = result.body.curves.begin(); it != result.body.curves.end();)
        if (!bindCurve(it->second, result.body.surface, result.body.topology, curveBudget))
            it = result.body.curves.erase(it);
        else
            ++it;
    result.body.surface.validate();
    result.body.topology.validate(result.body.surface);
    validateCurves(result.body.curves, result.body.surface, result.body.topology);
    validateEdgeAppearances(result.body);
    return result;
}
} // namespace
RegeneratedHost regenerateHost(const Surface &uncut, const Body &current,
                               const HostOpenings &previous,
                               const std::map<Id, OpeningProfile> &requested) {
    try {
        return regenerate(uncut, current, previous, requested);
    } catch (const OpeningError &) {
        throw;
    } catch (const std::exception &error) {
        throw OpeningError("HOST_RECORDS", error.what());
    }
}
} // namespace sketchy
