// sketchyup-ifc-worker --export-step (R098.c, ADR 0172 section 8). Writes a STEP file from the
// application's SKSTPX01 package through the XDE writer: AP214 DIS by default, AP203 as the option,
// AP242 refused, millimetres. Groups become assemblies; an element becomes a part, and every element
// that shares its part (the same mesh, name, layer and visibility) is one more instance of it.
#include "step_export.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "BRep_Builder.hxx"
#include "BRepBuilderAPI_MakeEdge.hxx"
#include "BRepBuilderAPI_MakeFace.hxx"
#include "BRepBuilderAPI_MakeVertex.hxx"
#include "DESTEP_Parameters.hxx"
#include "IFSelect_ReturnStatus.hxx"
#include "Quantity_Color.hxx"
#include "STEPCAFControl_Writer.hxx"
#include "STEPControl_StepModelType.hxx"
#include "Standard_Failure.hxx"
#include "TCollection_ExtendedString.hxx"
#include "TDF_Label.hxx"
#include "TDataStd_Name.hxx"
#include "TDocStd_Document.hxx"
#include "TopLoc_Location.hxx"
#include "TopoDS.hxx"
#include "TopoDS_Edge.hxx"
#include "TopoDS_Face.hxx"
#include "TopoDS_Shape.hxx"
#include "TopoDS_Shell.hxx"
#include "TopoDS_Solid.hxx"
#include "TopoDS_Vertex.hxx"
#include "TopoDS_Wire.hxx"
#include "UnitsMethods_LengthUnit.hxx"
#include "XCAFDoc_ColorTool.hxx"
#include "XCAFDoc_DocumentTool.hxx"
#include "XCAFDoc_LayerTool.hxx"
#include "XCAFDoc_ShapeTool.hxx"
#include "XSControl_WorkSession.hxx"
#include "gp_Ax3.hxx"
#include "gp_Dir.hxx"
#include "gp_Pln.hxx"
#include "gp_Pnt.hxx"
#include "gp_Trsf.hxx"
#include "gp_Vec.hxx"

#include "progress.hpp"
#include "step_export_package.hpp"

namespace sketchyup::ifc {
namespace {

using Edge = std::pair<std::uint32_t, std::uint32_t>;  // a directed edge: from, to

struct Refusal : std::runtime_error {
  std::string code;
  Refusal(std::string code_, const std::string& message) : std::runtime_error(message), code(std::move(code_)) {}
};

// The file's whole text (the package, or the STEP file the writer made).
std::string readFile(const std::string& path, const char* what) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw Refusal("packageInvalid", std::string("The ") + what + " cannot be read.");
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

// UTF-8 to the OCCT's UTF-16 text; a byte that is not UTF-8 becomes '?'.
TCollection_ExtendedString extended(const std::string& text) {
  std::u16string units;
  for (std::size_t i = 0; i < text.size();) {
    const auto lead = static_cast<unsigned char>(text[i]);
    std::uint32_t code = lead;
    std::size_t extra = 0;
    if (lead >= 0x80) {
      if ((lead >> 5) == 0x6) { code = lead & 0x1Fu; extra = 1; }
      else if ((lead >> 4) == 0xE) { code = lead & 0x0Fu; extra = 2; }
      else if ((lead >> 3) == 0x1E) { code = lead & 0x07u; extra = 3; }
      else { units += u'?'; ++i; continue; }
    }
    bool valid = i + extra < text.size();
    for (std::size_t k = 1; valid && k <= extra; ++k) {
      const auto continuation = static_cast<unsigned char>(text[i + k]);
      if ((continuation & 0xC0) != 0x80) valid = false;
      else code = (code << 6) | (continuation & 0x3Fu);
    }
    if (!valid) { units += u'?'; ++i; continue; }
    i += extra + 1;
    if (code >= 0x10000) {
      code -= 0x10000;
      units += static_cast<char16_t>(0xD800 + (code >> 10));
      units += static_cast<char16_t>(0xDC00 + (code & 0x3FFu));
    } else {
      units += static_cast<char16_t>(code);
    }
  }
  return TCollection_ExtendedString(units.c_str());
}

// The placement (origin, Axis z, RefDirection x) as the rigid transformation it is.
gp_Trsf placementOf(const std::array<double, 9>& p) {
  const gp_Ax3 frame(gp_Pnt(p[0], p[1], p[2]), gp_Dir(p[3], p[4], p[5]), gp_Dir(p[6], p[7], p[8]));
  const gp_Dir x = frame.XDirection(), y = frame.YDirection(), z = frame.Direction();
  gp_Trsf t;
  t.SetValues(x.X(), y.X(), z.X(), p[0], x.Y(), y.Y(), z.Y(), p[1], x.Z(), y.Z(), z.Z(), p[2]);
  return t;
}

Quantity_Color colourOf(const std::array<float, 3>& rgb) {
  return Quantity_Color(rgb[0], rgb[1], rgb[2], Quantity_TOC_RGB);
}

// ---------------------------------------------------------------------------------------------------
// The faces of one mesh: one planar face per native face, its boundary walked from the triangles'
// directed edges onto shared edges and vertices, so a closed mesh is a closed shell and a solid.

class Shaper {
public:
  struct Colored {
    TopoDS_Face face;
    std::uint32_t colour;
  };

  // `placement` moves every vertex: a top-level element is built already placed (a free label with identity location,
  // which the writer keeps as one part; a located free shape is written as a one-component assembly instead).
  explicit Shaper(const StepExportMesh& mesh, const gp_Trsf& placement = gp_Trsf())
      : mesh_(mesh), placement_(placement), vertices_(mesh.vertices.size() / 3) {}

  // The mesh's shape: a solid when the mesh is closed, else a shell. `faces` lists every face with its colour.
  TopoDS_Shape build(std::vector<Colored>& faces) {
    std::vector<std::vector<std::uint32_t>> groups(mesh_.faces);
    for (std::size_t t = 0; t < mesh_.faceOf.size(); ++t) groups[mesh_.faceOf[t]].push_back(static_cast<std::uint32_t>(t));
    for (std::size_t f = 0; f < groups.size(); ++f)
      if (!groups[f].empty()) addGroup(groups[f], mesh_.faceColor[f], faces);
    if (faces.empty()) return TopoDS_Shape();
    BRep_Builder builder;
    TopoDS_Shell shell;
    builder.MakeShell(shell);
    for (const Colored& c : faces) builder.Add(shell, c.face);
    if (!mesh_.closed) return shell;
    TopoDS_Solid solid;
    builder.MakeSolid(solid);
    builder.Add(solid, shell);
    return solid;
  }

  std::uint64_t fallbacks{};   // native faces written as one face per triangle (their boundary did not close)
  std::uint64_t degenerate{};  // triangles that make no face (zero area)

private:
  void addGroup(const std::vector<std::uint32_t>& group, std::uint32_t colour, std::vector<Colored>& faces) {
    TopoDS_Face face;
    if (groupFace(group, face)) {
      faces.push_back({face, colour});
      return;
    }
    if (group.size() == 1) {
      ++degenerate;
      return;
    }
    ++fallbacks;
    for (const std::uint32_t t : group) addGroup({t}, colour, faces);
  }

  gp_Pnt point(std::uint32_t i) const {
    const gp_Pnt p(mesh_.vertices[std::size_t(i) * 3], mesh_.vertices[std::size_t(i) * 3 + 1], mesh_.vertices[std::size_t(i) * 3 + 2]);
    return p.Transformed(placement_);
  }

  TopoDS_Vertex vertex(std::uint32_t i) {
    TopoDS_Vertex& v = vertices_[i];
    if (v.IsNull()) v = BRepBuilderAPI_MakeVertex(point(i)).Vertex();
    return v;
  }

  // The edge between two vertices, made once and shared by every face that uses it.
  bool edge(std::uint32_t a, std::uint32_t b, TopoDS_Edge& out) {
    const std::uint64_t key = (std::uint64_t(a) << 32) | b;
    auto found = edges_.find(key);
    if (found == edges_.end()) {
      BRepBuilderAPI_MakeEdge maker(vertex(a), vertex(b));
      if (!maker.IsDone()) return false;
      found = edges_.emplace(key, maker.Edge()).first;
    }
    out = found->second;
    return true;
  }

  // A cycle's wire, each shared edge oriented as the cycle walks it.
  bool wireOf(const std::vector<Edge>& cycle, TopoDS_Wire& wire) {
    BRep_Builder builder;
    builder.MakeWire(wire);
    for (const Edge& e : cycle) {
      const std::uint32_t low = std::min(e.first, e.second), high = std::max(e.first, e.second);
      TopoDS_Edge shared;
      if (!edge(low, high, shared)) return false;
      if (e.first == low) builder.Add(wire, shared);
      else builder.Add(wire, shared.Reversed());
    }
    return true;
  }

  // The boundary cycles of a group of triangles: each directed edge whose opposite is not in the group,
  // walked into closed cycles. False when a walk cannot close (an open or branching boundary).
  bool cycles(const std::vector<std::uint32_t>& group, std::vector<std::vector<Edge>>& out) const {
    std::vector<Edge> directed;
    directed.reserve(group.size() * 3);
    for (const std::uint32_t t : group) {
      const std::size_t at = std::size_t(t) * 3;
      const std::uint32_t a = mesh_.triangles[at], b = mesh_.triangles[at + 1], c = mesh_.triangles[at + 2];
      directed.push_back({a, b});
      directed.push_back({b, c});
      directed.push_back({c, a});
    }
    std::sort(directed.begin(), directed.end());
    std::vector<Edge> boundary;
    for (const Edge& e : directed)
      if (!std::binary_search(directed.begin(), directed.end(), Edge{e.second, e.first}) && (boundary.empty() || boundary.back() != e))
        boundary.push_back(e);
    std::unordered_map<std::uint32_t, std::vector<std::size_t>> from;
    for (std::size_t i = 0; i < boundary.size(); ++i) from[boundary[i].first].push_back(i);
    std::vector<char> used(boundary.size(), 0);
    for (std::size_t s = 0; s < boundary.size(); ++s) {
      if (used[s]) continue;
      used[s] = 1;
      std::vector<Edge> cycle{boundary[s]};
      const std::uint32_t start = boundary[s].first;
      std::uint32_t at = boundary[s].second;
      while (at != start) {
        std::size_t next = boundary.size();
        for (const std::size_t k : from[at])
          if (!used[k]) {
            next = k;
            break;
          }
        if (next == boundary.size()) return false;
        used[next] = 1;
        cycle.push_back(boundary[next]);
        at = boundary[next].second;
      }
      out.push_back(std::move(cycle));
    }
    return !out.empty();
  }

  // Newell's normal of a cycle: twice its area, along the cycle's own winding.
  gp_Vec newell(const std::vector<Edge>& cycle) const {
    gp_Vec sum(0, 0, 0);
    for (const Edge& e : cycle) sum += gp_Vec(point(e.first).XYZ()).Crossed(gp_Vec(point(e.second).XYZ()));
    return sum;
  }

  // One planar face of a group: its largest cycle is the outer loop, the others are holes. False when the group
  // has no area or the face cannot be made.
  bool groupFace(const std::vector<std::uint32_t>& group, TopoDS_Face& face) {
    std::vector<std::vector<Edge>> loops;
    if (!cycles(group, loops)) return false;
    std::vector<gp_Vec> normals;
    std::size_t outer = 0;
    for (std::size_t k = 0; k < loops.size(); ++k) {
      normals.push_back(newell(loops[k]));
      if (normals[k].SquareMagnitude() > normals[outer].SquareMagnitude()) outer = k;
    }
    const gp_Vec normal = normals[outer];
    if (normal.SquareMagnitude() < 1e-24) return false;
    TopoDS_Wire outerWire;
    if (!wireOf(loops[outer], outerWire)) return false;
    BRepBuilderAPI_MakeFace maker(gp_Pln(point(loops[outer].front().first), gp_Dir(normal)), outerWire, Standard_True);
    for (std::size_t k = 0; k < loops.size(); ++k) {
      if (k == outer) continue;
      TopoDS_Wire hole;
      if (!wireOf(loops[k], hole)) return false;
      maker.Add(hole);
    }
    if (!maker.IsDone()) return false;
    face = maker.Face();
    return true;
  }

  const StepExportMesh& mesh_;
  gp_Trsf placement_;
  std::vector<TopoDS_Vertex> vertices_;
  std::unordered_map<std::uint64_t, TopoDS_Edge> edges_;
};

// ---------------------------------------------------------------------------------------------------
// The package -> the XDE document.

struct Counts {
  std::uint64_t parts{}, assemblies{}, instances{}, faces{}, solids{}, shells{}, fallbacks{}, degenerate{}, hidden{}, colouredParts{};
  std::map<std::string, bool> layers;
};

class Document {
public:
  explicit Document(const StepExportPackage& package) : package_(package), doc_(new TDocStd_Document("XCAF")) {
    const TDF_Label shapes = XCAFDoc_DocumentTool::ShapesLabel(doc_->Main());
    shapeTool_ = XCAFDoc_ShapeTool::Set(shapes);
    colorTool_ = XCAFDoc_ColorTool::Set(shapes);
    layerTool_ = XCAFDoc_LayerTool::Set(XCAFDoc_DocumentTool::LayersLabel(doc_->Main()));
  }

  // Builds the tree. A group under the top level is folded: its placement moves into its children, and its own
  // label stays at the identity, so the top of the file is the world.
  void build() {
    const std::vector<StepExportNode>& nodes = package_.nodes;
    std::vector<TDF_Label> assemblyOf(nodes.size());
    std::map<std::tuple<long long, std::string, std::string, bool>, TDF_Label> parts;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
      const StepExportNode& node = nodes[i];
      const bool top = node.parent < 0;
      const bool folded = !top && package_.nodes[std::size_t(node.parent)].parent < 0;
      gp_Trsf location = placementOf(node.placement);
      if (folded) location = placementOf(package_.nodes[std::size_t(node.parent)].placement) * location;
      if (node.assembly) {
        const TDF_Label label = shapeTool_->NewShape();
        setName(label, node.name);
        if (node.hidden) setHidden(label);
        assemblyOf[i] = label;
        ++counts_.assemblies;
        if (!top) addInstance(assemblyOf[std::size_t(node.parent)], label, location);
      } else if (top) {
        // A top-level element is its own part, placed by its shape's location.
        makePart(node, &location);
      } else {
        const auto key = std::make_tuple(node.mesh, node.name, node.layer, node.hidden);
        auto found = parts.find(key);
        if (found == parts.end()) found = parts.emplace(key, makePart(node, nullptr)).first;
        addInstance(assemblyOf[std::size_t(node.parent)], found->second, location);
      }
    }
    shapeTool_->UpdateAssemblies();
  }

  Handle(TDocStd_Document) document() const { return doc_; }
  const Counts& counts() const { return counts_; }

private:
  void setName(const TDF_Label& label, const std::string& name) {
    if (!name.empty()) TDataStd_Name::Set(label, extended(name));
  }

  void setHidden(const TDF_Label& label) {
    colorTool_->SetVisibility(label, Standard_False);
    ++counts_.hidden;
  }

  void addInstance(const TDF_Label& assembly, const TDF_Label& child, const gp_Trsf& location) {
    shapeTool_->AddComponent(assembly, child, TopLoc_Location(location));
    ++counts_.instances;
  }

  // A part for an element: the mesh's shape (moved to `placed` for a top-level element), its name, colours, layer and
  // visibility. An element with no area is refused, since its component would have nothing to reference.
  TDF_Label makePart(const StepExportNode& node, const gp_Trsf* placed) {
    const StepExportMesh& mesh = package_.meshes[std::size_t(node.mesh)];
    Shaper shaper(mesh, placed != nullptr ? *placed : gp_Trsf());
    std::vector<Shaper::Colored> faces;
    const TopoDS_Shape shape = shaper.build(faces);
    counts_.fallbacks += shaper.fallbacks;
    counts_.degenerate += shaper.degenerate;
    if (shape.IsNull()) throw Refusal("meshInvalid", "An element has no triangle with an area; nothing was written.");
    const TDF_Label part = shapeTool_->AddShape(shape, Standard_False, Standard_False);
    counts_.solids += shape.ShapeType() == TopAbs_SOLID ? 1 : 0;
    counts_.shells += shape.ShapeType() == TopAbs_SOLID ? 0 : 1;
    counts_.faces += faces.size();
    ++counts_.parts;
    setName(part, node.name);
    colour(part, mesh, faces);
    if (!node.layer.empty()) {
      const TCollection_ExtendedString name = extended(node.layer);
      TDF_Label layer;
      if (!layerTool_->FindLayer(name, layer)) layer = layerTool_->AddLayer(name);
      layerTool_->SetLayer(part, layer);
      counts_.layers[node.layer] = true;
    }
    if (node.hidden) setHidden(part);
    return part;
  }

  // One colour on the part when all its faces share it; otherwise each face gets its own (a sub-shape colour).
  void colour(const TDF_Label& part, const StepExportMesh& mesh, const std::vector<Shaper::Colored>& faces) {
    const std::uint32_t first = mesh.faceColor.front();
    const bool uniform = std::all_of(mesh.faceColor.begin(), mesh.faceColor.end(), [first](std::uint32_t c) { return c == first; });
    if (uniform) {
      colorTool_->SetColor(part, colourOf(mesh.colors[first]), XCAFDoc_ColorGen);
      return;
    }
    ++counts_.colouredParts;
    for (const Shaper::Colored& f : faces) {
      const TDF_Label sub = shapeTool_->AddSubShape(part, f.face);
      colorTool_->SetColor(sub, colourOf(mesh.colors[f.colour]), XCAFDoc_ColorSurf);
    }
  }

  const StepExportPackage& package_;
  Handle(TDocStd_Document) doc_;
  Handle(XCAFDoc_ShapeTool) shapeTool_;
  Handle(XCAFDoc_ColorTool) colorTool_;
  Handle(XCAFDoc_LayerTool) layerTool_;
  Counts counts_;
};

std::string jsonEscaped(std::string text) {
  if (text.size() > 240) text.resize(240);
  std::string out;
  for (const char c : text) {
    if (c == '"' || c == '\\') out += '\\';
    out += static_cast<unsigned char>(c) < 0x20 ? ' ' : c;
  }
  return out;
}

}  // namespace

int runExportStep(const StepExportOptions& options) {
  Progress progress(true);
  try {
    progress.phase("read");
    const std::string bytes = readFile(options.input, "package");
    StepExportPackage package;
    try {
      package = readStepExportPackage(bytes);
    } catch (const std::runtime_error& problem) {
      throw Refusal("packageInvalid", problem.what());
    }
    // AP214 DIS by default, AP203 as the option. AP242 writing is not supported (ADR 0172 section 2).
    std::string schema = options.schema.empty() ? "ap214" : options.schema;
    if (schema == "ap242") throw Refusal("schema", "AP242 writing is not supported; the export writes AP214 (the default) or AP203.");
    if (schema != "ap214" && schema != "ap203") throw Refusal("schema", "The schema is ap214 (the default) or ap203.");
    if (options.output.empty()) throw Refusal("outputMissing", "The export needs an output file.");
    const bool anyElement = std::any_of(package.nodes.begin(), package.nodes.end(), [](const StepExportNode& n) { return !n.assembly; });
    if (!anyElement) throw Refusal("emptyModel", "The model has no body to export; nothing was written.");

    progress.phase("build");
    Document document(package);
    document.build();

    progress.phase("write");
    Handle(XSControl_WorkSession) session(new XSControl_WorkSession());
    STEPCAFControl_Writer writer(session, Standard_False);
    writer.SetNameMode(Standard_True);
    writer.SetColorMode(Standard_True);
    writer.SetLayerMode(Standard_True);
    if (!session->SelectNorm("STEP")) throw Refusal("writer", "The STEP norm is not available.");
    DESTEP_Parameters parameters;
    parameters.WriteSchema = schema == "ap203" ? DESTEP_Parameters::WriteMode_StepSchema_AP203
                                               : DESTEP_Parameters::WriteMode_StepSchema_AP214DIS;
    parameters.WriteUnit = UnitsMethods_LengthUnit_Millimeter;  // the document's unit: the package is never rescaled
    if (!writer.Transfer(document.document(), parameters, STEPControl_AsIs))
      throw Refusal("writer", "The XDE writer could not transfer the model.");
    if (writer.Write(options.output.c_str()) != IFSelect_RetDone) throw Refusal("writer", "The XDE writer could not write the file.");

    progress.phase("check");
    const std::string text = readFile(options.output, "written file");
    if (text.compare(0, 13, "ISO-10303-21;") != 0 || text.size() < 16 || text.find("END-ISO-10303-21;") == std::string::npos)
      throw Refusal("writer", "The written file is not a complete STEP file.");
    const std::string wanted = schema == "ap203" ? "CONFIG_CONTROL_DESIGN" : "AUTOMOTIVE_DESIGN";
    if (text.find(wanted) == std::string::npos) throw Refusal("writer", "The written file does not declare the requested schema.");
    if (text.find(".MILLI.") == std::string::npos) throw Refusal("writer", "The written file does not declare millimetres.");

    const Counts& counts = document.counts();
    std::ostringstream json;
    json << R"({"status":"ok","schema":")" << schema << R"(","unit":"mm","bytes":)" << text.size() << R"(,"parts":)" << counts.parts
         << R"(,"assemblies":)" << counts.assemblies << R"(,"instances":)" << counts.instances << R"(,"faces":)" << counts.faces
         << R"(,"solids":)" << counts.solids << R"(,"shells":)" << counts.shells << R"(,"fallbacks":)" << counts.fallbacks
         << R"(,"degenerate":)" << counts.degenerate << R"(,"hidden":)" << counts.hidden << R"(,"layers":)" << counts.layers.size()
         << R"(,"colouredParts":)" << counts.colouredParts << "}";
    std::printf("R %s\n", json.str().c_str());
    std::fflush(stdout);
    return 0;
  } catch (const Refusal& refusal) {
    std::ostringstream json;
    json << R"({"status":"refused","code":")" << jsonEscaped(refusal.code) << R"(","message":")" << jsonEscaped(refusal.what()) << R"("})";
    std::printf("R %s\n", json.str().c_str());
    std::fflush(stdout);
    return 3;
  } catch (const Standard_Failure& failure) {
    std::ostringstream json;
    json << R"({"status":"refused","code":"writer","message":")" << jsonEscaped(failure.GetMessageString()) << R"("})";
    std::printf("R %s\n", json.str().c_str());
    std::fflush(stdout);
    return 3;
  }
}

}  // namespace sketchyup::ifc
