#include "app/viewport.hpp"
#include "app/unit_display.hpp"
#include "automation/measurements.hpp"
#include "core/appearance.hpp"
#include "core/shading_normals.hpp"
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLVersionFunctionsFactory>
#include <QPainter>
#include <QRegularExpression>
#include <QTimer>
#include <QTabBar>
#include <QVector2D>
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
Viewport::Viewport(Document &doc, QWidget *parent)
    : QOpenGLWidget(parent), doc_(doc), session_(doc) {
    initializeSceneViews();
    initializeWalkNavigation();
    selection_.sync(doc_);
    session_.setScopeProvider([this] { return componentScope(); });
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(160, 160);
    setAccessibleName("3D model viewport");
    inferenceWorker_.request(doc_);
    auto *inferenceTimer = new QTimer(this);
    inferenceTimer->setInterval(25);
    connect(inferenceTimer, &QTimer::timeout, this, [this] {
        const auto textures = textureCache_.snapshot();
        if (textures != textureSnapshot_) {
            cacheDirty_ = pickDirty_ = true;
            update();
        }
        armReference();
        if (!inferencePending_)
            return;
        if (inferenceReady()) {
            inferencePending_ = false;
            if (drawingTool() && tool_ != Tool::Freehand) {
                try {
                    if (session_.active())
                        updateToolPreview(inferencePointer_);
                    else
                        acquireInference(inferencePointer_, false);
                } catch (const std::exception &error) {
                    emit message(error.what());
                }
            }
            update();
        } else if (const auto error = inferenceWorker_.error(doc_); !error.empty()) {
            inferencePending_ = false;
            emit message(QString::fromStdString(error));
            update();
        }
    });
    inferenceTimer->start();
}
Viewport::~Viewport() { cleanupGL(); }
void Viewport::cleanupGL() {
    disconnect(contextCleanup_);
    if (context() && gl_) {
        makeCurrent();
        for (auto *batch : {&gridGpu_, &groundGpu_, &profilesGpu_, &transparentGpu_, &benchmarkGpu_, &pickFacesGpu_,
                            &pickEdgesGpu_, &selectedFacesGpu_, &selectedEdgesGpu_, &hoverFacesGpu_,
                            &hoverEdgesGpu_, &assistantTrianglesGpu_, &assistantLinesGpu_}) {
            batch->buffer.destroy();
            batch->count = 0;
        }
        for (auto &[id, cache] : bodyCaches_)
            for (auto *batch : {&cache->opaqueGpu, &cache->linesGpu, &cache->hiddenLinesGpu}) {
                batch->buffer.destroy();
                batch->count = 0;
            }
        for (const auto &[id, name] : textureGpu_)
            gl_->glDeleteTextures(1, &name);
        cleanupSolarShadowMap();
        textureGpu_.clear();
        textureImages_.clear();
        vao_.destroy();
        shader_.reset();
        doneCurrent();
    }
    gl_ = nullptr;
    ready_ = false;
    assistantPreviewDirty_ = true;
    cacheDirty_ = true;
    transparentDirty_ = true;
    benchmarkDirty_ = true;
    gridDirty_ = true;
    pickDirty_ = overlayDirty_ = true;
}
void Viewport::initializeGL() {
    ready_ = false;
    gl_ = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_3_3_Core>(context());
    if (!gl_) {
        emit message("OpenGL 3.3 is required");
        return;
    }
    contextCleanup_ = connect(context(), &QOpenGLContext::aboutToBeDestroyed, this,
                              &Viewport::cleanupGL, Qt::DirectConnection);
    graphics_ = QString("%1 / %2").arg(reinterpret_cast<const char *>(gl_->glGetString(GL_RENDERER)),
                                       reinterpret_cast<const char *>(gl_->glGetString(GL_VERSION)));
    const char *vertex = R"(#version 330 core
layout(location=0) in vec3 position;
layout(location=1) in vec4 color;
layout(location=2) in vec4 backColor;
layout(location=3) in vec4 coordinates;
layout(location=4) in vec4 shading;
out vec4 uv;
out vec4 previewShading;
uniform mat4 mvp;
uniform int instanced;
uniform vec2 pixelOffset;
uniform int screenStroke;
out vec4 tint;
out vec4 backTint;
flat out vec2 faceOpacity;
out vec3 worldPosition;
void main() {
  uv=coordinates;previewShading=shading;
  vec3 p=position;
  if(instanced!=0) p+=vec3(float(gl_InstanceID%1000)*1.2,float(gl_InstanceID/1000)*1.2,0);
  gl_Position=mvp*vec4(p,1.0);
  if(screenStroke!=0) gl_Position.xy+=coordinates.xy*gl_Position.w;
  gl_Position.xy+=pixelOffset*gl_Position.w;tint=color;backTint=backColor;faceOpacity=vec2(color.a,backColor.a);worldPosition=p;
})";
    const char *fragment = R"(#version 330 core
in vec4 tint;
in vec4 backTint;
in vec4 uv;
in vec4 previewShading;
uniform sampler2D frontImage;
uniform sampler2D backImage;
uniform sampler2D solarShadowMap;
uniform mat4 solarShadowMatrix;
uniform float solarShadowBias;
uniform int solarShadowEnabled;
uniform int groundPass;
uniform vec2 textured;
uniform vec3 canvas;
flat in vec2 faceOpacity;
uniform int surfacePass;
uniform int styleTextureColor;
uniform int wireframe;
uniform int rasterExport;
uniform vec4 strokeColor;
in vec3 worldPosition;
uniform int clipEnabled;
uniform vec4 clipPlane;
uniform int stipple;
uniform float pixelRatio;
out vec4 fragment;
vec3 toLinear(vec3 c) {
  return mix(c/12.92,pow((c+0.055)/1.055,vec3(2.4)),greaterThan(c,vec3(0.04045)));
}
vec3 toSrgb(vec3 c) {
  return mix(c*12.92,1.055*pow(c,vec3(1.0/2.4))-0.055,greaterThan(c,vec3(0.0031308)));
}
// Explicit four-texel filtering guarantees sRGB decode before interpolation
// even on GL 3.3 implementations that filter sRGB textures in encoded space.
vec4 sampleImage(sampler2D source, vec2 coordinates) {
  ivec2 size=textureSize(source,0);
  bool reference=previewShading.w>0.5;
  vec2 p=(reference ? clamp(coordinates,vec2(0.0),vec2(1.0)) : fract(coordinates))*vec2(size)-0.5;
  ivec2 lo=ivec2(floor(p));
  vec2 f=fract(p);
  ivec2 a=(lo%size+size)%size;
  ivec2 b=(a+ivec2(1))%size;
  if(reference) { a=clamp(lo,ivec2(0),size-1); b=clamp(lo+1,ivec2(0),size-1); }
  return mix(mix(texelFetch(source,a,0),texelFetch(source,ivec2(b.x,a.y),0),f.x),
             mix(texelFetch(source,ivec2(a.x,b.y),0),texelFetch(source,b,0),f.x),f.y);
}
float sunlightVisibility() {
  vec3 p=(solarShadowMatrix*vec4(worldPosition,1.0)).xyz*0.5+0.5;
  if(any(lessThan(p,vec3(0.0))) || any(greaterThan(p,vec3(1.0)))) return 1.0;
  vec2 pixel=1.0/vec2(textureSize(solarShadowMap,0));
  float visible=0.0;
  for(int x=-1;x<=1;++x) for(int y=-1;y<=1;++y)
    visible += p.z-solarShadowBias <= texture(solarShadowMap,p.xy+vec2(x,y)*pixel).r ? 1.0 : 0.0;
  return visible/9.0;
}
void main() {
  if(clipEnabled!=0 && dot(clipPlane,vec4(worldPosition,1.0))<0.0) discard;
  if(stipple==1 && (mod(floor(gl_FragCoord.x/pixelRatio),4.0)>0.0 || mod(floor(gl_FragCoord.y/pixelRatio),4.0)>0.0)) discard;
  if(stipple==2 && mod(floor((gl_FragCoord.x+gl_FragCoord.y)/pixelRatio),8.0)>2.0) discard;
  bool reference=previewShading.w>0.5;
  if(surfacePass==4 && reference) discard;
  if((surfacePass==1 || surfacePass==2) && wireframe!=0 && !reference) discard;
  vec4 color=gl_FrontFacing ? tint : backTint;
  // Opacity is constant per face; interpolating 1 can round below 1 and discard opaque fragments.
  color.a=gl_FrontFacing ? faceOpacity.x : faceOpacity.y;
  bool hasImage=gl_FrontFacing ? textured.x>0.0 : textured.y>0.0;
  if(hasImage && surfacePass!=0) {
    vec4 imageColor=gl_FrontFacing ? sampleImage(frontImage,uv.xy) : sampleImage(backImage,uv.zw);
    color.a*=imageColor.a;
    if(surfacePass!=3 && (styleTextureColor!=0 || reference)) color.rgb=toSrgb(toLinear(color.rgb)*imageColor.rgb);
  }
  if(surfacePass==0 && strokeColor.a>0.0) color.rgb=strokeColor.rgb;
  float lighting=gl_FrontFacing ? previewShading.x : previewShading.z;
  if(!reference && solarShadowEnabled!=0 && (surfacePass==1 || surfacePass==2 || groundPass!=0))
    lighting=0.3+max(0.0,lighting-0.3)*sunlightVisibility();
  if(surfacePass!=3) color.rgb=mix(canvas,color.rgb,rasterExport!=0 ? 1.0 : previewShading.y)*lighting;
  if(surfacePass==4 && color.a<0.5) discard;
  if(surfacePass==1 && color.a<1.0) discard;
  if(surfacePass==2 && (color.a<=0.0 || color.a>=1.0)) discard;
  if(surfacePass==3 && color.a<=0.0) discard;
  fragment=surfacePass==3 ? vec4(color.rgb,1.0) : color;
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
    for (auto *batch : {&gridGpu_, &groundGpu_, &profilesGpu_, &transparentGpu_, &benchmarkGpu_, &pickFacesGpu_, &pickEdgesGpu_,
                        &selectedFacesGpu_, &selectedEdgesGpu_, &hoverFacesGpu_, &hoverEdgesGpu_,
                        &assistantTrianglesGpu_, &assistantLinesGpu_}) {
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
    if (measuredRasterMatrix_)
        return *measuredRasterMatrix_;
    QMatrix4x4 projection, view;
    float aspect = float(renderWidth()) / std::max(1, renderHeight());
    if (ortho_)
        projection.ortho(-distance_ * aspect * .45f, distance_ * aspect * .45f, -distance_ * .45f,
                         distance_ * .45f, .01f, std::max(1000.f, distance_ * 10));
    else
        projection.perspective(fov_, aspect, std::max(.001f, distance_ / 10000),
                               std::max(1000.f, distance_ * 10));
    QVector3D direction(std::cos(pitch_ * degreesToRadians) * std::cos(yaw_ * degreesToRadians),
                        std::cos(pitch_ * degreesToRadians) * std::sin(yaw_ * degreesToRadians),
                        std::sin(pitch_ * degreesToRadians));
    const auto up =
        std::abs(pitch_) > 89.999f ? QVector3D{0, pitch_ > 0 ? 1.f : -1.f, 0} : QVector3D{0, 0, 1};
    const auto target = qv(target_ - renderOrigin_);
    view.lookAt(target + direction * distance_, target, up);
    return projection * view;
}
QPointF Viewport::project(Vec3 p) const {
    auto v = matrix() * QVector4D(qv(p - renderOrigin_), 1);
    if (std::abs(v.w()) < 1e-9)
        return {};
    auto n = v.toVector3DAffine();
    return {(n.x() + 1) * renderWidth() / 2, (1 - n.y()) * renderHeight() / 2};
}
std::pair<Vec3, Vec3> Viewport::ray(QPointF p) const {
    auto inv = matrix().inverted();
    float x = 2 * p.x() / width() - 1, y = 1 - 2 * p.y() / height();
    auto a = (inv * QVector4D(x, y, -1, 1)).toVector3DAffine(),
         b = (inv * QVector4D(x, y, 1, 1)).toVector3DAffine();
    return {vec(a) + renderOrigin_, normalized(vec(b - a))};
}
InferenceCamera Viewport::inferenceCamera() const {
    InferenceCamera camera;
    const auto projection = matrix();
    const auto inverse = projection.inverted();
    for (int i = 0; i < 16; ++i) {
        camera.clipFromWorld[i] = projection.constData()[i];
        camera.worldFromClip[i] = inverse.constData()[i];
    }
    // Compose the world translations in double precision for public inference matrices.
    for (int row = 0; row < 4; ++row)
        camera.clipFromWorld[12 + row] -=
            double(projection.constData()[row]) * renderOrigin_.x +
            double(projection.constData()[4 + row]) * renderOrigin_.y +
            double(projection.constData()[8 + row]) * renderOrigin_.z;
    for (int col = 0; col < 4; ++col) {
        const auto w = double(inverse.constData()[col * 4 + 3]);
        camera.worldFromClip[col * 4] += renderOrigin_.x * w;
        camera.worldFromClip[col * 4 + 1] += renderOrigin_.y * w;
        camera.worldFromClip[col * 4 + 2] += renderOrigin_.z * w;
    }
    camera.width = width();
    camera.height = height();
    return camera;
}
std::optional<Vec3> Viewport::ground(QPointF point) {
    if (tool_ != Tool::Freehand) {
        acquireInference(point, true);
        if (heldPoint_)
            return heldPoint_->point;
        if (directionLocks_.current()) {
            const auto candidate = acquiredDirection();
            return candidate ? std::optional<Vec3>{candidate->point} : std::nullopt;
        }
        if (const auto candidate = acquiredInference())
            return candidate->point;
        if (const auto direction = acquiredDirection())
            return direction->point;
    }

    const auto [origin, direction] = ray(point);
    const auto denominator = dot(direction, plane_.normal);
    if (std::abs(denominator) < 1e-7)
        return {};
    const auto distance = dot(plane_.origin - origin, plane_.normal) / denominator;
    if (distance < 0)
        return {};
    auto local = plane_.coordinates(origin + direction * distance);
    if (tool_ != Tool::Freehand) {
        local.x = std::round(local.x * 10) / 10;
        local.y = std::round(local.y * 10) / 10;
    }
    const auto result = plane_.point(local.x, local.y);
    if (std::abs(result.x) > coordinateLimit || std::abs(result.y) > coordinateLimit ||
        std::abs(result.z) > coordinateLimit)
        return {};
    return result;
}
bool Viewport::drawingTool() const {
    return tool_ == Tool::Line || tool_ == Tool::Rectangle || tool_ == Tool::Circle ||
           tool_ == Tool::Polygon || tool_ == Tool::Freehand || tool_ == Tool::RotatedRectangle ||
           arcTool() || guideTool();
}
bool Viewport::arcTool() const {
    return tool_ == Tool::CenterArc || tool_ == Tool::TwoPointArc || tool_ == Tool::ThreePointArc ||
           tool_ == Tool::Pie;
}
bool Viewport::threePointTool() const {
    return tool_ == Tool::RotatedRectangle || arcTool() || tool_ == Tool::Protractor;
}
QString Viewport::nextPointHint() const {
    if (tool_ == Tool::Protractor)
        return "Choose the angle point or enter an angle (degrees)";
    if (tool_ == Tool::RotatedRectangle)
        return "Choose the height or enter width, height";
    if (tool_ == Tool::TwoPointArc)
        return "Choose the bulge point or enter a signed bulge";
    if (tool_ == Tool::ThreePointArc)
        return "Choose the arc endpoint";
    return "Choose the end direction or enter radius, angle (degrees)";
}
void Viewport::setDrawingPlane(std::optional<DrawingPlane> plane, Id context) {
    if (context && !doc_.bodies().contains(context))
        throw std::runtime_error("Drawing context does not exist");
    if (context && (!selection_.inContext(doc_, context) || selection_.locked(doc_, context)))
        throw std::runtime_error("Drawing context is inactive or locked");
    if (plane)
        *plane = DrawingPlane::make(plane->origin, plane->normal, plane->xAxis);
    cancel();
    configuredPlane_ = plane;
    configuredContext_ = plane ? context : 0;
    plane_ = plane.value_or(DrawingPlane{});
    drawingContext_ = configuredContext_ ? configuredContext_ : selection_.context();
    emit message(plane ? "Drawing plane locked" : "Drawing plane follows the first hovered face");
}
void Viewport::useSelectedFacePlane() {
    if (!selected_ || !selectedFace_)
        throw std::runtime_error("Select a face to use its plane");
    const auto &body = *doc_.bodies().at(selected_);
    const auto world = doc_.worldTransform(selected_);
    const auto &loop = body.surface.faces.at(selectedFace_).loops[0];
    const auto a = world.point(body.surface.vertices.at(loop[0]));
    const auto u =
        world.vector(body.surface.vertices.at(loop[1]) - body.surface.vertices.at(loop[0]));
    const auto n = body.surface.normal(selectedFace_);
    const auto localFrame =
        DrawingPlane::make({}, n, std::abs(n.x) < .8 ? Vec3{1, 0, 0} : Vec3{0, 1, 0});
    const auto normal = cross(world.vector(localFrame.xAxis), world.vector(localFrame.yAxis));
    setDrawingPlane(DrawingPlane::make(a, normal, u), selected_);
}
void Viewport::choosePlane(QPointF point) {
    if (heldPlane_) {
        plane_ = *heldPlane_;
        drawingContext_ = heldContext_;
        return;
    }
    if (configuredPlane_) {
        if (configuredContext_ && !doc_.bodies().contains(configuredContext_))
            throw std::runtime_error("Locked drawing context no longer exists");
        if (configuredContext_ && (!selection_.inContext(doc_, configuredContext_) ||
                                   selection_.locked(doc_, configuredContext_)))
            throw std::runtime_error(
                "Locked drawing plane belongs to an inactive or locked context");
        plane_ = *configuredPlane_;
        drawingContext_ = configuredContext_ ? configuredContext_ : selection_.context();
        return;
    }
    plane_ = DrawingPlane{};
    drawingContext_ = selection_.context();
    acquireInference(point, false);
    const auto candidate = acquiredInference();
    // The small-scene face fallback keeps drawing available during preparation.
    // A large document waits for its index instead of scanning all triangles.
    size_t faces = 0;
    if (!candidate && inferencePending_) {
        for (const auto &[id, body] : doc_.bodies())
            faces += body->surface.faces.size();
        if (faces > 5000)
            throw std::runtime_error(
                "Preparing inference; choose the point when indexing finishes");
    }
    const auto fallback = !candidate && inferencePending_ ? pick(point) : std::pair<Id, Id>{};
    const auto context = candidate ? candidate->body : fallback.first;
    if (!context || !selection_.inContext(doc_, context) || selection_.locked(doc_, context))
        return;
    const auto &body = *doc_.bodies().at(context);
    const auto world = doc_.worldTransform(context);
    Id face = fallback.second;
    if (candidate && candidate->kind == InferenceKind::OnFace)
        face = candidate->entity;
    else if (candidate) {
        const auto adjacency = body.topology.adjacency(body.surface);
        std::vector<Id> edges;
        if (candidate->entityType == InferenceEntity::Vertex) {
            if (adjacency.vertexEdges.contains(candidate->entity))
                edges = adjacency.vertexEdges.at(candidate->entity);
        } else if (candidate->kind == InferenceKind::Center &&
                   body.curves.contains(candidate->entity)) {
            for (auto edge : body.curves.at(candidate->entity).edges)
                edges.push_back(edge.edge);
        } else if (candidate->entityType == InferenceEntity::Edge &&
                   body.topology.edges.contains(candidate->entity))
            edges.push_back(candidate->entity);
        for (auto edge : edges) {
            const auto &incidence = adjacency.edgeFaces.at(edge);
            for (auto item : incidence)
                if (visible({context, SelectionKind::Face, item.face})) {
                    face = item.face;
                    break;
                }
            if (face)
                break;
        }
    }
    if (!face && candidate && candidate->entityType == InferenceEntity::Guide) {
        const auto hit = pick(point);
        if (hit.first == context && hit.second) {
            const auto &loop = body.surface.faces.at(hit.second).loops[0];
            const auto local = DrawingPlane::make(
                body.surface.vertices.at(loop[0]), body.surface.normal(hit.second),
                body.surface.vertices.at(loop[1]) - body.surface.vertices.at(loop[0]));
            const auto normal =
                normalized(cross(world.vector(local.xAxis), world.vector(local.yAxis)));
            if (std::abs(dot(candidate->point - world.point(local.origin), normal)) <= tolerance)
                face = hit.second;
        }
    }
    drawingContext_ = context;
    if (!face) {
        plane_ = DrawingPlane::make(candidate->point, {0, 0, 1}, {1, 0, 0});
        return;
    }
    const auto &loop = body.surface.faces.at(face).loops[0];
    const auto local =
        DrawingPlane::make(body.surface.vertices.at(loop[0]), body.surface.normal(face),
                           body.surface.vertices.at(loop[1]) - body.surface.vertices.at(loop[0]));
    plane_ = DrawingPlane::make(world.point(local.origin),
                                cross(world.vector(local.xAxis), world.vector(local.yAxis)),
                                world.vector(local.xAxis));
}
void Viewport::beginChain() {
    if (!chainPending_ || !committedEnd_)
        return;
    const auto point = *committedEnd_;
    clearPreview();
    session_.begin();
    anchor_ = point;
    cursor_ = point;
    drawingContext_ = chainContext_;
    plane_.origin = plane_.origin + plane_.normal * dot(point - plane_.origin, plane_.normal);
    chainPending_ = false;
}
Viewport::FaceHit Viewport::nearestFace(QPointF p) const {
    auto [o, d] = ray(p);
    FaceHit hit;
    std::vector<std::pair<Id, Triangle>> caps;
    auto rememberCaps = [&](Id body, const SectionMesh &mesh) {
        if (doc_.style().mode == ModelStyleMode::Wireframe)
            return;
        for (const auto &triangle : mesh.triangles)
            if (triangle.source == noSectionSource && doc_.sections().at(triangle.section)->fill)
                caps.push_back({body, {triangle.vertices[0].point, triangle.vertices[1].point,
                                      triangle.vertices[2].point, 0}});
    };
    auto intersect = [&](const Triangle &t, const Triangle &local, Id body) {
        if (!visible({body, t.face ? SelectionKind::Face : SelectionKind::Body, t.face}))
            return;
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
        if (distance > 0 && (nearer || tied) && !clipped(o + d * distance, body)) {
            const bool back = (a < 0) != (doc_.worldTransform(body).determinant() < 0);
            const auto &record = *doc_.bodies().at(body);
            if ((record.referenceImage
                     ? record.referenceImage->opacity
                     : surfaceAppearance(doc_.materials(), record, t.face, back).opacity) == 0)
                return;
            const auto projection = textureProjection(record, local, back);
            if (projection.image) {
                const auto &uv = projection.uv;
                TextureCoordinate coordinates{uv[0][0] * (1 - u - v) + uv[1][0] * u + uv[2][0] * v,
                                              uv[0][1] * (1 - u - v) + uv[1][1] * u + uv[2][1] * v};
                const auto &image = *textureImages_.at(projection.image);
                if (record.referenceImage) {
                    coordinates.u =
                        std::clamp(coordinates.u, .5 / image.width(), 1 - .5 / image.width());
                    coordinates.v =
                        std::clamp(coordinates.v, .5 / image.height(), 1 - .5 / image.height());
                }
                if (image.sampleLinear(coordinates)[3] == 0)
                    return;
            }
            hit = {body, t.face, distance};
        }
    };
    if (cacheDirty_ || cachedDocument_ != doc_.identity() || cachedRevision_ != doc_.revision()) {
        // Picking can occur before the queued repaint; inspect current geometry
        // without requiring a current OpenGL context.
        for (const auto &[id, body] : doc_.bodies()) {
            if (cachedDocument_ == doc_.identity() && opacity_.contains(id) && opacity_.at(id) == 0)
                continue;
            const auto world = doc_.worldTransform(id);
            const auto local = displayTriangles(*body);
            auto transformed = local;
            for (auto &triangle : transformed) {
                triangle.a = world.point(triangle.a);
                triangle.b = world.point(triangle.b);
                triangle.c = world.point(triangle.c);
            }
            const auto cuts = effectiveSectionCuts(doc_, id);
            if (!cuts.empty() && !body->referenceImage) {
                auto source = transformed;
                std::erase_if(source, [&](const auto &triangle) {
                    return !visible({id, SelectionKind::Face, triangle.face});
                });
                try {
                    rememberCaps(id, sectionMesh(source, cuts));
                } catch (const std::exception &) {
                    // The queued repaint reports the bounded-kernel failure. Do
                    // not select geometry which that view cannot display.
                    continue;
                }
            }
            for (size_t i = 0; i < transformed.size(); ++i)
                intersect(transformed[i], local[i], id);
        }
    } else
        for (const auto &[id, cache] : bodyCaches_) {
            if (cache->alpha == 0 || !cache->bounds.valid || !cache->sectionError.isEmpty())
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
            for (size_t i = 0; i < cache->worldTriangles.size(); ++i)
                intersect(cache->worldTriangles[i], cache->localTriangles[i], id);
            if (!cache->record->referenceImage)
                rememberCaps(id, cache->sectionMesh);
        }
    for (const auto &[body, triangle] : caps) {
        const auto e1 = triangle.b - triangle.a, e2 = triangle.c - triangle.a;
        const auto h = cross(d, e2);
        const auto determinant = dot(e1, h);
        if (std::abs(determinant) <= length(e1) * length(e2) * 1e-12)
            continue;
        const auto relative = o - triangle.a;
        const auto u = dot(relative, h) / determinant;
        const auto q = cross(relative, e1);
        const auto v = dot(d, q) / determinant;
        const auto distance = dot(e2, q) / determinant;
        if (u >= 0 && v >= 0 && u + v <= 1 && distance > 0 &&
            distance < hit.distance - tolerance && !clipped(o + d * distance, body))
            hit = {0, 0, distance}; // Cap occludes native face tools without inventing a face ID.
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
    if (const auto &image = doc_.bodies().at(id)->referenceImage) {
        for (const auto point : referenceImageCorners(*image, world)) {
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
    }
    return bounds;
}
std::pair<Id, Id> Viewport::pickEdge(QPointF point, double radius) const {
    if (!std::isfinite(radius) || radius < 0 || radius > 64)
        throw std::runtime_error("Edge pick radius must be between 0 and 64 logical pixels");
    std::pair<Id, Id> hit{};
    double best = radius * radius, nearest = INFINITY;
    std::map<Id, std::vector<SectionCut>> cutsByBody;
    const auto transform = matrix();
    auto candidate = [&](Id body, Id edge, Vec3 a, Vec3 b) {
        if (!selectable({body, SelectionKind::Edge, edge}))
            return;
        auto ca = transform * QVector4D(qv(a - renderOrigin_), 1),
             cb = transform * QVector4D(qv(b - renderOrigin_), 1);
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
        auto [cuts, inserted] = cutsByBody.try_emplace(body);
        if (inserted)
            cuts->second = effectiveSectionCuts(doc_, body);
        for (const auto &cut : cuts->second)
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
        ca = transform * QVector4D(qv(a - renderOrigin_), 1);
        cb = transform * QVector4D(qv(b - renderOrigin_), 1);
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
        if (face.distance < depth - std::max(1e-5, depth * 1e-5))
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
            if (cache->alpha == 0 || !cache->sectionError.isEmpty())
                continue;
            for (const auto &edge : cache->worldEdges)
                candidate(id, edge.id, edge.a, edge.b);
        }
    return hit;
}
void Viewport::rebuild() {
    syncSelection();
    pickDirty_ = overlayDirty_ = profilesDirty_ = true;
    if (selectionDocument_ != doc_.identity()) {
        cancel();
        configuredPlane_.reset();
        configuredContext_ = 0;
        drawingContext_ = 0;
        plane_ = DrawingPlane{};
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
    syncTextures();
    textureMappingFallbacks_ = 0;
    std::erase_if(opacity_, [&](const auto &item) { return !doc_.bodies().contains(item.first); });
    std::erase_if(bodyCaches_, [&](const auto &item) {
        if (doc_.bodies().contains(item.first))
            return false;
        if (!item.second->transparent.empty())
            transparentDirty_ = true;
        return true;
    });
    auto vertex = [](Vec3 p, std::array<float, 3> c) {
        return Vertex{p.x, p.y, p.z, c[0], c[1], c[2]};
    };
    if (gridDirty_) {
        std::vector<Vertex> grid;
        const auto &style = doc_.style();
        for (int i = -50; style.gridVisible && i <= 50; ++i) {
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
        if (style.axesVisible) {
            axis({0, 0, .002}, {8, 0, .002}, {.72f, .30f, .26f});
            axis({0, 0, .002}, {0, 8, .002}, {.29f, .52f, .35f});
            axis({0, 0, 0}, {0, 0, 5}, {.29f, .46f, .70f});
        }
        upload(gridGpu_, grid);
        gridDirty_ = false;
    }
    for (const auto &[id, body] : doc_.bodies()) {
        if (!bodyCaches_.contains(id))
            bodyCaches_.emplace(id, std::make_unique<BodyCache>());
        auto &cache = *bodyCaches_.at(id);
        const auto world = doc_.worldTransform(id);
        const float alpha = opacity_.contains(id) ? opacity_.at(id) : 1.f;

        const bool meshChanged =
            !cache.record ||
            (cache.record != body && (cache.record->surface != body->surface ||
                                      cache.record->referenceImage != body->referenceImage));
        const bool topologyChanged =
            meshChanged || !cache.record || cache.record->topology.edges != body->topology.edges;
        const bool worldChanged = meshChanged || !cache.record || cache.world != world;
        MaterialRecords materials;
        auto remember = [&](MaterialSides sides) {
            for (const auto material : {sides.front, sides.back})
                if (material)
                    materials.emplace(material, doc_.materials().at(material));
        };
        for (const auto &[face, record] : body->surface.faces)
            remember(faceMaterials(*body, face));
        std::map<Id, std::shared_ptr<const TextureImage>> images;
        for (const auto &[material, record] : materials)
            if (record->asset && textureImages_.contains(record->asset))
                images.emplace(record->asset, textureImages_.at(record->asset));
        if (body->referenceImage && textureImages_.contains(body->referenceImage->asset))
            images.emplace(body->referenceImage->asset,
                           textureImages_.at(body->referenceImage->asset));
        const auto cuts = effectiveSectionCuts(doc_, id);
        SectionRecords sections;
        for (const auto &cut : cuts)
            sections.emplace(cut.id, doc_.sections().at(cut.id));
        const bool appearanceChanged = cache.sectionCuts != cuts || cache.sections != sections ||
                                       worldChanged || topologyChanged || !cache.record ||
                                       cache.record->color != body->color ||
                                       cache.record->faceColors != body->faceColors ||
                                       cache.record->materials != body->materials ||
                                       cache.record->faceMaterials != body->faceMaterials ||
                                       cache.record->faceTextureMappings != body->faceTextureMappings ||
                                       cache.images != images ||
                                       cache.record->edgeAppearances != body->edgeAppearances ||
                                       cache.materials != materials || cache.alpha != alpha ||
                                       cache.presentationRevision != presentationRevision_;
        if (meshChanged) {
            cache.localTriangles = displayTriangles(*body);
            ++stats_.bodyMeshBuilds;
        }
        if (topologyChanged) {
            cache.adjacency = body->topology.adjacency(body->surface);
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
            cache.sectionCuts = cuts;
            cache.sections = std::move(sections);
            prepareSections(id, cache);
            transparentDirty_ = true;
            cache.mappingFallbacks = 0;
            cache.opaque.clear();
            cache.lines.clear();
            cache.hiddenLines.clear();
            cache.transparent.clear();
            if (alpha > 0) {
                const ShadingNormals shading(*body);
                const auto inverse = world.inverse();
                auto worldNormal = [&](Vec3 n) {
                    const Vec3 transformed{
                        inverse.m[0] * n.x + inverse.m[1] * n.y + inverse.m[2] * n.z,
                        inverse.m[4] * n.x + inverse.m[5] * n.y + inverse.m[6] * n.z,
                        inverse.m[8] * n.x + inverse.m[9] * n.y + inverse.m[10] * n.z};
                    return transformed * (1 / length(transformed));
                };
                auto appendTriangle = [&](size_t triangleIndex, const SectionTriangle *cut) {
                    const auto &triangle = cache.worldTriangles[triangleIndex];
                    const SelectedEntity entity{
                        id, body->referenceImage ? SelectionKind::Body : SelectionKind::Face,
                        triangle.face};
                    if (!visible(entity))
                        return;
                    if (body->referenceImage) {
                        std::array<Vertex, 3> vertices;
                        size_t corner = 0;
                        for (auto point : {triangle.a, triangle.b, triangle.c})
                            vertices[corner++] = vertex(point, {1, 1, 1});
                        referenceVertices(*body, cache.localTriangles[triangleIndex], vertices);
                        for (auto &v : vertices) {
                            v.a *= alpha;
                            v.ba = v.a;
                            if (selection_.hidden(doc_, entity))
                                v.a = v.ba = std::min(v.a, .18f);
                            v.dim = !selection_.inActiveHierarchy(doc_, id) ||
                                            selection_.locked(doc_, id)
                                        ? .35f
                                        : 1.f;
                        }
                        if (cut)
                            vertices = clippedVertices(vertices, *cut);
                        const auto &v = vertices[0];
                        if ((v.image && textureImages_.at(v.image)->hasTransparency()) ||
                            (v.a > 0 && v.a < 1))
                            cache.transparent.push_back(vertices);
                        if (v.a == 1)
                            cache.opaque.insert(cache.opaque.end(), vertices.begin(),
                                                vertices.end());
                        return;
                    }
                    const auto crossProduct =
                        cross(triangle.b - triangle.a, triangle.c - triangle.a);
                    const auto magnitude = length(crossProduct);
                    if (magnitude == 0)
                        return;
                    const auto normals = shading.triangle(cache.localTriangles[triangleIndex]);
                    auto front = surfaceAppearance(doc_.materials(), *body, triangle.face);
                    auto back = surfaceAppearance(doc_.materials(), *body, triangle.face, true);
                    if (doc_.style().mode == ModelStyleMode::Monochrome) {
                        front.color = doc_.style().front;
                        back.color = doc_.style().back;
                    }
                    // A reflected placement preserves the physical front of a face.
                    if (world.determinant() < 0)
                        std::swap(front, back);
                    for (auto *side : {&front, &back}) {
                        side->opacity *= alpha;
                        if (doc_.style().mode == ModelStyleMode::XRay)
                            side->opacity *= float(doc_.style().xrayOpacity);
                        if (selection_.hidden(doc_, entity))
                            side->opacity = std::min(side->opacity, .18f);
                    }
                    std::array<Vertex, 3> vertices;
                    size_t index = 0;
                    for (auto point : {triangle.a, triangle.b, triangle.c}) {
                        const float light = .64f + .36f * std::abs(dot(worldNormal(normals[index]),
                                                                       normalized({.3, -.5, .8})));
                        auto &v = vertices[index++];
                        auto color = front.color;
                        v = vertex(point, color);
                        v.a = front.opacity;
                        v.br = back.color[0];
                        v.bg = back.color[1];
                        v.bb = back.color[2];
                        v.ba = back.opacity;
                        if (doc_.solar().enabled) {
                            const auto normal = worldNormal(normals[index - 1]) * (world.determinant() < 0 ? -1 : 1);
                            v.light = solarLight(normal);
                            v.backLight = solarLight(normal * -1);
                        } else v.light = v.backLight = light;
                        v.dim = !selection_.inActiveHierarchy(doc_, id) || selection_.locked(doc_, id)
                                    ? .35f : 1.f;
                    }
                    textureVertices(*body, cache.localTriangles[triangleIndex],
                                    world.determinant() < 0, vertices);
                    if (cut)
                        vertices = clippedVertices(vertices, *cut);
                    const auto &first = vertices[0];
                    bool imageTransparency = false;
                    for (const auto side : {std::pair{front.material, first.image},
                                            std::pair{back.material, first.backImage}}) {
                        if (side.second)
                            imageTransparency |= textureImages_.at(side.second)->hasTransparency();
                        else if (side.first && images.contains(doc_.materials().at(side.first)->asset))
                            ++cache.mappingFallbacks;
                    }
                    // Mixed sides enter both passes; the shader discards the other side.
                    // Only the visible opaque side writes depth.
                    if (imageTransparency || (front.opacity > 0 && front.opacity < 1) ||
                        (back.opacity > 0 && back.opacity < 1))
                        cache.transparent.push_back(vertices);
                    if (front.opacity == 1 || back.opacity == 1)
                        cache.opaque.insert(cache.opaque.end(), vertices.begin(), vertices.end());
                };
                if (cache.sectionCuts.empty()) {
                    for (size_t i = 0; i < cache.worldTriangles.size(); ++i)
                        appendTriangle(i, nullptr);
                } else if (cache.sectionError.isEmpty()) {
                    for (const auto &triangle : cache.sectionMesh.triangles) {
                        if (triangle.source != noSectionSource) {
                            appendTriangle(triangle.source, &triangle);
                        } else if (cache.sections.at(triangle.section)->fill) {
                            const auto vertices = sectionCapVertices(id, triangle, alpha);
                            if (vertices[0].a == 1)
                                cache.opaque.insert(cache.opaque.end(), vertices.begin(), vertices.end());
                            else if (vertices[0].a > 0)
                                cache.transparent.push_back(vertices);
                        }
                    }
                    for (const auto &edge : cache.sectionMesh.edges)
                        if (cache.sections.at(edge.section)->edges) {
                            cache.lines.push_back(vertex(edge.a, doc_.style().edge));
                            cache.lines.push_back(vertex(edge.b, doc_.style().edge));
                        }
                }
                for (const auto &edge : cache.worldEdges) {
                    const SelectedEntity entity{id, SelectionKind::Edge, edge.id};
                    if (!visible(entity))
                        continue;
                    const std::array<float, 3> color =
                        selection_.hidden(doc_, entity) || !selection_.inActiveHierarchy(doc_, id)
                            ? std::array<float, 3>{.56f, .58f, .60f}
                            : doc_.style().edge;
                    auto &lines = selection_.hidden(doc_, entity) ? cache.hiddenLines : cache.lines;
                    if (!cache.sectionError.isEmpty())
                        continue;
                    if (const auto segment = sectionSegment(edge.a, edge.b, cache.sectionCuts)) {
                        lines.push_back(vertex((*segment)[0], color));
                        lines.push_back(vertex((*segment)[1], color));
                    }
                }
            }
        }
        if (appearanceChanged || cache.opaqueGpu.origin != renderOrigin_ ||
            !cache.opaqueGpu.buffer.isCreated() ||
            !cache.linesGpu.buffer.isCreated() || !cache.hiddenLinesGpu.buffer.isCreated()) {
            upload(cache.opaqueGpu, cache.opaque);
            upload(cache.linesGpu, cache.lines);
            upload(cache.hiddenLinesGpu, cache.hiddenLines);
            ++stats_.bodyUploads;
        }
        cache.record = body;
        cache.materials = std::move(materials);
        cache.images = std::move(images);
        textureMappingFallbacks_ += cache.mappingFallbacks;
        cache.world = world;
        cache.alpha = alpha;
        cache.presentationRevision = presentationRevision_;
        if (alpha > 0)
            stats_.meshTriangles += cache.worldTriangles.size();
        transparent_.insert(transparent_.end(), cache.transparent.begin(), cache.transparent.end());
    }
    stats_.cachedBodies = bodyCaches_.size();

    cachedRevision_ = doc_.revision();
    cacheDirty_ = false;
}
void Viewport::upload(GpuBatch &batch, const std::vector<Vertex> &vertices, bool transparent) {
    if (vertices.size() > size_t(std::numeric_limits<int>::max()) / sizeof(PackedVertex))
        throw std::runtime_error("Viewport buffer exceeds the supported size");
    if (!batch.buffer.isCreated()) {
        if (!batch.buffer.create())
            throw std::runtime_error("Could not create viewport buffer");
        batch.buffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
    }
    std::vector<PackedVertex> packed;
    packed.reserve(vertices.size());
    batch.runs.clear();
    for (const auto &v : vertices) {
        if (batch.runs.empty() || batch.runs.back().front != v.image ||
            batch.runs.back().back != v.backImage)
            batch.runs.push_back({int(packed.size()), 0, v.image, v.backImage});
        ++batch.runs.back().count;
        packed.push_back({float(v.x - renderOrigin_.x), float(v.y - renderOrigin_.y),
                          float(v.z - renderOrigin_.z), v.r, v.g, v.b, v.a, v.br, v.bg, v.bb, v.ba,
                          v.u, v.v, v.bu, v.bv, v.light, v.dim, v.backLight, v.reference});
    }
    batch.buffer.bind();
    batch.buffer.allocate(packed.data(), int(packed.size() * sizeof(PackedVertex)));
    batch.origin = renderOrigin_;
    batch.buffer.release();
    batch.count = int(vertices.size());
    stats_.uploadedBytes += vertices.size() * sizeof(PackedVertex);
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
    gl_->glEnableVertexAttribArray(0);
    gl_->glEnableVertexAttribArray(1);
    gl_->glEnableVertexAttribArray(2);
    gl_->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(PackedVertex), nullptr);
    gl_->glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(PackedVertex),
                          reinterpret_cast<void *>(3 * sizeof(float)));
    gl_->glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(PackedVertex),
                          reinterpret_cast<void *>(7 * sizeof(float)));
    gl_->glEnableVertexAttribArray(3);
    gl_->glEnableVertexAttribArray(4);
    gl_->glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(PackedVertex),
                          reinterpret_cast<void *>(11 * sizeof(float)));
    gl_->glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(PackedVertex),
                               reinterpret_cast<void *>(15 * sizeof(float)));
    shader_->setUniformValue("rasterExport", rasterSize_.isEmpty() ? 0 : 1);
    shader_->setUniformValue("wireframe", doc_.style().mode == ModelStyleMode::Wireframe ? 1 : 0);
    shader_->setUniformValue("frontImage", 0);
    shader_->setUniformValue("backImage", 1);
    shader_->setUniformValue("canvas", QVector3D(colors_.canvas.redF(), colors_.canvas.greenF(),
                                               colors_.canvas.blueF()));
    for (const auto &run : batch.runs) {
        const auto front = textureGpu_.contains(run.front) ? textureGpu_.at(run.front) : 0;
        const auto back = textureGpu_.contains(run.back) ? textureGpu_.at(run.back) : 0;
        gl_->glActiveTexture(GL_TEXTURE0);
        gl_->glBindTexture(GL_TEXTURE_2D, front);
        gl_->glActiveTexture(GL_TEXTURE1);
        gl_->glBindTexture(GL_TEXTURE_2D, back);
        shader_->setUniformValue("textured", QVector2D(front ? 1 : 0, back ? 1 : 0));
        if (count > 1)
            gl_->glDrawArraysInstanced(mode, run.first, run.count, count);
        else
            gl_->glDrawArrays(mode, run.first, run.count);
    }
    gl_->glBindTexture(GL_TEXTURE_2D, 0);
    gl_->glActiveTexture(GL_TEXTURE0);
    gl_->glBindTexture(GL_TEXTURE_2D, 0);
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
        const Vec3 center{(t[0].x + t[1].x + t[2].x) / 3,
                          (t[0].y + t[1].y + t[2].y) / 3,
                          (t[0].z + t[1].z + t[2].z) / 3};
        return dot(center - target_, vec(direction));
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
void Viewport::paintScene(QPaintDevice *device) {
    if (!ready_)
        return;
    syncModelStyle();
    syncSolar();
    // Decoding can publish between the polling timer and an explicit fresh frame.
    // Consume that generation before rendering surfaces, shadows or captures.
    if (textureCache_.snapshot() != textureSnapshot_)
        cacheDirty_ = pickDirty_ = true;
    QPainter p(device ? device : this);
    p.beginNativePainting();
    QElapsedTimer timer;
    timer.start();
    gl_->glClearColor(colors_.canvas.redF(), colors_.canvas.greenF(), colors_.canvas.blueF(), 1);
    gl_->glDepthMask(GL_TRUE);
    gl_->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gl_->glEnable(GL_DEPTH_TEST);
    gl_->glDepthFunc(GL_LEQUAL);
    gl_->glDisable(GL_BLEND);
    shader_->bind();
    auto transform = matrix();
    shader_->setUniformValue("mvp", transform);
    shader_->setUniformValue("instanced", instances_ > 0 ? 1 : 0);
    shader_->setUniformValue("stipple", 0);
    shader_->setUniformValue("pixelRatio", float(renderPixelRatio()));
    shader_->setUniformValue("surfacePass", 0);
    shader_->setUniformValue("solarShadowEnabled", 0);
    shader_->setUniformValue("groundPass", 0);
    shader_->setUniformValue("styleTextureColor", 1);
    shader_->setUniformValue("screenStroke", 0);
    shader_->setUniformValue("strokeColor", QVector4D{});
    shader_->setUniformValue("pixelOffset", QVector2D{});
    shader_->setUniformValue("clipEnabled", clipPlane_ ? 1 : 0);
    if (clipPlane_) {
        auto c = *clipPlane_;
        c[3] += c[0] * renderOrigin_.x + c[1] * renderOrigin_.y + c[2] * renderOrigin_.z;
        shader_->setUniformValue("clipPlane", QVector4D(c[0], c[1], c[2], c[3]));
    }
    if (benchmarkTriangles_ > 0) {
        if (benchmarkDirty_) {
            upload(benchmarkGpu_, benchmarkVertices_);
            benchmarkDirty_ = false;
        }
        draw(benchmarkGpu_, GL_TRIANGLES, std::max(1, instances_));
        gl_->glFinish();
    } else {
        if (cacheDirty_ || cachedRevision_ != doc_.revision() || cachedDocument_ != doc_.identity())
            rebuild();
        sortTransparent(transform);
        drawSolarShadowMap(transform);
        drawStyleGround(transform);
        // Reference grid does not write depth or shine through coplanar opaque faces.
        gl_->glDepthMask(GL_FALSE);
        draw(gridGpu_, GL_LINES);
        gl_->glDepthMask(GL_TRUE);
        {
            shader_->setUniformValue("styleTextureColor",
                                     doc_.style().mode == ModelStyleMode::Textured ? 1 : 0);
            gl_->glEnable(GL_POLYGON_OFFSET_FILL);
            gl_->glPolygonOffset(1, 1);
            shader_->setUniformValue("surfacePass", 1);
            for (auto &[id, cache] : bodyCaches_)
                draw(cache->opaqueGpu, GL_TRIANGLES);
            sortTransparent(transform);
            if (transparentGpu_.count) {
                gl_->glEnable(GL_BLEND);
                // Preserve opaque framebuffer alpha for Qt's premultiplied composition.
                gl_->glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                                    GL_ONE_MINUS_SRC_ALPHA);
                gl_->glDepthMask(GL_FALSE);
                shader_->setUniformValue("surfacePass", 2);
                draw(transparentGpu_, GL_TRIANGLES);
                gl_->glDepthMask(GL_TRUE);
                gl_->glDisable(GL_BLEND);
            }
            gl_->glDisable(GL_POLYGON_OFFSET_FILL);
        }
        shader_->setUniformValue("styleTextureColor", 1);
        shader_->setUniformValue("surfacePass", 0);
        if (doc_.style().edgesVisible || doc_.style().mode == ModelStyleMode::Wireframe) {
            for (auto &[id, cache] : bodyCaches_)
                draw(cache->linesGpu, GL_LINES);
            shader_->setUniformValue("stipple", 2);
            for (auto &[id, cache] : bodyCaches_)
                draw(cache->hiddenLinesGpu, GL_LINES);
            shader_->setUniformValue("stipple", 0);
        }
        drawStyleProfiles(transform);
        if (rasterSize_.isEmpty()) {
            drawSelectionOverlay();
            drawAssistantPreview();
        }
    }
    shader_->release();
    gl_->glDisable(GL_DEPTH_TEST);
    if (auto error = gl_->glGetError(); error != GL_NO_ERROR) {
        stats_.glError = error;
        if (!rasterSize_.isEmpty())
            throw std::runtime_error("Graphics error while rendering export");
    }
    ++stats_.frames;
    frameMs_ = timer.nsecsElapsed() / 1e6;
    p.endNativePainting();
    p.setRenderHint(QPainter::Antialiasing);
    paintAnnotations(p);
    if (!rasterSize_.isEmpty())
        return;
    paintGuides(p);
    paintSelection(p);
    paintAssistantPreview(p);
    if (hasFocus()) {
        p.setPen(QPen(colors_.accent, 2));
        p.setBrush(Qt::NoBrush);
        p.drawRect(rect().adjusted(1, 1, -1, -1));
    }
    p.setPen(colors_.ink);
    p.drawText(20, 28,
               (ortho_ ? "ORTHOGRAPHIC  /  " : "PERSPECTIVE  /  ") +
                   unitName(doc_.displayUnits()).toUpper());
    p.setPen(colors_.muted);
    const auto textures = textureSummary();
    if (!textures.isEmpty())
        p.drawText(QRect(20, 38, width() - 40, 40), Qt::TextWordWrap, textures);
    const int hudBottom = height() - (sceneTabs_->isVisible() ? sceneTabs_->height() + 12 : 0);
    p.drawText(20, hudBottom - 22,
               "Z up   ·   Inference 8 px   ·   Grid fallback " +
                   displayLength(.1, doc_.displayUnits()));
    if (transformTool())
        p.drawText(QRect(20, hudBottom - 66, width() - 40, 38), Qt::TextWordWrap,
                   QString("%1 axes · Ctrl: copy %2 · Choose pivot, then destination · Esc: cancel")
                       .arg(transformLocal_ ? "Local" : "World")
                       .arg(transformCopy_ ? "on" : "off"));
    if (tool_ == Tool::Extrude)
        p.drawText(QRect(20, hudBottom - 66, width() - 40, 38), Qt::TextWordWrap,
                   QString("Ctrl: create new face %1 · Double-click: repeat distance · Alt-drag: "
                           "orbit · Esc: cancel")
                       .arg(pushNewFace_ ? "on" : "off"));
    if (tool_ == Tool::Offset)
        p.drawText(QRect(20, hudBottom - 66, width() - 40, 38), Qt::TextWordWrap,
                   "Offset: choose a face · Move across its nearest edge · Positive: outward · "
                   "Negative: inward · Esc: cancel");
    if (tool_ == Tool::Sweep)
        p.drawText(QRect(20, hudBottom - 66, width() - 40, 38), Qt::TextWordWrap,
                   "Follow Me: select a profile face and path edges, then Shift+F · Enter or click "
                   "applies · Esc cancels · Alt-drag orbits");
    if (tool_ == Tool::HostedPlacement)
        p.drawText(QRect(20, hudBottom - 88, width() - 40, 62), Qt::TextWordWrap,
                   hostedPlacementSummary());
    if (tool_ == Tool::Orientation)
        p.drawText(QRect(20, hudBottom - 88, width() - 40, 62), Qt::TextWordWrap,
                   orientationSummary() + " · Enter applies · Esc cancels");
    if (tool_ == Tool::Boolean)
        p.drawText(QRect(20, hudBottom - 88, width() - 40, 62), Qt::TextWordWrap,
                   booleanSummary() + " · Enter applies · Esc cancels");
    if (tool_ == Tool::Intersect)
        p.drawText(QRect(20, hudBottom - 66, width() - 40, 38), Qt::TextWordWrap,
                   "Intersect · References: " + intersectionMode_ +
                       " · Enter or click applies · Esc cancels · Choose reference scope in Draw");
    if (tool_ == Tool::Orbit || tool_ == Tool::Pan || tool_ == Tool::Zoom)
        p.drawText(QRect(20, hudBottom - 66, width() - 40, 38), Qt::TextWordWrap,
                   trackpad_ ? "Two fingers: pan · Alt-scroll: orbit · Ctrl-scroll / pinch: zoom"
                             : "Drag: active camera tool · Shift/right drag: pan · Wheel: zoom");
    if (tool_ == Tool::Walk || tool_ == Tool::LookAround)
        p.drawText(QRect(20, hudBottom - 88, width() - 40, 62), Qt::TextWordWrap,
                   tool_ == Tool::Walk
                       ? "Walk: WASD/arrows · Q/E: lower/raise · Shift: faster · Drag: look · "
                         "Wheel: speed · Esc: select"
                       : "Look around: drag or arrow keys · Wheel: field of view · Esc: select");
    if (drawingTool() && tool_ != Tool::Freehand)
        p.drawText(QRect(20, hudBottom - 66, width() - 40, 38), Qt::TextWordWrap,
                   "Shift: hold inference · Arrows: axis / edge lock · Tab: alternatives · Hover: "
                   "arm reference");
    if (guideTool()) {
        p.setPen(colors_.muted);
        p.drawText(20, 88,
                   createGuides_ ? "Create guides · Ctrl: measure only"
                                 : "Measure only · Ctrl: create guides");
    }
    if (inferencePending_) {
        p.setPen(colors_.muted);
        p.drawText(20, 48, "Preparing inference · grid fallback available");
    }
    if (session_.active()) {
        p.setPen(QPen(previewValid_ ? QColor("#b9762f") : QColor("#bc4343"), 2, Qt::DashLine));
        for (const auto &edge : previewEdges_)
            p.drawLine(project(edge[0]), project(edge[1]));
        for (const auto &guide : previewGuides_)
            paintGuide(p, guide);
        if (tool_ == Tool::Orientation && previewValid_) {
            p.setPen(QPen(QColor("#b9762f"), 3));
            p.setBrush(QColor("#b9762f"));
            for (const auto &arrow : orientationNormals_) {
                const auto a = project(arrow[0]), b = project(arrow[1]);
                const auto delta = b - a;
                const auto size = std::hypot(delta.x(), delta.y());
                if (size < 4) {
                    p.drawEllipse(a, 5, 5);
                    continue;
                }
                const auto along = delta / size;
                const QPointF side(-along.y(), along.x());
                p.drawLine(a, b);
                p.drawPolygon(QPolygonF{b, b - along * 10 + side * 5, b - along * 10 - side * 5});
            }
        }
        if (anchor_) {
            p.setBrush(colors_.canvas);
            p.drawEllipse(project(*anchor_), 4, 4);
            if (cursor_ && previewEdges_.empty())
                p.drawLine(project(*anchor_), project(*cursor_));
        }
        if (!previewError_.isEmpty()) {
            const auto boxWidth = std::min(360, width() - 24);
            const QRectF box(
                std::clamp(previous_.x() + 12, 12.0, double(width() - boxWidth - 12)),
                std::clamp(previous_.y() + 12, 40.0, double(std::max(40, height() - 92))), boxWidth,
                72);
            p.fillRect(box, colors_.surface);
            p.setPen(colors_.ink);
            p.drawText(box.adjusted(6, 4, -6, -4), Qt::TextWordWrap, previewError_);
        }
    }
    if (reference_ && drawingTool()) {
        const auto pos = project(reference_->point);
        p.setPen(QPen(colors_.accent, 1, Qt::DotLine));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(pos, 9, 9);
        if (cursor_)
            p.drawLine(pos, project(*cursor_));
    }
    if (const auto direction = acquiredDirection(); direction && drawingTool()) {
        const auto &constraint = direction->constraint;
        QColor color = colors_.accent;
        if (constraint.kind == DirectionKind::RedAxis)
            color = QColor("#d84848");
        if (constraint.kind == DirectionKind::GreenAxis)
            color = QColor("#298c50");
        if (constraint.kind == DirectionKind::BlueAxis)
            color = QColor("#427ddd");
        p.setPen(QPen(color, directionLocks_.current() ? 3 : 2, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawLine(project(constraint.origin), project(direction->point));
        p.drawEllipse(project(direction->point), 5, 5);
        auto label =
            QString(directionLocks_.current() ? "Locked · " : "") + directionLabel(constraint.kind);
        if (!directionLocks_.current() && inferenceCount() > 1)
            label += QString(" · Tab %1/%2").arg(inferenceChoice_ + 1).arg(inferenceCount());
        if (constraint.body && doc_.bodies().contains(constraint.body)) {
            const auto &body = *doc_.bodies().at(constraint.body);
            const auto world = doc_.worldTransform(constraint.body);
            p.setPen(QPen(color, 3));
            auto highlight = [&](Id id) {
                if (!body.topology.edges.contains(id))
                    return;
                const auto &edge = body.topology.edges.at(id);
                p.drawLine(project(world.point(body.surface.vertices.at(edge.a))),
                           project(world.point(body.surface.vertices.at(edge.b))));
            };
            if (constraint.entityType == InferenceEntity::Guide &&
                body.guides.contains(constraint.entity)) {
                const auto &guide = body.guides.at(constraint.entity);
                if (guide.kind == GuideKind::Line)
                    paintGuide(p,
                               guideLine(world.point(guide.origin), world.vector(guide.direction)),
                               constraint.body);
            } else if (constraint.entityType == InferenceEntity::Edge)
                highlight(constraint.entity);
            else if (constraint.kind == DirectionKind::Tangent &&
                     body.curves.contains(constraint.entity))
                for (const auto &association : body.curves.at(constraint.entity).edges)
                    highlight(association.edge);
        }
        const auto pos = project(direction->point);
        const auto textWidth =
            std::min(p.fontMetrics().horizontalAdvance(label) + 30, std::max(1, width() - 24));
        const QRectF box(
            std::clamp(pos.x() + 12, 12., double(std::max(12, width() - textWidth - 12))),
            std::clamp(pos.y() - 34, 40., double(std::max(40, height() - 94))), textWidth, 26);
        p.fillRect(box, colors_.surface);
        p.setPen(QPen(color, 1.5));
        if (directionLocks_.current()) {
            const auto icon = box.topLeft() + QPointF(5, 10);
            p.drawRoundedRect(QRectF(icon, QSizeF(9, 8)), 1, 1);
            p.drawArc(QRectF(icon + QPointF(1, -6), QSizeF(7, 10)), 0, 180 * 16);
        }
        p.drawText(box.adjusted(19, 0, -4, 0), Qt::AlignVCenter, label);
    } else if (directionLocks_.current()) {
        p.setPen(colors_.accent);
        p.drawText(20, 68, QString("Locked · ") + directionLabel(directionLocks_.current()->kind));
    } else if (heldPlane_) {
        p.setPen(colors_.accent);
        p.drawText(20, 68, "Locked drawing plane");
    }
    if (const auto candidate = acquiredInference(); candidate && drawingTool()) {
        const auto pos = project(candidate->point);
        p.setPen(QPen(colors_.accent, 2));
        p.setBrush(colors_.accent);
        switch (candidate->kind) {
        case InferenceKind::GuidePoint:
        case InferenceKind::Endpoint:
            p.drawEllipse(pos, 4, 4);
            break;
        case InferenceKind::Midpoint:
            p.drawPolygon(
                QPolygonF{pos + QPointF(0, -5), pos + QPointF(5, 4), pos + QPointF(-5, 4)});
            break;
        case InferenceKind::Center:
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(pos, 6, 6);
            p.setBrush(colors_.accent);
            p.drawEllipse(pos, 1.5, 1.5);
            break;
        case InferenceKind::OnGuide:
        case InferenceKind::OnEdge:
            p.setBrush(Qt::NoBrush);
            p.drawRect(QRectF(pos - QPointF(4, 4), QSizeF(8, 8)));
            break;
        case InferenceKind::OnFace:
            p.setBrush(Qt::NoBrush);
            p.drawPolygon(QPolygonF{pos + QPointF(0, -5), pos + QPointF(5, 0), pos + QPointF(0, 5),
                                    pos + QPointF(-5, 0)});
            break;
        case InferenceKind::Intersection:
            p.drawLine(pos + QPointF(-5, -5), pos + QPointF(5, 5));
            p.drawLine(pos + QPointF(-5, 5), pos + QPointF(5, -5));
            break;
        }
        auto label = QString(heldPoint_ ? "Locked · " : "") + inferenceLabel(candidate->kind);
        if (inferenceCount() > 1)
            label += QString(" · Tab %1/%2%3")
                         .arg(inferenceChoice_ + 1)
                         .arg(inferenceCount())
                         .arg(inference_.truncated ? "+" : "");
        const auto extent = p.fontMetrics().boundingRect(label).adjusted(-6, -4, 6, 4);
        const auto labelWidth = std::min(extent.width(), std::max(1, width() - 24));
        const auto box =
            QRectF(std::clamp(pos.x() + 12, 12., double(std::max(12, width() - labelWidth - 12))),
                   std::clamp(pos.y() - 30, 40., double(std::max(40, height() - 50))), labelWidth,
                   extent.height());
        p.fillRect(box, colors_.surface);
        p.setPen(colors_.ink);
        p.drawText(box, Qt::AlignCenter, label);
    }
}
void Viewport::resizeEvent(QResizeEvent *event) {
    QOpenGLWidget::resizeEvent(event);
    layoutSceneTabs();
}
void Viewport::refresh() {
    syncSceneTabs();
    syncSelection();
    inferenceWorker_.request(doc_);
    inference_ = {};
    directions_.clear();
    hoverReference_.reset();
    if (referenceStamp_ && !doc_.isCurrentSnapshot(*referenceStamp_))
        clearConstraints();
    inferenceChoice_ = 0;
    if (session_.active() && !session_.current()) {
        cancel();
        emit message("Document changed; the uncommitted operation was canceled");
    }
    if (selectionDocument_ != doc_.identity()) {
        cancel();
        configuredPlane_.reset();
        configuredContext_ = 0;
        drawingContext_ = 0;
        plane_ = DrawingPlane{};
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
    selectEntities(
        body ? SelectionSet{{body, face ? SelectionKind::Face : SelectionKind::Body, face}}
             : SelectionSet{});
}

void Viewport::setTool(Tool tool) {
    cancel();
    tool_ = tool;
    if (tool == Tool::Walk || tool == Tool::LookAround)
        setOrthographic(false);
    if (tool == Tool::Circle)
        curveSegments_ = std::max(3u, curveSegments_);
    if (tool == Tool::Pie)
        curveSegments_ = std::max(2u, curveSegments_);
    emit toolChanged(int(tool));
    setCursor(tool == Tool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
    if (tool == Tool::HostedPlacement) {
        try {
            beginHostedPlacement();
        } catch (const std::exception &error) {
            emit message(error.what());
        }
    }
    if (tool == Tool::Sweep || tool == Tool::Intersect || tool == Tool::Boolean ||
        tool == Tool::Orientation) {
        try {
            if (tool == Tool::Sweep)
                beginSweep();
            else if (tool == Tool::Intersect)
                beginIntersection();
            else if (tool == Tool::Boolean)
                beginBoolean();
            else
                beginOrientation();
        } catch (const std::exception &error) {
            emit message(error.what());
        }
    }
}
void Viewport::clearPreview() {
    anchor_.reset();
    cursor_.reset();
    previewEdges_.clear();
    previewGuides_.clear();
    previewGuide_.reset();
    previewError_.clear();
    previewValid_ = false;
    baseline_.reset();
    samples_.clear();
    toolPressed_ = false;
    dragCommit_ = false;
    update();
}
void Viewport::cancel() {
    stopWalking();
    stopSceneTransition();
    hostedCommand_.reset();
    orientationCommand_.reset();
    orientationNormals_.clear();
    booleanCommand_.reset();
    sweepCommand_.reset();
    intersectionCommand_.reset();
    selectionPressed_ = selectingBox_ = false;
    hover_.reset();
    overlayDirty_ = true;
    clickCount_ = 0;
    session_.cancel();
    transformTargets_ = {};
    transformSelection_.clear();
    transformBase_.reset();
    transformEnd_.reset();
    transformPreview_ = {};
    transformArray_ = {};
    transformControlPending_ = false;
    guideControlPending_ = false;
    pushControlPending_ = false;
    clearConstraints();
    inference_ = {};
    inferenceChoice_ = 0;
    chainPending_ = false;
    chainContext_ = 0;
    tapeReference_.reset();
    committedBaseline_.reset();
    committedAnchor_.reset();
    committedEnd_.reset();
    committedBody_ = committedFace_ = 0;
    offsetBody_ = offsetFace_ = 0;
    committedShape_ = {};
    clearPreview();
    dragging_ = false;
    dragButton_ = Qt::NoButton;
}
void Viewport::fit() {
    if (doc_.bodies().empty()) {
        target_ = {0, 0, 0};
        distance_ = 14;
        cameraChanged();
        return;
    }
    Vec3 lo{1e9, 1e9, 1e9}, hi{-1e9, -1e9, -1e9};
    bool hasVertices = false;
    for (const auto &[id, b] : doc_.bodies()) {
        const auto world = doc_.worldTransform(id);
        auto include = [&](Vec3 local) {
            hasVertices = true;
            const auto point = world.point(local);
            lo = {std::min(lo.x, point.x), std::min(lo.y, point.y), std::min(lo.z, point.z)};
            hi = {std::max(hi.x, point.x), std::max(hi.y, point.y), std::max(hi.z, point.z)};
        };
        for (auto [vid, local] : b->surface.vertices)
            include(local);
        if (b->referenceImage)
            for (const auto point : referenceImageCorners(*b->referenceImage, {}))
                include(point);
        if (guidesVisible_)
            for (const auto &[gid, guide] : b->guides)
                include(guide.origin);
    }
    if (!hasVertices) {
        target_ = {0, 0, 0};
        distance_ = 14;
        cameraChanged();
        return;
    }
    target_ = (lo + hi) * .5;
    const auto aspect = double(width()) / std::max(1, height());
    const auto halfAngle = std::atan(std::tan(fov_ * degreesToRadians / 2) * std::min(1., aspect));
    const auto radius = length(hi - lo) * .5;
    distance_ =
        std::max(2., radius * 1.1 / (ortho_ ? .45 * std::min(1., aspect) : std::sin(halfAngle)));
    cameraChanged();
}
void Viewport::frameBounds(Vec3 low, Vec3 high) {
    for (const auto point : {low, high})
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
            throw std::runtime_error("Cannot frame non-finite geometry");
    if (low.x > high.x || low.y > high.y || low.z > high.z)
        throw std::runtime_error("Cannot frame empty bounds");
    target_ = low + (high - low) * .5;
    const auto aspect = double(width()) / std::max(1, height());
    const auto halfAngle = std::atan(std::tan(fov_ * degreesToRadians / 2) * std::min(1., aspect));
    const auto radius = length(high - low) * .5;
    distance_ =
        std::max(.05, radius * 1.1 / (ortho_ ? .45 * std::min(1., aspect) : std::sin(halfAngle)));
    cameraChanged();
}
QJsonObject Viewport::shapeCommand(Vec3 end) const {
    if (guideTool())
        return guideCommand(end);
    const auto origin =
        anchor_ ? anchor_ : (session_.canRevise() ? committedAnchor_ : std::nullopt);
    if (!origin)
        throw std::runtime_error("Choose a first point");
    const auto a = *origin;
    auto point = [](Vec3 p) { return QJsonArray{p.x, p.y, p.z}; };
    QJsonObject command{{"body", QString::number(drawingContext_)},
                        {"space", "world"},
                        {"origin", point(plane_.origin)},
                        {"normal", point(plane_.normal)}};
    if (tool_ == Tool::Line || tool_ == Tool::Freehand) {
        QJsonArray points;
        if (tool_ == Tool::Line) {
            points = {point(a), point(end)};
            if (std::abs(dot(end - a, plane_.normal)) > tolerance) {
                command["origin"] = point(a);
                auto normal = cross(end - a, plane_.xAxis);
                if (length(normal) <= tolerance)
                    normal = cross(end - a, plane_.yAxis);
                command["normal"] = point(normalized(normal));
            }
        } else {
            for (auto sample : samples_)
                points.append(point(sample));
            if (samples_.empty() || length(samples_.back() - end) >= tolerance)
                points.append(point(end));
        }
        command["command"] = "geometry.polyline";
        command["points"] = points;
        command["closed"] = false;
        return command;
    }
    const auto delta = end - a;
    if (tool_ == Tool::Circle || tool_ == Tool::Polygon) {
        const auto radius = length(delta);
        auto axis = length(delta) > tolerance ? normalized(delta) : plane_.xAxis;
        command["command"] = tool_ == Tool::Circle ? "geometry.circle" : "geometry.polygon";
        if (tool_ == Tool::Circle) {
            command.remove("origin");
            command["center"] = point(a);
            command["segments"] = int(curveSegments_);
        } else {
            command["origin"] = point(a);
            command["sides"] = int(polygonSides_);
        }
        command["xAxis"] = point(axis);
        command["radius"] = radius;
        return command;
    }
    if (arcTool()) {
        const auto base =
            baseline_ ? baseline_ : (session_.canRevise() ? committedBaseline_ : std::nullopt);
        if (!base)
            throw std::runtime_error("Choose the second construction point first");
        command.remove("origin");
        command["segments"] = int(curveSegments_);
        if (tool_ == Tool::ThreePointArc) {
            command["command"] = "geometry.arc_three_points";
            command.remove("normal");
            command["start"] = point(a);
            command["through"] = point(*base);
            command["end"] = point(end);
        } else if (tool_ == Tool::TwoPointArc) {
            command["command"] = "geometry.arc_two_points";
            command["start"] = point(a);
            command["end"] = point(*base);
            command["bulge"] =
                dot(end - (a + *base) * .5, cross(plane_.normal, normalized(*base - a)));
        } else {
            const auto axis = normalized(*base - a);
            auto sweep = std::atan2(dot(delta, cross(plane_.normal, axis)), dot(delta, axis));
            if (sweep < 0)
                sweep += 2 * std::numbers::pi;
            command["command"] = tool_ == Tool::Pie ? "geometry.pie" : "geometry.arc_center";
            command["center"] = point(a);
            command["xAxis"] = point(axis);
            command["radius"] = length(*base - a);
            command["startAngle"] = 0;
            command["sweepAngle"] = sweep;
        }
        return command;
    }
    Vec3 axis = plane_.xAxis, corner = a;
    double width = std::abs(dot(delta, plane_.xAxis)), height = std::abs(dot(delta, plane_.yAxis));
    if (tool_ == Tool::RotatedRectangle) {
        const auto base =
            baseline_ ? baseline_ : (session_.canRevise() ? committedBaseline_ : std::nullopt);
        if (!base)
            throw std::runtime_error("Choose the rectangle baseline endpoint first");
        width = length(*base - a);
        axis = normalized(*base - a);
        const auto perpendicular = cross(plane_.normal, axis);
        const auto signedHeight = dot(delta, perpendicular);
        height = std::abs(signedHeight);
        if (signedHeight < 0)
            corner = corner + perpendicular * signedHeight;
    } else {
        if (dot(delta, plane_.xAxis) < 0)
            corner = corner + plane_.xAxis * dot(delta, plane_.xAxis);
        if (dot(delta, plane_.yAxis) < 0)
            corner = corner + plane_.yAxis * dot(delta, plane_.yAxis);
    }
    command["command"] = "geometry.rectangle";
    command["origin"] = point(corner);
    command["xAxis"] = point(axis);
    command["width"] = width;
    command["height"] = height;
    return command;
}
QJsonObject Viewport::previewCommand(const QJsonObject &command) {
    QJsonObject result;
    previewValid_ = false;
    previewEdges_.clear();
    previewGuides_.clear();
    try {
        validateHostedTransform(command);
        result = session_.preview(command);
        const auto geometry = result["geometry"].toObject();
        for (auto it = geometry.begin(); it != geometry.end(); ++it) {
            std::map<QString, Vec3> vertices;
            const auto body = it.value().toObject();
            const auto values = body["worldTransform"].toArray();
            if (values.size() != 16)
                throw std::runtime_error("Preview omitted its world transform");
            Transform world;
            for (int i = 0; i < 16; ++i)
                world.m[i] = values[i].toDouble();
            for (const auto &value : body["vertices"].toArray()) {
                const auto vertex = value.toObject();
                const auto p = vertex["point"].toArray();
                vertices[vertex["id"].toString()] =
                    world.point({p[0].toDouble(), p[1].toDouble(), p[2].toDouble()});
            }
            if (transformTool())
                for (const auto &value : body["guides"].toArray()) {
                    const auto guide = value.toObject();
                    const auto origin = guide["origin"].toArray();
                    const auto p = world.point(
                        {origin[0].toDouble(), origin[1].toDouble(), origin[2].toDouble()});
                    if (guide["kind"].toString() == "point")
                        previewGuides_.push_back(guidePoint(p));
                    else {
                        const auto direction = guide["direction"].toArray();
                        previewGuides_.push_back(guideLine(
                            p, world.vector({direction[0].toDouble(), direction[1].toDouble(),
                                             direction[2].toDouble()})));
                    }
                }
            for (const auto &value : body["edges"].toArray()) {
                const auto edge = value.toObject()["vertices"].toArray();
                previewEdges_.push_back(
                    {vertices.at(edge[0].toString()), vertices.at(edge[1].toString())});
            }
        }
        previewValid_ = true;
        previewError_.clear();
    } catch (const std::exception &error) {
        const auto text = QString::fromUtf8(error.what());
        if (previewError_ != text)
            emit message(text);
        previewError_ = text;
    }
    update();
    return result;
}
void Viewport::updateToolPreview(QPointF point) {
    if (tool_ == Tool::HostedPlacement) {
        updateHostedPlacement(point);
        return;
    }
    if (!session_.active() || !anchor_)
        return;
    if (transformTool()) {
        updateTransformPreview(point);
        return;
    }
    if (tool_ == Tool::Offset) {
        updateOffsetPreview(point);
        return;
    }
    if (tool_ == Tool::Extrude) {
        const auto [origin, direction] = ray(point);
        const auto w = origin - *anchor_;
        const auto b = dot(direction, extrusionAxis_);
        const auto denominator = 1 - b * b;
        if (denominator < 1e-6) {
            previewValid_ = false;
            previewEdges_.clear();
            previewGuide_.reset();
            previewError_ = "Orbit away from the face normal or enter a distance";
            update();
            return;
        }
        previewDistance_ =
            std::round((dot(extrusionAxis_, w) - b * dot(direction, w)) / denominator * 10) / 10;
        previewCommand(extrusionCommand(previewDistance_));
        emit measurementPreview(QLocale().toString(previewDistance_, 'g', 8));
    } else if (auto end = ground(point)) {
        if (tool_ == Tool::Freehand && samples_.size() >= 3 &&
            (point - project(*anchor_)).manhattanLength() <= 6)
            end = anchor_;
        cursor_ = end;
        if (threePointTool() && !baseline_) {
            previewEdges_ = {{{*anchor_, *end}}};
            previewValid_ = false;
            previewError_.clear();
            update();
            return;
        }
        if (tool_ == Tool::Freehand &&
            (samples_.empty() || length(*end - samples_.back()) > tolerance)) {
            if (samples_.size() >= 512) {
                previewValid_ = false;
                previewError_ = "Freehand stroke reached its 512-point limit";
                update();
                return;
            }
            samples_.push_back(*end);
        }
        try {
            if (guideTool()) {
                previewEdges_.clear();
                previewGuide_.reset();
                guideMeasurement(*end);
                if (createGuides_) {
                    session_.preview(guideCommand(*end));
                    previewGuide_ = prospectiveGuide(*end);
                }
                previewValid_ = true;
                previewError_.clear();
                emit measurementPreview(guideMeasurementText(*end));
                update();
                return;
            }
            previewCommand(shapeCommand(*end));
        } catch (const std::exception &error) {
            previewValid_ = false;
            previewEdges_.clear();
            previewError_ = QString::fromUtf8(error.what());
            emit message(previewError_);
            update();
        }
        const auto delta = plane_.coordinates(*end) - plane_.coordinates(*anchor_);
        const auto locale = QLocale();
        if (guideTool())
            return;
        if (arcTool()) {
            try {
                const auto command = shapeCommand(*end);
                if (tool_ == Tool::CenterArc || tool_ == Tool::Pie)
                    emit measurementPreview(
                        locale.toString(command["radius"].toDouble(), 'g', 8) +
                        (locale.decimalPoint() == "," ? "; " : ", ") +
                        locale.toString(command["sweepAngle"].toDouble() * 180 / std::numbers::pi,
                                        'g', 8) +
                        " deg");
                else if (tool_ == Tool::TwoPointArc)
                    emit measurementPreview(locale.toString(command["bulge"].toDouble(), 'g', 8));
                else {
                    const auto separator = locale.decimalPoint() == "," ? ";" : ",";
                    emit measurementPreview("[" + locale.toString(end->x, 'g', 8) + separator +
                                            locale.toString(end->y, 'g', 8) + separator +
                                            locale.toString(end->z, 'g', 8) + "]");
                }
            } catch (const std::exception &) {
                // The authoritative preview already displays the constraint error.
            }
        } else
            emit measurementPreview(tool_ == Tool::Rectangle
                                        ? locale.toString(std::abs(delta.x), 'g', 8) +
                                              (locale.decimalPoint() == "," ? "; " : ", ") +
                                              locale.toString(std::abs(delta.y), 'g', 8)
                                        : locale.toString(length(delta), 'g', 8));
    } else {
        previewValid_ = false;
        previewEdges_.clear();
        previewGuide_.reset();
        previewError_ = directionLocks_.current()
                            ? "Orbit to view the locked direction or enter a length"
                            : "Orbit to view the drawing plane";
        update();
    }
}
void Viewport::finishShape(Vec3 end, std::optional<QJsonObject> overrideCommand) {
    try {
        if (guideTool()) {
            finishGuide(end);
            return;
        }
        validateLockedPoint(end);
        const auto origin = anchor_ ? anchor_ : committedAnchor_;
        const auto command = overrideCommand ? *overrideCommand : shapeCommand(end);
        const auto result = session_.commit(command);
        committedShape_ = command;
        committedAnchor_ = origin;
        committedEnd_ = end;
        committedBaseline_ = baseline_ ? baseline_ : committedBaseline_;
        const auto created = result["created"].toArray();
        const auto id = created.empty() ? drawingContext_ : created[0].toString().toULongLong();
        chainPending_ = tool_ == Tool::Line;
        chainContext_ = id;
        committedPointer_ = previous_;
        clearConstraints();
        clearPreview();
        if (id)
            setSelection(id, doc_.bodies().at(id)->surface.faces.empty()
                                 ? 0
                                 : doc_.bodies().at(id)->surface.faces.begin()->first);
        emit changed();
    } catch (const std::exception &e) {
        previewError_ = QString::fromUtf8(e.what());
        previewValid_ = false;
        emit message(previewError_);
        update();
    }
}
void Viewport::setTheme(const ThemeColors &) {
    // Application chrome changes independently of the saved model presentation.
    update();
}
bool Viewport::measurements(const QString &text) {
    measurementCompleted_ = false;
    const auto revision = doc_.revision();
    try {
        const auto trimmed = text.trimmed();
        if (trimmed.isEmpty() || trimmed.size() > 1024)
            throw std::runtime_error("Measurements must contain 1–1024 characters");
        if (tool_ == Tool::Protractor && !trimmed.startsWith('[') && !trimmed.startsWith('<')) {
            const auto origin =
                anchor_ ? anchor_ : (session_.canRevise() ? committedAnchor_ : std::nullopt);
            const auto base =
                baseline_ ? baseline_ : (session_.canRevise() ? committedBaseline_ : std::nullopt);
            if (!origin || !base)
                throw std::runtime_error("Choose the center and baseline before entering an angle");
            const auto line =
                angledGuide(DrawingPlane::make(*origin, plane_.normal, *base - *origin),
                            parseAngle(trimmed, "deg", QLocale()));
            finishShape(*origin + line.direction * length(*base - *origin));
            return doc_.revision() != revision || measurementCompleted_;
        }
        if (tool_ == Tool::HostedPlacement)
            return hostedMeasurements(trimmed);
        if (transformTool())
            return transformMeasurements(trimmed);
        const bool centerInput = tool_ == Tool::CenterArc || tool_ == Tool::Pie;
        if (centerInput && !trimmed.startsWith('[') && !trimmed.startsWith('<') &&
            !trimmed.endsWith('s', Qt::CaseInsensitive)) {
            if (session_.phase() == ToolSession::Phase::Committed && !session_.canRevise())
                throw std::runtime_error(
                    "Another edit changed the document; start a new operation");
            const auto origin =
                anchor_ ? anchor_ : (session_.canRevise() ? committedAnchor_ : std::nullopt);
            if (!origin)
                throw std::runtime_error("Choose the center or enter [x,y,z]");
            const auto locale = QLocale();
            const auto parts = trimmed.split(locale.decimalPoint() == "," ? ';' : ',');
            const auto base =
                baseline_ ? baseline_ : (session_.canRevise() ? committedBaseline_ : std::nullopt);
            if (parts.size() == 1 && !base) {
                const auto radius = parseLength(parts[0], inputUnit(doc_.displayUnits()), locale);
                if (radius <= tolerance)
                    throw std::runtime_error("Radius must exceed modeling tolerance");
                const auto radiusPoint = *origin + plane_.xAxis * radius;
                checkPoint(radiusPoint);
                baseline_ = radiusPoint;
                emit message(nextPointHint());
                return true;
            }
            if (parts.size() != 1 && parts.size() != 2)
                throw std::runtime_error("Enter radius, angle or an angle after the radius point");
            const auto radius = parts.size() == 2
                                    ? parseLength(parts[0], inputUnit(doc_.displayUnits()), locale)
                                    : length(*base - *origin);
            const auto sweep = parseAngle(parts.back(), "deg", locale);
            const auto axis = base ? normalized(*base - *origin) : plane_.xAxis;
            const auto kind = tool_ == Tool::Pie ? CurveKind::Pie : CurveKind::Arc;
            const auto curve = centerCurve(kind, DrawingPlane::make(*origin, plane_.normal, axis),
                                           radius, 0, sweep, curveSegments_);
            auto point = [](Vec3 p) { return QJsonArray{p.x, p.y, p.z}; };
            const QJsonObject command{
                {"command", tool_ == Tool::Pie ? "geometry.pie" : "geometry.arc_center"},
                {"body", QString::number(drawingContext_)},
                {"space", "world"},
                {"center", point(*origin)},
                {"normal", point(plane_.normal)},
                {"xAxis", point(axis)},
                {"radius", radius},
                {"startAngle", 0},
                {"sweepAngle", sweep},
                {"segments", int(curveSegments_)}};
            const auto oldBase = baseline_;
            baseline_ = *origin + axis * radius;
            finishShape(curve.point(sweep), command);
            if (doc_.revision() == revision)
                baseline_ = oldBase;
            return doc_.revision() != revision;
        }
        const auto input = parseMeasurements(text, inputUnit(doc_.displayUnits()), QLocale());
        const auto &values = input.values;
        if (input.kind == MeasurementKind::Segments &&
            (tool_ == Tool::Polygon || tool_ == Tool::Circle || arcTool())) {
            const auto minimum = tool_ == Tool::Polygon || tool_ == Tool::Circle ? 3
                                 : tool_ == Tool::Pie                            ? 2
                                                                                 : 1;
            if (values[0] < minimum || values[0] > 256)
                throw std::runtime_error("Segment count is outside this tool's supported range");
            auto &count = tool_ == Tool::Polygon ? polygonSides_ : curveSegments_;
            const auto previous = count;
            count = unsigned(values[0]);
            if (session_.phase() == ToolSession::Phase::Committed) {
                if (!session_.canRevise()) {
                    count = previous;
                    throw std::runtime_error(
                        "Another edit changed the document; start a new operation");
                }
                auto command = committedShape_;
                command[tool_ == Tool::Polygon ? "sides" : "segments"] = int(count);
                finishShape(*committedEnd_, command);
                if (doc_.revision() == revision) {
                    count = previous;
                    return false;
                }
            } else if (session_.active() && cursor_ && (!threePointTool() || baseline_))
                previewCommand(shapeCommand(*cursor_));
            emit message(QString("%1 segments").arg(count));
            return true;
        }
        if (input.kind == MeasurementKind::Segments || input.kind == MeasurementKind::Copies ||
            input.kind == MeasurementKind::Divisions)
            throw std::runtime_error("This tool expects a length, dimensions or coordinates");
        if (tool_ == Tool::Extrude) {
            if (input.kind != MeasurementKind::Values || values.size() != 1)
                throw std::runtime_error("Push/pull expects one signed distance");
            if (session_.phase() == ToolSession::Phase::Ready) {
                if (!selected_ || !selectedFace_)
                    throw std::runtime_error("Select one editable face to push/pull");
                const auto &surface = doc_.bodies().at(selected_)->surface;
                const auto anchor = doc_.worldTransform(selected_).point(
                    surface.vertices.at(surface.faces.at(selectedFace_).loops[0][0]));
                beginExtrusion(selected_, selectedFace_, anchor);
            }
            finishExtrusion(values[0]);
            return true;
        }
        if (tool_ == Tool::Offset) {
            if (input.kind != MeasurementKind::Values || values.size() != 1)
                throw std::runtime_error("Offset expects one signed distance");
            if (session_.phase() == ToolSession::Phase::Ready) {
                if (!selected_ || !selectedFace_)
                    throw std::runtime_error("Select one editable face to offset");
                const auto &surface = doc_.bodies().at(selected_)->surface;
                const auto anchor = doc_.worldTransform(selected_).point(
                    surface.vertices.at(surface.faces.at(selectedFace_).loops[0][0]));
                beginOffset(selected_, selectedFace_, anchor);
            }
            finishOffset(values[0]);
            return true;
        }
        if (!drawingTool())
            throw std::runtime_error("Choose a drawing tool before entering geometry");
        if (session_.phase() == ToolSession::Phase::Committed && !session_.canRevise())
            throw std::runtime_error("Another edit changed the document; start a new operation");
        auto origin = anchor_ ? anchor_ : (session_.canRevise() ? committedAnchor_ : std::nullopt);
        if (!origin) {
            plane_ = heldPlane_.value_or(configuredPlane_.value_or(DrawingPlane{}));
            drawingContext_ = heldPlane_ ? heldContext_ : configuredContext_;
            if (!drawingContext_)
                drawingContext_ = selection_.context();
            if (drawingContext_ && (!doc_.bodies().contains(drawingContext_) ||
                                    !selection_.inContext(doc_, drawingContext_) ||
                                    selection_.locked(doc_, drawingContext_)))
                throw std::runtime_error("Drawing context is missing, inactive or locked");
        }
        if (input.kind == MeasurementKind::AbsolutePoint ||
            input.kind == MeasurementKind::RelativePoint) {
            auto point = Vec3{values[0], values[1], values[2]};
            if (input.kind == MeasurementKind::RelativePoint)
                point = point + origin.value_or(Vec3{});
            checkPoint(point);
            const auto local = plane_.coordinates(point);
            if (!(tool_ == Tool::Line && directionLocks_.current())) {
                if (std::abs(local.z) > tolerance)
                    throw std::runtime_error("Coordinate is outside the active drawing plane");
                point = plane_.point(local.x, local.y);
            }
            validateLockedPoint(point);
            if (!origin) {
                clearPreview();
                tapeReference_.reset();
                session_.begin();
                anchor_ = point;
                cursor_ = point;
                if (tool_ == Tool::Freehand)
                    samples_ = {point};
                emit message("First point set · Enter the endpoint or dimensions");
                update();
                return true;
            }
            if (threePointTool() && !baseline_ && !session_.canRevise()) {
                if (length(point - *origin) <= tolerance)
                    throw std::runtime_error("Construction points must be distinct");
                baseline_ = point;
                emit message(nextPointHint());
                return true;
            }
            finishShape(point);
        } else {
            if (!origin)
                throw std::runtime_error("Choose the first point or enter [x,y,z]");
            if ((tool_ == Tool::Rectangle || tool_ == Tool::RotatedRectangle) &&
                values.size() == 2) {
                if (values[0] <= 0 || values[1] <= 0)
                    throw std::runtime_error("Rectangle dimensions must be greater than zero");
                auto axis = plane_.xAxis;
                if (tool_ == Tool::RotatedRectangle) {
                    const auto previous = baseline_ ? baseline_ : committedBaseline_;
                    if (previous)
                        axis = normalized(*previous - *origin);
                    baseline_ = *origin + axis * values[0];
                }
                finishShape(*origin + axis * values[0] + cross(plane_.normal, axis) * values[1]);
            } else if ((tool_ == Tool::Circle || tool_ == Tool::Polygon) && values.size() == 1) {
                if (values[0] <= 0)
                    throw std::runtime_error("Radius must be greater than zero");
                if (const auto lock = directionLocks_.current())
                    finishShape(constrainedLength(
                        *lock, *origin, cursor_.value_or(*origin + lock->direction), values[0]));
                else
                    finishShape(*origin + plane_.xAxis * values[0]);
            } else if (tool_ == Tool::TwoPointArc && values.size() == 1) {
                const auto base = baseline_
                                      ? baseline_
                                      : (session_.canRevise() ? committedBaseline_ : std::nullopt);
                if (!base)
                    throw std::runtime_error("Choose both endpoints before entering a bulge");
                finishShape((*origin + *base) * .5 +
                            cross(plane_.normal, normalized(*base - *origin)) * values[0]);
            } else if (tool_ == Tool::Tape && values.size() == 1 && tapeReference_) {
                finishShape(tapeReference_->origin +
                            cross(plane_.normal, tapeReference_->direction) * values[0]);
            } else if (tool_ == Tool::Line && values.size() == 2) {
                finishShape(*origin + plane_.xAxis * values[0] + plane_.yAxis * values[1]);
            } else if ((tool_ == Tool::Line || tool_ == Tool::Tape) && values.size() == 1 &&
                       (cursor_ || committedEnd_)) {
                if (values[0] <= 0)
                    throw std::runtime_error("Length must be greater than zero");
                if (const auto lock = directionLocks_.current()) {
                    auto preview = cursor_.value_or(*origin);
                    if (length(preview - *origin) <= tolerance)
                        preview = *origin + lock->direction;
                    finishShape(constrainedLength(*lock, *origin, preview, values[0]));
                } else
                    finishShape(
                        *origin +
                        normalized(cursor_.value_or(committedEnd_.value_or(*origin)) - *origin) *
                            values[0]);
            } else
                throw std::runtime_error("Rectangle: two dimensions. Circle: radius. Line: length "
                                         "along preview or coordinates.");
        }
    } catch (const std::exception &error) {
        emit message(error.what());
    }
    return doc_.revision() != revision || measurementCompleted_;
}
bool Viewport::event(QEvent *event) {
    if (walkNavigation(event))
        return true;
    if (nativeNavigation(event))
        return true;
    if (event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (tool_ == Tool::Select && (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab)) {
            selectionKey(key);
            event->accept();
            return true;
        }
        if (key->key() == Qt::Key_Tab && !key->modifiers() &&
            (drawingTool() || tool_ == Tool::Move) && inferenceCount() &&
            !directionLocks_.current() && !heldPoint_) {
            inferenceChoice_ = (inferenceChoice_ + 1) % inferenceCount();
            inferenceCycled_ = true;
            if (session_.active())
                updateToolPreview(inferencePointer_);
            if (const auto candidate = acquiredInference())
                emit message(QString::fromUtf8(inferenceLabel(candidate->kind)));
            else if (const auto direction = acquiredDirection())
                emit message(QString::fromUtf8(directionLabel(direction->constraint.kind)));
            update();
            event->accept();
            return true;
        }
    }
    if (event->type() == QEvent::ShortcutOverride) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() != Qt::Key_Control) {
            guideControlPending_ = false;
            pushControlPending_ = false;
            transformControlPending_ = false;
        }
        if (tool_ != Tool::Select && tool_ != Tool::Orbit && tool_ != Tool::Pan &&
            tool_ != Tool::Zoom && tool_ != Tool::Walk && tool_ != Tool::LookAround &&
            tool_ != Tool::Paint && tool_ != Tool::Sweep && tool_ != Tool::Intersect &&
            tool_ != Tool::Boolean && tool_ != Tool::Orientation &&
            !(key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) &&
            !key->text().isEmpty() && QString("0123456789.+-[<xX/").contains(key->text()[0])) {
            event->accept();
            return true;
        }
    }
    if (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut)
        update();
    if (event->type() == QEvent::WindowDeactivate || event->type() == QEvent::Hide ||
        event->type() == QEvent::TouchCancel) {
        cancel();
    } else if (event->type() == QEvent::UngrabMouse || event->type() == QEvent::FocusOut) {
        guideControlPending_ = false;
        pushControlPending_ = false;
        transformControlPending_ = false;
        selectionPressed_ = selectingBox_ = false;
        releaseInferenceHold();
        // Keep the anchor when focus moves to Measurements. End every button
        // gesture so a late release cannot publish an unintended edit.
        dragging_ = false;
        dragButton_ = Qt::NoButton;
        toolPressed_ = false;
        dragCommit_ = false;
    }
    return QOpenGLWidget::event(event);
}
void Viewport::mousePressEvent(QMouseEvent *e) {
    setFocus();
    guideControlPending_ = false;
    pushControlPending_ = false;
    transformControlPending_ = false;
    previous_ = e->position();
    if (tool_ == Tool::Paint && e->button() == Qt::LeftButton) {
        try {
            paintAt(e->position(), e->modifiers().testFlag(Qt::AltModifier));
        } catch (const std::exception &error) {
            emit message(error.what());
        }
        return;
    }
    if (e->button() != Qt::LeftButton || tool_ == Tool::Orbit || tool_ == Tool::Pan ||
        tool_ == Tool::Zoom || tool_ == Tool::LookAround || tool_ == Tool::Walk ||
        e->modifiers().testFlag(Qt::AltModifier)) {
        dragging_ = true;
        dragButton_ = e->button();
        toolPressed_ = false;
        dragCommit_ = false;
        return;
    }
    if (tool_ == Tool::Select) {
        syncSelection();
        selectionPressed_ = true;
        selectingBox_ = false;
        selectionGestureStamp_ = doc_.saveStamp();
        selectionStart_ = selectionEnd_ = e->position();
        selectionMode_ = selectionMode(e->modifiers());
        boxBase_ = selection_.entities();
        return;
    }
    if (tool_ == Tool::Sweep || tool_ == Tool::Intersect || tool_ == Tool::Boolean ||
        tool_ == Tool::Orientation) {
        try {
            if (tool_ == Tool::Sweep)
                finishSweep();
            else if (tool_ == Tool::Intersect)
                finishIntersection();
            else if (tool_ == Tool::Boolean)
                finishBoolean();
            else
                finishOrientation();
        } catch (const std::exception &error) {
            emit message(error.what());
        }
        return;
    }
    if (tool_ == Tool::HostedPlacement) {
        try {
            if (!hostedRetainPose_)
                updateHostedPlacement(e->position());
            finishHostedPlacement();
        } catch (const std::exception &error) {
            emit message(error.what());
        }
        return;
    }
    toolPressed_ = true;
    dragCommit_ = false;
    toolPressPosition_ = e->position();
    if (transformTool()) {
        transformClick(e->position());
        return;
    }
    if (drawingTool()) {
        try {
            if (chainPending_) {
                if (session_.canRevise())
                    beginChain();
                else
                    cancel();
            }
            if (!session_.active())
                choosePlane(e->position());
            if (auto point = ground(e->position())) {
                if (!session_.active()) {
                    clearPreview();
                    captureTapeReference();
                    session_.begin();
                    anchor_ = point;
                    cursor_ = point;
                    if (tool_ == Tool::Freehand)
                        samples_ = {*point};
                    toolPressed_ = true;
                    toolPressPosition_ = e->position();
                    emit message(
                        threePointTool()
                            ? "Choose the second construction point · Esc cancels"
                            : "Click the endpoint, drag, or enter measurements · Esc cancels");
                } else if (threePointTool() && !baseline_) {
                    if (length(*point - *anchor_) <= tolerance)
                        throw std::runtime_error("Construction points must be distinct");
                    baseline_ = point;
                    cursor_ = point;
                    toolPressed_ = false;
                    emit message(nextPointHint());
                } else {
                    if (tool_ == Tool::Freehand)
                        updateToolPreview(e->position());
                    finishShape(tool_ == Tool::Freehand && cursor_ ? *cursor_ : *point);
                }
            }
        } catch (const std::exception &error) {
            emit message(error.what());
        }
        update();
        return;
    }
    if ((tool_ == Tool::Extrude || tool_ == Tool::Offset) && session_.active()) {
        updateToolPreview(e->position());
        if (previewValid_) {
            try {
                if (tool_ == Tool::Offset)
                    finishOffset(previewDistance_);
                else
                    finishExtrusion(previewDistance_);
            } catch (const std::exception &error) {
                emit message(error.what());
            }
        }
        toolPressed_ = false;
        return;
    }
    auto [body, face] = pick(e->position());
    if (body && !selectable({body, SelectionKind::Face, face}))
        body = face = 0;
    setSelection(body, face);
    if (tool_ == Tool::Extrude && body && face) {
        const auto [origin, direction] = ray(e->position());
        beginExtrusion(body, face, origin + direction * nearestFace(e->position()).distance);
        emit message("Move to preview, click or drag to finish, or enter a distance · Esc cancels");
    }
    if (tool_ == Tool::Offset && body && face) {
        try {
            const auto [origin, direction] = ray(e->position());
            beginOffset(body, face, origin + direction * nearestFace(e->position()).distance);
            toolPressed_ = true;
            toolPressPosition_ = e->position();
            emit message("Move across the nearest edge to preview · Click, drag or enter a signed "
                         "distance · Esc cancels");
        } catch (const std::exception &error) {
            emit message(error.what());
        }
    }
    update();
}
void Viewport::mouseMoveEvent(QMouseEvent *e) {
    if (dragging_ && !e->buttons().testFlag(dragButton_)) {
        dragging_ = false;
        dragButton_ = Qt::NoButton;
    }
    const auto delta = e->position() - previous_;
    previous_ = e->position();
    if (!dragging_ && chainPending_ && (e->position() - committedPointer_).manhattanLength() >= 4) {
        if (session_.canRevise())
            beginChain();
        else
            cancel();
    }
    if (dragging_) {
        if ((tool_ == Tool::Walk || tool_ == Tool::LookAround) && dragButton_ == Qt::LeftButton)
            orbitCamera(delta);
        else if (tool_ == Tool::Zoom && dragButton_ == Qt::LeftButton)
            zoomCamera(e->position(), std::exp(std::clamp(delta.y() * .01, -2., 2.)));
        else if (tool_ == Tool::Pan || dragButton_ == Qt::RightButton ||
                 e->modifiers().testFlag(Qt::ShiftModifier))
            panCamera(e->position(), delta);
        else
            orbitCamera(delta);
    } else if (tool_ == Tool::Select) {
        if (selectionPressed_) {
            if (!e->buttons().testFlag(Qt::LeftButton))
                selectionPressed_ = selectingBox_ = false;
            else {
                selectionEnd_ = e->position();
                selectingBox_ |= (selectionEnd_ - selectionStart_).manhattanLength() >= 4;
            }
        }
        if (!selectingBox_) {
            const auto candidate = selectionAt(e->position());
            if (candidate != hover_) {
                hover_ = candidate;
                overlayDirty_ = true;
            }
        }
    } else if (session_.active()) {
        if (toolPressed_ && e->buttons().testFlag(Qt::LeftButton) &&
            (e->position() - toolPressPosition_).manhattanLength() >= 4)
            dragCommit_ = true;
        updateToolPreview(e->position());
    } else if ((drawingTool() || transformTool()) && tool_ != Tool::Freehand) {
        try {
            acquireInference(e->position(), false);
        } catch (const std::exception &error) {
            inference_ = {};
            emit message(error.what());
        }
    }
    if (dragging_) {
        inference_ = {};
        directions_.clear();
        hoverReference_.reset();
    }
    update();
}
void Viewport::mouseReleaseEvent(QMouseEvent *e) {
    if (e->button() == dragButton_) {
        dragging_ = false;
        dragButton_ = Qt::NoButton;
        return;
    }
    if (e->button() != Qt::LeftButton)
        return;
    if (tool_ == Tool::Select && selectionPressed_) {
        selectionRelease(e->position());
        return;
    }
    const auto finish = toolPressed_ && dragCommit_ && session_.active();
    toolPressed_ = false;
    dragCommit_ = false;
    if (finish) {
        updateToolPreview(e->position());
        if (threePointTool() && !baseline_ && cursor_ && anchor_ &&
            length(*cursor_ - *anchor_) > tolerance) {
            baseline_ = cursor_;
            emit message(nextPointHint());
            return;
        }
        if (!previewValid_)
            return;
        if (transformTool()) {
            try {
                finishTransform(transformPreview_);
            } catch (const std::exception &error) {
                emit message(error.what());
            }
        } else if (tool_ == Tool::Extrude || tool_ == Tool::Offset) {
            try {
                if (tool_ == Tool::Offset)
                    finishOffset(previewDistance_);
                else
                    finishExtrusion(previewDistance_);
            } catch (const std::exception &error) {
                emit message(error.what());
            }
        } else if (cursor_)
            finishShape(*cursor_);
    }
}
void Viewport::keyPressEvent(QKeyEvent *e) {
    if (tool_ == Tool::HostedPlacement &&
        (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)) {
        try {
            finishHostedPlacement();
        } catch (const std::exception &error) {
            emit message(error.what());
        }
        e->accept();
        return;
    }
    if ((tool_ == Tool::Sweep || tool_ == Tool::Intersect || tool_ == Tool::Boolean ||
         tool_ == Tool::Orientation) &&
        (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)) {
        try {
            if (tool_ == Tool::Sweep)
                finishSweep();
            else if (tool_ == Tool::Intersect)
                finishIntersection();
            else if (tool_ == Tool::Boolean)
                finishBoolean();
            else
                finishOrientation();
        } catch (const std::exception &error) {
            emit message(error.what());
        }
        e->accept();
        return;
    }
    if (transformTool() && e->key() == Qt::Key_Control) {
        if (!e->isAutoRepeat())
            transformControlPending_ = true;
        e->accept();
        return;
    }
    if (tool_ == Tool::Extrude && e->key() == Qt::Key_Control) {
        if (!e->isAutoRepeat())
            pushControlPending_ = true;
        e->accept();
        return;
    }
    if (guideTool() && e->key() == Qt::Key_Control) {
        if (!e->isAutoRepeat())
            guideControlPending_ = true;
        e->accept();
        return;
    }
    if (selectionKey(e)) {
        e->accept();
        return;
    }
    if (constraintKey(e)) {
        e->accept();
        return;
    }
    if (tool_ != Tool::Select && tool_ != Tool::Orbit && tool_ != Tool::Pan &&
        tool_ != Tool::Zoom && tool_ != Tool::Walk && tool_ != Tool::LookAround &&
        tool_ != Tool::Paint && tool_ != Tool::Sweep && tool_ != Tool::Intersect &&
        tool_ != Tool::Boolean && tool_ != Tool::Orientation &&
        !(e->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) &&
        !e->text().isEmpty() && QString("0123456789.+-[<xX/").contains(e->text()[0])) {
        emit measurementsRequested(e->text());
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_Escape) {
        if (session_.active() || chainPending_) {
            cancel();
            emit message("Operation canceled");
        } else {
            setTool(Tool::Select);
            emit message("Select a face");
        }
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
bool Viewport::clipped(Vec3 point, Id body) const {
    if (clipPlane_) {
        const auto &p = *clipPlane_;
        if (p[0] * point.x + p[1] * point.y + p[2] * point.z + p[3] < 0)
            return true;
    }
    if (doc_.activeSections().empty())
        return false;
    if (!cacheDirty_ && cachedDocument_ == doc_.identity() &&
        cachedRevision_ == doc_.revision() && bodyCaches_.contains(body)) {
        const auto &cache = *bodyCaches_.at(body);
        return !cache.sectionError.isEmpty() || !sectionContains(point, cache.sectionCuts);
    }
    return !sectionContains(point, effectiveSectionCuts(doc_, body));
}
void Viewport::benchmark(int count, bool instanced) {
    if (count < 1 || count > 1000000)
        throw std::runtime_error("Benchmark supports 1–1000000 triangles");
    benchmarkTriangles_ = count;
    stats_.profileEdges = 0;
    instances_ = instanced ? count : 0;
    benchmarkVertices_.clear();
    benchmarkVertices_.reserve(instanced ? 3 : count * 3);
    for (int i = 0; i < (instanced ? 1 : count); ++i) {
        float x = (i % 1000) * 1.2f, y = (i / 1000) * 1.2f;
        float z = instanced ? 0 : std::sin(float(i) * .017f) * .2f;
        for (auto p : {Vec3{x, y, z}, Vec3{x + 1, y, z}, Vec3{x, y + 1, z + .1f}})
            benchmarkVertices_.push_back({p.x, p.y, p.z, .4f, .6f, .5f});
    }
    stats_.meshTriangles = count;
    benchmarkDirty_ = true;
    target_ = {600, float(count / 1000) * .6f, 0};
    distance_ = 1600;
    pitch_ = 89;
    ortho_ = true;
    cameraChanged();
}
} // namespace sketchy
