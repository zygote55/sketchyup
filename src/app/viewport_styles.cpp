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
namespace {

// The stroke the profiles draw with (UX_DESIGN §18.3). While the edge stroke is on it replaces
// Profile width, so the two never disagree. With it off, the profiles are drawn as they were
// before.
struct ProfileStroke {
    double width;
    double opacity;
    EdgeDash dash;
    Id texture;   // the edge texture asset, zero for none
    bool missing; // the texture is named, and its asset has no bytes (ADR 0174 section 5)
};

ProfileStroke profileStroke(const ModelStyle &style, const AssetRecords &assets) {
    if (!style.edgeStroke)
        return {style.profileWidth, 1, EdgeDash::Solid, 0, false};
    const auto &stroke = *style.edgeStroke;
    const auto found = assets.find(stroke.texture);
    const bool missing =
        stroke.texture && (found == assets.end() || !found->second || !found->second->payload);
    return {stroke.width, stroke.opacity, stroke.dash, stroke.texture, missing};
}

// How a stroke fragment is shaded: the colour and opacity of the edge, the edge texture tiled
// along the edge, or a hatch in the warning colour where the texture cannot be drawn.
enum class EdgeMode { Plain, Texture, Hatch };

} // namespace

void Viewport::drawStyleProfiles(const QMatrix4x4 &transform) {
    const auto &style = doc_.style();
    if (!style.profiles) {
        stats_.profileEdges = stats_.profileSegments = 0;
        return;
    }
    const auto stroke = profileStroke(style, doc_.assets());
    if (profilesDirty_ || profileMatrix_ != transform || profilesGpu_.origin != renderOrigin_ ||
        !profilesGpu_.buffer.isCreated()) {
        std::vector<Vertex> vertices;
        stats_.profileEdges = stats_.profileSegments = 0;
        const auto camera = renderCamera();
        auto clipPoint = [&](Vec3 p) {
            p = p - renderOrigin_;
            return transform * QVector4D(float(p.x), float(p.y), float(p.z), 1);
        };
        // One stroke quad over a visible span p to q, in front of the camera. `from` and `to` are
        // the span's distances along the edge in logical pixels; the texture and the hatch read
        // them.
        auto quad = [&](Vec3 p, Vec3 q, double from, double to, std::array<float, 3> color) {
            const auto cp = clipPoint(p), cq = clipPoint(q);
            if (cp.w() <= 0 || cq.w() <= 0)
                return;
            const double dx = (cq.x() / cq.w() - cp.x() / cp.w()) * renderWidth() / 2;
            const double dy = (cq.y() / cq.w() - cp.y() / cp.w()) * renderHeight() / 2;
            const double length = std::hypot(dx, dy);
            if (length < 1e-6)
                return;
            const float x = float(-dy / length * stroke.width / renderWidth());
            const float y = float(dx / length * stroke.width / renderHeight());
            auto vertex = [&](Vec3 point, double along, float sign) {
                Vertex v{point.x, point.y, point.z, color[0], color[1], color[2]};
                v.a = v.ba = float(stroke.opacity);
                // The edge texture is bound as the back image: the shader samples it on both sides.
                v.backImage = stroke.texture;
                v.u = sign * x;
                v.v = sign * y;
                // Logical pixels along the edge and across the stroke, read without perspective.
                v.bu = float(along);
                v.bv = float((sign + 1) / 2 * stroke.width);
                return v;
            };
            for (const auto &v : {vertex(p, from, -1), vertex(p, from, 1), vertex(q, to, 1),
                                  vertex(p, from, -1), vertex(q, to, 1), vertex(q, to, -1)})
                vertices.push_back(v);
            ++stats_.profileSegments;
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
            const double dx = (cb.x() / cb.w() - ca.x() / ca.w()) * renderWidth() / 2;
            const double dy = (cb.y() / cb.w() - ca.y() / ca.w()) * renderHeight() / 2;
            const double length = std::hypot(dx, dy);
            if (length < 1e-6)
                return;
            ++stats_.profileEdges;
            if (stroke.dash == EdgeDash::Solid) {
                quad(a, b, 0, length, color);
                return;
            }
            // A dash run is a span of the visible edge. Its world ends follow from its screen
            // distance: screen position is linear in the screen fraction sigma, and sigma relates
            // to the world parameter t by t = sigma w0 / (w1 - sigma (w1 - w0)) (w0, w1: clip w).
            const double w0 = ca.w(), w1 = cb.w();
            auto pointAt = [&](double distance) {
                const double sigma = distance / length;
                return a + (b - a) * (sigma * w0 / (w1 - sigma * (w1 - w0)));
            };
            for (const auto &[from, to] : edgeDashRuns(length, stroke.dash))
                quad(pointAt(from), pointAt(to), from, to, color);
        };
        for (const auto &[id, cache] : bodyCaches_) {
            if (cache->alpha == 0 || !cache->record || !cache->sectionError.isEmpty())
                continue;
            const auto &body = *cache->record;
            if (body.referenceImage)
                continue;
            std::map<Id, bool> facing;
            for (size_t i = 0; i < cache->geometry->localTriangles.size(); ++i) {
                const auto face = cache->geometry->localTriangles[i].face;
                if (facing.contains(face) || !visible({id, SelectionKind::Face, face}))
                    continue;
                const auto triangle = worldTriangle(*cache, i);
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
            for (size_t i = 0; i < cache->geometry->localEdges.size(); ++i) {
                const auto edge = worldEdge(*cache, i);
                const SelectedEntity entity{id, SelectionKind::Edge, edge.id};
                // Softened edges can form silhouettes; explicit hiding still applies.
                if (!selection_.showingHidden() && selection_.hidden(doc_, entity, false))
                    continue;
                const auto found = cache->geometry->adjacency.edgeFaces.find(edge.id);
                if (found == cache->geometry->adjacency.edgeFaces.end())
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
    // The texture mode follows the frame: the texture may still be loading.
    EdgeMode mode = EdgeMode::Plain;
    if (stroke.texture) {
        if (stroke.missing) {
            mode = EdgeMode::Hatch;
        } else if (textureImages_.contains(stroke.texture)) {
            mode = EdgeMode::Texture;
        } else {
            // Nothing is drawn in place of a texture that is still decoding or uploading. Only
            // bytes the decoder refused are hatched.
            const auto snapshot = textureCache_.snapshot();
            const auto found = doc_.assets().find(stroke.texture);
            const TextureCache::Entry *entry = snapshot && found != doc_.assets().end()
                                                   ? TextureCache::find(*snapshot, found->second)
                                                   : nullptr;
            if (!entry || entry->image)
                return;
            mode = EdgeMode::Hatch;
        }
    }
    // Opacity blends the stroke over the faces; the framebuffer keeps its own alpha.
    const bool translucent = mode == EdgeMode::Texture || stroke.opacity < 1;
    if (translucent) {
        gl_->glEnable(GL_BLEND);
        gl_->glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                                 GL_ONE_MINUS_SRC_ALPHA);
        gl_->glDepthMask(GL_FALSE);
    }
    const auto warning = colors_.warning;
    shader_->setUniformValue("screenStroke", 1);
    shader_->setUniformValue("edgeMode", int(mode));
    shader_->setUniformValue(
        "edgeHatchColor",
        QVector3D(float(warning.redF()), float(warning.greenF()), float(warning.blueF())));
    draw(profilesGpu_, GL_TRIANGLES);
    shader_->setUniformValue("edgeMode", 0);
    shader_->setUniformValue("screenStroke", 0);
    if (translucent) {
        gl_->glDisable(GL_BLEND);
        gl_->glDepthMask(GL_TRUE);
    }
}

bool Viewport::edgeTextureMissing() const {
    const auto &style = doc_.style();
    return style.edgeStroke && style.edgeStroke->texture &&
           profileStroke(style, doc_.assets()).missing;
}

} // namespace sketchy
