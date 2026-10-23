// The STEP probe of ADR 0172 (R098.a): what the OpenCASCADE STEP stack really does, as JSON on
// stdout. The IFC probes (probe.hpp) do the same for the pinned IfcOpenShell library; this one
// checks the claims about units, schemas and the writer, so a library upgrade that changes any
// of it fails the test instead of being absorbed silently.
#pragma once

#include <string>

namespace sketchyup::ifc {

struct StepProbeOptions {
  std::string file;
  // When non-empty: build a box, write it through the basic STEP writer into this directory and
  // read it back. This settles, on every run, what schema the writer actually writes (ADR 0172:
  // AP242 is not claimed as writer output; the round trip shows what the file really says).
  std::string roundTripDirectory;
};

// --probe-step: worker and OCCT versions, the file's header, FILE_SCHEMA (raw text and the
// model's), the declared units (raw text and the model's), the entity and root counts and the
// root shapes (solid/face counts and bounding boxes). Never throws.
std::string probeStep(const StepProbeOptions& options);

// Like probeStep, but runs and reports the exit code the import will use: 0 when the file was
// read and at least one shape transferred, 1 otherwise.
int runStepProbe(const StepProbeOptions& options);

}  // namespace sketchyup::ifc
