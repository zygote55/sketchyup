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
  std::string lengthUnit;  // the unit the file's unit context assigns (FileUnits: "" = none)
  std::string angleUnit;
  std::string solidAngleUnit;
  bool lengthUnitResolved = false;  // the model resolved a length unit: the reader applied the
                                   // file's unit to the geometry (the shapes come out in mm)
  double lengthMetres = 0.0;  // the factor of metres of the unit the model resolves, from the
                              // name (0.0: not resolved, or the name is not a known unit)
  long long entities = 0;
  long long roots = 0;
  long long transferred = 0;
  std::vector<StepShapeFacts> shapes;
  std::string error;
};

// Read a STEP file with the basic OCCT reader (STEPControl_Reader). Never throws: the failure
// ends up in readStatus / error. lengthUnitResolved is the unit the file's unit context
// assigns (FileUnits: the combined context wrappers only - a bare GLOBAL_UNIT_ASSIGNED_CONTEXT
// is not resolved; a conversion-based file resolves the conversion unit, not the base unit the
// raw text's first LENGTH_UNIT stands for): when resolved, TransferRoots has normalized the
// geometry to the cascade unit (millimetre by default) - a metre-declaring box of 10 x 20 x 30
// comes out 10000 x 20000 x 30000. A file the reader resolves no unit for comes out in the
// file's own units. A bare ReadFile never sets the model's LocalLengthUnit (SystemLengthUnit
// stays 1.0; the DESTEP XCAF provider is the only thing that sets it), so the factor is
// resolved from the FileUnits name. The shapes are the roots as transferred, un-scaled by us.
StepModelFacts readStepModel(const std::string& path);

// The raw-text facts (the ADR 0172 unit policy reads the file as written): the ISO-10303-21
// header, the FILE_SCHEMA name and its family (ap203 / ap214 / ap242 / other) and the unit
// declarations - the first one in the text, which for a conversion-based file is the
// conversion's base, not the unit in use (the model's resolution carries that). lengthMetres
// is 0.0 when declared but not convertible from the raw text; it is the import's fallback for
// a file the model resolves no unit for.
struct StepFileFacts {
  bool readable = false;
  long long bytes = 0;
  bool isoHeader = false;
  std::string schemaRaw;
  std::string schema;
  bool lengthDeclared = false;
  std::string length;  // the first LENGTH_UNIT declaration as written (a conversion-based file:
  // the conversion's base, e.g. "millimetre"; the conversion unit's name if it is first)
  std::string angle;
  std::string solidAngle;
  double lengthMetres = 0.0;  // the raw text's declaration as a factor of metres (0.0: not
  // convertible from the raw text; the model's factor stands)
  std::string sha256;  // the file's digest (the provenance's step.Source, ADR 0172 §4)
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
