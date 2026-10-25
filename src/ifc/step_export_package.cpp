#include "step_export_package.hpp"

#include <cmath>
#include <cstring>
#include <stdexcept>

#include "json_in.hpp"
#include "sha256.hpp"

namespace sketchyup::ifc {
namespace {

[[noreturn]] void bad(const std::string& problem) { throw std::runtime_error("STEP export package: " + problem); }

std::uint32_t u32(const unsigned char* p) {
  return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) | (static_cast<std::uint32_t>(p[2]) << 16) |
         (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint64_t u64(const unsigned char* p) { return static_cast<std::uint64_t>(u32(p)) | (static_cast<std::uint64_t>(u32(p + 4)) << 32); }

double f64(const unsigned char* p) {
  const std::uint64_t bits = u64(p);
  double value;
  std::memcpy(&value, &bits, sizeof value);
  return value;
}

float f32(const unsigned char* p) {
  const std::uint32_t bits = u32(p);
  float value;
  std::memcpy(&value, &bits, sizeof value);
  return value;
}

// A text value of at most `limit` bytes with no control character (a tab is allowed).
std::string boundedText(const JsonValue& value, std::size_t limit, const char* what) {
  const std::string& text = value.asString();
  if (text.size() > limit) bad(std::string(what) + " is longer than " + std::to_string(limit) + " bytes");
  for (const char c : text)
    if (static_cast<unsigned char>(c) < 0x20 && c != '\t') bad(std::string(what) + " has a control character");
  return text;
}

double finite(const JsonValue& value, const char* what) {
  const double number = value.asNumber();
  if (!std::isfinite(number)) bad(std::string(what) + " is not finite");
  return number;
}

}  // namespace

StepExportPackage readStepExportPackage(std::string_view bytes) {
  const auto* data = reinterpret_cast<const unsigned char*>(bytes.data());
  if (bytes.size() < StepExportLimits::headerBytes || std::memcmp(data, "SKSTPX01", 8) != 0) bad("not an export package");
  const std::uint64_t manifestBytes = u32(data + 8);
  if (u32(data + 12) != 0) bad("reserved word is not zero");
  const std::uint64_t meshBytes = u64(data + 16);
  if (manifestBytes > StepExportLimits::manifestBytes || meshBytes > StepExportLimits::meshBytes) bad("a part is over its bound");
  if (bytes.size() != StepExportLimits::headerBytes + manifestBytes + meshBytes) bad("the length does not match the header");
  const std::string_view manifestText = bytes.substr(StepExportLimits::headerBytes, static_cast<std::size_t>(manifestBytes));
  const unsigned char* mesh = data + StepExportLimits::headerBytes + manifestBytes;

  JsonLimits limits;
  limits.depth = 12;
  const JsonValue manifest = JsonReader(manifestText, limits).parse();
  if (!manifest.isObject()) bad("the manifest is not an object");
  if (manifest.at("format").asString() != "sketchyup-step-export-package" || manifest.at("version").asInteger() != 1)
    bad("unknown format or version");
  {
    const std::string chunk(reinterpret_cast<const char*>(mesh), static_cast<std::size_t>(meshBytes));
    if (manifest.at("meshSha256").asString() != sha256Hex(chunk)) bad("the mesh chunk does not match its digest");
  }

  StepExportPackage package;
  package.fileName = boundedText(manifest.at("fileName"), 255, "fileName");

  std::uint64_t cursor = 0;
  const auto take = [&](std::uint64_t count, std::uint64_t width) {
    if (count > meshBytes || width == 0 || count > (meshBytes - cursor) / width) bad("the mesh chunk is too short");
    const unsigned char* at = mesh + cursor;
    cursor += count * width;
    return at;
  };
  std::uint64_t totalTriangles = 0;
  for (const JsonValue& entry : manifest.at("meshes").asArray()) {
    if (package.meshes.size() >= StepExportLimits::meshes) bad("too many meshes");
    const long long vertices = entry.at("vertices").asInteger(), triangles = entry.at("triangles").asInteger(),
                    faces = entry.at("faces").asInteger(), colors = entry.at("colors").asInteger();
    if (vertices < 3 || vertices > static_cast<long long>(StepExportLimits::meshVertices) || triangles < 1 ||
        triangles > static_cast<long long>(StepExportLimits::meshTriangles) || faces < 1 || faces > triangles || colors < 1 ||
        colors > faces)
      bad("a mesh count is out of range");
    totalTriangles += static_cast<std::uint64_t>(triangles);
    if (totalTriangles > StepExportLimits::totalTriangles) bad("the package has too many triangles");
    StepExportMesh m;
    const unsigned char* v = take(static_cast<std::uint64_t>(vertices) * 3, 8);
    m.vertices.resize(static_cast<std::size_t>(vertices) * 3);
    for (std::size_t i = 0; i < m.vertices.size(); ++i) {
      m.vertices[i] = f64(v + 8 * i);
      if (!std::isfinite(m.vertices[i]) || std::abs(m.vertices[i]) > 1e9) bad("a vertex coordinate is not finite or is beyond 1e9 mm");
    }
    const unsigned char* t = take(static_cast<std::uint64_t>(triangles) * 3, 4);
    m.triangles.resize(static_cast<std::size_t>(triangles) * 3);
    for (std::size_t i = 0; i < m.triangles.size(); ++i) {
      m.triangles[i] = u32(t + 4 * i);
      if (m.triangles[i] >= static_cast<std::uint64_t>(vertices)) bad("a triangle index is out of range");
    }
    const unsigned char* faceOf = take(static_cast<std::uint64_t>(triangles), 4);
    m.faceOf.resize(static_cast<std::size_t>(triangles));
    for (std::size_t i = 0; i < m.faceOf.size(); ++i) {
      m.faceOf[i] = u32(faceOf + 4 * i);
      if (m.faceOf[i] >= static_cast<std::uint64_t>(faces)) bad("a face index is out of range");
    }
    const unsigned char* faceColor = take(static_cast<std::uint64_t>(faces), 4);
    m.faceColor.resize(static_cast<std::size_t>(faces));
    for (std::size_t i = 0; i < m.faceColor.size(); ++i) {
      m.faceColor[i] = u32(faceColor + 4 * i);
      if (m.faceColor[i] >= static_cast<std::uint64_t>(colors)) bad("a colour index is out of range");
    }
    const unsigned char* table = take(static_cast<std::uint64_t>(colors) * 3, 4);
    m.colors.resize(static_cast<std::size_t>(colors));
    for (std::size_t i = 0; i < m.colors.size(); ++i)
      for (std::size_t k = 0; k < 3; ++k) {
        m.colors[i][k] = f32(table + 4 * (3 * i + k));
        if (!(m.colors[i][k] >= 0 && m.colors[i][k] <= 1)) bad("a mesh colour is outside 0 to 1");
      }
    m.faces = static_cast<std::size_t>(faces);
    m.closed = entry.at("closed").asBool();
    package.meshes.push_back(std::move(m));
  }
  if (cursor != meshBytes) bad("the mesh chunk holds bytes that no mesh uses");

  for (const JsonValue& entry : manifest.at("nodes").asArray()) {
    if (package.nodes.size() >= StepExportLimits::nodes) bad("too many nodes");
    StepExportNode node;
    const std::string& role = entry.at("role").asString();
    if (role == "assembly") node.assembly = true;
    else if (role == "element") node.assembly = false;
    else bad("an unknown role");
    node.name = boundedText(entry.at("name"), StepExportLimits::nameBytes, "a name");
    node.layer = boundedText(entry.at("layer"), StepExportLimits::layerBytes, "a layer");
    node.parent = entry.at("parent").asInteger();
    const auto index = static_cast<long long>(package.nodes.size());
    if (node.parent < -1 || node.parent >= index) bad("a parent is not an earlier node");
    if (node.parent >= 0 && !package.nodes[static_cast<std::size_t>(node.parent)].assembly) bad("an element has a child");
    node.mesh = entry.at("mesh").asInteger();
    if (node.assembly ? node.mesh != -1 : (node.mesh < 0 || node.mesh >= static_cast<long long>(package.meshes.size())))
      bad("a mesh index is out of range");
    const JsonArray& placement = entry.at("placement").asArray();
    if (placement.size() != 9) bad("a placement is not nine numbers");
    for (std::size_t k = 0; k < 9; ++k) node.placement[k] = finite(placement[k], "a placement number");
    node.hidden = entry.at("hidden").asBool();
    package.nodes.push_back(std::move(node));
  }
  if (package.nodes.empty()) bad("the package has no nodes");
  return package;
}

}  // namespace sketchyup::ifc
