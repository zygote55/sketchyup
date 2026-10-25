#include "package.hpp"

#include "json_out.hpp"
#include "sha256.hpp"
#include "report.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace sketchyup::skp {
namespace {

using Bytes = std::vector<std::uint8_t>;

void putU32(Bytes& out, std::uint32_t value) {
  out.push_back(value & 0xFF);
  out.push_back((value >> 8) & 0xFF);
  out.push_back((value >> 16) & 0xFF);
  out.push_back((value >> 24) & 0xFF);
}

void putU64(Bytes& out, std::uint64_t value) {
  for (int i = 0; i <= 7; ++i) out.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xFF));
}

void putF64(Bytes& out, double value) {
  std::uint64_t bits = 0;
  static_assert(sizeof bits == sizeof value);
  std::memcpy(&bits, &value, sizeof bits);
  putU64(out, bits);
}

template <std::size_t N>
bool finiteAll(const std::array<double, N>& numbers, const char* what, std::string& problem) {
  for (const double number : numbers)
    if (!std::isfinite(number) || std::abs(number) > 1e6) {
      problem = std::string(what) + " is not finite or beyond 1e6 m";
      return false;
    }
  return true;
}

}  // namespace

std::string countsJson(const SkpPackage& package) {
  JsonOut json;
  json.beginObject()
      .field("definitions", package.counts.definitions)
      .field("vertices", package.counts.vertices)
      .field("edges", package.counts.edges)
      .field("faces", package.counts.faces)
      .field("holes", package.counts.holes)
      .field("instances", package.counts.instances)
      .field("materials", package.counts.materials)
      .field("tags", package.counts.tags)
      .field("uvTransforms", package.counts.uvTransforms)
      .field("textures", package.counts.textures)
      .endObject();
  return json.str();
}

std::string reportJson(const SkpPackage& package) {
  JsonOut json;
  json.beginObject().field("apiVersion", 1).key("counts").raw(countsJson(package)).key("notices");
  json.beginArray();
  for (const auto& [code, count] : package.notices)
    json.beginObject().field("code", code).field("count", count).endObject();
  json.endArray().key("locations");
  json.beginArray();
  for (const auto& place : package.locations)
    json.beginObject()
        .field("kind", place.kind)
        .field("definition", place.definition)
        .field("message", place.message)
        .endObject();
  json.endArray().key("locationTotals");
  json.beginObject();
  for (const auto& [kind, total] : package.locationTotals) json.field(kind, total);
  json.endObject().key("omitted");
  json.beginObject();
  for (const auto& [code, count] : package.omitted) json.field(code, count);
  json.endObject().key("source");
  json.beginObject();
  for (const auto& [kind, total] : package.sourceTotals) json.field(kind, total);
  json.endObject().endObject();
  return json.str();
}

std::string serializeSkpPackage(const SkpPackage& package, std::string& problem) {
  // The limits, in order (the decoder checks the same).
  if (package.definitions.size() > SkpPackageLimits::definitions)
    return (problem = "The file has " + std::to_string(package.definitions.size()) +
                    " definitions; the limit is " + std::to_string(SkpPackageLimits::definitions) +
                    "."),
           std::string();
  if (static_cast<std::uint64_t>(package.counts.vertices) > SkpPackageLimits::vertices)
    return (problem = "The file has " + std::to_string(package.counts.vertices) +
                    " vertices; the limit is " + std::to_string(SkpPackageLimits::vertices) +
                    "."),
           std::string();
  if (static_cast<std::uint64_t>(package.counts.edges) > SkpPackageLimits::edges)
    return (problem = "The file has " + std::to_string(package.counts.edges) +
                    " edges; the limit is " + std::to_string(SkpPackageLimits::edges) +
                    "."),
           std::string();
  if (static_cast<std::uint64_t>(package.counts.faces) > SkpPackageLimits::faces)
    return (problem = "The file has " + std::to_string(package.counts.faces) +
                    " faces; the limit is " + std::to_string(SkpPackageLimits::faces) +
                    "."),
           std::string();
  if (static_cast<std::uint64_t>(package.counts.instances) > SkpPackageLimits::instances)
    return (problem = "The file has " + std::to_string(package.counts.instances) +
                    " component or group placements; the limit is " +
                    std::to_string(SkpPackageLimits::instances) +
                    "."),
           std::string();
  if (package.materials.size() > SkpPackageLimits::materials)
    return (problem = "The file has " + std::to_string(package.materials.size()) +
                    " materials; the limit is " + std::to_string(SkpPackageLimits::materials) +
                    "."),
           std::string();
  if (package.tags.size() > SkpPackageLimits::tags)
    return (problem = "The file has " + std::to_string(package.tags.size()) +
                    " tags; the limit is " + std::to_string(SkpPackageLimits::tags) +
                    "."),
           std::string();
  if (package.textures.size() > SkpPackageLimits::textures)
    return (problem = "The file has " + std::to_string(package.textures.size()) +
                    " textures; the limit is " + std::to_string(SkpPackageLimits::textures) +
                    "."),
           std::string();
  for (const auto& texture : package.textures) {
    if (texture.bytes.size() > SkpPackageLimits::perTextureBytes)
      return (problem = "A texture is " + std::to_string(texture.bytes.size()) +
                      " bytes; the limit is " + std::to_string(SkpPackageLimits::perTextureBytes) +
                      "."),
             std::string();
  }
  for (const auto& definition : package.definitions) {
    for (const auto& vertex : definition.vertices) {
      for (const double number : vertex)
        if (!std::isfinite(number) || std::abs(number) > 1e6)
          return (problem = "A coordinate is not finite or beyond 1e6 m"), std::string();
    }
    for (const auto& face : definition.faces) {
      if (face.loops.empty()) return (problem = "A face has no outer loop."), std::string();
      if (face.loops.size() > SkpPackageLimits::loopsPerFace)
        return (problem = "A face has " + std::to_string(face.loops.size()) +
                        " loops; the limit is " + std::to_string(SkpPackageLimits::loopsPerFace) +
                        "."),
               std::string();
      for (const auto& loop : face.loops) {
        if (loop.size() < 3)
          return (problem = "A face loop has " + std::to_string(loop.size()) +
                        " vertices; it needs at least 3."),
                 std::string();
        if (loop.size() > SkpPackageLimits::loopVertices)
          return (problem = "A face loop has " + std::to_string(loop.size()) +
                      " vertices; the limit is " + std::to_string(SkpPackageLimits::loopVertices) +
                      "."),
                 std::string();
        for (const std::uint32_t index : loop)
          if (index >= definition.vertices.size())
            return (problem = "A face references vertex " + std::to_string(index) +
                        " of a definition with " + std::to_string(definition.vertices.size()) +
                        " vertices."),
                   std::string();
      }
    }
    for (const auto& edge : definition.edges) {
      if (edge.v1 >= definition.vertices.size() || edge.v2 >= definition.vertices.size())
        return (problem = "An edge references a vertex the definition does not have."),
               std::string();
    }
    for (const auto& instance : definition.instances)
      if (instance.definition < 0 ||
          static_cast<std::size_t>(instance.definition) >= package.definitions.size())
        return (problem = "An instance references a definition the file does not have."),
               std::string();
    for (const auto& face : definition.faces) {
      for (const long long material : {face.materialFront, face.materialBack})
        if (material != -1 && static_cast<std::size_t>(material) >= package.materials.size())
          return (problem = "A face references a material the file does not have."),
                 std::string();
      if (face.tag != -1 && static_cast<std::size_t>(face.tag) >= package.tags.size())
        return (problem = "A face references a tag the file does not have."),
               std::string();
    }
  }
  for (const auto& material : package.materials)
    if (material.texture != -1 && static_cast<std::size_t>(material.texture) >= package.textures.size())
      return (problem = "A material references a texture the file does not have."), std::string();

  // The texture chunk: the image bytes, in the manifest's order.
  Bytes textureChunk;
  for (const auto& texture : package.textures)
    textureChunk.insert(textureChunk.end(), texture.bytes.begin(), texture.bytes.end());

  // The geometry chunk: the definitions of the manifest back to back, in order, self-describing
  // (the decoder checks every count against the manifest, the limits and the chunk length).
  Bytes geometry;
  for (const auto& definition : package.definitions) {
    putU32(geometry, static_cast<std::uint32_t>(definition.vertices.size()));
    for (const auto& vertex : definition.vertices) {
      putF64(geometry, vertex[0]);
      putF64(geometry, vertex[1]);
      putF64(geometry, vertex[2]);
    }
    putU32(geometry, static_cast<std::uint32_t>(definition.edges.size()));
    for (const auto& edge : definition.edges) {
      putU32(geometry, edge.v1);
      putU32(geometry, edge.v2);
      std::uint8_t flags{};
      if (edge.soft) flags |= 0x01;
      if (edge.smooth) flags |= 0x02;
      if (edge.hidden) flags |= 0x04;
      if (edge.wire) flags |= 0x08;
      geometry.push_back(flags);
    }
    putU32(geometry, static_cast<std::uint32_t>(definition.faces.size()));
    for (const auto& face : definition.faces) {
      std::uint8_t flags{};
      if (face.hidden) flags |= 0x01;
      if (face.hasUvFront) flags |= 0x02;
      if (face.hasUvBack) flags |= 0x04;
      if (face.uvFrontProjected) flags |= 0x08;
      if (face.uvBackProjected) flags |= 0x10;
      geometry.push_back(flags);
      putU32(geometry, face.materialFront == -1 ? 0xFFFFFFFFu
                                               : static_cast<std::uint32_t>(face.materialFront));
      putU32(geometry, face.materialBack == -1 ? 0xFFFFFFFFu
                                               : static_cast<std::uint32_t>(face.materialBack));
      putU32(geometry, face.tag == -1 ? 0xFFFFFFFFu : static_cast<std::uint32_t>(face.tag));
      putU32(geometry, static_cast<std::uint32_t>(face.loops.size()));
      for (const auto& loop : face.loops) {
        putU32(geometry, static_cast<std::uint32_t>(loop.size()));
        for (const std::uint32_t index : loop) putU32(geometry, index);
      }
      if (face.hasUvFront)
        if (!finiteAll(face.uvFront, "a UV transform", problem)) return std::string();
      if (face.hasUvBack)
        if (!finiteAll(face.uvBack, "a UV transform", problem)) return std::string();
      if (face.hasUvFront)
        for (const double number : face.uvFront) putF64(geometry, number);
      if (face.hasUvBack)
        for (const double number : face.uvBack) putF64(geometry, number);
    }
    putU32(geometry, static_cast<std::uint32_t>(definition.instances.size()));
    for (const auto& instance : definition.instances) {
      putU32(geometry, instance.definition == -1 ? 0xFFFFFFFFu
                                                : static_cast<std::uint32_t>(instance.definition));
      std::uint8_t flags{};
      if (instance.hidden) flags |= 0x01;
      geometry.push_back(flags);
      putU32(geometry, instance.tag == -1 ? 0xFFFFFFFFu : static_cast<std::uint32_t>(instance.tag));
      putU32(geometry, instance.material == -1
                          ? 0xFFFFFFFFu
                          : static_cast<std::uint32_t>(instance.material));
      if (!finiteAll(instance.transform, "an instance transform", problem)) return std::string();
      for (const double number : instance.transform) putF64(geometry, number);
    }
  }

  // The manifest.
  JsonOut json;
  json.beginObject()
      .field("format", "sketchyup-skp-package")
      .field("version", 1)
      .field("geometrySha256", sha256_hex(geometry.data(), geometry.size()))
      .field("textureSha256", sha256_hex(textureChunk.data(), textureChunk.size()))
      .key("source")
      .beginObject()
      .field("name", clip(package.sourceName, SkpPackageLimits::nameBytes))
      .field("size", package.sourceSize)
      .field("sha256", package.sourceSha256)
      .endObject()
      .field("formatVersion", package.formatVersion)
      .field("container", package.container);
  if (package.units.empty()) json.key("units").null();
  else json.field("units", package.units);
  json.field("unitScale", package.unitScale).key("report");
  json.raw(reportJson(package));
  json.key("definitions");
  json.beginArray();
  for (const auto& definition : package.definitions)
    json.beginObject()
        .field("name", clip(definition.name, SkpPackageLimits::nameBytes))
        .field("guid", clip(definition.guid, SkpPackageLimits::stringBytes))
        .field("vertices", definition.vertices.size())
        .field("edges", definition.edges.size())
        .field("faces", definition.faces.size())
        .field("instances", definition.instances.size())
        .endObject();
  json.endArray().key("tags");
  json.beginArray();
  for (const auto& tag : package.tags)
    json.beginObject()
        .field("name", clip(tag.name, SkpPackageLimits::nameBytes))
        .key("color")
        .beginArray()
        .value(static_cast<long long>(tag.color[0]))
        .value(static_cast<long long>(tag.color[1]))
        .value(static_cast<long long>(tag.color[2]))
        .endArray()
        .field("hidden", tag.hidden)
        .endObject();
  json.endArray().key("materials");
  json.beginArray();
  for (const auto& material : package.materials)
    json.beginObject()
        .field("name", clip(material.name, SkpPackageLimits::nameBytes))
        .key("color")
        .beginArray()
        .value(static_cast<long long>(material.color[0]))
        .value(static_cast<long long>(material.color[1]))
        .value(static_cast<long long>(material.color[2]))
        .value(static_cast<long long>(material.color[3]))
        .endArray()
        .field("opacity", material.opacity)
        .field("texture", material.texture)
        .endObject();
  json.endArray().key("textures");
  json.beginArray();
  for (const auto& texture : package.textures)
    json.beginObject()
        .field("filename", clip(texture.filename, SkpPackageLimits::nameBytes))
        .field("width", texture.width)
        .field("height", texture.height)
        .field("bytes", texture.bytes.size())
        .endObject();
  json.endArray();
  json.key("counts").raw(countsJson(package));
  json.endObject();
  if (json.str().size() > SkpPackageLimits::manifestBytes)
    return (problem = "The manifest is " + std::to_string(json.str().size()) +
                        " bytes; the limit is " + std::to_string(SkpPackageLimits::manifestBytes) +
                        " (names and GUIDs are clipped, so this should not happen)."),
           std::string();

  // The manifest.
  Bytes file;
  file.insert(file.end(), 'S');
  file.insert(file.end(), 'K');
  file.insert(file.end(), 'S');
  file.insert(file.end(), 'K');
  file.insert(file.end(), 'P');
  file.insert(file.end(), 'P');
  file.insert(file.end(), '0');
  file.insert(file.end(), '1');
  putU32(file, static_cast<std::uint32_t>(json.str().size()));
  putU32(file, 0);
  putU64(file, geometry.size());
  putU64(file, textureChunk.size());
  file.insert(file.end(), json.str().begin(), json.str().end());
  file.insert(file.end(), geometry.begin(), geometry.end());
  file.insert(file.end(), textureChunk.begin(), textureChunk.end());
  // The total cap: the worker runs with a 512 MiB RLIMIT_FSIZE, and a write past it would
  // kill the process instead of refusing. 512 MiB keeps every package the FSIZE allows.
  if (file.size() > 512 * 1024 * 1024)
    return (problem = "The package exceeds its 512 MiB total limit."), std::string();
  return std::string(file.begin(), file.end());
}

}  // namespace sketchyup::skp
