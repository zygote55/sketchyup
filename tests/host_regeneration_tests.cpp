#include "core/host_regeneration.hpp"
#include "core/model.hpp"
#include "geometry/solid.hpp"
#include <algorithm>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(const char *code, F operation) {
    try {
        operation();
    } catch (const OpeningError &error) {
        check(error.code() == code, error.what());
        return;
    }
    throw std::runtime_error(std::string("Expected ") + code);
}
Body wall() {
    Body body;
    body.id = 1;
    body.name = "Host wall";
    const auto face = body.surface.addFace({{{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}}});
    body.surface.extrude(face, 1);
    body.topology = Topology::rebuild(body.surface, {});
    return body;
}
Id top(const Body &body) {
    for (const auto &[id, face] : body.surface.faces)
        if (body.surface.normal(id).z > .99)
            return id;
    throw std::runtime_error("Missing wall top");
}
OpeningProfile profile(Id face, double x, double y, double width = 2) {
    return {face,
            {{11, {x, y, 1}},
             {12, {x + width, y, 1}},
             {13, {x + width, y + 2, 1}},
             {14, {x, y + 2, 1}}}};
}
void volume(const Body &body, double expected) {
    const auto result = analyzeSolidShells(body.surface, body.topology);
    check(result.report.status == "validated_shells" && result.report.volume &&
              std::abs(*result.report.volume - expected) < 1e-7,
          "Regenerated host has independently expected closed material volume");
}
Id edge(const Body &body, Id a, Id b) {
    for (const auto &[id, record] : body.topology.edges)
        if (record.a == std::min(a, b) && record.b == std::max(a, b))
            return id;
    throw std::runtime_error("Expected native edge");
}
void lifecycle() {
    auto original = wall();
    const auto entry = top(original);
    const auto uncut = original.surface;
    const auto originalEdge = original.topology.edges.begin()->first;
    original.faceColors[entry] = {.2f, .3f, .4f};
    original.faceMaterials[entry] = {10, 20};
    original.edgeAppearances[originalEdge] = {true, false, true};
    const std::map<Id, OpeningProfile> initial{{100, profile(entry, 1, 1)},
                                               {200, profile(entry, 6, 6)}};
    auto created = regenerateHost(uncut, original, {}, initial);
    volume(created.body, 92);
    for (const auto &[owner, opening] : created.openings)
        for (const auto &[key, jamb] : opening.jambs) {
            check(created.body.faceColors.at(jamb) == original.faceColors.at(entry) &&
                      created.body.faceMaterials.at(jamb) == MaterialSides{10, 20},
                  "New reveals inherit current physical entry appearance");
        }
    const auto &first = created.openings.at(100);
    const auto paintedJamb = first.jambs.begin()->second;
    const auto styledEdge = edge(created.body, first.vertices.at(11)[0], first.vertices.at(12)[0]);
    created.body.faceColors[paintedJamb] = {.9f, .1f, .2f};
    created.body.faceMaterials[paintedJamb] = {30, 40};
    created.body.edgeAppearances[styledEdge] = {false, true, true};
    created.body.edgeAppearances.erase(originalEdge);
    created.body.properties["note"] = std::string("Keep authored state");
    created.body.transform = Transform::translation({10, 20, 30}) * Transform::scaling({-2, 3, .5});
    const auto before = created.body;
    auto movedProfiles = initial;
    movedProfiles[100] = profile(entry, 2, 1);
    const auto moved = regenerateHost(uncut, created.body, created.openings, movedProfiles);
    check(created.body == before && original.surface == uncut,
          "Regeneration never mutates current or uncut source");
    volume(moved.body, 92);
    check(moved.body.surface.nextId == created.body.surface.nextId &&
              moved.body.topology.nextId == created.body.topology.nextId &&
              moved.openings.at(100).vertices == first.vertices &&
              moved.openings.at(100).jambs == first.jambs,
          "Moving an outline retains every native corner/reveal/edge identity without allocation");
    check(moved.openings.at(200) == created.openings.at(200),
          "Unrelated opening records stay exact");
    for (const auto &[key, jamb] : created.openings.at(200).jambs)
        check(moved.body.surface.faces.at(jamb) == created.body.surface.faces.at(jamb),
              "Unrelated reveal records stay exact");
    check(moved.body.faceColors.at(paintedJamb) == std::array<float, 3>{.9f, .1f, .2f} &&
              moved.body.faceMaterials.at(paintedJamb) == MaterialSides{30, 40} &&
              moved.body.edgeAppearances.at(styledEdge) == EdgeAppearance{false, true, true} &&
              !moved.body.edgeAppearances.contains(originalEdge),
          "Authored reveal styles survive movement and cleared baseline edge flags stay cleared");
    check(moved.body.transform == created.body.transform &&
              moved.body.properties == created.body.properties,
          "Host placement and nongeometry state remain unchanged");
    const auto repeated = regenerateHost(uncut, moved.body, moved.openings, movedProfiles);
    check(repeated.body == moved.body && repeated.openings == moved.openings,
          "Repeated regeneration is exactly idempotent");
    auto resizedProfiles = movedProfiles;
    resizedProfiles[100] = profile(entry, 2, 1, 3);
    const auto resized = regenerateHost(uncut, moved.body, moved.openings, resizedProfiles);
    volume(resized.body, 90);
    check(resized.openings.at(100).vertices == first.vertices &&
              resized.body.surface.nextId == moved.body.surface.nextId,
          "Shared profile resizing retains corner identity and allocator floors");
    auto reversedProfiles = resizedProfiles;
    std::reverse(reversedProfiles[100].corners.begin(), reversedProfiles[100].corners.end());
    const auto reversed = regenerateHost(uncut, resized.body, resized.openings, reversedProfiles);
    check(reversed.body == resized.body,
          "Reversed component outline winding preserves native host geometry");
    const auto deleted =
        regenerateHost(uncut, resized.body, resized.openings, {{200, initial.at(200)}});
    volume(deleted.body, 96);
    check(!deleted.body.faceColors.contains(paintedJamb) &&
              !deleted.body.faceMaterials.contains(paintedJamb) &&
              !deleted.body.edgeAppearances.contains(styledEdge) &&
              deleted.openings.at(200) == created.openings.at(200),
          "Deleting one cut removes only its generated styles and retains its peer");
    const auto restored = regenerateHost(uncut, deleted.body, deleted.openings, {});
    auto expected = uncut;
    expected.nextId = restored.body.surface.nextId;
    check(restored.body.surface == expected && restored.body.faceColors.size() == 1 &&
              restored.body.faceMaterials.size() == 1 && restored.body.edgeAppearances.empty(),
          "Deleting final cut restores uncut geometry without stale paint/edge metadata");
    const auto fresh = regenerateHost(uncut, restored.body, {}, {{100, initial.at(100)}});
    for (const auto &[key, pair] : fresh.openings.at(100).vertices)
        check(pair[0] >= restored.body.surface.nextId && pair[1] >= restored.body.surface.nextId,
              "Recreating a removed opening never reuses retired vertex IDs");
}
void topologyChangesAndFloors() {
    const auto original = wall();
    const auto entry = top(original);
    const auto initial = profile(entry, 1, 1);
    auto created = regenerateHost(original.surface, original, {}, {{100, initial}});
    const auto old = created.openings.at(100);
    const auto retiredJamb = old.jambs.at({11, 12});
    const auto retiredEdge = edge(created.body, old.vertices.at(11)[0], old.vertices.at(12)[0]);
    created.body.edgeAppearances[retiredEdge] = {true, true, true};
    auto split = initial;
    split.corners.insert(split.corners.begin() + 1, {15, {2, 1, 1}});
    const auto result =
        regenerateHost(original.surface, created.body, created.openings, {{100, split}});
    volume(result.body, 96);
    check(result.openings.at(100).vertices.at(11) == old.vertices.at(11) &&
              result.openings.at(100).jambs.at({12, 13}) == old.jambs.at({12, 13}) &&
              !result.body.surface.faces.contains(retiredJamb) &&
              !result.body.topology.edges.contains(retiredEdge),
          "Changed outline topology retains matching features and retires replaced edges/reveals");
    check(result.body.edgeAppearances.empty(),
          "Retired edge flags cannot resurrect onto fresh outline edges");
    for (const auto &[key, pair] : result.openings.at(100).vertices)
        if (key == 15)
            check(pair[0] >= created.body.surface.nextId && pair[1] >= created.body.surface.nextId,
                  "Added source corners use fresh native identities");

    auto exhausted = created.body;
    exhausted.surface.nextId = UINT64_MAX;
    exhausted.topology.nextId = UINT64_MAX;
    const auto moved = regenerateHost(original.surface, exhausted, created.openings,
                                      {{100, profile(entry, 2, 1)}});
    check(moved.body.surface.nextId == UINT64_MAX && moved.body.topology.nextId == UINT64_MAX,
          "Movement and old-state validation need no free live identity space");
    const auto restored = regenerateHost(original.surface, exhausted, created.openings, {});
    volume(restored.body, 100);
    rejects("HOST_LIMIT", [&] {
        regenerateHost(original.surface, exhausted, created.openings,
                       {{100, initial}, {200, profile(entry, 6, 6)}});
    });
}
void failuresAndPreview() {
    const auto original = wall();
    const auto entry = top(original);
    const std::map<Id, OpeningProfile> profiles{{100, profile(entry, 1, 1)}};
    const auto created = regenerateHost(original.surface, original, {}, profiles);
    auto changed = created.body;
    changed.surface.vertices.begin()->second.x += .1;
    rejects("HOST_CHANGED",
            [&] { regenerateHost(original.surface, changed, created.openings, profiles); });
    auto corrupt = created.openings;
    corrupt.at(100).vertices.at(11)[0] = original.surface.vertices.begin()->first;
    rejects("HOST_RECORDS",
            [&] { regenerateHost(original.surface, created.body, corrupt, profiles); });
    auto bad = profiles;
    bad.at(100).corners[1].key = 11;
    rejects("HOST_RECORDS",
            [&] { regenerateHost(original.surface, created.body, created.openings, bad); });
    bad = profiles;
    for (Id i = 1; i < 17; ++i)
        bad[i] = profiles.at(100);
    rejects("HOST_LIMIT",
            [&] { regenerateHost(original.surface, created.body, created.openings, bad); });
    rejects("OPENING_PROFILE", [&] {
        regenerateHost(original.surface, created.body, created.openings,
                       {{100, profile(entry, 1, 1)}, {200, profile(entry, 2, 2)}});
    });

    Document doc;
    doc.apply({"Host fixture", {{1, nullptr, std::make_shared<Body>(original)}}}, doc.revision());
    const auto before = doc.bodies().at(1);
    const auto proposal = doc.prepareEdit([&](Document &candidate) {
        const auto generated =
            regenerateHost(original.surface, *candidate.bodies().at(1), {}, profiles);
        candidate.apply({"Cut component opening",
                         {{1,
                           candidate.bodies().at(1),
                           std::make_shared<Body>(generated.body),
                           generated.faceDescendants,
                           {},
                           {},
                           true}}},
                        candidate.revision());
    });
    check(doc.bodies().at(1) == before, "Prepared opening geometry does not publish itself");
    doc.applyPrepared(proposal);
    volume(*doc.bodies().at(1), 96);
    const auto after = doc.bodies().at(1);
    doc.undo();
    check(doc.bodies().at(1)->surface.vertices == before->surface.vertices &&
              doc.bodies().at(1)->surface.faces == before->surface.faces,
          "One Undo restores original native host geometry");
    doc.redo();
    check(*doc.bodies().at(1) == *after, "Redo restores exact generated host and appearance");
}
} // namespace
int main() {
    try {
        lifecycle();
        topologyChangesAndFloors();
        failuresAndPreview();
        std::cout << "Host regeneration, stable opening identities, appearance preservation, "
                     "deletion, bounded rejection and prepared Undo passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
