#pragma once
#include <QJsonObject>
#include <QString>
namespace sketchy {
class Document;
// Verify and atomically publish a new native container; never replace an existing path.
QJsonObject createNativeFile(const Document &document, const QString &output);
// The newest schema this build reads and can write (ADR 0174 section 3): a material with a
// real-world texture size, with required feature material-texture-size-v1.
inline constexpr int nativeDocumentVersion = 32, nativeContainerVersion = 2;
// The schema of the R100.a edge stroke on the style (ADR 0174 section 2), the newest one before 32.
inline constexpr int nativeEdgeStyleDocumentVersion = 31;
// The schema of the R099.a presentation records (ADR 0173 §3), the newest one before 31.
inline constexpr int nativePresentationDocumentVersion = 30;
// The newest schema SketchyUp 1.3 reads (sketchyup-document-v29).
inline constexpr int nativeTerrainDocumentVersion = 29;
// The newest schema SketchyUp 1.0 reads (sketchyup-document-v26).
inline constexpr int nativeSheetFreeDocumentVersion = 26;
// The newest schema SketchyUp 1.1 reads (sketchyup-document-v27).
inline constexpr int nativeParameterFreeDocumentVersion = 27;
// The newest schema SketchyUp 1.2 reads (sketchyup-document-v28).
inline constexpr int nativeTerrainFreeDocumentVersion = 28;
// The writer emits the lowest schema the content needs (ADR 0163 decision 2, ADR 0165
// §5): a document with no drawing sheets, views or items and no component parameter
// records stays schema 26, without the drawing-sheets-v1 feature, so SketchyUp 1.0 can
// still open it. Sheets without parameter records are schema 27, which SketchyUp 1.1
// opens. Any parameter record (or override) is schema 28. A terrain or a site origin
// is schema 29 with required feature terrain-v1 (ADR 0168 §2), which 1.2 refuses with
// its newer-format error. A camera path or render preset is schema 30 with required
// feature presentation-v1 (ADR 0173 §3); a 1.3 reader refuses it with the same error. An
// edge stroke on the style is schema 31 with required feature edge-styles-v1 (ADR 0174 §2),
// refused by 1.3 and 1.2 with the same newer-format path. A material with a texture size is
// schema 32 with required feature material-texture-size-v1 (ADR 0174 section 3), refused by
// the released readers with the same container refusal. Allocator floors alone never raise
// the schema.
int nativeDocumentVersionFor(const Document &document);
// Fully decode and validate before reporting any file as valid. Never rewrite input.
QJsonObject inspectNativeFile(const QString &path);
// Publishes a verified current container to a new path without replacing any file.
QJsonObject migrateNativeFile(const QString &input, const QString &output);
QJsonObject nativeFormatCapabilities();
} // namespace sketchy
