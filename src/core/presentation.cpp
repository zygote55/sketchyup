#include "core/presentation.hpp"
namespace sketchy {
namespace {
CameraPathPtr requirePath(const Document &doc, Id id) {
    const auto found = doc.cameraPaths().find(id);
    if (found == doc.cameraPaths().end())
        throw std::runtime_error("Camera path does not exist");
    return found->second;
}
RenderPresetPtr requirePreset(const Document &doc, Id id) {
    const auto found = doc.renderPresets().find(id);
    if (found == doc.renderPresets().end())
        throw std::runtime_error("Render preset does not exist");
    return found->second;
}
} // namespace
Id createCameraPath(Document &doc, std::string name, std::vector<Id> keyframes,
                    CameraPathTiming timing) {
    const auto id = doc.nextCameraPathId();
    CameraPathRecord record{id, std::move(name),
                            static_cast<std::uint32_t>(doc.cameraPaths().size()),
                            std::move(keyframes), timing};
    validateCameraPathCapture(doc.scenes(), record);
    Edit edit{"Create camera path", {}};
    edit.cameraPaths.push_back({id, nullptr, std::make_shared<CameraPathRecord>(std::move(record))});
    doc.apply(std::move(edit), doc.revision());
    return id;
}
void updateCameraPath(Document &doc, Id id, std::string name, std::vector<Id> keyframes,
                      CameraPathTiming timing) {
    const auto before = requirePath(doc, id);
    auto after = std::make_shared<CameraPathRecord>(*before);
    after->name = std::move(name);
    after->keyframes = std::move(keyframes);
    after->timing = timing;
    // A broken path keeps its diagnosed references; only changed keyframes must be live.
    if (after->keyframes != before->keyframes)
        validateCameraPathCapture(doc.scenes(), *after);
    if (*after == *before)
        return;
    Edit edit{"Update camera path", {}};
    edit.cameraPaths.push_back({id, before, std::move(after)});
    doc.apply(std::move(edit), doc.revision());
}
void eraseCameraPath(Document &doc, Id id) {
    const auto before = requirePath(doc, id);
    Edit edit{"Delete camera path", {}};
    edit.cameraPaths.push_back({id, before, nullptr});
    for (const auto &[other, path] : doc.cameraPaths()) {
        if (path->position <= before->position)
            continue;
        auto after = std::make_shared<CameraPathRecord>(*path);
        --after->position;
        edit.cameraPaths.push_back({other, path, std::move(after)});
    }
    doc.apply(std::move(edit), doc.revision());
}
Id createRenderPreset(Document &doc, std::string name, RenderSettings settings) {
    const auto id = doc.nextRenderPresetId();
    auto preset = std::make_shared<RenderPresetRecord>(
        RenderPresetRecord{id, std::move(name), settings});
    Edit edit{"Create render preset", {}};
    edit.renderPresets.push_back({id, nullptr, std::move(preset)});
    doc.apply(std::move(edit), doc.revision());
    return id;
}
void updateRenderPreset(Document &doc, Id id, std::string name, RenderSettings settings) {
    const auto before = requirePreset(doc, id);
    auto after = std::make_shared<RenderPresetRecord>(*before);
    after->name = std::move(name);
    after->settings = settings;
    if (*after == *before)
        return;
    Edit edit{"Update render preset", {}};
    edit.renderPresets.push_back({id, before, std::move(after)});
    doc.apply(std::move(edit), doc.revision());
}
void eraseRenderPreset(Document &doc, Id id) {
    const auto before = requirePreset(doc, id);
    Edit edit{"Delete render preset", {}};
    edit.renderPresets.push_back({id, before, nullptr});
    doc.apply(std::move(edit), doc.revision());
}
} // namespace sketchy
