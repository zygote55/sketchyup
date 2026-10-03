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
constexpr float radians = std::numbers::pi_v<float> / 180;
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
        for (auto *batch : {&opaqueGpu_, &linesGpu_, &transparentGpu_, &benchmarkGpu_}) {
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
    for (auto *batch : {&opaqueGpu_, &linesGpu_, &transparentGpu_, &benchmarkGpu_}) {
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
    QVector3D direction(std::cos(pitch_ * radians) * std::cos(yaw_ * radians),
                        std::cos(pitch_ * radians) * std::sin(yaw_ * radians),
                        std::sin(pitch_ * radians));
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
std::pair<Id, Id> Viewport::pick(QPointF p) const {
    auto [o, d] = ray(p);
    double nearest = INFINITY;
    std::pair<Id, Id> hit{};
    auto intersect = [&](const Triangle &t, Id body) {
        auto e1 = t.b - t.a, e2 = t.c - t.a;
        auto h = cross(d, e2);
        double a = dot(e1, h);
        if (std::abs(a) < 1e-10)
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
        if (distance > 0 && distance < nearest && !clipped(o + d * distance)) {
            nearest = distance;
            hit = {body, t.face};
        }
    };
    if (cacheDirty_ || cachedDocument_ != doc_.identity() || cachedRevision_ != doc_.revision()) {
        // Picking can occur before the queued repaint; inspect current geometry
        // without requiring a current OpenGL context.
        for (const auto &[id, body] : doc_.bodies()) {
            if (cachedDocument_ == doc_.identity() && opacity_.contains(id) && opacity_.at(id) == 0)
                continue;
            for (const auto &triangle : body->surface.triangles())
                intersect(triangle, id);
        }
    } else
        for (const auto &item : picking_)
            intersect(item.triangle, item.body);
    return hit;
}
void Viewport::rebuild() {
    triangles_.clear();
    lines_.clear();
    picking_.clear();
    transparent_.clear();
    if (cachedDocument_ != doc_.identity()) {
        opacity_.clear();
        cachedDocument_ = doc_.identity();
    }
    std::erase_if(opacity_, [&](const auto &item) { return !doc_.bodies().contains(item.first); });
    auto vertex = [](Vec3 p, std::array<float, 3> c) {
        return Vertex{float(p.x), float(p.y), float(p.z), c[0], c[1], c[2]};
    };
    for (int i = -50; i <= 50; ++i) {
        const float shade = (i % 5 == 0) ? .79f : .87f;
        for (auto p : {Vec3{double(i), -50, 0}, Vec3{double(i), 50, 0}, Vec3{-50, double(i), 0},
                       Vec3{50, double(i), 0}})
            lines_.push_back(vertex(p, {shade, shade, shade}));
    }
    for (const auto &[id, b] : doc_.bodies()) {
        float alpha = opacity_.contains(id) ? opacity_.at(id) : 1.f;
        if (alpha == 0)
            continue;
        for (auto t : b->surface.triangles()) {
            picking_.push_back({t, id});
            auto n = normalized(cross(t.b - t.a, t.c - t.a));
            float light = .64f + .36f * std::abs(dot(n, normalized({.3, -.5, .8})));
            auto color = b->color;
            if (id == selected_)
                color = {.83f, .66f, .40f};
            if (id == selected_ && t.face == selectedFace_)
                color = {.94f, .72f, .38f};
            for (auto &c : color)
                c *= light;
            std::array<Vertex, 3> triangle{vertex(t.a, color), vertex(t.b, color),
                                           vertex(t.c, color)};
            for (auto &v : triangle)
                v.a = alpha;
            if (alpha < 1)
                transparent_.push_back(triangle);
            else
                triangles_.insert(triangles_.end(), triangle.begin(), triangle.end());
        }
        for (auto e : b->surface.edges())
            for (auto p : {b->surface.vertices.at(e.a), b->surface.vertices.at(e.b)})
                lines_.push_back(vertex(p, {.19f, .24f, .23f}));
    }
    auto line = [&](Vec3 a, Vec3 b, std::array<float, 3> c) {
        lines_.push_back(vertex(a, c));
        lines_.push_back(vertex(b, c));
    };
    line({0, 0, .002}, {8, 0, .002}, {.72f, .30f, .26f});
    line({0, 0, .002}, {0, 8, .002}, {.29f, .52f, .35f});
    line({0, 0, 0}, {0, 0, 5}, {.29f, .46f, .70f});
    upload(opaqueGpu_, triangles_);
    upload(linesGpu_, lines_);
    stats_.meshTriangles = picking_.size();
    transparentDirty_ = true;
    cachedRevision_ = doc_.revision();
    cacheDirty_ = false;
}
void Viewport::upload(GpuBatch &batch, const std::vector<Vertex> &vertices, bool transparent) {
    if (vertices.size() > size_t(std::numeric_limits<int>::max()) / sizeof(Vertex))
        throw std::runtime_error("Viewport buffer exceeds the supported size");
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
    QVector3D direction(std::cos(pitch_ * radians) * std::cos(yaw_ * radians),
                        std::cos(pitch_ * radians) * std::sin(yaw_ * radians),
                        std::sin(pitch_ * radians));
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
    if (!ready_)
        return;
    QElapsedTimer timer;
    timer.start();
    glClearColor(.94f, .945f, .925f, 1);
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
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1, 1);
        draw(opaqueGpu_, GL_TRIANGLES);
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
        draw(linesGpu_, GL_LINES);
    }
    shader_->release();
    glDisable(GL_DEPTH_TEST);
    if (auto error = glGetError(); error != GL_NO_ERROR)
        stats_.glError = error;
    ++stats_.frames;
    frameMs_ = timer.nsecsElapsed() / 1e6;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QColor("#4b5a54"));
    p.drawText(20, 28, ortho_ ? "ORTHOGRAPHIC  /  METERS" : "PERSPECTIVE  /  METERS");
    p.setPen(QColor("#79827b"));
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
    if (!doc_.bodies().contains(selected_)) {
        selected_ = 0;
        selectedFace_ = 0;
    }
    cacheDirty_ = true;
    update();
}
void Viewport::setSelection(Id body, Id face) {
    selected_ = body;
    selectedFace_ = face;
    refresh();
    emit selected(body, face);
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
    for (const auto &[id, b] : doc_.bodies())
        for (auto [vid, p] : b->surface.vertices)
            for (int i = 0; i < 3; ++i) {
                lo[i] = std::min(lo[i], qv(p)[i]);
                hi[i] = std::max(hi[i], qv(p)[i]);
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
void Viewport::measurements(const QString &text) {
    auto parts = text.split(QRegularExpression("[,;\\s]+"), Qt::SkipEmptyParts);
    std::vector<double> values;
    for (auto part : parts) {
        bool ok = false;
        double d = part.toDouble(&ok);
        if (!ok || !std::isfinite(d)) {
            emit message("Enter finite measurements in meters");
            return;
        }
        values.push_back(d);
    }
    try {
        if (tool_ == Tool::Extrude && selected_ && selectedFace_ && values.size() == 1) {
            doc_.extrude(selected_, selectedFace_, values[0]);
            refresh();
            emit changed();
            emit message("Extruded face. Ctrl+Z undoes this edit.");
            return;
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
}
bool Viewport::event(QEvent *event) {
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
    distance_ = std::clamp(distance_ * std::exp(-e->angleDelta().y() * .001f), .05f, 1e7f);
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
