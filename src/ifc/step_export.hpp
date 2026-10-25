// The worker's STEP export (R098.c, ADR 0172). The application captures the document into the
// SKIFCX01 package (src/io/step_export.hpp) and the helper writes the STEP file from it.
//
// What the writer does (verified against OCCT 7.9.3 in the pinned image, 2026-10-25):
// - The XDE writer with DESTEP_Parameters::WriteMode_StepSchema_AP214DIS writes
//   FILE_SCHEMA(('AUTOMOTIVE_DESIGN { 1 2 10303 214 0 1 1 1 }')) - the AP214 CC2 DIS schema;
//   WriteMode_StepSchema_AP203 writes CONFIG_CONTROL_DESIGN. The default (no mode) is AP214IS,
//   so the mode is set explicitly and the written FILE_SCHEMA is checked afterwards.
// - Lengths are written in the file's declared unit; with WriteUnit=Millimeter the file
//   declares MILLI and the coordinates are the millimetres the package carries - never
//   rescaled.
// - A shape added with a location writes its placement explicitly (CONTEXT_DEPENDENT_SHAPE_
//   REPRESENTATION + ITEM_DEFINED_TRANSFORMATION); a shape already in the document is written
//   as a reference to its first label, so repeated geometry becomes shared instances.
// - XCAF attributes the reader reads: TDataStd_Name (a part's name is read from its base's
//   label, an assembly's from its own), TDataStd_NamedData (metadata from the base),
//   XCAFDoc_ColorTool (from the base), XCAFDoc_LayerTool (from the base).
// - The writer segfaults when a reference label carries a visibility attribute, and it writes
//   every layer as 'visible': hidden state is not representable through it, so it is reported
//   as hiddenNotPreserved, never silently dropped.
#pragma once

#include <string>

namespace sketchyup::ifc {

struct StepExportOptions {
  std::string input;  // the export package (SKIFCX01)
  std::string output;  // the .step file
  std::string schema;  // "ap214" (default) or "ap203"
};

// 0 = the file was written; 3 = refused (the message was already reported on stdout).
int runExportStep(const StepExportOptions& options);

}  // namespace sketchyup::ifc
