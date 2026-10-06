#include "app/viewport.hpp"
#include "automation/section_commands.hpp"
namespace sketchy {
bool Viewport::sectionOccludes(Vec3 origin, Vec3 direction, double target) const {
    if (doc_.activeSections().empty() || doc_.style().mode == ModelStyleMode::Wireframe)
        return false;
    for (const auto &[body, cache] : bodyCaches_) {
        if (cache->alpha == 0 || !cache->sectionError.isEmpty())
            continue;
        for (const auto &[section, triangle] : cache->sectionCaps) {
            if (!cache->sections.at(section)->fill)
                continue;
            const auto e1 = triangle.b - triangle.a, e2 = triangle.c - triangle.a;
            const auto h = cross(direction, e2);
            const auto determinant = dot(e1, h);
            if (std::abs(determinant) <= length(e1) * length(e2) * 1e-12)
                continue;
            const auto relative = origin - triangle.a;
            const auto u = dot(relative, h) / determinant;
            const auto q = cross(relative, e1);
            const auto v = dot(direction, q) / determinant;
            const auto distance = dot(e2, q) / determinant;
            if (u >= 0 && v >= 0 && u + v <= 1 && distance > 0 &&
                distance < target - std::max(tolerance, target * 1e-8) &&
                !clipped(origin + direction * distance, body))
                return true;
        }
    }
    return false;
}

void Viewport::prepareSections(Id body, BodyCache &cache) {
    const auto previousError = cache.sectionError;
    const auto previousUnfilled = cache.sectionMesh.unfilledSections;
    cache.sectionMesh = {};
    cache.sectionCaps.clear();
    cache.sectionError.clear();
    if (cache.sectionCuts.empty())
        return;
    try {
        std::vector<Triangle> source;
        std::vector<size_t> indices;
        for (size_t i = 0; i < cache.worldTriangles.size(); ++i) {
            const auto &triangle = cache.worldTriangles[i];
            if (visible({body, SelectionKind::Face, triangle.face})) {
                source.push_back(triangle);
                indices.push_back(i);
            }
        }
        cache.sectionMesh = sectionMesh(source, cache.sectionCuts);
        for (auto &triangle : cache.sectionMesh.triangles) {
            if (triangle.source != noSectionSource)
                triangle.source = indices.at(triangle.source);
            else
                cache.sectionCaps.push_back(
                    {triangle.section,
                     {triangle.vertices[0].point, triangle.vertices[1].point,
                      triangle.vertices[2].point, 0}});
        }
        if (!cache.sectionMesh.unfilledSections.empty() &&
            cache.sectionMesh.unfilledSections != previousUnfilled)
            emit message("Section fill unavailable for an open or overlapping boundary");
    } catch (const std::exception &error) {
        cache.sectionMesh = {};
        cache.sectionCaps.clear();
        cache.sectionError = QString::fromUtf8(error.what());
        if (cache.sectionError != previousError)
            emit message("Section view unavailable: " + cache.sectionError);
    }
}

std::array<Viewport::Vertex, 3> Viewport::clippedVertices(const std::array<Vertex, 3> &source,
                                                          const SectionTriangle &triangle) {
    std::array<Vertex, 3> result;
    for (size_t i = 0; i < result.size(); ++i) {
        const auto &corner = triangle.vertices[i];
        auto &vertex = result[i];
        vertex = source[0]; // Face colors, physical side IDs and selection IDs stay constant.
        vertex.x = corner.point.x;
        vertex.y = corner.point.y;
        vertex.z = corner.point.z;
        auto interpolate = [&](float Vertex::*member) {
            return float(source[0].*member * corner.weights[0] +
                         source[1].*member * corner.weights[1] +
                         source[2].*member * corner.weights[2]);
        };
        vertex.u = interpolate(&Vertex::u);
        vertex.v = interpolate(&Vertex::v);
        vertex.bu = interpolate(&Vertex::bu);
        vertex.bv = interpolate(&Vertex::bv);
        vertex.light = interpolate(&Vertex::light);
        vertex.backLight = interpolate(&Vertex::backLight);
    }
    return result;
}

std::array<Viewport::Vertex, 3>
Viewport::sectionCapVertices(Id body, const SectionTriangle &triangle, float alpha) const {
    const auto &color = doc_.sections().at(triangle.section)->color;
    if (doc_.style().mode == ModelStyleMode::XRay)
        alpha *= float(doc_.style().xrayOpacity);
    std::array<Vertex, 3> result;
    for (size_t i = 0; i < result.size(); ++i) {
        const auto point = triangle.vertices[i].point;
        auto &vertex = result[i];
        vertex = {point.x, point.y, point.z, color[0], color[1], color[2], alpha};
        const auto crossNormal = cross(triangle.vertices[1].point - triangle.vertices[0].point,
                                       triangle.vertices[2].point - triangle.vertices[0].point);
        const auto normal = length(crossNormal) > 0 ? crossNormal * (1 / length(crossNormal)) : Vec3{0,0,1};
        vertex.light = doc_.solar().enabled ? solarLight(normal) : .9f;
        vertex.backLight = doc_.solar().enabled ? solarLight(normal * -1) : .9f;
        vertex.dim =
            !selection_.inActiveHierarchy(doc_, body) || selection_.locked(doc_, body) ? .35f : 1.f;
    }
    return result;
}

QJsonObject Viewport::editSections(const QJsonArray &commands) {
    for (const auto &command : commands)
        if (!isSectionCommand(command.toObject()["command"].toString()))
            throw std::runtime_error("Expected section plane edit");
    cancel();
    auto result = commitCommands(commands, false);
    refresh();
    emit changed();
    return result;
}
} // namespace sketchy
