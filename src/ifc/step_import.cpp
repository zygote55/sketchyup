// The STEP import (ADR 0172, R098.a and R098.a2): the file's metadata through the basic OCCT
// reader and the unit policy of §2, and the tessellated import - the reader's transferred
// solids tessellated with BRepMesh and shipped in the conversion package. Nothing is ever
// silently rescaled and a unit is never guessed.
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

#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "Bnd_Box.hxx"
#include "BRepBndLib.hxx"
#include "BRepMesh_IncrementalMesh.hxx"
#include "BRep_Tool.hxx"
#include "HeaderSection_FileSchema.hxx"
#include "IFSelect_ReturnStatus.hxx"
#include "Interface_EntityIterator.hxx"
#include "Poly_Triangle.hxx"
#include "Poly_Triangulation.hxx"
#include "STEPControl_Reader.hxx"
#include "Standard_Failure.hxx"
#include "StepData_StepModel.hxx"
#include "TColStd_SequenceOfAsciiString.hxx"
#include "TopExp_Explorer.hxx"
#include "TopoDS.hxx"
#include "TopoDS_Face.hxx"
#include "TopoDS_Shape.hxx"
#include "TopoDS_Solid.hxx"

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

// The factor to metres of a unit name as STEPControl_Reader::FileUnits returns it (the same
// table as the probe's, so the report and the import agree): the SI unit as getSiName
// spells it (a lowercase prefix, the base "metre"; no prefix the plain metre), the
// conversion unit as the file wrote its quoted name. nullopt: declared but not
// convertible from the name.
std::optional<double> unitMetres(const std::string& name) {
  static const std::map<std::string, double> factors = {
      {"millimetre", 1e-3}, {"centimetre", 1e-2}, {"decimetre", 1e-1}, {"metre", 1.0},
      {"decametre", 1e1}, {"hectometre", 1e2}, {"kilometre", 1e3}, {"megametre", 1e6},
      {"gigametre", 1e9}, {"terametre", 1e12}, {"petametre", 1e15}, {"exametre", 1e18},
      {"nanometre", 1e-9}, {"micrometre", 1e-6}, {"picometre", 1e-12}, {"femtometre", 1e-15},
      {"attometre", 1e-18},
      {"inch", 0.0254}, {"foot", 0.3048},
  };
  std::string folded = name;
  for (std::size_t i = 0; i < folded.size(); ++i) folded[i] = std::tolower(static_cast<unsigned char>(folded[i]));
  const auto entry = factors.find(folded);
  return entry == factors.end() ? std::nullopt : std::optional<double>(entry->second);
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

// One transferred shape for the tessellation: its solids (the importable entities, ADR
// 0172 §3) and its free faces (surfaces without volume, reported as not imported).
struct StepShapeTessellation {
  TopoDS_Shape shape;
  std::vector<TopoDS_Shape> solids;
  long long faces = 0;
  long long freeFaces = 0;
};

struct StepTessellationModel {
  StepModelFacts facts;  // the model's facts (read, schema, units, entities, roots, transferred)
  std::vector<StepShapeTessellation> shapes;
};

// Read, transfer and keep the shapes for the tessellated import (the probe's reader, with
// the shapes retained and the free faces counted).
StepTessellationModel readStepTessellationModel(const std::string& path) {
  StepTessellationModel m;
  m.facts.unitsDeclared = stepFileFacts(path).lengthDeclared;  // the "declared" fact (the raw text)
  STEPControl_Reader reader;
  try {
    const IFSelect_ReturnStatus status = reader.ReadFile(path.c_str());
    m.facts.read = status == IFSelect_RetDone;
    m.facts.readStatus = m.facts.read ? "done" : status == IFSelect_RetFail ? "fail" : "error";
  } catch (const Standard_Failure& failure) {
    m.facts.readStatus = "exception";
    m.facts.error = failure.GetMessageString();
    return m;
  }
  if (!m.facts.read) return m;
  try {
    const Handle(StepData_StepModel) model = Handle(StepData_StepModel)::DownCast(reader.Model());
    if (model.IsNull()) {
      m.facts.read = false;
      m.facts.readStatus = "no-model";
      return m;
    }
    // The schema: the header's FILE_SCHEMA entity (7.9 has no static GetID; walk the
    // header, as the probe does).
    Handle(HeaderSection_FileSchema) fileSchema;
    Interface_EntityIterator header = model->Header();
    while (header.More()) {
      fileSchema = Handle(HeaderSection_FileSchema)::DownCast(header.Value());
      if (!fileSchema.IsNull()) break;
      header.Next();
    }
    if (!fileSchema.IsNull()) {
      for (int i = 1; i <= fileSchema->NbSchemaIdentifiers(); ++i) {
        if (i > 1) m.facts.schema += " ";
        m.facts.schema += fileSchema->SchemaIdentifiersValue(i)->ToCString();
      }
    }
    // The unit the file applies to its shapes (FileUnits; the model-level source the raw
    // text's factor stands behind when it resolves no unit - decideUnits carries the
    // policy).
    TColStd_SequenceOfAsciiString length, angle, solid;
    reader.FileUnits(length, angle, solid);
    if (length.Length() > 0) {
      m.facts.lengthUnit = length.Value(1).ToCString();
      m.facts.lengthUnitResolved = true;
    }
    if (angle.Length() > 0) m.facts.angleUnit = angle.Value(1).ToCString();
    if (solid.Length() > 0) m.facts.solidAngleUnit = solid.Value(1).ToCString();
    if (const auto metres = unitMetres(m.facts.lengthUnit)) m.facts.lengthMetres = *metres;
    m.facts.entities = reader.Model()->NbEntities();
    m.facts.roots = reader.NbRootsForTransfer();
    m.facts.transferred = reader.TransferRoots();
    for (int i = 1; i <= reader.NbShapes(); ++i) {
      StepShapeTessellation st;
      st.shape = reader.Shape(i);
      {
        TopExp_Explorer e(st.shape, TopAbs_FACE);
        while (e.More()) {
          ++st.faces;
          e.Next();
        }
      }
      long long solidFaces = 0;
      TopExp_Explorer e(st.shape, TopAbs_SOLID);
      while (e.More()) {
        const TopoDS_Shape solid = e.Current();
        {
          TopExp_Explorer f(solid, TopAbs_FACE);
          while (f.More()) {
            ++solidFaces;
            f.Next();
          }
        }
        st.solids.push_back(solid);
        e.Next();
      }
      st.freeFaces = std::max(0LL, st.faces - solidFaces);
      m.shapes.push_back(std::move(st));
    }
  } catch (const Standard_Failure& failure) {
    m.facts.error = failure.GetMessageString();
  }
  return m;
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

SolidMesh tessellateSolid(const TopoDS_Shape& solid, const double deflectionMm, const double angleRadians,
                          const double metresPerUnit) {
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
        // The winding is the product of the face's orientation and the location's handedness:
        // a reversed face under a mirror location is reversed twice, which is forward —
        // XOR, not OR (each rule fires once).
        const bool flip = (f.Orientation() == TopAbs_REVERSED) != mirrorLocation(loc);
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

  const StepTessellationModel in = readStepTessellationModel(options.file);
  const StepModelFacts& model = in.facts;
  if (!model.read || !model.error.empty())
    return refuse("read", model.error.empty() ? "The STEP reader failed to read the file." : clip(model.error, 200));

  const UnitDecision units = decideUnits(file, model, options.lengthUnitOverride);
  if (!units.status.empty()) return refuse(units.status == "refused-option" ? "option" : "units", units.notices.front());
  const double metresPerUnit = units.mmPerUnit * 0.001;  // mm -> m: the package's unit, the only scale step

  if (in.shapes.empty()) return refuse("nothingToImport", "The file read, but no shape could be transferred.");
  long long totalSolids = 0, noSolidShapes = 0, freeFaces = 0;
  for (const StepShapeTessellation& s : in.shapes) {
    totalSolids += static_cast<long long>(s.solids.size());
    if (s.solids.empty()) ++noSolidShapes;
    freeFaces += s.freeFaces;
  }
  // ADR 0172 §3: the accounting identity (converted + supporting + omitted = counted) is
  // kept with the solids as the counted entities, a shape without a solid counting for its
  // not-imported share, and a solid's free faces reported, never silently dropped.
  long long slots = totalSolids + noSolidShapes;

  Report report;
  long long converted = 0, omitted = 0, unsupported = 0, triangles = 0, degenerate = 0, faces = 0;
  for (const StepShapeTessellation& s : in.shapes) {
    if (s.solids.empty()) report.notImported("shape", "no-solid", 1);
    if (s.freeFaces > 0) report.notImported("face", "free-surface", s.freeFaces);
  }
  if (noSolidShapes > 0) report.notice("noSolidShapes", noSolidShapes);
  if (freeFaces > 0) report.notice("freeFaces", freeFaces);

  progress.phase("tessellate");
  progress.tasks(slots);
  PackageData package;
  package.engineJson = engineReportJson();
  JsonOut source;
  source.beginObject().field("file", baseName(options.file)).field("bytes", file.bytes).field("schema", file.schema).field("schemaRaw", file.schemaRaw).field("entities", model.entities).endObject();
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
      .field("metresPerUnit", metresPerUnit)
      .field("angle", file.angle)
      .field("solidAngle", file.solidAngle)
      .endObject();
  package.unitsJson = unitsJson.str();
  for (const std::string& notice : units.notices) report.notice(notice, 1);

  std::vector<TopoDS_Shape> slot;  // every solid, and a marker per solid-less shape
  slot.reserve(slots);
  auto mark = [&] { slot.push_back(TopoDS_Shape()); };
  for (const StepShapeTessellation& s : in.shapes) {
    if (s.solids.empty()) mark();
    for (const TopoDS_Shape& solid : s.solids) slot.push_back(solid);
  }
  int solidOrdinal = 0;
  for (long long i = 0; i < static_cast<long long>(slot.size()); ++i) {
    progress.at(i);
    const TopoDS_Shape& s = slot[static_cast<std::size_t>(i)];
    if (s.IsNull()) {  // a solid-less shape: omitted and reported, nothing to convert
      ++omitted;
      continue;
    }
    if (triangles >= options.maxTotalTriangles) {
      report.notImported("solid", "triangleBudget", 1);
      ++omitted;
      report.notice("triangleBudgetApplied", 1);
      continue;
    }
    std::string tooFar;
    if (solidTooFarFromOrigin(s, metresPerUnit, tooFar)) return refuse("farFromOrigin", tooFar);
    SolidMesh mesh = tessellateSolid(s, options.deflectionMm, options.angleDeg * kPi / 180.0, metresPerUnit);
    if (!mesh.ok) {
      report.notImported("solid", "tessellation-failed", 1);
      report.kernelMessage("tessellation", clip(mesh.error, 64), "error", clip(mesh.error, 240));
      ++unsupported;
      continue;
    }
    if (mesh.triangles == 0) {
      report.notImported("solid", "no-triangles", 1);
      ++unsupported;
      continue;
    }
    if (mesh.triangles > options.maxTrianglesPerElement) {
      report.notImported("solid", "meshTriangleLimit", 1);
      ++omitted;
      report.notice("triangleCapApplied", 1);
      continue;
    }
    if (triangles + mesh.triangles > options.maxTotalTriangles) {
      report.notImported("solid", "triangleBudget", 1);
      ++omitted;
      report.notice("triangleBudgetApplied", 1);
      continue;
    }
    const int meshIndex = static_cast<int>(package.meshes.size());
    PackageMesh pm;
    pm.vertices = std::move(mesh.vertices);
    pm.triangles = std::move(mesh.indices);
    // No material (0xFFFFFFFF, the decoder's noMaterial): the step import's colours and
    // materials ride R098.b's mapping; the package's material list stays empty. One entry
    // per triangle (the decoder reads one u32 per triangle, the manifest the /3 count).
    pm.materials.assign(pm.triangles.size() / 3, 0xFFFFFFFFu);
    package.meshes.push_back(std::move(pm));
    PackageNode node;
    node.step = 0;  // the solid's shape index rides the provenance; the step is the app's own order
    node.className = "IfcStepSolid";
    node.name = "Solid " + std::to_string(++solidOrdinal);
    node.role = "element";
    node.parent = 0;  // the root node
    node.mesh = meshIndex;
    node.provenance.push_back({"step.Solid", static_cast<double>(solidOrdinal)});
    package.nodes.push_back(std::move(node));
    triangles += mesh.triangles;
    degenerate += mesh.degenerate;
    faces += mesh.faces;
    ++converted;
  }
  if (converted == 0) return refuse("nothingImported", "No solid could be converted; the package is not written.");
  // The root: the file's product structure as one group (ADR 0172 §4's assembly mapping is
  // R098.b; a flat tree under one root keeps the package decodable and the solids named).
  PackageNode root;
  root.className = "IfcStepAssembly";
  root.name = clip(baseName(options.file), 4096);
  root.role = "assembly";
  root.parent = -1;
  root.provenance.push_back({"step.Schema", file.schemaRaw});
  root.provenance.push_back({"step.Source", baseName(options.file)});
  root.provenance.push_back({"step.Entities", static_cast<double>(model.entities)});
  package.nodes.insert(package.nodes.begin(), std::move(root));  // the root first: the node 0 every solid parents to

  if (degenerate > 0) report.notice("degenerateTriangles", degenerate);
  JsonOut r;
  r.beginObject();
  r.field("apiVersion", 1);
  r.key("counts").beginObject()
      .field("shapes", static_cast<long long>(in.shapes.size()))
      .field("solids", totalSolids)
      .field("slots", slots)
      .field("converted", converted)
      .field("omitted", omitted)
      .field("unsupported", unsupported)
      .field("freeFaces", freeFaces)
      .field("faces", faces)
      .field("triangles", triangles)
      .field("degenerate", degenerate)
      .endObject();
  r.key("notImported").beginArray();
  for (const auto& [key, count] : report.notImportedClasses())
    r.beginObject().field("class", key.first).field("reason", key.second).field("count", count).endObject();
  r.endArray();
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
      .endObject();
  progress.line("R " + json.str());
  return 0;
}

}  // namespace sketchyup::ifc
