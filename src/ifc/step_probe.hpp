// The STEP probe of ADR 0172 (R098.a): what the OpenCASCADE STEP stack really does, as JSON on
// stdout. The IFC probes (probe.hpp) do the same for the pinned IfcOpenShell library; this one
// checks the claims about versions, units, schemas and the writer, so a library upgrade that
// changes any of it fails the test instead of being absorbed silently.
#pragma once

#include <string>
#include <vector>

namespace sketchyup::ifc {

// The facts of one root shape (the basic reader's result, in the file's local units).
struct StepShapeFacts {
  int index = 0;
  long long solids = 0;
  long long faces = 0;
  std::vector<double> box;  // x1 y1 z1 x2 y2 z2
  bool ok = false;
};

// What the model says about the file (the import's source of truth).
struct StepModelFacts {
  bool read = false;
  std::string readStatus;  // done / fail / error / exception / no-model
  std::string schema;  // the FILE_SCHEMA identifiers of the model's header
  bool unitsDeclared = false;  // the file has a LENGTH_UNIT declaration (the raw text says so)
  std::string lengthUnit;  // the unit names as the model reads them
  std::string angleUnit;
  std::string solidAngleUnit;
  double lengthMetres = 1.0;  // the file's local length unit as a factor of metres (1.0: none declared)
  long long entities = 0;
  long long roots = 0;
  long long transferred = 0;
  std::vector<StepShapeFacts> shapes;
  std::string error;
};

// Read a STEP file with the basic OCCT reader (STEPControl_Reader). Never throws: the failure
// ends up in readStatus / error. lengthMetres is the model's LocalLengthUnit: the file's local
// length unit as a factor of metres, 1.0 when the file declares no unit (the reader's default
// is a metre). The shapes are the roots as transferred, un-scaled (in the file's local unit).
StepModelFacts readStepModel(const std::string& path);

// The raw-text facts (the ADR 0172 unit policy reads the file as written): the ISO-10303-21
// header, the FILE_SCHEMA name and its family (ap203 / ap214 / ap242 / other) and the unit
// declarations. lengthMetres is meaningful only when lengthDeclared.
struct StepFileFacts {
  bool readable = false;
  long long bytes = 0;
  bool isoHeader = false;
  std::string schemaRaw;
  std::string schema;
  bool lengthDeclared = false;
  std::string length;
  std::string angle;
  std::string solidAngle;
  double lengthMetres = 1.0;
};

StepFileFacts stepFileFacts(const std::string& path);

struct StepProbeOptions {
  std::string file;
  // When non-empty: write a 10 x 20 x 30 box through the basic STEP writer into this
  // directory (once with the default schema, once with the AP242 option) and read both files
  // back. This settles, on every run, what schema the writer actually writes (ADR 0172: AP242
  // is not claimed as writer output; the round trip shows what the file really says).
  std::string roundTripDirectory;
};

// --probe-step: worker and OCCT versions (runtime and configured), the file's header,
// FILE_SCHEMA (raw text and the model's), the declared units (raw text and the model's), the
// entity and root counts and the root shapes (solid/face counts and bounding boxes). Never
// throws.
std::string probeStep(const StepProbeOptions& options);

// Like probeStep, but runs and reports the exit code the import will use: 0 when the file was
// read and at least one shape transferred, 1 otherwise.
int runStepProbe(const StepProbeOptions& options);

// --write-step: the self-generated STEP corpus of ADR 0172 §7 (a 10 x 20 x 30 box, a cylinder
// of radius 5 and height 20, and a two-shape assembly of the two), written in millimetres
// through the basic writer into the directory. Returns a JSON list of the written files.
std::string writeStep(const std::string& directory);

}  // namespace sketchyup::ifc
