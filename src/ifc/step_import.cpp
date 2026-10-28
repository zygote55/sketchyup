// The STEP import (ADR 0172, R098.a, R098.a2 and R098.b): the file's metadata through the
// basic OCCT reader and the unit policy of §2, and the package import - R098.b's XDE tree
// (the assemblies, the instances, the names, the colours, the layers and the provenance)
// tessellated with BRepMesh and shipped in the conversion package. Nothing is ever silently
// rescaled and a unit is never guessed.
//
// The tessellation (R098.a2, ADR 0172 §3) uses the OpenCASCADE 7.9.3 API as installed
// (the pinned build's headers; the same spelling the probe records):
//  - BRepMesh_IncrementalMesh(shape, linDeflection, isRelative, angDeflection, isInParallel)
//    tessellates and performs in one step (deflection in the cascade unit, millimetres, the
//    angle in radians);
//  - BRep_Tool::Triangulation(face, loc) hands the face's triangulation with the face
//    location; the triangulation's nodes are in the face's local (surface) coordinates, so
//    a node's global position is node.Transformed(loc.Transformation()) (a no-op for the
//    identity location a transferred solid carries);
//  - a face's winding is flipped when exactly one of the face's REVERSED orientation and a
//    mirror location (a negative determinant) applies, so a solid's triangles all face out,
//    as the STL writer does;
//  - Poly_Triangulation's Node and Triangle are 1-based, and a Poly_Triangle's node indices
//    1 to 3; the nodes are 3D (the mesh carries no UVs here).
#include "ifc/step_import.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "Bnd_Box.hxx"
#include "BRepBndLib.hxx"
#include "BRepMesh_IncrementalMesh.hxx"
#include "BRep_Builder.hxx"
#include "BRep_Tool.hxx"
#include "HeaderSection_FileSchema.hxx"
#include "IFSelect_ReturnStatus.hxx"
#include "Interface_EntityIterator.hxx"
#include "Interface_InterfaceModel.hxx"
#include "Poly_Triangle.hxx"
#include "Poly_Triangulation.hxx"
#include "Quantity_ColorRGBA.hxx"
#include "STEPCAFControl_Reader.hxx"
#include "STEPControl_Reader.hxx"
#include "Standard_Failure.hxx"
#include "StepData_FreeFormEntity.hxx"
#include "StepData_Plex.hxx"
#include "StepData_Simple.hxx"
#include "StepData_StepModel.hxx"
#include "TColStd_HArray1OfInteger.hxx"
#include "TColStd_HArray1OfReal.hxx"
#include "TColStd_HSequenceOfExtendedString.hxx"
#include "TColStd_SequenceOfAsciiString.hxx"
#include "TColStd_DataMapOfStringInteger.hxx"
#include "TDataStd_DataMapOfStringByte.hxx"
#include "TCollection_ExtendedString.hxx"
#include "TDataStd_DataMapOfStringHArray1OfInteger.hxx"
#include "TDataStd_DataMapOfStringHArray1OfReal.hxx"
#include "TDataStd_DataMapOfStringReal.hxx"
#include "TDataStd_DataMapOfStringString.hxx"
#include "TDataStd_Name.hxx"
#include "TDataStd_NamedData.hxx"
#include "TDF_ChildIterator.hxx"
#include "TDF_Label.hxx"
#include "TDF_LabelSequence.hxx"
#include "TDF_TagSource.hxx"
#include "TDocStd_Document.hxx"
#include "TopExp_Explorer.hxx"
#include "TopLoc_Location.hxx"
#include "XCAFDoc_ColorTool.hxx"
#include "XCAFDoc_DocumentTool.hxx"
#include "XCAFDoc_LayerTool.hxx"
#include "XCAFDoc_ShapeTool.hxx"
#include "XSControl_WorkSession.hxx"
#include "TopoDS.hxx"
#include "TopoDS_Compound.hxx"
#include "TopoDS_Face.hxx"
#include "TopoDS_Shape.hxx"
#include "TopoDS_Solid.hxx"
#include "gp_Trsf.hxx"

// The OpenCASCADE spelling (7.9+ installs Standard_VersionInfo.hxx, 7.8 the other way
// round), as the probe selects it.
#if __has_include("Standard_VersionInfo.hxx")
#include "Standard_VersionInfo.hxx"
#define SKETCHYUP_STEP_OCCT_79 1
#else
#include "Standard_Version.hxx"
#endif

#include "ifc/json_out.hpp"
#include "ifc/package.hpp"
#include "ifc/progress.hpp"
#include "ifc/report.hpp"
#include "ifc/step_probe.hpp"

namespace sketchyup::ifc {
namespace {

const long long kMaxInputBytes = 128ll * 1024 * 1024;  // the application's input bound
const double kCoordinateLimit = 1e6;  // metres (the package's coordinate bound, ADR 0172 §6)
const double kDegenerateArea = 1e-12;  // m^2: a triangle this small is counted, not hidden
const double kPi = 3.14159265358979323846;
const int kMaxTreeDepth = 127;  // a walked tree level: the package node's level (its parents + the
                                // root) must stay at the decoder's depth limit (128)
const int kMaxMetadataEntries = 64;  // a part's metadata, truncated (a property text is 2048 bytes)

// ADR 0172 §2: the --step-length-unit option values as a factor of metres.
bool optionFactor(const std::string& unit, double& metres) {
  if (unit == "mm") {
    metres = 0.001;
    return true;
  }
  if (unit == "cm") {
    metres = 0.01;
    return true;
  }
  if (unit == "m") {
    metres = 1.0;
    return true;
  }
  if (unit == "in") {
    metres = 0.0254;
    return true;
  }
  if (unit == "ft") {
    metres = 0.3048;
    return true;
  }
  return false;
}

// The two factors disagree (the raw text is what the file says; the model's is what the
// reader made of it).
bool factorMismatch(const double raw, const double model) {
  return std::abs(raw - model) > 1e-9 * std::max(1.0, std::abs(model));
}

struct UnitDecision {
  std::string status;  // "" | "refused-units" | "refused-option"
  std::string applied;  // "" | "declared" | "option"
  double mmPerUnit = 0.0;  // raw file number -> mm (0: refused)
  std::vector<std::string> notices;
};

// ADR 0172 decision 4, the import's unit decision (kept exactly as the metadata report
// carries it): the file's declared unit is authoritative - when the reader's unit context
// resolves, the transfer has normalized the geometry to the cascade unit (millimetres) and
// nothing is rescaled; when it does not, the raw text's first declaration converts the
// geometry, flagged. The option stands only for a file that declares nothing; a file with
// neither, and a declaration that neither the text nor the reader can convert, is refused.
UnitDecision decideUnits(const StepFileFacts& file, const StepModelFacts& model,
                         const std::string& option) {
  UnitDecision units;
  if (file.lengthDeclared) {
    units.applied = "declared";
    if (model.lengthUnitResolved) {
      units.mmPerUnit = 1.0;
      if (model.lengthMetres > 0.0) {
        if (file.lengthMetres > 0.0 && factorMismatch(file.lengthMetres, model.lengthMetres)) {
          units.notices.push_back("the file applies a conversion-based unit: the raw text's first "
                                  "declaration is 1 unit = " + std::to_string(file.lengthMetres) + " m, the "
                                  "unit the file's context applies (" + model.lengthUnit + ") is 1 unit = " +
                                  std::to_string(model.lengthMetres) + " m; the geometry comes out in mm");
        }
      } else {
        units.notices.push_back("the file's length unit (" + model.lengthUnit + ") could not be "
                              "converted from the name; the reader's conversion stands (the geometry "
                              "is in mm)");
      }
    } else if (file.lengthMetres > 0.0) {
      units.mmPerUnit = 1000.0 * file.lengthMetres;
      units.notices.push_back("the reader resolved no length unit for the file; the raw text's "
                            "declaration (" + file.length + ", 1 unit = " + std::to_string(file.lengthMetres) +
                              " m) is applied to the geometry");
    } else if (model.read) {
      units.status = "refused-units";
      units.notices.push_back("the file's length unit cannot be converted; pass --step-length-unit "
                              "to import it as a named unit");
    }
  } else if (!option.empty()) {
    double factor = 0.0;
    if (!optionFactor(option, factor)) {
      units.status = "refused-option";
      units.notices.push_back("--step-length-unit must be one of mm, cm, m, in or ft");
    } else {
      units.applied = "option";
      units.mmPerUnit = 1000.0 * factor;
      units.notices.push_back("the file declares no length unit; the option " + option + " (1 unit = " +
                              std::to_string(factor) + " m) is used");
    }
  } else {
    units.status = "refused-units";
    units.notices.push_back("the file declares no length unit and no --step-length-unit was given; it is "
                            "refused (use --step-length-unit mm|cm|m|in|ft; a unit is never guessed)");
  }
  return units;
}

// ---------------------------------------------------------------------------
// R098.b: the XDE tree (the assemblies, the instances, the names, the colours,
// the layers and the provenance - ADR 0172 section 4).
//
// STEPCAFControl_Reader transfers the file into a TDocStd_Document: the product
// structure is an XDE shape tree (an assembly label with component and
// reference children, a part label with its shape, a reference label for a
// repeated shape), and the file's colours (XCAFDoc_ColorTool), the layer
// visibility (XCAFDoc_LayerTool) and the named data (TDataStd_NamedData) come
// with it. The package is the section 4 tree: the root, an assembly a group,
// a part an element.
//
// A part's instances share the base's mesh (in the base's own frame); the node's transform
// carries the placement (the base's location, then the instance's). A mirror placement's
// negative determinant is affine (the decoder admits it), so a mirrored instance is the
// shared mesh under a mirrored transform, not a baked copy.
//
// The unit policy (decision 4) rides the reader's own unit handling: with a
// resolved unit context the transfer has normalised the geometry to the
// cascade unit (millimetres) and nothing is rescaled; without one the raw
// text's first declaration converts the geometry, flagged; the option stands
// only for a file that declares nothing; a unit is never guessed. The one
// scale step is the file-unit number to metres (toMetres), exactly as the
// metadata report's mmPerUnit says it is.

// An ExtendedString (UTF-16 code units, the reader's strings) as UTF-8 bytes (its
// ToUTF8CString fills a pre-allocated buffer and returns its length; its
// LengthOfCString sizes that buffer). The app's names are bytes (UTF-8).
std::string toUtf8(const TCollection_ExtendedString& s) {
  const int len = s.LengthOfCString();
  if (len <= 0) return {};
  std::string out;
  out.resize(static_cast<std::size_t>(len) + 1);  // the NUL its ToUTF8CString writes
  char* buf = &out[0];  // its ToUTF8CString takes a non-const reference (it nulls it on failure)
  const int used = s.ToUTF8CString(buf);
  out.resize(static_cast<std::size_t>(used < 0 ? 0 : used));
  return out;
}

// A label's name (the XDE's auto- or file-given name; the TDataStd_Name - 7.9
// has no name tool).
std::string labelName(const TDF_Label& L) {
  Handle(TDataStd_Name) name;
  if (L.FindAttribute(TDataStd_Name::GetID(), name) && !name->Get().IsEmpty())
    return toUtf8(name->Get());
  return {};
}

// A part's colour: the XDE's colour entry (the surface over the curve over the
// generic, the specific over the generic) with the file's transparency. A fully
// transparent colour is no colour (the package carries RGB); a partial one keeps
// its RGB and the dropped transparency is a notice.
struct ColorDecision {
  bool rgb = false;
  std::array<float, 3> color{};
  bool alphaDropped = false;
};

ColorDecision colorOf(const TDF_Label& L) {
  ColorDecision out;
  const XCAFDoc_ColorType types[3] = {XCAFDoc_ColorSurf, XCAFDoc_ColorCurv, XCAFDoc_ColorGen};
  for (const XCAFDoc_ColorType type : types) {
    Quantity_ColorRGBA rgba;
    if (!XCAFDoc_ColorTool::GetColor(L, type, rgba)) continue;
    out.rgb = true;
    out.color = {static_cast<float>(rgba.GetRGB().Red()), static_cast<float>(rgba.GetRGB().Green()),
                 static_cast<float>(rgba.GetRGB().Blue())};
    if (rgba.Alpha() <= 0.0f) {
      out.rgb = false;  // fully transparent: no colour to carry
      out.alphaDropped = true;
      return out;
    }
    if (rgba.Alpha() < 1.0f) out.alphaDropped =  true;  // the transparency is dropped (a notice)
    return out;
  }
  return out;
}

// The colour of each face of a part that has its own colour (the surface colour of its sub-shape), keyed by the face's
// TShape: the solid's faces are the same TShapes as the part's sub-shapes, whatever the location or orientation.
using FaceColours = std::map<const TopoDS_TShape*, std::array<float, 3>>;

FaceColours faceColoursOf(const XCAFDoc_ShapeTool& shapeTool, const TDF_Label& part) {
  FaceColours out;
  TDF_LabelSequence subs;
  shapeTool.GetSubShapes(part, subs);
  for (int i = 1; i <= subs.Length(); ++i) {
    TopoDS_Shape face;
    if (!shapeTool.GetShape(subs.Value(i), face) || face.ShapeType() != TopAbs_FACE) continue;
    const ColorDecision color = colorOf(subs.Value(i));
    if (color.rgb) out[face.TShape().operator->()] = color.color;
  }
  return out;
}

// A label's layers: their names (the layer labels' names, joined) and the file's
// layer visibility (an invisible layer hides its shapes).
struct LayerDecision {
  std::string name;
  bool hidden = false;
};

LayerDecision layersOf(const TDF_Label& L, const TDF_Label& layersSection) {
  LayerDecision out;
  const Handle(XCAFDoc_LayerTool) hTool = XCAFDoc_LayerTool::Set(layersSection);  // the document's layer tool (the Layers section)
  if (hTool.IsNull()) return out;
  XCAFDoc_LayerTool tool = *hTool;
  TDF_LabelSequence layers;
  if (!tool.GetLayers(L, layers) || layers.Length() == 0) return out;
  for (int i = 1; i <= layers.Length(); ++i) {
    if (i > 1) out.name += "; ";
    out.name += labelName(layers.Value(i));
    out.hidden = out.hidden || !tool.IsVisible(layers.Value(i));
  }
  return out;
}

// A label's named data (the reader's ReadMetadata) as a `k=v` text: the strings
// first, then the reals, the integers and the arrays (joined with `,`), the
// bytes as two hex digits; truncated at kMaxMetadataEntries.
std::string metadataOf(const TDF_Label& L, bool& truncated) {
  Handle(TDataStd_NamedData) data;
  if (!L.FindAttribute(TDataStd_NamedData::GetID(), data)) return {};
  std::string text;
  int count = 0;
  truncated = false;
  auto take = [&](const std::string& k, const std::string& v) {
    if (count >= kMaxMetadataEntries) {
      truncated = true;
      return;
    }
    if (count++ > 0) text += "; ";
    text += k + "=" + v;
  };
  auto takeKey = [&](const TCollection_ExtendedString& k, const std::string& v) { take(toUtf8(k), v); };
  {
    const TDataStd_DataMapOfStringString& m = data->GetStringsContainer();
    for (TDataStd_DataMapOfStringString::Iterator it(m); it.More(); it.Next())
      takeKey(it.Key(), toUtf8(it.Value()));
  }
  {
    const TDataStd_DataMapOfStringReal& m = data->GetRealsContainer();
    for (TDataStd_DataMapOfStringReal::Iterator it(m); it.More(); it.Next())
      takeKey(it.Key(), std::to_string(it.Value()));
  }
  {
    const TColStd_DataMapOfStringInteger& m = data->GetIntegersContainer();
    for (TColStd_DataMapOfStringInteger::Iterator it(m); it.More(); it.Next())
      takeKey(it.Key(), std::to_string(it.Value()));
  }
  {
    const TDataStd_DataMapOfStringHArray1OfInteger& m = data->GetArraysOfIntegersContainer();
    for (TDataStd_DataMapOfStringHArray1OfInteger::Iterator it(m); it.More(); it.Next()) {
      const Handle(TColStd_HArray1OfInteger) a = it.Value();
      std::string joined;
      for (int i = 1; i <= a->Length(); ++i) {
        if (i > 1) joined += ",";
        joined += std::to_string(a->Value(i));
      }
      takeKey(it.Key(), joined);
    }
  }
  {
    const TDataStd_DataMapOfStringHArray1OfReal& m = data->GetArraysOfRealsContainer();
    for (TDataStd_DataMapOfStringHArray1OfReal::Iterator it(m); it.More(); it.Next()) {
      const Handle(TColStd_HArray1OfReal) a = it.Value();
      std::string joined;
      for (int i = 1; i <= a->Length(); ++i) {
        if (i > 1) joined += ",";
        joined += std::to_string(a->Value(i));
      }
      takeKey(it.Key(), joined);
    }
  }
  {
    const TDataStd_DataMapOfStringByte& m = data->GetBytesContainer();
    for (TDataStd_DataMapOfStringByte::Iterator it(m); it.More(); it.Next()) {
      const int v = it.Value();
      const char hex[3] = {static_cast<char>(std::toupper(v >> 4 & 0xF)), static_cast<char>(std::toupper(v & 0xF)), 0};
      takeKey(it.Key(), std::string(hex));
    }
  }
  return text;
}

// A solid's mesh (ADR 0172 §3): BRepMesh at the options' deflection (the cascade unit,
// millimetres) and crease angle, the faces' triangulations in metres, the winding flipped
// for a reversed face so the solid's triangles face out, and a collapsed triangle
// (degenerate) counted, not hidden. `ok` false: the tessellation failed (OCCT said why).
struct SolidMesh {
  bool ok = false;
  long long triangles = 0;
  long long degenerate = 0;
  long long faces = 0;
  std::string error;
  std::vector<double> vertices;  // metres, 3 per vertex
  std::vector<std::uint32_t> indices;
  // One colour per triangle, when the part has face colours (empty otherwise): a face's own colour, the part's
  // colour for a face without one, none when neither.
  std::vector<std::optional<std::array<float, 3>>> colors;
};

// A mirror location (a negative determinant) reverses the winding like a reversed face.
bool mirrorLocation(const TopLoc_Location& loc) {
  if (loc.IsIdentity()) return false;
  const gp_Trsf& t = loc.Transformation();
  const double det = t.Value(1, 1) * (t.Value(2, 2) * t.Value(3, 3) - t.Value(2, 3) * t.Value(3, 2)) -
                       t.Value(1, 2) * (t.Value(2, 1) * t.Value(3, 3) - t.Value(2, 3) * t.Value(3, 1)) +
                       t.Value(1, 3) * (t.Value(2, 1) * t.Value(3, 2) - t.Value(2, 2) * t.Value(3, 1));
  return det < 0.0;
}

// The winding the decoder will see. The tessellator's raw winding is the face's own
// orientation under the shape's own location (a mirror location reverses it, the ADR 0172
// section 7 mirrored-solid measurement); the node's transform applies on top, and a
// negative-determinant transform reverses the winding a second time. So the raw winding
// is flipped when the face-orientation rule and the instance's handedness disagree: a
// forward face under a mirror instance is stored reversed so the decoder's mirror leaves
// it outward, and a reversed face under a mirror instance is stored forward.
SolidMesh tessellateSolid(const TopoDS_Shape& solid, const double deflectionMm, const double angleRadians,
                          const double metresPerUnit, const bool instanceMirror, const FaceColours& faceColours,
                          const ColorDecision& partColour) {
  SolidMesh out;
  try {
    // The constructor performs the meshing (OCCT 7.9.3; not parallel, so the output is
    // deterministic).
    BRepMesh_IncrementalMesh(solid, deflectionMm, Standard_False, angleRadians, Standard_False);
  } catch (const Standard_Failure& failure) {
    out.error = failure.GetMessageString();
    return out;
  }
  out.ok = true;
  TopExp_Explorer face(solid, TopAbs_FACE);
  while (face.More()) {
    const TopoDS_Shape current = face.Current();
    if (current.ShapeType() == TopAbs_FACE) {
      const TopoDS_Face f = TopoDS::Face(current);
      TopLoc_Location loc;
      const Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(f, loc);
      if (!tri.IsNull()) {
        ++out.faces;
        // The winding is the product of three factors, XOR (each reverses at most once):
        // the face's REVERSED orientation, the storage-location's handedness (a mirror
        // location reverses the winding the storage-to-face transform applies, the ADR
        // 0172 section 7 rule), and the instance's transform (the decoder applies the
        // node transform on top of the mesh; a negative-determinant transform reverses
        // the winding a second time, so a mirror instance stores the base's winding
        // flipped and comes out outward).
        const bool flip = (f.Orientation() == TopAbs_REVERSED) != mirrorLocation(loc) != instanceMirror;
        std::optional<std::array<float, 3>> faceColour;
        if (const auto found = faceColours.find(f.TShape().operator->()); found != faceColours.end()) faceColour = found->second;
        else if (partColour.rgb) faceColour = partColour.color;
        // Each node of the face's triangulation once (transformed and scaled); the triangles
        // index into them. A node written per triangle would repeat the shared nodes up to
        // six times (about 72 MB of vertex data at the 1,000,000-triangle cap, close to the
        // 128 MiB mesh-chunk bound).
        const int nodes = tri->NbNodes();
        std::vector<double> faceVertices;
        faceVertices.reserve(3 * static_cast<size_t>(nodes));
        for (int i = 1; i <= nodes; ++i) {
          const gp_Pnt node = tri->Node(i);
          gp_Pnt p = node;
          if (!loc.IsIdentity()) p.Transform(loc.Transformation());
          // The raw numbers (the cascade unit when the reader's context resolved, the
          // file's own unit when it did not) to metres; the unit policy (decideUnits) has
          // already decided which conversion applies, so this is the only scale step.
          faceVertices.push_back(p.X() * metresPerUnit);
          faceVertices.push_back(p.Y() * metresPerUnit);
          faceVertices.push_back(p.Z() * metresPerUnit);
        }
        const std::uint32_t base = static_cast<std::uint32_t>(out.vertices.size() / 3);
        out.vertices.insert(out.vertices.end(), faceVertices.begin(), faceVertices.end());
        for (int t = 1; t <= tri->NbTriangles(); ++t) {
          const Poly_Triangle tr = tri->Triangle(t);
          // The node numbers are 1-based (node i is the i - 1 group of faceVertices).
          const double x1 = faceVertices[3 * static_cast<size_t>(tr.Value(1) - 1)];
          const double y1 = faceVertices[3 * static_cast<size_t>(tr.Value(1) - 1) + 1];
          const double z1 = faceVertices[3 * static_cast<size_t>(tr.Value(1) - 1) + 2];
          const double x2 = faceVertices[3 * static_cast<size_t>(tr.Value(2) - 1)];
          const double y2 = faceVertices[3 * static_cast<size_t>(tr.Value(2) - 1) + 1];
          const double z2 = faceVertices[3 * static_cast<size_t>(tr.Value(2) - 1) + 2];
          const double x3 = faceVertices[3 * static_cast<size_t>(tr.Value(3) - 1)];
          const double y3 = faceVertices[3 * static_cast<size_t>(tr.Value(3) - 1) + 1];
          const double z3 = faceVertices[3 * static_cast<size_t>(tr.Value(3) - 1) + 2];
          const double ax = x2 - x1, ay = y2 - y1, az = z2 - z1;
          const double bx = x3 - x1, by = y3 - y1, bz = z3 - z1;
          if (0.5 * std::abs(ay * bz - az * by) + 0.5 * std::abs(az * bx - ax * bz) +
              0.5 * std::abs(ax * by - ay * bx) < kDegenerateArea) {
            ++out.degenerate;  // counted (degenerateTriangles), the app's merge drops it
          }
          ++out.triangles;
          if (!faceColours.empty()) out.colors.push_back(faceColour);
          // Outward for a forward face, flipped for a reversed one (the STL writer's rule),
          // into the face's node table (node i is the i - 1 group of faceVertices).
          const std::uint32_t n1 = base + static_cast<std::uint32_t>(tr.Value(1) - 1);
          const std::uint32_t n2 = base + static_cast<std::uint32_t>(tr.Value(2) - 1);
          const std::uint32_t n3 = base + static_cast<std::uint32_t>(tr.Value(3) - 1);
          if (flip) {
            out.indices.push_back(n1);
            out.indices.push_back(n3);
            out.indices.push_back(n2);
          } else {
            out.indices.push_back(n1);
            out.indices.push_back(n2);
            out.indices.push_back(n3);
          }
        }
      }
    }
    face.Next();
  }
  return out;
}

// The solid's bounding box in metres, before the (potentially heavy) tessellation: a solid
// beyond the package's 1e6 m coordinate bound is a refusal of the whole file, as the IFC
// import's farFromOrigin, not a silently clipped mesh.
bool solidTooFarFromOrigin(const TopoDS_Shape& solid, const double metresPerUnit, std::string& problem) {
  Bnd_Box box;
  BRepBndLib::Add(solid, box);
  if (box.IsVoid()) return false;
  Standard_Real x1, y1, z1, x2, y2, z2;
  box.Get(x1, y1, z1, x2, y2, z2);
  for (const double value : {x1 * metresPerUnit, y1 * metresPerUnit, z1 * metresPerUnit,
                             x2 * metresPerUnit, y2 * metresPerUnit, z2 * metresPerUnit}) {
    if (!std::isfinite(value) || std::abs(value) > kCoordinateLimit) {
      problem = "A solid is beyond 1e6 m from the origin; it is refused (the package's "
                "coordinates are bounded).";
      return true;
    }
  }
  return false;
}

// The solids of a shape as one compound, the surfaces without volume left out (ADR 0172 section 3). A shape with
// such surfaces is tessellated from this compound, so they never join a body.
TopoDS_Shape solidsOf(const TopoDS_Shape& shape) {
  BRep_Builder builder;
  TopoDS_Compound compound;
  builder.MakeCompound(compound);
  TopExp_Explorer s(shape, TopAbs_SOLID);
  for (; s.More(); s.Next()) builder.Add(compound, s.Current());
  return compound;
}

// A location of the report (ADR 0172 section 5): the entity label, the kind and a message. The report keeps 32 per
// kind and counts the rest.
void locate(Report& report, const std::string& kind, const long long label, const std::string& message) {
  Location place;
  place.kind = kind;
  place.step = static_cast<int>(label);
  place.message = message;
  report.location(std::move(place));
}

// The kinds the import reads as data (ADR 0172 section 5): the product structure and its links, the representation
// contexts and the units. Every other kind is one the import does not read. A complex entity is named by its members.
const std::set<std::string> kReadKinds = {
    "APPLICATION_PROTOCOL_DEFINITION", "APPLICATION_CONTEXT", "PRODUCT", "PRODUCT_CONTEXT", "PRODUCT_DEFINITION",
    "PRODUCT_DEFINITION_FORMATION", "PRODUCT_DEFINITION_FORMATION_WITH_SPECIFIED_SOURCE", "PRODUCT_DEFINITION_CONTEXT",
    "PRODUCT_DEFINITION_SHAPE", "PRODUCT_RELATED_PRODUCT_CATEGORY", "PRODUCT_CATEGORY", "PRODUCT_DEFINITION_USAGE",
    "NEXT_ASSEMBLY_USAGE_OCCURRENCE", "CONTEXT_DEPENDENT_SHAPE_REPRESENTATION", "ITEM_DEFINED_TRANSFORMATION",
    "REPRESENTATION_RELATIONSHIP", "SHAPE_REPRESENTATION_RELATIONSHIP", "SHAPE_DEFINITION_REPRESENTATION",
    "GEOMETRIC_REPRESENTATION_CONTEXT", "GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT", "GLOBAL_UNIT_ASSIGNED_CONTEXT",
    "REPRESENTATION_CONTEXT", "UNCERTAINTY_MEASURE_WITH_UNIT", "LENGTH_UNIT", "NAMED_UNIT", "SI_UNIT",
    "PLANE_ANGLE_UNIT", "SOLID_ANGLE_UNIT", "CONVERSION_BASED_UNIT", "LENGTH_MEASURE_WITH_UNIT", "DIMENSIONAL_EXPONENTS"};

bool readKind(const std::string& kind) {
  std::size_t begin = 0;
  for (;;) {
    const std::size_t plus = kind.find('+', begin);
    if (kReadKinds.count(kind.substr(begin, plus - begin)) == 0) return false;
    if (plus == std::string::npos) return true;
    begin = plus + 1;
  }
}

// An entity's kind as the file names it: the StepType of a simple entity, the members of a complex one joined with "+".
std::string stepKind(const Handle(Standard_Transient)& entity, const Handle(Interface_InterfaceModel)& model) {
  if (const Handle(StepData_Plex) plex = Handle(StepData_Plex)::DownCast(entity); !plex.IsNull()) {
    std::string kind;
    for (Standard_Integer i = 1; i <= plex->NbMembers(); ++i) {
      if (i > 1) kind += "+";
      const Handle(StepData_Simple) member = plex->Member(i);
      if (!member.IsNull()) kind += member->StepType();
    }
    return kind;
  }
  if (const Handle(StepData_Simple) simple = Handle(StepData_Simple)::DownCast(entity); !simple.IsNull())
    return simple->StepType();
  if (const Handle(StepData_FreeFormEntity) free = Handle(StepData_FreeFormEntity)::DownCast(entity); !free.IsNull())
    return free->StepType();
  // Any other entity: the model's name, which for a complex entity is "(A,B,C)" - the same kind as "A+B+C" above.
  std::string kind;
  for (const char c : std::string(model->TypeName(entity)))
    if (c != '(' && c != ')' && c != ' ') kind += c == ',' ? '+' : c;
  return kind;
}

// The entities of a file that transferred nothing, counted by kind and located by their STEP label: the import does
// not read them (a PMI-only AP242 file, for one). Only the kinds the import does not read are counted (ADR 0172 E4).
void tallyUnread(const Handle(XSControl_WorkSession)& session, Report& report) {
  const Handle(Interface_InterfaceModel) model = session->Model();
  if (model.IsNull()) return;
  const Handle(StepData_StepModel) steps = Handle(StepData_StepModel)::DownCast(model);
  for (Standard_Integer i = 1; i <= model->NbEntities(); ++i) {
    const Handle(Standard_Transient) entity = model->Value(i);
    const std::string kind = stepKind(entity, model);
    if (readKind(kind)) continue;
    const Standard_Integer label = steps.IsNull() ? 0 : steps->IdentLabel(entity);
    report.notImported(kind.empty() ? "unnamed" : kind, "not-read", 1);
    locate(report, "notImported", label > 0 ? label : i, "not read by the import: a file with no shape to convert");
  }
}

// The report's lists in the IFC shape: the elements not imported by class and reason, the locations and their totals.
void writeLists(JsonOut& j, const Report& report) {
  j.key("elements").beginObject();
  j.key("notImported").beginArray();
  for (const auto& [key, count] : report.notImportedClasses())
    j.beginObject().field("class", key.first).field("reason", key.second).field("count", count).endObject();
  j.endArray();
  j.endObject();
  j.key("locations").beginArray();
  for (const Location& place : report.locations())
    j.beginObject().field("kind", place.kind).field("step", place.step).field("globalId", place.globalId).field("message", place.message).endObject();
  j.endArray();
  j.key("locationTotals").beginObject();
  for (const auto& [kind, total] : report.locationTotals()) j.field(kind, total);
  j.endObject();
}

std::string baseName(const std::string& path) {
  const auto at = path.find_last_of('/');
  return at == std::string::npos ? path : path.substr(at + 1);
}

// The package's engine section: the library and the OCCT version the build was configured
// against (the probe records the runtime version in its own report).
std::string engineReportJson() {
  JsonOut j;
  j.beginObject().field("library", "OpenCASCADE");
#if defined(SKETCHYUP_STEP_OCCT_79)
  j.field("version", OCCT_Version_String_Complete());
#else
  j.field("version", std::to_string(OCC_VERSION_MAJOR) + "." + std::to_string(OCC_VERSION_MINOR) + "." +
                  std::to_string(OCC_VERSION_MAINTENANCE));
#endif
  j.field("kernel", "step").field("threads", 1).field("completed", true).endObject();
  return j.str();
}

}  // namespace

std::string importStep(const StepImportOptions& options) {
  JsonOut json;
  json.beginObject();
  json.field("apiVersion", 1);
  const StepFileFacts file = stepFileFacts(options.file);
  json.key("file").beginObject()
      .field("path", options.file)
      .field("bytes", file.bytes)
      .endObject();
  json.field("schemaRaw", file.schemaRaw);
  json.field("schema", file.schema);
  std::vector<std::string> notices;
  std::string status;
  double mmPerUnit = 0.0;  // 0: no conversion (refused or failed)
  UnitDecision units;
  StepModelFacts model;

  if (file.bytes > kMaxInputBytes) {
    status = "refused-size";
    notices.push_back("the file is over 128 MiB; it is refused (ADR 0172 §6)");
  } else if (!file.readable) {
    status = "failed-read";
    notices.push_back("the file could not be read");
  } else if (!file.isoHeader) {
    status = "refused-header";
    notices.push_back("the file does not start with an ISO-10303-21 header; it is refused");
  } else if (file.schema == "other") {
    status = "refused-schema";
    notices.push_back("the FILE_SCHEMA is not AP203, AP214 or AP242; it is refused");
  } else {
    // The unit the import applies (ADR 0172 decision 4): the file's declared unit is
    // authoritative, the option stands only when nothing is declared, and a unit is never
    // guessed.
    model = readStepModel(options.file);
    units = decideUnits(file, model, options.lengthUnitOverride);
    status = units.status;
    notices = units.notices;
    mmPerUnit = units.mmPerUnit;
  }

  if (!model.read) model = readStepModel(options.file);
  json.field("readStatus", model.readStatus);
  if (!model.error.empty()) json.field("error", model.error);
  json.field("modelSchema", model.schema);
  json.field("modelLengthUnit", model.lengthUnit);
  json.field("modelLengthResolved", model.lengthUnitResolved);
  json.field("modelLengthMetres", model.lengthMetres);
  json.key("units").beginObject()
      .field("declared", file.lengthDeclared)
      .field("length", file.length)
      .field("angle", file.angle)
      .field("solidAngle", file.solidAngle)
      .field("rawLengthMetres", file.lengthDeclared ? file.lengthMetres : 0.0)
      .field("modelLengthMetres", model.lengthMetres)
      .field("applied", units.applied)
      .field("mmPerUnit", mmPerUnit)
      .endObject();
  if (status.empty()) {
    if (!model.read) {
      status = "failed-read";
      notices.push_back("the STEP reader failed to read the file");
    } else {
      json.field("entities", model.entities);
      json.field("roots", model.roots);
      json.field("transferred", model.transferred);
      if (model.transferred == 0) {
        status = "no-shapes";
        notices.push_back("the file read, but no shape could be transferred");
      } else {
        status = "ok";
        json.key("shapes").beginArray();
        for (const StepShapeFacts& shape : model.shapes) {
          json.beginObject()
              .field("index", shape.index)
              .field("solids", shape.solids)
              .field("faces", shape.faces);
          if (shape.ok) {
            std::vector<double> boxMm;
            boxMm.reserve(shape.box.size());
            for (const double v : shape.box) boxMm.push_back(v * mmPerUnit);
            json.key("boundingBoxMm").doubles(boxMm);
          }
          json.endObject();
        }
        json.endArray();
      }
    }
  }
  json.field("status", status);
  json.key("notices").strings(notices);
  json.endObject();
  return json.str();
}

int runStepImport(const StepImportOptions& options) {
  const std::string report = importStep(options);
  std::cout << report << std::endl;
  // The status is carried in the report; the exit code is a compact version of it: 0 when
  // the file was read (with or without transferred shapes), 2 when it was refused, 1 when
  // the read itself failed. Nothing is created any way, and the code says why.
  size_t begin = report.rfind("\"status\"");
  if (begin == std::string::npos) return 1;
  begin = report.find('"', begin + 8);
  const size_t end = report.find('"', begin + 1);
  if (begin == std::string::npos || end == std::string::npos) return 1;
  const std::string status = report.substr(begin + 1, end - begin - 1);
  if (status == "ok" || status == "no-shapes") return 0;
  if (status.rfind("refused", 0) == 0) return 2;
  return 1;
}

int runStepImportPackage(const StepImportOptions& options) {
  Progress progress(true);
  const auto refuse = [&](const std::string& code, const std::string& message) {
    JsonOut json;
    json.beginObject().field("status", "refused").field("code", code).field("message", clip(message, 240)).endObject();
    progress.line("R " + json.str());
    return 3;
  };

  progress.phase("read");
  const StepFileFacts file = stepFileFacts(options.file);
  if (file.bytes > kMaxInputBytes) return refuse("size", "The file is over 128 MiB; it is refused (ADR 0172 §6).");
  if (!file.readable) return refuse("read", "The file could not be read.");
  if (!file.isoHeader) return refuse("header", "The file does not start with an ISO-10303-21 header; it is refused.");
  if (file.schema == "other") return refuse("schema", "The FILE_SCHEMA " + clip(file.schemaRaw, 40) + " is not AP203, AP214 or AP242; it is refused.");
  if (!std::isfinite(options.deflectionMm) || options.deflectionMm <= 0.0) return refuse("deflection", "The deflection must be a positive number of millimetres.");
  if (!std::isfinite(options.angleDeg) || options.angleDeg <= 0.0 || options.angleDeg > 180.0) return refuse("angle", "The crease angle must be in (0, 180] degrees.");
  if (options.maxTrianglesPerElement < 1) return refuse("cap", "The per-element triangle cap must be at least 1.");
  if (options.maxTotalTriangles < 1) return refuse("cap", "The total triangle cap must be at least 1.");

  // ADR 0172 §2 decision 4 (the probe's facts as the source): the file's declaration wins a mismatch,
  // the option stands only for a file that declares nothing, an unconvertible unit is refused, a unit
  // is never guessed. The XDE transfer carries the geometry through the reader's own unit handling
  // (a resolved unit context normalises it to the cascade unit, millimetres; without one it stays in
  // the file's own unit), so the one scale step is the file's unit to metres (toMetres, the report's
  // mmPerUnit). Nothing is ever silently rescaled.
  const StepModelFacts model = readStepModel(options.file);
  if (!model.read || !model.error.empty())
    return refuse("read", model.error.empty() ? "The STEP reader failed to read the file." : clip(model.error, 200));
  const UnitDecision units = decideUnits(file, model, options.lengthUnitOverride);
  if (!units.status.empty()) return refuse(units.status == "refused-option" ? "option" : "units", units.notices.front());
  const double toMetres = units.mmPerUnit * 0.001;  // file unit -> metres: the package's unit, the only scale step
  Report report;
  for (const std::string& notice : units.notices) report.notice(notice, 1);

  // The package's fixed fields (independent of the document): the engine, the source facts and
  // the unit decision (ADR 0172 §2, the report's units block).
  PackageData package;
  package.engineJson = engineReportJson();
  JsonOut source;
  source.beginObject().field("file", baseName(options.file)).field("bytes", file.bytes).field("schema", file.schema).field("schemaRaw", file.schemaRaw).field("sha256", file.sha256).field("entities", model.entities).endObject();
  package.sourceJson = source.str();
  JsonOut unitsJson;
  unitsJson.beginObject()
      .field("declared", file.lengthDeclared)
      .field("length", file.length)
      .field("rawLengthMetres", file.lengthDeclared ? file.lengthMetres : 0.0)
      .field("modelLengthUnit", model.lengthUnit)
      .field("modelLengthMetres", model.lengthMetres)
      .field("applied", units.applied)
      .field("mmPerUnit", units.mmPerUnit)
      .field("metresPerUnit", toMetres)
      .field("angle", file.angle)
      .field("solidAngle", file.solidAngle)
      .endObject();
  package.unitsJson = unitsJson.str();

  // The XDE read: the file's product structure (the assemblies, the instances), its colours,
  // its layers and its named data, into a TDocStd_Document (STEPCAFControl_Reader's Transfer fills it).
  // Its modes on (the default constructor's): the colours, the layers, the names and the
  // properties (the TDataStd_NamedData) transfer with it.
  Handle(XSControl_WorkSession) ws(new XSControl_WorkSession());
  STEPCAFControl_Reader xde(ws, Standard_False);  // its controller's Init registers the STEP norm for the session
  if (!ws->SelectNorm("STEP"))  // without a norm the session has no work library: the read could not happen
    return refuse("read", "The STEP reader's session could not select the STEP norm.");
  // A refusal for a file that transferred nothing (ADR 0172 E4): the refusal stays, and the report names and counts
  // the entities the import does not read, by kind, with their locations.
  const auto refuseUnread = [&](const std::string& message) {
    Report unread;
    tallyUnread(ws, unread);
    long long entities = 0;
    for (const auto& [key, count] : unread.notImportedClasses()) entities += count;
    std::string text = message;
    if (entities > 0)
      text += " " + std::to_string(entities) + " entities of " + std::to_string(unread.notImportedClasses().size()) +
              " kinds that the import does not read are listed in the report.";
    JsonOut json;
    json.beginObject().field("status", "refused").field("code", "nothingToImport").field("message", clip(text, 240));
    writeLists(json, unread);
    json.endObject();
    progress.line("R " + json.str());
    return 3;
  };
  xde.SetColorMode(Standard_True);
  xde.SetLayerMode(Standard_True);
  xde.SetNameMode(Standard_True);
  xde.SetPropsMode(Standard_True);
  Handle(TDocStd_Document) doc(new TDocStd_Document("XCAF"));
  try {
    if (xde.ReadFile(options.file.c_str()) != IFSelect_RetDone)
      return refuse("read", "The STEP reader failed to read the file.");
  } catch (const Standard_Failure& failure) {
    return refuse("read", clip(failure.GetMessageString(), 200));
  }
  if (xde.NbRootsForTransfer() == 0) return refuseUnread("The file read, but no shape could be transferred.");
  try {
    if (!xde.Transfer(doc)) return refuseUnread("The reader could not transfer the file into the document.");
  } catch (const Standard_Failure& failure) {
    return refuse("read", clip(failure.GetMessageString(), 200));
  }
  // The XDE's shape and layer tools attach to the document's sections (the Shapes and the Layers),
  // not to the document's main label (which holds the sections, not the shapes).
  const TDF_Label shapes = XCAFDoc_DocumentTool::ShapesLabel(doc->Main());
  const TDF_Label layers = XCAFDoc_DocumentTool::LayersLabel(doc->Main());
  const Handle(XCAFDoc_ShapeTool) hShapeTool = XCAFDoc_ShapeTool::Set(shapes);
  if (hShapeTool.IsNull()) return refuse("no-shape-tool", "The document holds no shape tool.");
  XCAFDoc_ShapeTool shapeTool = *hShapeTool;  // the document's shape tool (its Shapes section)

  // The top-level products (the Shapes section's children): the assemblies and the free shapes (a
  // shape that is a reference's base - repeated - is not a product of its own; XDE keeps it in
  // the section for the reference to point at, and its IsFree is false).

  // The tree in walk order (a parent before its children): the label (its own entry), its base (the
  // reference's referred shape, or the label itself), the parent (in the walk order) and whether the
  // label is a reference (the instance evidence; a reference is one use of a repeated shape).
  struct FlatNode {
    std::int32_t tag = -1;  // the label's tag (-1: a label the XDE left untagged)
    TDF_Label label;
    std::size_t baseIndex = 0;  // its base in the unique-bases list (its mesh, its accounting)
    TDF_Label base;
    long long parent = -1;
    bool reference = false;
    std::array<double, 16> transform = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};  // local (to the parent)
    double detLocal = 1.0;  // the 3x3's determinant (1: identity or a degenerate placement)
    double detChain = 1.0;   // the product of the chain's (the decoder composes the chain)
    bool nonIdentity = false;
    bool provenancePlaced = false;
    bool degenerate = false;
    bool isAssembly = false;
  };
  std::vector<FlatNode> order;
  std::vector<TDF_Label> baseLabels;  // the unique bases (their label identity - IsEqual - the XDE
                                     // leaves its component and base labels untagged, so the tag
                                     // is no key), first-seen order
  bool treeTooDeep = false;
  auto baseIndexOf = [&](const TDF_Label& B) -> std::size_t {
    for (std::size_t k = 0; k < baseLabels.size(); ++k)
      if (baseLabels[k].IsEqual(B)) return k;
    baseLabels.push_back(B);
    return baseLabels.size() - 1;
  };
  auto walk = [&](auto&& self, const TDF_Label& L, const long long parent, const int depth) -> void {
    if (depth > kMaxTreeDepth) {
      treeTooDeep = true;  // the package's node depth (128) is not left over
      return;
    }
    FlatNode flat;
    flat.tag = L.Tag();
    flat.label = L;
    flat.parent = parent;
    if (shapeTool.IsAssembly(L)) {
      flat.base = L;  // a group: its own label is its base (a group always converts, no mesh)
    } else {
      flat.reference = shapeTool.IsReference(L);
      flat.base = L;
      if (flat.reference) {
        TDF_Label ref;
        if (shapeTool.GetReferredShape(L, ref)) flat.base = ref;  // a repeated shape: the shape it refers to
      }
    }
    flat.baseIndex = baseIndexOf(flat.base);
    const long long index = static_cast<long long>(order.size());
    order.push_back(flat);
    // A group's components, and the components of the group a reference refers to (a sub-assembly occurrence:
    // the sub-assembly's own components, placed under this node).
    if (shapeTool.IsAssembly(flat.base)) {
      TDF_LabelSequence comps;
      shapeTool.GetComponents(flat.base, comps);
      for (int i = 1; i <= comps.Length(); ++i) self(self, comps.Value(i), index, depth + 1);
    }
  };
  {
    TDF_ChildIterator it(shapes);
    while (it.More()) {
      const TDF_Label L = it.Value();
      it.Next();
      // Roots are the free labels: a sub-assembly is walked under the occurrence that places it, not as a root.
      if (shapeTool.IsFree(L)) walk(walk, L, -1, 0);
    }
  }

  if (order.empty()) return refuseUnread("The file transferred, but the document holds no shape.");
  if (treeTooDeep) report.notice("treeTooDeep", 1);
  // ADR 0172 §4: an element with no parent (a free part; a free assembly is a top-level group)
  // goes under the top-level group, counted as a notice (the IFC uncontainedElements pattern).
  long long uncontained = 0;
  for (const auto& flat : order)
    if (!flat.isAssembly && flat.parent < 0) ++uncontained;
  if (uncontained > 0) report.notice("uncontainedElements", uncontained);

  auto compose = [](const std::array<double, 16>& A, const std::array<double, 16>& B) {
    std::array<double, 16> C{};
    for (int r = 0; r < 4; ++r)
      for (int c = 0; c < 4; ++c) {
        double v = 0;
        for (int k = 0; k < 4; ++k) v += A[r * 4 + k] * B[k * 4 + c];
        C[r * 4 + c] = v;
      }
    return C;
  };
  auto placementOf = [&](const TDF_Label& L) {
    std::array<double, 16> W = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const TopLoc_Location loc = shapeTool.GetLocation(L);
    if (!loc.IsIdentity()) {
      const gp_Trsf& t = loc.Transformation();
      for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 4; ++c) W[r * 4 + c] = t.Value(r + 1, c + 1);
    }
    return W;
  };
  auto det3 = [](const std::array<double, 16>& W) {
    const double a = W[0], b = W[1], c = W[2], d = W[4], e = W[5], f = W[6], g = W[8], h = W[9], i = W[10];
    return a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
  };
  // Each node's placement (ADR 0172 section 4): a point of a part's mesh (in the base's own
  // frame, in metres) is mapped through the base's location, then the label's (the
  // reference's instance placement); the node's transform is the local one (the decoder
  // composes the tree), and a degenerate (non-finite) placement falls back to the identity
  // (the base's mesh, as written). The 3x3's determinant (a mirror's -1, a translation's
  // +1, unaffected by the translation scale) decides the tessellator's winding factor.
  // The pre-pass runs before the nodes exist, so the placement count and the placed
  // provenance are carried on the entry and applied to the node as it is emitted.
  long long placedCount = 0;
  for (std::size_t i = 0; i < order.size(); ++i) {
    FlatNode& flat = order[i];
    std::array<double, 16> W = placementOf(flat.label);
    if (!shapeTool.IsAssembly(flat.base) && flat.reference && flat.base != flat.label)
      W = compose(W, placementOf(flat.base));
    W[3] *= toMetres;  // the placement is in the file's unit, the mesh's in metres
    W[7] *= toMetres;
    W[11] *= toMetres;
    flat.isAssembly = shapeTool.IsAssembly(flat.base);
    bool finite = true;
    for (double v : W) finite = finite && std::isfinite(v);
    if (!finite) {
      flat.degenerate = true;  // a degenerate location: the identity (the base's mesh, as written)
      W = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
      if (!shapeTool.IsAssembly(flat.base)) report.notice("degeneratePlacement", 1);
    }
    flat.detLocal = det3(W);
    flat.transform = W;
    bool identity = true;
    for (int r = 0; r < 4; ++r)
      for (int c = 0; c < 4; ++c) identity = identity && std::abs(W[r * 4 + c] - (r == c ? 1.0 : 0.0)) < 1e-12;
    flat.nonIdentity = !identity;
    if (flat.nonIdentity && !shapeTool.IsAssembly(flat.base)) {
      ++placedCount;
      flat.provenancePlaced = true;  // the node's step.Placement provenance
    }
  }
  // The chain's determinant: the decoder composes the chain (the world transform is the
  // product of the chain's), and the winding's mirror factor is the chain's (a mirror
  // ancestor reverses every descendant's winding).
  for (std::size_t i = 0; i < order.size(); ++i) {
    const double parentDet = order[i].parent >= 0 ? order[static_cast<std::size_t>(order[i].parent)].detChain : 1.0;
    order[i].detChain = order[i].detLocal * parentDet;
  }
  // Each base's first place in the walk: its locations name that entry's label and the path above it (ADR 0172 section 5).
  std::vector<long long> firstEntry(baseLabels.size(), -1);
  for (std::size_t i = 0; i < order.size(); ++i)
    if (!order[i].isAssembly && firstEntry[order[i].baseIndex] < 0) firstEntry[order[i].baseIndex] = static_cast<long long>(i);
  // The walk's path to an entry, for a message: the names of the assemblies above it and its own, " (in A / B)".
  const auto placeOf = [&](const long long entry) -> std::string {
    if (entry < 0) return {};
    std::vector<std::string> names;
    for (long long at = entry; at >= 0; at = order[static_cast<std::size_t>(at)].parent) {
      const std::string name = labelName(order[static_cast<std::size_t>(at)].base);
      names.push_back(name.empty() ? "(unnamed)" : name);
    }
    std::string path;
    for (auto it = names.rbegin(); it != names.rend(); ++it) path += (path.empty() ? "" : " / ") + *it;
    return " (in " + path + ")";
  };

  // The unique bases (the shapes the instances refer to), first-seen order: meshed once, shared by
  // every instance. A group (an assembly) is always converted; a part is converted, omitted (a cap)
  // or unsupported (nothing meshable) - the accounting stays the ADR 0172 §3 identity over the
  // bases: converted + omitted + unsupported = bases.
  long long converted = 0, omitted = 0, unsupported = 0;
  long long omittedEntries = 0;  // the walk's entries that no body was made for (their base was left out)
  long long triangles = 0, degenerate = 0, faces = 0, solidCount = 0, freeFaces = 0;
  std::map<std::pair<std::size_t, int>, long long> baseMesh;  // (the base's index, its mirror
                                                          // state) -> its package mesh
  const double deflection = options.deflectionMm / units.mmPerUnit;  // millimetres, in the shape's (file's) unit
  const double angleRadians = options.angleDeg * kPi / 180.0;
  progress.phase("tessellate");
  progress.tasks(static_cast<std::uint32_t>(baseLabels.size()));
  for (std::size_t baseIndex_ = 0; baseIndex_ < baseLabels.size(); ++baseIndex_) {
    progress.at(static_cast<std::uint32_t>(baseIndex_));
    const TDF_Label& base = baseLabels[baseIndex_];
    if (shapeTool.IsAssembly(base)) {
      ++converted;  // a group, no mesh of its own (its parts carry the geometry)
      continue;
    }
    // The base's label for its locations: its first walk entry's (the entity label, ADR 0172 section 5), else its own.
    const long long entry = firstEntry[baseIndex_];
    const long long label = std::max<long long>(0, entry >= 0 ? order[static_cast<std::size_t>(entry)].tag : base.Tag());
    const std::string where = placeOf(entry);
    TopoDS_Shape shape;
    shapeTool.GetShape(base, shape);
    if (shape.IsNull()) {
      report.notImported("StepPart", "no-shape", 1);
      locate(report, "notImported", label, "no shape to convert: not imported" + where);
      ++unsupported;
      continue;
    }
    // The shape's surfaces without volume and its solids. A surface is counted and located, and it is not imported
    // (ADR 0172 section 3): it is never tessellated into a body, so it can never come back in another element's body.
    long long allFaces = 0, solidFaces = 0, solids = 0;
    {
      TopExp_Explorer e(shape, TopAbs_FACE);
      while (e.More()) {
        ++allFaces;
        e.Next();
      }
      TopExp_Explorer s(shape, TopAbs_SOLID);
      while (s.More()) {
        ++solids;
        TopExp_Explorer f(s.Current(), TopAbs_FACE);
        while (f.More()) {
          ++solidFaces;
          f.Next();
        }
        s.Next();
      }
    }
    solidCount += solids;
    const long long surfaces = allFaces - solidFaces;
    if (surfaces > 0) {
      freeFaces += surfaces;
      report.notImported("face", "free-surface", surfaces);
      report.notice("freeFaces", surfaces);
      for (long long n = 1; n <= surfaces; ++n)
        locate(report, "notImported", label,
               "surface " + std::to_string(n) + " of " + std::to_string(surfaces) + " is bounded by no solid: not imported" + where);
    }
    if (solids == 0) {
      report.notImported("StepPart", "no-solids", 1);
      report.notice("noSolids", 1);
      locate(report, "notImported", label, "no solid: not imported" + where);
      ++omitted;  // a counted slot, omitted: nothing of volume to convert
      continue;
    }
    // With surfaces, the solids alone are checked and tessellated: the surfaces are not imported, so they are not in the
    // body. A shape without surfaces is used as it is, as before.
    const TopoDS_Shape solidShape = surfaces > 0 ? solidsOf(shape) : shape;
    std::string tooFar;
    if (solidTooFarFromOrigin(solidShape, toMetres, tooFar)) return refuse("farFromOrigin", tooFar);
    // The base's primary variant: tessellated in the base's own frame (the storage frame: the
    // tessellator's raw winding is the face's orientation under the shape's location, the ADR
    // 0172 section 7 rule; the instance's mirror is the second factor, the flip argument).
    const ColorDecision color = colorOf(base);  // the file's colour (the reader's colour propagation)
    const FaceColours faceColours = faceColoursOf(shapeTool, base);
    SolidMesh mesh = tessellateSolid(solidShape, deflection, angleRadians, toMetres, false, faceColours, color);
    if (!mesh.ok) {
      report.notImported("StepPart", "tessellation-failed", 1);
      report.kernelMessage("tessellation", clip(mesh.error, 64), "error", clip(mesh.error, 240));
      locate(report, "notImported", label, "tessellation failed (" + clip(mesh.error, 120) + "): not imported" + where);
      ++unsupported;
      continue;
    }
    if (mesh.triangles == 0) {
      report.notImported("StepPart", "no-triangles", 1);
      locate(report, "notImported", label, "no triangles: not imported" + where);
      ++unsupported;
      continue;
    }
    if (mesh.triangles > options.maxTrianglesPerElement) {
      report.notImported("StepPart", "meshTriangleLimit", 1);
      report.notice("triangleCapApplied", 1);
      locate(report, "notImported", label,
             std::to_string(mesh.triangles) + " triangles, over the cap of " + std::to_string(options.maxTrianglesPerElement) +
                 " per element: omitted" + where);
      ++omitted;
      continue;
    }
    if (triangles + mesh.triangles > options.maxTotalTriangles) {
      report.notImported("StepPart", "triangleBudget", 1);
      report.notice("triangleBudgetApplied", 1);
      locate(report, "notImported", label,
             "the total budget of " + std::to_string(options.maxTotalTriangles) + " triangles is reached: omitted" + where);
      ++omitted;
      continue;
    }
    if (color.alphaDropped) report.notice("colorAlphaDropped", 1);
    // A shape without a file colour takes the DefaultMaterial (0.7 grey) with the notice (the
    // IFC pattern, ADR 0172 section 4): the package carries the material once, and the
    // unstyled mesh's triangles reference it (a styled one keeps the no-material index - the
    // colour is its own, per triangle).
    std::uint32_t materialIndex = 0xFFFFFFFFu;
    if (!color.rgb) {
      if (package.materials.empty())
        package.materials.push_back(PackageMaterial{"DefaultMaterial", {0.7f, 0.7f, 0.7f}, 1.0f});
      materialIndex = 0u;
      report.notice("defaultMaterial", 1);
    }
    PackageMesh pm;
    pm.vertices = std::move(mesh.vertices);
    pm.triangles = std::move(mesh.indices);
    pm.materials.assign(pm.triangles.size() / 3, materialIndex);
    if (!mesh.colors.empty()) {
      pm.colors = std::move(mesh.colors);
    } else if (color.rgb) {
      pm.colors.resize(pm.triangles.size() / 3);
      for (auto& c : pm.colors) c = color.color;
    }
    package.meshes.push_back(std::move(pm));
    baseMesh[std::make_pair(baseIndex_, 0)] = static_cast<long long>(package.meshes.size()) - 1;
    triangles += mesh.triangles;
    degenerate += mesh.degenerate;
    faces += mesh.faces;
    ++converted;
    // A mirror instance of this base takes its own variant: the shared winding is single, and
    // the decoder's negative-determinant transform reverses it (the variant is tessellated
    // with the winding flipped). The variant's failure (a cap) skips its instances only.
    bool needMirror = false;
    for (std::size_t i = 0; i < order.size(); ++i)
      if (!order[i].isAssembly && order[i].baseIndex == baseIndex_ && order[i].detChain < 0.0) {
        needMirror = true;
        break;
      }
    if (needMirror) {
      SolidMesh mirror = tessellateSolid(solidShape, deflection, angleRadians, toMetres, true, faceColours, color);
      if (mirror.ok && mirror.triangles > 0 && mirror.triangles <= options.maxTrianglesPerElement
          && triangles + mirror.triangles <= options.maxTotalTriangles) {
        PackageMesh mpm;
        mpm.vertices = std::move(mirror.vertices);
        mpm.triangles = std::move(mirror.indices);
        mpm.materials.assign(mpm.triangles.size() / 3, materialIndex);
        if (!mirror.colors.empty()) {
          mpm.colors = std::move(mirror.colors);
        } else if (color.rgb) {
          mpm.colors.resize(mpm.triangles.size() / 3);
          for (auto& c : mpm.colors) c = color.color;
        }
        package.meshes.push_back(std::move(mpm));
        baseMesh[std::make_pair(baseIndex_, 1)] = static_cast<long long>(package.meshes.size()) - 1;
        triangles += mirror.triangles;
        degenerate += mirror.degenerate;
        faces += mirror.faces;
      } else {
        report.notice("mirrorVariantOmitted", 1);
      }
    }
  }

  // The package's nodes: the root (0) and one node per walked entry (the walk order, the package's
  // node depth following the walk's, capped at 128, the package's depth limit). A part's instances
  // share the base's mesh (in the base's own frame); the node's transform carries the placement
  // (the base's location, then the instance's - a mirror's negative determinant is affine, the
  // decoder admits it), and the application composes the tree (the world transform is the product
  // of the chain's).
  // The root: the file as the tree's root (ADR 0172 §4: the product structure under one root).
  PackageNode root;
  root.step = 0;
  root.className = "IfcStepAssembly";
  root.name = clip(baseName(options.file), 2048);  // the 2,048-byte name cap (ADR 0172 §4)
  root.role = "assembly";
  root.parent = -1;
  root.provenance.push_back({"step.Schema", file.schemaRaw});
  root.provenance.push_back({"step.Source", file.sha256});  // the file's SHA-256 (ADR 0172 §4)
  root.provenance.push_back({"step.Entities", static_cast<double>(model.entities)});
  package.nodes.push_back(root);

  std::vector<long long> pkgIndex;  // the walk's order -> its package node
  pkgIndex.resize(order.size(), -1);
  long long assemblyCount = 0, referenceCount = 0, bodyCount = 0;
  for (std::size_t i = 0; i < order.size(); ++i) {
    const FlatNode& flat = order[i];
    const long long parentPackage = flat.parent >= 0 ? pkgIndex[static_cast<std::size_t>(flat.parent)] : 0;
    PackageNode node;
    node.step = 0;  // the provenance carries the shape's tag; the step is the app's own order
    node.parent = parentPackage;
    node.provenance.push_back({"step.Entity", static_cast<double>(std::max(0, flat.tag))});
    // ADR 0172 §4: the body resolves back to its origin - the entity, the schema, the file's digest.
    node.provenance.push_back({"step.Schema", file.schemaRaw});
    node.provenance.push_back({"step.Source", file.sha256});
    const LayerDecision layerDecision = flat.isAssembly ? LayerDecision{} : layersOf(flat.base, layers);
    if (flat.isAssembly) {
      ++assemblyCount;
      node.className = "IfcStepAssembly";
      node.name = labelName(flat.base);  // a sub-assembly occurrence is named after the sub-assembly it places
      if (node.name.empty()) node.name = node.className;  // an unnamed assembly is named after its class
      node.role = "assembly";
      // The assembly's placement (its label's location; its children are placed relative to
      // it), from the pre-pass (the degenerate fallback applied).
      node.transform = flat.transform;
    } else {
      // The part's mesh: the base's, shared by every instance of the same handedness. A
      // mirror instance (its chain's determinant -1, the decoder composes the chain) takes
      // the mirror variant (tessellated with the winding flipped, the tessellateSolid's
      // instanceMirror argument); a missing variant (its tessellation did not convert)
      // has no node.
      const auto key = std::make_pair(flat.baseIndex, flat.detChain < 0.0 ? 1 : 0);
      const auto found = baseMesh.find(key);
      if (found == baseMesh.end()) {
        // No body for this entry: its base was left out (located with the base), or its mirror variant was (located here).
        ++omittedEntries;
        if (flat.detChain < 0.0 && baseMesh.count(std::make_pair(flat.baseIndex, 0)) > 0)
          locate(report, "notImported", std::max<long long>(0, flat.tag),
                 "a mirror instance whose variant was omitted: not imported" + placeOf(static_cast<long long>(i)));
        continue;
      }
      const long long mesh = found->second;
      ++bodyCount;
      node.className = "IfcStepSolid";
      node.name = labelName(flat.base);  // the part's name (its base's; the file's product name)
      if (node.name.empty()) node.name = node.className;  // an unnamed part is named after its class
      node.role = "element";
      node.mesh = mesh;
      if (!layerDecision.name.empty()) node.properties.push_back(PackageNode::Property{"step", "Layer", 2, PackageValue(layerDecision.name)});
      bool truncated = false;
      const std::string metadata = metadataOf(flat.base, truncated);  // the part's metadata (its base's)
      if (!metadata.empty()) {
        node.properties.push_back(PackageNode::Property{"step", "Metadata", 2, PackageValue(clip(metadata, 2048))});
        if (truncated) report.notice("metadataTruncated", 1);
      }
      // The part's placement (ADR 0172 section 4): a point of the base's mesh (in the base's
      // own frame, in metres) is mapped through the base's location, then the label's (the
      // reference's instance placement); the node's transform is the local one (the pre-pass
      // applied the degenerate fallback, the placement count and the step.Placement
      // provenance were tallied there, so a node is never skipped on the transform).
      node.transform = flat.transform;
      if (flat.provenancePlaced) node.provenance.push_back({"step.Placement", 1.0});
      if (flat.reference) {
        ++referenceCount;
        node.provenance.push_back({"step.Reference", 1.0});
      }
    }
    // The 2,048-byte cap (ADR 0172 §4): over it the name is truncated with the notice.
    if (node.name.size() > 2048) {
      report.notice("nameTruncated", 1);
      node.name = clip(node.name, 2048);
    }
    node.hidden = !XCAFDoc_ColorTool::IsVisible(flat.label);
    if (node.hidden) report.notice("invisibleShapes", 1);
    if (layerDecision.hidden) {
      node.hidden = true;
      report.notice("layerHidden", 1);  // the file's layer visibility (an off layer hides its shapes)
    }
    pkgIndex[i] = static_cast<long long>(package.nodes.size());
    package.nodes.push_back(std::move(node));
  }
  if (bodyCount == 0)
    return refuse("nothingImported", "The file transferred, but no part with volume was converted; it is refused (ADR 0172 §3).");

  if (degenerate > 0) report.notice("degenerateTriangles", degenerate);
  JsonOut r;
  r.beginObject();
  r.field("apiVersion", 1);
  r.key("counts").beginObject()
      .field("shapes", static_cast<long long>(order.size()))
      .field("solids", solidCount)
      .field("slots", static_cast<long long>(baseLabels.size()))
      .field("assemblies", assemblyCount)
      .field("references", referenceCount)
      .field("placed", placedCount)
      .field("converted", converted)
      .field("omitted", omitted)
      .field("unsupported", unsupported)
      .field("freeFaces", freeFaces)
      .field("faces", faces)
      .field("triangles", triangles)
      .field("degenerate", degenerate)
      .field("omittedEntries", omittedEntries)
      .endObject();
  writeLists(r, report);
  r.key("notices").beginArray();
  for (const auto& [code, count] : report.notices())
    r.beginObject().field("code", code).field("count", count).endObject();
  r.endArray();
  r.key("kernelMessages").beginArray();
  for (const KernelMessage& m : report.kernelMessages())
    r.beginObject().field("category", m.category).field("code", m.code).field("severity", m.severity).field("count", m.count).field("message", m.sample).endObject();
  r.endArray();
  r.endObject();
  package.reportJson = r.str();

  progress.phase("write");
  std::string problem;
  const std::string bytes = serializePackage(package, problem);
  if (bytes.empty()) return refuse("packageTooLarge", problem);
  {
    std::ofstream out(options.output, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.flush();
    if (!out) return refuse("writeFailed", "The package could not be written.");
  }
  progress.done(static_cast<std::size_t>(converted));
  JsonOut json;
  json.beginObject()
      .field("status", "ok")
      .field("bytes", static_cast<long long>(bytes.size()))
      .field("nodes", static_cast<long long>(package.nodes.size()))
      .field("meshes", static_cast<long long>(package.meshes.size()))
      .field("converted", converted)
      .field("omitted", omitted)
      .field("unsupported", unsupported)
      .field("triangles", triangles)
      .field("freeFaces", freeFaces)
      .field("omittedEntries", omittedEntries)
      .endObject();
  progress.line("R " + json.str());
  return 0;
}

}  // namespace sketchyup::ifc
