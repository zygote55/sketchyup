#pragma once
// R102.b (ADR 0176 section 4): the application's side of the SketchUp (.skp) import. A decoded
// SKSKPP01 package becomes one new unsaved document in one edit labelled "Import .skp file". The
// package is data: nothing in it is trusted beyond the checked decoder's guarantees, and every
// element the native model cannot take is refused with a notice and counted in the report.
#include "core/model.hpp"
#include "geometry/surface.hpp"
#include "io/ifc_import.hpp"
#include "io/skp_package.hpp"

#include <QJsonObject>
#include <functional>

namespace sketchy {

// The import has no options (ADR 0176 section 9): the dialog and the CLI add none.
// The product takes no options. The one field is a test bound: a body over it is split (R115 keeps the product
// value, Surface::bodyLimit, so the split is reached in a test with a lowered value).
struct SkpImportOptions {
  std::size_t bodyLimit{Surface::bodyLimit};
};

struct SkpImport {
    Document document;
    QJsonObject report;
};

// Checked between elements; true cancels with an IfcImportError("cancelled").
struct SkpConversionHooks {
    std::function<bool()> cancelled;
};

// One document, one "Import .skp file" edit. The root's geometry goes into a top-level group named
// after the file; each definition becomes a component definition and each placement an instance.
// Throws IfcImportError for a refusal (an unusable package) or a cancel.
SkpImport importSkpPackage(const SkpPackage &package, const SkpImportOptions &options = {},
                           const SkpConversionHooks &hooks = {});

} // namespace sketchy
