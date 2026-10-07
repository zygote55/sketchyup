#include "integrations/render_snapshot.hpp"
#include "core/camera_motion.hpp"
#include "core/scenes.hpp"
#include <QJsonArray>
#include <algorithm>
#include <limits>
#include <numbers>
namespace sketchy {
namespace {
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void cameraPoint(Vec3 p) {
    require(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
                std::max({std::abs(p.x), std::abs(p.y), std::abs(p.z)}) <= 1e9,
            "Camera coordinates must be finite and within one billion metres");
}
QJsonArray point(Vec3 p) { return {p.x, p.y, p.z}; }
void fields(const QJsonObject &json, std::initializer_list<const char *> allowed) {
    for (auto it = json.begin(); it != json.end(); ++it)
        require(std::any_of(allowed.begin(), allowed.end(),
                            [&](const char *key) { return it.key() == key; }),
                "Unknown render setting");
}
double number(const QJsonObject &json, const char *key, double fallback) {
    if (!json.contains(key))
        return fallback;
    const auto value = json.value(key);
    require(value.isDouble() && std::isfinite(value.toDouble()),
            "Render setting must be a finite number");
    return value.toDouble();
}
int integer(const QJsonObject &json, const char *key, int fallback) {
    const auto value = number(json, key, fallback);
    require(value == std::floor(value) && value >= 0 && value <= 1000000,
            "Render setting must be a bounded integer");
    return int(value);
}
Vec3 vector(const QJsonValue &value) {
    require(value.isArray() && value.toArray().size() == 3,
            "Camera vector must contain three numbers");
    const auto v = value.toArray();
    for (auto component : v)
        require(component.isDouble() && std::isfinite(component.toDouble()),
                "Camera vector must contain finite numbers");
    Vec3 result{v[0].toDouble(), v[1].toDouble(), v[2].toDouble()};
    cameraPoint(result);
    return result;
}
} // namespace
void validateRenderSettings(const RenderSettings &s) {
    require(s.engine == RenderEngine::Cycles || s.engine == RenderEngine::Eevee,
            "Unsupported render engine");
    require(s.width >= 64 && s.width <= 4096 && s.height >= 64 && s.height <= 4096 &&
                s.samples >= 1 && s.samples <= 1024 && s.seed >= 0 && s.seed <= 1000000,
            "Render resolution, samples or seed exceed bounds");
}
Transform renderCameraTransform(const RenderCamera &c) {
    cameraPoint(c.position);
    cameraPoint(c.target);
    cameraPoint(c.up);
    require(std::isfinite(c.nearClip) && std::isfinite(c.farClip) && c.nearClip >= .00001 &&
                c.farClip > c.nearClip && c.farClip <= 1e10,
            "Invalid camera clipping range");
    require(std::isfinite(c.verticalFov) && c.verticalFov >= .01 && c.verticalFov <= 3 &&
                std::isfinite(c.yMag) && c.yMag >= .00001 && c.yMag <= 1e7,
            "Invalid camera projection");
    const auto z = normalized(c.position - c.target), x = normalized(cross(c.up, z)),
               y = cross(z, x);
    Transform result;
    result.m = {x.x, x.y, x.z, 0, y.x,          y.y,          y.z,          0,
                z.x, z.y, z.z, 0, c.position.x, c.position.y, c.position.z, 1};
    result.validate();
    return result;
}
QJsonObject describeRenderSettings(const RenderSettings &s) {
    return {{"width", s.width},
            {"height", s.height},
            {"samples", s.samples},
            {"seed", s.seed},
            {"engine", s.engine == RenderEngine::Eevee ? "eevee" : "cycles"}};
}
QJsonObject describeRenderCamera(const RenderCamera &c) {
    return {{"projection", c.orthographic ? "orthographic" : "perspective"},
            {"position", point(c.position)},
            {"target", point(c.target)},
            {"up", point(c.up)},
            {"verticalFov", c.verticalFov},
            {"yMag", c.yMag},
            {"nearClip", c.nearClip},
            {"farClip", c.farClip}};
}
RenderOptions parseRenderOptions(const QJsonObject &json) {
    fields(json,
           {"apiVersion", "width", "height", "samples", "seed", "camera", "environment", "engine"});
    require(json.value("apiVersion") == 1, "Render settings require apiVersion 1");
    RenderOptions result;
    auto &s = result.settings;
    s.width = integer(json, "width", s.width);
    s.height = integer(json, "height", s.height);
    s.samples = integer(json, "samples", s.samples);
    s.seed = integer(json, "seed", s.seed);
    if (json.contains("engine")) {
        require(json.value("engine") == "cycles" || json.value("engine") == "eevee",
                "Choose cycles or eevee rendering");
        s.engine = json.value("engine") == "eevee" ? RenderEngine::Eevee : RenderEngine::Cycles;
    }
    validateRenderSettings(s);
    if (json.contains("camera")) {
        require(json.value("camera").isObject(), "Camera settings must be an object");
        const auto camera = json.value("camera").toObject();
        fields(camera, {"projection", "position", "target", "up", "verticalFov", "yMag", "nearClip",
                        "farClip"});
        RenderCamera c;
        require(camera.value("projection") == "perspective" ||
                    camera.value("projection") == "orthographic",
                "Choose a camera projection");
        c.orthographic = camera.value("projection") == "orthographic";
        c.position = vector(camera.value("position"));
        c.target = vector(camera.value("target"));
        if (camera.contains("up"))
            c.up = vector(camera.value("up"));
        c.verticalFov = number(camera, "verticalFov", c.verticalFov);
        c.yMag = number(camera, "yMag", c.yMag);
        c.nearClip = number(camera, "nearClip", c.nearClip);
        c.farClip = number(camera, "farClip", c.farClip);
        renderCameraTransform(c);
        result.camera = c;
    }
    if (json.contains("environment")) {
        require(json.value("environment").isObject(), "Environment settings must be an object");
        const auto environment = json.value("environment").toObject();
        fields(environment, {"path", "strength", "rotationDegrees"});
        require(environment.value("path").isString() &&
                    !environment.value("path").toString().isEmpty(),
                "Choose an HDR environment file");
        result.environment = readRenderEnvironment(environment.value("path").toString(),
                                                   number(environment, "strength", 1),
                                                   number(environment, "rotationDegrees", 0));
    }
    return result;
}
RenderCamera renderSceneCamera(const SceneCamera &pose) {
    pose.validate();
    RenderCamera camera;
    camera.target = pose.target;
    camera.position = sceneCameraEye(pose);
    camera.up =
        std::abs(pose.pitch) > 89.999 ? Vec3{0, pose.pitch > 0 ? 1. : -1., 0} : Vec3{0, 0, 1};
    camera.orthographic = pose.orthographic;
    camera.verticalFov = pose.fieldOfView * std::numbers::pi / 180;
    camera.yMag = pose.distance * .45;
    camera.nearClip = pose.orthographic ? .01 : std::max(.001, pose.distance / 10000);
    camera.farClip = std::max(1000., pose.distance * 10);
    return camera;
}
RenderSnapshot RenderSnapshot::captureSavedScene(const Document &document, Id scene,
                                                 RenderOptions options) {
    require(document.scenes().contains(scene), "Saved render scene no longer exists");
    const auto &saved = document.scenes().at(scene)->snapshot;
    require(missingSceneReferences(document, saved).empty(),
            "Saved render scene has missing references");
    require(!saved.section || !saved.section->plane,
            "Animation requires named sections, not temporary clipping planes");
    require(!saved.visibility || !saved.visibility->showHidden,
            "Animation cannot render the show-hidden editor overlay");
    if (!options.camera) {
        require(saved.camera.has_value(), "Saved render scene requires a camera");
        options.camera = renderSceneCamera(*saved.camera);
    }
    auto view = document.readSnapshot();
    recallSceneModel(view, scene);
    SelectionSet hidden;
    if (saved.visibility)
        for (const auto &entity : saved.visibility->hiddenEntities) {
            // Only bodies and faces contribute to the surface render.
            if (entity.kind == SceneEntityKind::Body)
                hidden.insert({entity.body, SelectionKind::Body, 0});
            else if (entity.kind == SceneEntityKind::Face)
                hidden.insert({entity.body, SelectionKind::Face, entity.entity});
        }
    auto result = capture(view, std::move(options), std::move(hidden));
    result.sourceIdentity_ = document.identity();
    result.sourceRevision_ = document.revision();
    result.sourceScene_ = scene;
    return result;
}
RenderSnapshot::RenderSnapshot(Document document, RenderSettings settings, RenderCamera camera,
                               SelectionSet hidden)
    : document_(std::move(document)), sourceIdentity_(document_.identity()),
      sourceRevision_(document_.revision()), settings_(settings), camera_(camera),
      hidden_(std::move(hidden)) {}
bool RenderSnapshot::edgeVisible(Id body, Id edge) const {
    return visible(body) && !hidden_.contains({body, SelectionKind::Edge, edge});
}
bool RenderSnapshot::visible(Id body, Id face) const {
    if (face && hidden_.contains({body, SelectionKind::Face, face}))
        return false;
    for (auto current = body; current; current = document_.bodies().at(current)->parent) {
        const auto &record = *document_.bodies().at(current);
        if (record.hidden || !tagVisible(document_.tags(), record.tag) ||
            hidden_.contains({current, SelectionKind::Body, 0}))
            return false;
    }
    return true;
}
RenderSnapshot RenderSnapshot::capture(const Document &document, RenderOptions options,
                                       SelectionSet hidden) {
    validateRenderSettings(options.settings);
    require(document.readSnapshotBytes() <= 256 * 1024 * 1024, "Render snapshot exceeds 256 MiB");
    require(document.bodies().size() <= 20000 && hidden.size() <= 10000,
            "Render snapshot contains too many records");
    for (auto entity : hidden) {
        require(document.bodies().contains(entity.body), "Hidden render entity no longer exists");
        if (entity.kind == SelectionKind::Face)
            require(document.bodies().at(entity.body)->surface.faces.contains(entity.entity),
                    "Hidden render face no longer exists");
    }
    for (auto entity : hidden)
        if (entity.kind == SelectionKind::Edge)
            require(document.bodies().at(entity.body)->topology.edges.contains(entity.entity),
                    "Hidden render edge no longer exists");
    RenderSnapshot result(document.readSnapshot(), options.settings,
                          options.camera.value_or(RenderCamera{}), std::move(hidden));
    if (options.environment)
        validateRenderEnvironment(*options.environment);
    result.environment_ = std::move(options.environment);
    if (!options.camera) {
        const double inf = std::numeric_limits<double>::infinity();
        Vec3 low{inf, inf, inf}, high{-inf, -inf, -inf};
        size_t references{};
        for (const auto &[id, record] : result.document_.bodies()) {
            if (!result.visible(id))
                continue;
            const auto transform = result.document_.worldTransform(id);
            for (const auto &[face, value] : record->surface.faces)
                if (result.visible(id, face))
                    for (const auto &loop : value.loops)
                        for (auto vertex : loop) {
                            require(++references <= 2000000,
                                    "Render snapshot contains too many face vertices");
                            const auto p = transform.point(record->surface.vertices.at(vertex));
                            low = {std::min(low.x, p.x), std::min(low.y, p.y),
                                   std::min(low.z, p.z)};
                            high = {std::max(high.x, p.x), std::max(high.y, p.y),
                                    std::max(high.z, p.z)};
                        }
        }
        if (references) {
            auto &c = result.camera_;
            c.target = (low + high) * .5;
            const double radius = std::max(.01, length(high - low) / 2),
                         aspect = double(options.settings.width) / options.settings.height;
            const double distance =
                radius / std::sin(c.verticalFov / 2) / std::min(1., aspect) * 1.2;
            c.position = c.target + normalized(Vec3{1, -1, .8}) * distance;
            c.yMag = radius * 1.2;
            c.nearClip = std::max(.00001, distance / 10000);
            c.farClip = std::max(1000., distance + radius * 3);
        }
    }
    renderCameraTransform(result.camera_);
    return result;
}
} // namespace sketchy
