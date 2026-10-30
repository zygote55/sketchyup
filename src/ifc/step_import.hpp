// The STEP import of ADR 0172 (R098.a): the file's metadata through the basic OCCT reader and
// the unit policy of §2, and (R098.a2) the tessellated import - the reader's transferred
// shapes tessellated with BRepMesh and shipped in the conversion package. Everything is
// reported; nothing is ever silently rescaled and a unit is never guessed.
#pragma once

#include <string>
#include <vector>

namespace sketchyup::ifc {

struct StepImportOptions {
  std::string file;
  // The ADR 0172 §2 unit policy: used only when the file declares no length unit (mm, cm,
  // m, in or ft). An unrecognised value is a refusal, not a guess.
  std::string lengthUnitOverride;
  // The ADR 0172 §3 tessellation defaults: 1 mm (the IFC chordDeviationMm default) and
  // 30 degrees (the IFC tessellation default). The reader's unit context is applied at
  // read time (the geometry comes out in the file's unit, normalized to millimetres), so
  // the deflection is a maximum chord deviation in millimetres.
  double deflectionMm = 1.0;
  double angleDeg = 30.0;
  // The ADR 0172 §6 triangle caps (100,000 per element, 1,000,000 in total). The test
  // options lower them to check the refusal and the per-element report.
  long long maxTrianglesPerElement = 100000;
  long long maxTotalTriangles = 1000000;
  // R098.k (ADR 0172 section 6): the entity cap, as the IFC import's. A file whose reader holds more entities is refused
  // before any transfer or tessellation, naming the count and the limit. The tests lower it.
  long long maxEntities = 2000000;
  // R098.k (ADR 0172 section 6): the threads the tessellation may use (BRepMesh in parallel above 1, at most 4).
  int threads = 1;
  // Empty: report the metadata only (the R098.a behaviour). Set: also tessellate the
  // transferred solids and write the conversion package.
  std::string output;
  // R098.i (ADR 0172 section 5): the bases left out, by their index in the tessellation's order (the
  // value of its `P at` line). The application runs the helper again with the base it stopped on
  // here. A left-out base is located as excluded and counted as omitted; a group is never left out.
  std::vector<int> excludeSteps;
};

// --import-step: read the file and report its metadata as JSON (schema, the units with the
// conversion to the document's millimetres, entity counts, root shapes and per-shape
// bounding boxes in millimetres). Never throws; the status carries the refusal.
std::string importStep(const StepImportOptions& options);

// Like importStep, but runs and reports the exit code the import will use: 0 when the file
// was read (with or without transferred shapes), 1 when the read failed, 2 when the file
// was refused (nothing is created either way).
int runStepImport(const StepImportOptions& options);

// The XDE import (R098.a2's tessellated import, R098.b's mapping, ADR 0172 §3 and §4): the
// file is transferred through the XDE reader (STEPCAFControl_Reader) into a
// TDocStd_Document - the product structure (the assemblies and the repeated shapes, the
// XDE's assembly / part / reference labels), the file's colours (XCAFDoc_ColorTool),
// its layers (XCAFDoc_LayerTool) and its named data (TDataStd_NamedData) come with it.
// The unique base shapes are tessellated once, with BRepMesh at the options' deflection
// and angle, a part bakes its placement into its mesh (the package's node transform
// admits affine transforms only, and a mirrored placement is not affine) and reuses the
// base's mesh when its placement is the identity; the package is the §4 tree - the root,
// an assembly a group, a part an element - shipped through the IFC package writer.
// Progress lines and the final R line follow the IFC import: 0 when the package was
// written, 3 on a refusal or a failure (nothing is created, and the refusal names what
// it is).
int runStepImportPackage(const StepImportOptions& options);

}  // namespace sketchyup::ifc
