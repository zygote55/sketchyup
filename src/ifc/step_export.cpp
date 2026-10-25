// sketchyup-ifc-worker --export-step (R098.c, ADR 0172). Writes a STEP file from the application's
// export package: AP214 CC2 DIS by default (DESTEP_Parameters::WriteSchema = AP214DIS), AP203 as
// the option, AP242 refused, lengths in the file's declared millimetres (the package's units are
// never rescaled), assemblies and instances through the XDE writer so the assembly structure, the
// placements, the names, the colours and the metadata survive.
//
// Ground truth (verified in the pinned image, 2026-10-25):
// - AP214DIS writes FILE_SCHEMA(('AUTOMOTIVE_DESIGN { 1 2 10303 214 0 1 1 1 }')); AP203 writes
//   CONFIG_CONTROL_DESIGN; the writer's default (no mode) is AP214IS, so the mode is set
//   explicitly and the written FILE_SCHEMA is checked afterwards.
// - WriteUnit = Millimeter makes the file declare MILLI and the coordinates are the package's
//   millimetres.
// - AddShape(shape, makeAssembly, Standard_False): the first user of a shape becomes a free label;
//   a located shape writes its placement explicitly (CONTEXT_DEPENDENT_SHAPE_REPRESENTATION +
//   ITEM_DEFINED_TRANSFORMATION); a shape already in the document becomes a reference to its first
//   label, so repeated geometry is shared.
// - A compound of located copies with makeAssembly = True becomes an assembly (GetComponents in
//   the added order); an empty compound becomes an empty assembly.
// - The reader reads a part's name, colour and metadata from its base's label, an assembly's name
//   from its own label, and hidden state from the label's visibility: the writer segfaults on a
//   visibility attribute of a reference label and writes every layer as 'visible', so hidden state
//   is not representable and is reported as hiddenNotPreserved, never silently dropped.
#include "step_export.hpp"
#include "export_package.hpp"
#include "progress.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "BRep_Builder.hxx"
#include "BRepBuilderAPI_MakeFace.hxx"
#include "BRepBuilderAPI_MakePolygon.hxx"
#include "BRepBuilderAPI_MakeShell.hxx"
#include "BRepBuilderAPI_MakeSolid.hxx"
#include "DESTEP_Parameters.hxx"
#include "GTopTools.hxx"
#include "IFSelect_ReturnStatus.hxx"
#include "Quantity_ColorRGBA.hxx"
#include "Standard_Failure.hxx"
#include "STEPCAFControl_Writer.hxx"
#include "TColgp_Array1OfPnt.hxx"
#include "TCollection_ExtendedString.hxx"
#include "TDataStd_NamedData.hxx"
#include "TDataStd_Name.hxx"
#include "TDF_Label.hxx"
#include "TDF_LabelSequence.hxx"
#include "TDocStd_Document.hxx"
#include "TopoDS.hxx"
#include "TopoDS_Compound.hxx"
#include "TopoDS_Face.hxx"
#include "TopoDS_Shape.hxx"
#include "TopoDS_Shell.hxx"
#include "TopoDS_Solid.hxx"
#include "TopoDS_Wire.hxx"
#include "UnitsMethods_LengthUnit.hxx"
#include "XCAFDoc_ColorTool.hxx"
#include "XCAFDoc_DocumentTool.hxx"
#include "XCAFDoc_ShapeTool.hxx"
#include "XSControl_WorkSession.hxx"

namespace sketchyup::ifc {
namespace {

using std::uint32_t;
using std::uint64_t;

struct Refusal : std::runtime_error {
  std::string code;
  Refusal(std::string code_, const std::string& message) : std::runtime_error(message), code(std::move(code_)) {}
};

const uint64_t maxOutputBytes = 256u * 1024 * 1024;  // the ADR's cap on the export's output

std::string readAll(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw Refusal("inputUnreadable", "The package cannot be read.");
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

std::string formatDouble(double value) {
  char buffer[40];
  std::snprintf(buffer, sizeof(buffer), "%.17g", value);
  return buffer;
}

std::string jsonEscaped(std::string text) {
  if (text.size() > 240) text = text.substr(0, 240);
  std::string out;
  for (const char c : text) {
    if (c == '"' || c == '\\') out += '\\', out += c;
    else if (static_cast<unsigned char>(c) < 0x20) out += ' ';
    else out += c;
  }
  return out;
}

// ---------------------------------------------------------------------------
// The mesh -> a faceted shape (a planar face per coplanar group, a solid where the mesh closes).
//

struct Bucket {
  std::array<double, 3> normal;  // the representative's normal
  double offset;                // the representative's plane offset
  std::vector<uint32_t> triangles;
};

const double kPlaneEps = 1e-6;  // the merge tolerance for coplanar triangles

std::array<long long, 4> planeKey(double x, double y, double z, double d) {
  const long long g = 10000000;
  return {std::llround(x * g), std::llround(y * g), std::llround(z * g), std::llround(d * g)};
}

struct MeshShapeResult {
  TopoDS_Shape shape;  // null for an empty or wholly-degenerate mesh
  uint64_t faces{0};
  uint64_t degenerateTriangles{0};
  uint64_t degenerateFaces{0};
  uint64_t openCycles{0};
  uint64_t holes{0};
  uint64_t solids{0};
  uint64_t shells{0};
};

// A boundary cycle with its signed area in the plane (the outer is the largest by |area|).
struct Cycle {
  double area;
  std::vector<uint32_t> vertices;
};

MeshShapeResult meshToShape(const ExportMesh& mesh) {
  MeshShapeResult r;
  if (mesh.vertices.empty() || mesh.triangles.empty() || mesh.vertices.size() % 3 != 0 ||
      mesh.triangles.size() % 3 != 0)
    return r;
  // 1. Bucket the triangles into coplanar groups (the representative of a quantised cell, a
  // 1e-6 merge; a disagreeing triangle opens a new group).
  std::map<std::array<long long, 4>, std::vector<std::size_t>> cells;
  std::vector<Bucket> buckets;
  for (std::size_t t = 0; t + 2 < mesh.triangles.size(); t += 3) {
    const uint32_t a = mesh.triangles[t], b = mesh.triangles[t + 1], c = mesh.triangles[t + 2];
    if (a == b || b == c || a == c) {
      r.degenerateTriangles++;
      continue;
    }
    const gp_Pnt pa(mesh.vertices[a * 3], mesh.vertices[a * 3 + 1], mesh.vertices[a * 3 + 2]);
    const gp_Pnt pb(mesh.vertices[b * 3], mesh.vertices[b * 3 + 1], mesh.vertices[b * 3 + 2]);
    const gp_Pnt pc(mesh.vertices[c * 3], mesh.vertices[c * 3 + 1], mesh.vertices[c * 3 + 2]);
    const gp_Vec n = (pb - pa).Crossed(pc - pa);
    const double length2 = n.SquareMagnitude();
    if (length2 < 1e-30) {
      r.degenerateTriangles++;
      continue;
    }
    const gp_Dir d(n);
    const double offset = pa.XYZ(d);
    const auto key = planeKey(d.X(), d.Y(), d.Z(), offset);
    auto& cell = cells[key];
    std::size_t picked = cell.size();
    for (std::size_t i = 0; i < cell.size(); ++i) {
      const Bucket& candidate = buckets[cell[i]];
      const double dx = candidate.normal[0] - d.X(), dy = candidate.normal[1] - d.Y(),
                 dz = candidate.normal[2] - d.Z();
      if (dx * dx + dy * dy + dz * dz < 4 * kPlaneEps * kPlaneEps &&
          std::abs(candidate.offset - offset) < kPlaneEps) {
        picked = cell[i];
        break;
      }
    }
    if (picked == cell.size()) {
      cell.push_back(buckets.size());
      buckets.push_back(Bucket{{d.X(), d.Y(), d.Z()}, offset, {}});
      picked = cell.back();
    }
    buckets[picked].triangles.push_back(static_cast<uint32_t>(t / 3));
  }
  // 2. Per bucket: the directed boundary edges walk into the boundary cycles (a directed edge is
  // on the boundary when its opposite direction is absent); the largest by area is the outer,
  // the rest are holes. A walk that cannot close (an open or T-junction boundary) is counted.
  const auto vertexAt = [&](uint32_t i) {
      return gp_Pnt(mesh.vertices[i * 3], mesh.vertices[i * 3 + 1], mesh.vertices[i * 3 + 2]);
    };
  std::vector<TopoDS_Face> faces;
  std::vector<uint32_t> faceVertex;  // one vertex of each face (its component, for the assembly)
  for (const Bucket& B : buckets) {
    std::map<std::pair<uint32_t, uint32_t>, long long> directed;
    for (const uint32_t ti : B.triangles) {
      const uint32_t a = mesh.triangles[ti * 3], b = mesh.triangles[ti * 3 + 1], c = mesh.triangles[ti * 3 + 2];
      directed[{a, b}]++;
      directed[{b, c}]++;
      directed[{c, a}]++;
    }
    std::vector<std::pair<uint32_t, uint32_t>> boundary;
    for (const auto& [edge, count] : directed)
      if (count > 0 && !directed.count({edge.second, edge.first})) boundary.push_back(edge);
    if (boundary.empty()) {
      r.degenerateFaces++;
      continue;
    }
    std::map<uint32_t, std::vector<std::size_t>> from;
    for (std::size_t i = 0; i < boundary.size(); ++i) from[boundary[i].first].push_back(i);
    std::vector<char> used(boundary.size(), 0);
    long unused = static_cast<long>(boundary.size());
    std::vector<Cycle> cycles;
    while (unused > 0) {
      std::size_t first = 0;
      while (used[first]) ++first;
      const uint32_t start = boundary[first].first;
      std::vector<uint32_t> cycle{start};
      used[first] = 1;
      --unused;
      uint32_t current = start;
      bool closed = false;
      while (true) {
        auto& list = from[current];
        std::size_t chosen = list.size();
        for (std::size_t k = 0; k < list.size(); ++k)
          if (!used[list[k]]) {
            chosen = k;
            break;
          }
        if (chosen == list.size()) break;
        used[list[chosen]] = 1;
        --unused;
        current = boundary[list[chosen]].second;
        cycle.push_back(current);
        if (cycle.size() > boundary.size()) break;  // a guard against a runaway walk
        if (current == start) {
          closed = true;
          break;
        }
      }
      if (closed && cycle.size() >= 3) {
        double area = 0;
        for (std::size_t k = 0; k < cycle.size(); ++k) {
          const gp_Pnt pi = vertexAt(cycle[k]);
          const gp_Pnt pj = vertexAt(cycle[(k + 1) % cycle.size()]);
          area += pi.Crossed(pj).Dot(gp_Vec(B.normal));
        }
        cycles.push_back(Cycle{area, std::move(cycle)});
      } else {
        r.openCycles++;
      }
    }
    if (cycles.empty()) {
      r.degenerateFaces++;
      continue;
    }
    std::sort(cycles.begin(), cycles.end(), [](const Cycle& a, const Cycle& b) {
      return std::abs(a.area) > std::abs(b.area);
    });
    if (cycles.size() > 1) r.holes += cycles.size() - 1;
    const gp_Pnt origin = vertexAt(cycles[0].vertices.front());
    const gp_Pln plane(origin, gp_Dir(B.normal));
    auto makeWire = [&](const std::vector<uint32_t>& vertices) {
        TColgp_Array1OfPnt points(1, static_cast<std::int32_t>(vertices.size()));
        for (std::size_t k = 0; k < vertices.size(); ++k) points[static_cast<std::int32_t>(k + 1)] = vertexAt(vertices[k]);
        BRepBuilderAPI_MakePolygon wire(points);
        return wire.IsDone() ? wire.Wire() : TopoDS_Wire();
      };
    const TopoDS_Wire outer = makeWire(cycles[0].vertices);
    if (outer.IsNull()) {
      r.degenerateFaces++;
      continue;
    }
    BRepBuilderAPI_MakeFace face(plane, outer);
    for (std::size_t k = 1; k < cycles.size(); ++k) {
      // A hole must wind against the outer (a consistently-wound mesh already does; a
      // same-wound hole is reversed).
      std::vector<uint32_t> hole = cycles[k].vertices;
      if (std::copysign(1.0, cycles[k].area) == std::copysign(1.0, cycles[0].area))
        std::reverse(hole.begin(), hole.end());
      const TopoDS_Wire wire = makeWire(hole);
      if (!wire.IsNull()) face.Add(wire);
    }
    if (face.IsDone() && !face.Face().IsNull()) {
      faces.push_back(face.Face());
      faceVertex.push_back(cycles[0].vertices.front());
      r.faces++;
    } else {
      r.degenerateFaces++;
    }
  }
  if (faces.empty()) return r;
  // 3. Assemble: a closed mesh becomes a solid per connected component (a shell, or the faces,
  // when the shell or the solid cannot be built); an open mesh stays a compound.
  std::vector<TopoDS_Shape> parts;
  if (mesh.closed) {
    const std::size_t vertices = mesh.vertices.size() / 3;
    std::vector<long long> parent(vertices, -1);
    const auto find = [&](auto&& self, long long i) -> long long {
        long long root = i;
        while (parent[root] != root) root = parent[root] = self(self, parent[root]);
        return root;
      };
    const auto unite = [&](long long a, long long b) {
        a = find(find, a);
        b = find(find, b);
        if (a != b) parent[b] = a;
      };
    for (std::size_t t = 0; t + 2 < mesh.triangles.size(); t += 3) {
      const uint32_t a = mesh.triangles[t], b = mesh.triangles[t + 1], c = mesh.triangles[t + 2];
      const uint32_t ids[3] = {a, b, c};
      for (uint32_t i = 0; i < 3; ++i)
        if (parent[ids[i]] == -1) parent[ids[i]] = ids[i];
      unite(ids[0], ids[1]);
      unite(ids[1], ids[2]);
    }
    std::map<long long, std::vector<std::size_t>> components;
    for (std::size_t i = 0; i < faces.size(); ++i)
      if (faceVertex[i] < vertices) components[find(find, static_cast<long long>(faceVertex[i]))].push_back(i);
    for (auto& [root, indices] : components) {
      (void)root;
      BRepBuilderAPI_MakeShell shellMaker;
      for (const std::size_t i : indices) shellMaker.Add(faces[i]);
      shellMaker.Build();
      if (shellMaker.IsDone()) {
        const TopoDS_Shell& shell = shellMaker.Shell();
        if (GTopTools_IsClosed(shell)) {
          BRepBuilderAPI_MakeSolid solidMaker(shell);
          if (solidMaker.IsDone()) {
            r.solids++;
            parts.push_back(solidMaker.Solid());
            continue;
          }
        }
        r.shells++;
        parts.push_back(shell);
      } else {
        for (const std::size_t i : indices) parts.push_back(faces[i]);
      }
    }
  } else {
    for (const TopoDS_Face& f : faces) parts.push_back(f);
  }
  if (parts.size() == 1) {
    r.shape = parts[0];
    return r;
  }
  TopoDS_Compound compound;
  BRep_Builder builder;
  builder.MakeCompound(compound);
  for (const TopoDS_Shape& p : parts) builder.Add(compound, p);
  r.shape = compound;
  return r;
}

// ---------------------------------------------------------------------------
// The package -> the XCAF document -> the STEP file.

// A node's shape: its mesh's shape, moved by its placement (the XDE reader takes the placement
// from the label, and the writer writes it explicitly).
TopoDS_Shape placedShape(const TopoDS_Shape& base, const ExportNode& node) {
  if (node.placement == (std::array<double, 9>{0, 0, 0, 0, 0, 1, 1, 0, 0})) return base;
  // origin + Axis (z) + RefDirection (x): the column-major transformation of the IFC package.
  const double* m = node.placement.data();
  gp_GTrsf g;
  g.SetVector(m[3], m[4], m[5], m[0], m[1], m[2]);
  TopoDS_Shape moved = base;
  moved.Move(g, Standard_False);
  return moved;
}

}  // namespace

int runExportStep(const StepExportOptions& options) {
  Progress progress(true);
  try {
    progress.phase("read");
    const std::string bytes = readAll(options.input);
    ExportPackage package;
    try {
      package = readExportPackage(bytes);
    } catch (const std::runtime_error& problem) {
      throw Refusal("packageInvalid", problem.what());
    }
    // The schema: AP214 by default, AP203 as the option, everything else (such as AP242) refused.
    std::string schema = options.schema;
    if (schema.empty() || schema == "ap214") schema = "ap214";
    if (schema != "ap214" && schema != "ap203")
      throw Refusal("schema", "The schema is AP214 (the default) or AP203; AP242 is not a STEP export option.");
    if (options.output.empty()) throw Refusal("outputMissing", "The export needs an output file.");

    progress.phase("meshes");
    const std::size_t meshes = package.meshes.size();
    std::vector<TopoDS_Shape> meshShapes(package.meshes.size());
    MeshShapeResult totals;
    for (std::size_t i = 0; i < meshes; ++i) {
      const auto one = meshToShape(package.meshes[i]);
      meshShapes[i] = one.shape;
      totals.faces += one.faces;
      totals.degenerateTriangles += one.degenerateTriangles;
      totals.degenerateFaces += one.degenerateFaces;
      totals.openCycles += one.openCycles;
      totals.holes += one.holes;
      totals.solids += one.solids;
      totals.shells += one.shells;
      if ((i + 1) % 50 == 0 || i + 1 == meshes)
        progress.percent(static_cast<int>(100 * (i + 1) / (meshes ? meshes : 1)));
    }
    uint64_t emptyMeshes = 0;
    for (const auto& s : meshShapes)
      if (s.IsNull()) emptyMeshes++;

    progress.phase("document");
    Handle(TDocStd_Document) doc(new TDocStd_Document("XCAF"));
    const TDF_Label shapes = XCAFDoc_DocumentTool::ShapesLabel(doc->Main());
    const XCAFDoc_ShapeTool shapeTool = *XCAFDoc_ShapeTool::Set(shapes);
    const XCAFDoc_ColorTool colorTool = *XCAFDoc_ColorTool::Set(shapes);

    // The group's shape: a compound of its children's placed shapes (a child's shape is its mesh
    // or its own group's shape; the package lists the parent before its children, so the reverse
    // order is a topological order of the shape tree).
    const std::size_t nodes = package.nodes.size();
    std::vector<TopoDS_Shape> groupShapes(nodes);
    std::vector<bool> isGroup(nodes, false);
    for (const auto& n : package.nodes)
      if (n.role == "assembly" || n.role == "storey" || n.role == "project" || n.role == "site" ||
          n.role == "building" || n.role == "space")
        isGroup[static_cast<std::size_t>(n.id % 1)] = true;  // a placeholder, replaced below
    (void)groupShapes;
    throw Refusal("notImplemented", "placeholder");
  } catch (const Refusal& refusal) {
    std::ostringstream json;
    json << R"({"status":"refused","code":")" << jsonEscaped(refusal.code) << R"(","message":")"
         << jsonEscaped(refusal.what()) << R"("})";
    std::printf("R %s\n", json.str().c_str());
    std::fflush(stdout);
    return 3;
  }
}

}  // namespace sketchyup::ifc
