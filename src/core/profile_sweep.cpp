#include "core/profile_sweep.hpp"
#include "core/appearance.hpp"
#include "core/groups.hpp"
namespace sketchy {
ProfileSweepResult sweepFace(Document &doc, Id source, Id face, const std::vector<Vec3> &path,
                             bool closed, bool worldSpace) {
    const auto old = doc.bodies().at(source);
    if (persistentlyLocked(doc, source))
        throw SweepError("SWEEP_LOCKED", "Unlock the profile context before sweeping");
    Surface input;
    input.faces.emplace(face, old->surface.faces.at(face));
    const auto frame = worldSpace ? doc.worldTransform(source) : Transform{};
    for (const auto &loop : input.faces.at(face).loops)
        for (auto vertex : loop)
            input.vertices[vertex] = frame.point(old->surface.vertices.at(vertex));
    auto result = sweepProfile(input, face, path, closed);
    const auto inverse = frame.inverse();
    for (auto &[id, point] : result.surface.vertices)
        point = inverse.point(point);
    auto body = std::make_shared<Body>();
    body->id = doc.nextId();
    body->name = "Sweep";
    body->surface = std::move(result.surface);
    body->parent = old->kind == BodyKind::Group ? source : old->parent;
    body->transform = old->kind == BodyKind::Group ? Transform{} : old->transform;
    body->color = faceColor(*old, face);
    body->materials = faceMaterials(*old, face);
    body->tag = old->tag;
    auto changes = doc.apply({"Sweep profile", {{body->id, nullptr, body}}}, doc.revision());
    return {body->id, std::move(result.caps), std::move(result.sides), std::move(result.segments),
            std::move(changes)};
}
} // namespace sketchy
