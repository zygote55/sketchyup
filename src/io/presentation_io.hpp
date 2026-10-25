#pragma once
#include "core/presentation_records.hpp"
#include <QJsonObject>
namespace sketchy {
class Document;
// The schema-30 `presentation` payload object (ADR 0173 §3): the camera path table with its
// allocator and the render preset table with its allocator. Decoding validates every record
// and budget; a missing scene is not a decode error (the path loads as broken).
struct PresentationTables {
    CameraPathRecords cameraPaths;
    Id nextCameraPathId{1};
    RenderPresetRecords renderPresets;
    Id nextRenderPresetId{1};
};
QJsonObject encodePresentation(const Document &doc);
PresentationTables decodePresentation(const QJsonValue &value);
} // namespace sketchy
