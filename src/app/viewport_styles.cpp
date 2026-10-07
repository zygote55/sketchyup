#include "app/viewport.hpp"
#include "core/appearance.hpp"
#include "io/model_style_io.hpp"
#include <algorithm>
namespace sketchy {
void Viewport::applyModelStyle(const ModelStyle &style) {
    style.validate();
    if (style == doc_.style())
        return;
    cancel();
    commitCommands({QJsonObject{{"command", "document.style"}, {"style", encodeModelStyle(style)}}},
                   false);
    refresh();
    emit changed();
}
void Viewport::syncModelStyle() {
    const auto &style = doc_.style();
    if (displayedStyle_ && *displayedStyle_ == style)
        return;
    displayedStyle_ = style;
    const auto &rgb = style.background;
    auto linear = [](float c) {
        return c <= .04045f ? c / 12.92f : std::pow((c + .055f) / 1.055f, 2.4f);
    };
    const auto luminance =
        .2126f * linear(rgb[0]) + .7152f * linear(rgb[1]) + .0722f * linear(rgb[2]);
    colors_ = themeColors(luminance < .179f);
    colors_.canvas = QColor::fromRgbF(rgb[0], rgb[1], rgb[2]);
    colors_.ink = colors_.muted = luminance < .179f ? Qt::white : Qt::black;
    ++presentationRevision_;
    gridDirty_ = cacheDirty_ = pickDirty_ = overlayDirty_ = profilesDirty_ = true;
    groundMatrix_ = {};
}
void Viewport::drawStyleGround(const QMatrix4x4 &transform) {
    const auto &style = doc_.style();
    if (!style.groundVisible)
        return;
    if (groundMatrix_ != transform || groundGpu_.origin != renderOrigin_ || !groundGpu_.count) {
        // A visual reference plane surrounds the current camera target. It writes
        // no depth and is not geometry or a picking occluder.
        const double extent = std::max(100.0, double(distance_) * 8);
        const double x = target_.x, y = target_.y, z = style.groundHeight;
        const Vec3 a{x - extent, y - extent, z}, b{x + extent, y - extent, z},
            c{x + extent, y + extent, z}, d{x - extent, y + extent, z};
        std::vector<Vertex> vertices;
        for (const auto p : {a, b, c, a, c, d}) {
            Vertex v{p.x, p.y, p.z, style.ground[0], style.ground[1], style.ground[2]};
            if (doc_.solar().enabled) {
                v.light = solarLight({0,0,1});
                v.backLight = solarLight({0,0,-1});
            }
            vertices.push_back(v);
        }
        upload(groundGpu_, vertices);
        groundMatrix_ = transform;
    }
    gl_->glDepthMask(GL_FALSE);
    shader_->setUniformValue("groundPass", 1);
    draw(groundGpu_, GL_TRIANGLES);
    shader_->setUniformValue("groundPass", 0);
    gl_->glDepthMask(GL_TRUE);
}
void Viewport::drawStyleProfiles(const QMatrix4x4 &transform) {
    const auto &style = doc_.style();
    if (!style.profiles) {
        stats_.profileEdges = 0;
        return;
    }
    if (profilesDirty_ || profileMatrix_ != transform || profilesGpu_.origin != renderOrigin_ ||
        !profilesGpu_.buffer.isCreated()) {
        std::vector<Vertex> vertices;
        stats_.profileEdges = 0;
        const auto camera = renderCamera();
        auto clipPoint = [&](Vec3 p) {
            p = p - renderOrigin_;
            return transform * QVector4D(float(p.x), float(p.y), float(p.z), 1);
        };
        auto append = [&](Vec3 a, Vec3 b, std::array<float, 3> color,
                          const std::vector<SectionCut> &cuts) {
            auto ca = clipPoint(a), cb = clipPoint(b);
            double first = 0, last = 1;
            auto clip = [&](double fa, double fb) {
                if (fa < 0 && fb < 0)
                    return false;
                if (fa < 0)
                    first = std::max(first, -fa / (fb - fa));
                if (fb < 0)
                    last = std::min(last, -fa / (fb - fa));
                return first <= last;
            };
            for (int axis = 0; axis < 3; ++axis)
                if (!clip(ca.w() + ca[axis], cb.w() + cb[axis]) ||
                    !clip(ca.w() - ca[axis], cb.w() - cb[axis]))
                    return;
            for (const auto &cut : cuts)
                if (!clip(cut.plane.distance(a), cut.plane.distance(b)))
                    return;
            if (clipPlane_) {
                const auto &p = *clipPlane_;
                if (!clip(p[0] * a.x + p[1] * a.y + p[2] * a.z + p[3],
                          p[0] * b.x + p[1] * b.y + p[2] * b.z + p[3]))
                    return;
            }
            const auto delta = b - a;
            b = a + delta * last;
            a = a + delta * first;
            ca = clipPoint(a);
            cb = clipPoint(b);
            if (ca.w() <= 0 || cb.w() <= 0)
                return;
            const double dx = (cb.x() / cb.w() - ca.x() / ca.w()) * width() / 2;
            const double dy = (cb.y() / cb.w() - ca.y() / ca.w()) * height() / 2;
            const double length = std::hypot(dx, dy);
            if (length < 1e-6)
                return;
            const float x = float(-dy / length * style.profileWidth / width());
            const float y = float(dx / length * style.profileWidth / height());
            auto vertex = [&](Vec3 p, float sign) {
                Vertex v{p.x, p.y, p.z, color[0], color[1], color[2]};
                v.u = sign * x;
                v.v = sign * y;
                return v;
            };
            for (const auto &v : {vertex(a, -1), vertex(a, 1), vertex(b, 1), vertex(a, -1),
                                  vertex(b, 1), vertex(b, -1)})
                vertices.push_back(v);
            ++stats_.profileEdges;
        };
        for (const auto &[id, cache] : bodyCaches_) {
            if (cache->alpha == 0 || !cache->record || !cache->sectionError.isEmpty())
                continue;
            const auto &body = *cache->record;
            std::map<Id, bool> facing;
            for (const auto &triangle : cache->worldTriangles) {
                if (facing.contains(triangle.face) ||
                    !visible({id, SelectionKind::Face, triangle.face}))
                    continue;
                const auto normal = cross(triangle.b - triangle.a, triangle.c - triangle.a);
                if (length(normal) == 0)
                    continue;
                const auto direction = camera.orthographic ? camera.position - camera.target
                                                           : camera.position - triangle.a;
                const bool front = dot(normal, direction) >= 0;
                const bool physicalBack = (!front) != (cache->world.determinant() < 0);
                if (surfaceAppearance(doc_.materials(), body, triangle.face, physicalBack).opacity >
                    0)
                    facing.emplace(triangle.face, front);
            }
            for (const auto &edge : cache->worldEdges) {
                const SelectedEntity entity{id, SelectionKind::Edge, edge.id};
                // Softened edges can form silhouettes; explicit hiding still applies.
                if (!selection_.showingHidden() && selection_.hidden(doc_, entity, false))
                    continue;
                const auto found = cache->adjacency.edgeFaces.find(edge.id);
                if (found == cache->adjacency.edgeFaces.end())
                    continue;
                size_t count = 0;
                bool front = false, back = false;
                for (const auto &incidence : found->second) {
                    const auto face = facing.find(incidence.face);
                    if (face == facing.end())
                        continue;
                    ++count;
                    front |= face->second;
                    back |= !face->second;
                }
                if (count == 1 || (front && back))
                    append(edge.a, edge.b,
                           selection_.inActiveHierarchy(doc_, id)
                               ? style.edge
                               : std::array<float, 3>{.56f, .58f, .60f}, cache->sectionCuts);
            }
        }
        upload(profilesGpu_, vertices);
        profileMatrix_ = transform;
        profilesDirty_ = false;
    }
    // Expand each edge into a screen-space quad; width does not depend on the
    // driver's supported GL line widths or device pixel ratio.
    shader_->setUniformValue("screenStroke", 1);
    draw(profilesGpu_, GL_TRIANGLES);
    shader_->setUniformValue("screenStroke", 0);
}
} // namespace sketchy
