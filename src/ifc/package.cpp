#include "ifc/package.hpp"

#include <cmath>
#include <cstring>

#include "ifc/sha256.hpp"

namespace sketchyup::ifc {
namespace {

void putU32(std::string& out, std::uint32_t v) {
  const char bytes[4] = {static_cast<char>(v & 0xFF), static_cast<char>((v >> 8) & 0xFF),
                         static_cast<char>((v >> 16) & 0xFF), static_cast<char>((v >> 24) & 0xFF)};
  out.append(bytes, 4);
}
void putU64(std::string& out, std::uint64_t v) {
  for (int i = 0; i < 8; ++i) out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}
void putF64(std::string& out, double v) {
  std::uint64_t bits;
  std::memcpy(&bits, &v, 8);
  putU64(out, bits);
}
void putF32(std::string& out, float v) {
  std::uint32_t bits;
  std::memcpy(&bits, &v, 4);
  putU32(out, bits);
}

void writeValue(JsonOut& json, const PackageValue& value) {
  if (const auto* flag = std::get_if<bool>(&value)) {
    json.value("b").value(*flag);
  } else if (const auto* number = std::get_if<double>(&value)) {
    json.value("d").value(*number);
  } else {
    json.value("s").value(std::get<std::string>(value));
  }
}

}  // namespace

std::string serializePackage(const PackageData& data, std::string& problem) {
  std::string mesh;
  JsonOut meshes;
  meshes.beginArray();
  for (const auto& m : data.meshes) {
    const bool colors = !m.colors.empty();
    for (std::size_t i = 0; i < m.vertices.size(); ++i) putF64(mesh, m.vertices[i]);
    for (const auto index : m.triangles) putU32(mesh, index);
    for (const auto material : m.materials) putU32(mesh, material);
    for (const auto index : m.edges) putU32(mesh, index);
    for (const auto& color : m.colors)
      for (std::size_t k = 0; k < 3; ++k) putF32(mesh, color ? (*color)[k] : std::nanf(""));
    meshes.beginObject()
        .field("vertices", m.vertices.size() / 3)
        .field("triangles", m.triangles.size() / 3)
        .field("edges", m.edges.size() / 2)
        .field("colors", colors)
        .endObject();
  }
  meshes.endArray();
  if (mesh.size() > 128u * 1024 * 1024) {
    problem = "The meshes exceed the 128 MiB package bound.";
    return {};
  }
  JsonOut materials;
  materials.beginArray();
  for (const auto& m : data.materials)
    materials.beginObject()
        .field("name", m.name)
        .key("color")
        .doubles({static_cast<double>(m.color[0]), static_cast<double>(m.color[1]), static_cast<double>(m.color[2])})
        .field("opacity", static_cast<double>(m.opacity))
        .endObject();
  materials.endArray();
  JsonOut nodes;
  nodes.beginArray();
  for (const auto& n : data.nodes) {
    nodes.beginObject();
    nodes.field("step", static_cast<long long>(n.step));
    nodes.field("class", n.className);
    nodes.field("guid", n.guid);
    nodes.field("name", n.name);
    nodes.field("parent", n.parent);
    nodes.field("role", n.role);
    nodes.field("hidden", n.hidden);
    nodes.key("transform").doubles(std::vector<double>(n.transform.begin(), n.transform.end()));
    nodes.field("mesh", n.mesh);
    nodes.key("ifc").beginArray();
    for (const auto& [key, value] : n.provenance) {
      nodes.beginArray().value(key);
      writeValue(nodes, value);
      nodes.endArray();
    }
    nodes.endArray();
    nodes.key("props").beginArray();
    for (const auto& p : n.properties) {
      nodes.beginArray().value(p.set).value(p.name).value(p.origin);
      writeValue(nodes, p.value);
      nodes.endArray();
    }
    nodes.endArray();
    nodes.endObject();
  }
  nodes.endArray();
  // The decoder reads (type, value) after the key; the writer's helper wrote [key, type, value] for
  // provenance and [set, name, origin, type, value] for properties, which is the documented form.
  std::string manifest = "{";
  const auto member = [&](const char* key, const std::string& json) {
    if (manifest.size() > 1) manifest += ',';
    manifest += '"';
    manifest += key;
    manifest += "\":";
    manifest += json;
  };
  member("format", "\"sketchyup-ifc-package\"");
  member("version", "1");
  member("meshSha256", "\"" + sha256Hex(mesh) + "\"");
  member("engine", data.engineJson);
  member("source", data.sourceJson);
  member("units", data.unitsJson);
  member("report", data.reportJson);
  member("meshes", meshes.str());
  member("materials", materials.str());
  member("nodes", nodes.str());
  manifest += '}';
  if (manifest.size() > 32u * 1024 * 1024) {
    problem = "The manifest exceeds the 32 MiB package bound.";
    return {};
  }
  std::string out("SKIFCP01", 8);
  putU32(out, static_cast<std::uint32_t>(manifest.size()));
  putU32(out, 0);
  putU64(out, mesh.size());
  out += manifest;
  out += mesh;
  return out;
}

}  // namespace sketchyup::ifc
