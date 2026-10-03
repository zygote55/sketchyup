#include "app/viewport.hpp"
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QPainter>
#include <QRegularExpression>
#include <QWheelEvent>
#include <algorithm>
#include <limits>
#include <numbers>
namespace sketchy {
namespace {
QVector3D qv(Vec3 p) { return {float(p.x), float(p.y), float(p.z)}; }
Vec3 vec(QVector3D p) { return {p.x(), p.y(), p.z()}; }
constexpr float degreesToRadians = std::numbers::pi_v<float> / 180;
} // namespace
Viewport::Viewport(Document &doc, QWidget *parent) : QOpenGLWidget(parent), doc_(doc) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(160, 160);
    setAccessibleName("3D model viewport");
}
Viewport::~Viewport() { cleanupGL(); }
void Viewport::cleanupGL() {
    disconnect(contextCleanup_);
    if (context()) {
        makeCurrent();
        for (auto *batch : {&gridGpu_, &transparentGpu_, &benchmarkGpu_}) {
            batch->buffer.destroy();
            batch->count = 0;
        }
        for (auto &[id, cache] : bodyCaches_)
            for (auto *batch : {&cache->opaqueGpu, &cache->linesGpu}) {
                batch->buffer.destroy();
                batch->count = 0;
            }
        vao_.destroy();
        shader_.reset();
        doneCurrent();
    }
    ready_ = false;
    cacheDirty_ = true;
    transparentDirty_ = true;
    benchmarkDirty_ = true;
    gridDirty_ = true;
}
void Viewport::initializeGL() {
    ready_ = false;
    if (!initializeOpenGLFunctions()) {
        emit message("OpenGL 3.3 is required");
        return;
    }
    contextCleanup_ = connect(context(), &QOpenGLContext::aboutToBeDestroyed, this,
                              &Viewport::cleanupGL, Qt::DirectConnection);
    graphics_ = QString("%1 / %2").arg(reinterpret_cast<const char *>(glGetString(GL_RENDERER)),
                                       reinterpret_cast<const char *>(glGetString(GL_VERSION)));
    const char *vertex = R"(#version 330 core
layout(location=0) in vec3 position;
layout(location=1) in vec4 color;
uniform mat4 mvp;
uniform int instanced;
out vec4 tint;
out vec3 worldPosition;
void main() {
  vec3 p=position;
  if(instanced!=0) p+=vec3(float(gl_InstanceID%1000)*1.2,float(gl_InstanceID/1000)*1.2,0);
  gl_Position=mvp*vec4(p,1.0);tint=color;worldPosition=p;
})";
    const char *fragment = R"(#version 330 core
in vec4 tint;
in vec3 worldPosition;
uniform int clipEnabled;
uniform vec4 clipPlane;
out vec4 fragment;
void main() {
  if(clipEnabled!=0 && dot(clipPlane,vec4(worldPosition,1.0))<0.0) discard;
  fragment=tint;
})";
    shader_ = std::make_unique<QOpenGLShaderProgram>();
    if (!shader_->addShaderFromSourceCode(QOpenGLShader::Vertex, vertex) ||
        !shader_->addShaderFromSourceCode(QOpenGLShader::Fragment, fragment) || !shader_->link()) {
        emit message(shader_->log());
        return;
    }
    if (!vao_.create()) {
        emit message("Could not create the viewport vertex array");
        return;
    }
    for (auto *batch : {&gridGpu_, &transparentGpu_, &benchmarkGpu_}) {
        if (!batch->buffer.create()) {
            emit message("Could not create a viewport buffer");
            return;
        }
        batch->buffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
    }
    ++stats_.contextGeneration;
    cacheDirty_ = true;
    transparentDirty_ = true;
    benchmarkDirty_ = true;
    ready_ = true;
    gridDirty_ = true;
}
QMatrix4x4 Viewport::matrix() const {
    QMatrix4x4 projection, view;
    float aspect = float(width()) / std::max(1, height());
    if (ortho_)
        projection.ortho(-distance_ * aspect * .45f, distance_ * aspect * .45f, -distance_ * .45f,
                         distance_ * .45f, .01f, std::max(1000.f, distance_ * 10));
    else
        projection.perspective(45, aspect, std::max(.001f, distance_ / 10000),
                               std::max(1000.f, distance_ * 10));
    QVector3D direction(std::cos(pitch_ * degreesToRadians) * std::cos(yaw_ * degreesToRadians),
                        std::cos(pitch_ * degreesToRadians) * std::sin(yaw_ * degreesToRadians),
                        std::sin(pitch_ * degreesToRadians));
    view.lookAt(target_ + direction * distance_, target_, {0, 0, 1});
    return projection * view;
}
QPointF Viewport::project(Vec3 p) const {
    auto v = matrix() * QVector4D(qv(p), 1);
    if (std::abs(v.w()) < 1e-9)
        return {};
    auto n = v.toVector3DAffine();
    return {(n.x() + 1) * width() / 2, (1 - n.y()) * height() / 2};
}
std::pair<Vec3, Vec3> Viewport::ray(QPointF p) const {
    auto inv = matrix().inverted();
    float x = 2 * p.x() / width() - 1, y = 1 - 2 * p.y() / height();
    auto a = (inv * QVector4D(x, y, -1, 1)).toVector3DAffine(),
         b = (inv * QVector4D(x, y, 1, 1)).toVector3DAffine();
    return {vec(a), normalized(vec(b - a))};
}
std::optional<Vec3> Viewport::ground(QPointF p) const {
    auto [o, d] = ray(p);
    if (std::abs(d.z) < 1e-7)
        return {};
    double t = -o.z / d.z;
    if (t < 0)
        return {};
    auto v = o + d * t;
    v.x = std::round(v.x * 10) / 10;
    v.y = std::round(v.y * 10) / 10;
    v.z = 0;
    return v;
}
Viewport::FaceHit Viewport::nearestFace(QPointF p) const {
    auto [o, d] = ray(p);
    FaceHit hit;
    auto intersect = [&](const Triangle &t, Id body) {
        auto e1 = t.b - t.a, e2 = t.c - t.a;
        auto h = cross(d, e2);
        double a = dot(e1, h);
        if (std::abs(a) <= length(e1) * length(e2) * 1e-12)
            return;
        double f = 1 / a;
        auto s = o - t.a;
        double u = f * dot(s, h);
        if (u < 0 || u > 1)
            return;
        auto q = cross(s, e1);
        double v = f * dot(d, q);
        if (v < 0 || u + v > 1)
            return;
        double distance = f * dot(e2, q);
        const bool nearer = distance < hit.distance - tolerance;
        const bool tied = std::abs(distance - hit.distance) <= tolerance &&
                          std::pair{body, t.face} > std::pair{hit.body, hit.face};
        if (distance > 0 && (nearer || tied) && !clipped(o + d * distance)) {
            hit = {body, t.face, distance};
        }
    };
    if (cacheDirty_ || cachedDocument_ != doc_.identity() || cachedRevision_ != doc_.revision()) {
        // Picking can occur before the queued repaint; inspect current geometry
        // without requiring a current OpenGL context.
        for (const auto &[id, body] : doc_.bodies()) {
            if (cachedDocument_ == doc_.identity() && opacity_.contains(id) && opacity_.at(id) == 0)
                continue;
            for (const auto &triangle : doc_.worldTriangles(id))
                intersect(triangle, id);
        }
    } else
        for (const auto &[id, cache] : bodyCaches_) {
            if (cache->alpha == 0 || !cache->bounds.valid)
                continue;
            double low = 0, high = INFINITY;
            for (auto values :
                 {std::array<double, 4>{o.x, d.x, cache->bounds.minimum.x, cache->bounds.maximum.x},
                  std::array<double, 4>{o.y, d.y, cache->bounds.minimum.y, cache->bounds.maximum.y},
                  std::array<double, 4>{o.z, d.z, cache->bounds.minimum.z,
                                        cache->bounds.maximum.z}}) {
                if (std::abs(values[1]) < 1e-15) {
                    if (values[0] < values[2] - tolerance || values[0] > values[3] + tolerance)
                        high = -1;
                } else {
                    auto a = (values[2] - tolerance - values[0]) / values[1],
                         b = (values[3] + tolerance - values[0]) / values[1];
                    if (a > b)
                        std::swap(a, b);
                    low = std::max(low, a);
                    high = std::min(high, b);
                }
            }
            if (high < low || low > hit.distance + tolerance)
                continue;
            for (const auto &triangle : cache->worldTriangles)
                intersect(triangle, id);
        }
    return hit;
}
std::pair<Id, Id> Viewport::pick(QPointF point) const {
    const auto hit = nearestFace(point);
    return {hit.body, hit.face};
}
Viewport::Bounds Viewport::bodyBounds(Id id) const {
    if (!cacheDirty_ && cachedDocument_ == doc_.identity() && cachedRevision_ == doc_.revision() &&
        bodyCaches_.contains(id) && bodyCaches_.at(id)->record == doc_.bodies().at(id))
        return bodyCaches_.at(id)->bounds;
    Bounds bounds;
    const auto world = doc_.worldTransform(id);
    for (const auto &[vertex, local] : doc_.bodies().at(id)->surface.vertices) {
        const auto point = world.point(local);
        if (!bounds.valid) {
            bounds.minimum = bounds.maximum = point;
            bounds.valid = true;
        } else {
            bounds.minimum = {std::min(bounds.minimum.x, point.x),
                              std::min(bounds.minimum.y, point.y),
                              std::min(bounds.minimum.z, point.z)};
            bounds.maximum = {std::max(bounds.maximum.x, point.x),
                              std::max(bounds.maximum.y, point.y),
                              std::max(bounds.maximum.z, point.z)};
        }
    }
    return bounds;
}
std::pair<Id, Id> Viewport::pickEdge(QPointF point, double radius) const {
    if (!std::isfinite(radius) || radius < 0 || radius > 64)
        throw std::runtime_error("Edge pick radius must be between 0 and 64 logical pixels");
    std::pair<Id, Id> hit{};
    double best = radius * radius, nearest = INFINITY;
    const auto transform = matrix();
    auto candidate = [&](Id body, Id edge, Vec3 a, Vec3 b) {
        auto ca = transform * QVector4D(qv(a), 1), cb = transform * QVector4D(qv(b), 1);
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
        if (clipPlane_) {
            const auto &p = *clipPlane_;
            if (!clip(p[0] * a.x + p[1] * a.y + p[2] * a.z + p[3],
                      p[0] * b.x + p[1] * b.y + p[2] * b.z + p[3]))
                return;
        }
        const auto delta = b - a;
        b = a + delta * last;
        a = a + delta * first;
        ca = transform * QVector4D(qv(a), 1);
        cb = transform * QVector4D(qv(b), 1);
        if (ca.w() <= 0 || cb.w() <= 0)
            return;
        const auto pa = project(a), pb = project(b), screen = pb - pa;
        const auto lengthSquared = QPointF::dotProduct(screen, screen);
        const auto fraction =
            lengthSquared > 0
                ? std::clamp(QPointF::dotProduct(point - pa, screen) / lengthSquared, 0.0, 1.0)
                : 0.0;
        const auto nearestPixel = pa + screen * fraction;
        const auto gap = point - nearestPixel;
        const auto distanceSquared = QPointF::dotProduct(gap, gap);
        if (distanceSquared > best)
            return;
        const auto weight = (fraction / cb.w()) / ((1 - fraction) / ca.w() + fraction / cb.w());
        const auto worldPoint = a + (b - a) * weight;
        const auto [origin, direction] = ray(nearestPixel);
        const auto depth = dot(worldPoint - origin, direction);
        if (depth < 0)
            return;
        if (std::abs(distanceSquared - best) < 1e-9 &&
            (depth > nearest + tolerance ||
             (std::abs(depth - nearest) <= tolerance && std::pair{body, edge} <= hit)))
            return;
        const auto face = nearestFace(nearestPixel);
        // A small depth tolerance accommodates the float projection and edge bias.
        if (face.body && face.distance < depth - std::max(1e-5, depth * 1e-5))
            return;
        best = distanceSquared;
        nearest = depth;
        hit = {body, edge};
    };
    if (cacheDirty_ || cachedDocument_ != doc_.identity() || cachedRevision_ != doc_.revision()) {
        for (const auto &[id, body] : doc_.bodies()) {
            if (cachedDocument_ == doc_.identity() && opacity_.contains(id) && opacity_.at(id) == 0)
                continue;
            const auto world = doc_.worldTransform(id);
            for (const auto &[edge, record] : body->topology.edges)
                candidate(id, edge, world.point(body->surface.vertices.at(record.a)),
                          world.point(body->surface.vertices.at(record.b)));
        }
    } else
        for (const auto &[id, cache] : bodyCaches_) {
            if (cache->alpha == 0)
                continue;
            for (const auto &edge : cache->worldEdges)
                candidate(id, edge.id, edge.a, edge.b);
        }
    return hit;
}
void Viewport::rebuild() {
    if (selectionDocument_ != doc_.identity()) {
        selected_ = selectedFace_ = 0;
        selectionDocument_ = doc_.identity();
    }
    stats_.meshTriangles = 0;
    transparent_.clear();
    if (cachedDocument_ != doc_.identity()) {
        opacity_.clear();
        bodyCaches_.clear(); // Rebuild runs with the owning GL context current.
        cachedDocument_ = doc_.identity();
        transparentDirty_ = true;
    }
    std::erase_if(opacity_, [&](const auto &item) { return !doc_.bodies().contains(item.first); });
    std::erase_if(bodyCaches_, [&](const auto &item) {
        if (doc_.bodies().contains(item.first))
            return false;
        if (!item.second->transparent.empty())
            transparentDirty_ = true;
        return true;
    });
    auto vertex = [](Vec3 p, std::array<float, 3> c) {
        return Vertex{float(p.x), float(p.y), float(p.z), c[0], c[1], c[2]};
    };
    if (gridDirty_) {
        std::vector<Vertex> grid;
        for (int i = -50; i <= 50; ++i) {
            const float shade =
                colors_.dark ? ((i % 5 == 0) ? .27f : .21f) : ((i % 5 == 0) ? .79f : .87f);
            for (auto p : {Vec3{double(i), -50, 0}, Vec3{double(i), 50, 0}, Vec3{-50, double(i), 0},
                           Vec3{50, double(i), 0}})
                grid.push_back(vertex(p, {shade, shade, shade}));
        }
        auto axis = [&](Vec3 a, Vec3 b, std::array<float, 3> color) {
            grid.push_back(vertex(a, color));
            grid.push_back(vertex(b, color));
        };
        axis({0, 0, .002}, {8, 0, .002}, {.72f, .30f, .26f});
        axis({0, 0, .002}, {0, 8, .002}, {.29f, .52f, .35f});
        axis({0, 0, 0}, {0, 0, 5}, {.29f, .46f, .70f});
        upload(gridGpu_, grid);
        gridDirty_ = false;
    }
    for (const auto &[id, body] : doc_.bodies()) {
        if (!bodyCaches_.contains(id))
            bodyCaches_.emplace(id, std::make_unique<BodyCache>());
        auto &cache = *bodyCaches_.at(id);
        const auto world = doc_.worldTransform(id);
        const float alpha = opacity_.contains(id) ? opacity_.at(id) : 1.f;
        const bool selected = selected_ == id;
        const auto selectedFace = selected ? selectedFace_ : 0;
        const bool meshChanged =
            !cache.record || (cache.record != body && cache.record->surface != body->surface);
        const bool topologyChanged =
            meshChanged || !cache.record || cache.record->topology.edges != body->topology.edges;
        const bool worldChanged = meshChanged || !cache.record || cache.world != world;
        const bool appearanceChanged = worldChanged || topologyChanged || !cache.record ||
                                       cache.record->color != body->color || cache.alpha != alpha ||
                                       cache.selected != selected ||
                                       cache.selectedFace != selectedFace;
        if (meshChanged) {
            cache.localTriangles = body->surface.triangles();
            ++stats_.bodyMeshBuilds;
        }
        if (topologyChanged) {
            cache.localEdges.clear();
            for (const auto &[edge, record] : body->topology.edges)
                cache.localEdges.push_back({body->surface.vertices.at(record.a),
                                            body->surface.vertices.at(record.b), edge});
        }
        if (worldChanged) {
            cache.bounds = bodyBounds(id);
            cache.worldTriangles = cache.localTriangles;
            for (auto &triangle : cache.worldTriangles) {
                triangle.a = world.point(triangle.a);
                triangle.b = world.point(triangle.b);
                triangle.c = world.point(triangle.c);
            }
            ++stats_.bodyWorldUpdates;
        }
        if (worldChanged || topologyChanged) {
            cache.worldEdges = cache.localEdges;
            for (auto &edge : cache.worldEdges) {
                edge.a = world.point(edge.a);
                edge.b = world.point(edge.b);
            }
        }
        if (appearanceChanged) {
            if (!cache.transparent.empty() || (alpha > 0 && alpha < 1))
                transparentDirty_ = true;
            cache.opaque.clear();
            cache.lines.clear();
            cache.transparent.clear();
            if (alpha > 0) {
                for (const auto &triangle : cache.worldTriangles) {
                    const auto crossProduct =
                        cross(triangle.b - triangle.a, triangle.c - triangle.a);
                    const auto magnitude = length(crossProduct);
                    if (magnitude == 0)
                        continue;
                    const auto normal = crossProduct * (1 / magnitude);
                    const float light =
                        .64f + .36f * std::abs(dot(normal, normalized({.3, -.5, .8})));
                    auto color = body->color;
                    if (selected)
                        color = {.83f, .66f, .40f};
                    if (selected && triangle.face == selectedFace)
                        color = {.94f, .72f, .38f};
                    for (auto &component : color)
                        component *= light;
                    std::array<Vertex, 3> vertices{vertex(triangle.a, color),
                                                   vertex(triangle.b, color),
                                                   vertex(triangle.c, color)};
                    for (auto &v : vertices)
                        v.a = alpha;
                    if (alpha < 1)
                        cache.transparent.push_back(vertices);
                    else
                        cache.opaque.insert(cache.opaque.end(), vertices.begin(), vertices.end());
                }
                for (const auto &edge : cache.worldEdges) {
                    cache.lines.push_back(vertex(edge.a, {.19f, .24f, .23f}));
                    cache.lines.push_back(vertex(edge.b, {.19f, .24f, .23f}));
                }
            }
        }
        if (appearanceChanged || !cache.opaqueGpu.buffer.isCreated() ||
            !cache.linesGpu.buffer.isCreated()) {
            upload(cache.opaqueGpu, cache.opaque);
            upload(cache.linesGpu, cache.lines);
            ++stats_.bodyUploads;
        }
        cache.record = body;
        cache.world = world;
        cache.alpha = alpha;
        cache.selected = selected;
        cache.selectedFace = selectedFace;
        if (alpha > 0)
            stats_.meshTriangles += cache.worldTriangles.size();
        transparent_.insert(transparent_.end(), cache.transparent.begin(), cache.transparent.end());
    }
    stats_.cachedBodies = bodyCaches_.size();

    cachedRevision_ = doc_.revision();
    cacheDirty_ = false;
}
void Viewport::upload(GpuBatch &batch, const std::vector<Vertex> &vertices, bool transparent) {
    if (vertices.size() > size_t(std::numeric_limits<int>::max()) / sizeof(Vertex))
        throw std::runtime_error("Viewport buffer exceeds the supported size");
    if (!batch.buffer.isCreated()) {
        if (!batch.buffer.create())
            throw std::runtime_error("Could not create viewport buffer");
        batch.buffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
    }
    batch.buffer.bind();
    batch.buffer.allocate(vertices.data(), int(vertices.size() * sizeof(Vertex)));
    batch.buffer.release();
    batch.count = int(vertices.size());
    stats_.uploadedBytes += vertices.size() * sizeof(Vertex);
    if (transparent)
        ++stats_.transparencyUploads;
    else
        ++stats_.geometryUploads;
}
void Viewport::draw(GpuBatch &batch, GLenum mode, int count) {
    if (batch.count == 0)
        return;
    vao_.bind();
    batch.buffer.bind();
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void *>(3 * sizeof(float)));
    if (count > 1)
        glDrawArraysInstanced(mode, 0, batch.count, count);
    else
        glDrawArrays(mode, 0, batch.count);
    batch.buffer.release();
    vao_.release();
}
void Viewport::sortTransparent(const QMatrix4x4 &transform) {
    if (!transparentDirty_ && transform == sortedMatrix_)
        return;
    if (!transparentDirty_ && transparent_.empty()) {
        sortedMatrix_ = transform;
        return;
    }
    // Centroid depth sorting is a feasibility path for non-intersecting layers.
    // Intersecting transparent surfaces need order-independent transparency later.
    auto sorted = transparent_;
    QVector3D direction(std::cos(pitch_ * degreesToRadians) * std::cos(yaw_ * degreesToRadians),
                        std::cos(pitch_ * degreesToRadians) * std::sin(yaw_ * degreesToRadians),
                        std::sin(pitch_ * degreesToRadians));
    auto depth = [&](const auto &t) {
        QVector3D center((t[0].x + t[1].x + t[2].x) / 3, (t[0].y + t[1].y + t[2].y) / 3,
                         (t[0].z + t[1].z + t[2].z) / 3);
        return QVector3D::dotProduct(center - target_, direction);
    };
    std::stable_sort(sorted.begin(), sorted.end(),
                     [&](const auto &a, const auto &b) { return depth(a) < depth(b); });
    std::vector<Vertex> vertices;
    vertices.reserve(sorted.size() * 3);
    for (const auto &triangle : sorted)
        vertices.insert(vertices.end(), triangle.begin(), triangle.end());
    upload(transparentGpu_, vertices, true);
    sortedMatrix_ = transform;
    transparentDirty_ = false;
}
void Viewport::paintGL() {
    try {
        paintScene();
    } catch (const std::exception &error) {
        ready_ = false;
        if (shader_)
            shader_->release();
        emit message(QString("Viewport unavailable: %1").arg(error.what()));
    }
}
void Viewport::paintScene() {
    if (!ready_)
        return;
    QElapsedTimer timer;
    timer.start();
    glClearColor(colors_.canvas.redF(), colors_.canvas.greenF(), colors_.canvas.blueF(), 1);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_BLEND);
    shader_->bind();
    auto transform = matrix();
    shader_->setUniformValue("mvp", transform);
    shader_->setUniformValue("instanced", instances_ > 0 ? 1 : 0);
    shader_->setUniformValue("clipEnabled", clipPlane_ ? 1 : 0);
    if (clipPlane_) {
        auto c = *clipPlane_;
        shader_->setUniformValue("clipPlane", QVector4D(c[0], c[1], c[2], c[3]));
    }
    if (benchmarkTriangles_ > 0) {
        if (benchmarkDirty_) {
            upload(benchmarkGpu_, benchmarkVertices_);
            benchmarkDirty_ = false;
        }
        draw(benchmarkGpu_, GL_TRIANGLES, std::max(1, instances_));
        glFinish();
    } else {
        if (cacheDirty_ || cachedRevision_ != doc_.revision() || cachedDocument_ != doc_.identity())
            rebuild();
        // Reference grid does not write depth or shine through coplanar opaque faces.
        glDepthMask(GL_FALSE);
        draw(gridGpu_, GL_LINES);
        glDepthMask(GL_TRUE);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1, 1);
        for (auto &[id, cache] : bodyCaches_)
            draw(cache->opaqueGpu, GL_TRIANGLES);
        sortTransparent(transform);
        if (transparentGpu_.count) {
            glEnable(GL_BLEND);
            // Preserve opaque framebuffer alpha for Qt's premultiplied composition.
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                                GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
            draw(transparentGpu_, GL_TRIANGLES);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }
        glDisable(GL_POLYGON_OFFSET_FILL);
        for (auto &[id, cache] : bodyCaches_)
            draw(cache->linesGpu, GL_LINES);
    }
    shader_->release();
    glDisable(GL_DEPTH_TEST);
    if (auto error = glGetError(); error != GL_NO_ERROR)
        stats_.glError = error;
    ++stats_.frames;
    frameMs_ = timer.nsecsElapsed() / 1e6;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (hasFocus()) {
        p.setPen(QPen(colors_.accent, 2));
        p.setBrush(Qt::NoBrush);
        p.drawRect(rect().adjusted(1, 1, -1, -1));
    }
    p.setPen(colors_.ink);
    p.drawText(20, 28, ortho_ ? "ORTHOGRAPHIC  /  METERS" : "PERSPECTIVE  /  METERS");
    p.setPen(colors_.muted);
    p.drawText(20, height() - 22, "Z up   ·   Grid 1 m   ·   Snap 0.1 m");
    if (anchor_ && cursor_) {
        p.setPen(QPen(QColor("#b9762f"), 2, Qt::DashLine));
        p.setBrush(QBrush(QColor(190, 130, 65, 45), Qt::BDiagPattern));
        QPolygonF poly;
        if (tool_ == Tool::Circle) {
            double r = length(*cursor_ - *anchor_);
            for (int i = 0; i < 48; ++i) {
                double a = i * 2 * std::numbers::pi / 48;
                poly << project(*anchor_ + Vec3{r * std::cos(a), r * std::sin(a), 0});
            }
        } else {
            auto a = *anchor_, b = *cursor_;
            for (auto v : {a, Vec3{b.x, a.y, 0}, b, Vec3{a.x, b.y, 0}})
                poly << project(v);
        }
        p.drawPolygon(poly);
    }
}
void Viewport::refresh() {
    if (selectionDocument_ != doc_.identity()) {
        selected_ = selectedFace_ = 0;
        selectionDocument_ = doc_.identity();
    }
    if (!doc_.bodies().contains(selected_)) {
        selected_ = 0;
        selectedFace_ = 0;
    }
    if (selected_ && selectedFace_ &&
        !doc_.bodies().at(selected_)->surface.faces.contains(selectedFace_))
        selectedFace_ = 0;
    cacheDirty_ = true;
    update();
}
void Viewport::setSelection(Id body, Id face) {
    selectionDocument_ = doc_.identity();
    selected_ = body;
    selectedFace_ = face;
    refresh();
    emit selected(selected_, selectedFace_);
}
void Viewport::setTool(Tool tool) {
    cancel();
    tool_ = tool;
    setCursor(tool == Tool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
}
void Viewport::cancel() {
    anchor_.reset();
    cursor_.reset();
    dragging_ = false;
    dragButton_ = Qt::NoButton;
    update();
}
void Viewport::fit() {
    if (doc_.bodies().empty()) {
        target_ = {0, 0, 0};
        distance_ = 14;
        update();
        return;
    }
    QVector3D lo(1e9, 1e9, 1e9), hi(-1e9, -1e9, -1e9);
    bool hasVertices = false;
    for (const auto &[id, b] : doc_.bodies()) {
        const auto world = doc_.worldTransform(id);
        for (auto [vid, local] : b->surface.vertices) {
            hasVertices = true;
            const auto point = qv(world.point(local));
            for (int i = 0; i < 3; ++i) {
                lo[i] = std::min(lo[i], point[i]);
                hi[i] = std::max(hi[i], point[i]);
            }
        }
    }
    if (!hasVertices) {
        target_ = {0, 0, 0};
        distance_ = 14;
        update();
        return;
    }
    target_ = (lo + hi) / 2;
    distance_ = std::max(2.f, (hi - lo).length() * 1.7f);
    update();
}
void Viewport::standardView(int view) {
    ortho_ = view != 0;
    yaw_ = view == 2 ? -90 : -45;
    pitch_ = view == 1 ? 89.99f : (view == 2 ? .01f : 35);
    update();
}
void Viewport::finishShape(Vec3 end) {
    if (!anchor_)
        return;
    auto a = *anchor_;
    std::vector<Vec3> loop;
    if (tool_ == Tool::Circle) {
        double r = length(end - a);
        for (int i = 0; i < 48; ++i) {
            double angle = i * 2 * std::numbers::pi / 48;
            loop.push_back(a + Vec3{r * std::cos(angle), r * std::sin(angle), 0});
        }
    } else {
        double x = std::min(a.x, end.x), y = std::min(a.y, end.y), w = std::abs(a.x - end.x),
               h = std::abs(a.y - end.y);
        loop = {{x, y, 0}, {x + w, y, 0}, {x + w, y + h, 0}, {x, y + h, 0}};
    }
    try {
        auto id = doc_.addFace({loop}, tool_ == Tool::Circle ? "Circle" : "Rectangle");
        cancel();
        setSelection(id, doc_.bodies().at(id)->surface.faces.begin()->first);
        emit changed();
    } catch (const std::exception &e) {
        emit message(e.what());
    }
}
void Viewport::setTheme(const ThemeColors &colors) {
    if (colors_.dark != colors.dark)
        gridDirty_ = true;
    colors_ = colors;
    refresh();
}
bool Viewport::measurements(const QString &text) {
    const auto revision = doc_.revision();
    auto parts = text.split(QRegularExpression("[,;\\s]+"), Qt::SkipEmptyParts);
    std::vector<double> values;
    for (auto part : parts) {
        bool ok = false;
        double d = part.toDouble(&ok);
        if (!ok || !std::isfinite(d)) {
            emit message("Enter finite measurements in meters");
            return false;
        }
        values.push_back(d);
    }
    try {
        if (tool_ == Tool::Extrude && selected_ && selectedFace_ && values.size() == 1) {
            doc_.extrude(selected_, selectedFace_, values[0]);
            refresh();
            emit changed();
            emit message("Extruded face. Ctrl+Z undoes this edit.");
            return true;
        }
        if (anchor_ && tool_ == Tool::Rectangle && values.size() == 2)
            finishShape(*anchor_ + Vec3{values[0], values[1], 0});
        else if (anchor_ && tool_ == Tool::Circle && values.size() == 1 && values[0] > 0)
            finishShape(*anchor_ + Vec3{values[0], 0, 0});
        else
            emit message("Rectangle: click first corner, enter width, depth. Circle: enter radius. "
                         "Extrude: select a face, enter distance.");
    } catch (const std::exception &e) {
        emit message(e.what());
    }
    return doc_.revision() != revision;
}
bool Viewport::event(QEvent *event) {
    if (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut)
        update();
    // A compositor can end an implicit pointer grab without delivering release.
    // Keep a drawing preview when focus moves to Measurements, but end navigation.
    if (event->type() == QEvent::UngrabMouse || event->type() == QEvent::WindowDeactivate ||
        event->type() == QEvent::Hide || event->type() == QEvent::FocusOut) {
        dragging_ = false;
        dragButton_ = Qt::NoButton;
    }
    return QOpenGLWidget::event(event);
}
void Viewport::mousePressEvent(QMouseEvent *e) {
    setFocus();
    previous_ = e->position();
    dragButton_ = e->button();
    if (e->button() != Qt::LeftButton || tool_ == Tool::Orbit || tool_ == Tool::Pan ||
        e->modifiers().testFlag(Qt::AltModifier)) {
        dragging_ = true;
        return;
    }
    if (tool_ == Tool::Rectangle || tool_ == Tool::Circle) {
        if (auto p = ground(e->position())) {
            if (!anchor_) {
                anchor_ = p;
                cursor_ = p;
                emit message("Click the second point or type measurements below · Esc cancels");
            } else
                finishShape(*p);
        }
        return;
    }
    auto [body, face] = pick(e->position());
    setSelection(body, face);
    if (tool_ == Tool::Extrude)
        emit message(body ? "Enter extrusion distance in meters, then press Enter"
                          : "Select an isolated face to extrude");
}
void Viewport::mouseMoveEvent(QMouseEvent *e) {
    if (dragging_ && !e->buttons().testFlag(dragButton_)) {
        dragging_ = false;
        dragButton_ = Qt::NoButton;
    }
    auto delta = e->position() - previous_;
    previous_ = e->position();
    if (dragging_) {
        if (tool_ == Tool::Pan || dragButton_ == Qt::RightButton ||
            e->modifiers().testFlag(Qt::ShiftModifier)) {
            auto [a, da] = ray(e->position());
            auto [b, db] = ray(e->position() - delta);
            target_ += qv((b + db * distance_) - (a + da * distance_));
        } else {
            yaw_ -= delta.x() * .4f;
            pitch_ = std::clamp(pitch_ + float(delta.y()) * .4f, -89.f, 89.f);
        }
    } else if (anchor_)
        cursor_ = ground(e->position());
    update();
}
void Viewport::mouseReleaseEvent(QMouseEvent *e) {
    if (e->button() != dragButton_)
        return;
    dragging_ = false;
    dragButton_ = Qt::NoButton;
}
void Viewport::wheelEvent(QWheelEvent *e) {
    const auto steps =
        e->pixelDelta().isNull() ? e->angleDelta().y() / 120.f : e->pixelDelta().y() / 15.f;
    distance_ = std::clamp(distance_ * std::exp(-steps * .12f), .05f, 1e7f);
    update();
    e->accept();
}
void Viewport::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Escape) {
        cancel();
        emit message("Operation canceled");
    } else
        QOpenGLWidget::keyPressEvent(e);
}
void Viewport::setBodyOpacity(Id body, float opacity) {
    if (!doc_.bodies().contains(body) || !std::isfinite(opacity) || opacity < 0 || opacity > 1)
        throw std::runtime_error("Opacity requires an existing body and a value from 0 to 1");
    if (cachedDocument_ != doc_.identity()) {
        opacity_.clear();
        cachedDocument_ = doc_.identity();
    }
    opacity_[body] = opacity;
    refresh();
}
void Viewport::setClipPlane(std::optional<std::array<double, 4>> plane) {
    if (plane) {
        for (double x : *plane)
            if (!std::isfinite(x))
                throw std::runtime_error("Clip plane must be finite");
        auto n = length({(*plane)[0], (*plane)[1], (*plane)[2]});
        if (n < tolerance || !std::isfinite(n))
            throw std::runtime_error("Clip plane needs a normal");
        for (auto &x : *plane)
            x /= n;
        if (std::abs((*plane)[3]) > coordinateLimit)
            throw std::runtime_error("Clip plane exceeds coordinate limits");
    }
    clipPlane_ = plane;
    update();
}
bool Viewport::clipped(Vec3 point) const {
    if (!clipPlane_)
        return false;
    auto p = *clipPlane_;
    return p[0] * point.x + p[1] * point.y + p[2] * point.z + p[3] < 0;
}
void Viewport::benchmark(int count, bool instanced) {
    if (count < 1 || count > 1000000)
        throw std::runtime_error("Benchmark supports 1–1000000 triangles");
    benchmarkTriangles_ = count;
    instances_ = instanced ? count : 0;
    benchmarkVertices_.clear();
    benchmarkVertices_.reserve(instanced ? 3 : count * 3);
    for (int i = 0; i < (instanced ? 1 : count); ++i) {
        float x = (i % 1000) * 1.2f, y = (i / 1000) * 1.2f;
        float z = instanced ? 0 : std::sin(float(i) * .017f) * .2f;
        for (auto p : {Vec3{x, y, z}, Vec3{x + 1, y, z}, Vec3{x, y + 1, z + .1f}})
            benchmarkVertices_.push_back({float(p.x), float(p.y), float(p.z), .4f, .6f, .5f});
    }
    stats_.meshTriangles = count;
    benchmarkDirty_ = true;
    target_ = {600, float(count / 1000) * .6f, 0};
    distance_ = 1600;
    pitch_ = 89;
    ortho_ = true;
    update();
}
} // namespace sketchy
