#pragma once
// ADR 0166 §6.1 (R094.f): the configurable assemblies that ship with SketchyUp are built at
// build time from text sources in resources/assemblies/ into .sketchylib bundles.
#include "core/component_parameter_records.hpp"
#include "io/library_bundle.hpp"
#include <QJsonArray>
namespace sketchy {
// One source file: library metadata, the assembly identity and one transaction batch in the
// public transaction format that builds the geometry and the component, defines the
// parameters and drivers and sets the glue face.
//   {"format": "sketchyup-assembly-source", "version": 1, "license": "MIT",
//    "assembly": {"id": "sketchyup/interior-door", "version": 1},
//    "name": "...", "description": "...", "labels": [...], "units": "mm" | "m",
//    "commands": [...]}
struct AssemblySource {
    ComponentAssembly identity;
    TemplateMetadata metadata;
    DisplayUnit units{DisplayUnit::Millimeters};
    QJsonArray commands;
};
// Throws std::runtime_error naming the first malformed field.
AssemblySource parseAssemblySource(const QByteArray &json);
// Applies the batch to a new document and stamps the assembly identity on the one top-level
// placement's definition. `definition` receives that definition.
Document buildAssemblyDocument(const AssemblySource &source, Id &definition);
// A 320 × 240 PNG of the placement's technical lines in an isometric view, drawn with Qt Gui
// only (the measured-drawing hidden-line pass and QPainter on a QImage). No text is drawn, so
// the bytes do not depend on installed fonts.
QByteArray drawAssemblyThumbnail(const Document &document);
// The captured document's identity: 32 hex digits of SHA-256 over the assembly identity, so
// the same source builds the same bytes every time.
std::string assemblyDocumentIdentity(const ComponentAssembly &identity);
// The whole build: parse, build, draw and encode. Deterministic for a given source and writer.
QByteArray buildAssemblyBundle(const QByteArray &sourceJson);
} // namespace sketchy
