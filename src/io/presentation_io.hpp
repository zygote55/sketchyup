#pragma once
#include "core/presentation_records.hpp"
#include <QJsonObject>
#include <QString>
namespace sketchy {
class Document;

// The output bounds a batch is planned against (ADR 0173 §5): one frame image, its metadata and
// the whole output. The batch engine and the presentation.describe query both read them.
inline constexpr qint64 presentationFrameImageLimit = 64LL * 1024 * 1024;
inline constexpr qint64 presentationMetadataLimit = 1024LL * 1024;
inline constexpr qint64 presentationOutputLimit = 2048LL * 1024 * 1024;
// The worst-case bytes of one frame: the PNG bound (about five bytes a pixel, capped) plus
// metadata.
qint64 presentationFrameBudget(const RenderSettings &settings);
// The folder of a preset: its 1-based number, then its name with every unsafe character replaced.
QString presentationPresetDirectory(size_t index, const QString &name);
// The schema-30 `presentation` payload object (ADR 0173 §3): the camera path table with its
// allocator and the render preset table with its allocator. Decoding validates every record
// and budget; a missing scene is not a decode error (the path loads as broken).
struct PresentationTables {
    CameraPathRecords cameraPaths;
    Id nextCameraPathId{1};
    RenderPresetRecords renderPresets;
    Id nextRenderPresetId{1};
};
PresentationTables presentationTablesOf(const Document &doc);
QJsonObject encodePresentation(const PresentationTables &tables);
PresentationTables decodePresentation(const QJsonValue &value);
} // namespace sketchy
