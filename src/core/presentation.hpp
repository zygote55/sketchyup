#pragma once
#include "core/model.hpp"
#include "core/presentation_records.hpp"
namespace sketchy {
// Ordinary undoable model edits, one history entry each (ADR 0173 §2, §3).
Id createCameraPath(Document &doc, std::string name, std::vector<Id> keyframes,
                    CameraPathTiming timing = {});
// Renaming or retiming a broken path is allowed; new keyframes must name scenes with cameras.
void updateCameraPath(Document &doc, Id path, std::string name, std::vector<Id> keyframes,
                      CameraPathTiming timing);
void eraseCameraPath(Document &doc, Id path);
Id createRenderPreset(Document &doc, std::string name, RenderSettings settings = {});
void updateRenderPreset(Document &doc, Id preset, std::string name, RenderSettings settings);
void eraseRenderPreset(Document &doc, Id preset);
} // namespace sketchy
