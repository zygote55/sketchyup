#pragma once
#include "core/render_settings.hpp"
#include "core/selection.hpp"
#include "core/source_credits.hpp"
#include "integrations/render_environment.hpp"
#include <QJsonObject>
namespace sketchy {
struct RenderCamera {
    Vec3 position{8, -8, 6}, target{}, up{0, 0, 1};
    bool orthographic{};
    double verticalFov{0.7853981633974483}, yMag{5}, nearClip{.01}, farClip{1000};
};
struct RenderOptions {
    RenderSettings settings;
    std::optional<RenderCamera> camera;
    std::optional<RenderEnvironment> environment;
};
// Bodies a capture may hold (ADR 0032). R099.f keeps it: a 20,000-body capture already takes
// 1.7 s to prepare (p50), over the 1 s per-frame budget, so no higher limit is within budget.
inline constexpr std::size_t renderSnapshotBodyLimit = 20000;
RenderCamera renderSceneCamera(const SceneCamera &camera);
Transform renderCameraTransform(const RenderCamera &camera);
QJsonObject describeRenderSettings(const RenderSettings &settings);
QJsonObject describeRenderCamera(const RenderCamera &camera);
RenderOptions parseRenderOptions(const QJsonObject &json);
// Result of RenderSnapshot::captureDrawingView. A broken capture has named reasons
// and no snapshot; it never throws for a missing scene or an unsupported scene setting.
struct DrawingViewCapture;
class RenderSnapshot {
  public:
    // Capture on the document's owner thread. Export may run on a worker using
    // only this immutable value; no live document or editor references are held.
    static RenderSnapshot capture(const Document &document, RenderOptions options = {},
                                  SelectionSet hidden = {});
    static RenderSnapshot captureSavedScene(const Document &document, Id scene,
                                            RenderOptions options = {});
    // Scene-based capture for drawing-sheet views (ADR 0163). Unlike
    // captureSavedScene it tolerates and counts missing scene references, reports a
    // missing scene, a scene without a camera, a temporary clip plane and show-hidden
    // as broken reasons instead of throwing, and honours hidden edges as well as
    // hidden bodies and faces. options.camera, when given, replaces the scene camera
    // (the drawing-view mapping uses it to apply the paper-space pan); the scene must
    // still have a camera. Work-limit failures still throw.
    static DrawingViewCapture captureDrawingView(const Document &document, Id scene,
                                                 RenderOptions options = {});
    const std::string &sourceIdentity() const { return sourceIdentity_; }
    std::uint64_t sourceRevision() const { return sourceRevision_; }
    std::optional<Id> sourceScene() const { return sourceScene_; }
    const Document &document() const { return document_; }
    const RenderSettings &settings() const { return settings_; }
    const RenderCamera &camera() const { return camera_; }
    const std::optional<RenderEnvironment> &environment() const { return environment_; }
    bool visible(Id body, Id face = 0) const;
    bool edgeVisible(Id body, Id edge) const;

  private:
    RenderSnapshot(Document document, RenderSettings settings, RenderCamera camera,
                   SelectionSet hidden);
    Document document_;
    std::string sourceIdentity_;
    std::uint64_t sourceRevision_{};
    std::optional<Id> sourceScene_;
    RenderSettings settings_;
    RenderCamera camera_;
    std::optional<RenderEnvironment> environment_;
    SelectionSet hidden_;
};
// R107.c2 (ADR 0181 §3.3): the DEM notices of the terrain a scene shows: each DEM body that has a
// visible face in the snapshot. A document without DEM terrain, or a scene that hides it, gives
// none.
SourceCredits sceneDemCredits(const Document &document, const RenderSnapshot &snapshot);

struct DrawingViewCapture {
    std::optional<RenderSnapshot> snapshot; // Absent when broken.
    // Stable broken-reason codes, in this order when present: sceneMissing,
    // sceneHasNoCamera, temporaryClipPlane, showHidden.
    std::vector<std::string> broken;
    size_t missingReferences{}; // Warnings: references the scene names that are gone.
    bool perspective{};         // The scene camera is perspective (not to scale).
};
} // namespace sketchy
