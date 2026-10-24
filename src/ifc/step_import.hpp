// The STEP import skeleton of ADR 0172 (R098.a): the file's metadata through the basic OCCT
// reader and the unit policy of §2, with no document conversion yet (the tessellated import
// builds on this). Everything is reported; nothing is ever silently rescaled.
#pragma once

#include <string>

namespace sketchyup::ifc {

struct StepImportOptions {
  std::string file;
  // The ADR 0172 §2 unit policy: used only when the file declares no length unit (mm, cm,
  // m, in or ft). An unrecognised value is a refusal, not a guess.
  std::string lengthUnitOverride;
};

// --import-step: read the file and report its metadata as JSON (schema, the units with the
// conversion to the document's millimetres, entity counts, root shapes and per-shape
// bounding boxes in millimetres). Never throws; the status carries the refusal.
std::string importStep(const StepImportOptions& options);

// Like importStep, but runs and reports the exit code the import will use: 0 when the file
// was read (with or without transferred shapes), 1 when the read failed, 2 when the file
// was refused (nothing is created either way).
int runStepImport(const StepImportOptions& options);

}  // namespace sketchyup::ifc
