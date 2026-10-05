#pragma once
#include "core/selection.hpp"
#include <QJsonObject>
namespace sketchy {
struct RenderSettings {
    int width{1024}, height{768}, samples{32}, seed{};
};
struct RenderCamera {
    Vec3 position{8, -8, 6}, target{}, up{0, 0, 1};
    bool orthographic{};
    double verticalFov{0.7853981633974483}, yMag{5}, nearClip{.01}, farClip{1000};
};
struct RenderOptions {
    RenderSettings settings;
    std::optional<RenderCamera> camera;
};
void validateRenderSettings(const RenderSettings &settings);
Transform renderCameraTransform(const RenderCamera &camera);
QJsonObject describeRenderSettings(const RenderSettings &settings);
QJsonObject describeRenderCamera(const RenderCamera &camera);
RenderOptions parseRenderOptions(const QJsonObject &json);
class RenderSnapshot {
  public:
    // Capture on the document's owner thread. Export may run on a worker using
    // only this immutable value; no live document or editor references are held.
    static RenderSnapshot capture(const Document &document, RenderOptions options = {},
                                  SelectionSet hidden = {});
    const Document &document() const { return document_; }
    const RenderSettings &settings() const { return settings_; }
    const RenderCamera &camera() const { return camera_; }
    bool visible(Id body, Id face = 0) const;

  private:
    RenderSnapshot(Document document, RenderSettings settings, RenderCamera camera,
                   SelectionSet hidden);
    Document document_;
    RenderSettings settings_;
    RenderCamera camera_;
    SelectionSet hidden_;
};
} // namespace sketchy
