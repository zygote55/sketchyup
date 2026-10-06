#include "core/component_glue.hpp"
#include "core/component_placement.hpp"
namespace sketchy {
ResolvedComponentGlue resolveComponentGlue(const ComponentDefinition &definition) {
    try {
        if (!definition.glue)
            throw PlacementError("MISSING_GLUE", "Component has no configured glue face");
        const auto &glue = *definition.glue;
        if (!definition.root || !glue.member || glue.member == definition.root || !glue.face ||
            !definition.members.contains(glue.member) || !definition.members.at(glue.member) ||
            definition.references.contains(glue.member))
            throw PlacementError("INVALID_GLUE_FACE",
                                 "Glue face must belong to a canonical member");
        const auto &surface = definition.members.at(glue.member)->surface;
        if (!surface.faces.contains(glue.face))
            throw PlacementError("INVALID_GLUE_FACE", "Component glue face no longer exists");
        const auto &face = surface.faces.at(glue.face);
        if (face.id != glue.face)
            throw PlacementError("INVALID_GLUE_FACE", "Component glue face identity mismatch");
        if (face.loops.empty() || face.loops.size() > 64)
            throw PlacementError("PLACEMENT_LIMIT", "Glue face exceeds the boundary loop limit");
        size_t corners = 0;
        for (const auto &loop : face.loops) {
            if (loop.size() < 3 || loop.size() > 4096 - corners)
                throw PlacementError("PLACEMENT_LIMIT", "Glue face accepts at most 4096 corners");
            corners += loop.size();
        }
        if (glue.cutsOpening && face.loops.front().size() > 256)
            throw PlacementError("PLACEMENT_LIMIT",
                                 "An opening outline accepts at most 256 corners");
        (void)surface.triangulate(glue.face);
        checkPoint(glue.anchor);
        const auto normal = surface.normal(glue.face);
        const auto origin = surface.vertices.at(face.loops.front().front());
        const auto local = DrawingPlane::make(origin, normal, glue.tangent);
        const auto distance = local.coordinates(glue.anchor).z;
        if (std::abs(distance) > tolerance)
            throw PlacementError("INVALID_GLUE_FRAME",
                                 "Glue anchor must lie on its reference plane");
        Transform transform;
        auto member = glue.member;
        size_t depth = 0;
        while (member != definition.root) {
            if (!member || !definition.members.contains(member) || !definition.members.at(member) ||
                definition.references.contains(member) || ++depth > 128)
                throw PlacementError("INVALID_GLUE_FACE",
                                     "Glue face has an invalid canonical path");
            const auto &body = *definition.members.at(member);
            transform = body.transform * transform;
            member = body.parent;
        }
        const auto inverse = transform.inverse();
        const Vec3 physicalNormal{
            inverse.m[0] * normal.x + inverse.m[1] * normal.y + inverse.m[2] * normal.z,
            inverse.m[4] * normal.x + inverse.m[5] * normal.y + inverse.m[6] * normal.z,
            inverse.m[8] * normal.x + inverse.m[9] * normal.y + inverse.m[10] * normal.z};
        ResolvedComponentGlue result{
            DrawingPlane::make(transform.point(glue.anchor - normal * distance), physicalNormal,
                               transform.vector(local.xAxis)),
            {}};
        if (glue.cutsOpening)
            for (auto id : face.loops.front()) {
                const auto point = transform.point(surface.vertices.at(id));
                checkPoint(point);
                result.profile.push_back(point);
            }
        return result;
    } catch (const PlacementError &) {
        throw;
    } catch (const std::exception &error) {
        throw PlacementError("INVALID_GLUE_FRAME", error.what());
    }
}
} // namespace sketchy
