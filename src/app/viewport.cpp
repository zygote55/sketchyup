#include "app/viewport.hpp"
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QWheelEvent>
#include <algorithm>
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
Viewport::~Viewport() {
    if (context()) {
        makeCurrent();
        buffer_.destroy();
        vao_.destroy();
        shader_.removeAllShaders();
        doneCurrent();
    }
}
void Viewport::initializeGL() {
    if (!initializeOpenGLFunctions()) {
        emit message("OpenGL 3.3 is required");
        return;
    }
    graphics_ = QString("%1 / %2").arg(reinterpret_cast<const char *>(glGetString(GL_RENDERER)),
                                       reinterpret_cast<const char *>(glGetString(GL_VERSION)));
    const char *vertex = R"(#version 330 core
layout(location=0) in vec3 position;
layout(location=1) in vec3 color;
uniform mat4 mvp;
uniform int instanced;
out vec3 tint;
void main() {
  vec3 p=position;
  if(instanced!=0) p+=vec3(float(gl_InstanceID%1000)*1.2,float(gl_InstanceID/1000)*1.2,0);
  gl_Position=mvp*vec4(p,1.0);tint=color;
})";
    const char *fragment = R"(#version 330 core
in vec3 tint;
out vec4 fragment;
void main() { fragment=vec4(tint,1.0); }
)";
    if (!shader_.addShaderFromSourceCode(QOpenGLShader::Vertex, vertex) ||
        !shader_.addShaderFromSourceCode(QOpenGLShader::Fragment, fragment) || !shader_.link()) {
        emit message(shader_.log());
        return;
    }
    vao_.create();
    buffer_.create();
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
    for (const auto &item : picking_) {
        auto t = item.triangle;
        auto e1 = t.b - t.a, e2 = t.c - t.a;
        auto h = cross(d, e2);
        double a = dot(e1, h);
        if (std::abs(a) < 1e-10)
            continue;
        double f = 1 / a;
        auto s = o - t.a;
        double u = f * dot(s, h);
        if (u < 0 || u > 1)
            continue;
        auto q = cross(s, e1);
        double v = f * dot(d, q);
        if (v < 0 || u + v > 1)
            continue;
        double distance = f * dot(e2, q);
        if (distance > 0 && distance < nearest) {
            nearest = distance;
            hit = {item.body, t.face};
        }
    }
    return hit;
}
void Viewport::rebuild() {
    triangles_.clear();
    lines_.clear();
    picking_.clear();
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
            for (auto p : {t.a, t.b, t.c})
                triangles_.push_back(vertex(p, color));
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
    cacheDirty_ = false;
}
void Viewport::draw(const std::vector<Vertex> &vertices, GLenum mode, int count) {
    if (vertices.empty())
        return;
    vao_.bind();
    buffer_.bind();
    buffer_.allocate(vertices.data(), int(vertices.size() * sizeof(Vertex)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void *>(3 * sizeof(float)));
    if (count > 1)
        glDrawArraysInstanced(mode, 0, int(vertices.size()), count);
    else
        glDrawArrays(mode, 0, int(vertices.size()));
    buffer_.release();
    vao_.release();
}
void Viewport::paintGL() {
    if (!ready_)
        return;
    QElapsedTimer timer;
    timer.start();
    glClearColor(.94f, .945f, .925f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    shader_.bind();
    shader_.setUniformValue("mvp", matrix());
    shader_.setUniformValue("instanced", instances_ > 0 ? 1 : 0);
    if (instances_ > 0) {
        std::vector<Vertex> t{
            {0, 0, 0, .4f, .6f, .5f}, {1, 0, 0, .4f, .6f, .5f}, {0, 1, 0, .4f, .6f, .5f}};
        draw(t, GL_TRIANGLES, instances_);
        glFinish();
    } else {
        if (cacheDirty_)
            rebuild();
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1, 1);
        draw(triangles_, GL_TRIANGLES);
        glDisable(GL_POLYGON_OFFSET_FILL);
        draw(lines_, GL_LINES);
    }
    shader_.release();
    glDisable(GL_DEPTH_TEST);
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
void Viewport::mouseReleaseEvent(QMouseEvent *) {
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
void Viewport::benchmark(int count) {
    instances_ = count;
    target_ = {600, float(count / 1000) * .6f, 0};
    distance_ = 1600;
    pitch_ = 89;
    ortho_ = true;
    update();
}
} // namespace sketchy
