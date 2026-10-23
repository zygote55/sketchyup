#pragma once
// R097.d: the application's side of the IFC4 export (ADR 0171 sections 3.8 and 4, decisions 2, 3, 4,
// 13 and 16). The application captures the open document into a bounded export package, the
// isolated helper (sketchyup-ifc-worker --export) builds the IFC4 file from it with IfcOpenShell's
// typed writer and writes it to a private temporary file, and the application checks what came back
// and publishes it with the non-replacing publisher the other exporters use. Nothing here touches
// the document, and no geometry kernel is involved: every body is written as one
// IfcTriangulatedFaceSet of its own tessellation.
//
// Export package, little-endian ("the same container as the import package", ADR 0171 3.8):
//   "SKIFCX01"              8 bytes
//   u32 manifest bytes      at most 32 MiB
//   u32 reserved            zero
//   u64 mesh bytes          at most 128 MiB
//   manifest                UTF-8 JSON object
//   mesh chunk              per mesh, in the manifest's order: f64 x3 per vertex, u32 x3 per
//                           triangle (0-based vertex indices, counter-clockwise from outside) and,
//                           when the mesh has colours, u32 per triangle (an index into the colour
//                           table) and f32 x3 per colour.
// Manifest members (all required): format "sketchyup-ifc-export-package", version 1, meshSha256,
// documentId, application {name, version}, timestamp (UTC, YYYY-MM-DDTHH:MM:SS), fileName,
// georeference (null, or {crs, eastings, northings, height}), styles [{name, color, opacity,
// material}], meshes [{vertices, triangles, colors, closed}] and nodes [{id, role, class, guid,
// name, longName, objectType, predefinedType, parent, placement, mesh, style, hidden, elevation,
// props}]. A node's placement is [origin x y z, Axis x y z, RefDirection x y z] relative to its
// parent's placement; its mesh is in its own frame; its parent precedes it.
#include "core/model.hpp"
#include "io/ifc_worker.hpp"
#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <optional>
#include <stdexcept>
namespace sketchy {
struct IfcExportOptions {
    // The time stamped into the file header, UTC seconds since the epoch. Unset: SOURCE_DATE_EPOCH when
    // the environment sets it, otherwise the clock. A fixed value makes two exports of one document
    // byte for byte equal (check G3).
    std::optional<qint64> timestamp;
};
// A refusal or failure, with the report so far when there is one.
class IfcExportError : public std::runtime_error {
  public:
    IfcExportError(std::string code, const std::string &message, QJsonObject report = {})
        : std::runtime_error(message), code_(std::move(code)), report_(std::move(report)) {}
    const std::string &code() const { return code_; }
    const QJsonObject &report() const { return report_; }

  private:
    std::string code_;
    QJsonObject report_;
};
struct IfcExportCapture {
    QByteArray package;
    // What the export will write and what it will not, computed from the document alone (the same
    // numbers as the report's `written`, `notWritten`, `accounting` and `georeference` members);
    // the desktop dialog shows it before anything is written.
    QJsonObject plan;
};
// Captures the document. `fileName` is the name stamped into the header (the destination's file
// name). Throws IfcExportError("emptyModel") when nothing can be written and
// IfcExportError("packageTooLarge") when the package would pass its bounds.
IfcExportCapture captureIfcExport(const Document &document, const IfcExportOptions &options,
                                  const QString &fileName);
// The plan alone.
QJsonObject planIfcExport(const Document &document);
// Whether a CRS label looks like an authority code ("EPSG:32760"): letters, a colon, an identifier.
bool isAuthorityCode(const std::string &label);
// The deterministic GlobalId of decision 16: the compressed first 128 bits of SHA-256 over a fixed
// prefix, the document identity, a kind and the body's stable ID; `attempt` > 0 only after a
// collision with another identifier of the same export.
std::string derivedIfcGlobalId(const std::string &identity, const std::string &kind, Id id, int attempt = 0);
// 22 characters of 0-9A-Za-z_$ whose first character is 0 to 3.
bool validIfcGlobalId(const std::string &guid);
struct IfcExportResult {
    QJsonObject report;
    qint64 bytes{};
};
// The whole export: capture, helper, checks, publication at `path` (which must not exist). Throws
// IfcExportError; the document and an existing file are never touched.
IfcExportResult exportIfcFile(const Document &document, const QString &path, const IfcExportOptions &options,
                              const IfcWorkerOptions &worker);
} // namespace sketchy
