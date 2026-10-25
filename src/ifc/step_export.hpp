// The worker's STEP export (R098.c, ADR 0172 section 8). The application captures the document into
// the SKSTPX01 package (src/io/step_export.hpp documents the layout) and the helper writes the STEP
// file from it through the XDE writer (STEPCAFControl_Writer), so the assemblies, the instances, the
// names, the colours and the layers survive.
//
// What the writer does (verified against OCCT 7.9.3 in the pinned image):
// - Schema: DESTEP_Parameters::WriteSchema selects AP214DIS (the default) or AP203. AP242 is refused.
//   The written FILE_SCHEMA is checked afterwards.
// - Units: WriteUnit is millimetres; the file declares MILLI and the coordinates are the package's
//   millimetres, never rescaled.
// - Assemblies: a group is an assembly label; an element is a part label, referenced by a component
//   with the element's placement as its location (AddComponent). Several components of one part are
//   several instances of it.
// - Attributes the reader takes from the labels: names (a part's from its own label, an assembly's
//   from its own), colours (ColorGen on the part, or ColorSurf per face when the faces differ), the
//   layer (LayerTool on the part) and visibility (ColorTool::SetVisibility on a part or an assembly
//   label). A reference (component) label never carries an attribute: the writer segfaults on one.
#pragma once

#include <string>

namespace sketchyup::ifc {

struct StepExportOptions {
  std::string input;   // the export package (SKSTPX01)
  std::string output;  // the .step file
  std::string schema;  // "ap214" (default) or "ap203"
};

// 0 = the file was written; 3 = refused (the message was already reported on stdout).
int runExportStep(const StepExportOptions& options);

}  // namespace sketchyup::ifc
