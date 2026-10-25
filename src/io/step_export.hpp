#pragma once
// R098.c (ADR 0172 section 8): the application's side of the STEP export. The application captures the open document
// into a bounded export package, the isolated helper (sketchyup-ifc-worker --export-step) writes the STEP file from it
// through the XDE writer to a private temporary file, and the application checks what came back and publishes it with
// the non-replacing publisher the other exporters use. Nothing here touches the document, and no geometry kernel is
// involved: every body is written as its own tessellation, one planar face per native face, so a closed body is a
// solid and an open one a shell.
//
// Export package, little-endian (the IFC export's container, with its own magic and manifest):
//   "SKSTPX01"              8 bytes
//   u32 manifest bytes      at most 32 MiB
//   u32 reserved            zero
//   u64 mesh bytes          at most 128 MiB
//   manifest                UTF-8 JSON object
//   mesh chunk              per mesh, in the manifest's order: f64 x3 per vertex, u32 x3 per triangle (0-based vertex
//                           indices, counter-clockwise from outside), u32 per triangle (its face, 0 to faces - 1),
//                           u32 per face (its colour, an index into the mesh's colours) and f32 x3 per colour.
// Manifest members (all required): format "sketchyup-step-export-package", version 1, meshSha256 (the digest of the
// mesh chunk), application {name, version}, fileName, meshes [{vertices, triangles, faces, colors, closed}] and nodes
// [{role ("assembly" or "element"), name, layer, parent, mesh, placement, hidden}]. A node's placement is
// [origin x y z, Axis x y z, RefDirection x y z] relative to its parent's frame (the top level: the world); an element's
// mesh is in its own frame; a parent precedes its children, and only an assembly is a parent.
#include "core/model.hpp"
#include "io/ifc_worker.hpp"

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include <optional>
#include <stdexcept>
#include <string>

namespace sketchy {

struct StepExportOptions {
    // "ap214" (the default: AP214 CC2 DIS) or "ap203". AP242 writing is not supported and is refused.
    QString schema{"ap214"};
};

// A refusal or failure, with the report so far when there is one.
class StepExportError : public std::runtime_error {
  public:
    StepExportError(std::string code, const std::string &message, QJsonObject report = {})
        : std::runtime_error(message), code_(std::move(code)), report_(std::move(report)) {}
    const std::string &code() const { return code_; }
    const QJsonObject &report() const { return report_; }

  private:
    std::string code_;
    QJsonObject report_;
};

struct StepExportCapture {
    QByteArray package;
    // What the export will write, computed from the document alone; the report's `plan` member.
    QJsonObject plan;
};

// Captures the document. Throws StepExportError("emptyModel") when nothing can be written, "meshTooLarge" for a body
// over 100,000 triangles, "tooManyTriangles" beyond 1,000,000, and "packageTooLarge" when the package would pass its
// bounds.
StepExportCapture captureStepExport(const Document &document, const QString &fileName);

struct StepExportResult {
    QJsonObject report;
    qint64 bytes{};
};

// The whole export: capture, helper, checks, publication at `path` (which must not exist). Throws StepExportError; the
// document and an existing file are never touched.
StepExportResult exportStepFile(const Document &document, const QString &path, const StepExportOptions &options,
                                const IfcWorkerOptions &worker);

} // namespace sketchy
