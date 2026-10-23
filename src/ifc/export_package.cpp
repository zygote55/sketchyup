#include "ifc/export_package.hpp"

#include <cmath>
#include <cstring>
#include <set>
#include <stdexcept>

#include "ifc/json_in.hpp"
#include "ifc/sha256.hpp"

namespace sketchyup::ifc {
namespace {

[[noreturn]] void bad(const std::string& message) { throw std::runtime_error("export package: " + message); }

constexpr char kDigits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_$";

std::uint32_t u32(const unsigned char* p) {
  return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) | (static_cast<std::uint32_t>(p[2]) << 16) |
         (static_cast<std::uint32_t>(p[3]) << 24);
}
std::uint64_t u64(const unsigned char* p) { return static_cast<std::uint64_t>(u32(p)) | (static_cast<std::uint64_t>(u32(p + 4)) << 32); }
double f64(const unsigned char* p) {
  const std::uint64_t bits = u64(p);
  double value;
  std::memcpy(&value, &bits, 8);
  return value;
}
float f32(const unsigned char* p) {
  const std::uint32_t bits = u32(p);
  float value;
  std::memcpy(&value, &bits, 4);
  return value;
}

bool validUtf8(const std::string& text) {
  for (std::size_t i = 0; i < text.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (c < 0x80) continue;
    const std::size_t length = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC2 ? 2 : 0;
    if (length == 0 || c >= 0xF5 || i + length > text.size()) return false;
    for (std::size_t k = 1; k < length; ++k)
      if ((static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80) return false;
    i += length - 1;
  }
  return true;
}

const std::string& boundedText(const JsonValue& value, std::size_t limit, const char* what) {
  const std::string& text = value.asString();
  if (text.size() > limit) bad(std::string(what) + " is too long");
  if (!validUtf8(text)) bad(std::string(what) + " is not valid UTF-8");
  for (const char c : text)
    if (static_cast<unsigned char>(c) < 0x20 && c != '\t' && c != '\n') bad(std::string(what) + " has a control character");
  return text;
}

double finite(const JsonValue& value, const char* what) {
  const double number = value.asNumber();
  if (!std::isfinite(number)) bad(std::string(what) + " is not finite");
  return number;
}

std::string optionalText(const JsonValue& object, const char* key, std::size_t limit) {
  const JsonValue* found = object.find(key);
  if (found == nullptr || found->isNull()) return {};
  return boundedText(*found, limit, key);
}

std::vector<std::uint8_t> hexBytes(const std::string& hex) {
  std::vector<std::uint8_t> out;
  const auto nibble = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
  };
  for (std::size_t i = 0; i + 1 < hex.size(); i += 2) out.push_back(static_cast<std::uint8_t>(nibble(hex[i]) * 16 + nibble(hex[i + 1])));
  return out;
}

bool knownRole(const std::string& role) {
  static const std::set<std::string> roles{"project", "site", "building", "storey", "space", "assembly", "element"};
  return roles.count(role) != 0;
}

}  // namespace

bool validGlobalId(std::string_view guid) {
  if (guid.size() != 22) return false;
  for (const char c : guid)
    if (std::strchr(kDigits, c) == nullptr || c == '\0') return false;
  return guid[0] >= '0' && guid[0] <= '3';
}

std::string compressGuid(const std::array<std::uint8_t, 16>& bytes) {
  std::string out;
  // 128 bits, most significant first: a leading group of 2 bits, then 21 groups of 6.
  int bit = 0;
  for (int group = 0; group < 22; ++group) {
    const int width = group == 0 ? 2 : 6;
    unsigned value = 0;
    for (int k = 0; k < width; ++k, ++bit) value = (value << 1) | ((bytes[static_cast<std::size_t>(bit / 8)] >> (7 - bit % 8)) & 1u);
    out.push_back(kDigits[value]);
  }
  return out;
}

std::string derivedGlobalId(const std::string& seed) {
  const auto digest = hexBytes(sha256Hex(seed));
  std::array<std::uint8_t, 16> bytes{};
  for (std::size_t i = 0; i < 16; ++i) bytes[i] = digest[i];
  return compressGuid(bytes);
}

ExportPackage readExportPackage(std::string_view bytes) {
  const auto* data = reinterpret_cast<const unsigned char*>(bytes.data());
  if (bytes.size() < ExportLimits::headerBytes || std::memcmp(data, "SKIFCX01", 8) != 0) bad("not an export package");
  const std::uint64_t manifestBytes = u32(data + 8);
  if (u32(data + 12) != 0) bad("reserved word is not zero");
  const std::uint64_t meshBytes = u64(data + 16);
  if (manifestBytes > ExportLimits::manifestBytes || meshBytes > ExportLimits::meshBytes) bad("a part is over its bound");
  if (bytes.size() != ExportLimits::headerBytes + manifestBytes + meshBytes) bad("the length does not match the header");
  const std::string_view manifestText = bytes.substr(ExportLimits::headerBytes, static_cast<std::size_t>(manifestBytes));
  const unsigned char* mesh = data + ExportLimits::headerBytes + manifestBytes;

  JsonLimits limits;
  limits.depth = 12;
  const JsonValue manifest = JsonReader(manifestText, limits).parse();
  if (!manifest.isObject()) bad("the manifest is not an object");
  if (manifest.at("format").asString() != "sketchyup-ifc-export-package" || manifest.at("version").asInteger() != 1)
    bad("unknown format or version");
  {
    const std::string digest = manifest.at("meshSha256").asString();
    const std::string chunk(reinterpret_cast<const char*>(mesh), static_cast<std::size_t>(meshBytes));
    if (digest != sha256Hex(chunk)) bad("the mesh chunk does not match its digest");
  }

  ExportPackage package;
  package.documentId = boundedText(manifest.at("documentId"), 128, "documentId");
  {
    const JsonValue& application = manifest.at("application");
    package.applicationName = boundedText(application.at("name"), 64, "application name");
    package.applicationVersion = boundedText(application.at("version"), 64, "application version");
  }
  package.timestamp = boundedText(manifest.at("timestamp"), 19, "timestamp");
  {
    const std::string& t = package.timestamp;
    const bool shape = t.size() == 19 && t[4] == '-' && t[7] == '-' && t[10] == 'T' && t[13] == ':' && t[16] == ':';
    for (std::size_t i = 0; shape && i < t.size(); ++i)
      if (i != 4 && i != 7 && i != 10 && i != 13 && i != 16 && (t[i] < '0' || t[i] > '9')) bad("timestamp is not YYYY-MM-DDTHH:MM:SS");
    if (!shape) bad("timestamp is not YYYY-MM-DDTHH:MM:SS");
  }
  package.fileName = boundedText(manifest.at("fileName"), 255, "fileName");
  if (const JsonValue* geo = manifest.find("georeference"); geo != nullptr && !geo->isNull()) {
    package.georeference.write = true;
    package.georeference.crs = boundedText(geo->at("crs"), 64, "crs");
    package.georeference.eastings = finite(geo->at("eastings"), "eastings");
    package.georeference.northings = finite(geo->at("northings"), "northings");
    package.georeference.height = finite(geo->at("height"), "height");
    if (package.georeference.crs.empty()) bad("an empty CRS name");
    for (const double v : {package.georeference.eastings, package.georeference.northings, package.georeference.height})
      if (std::abs(v) > 1e8) bad("a site coordinate is beyond 1e8 m");
  }

  for (const JsonValue& entry : manifest.at("styles").asArray()) {
    if (package.styles.size() >= ExportLimits::styles) bad("too many styles");
    ExportStyle style;
    style.name = boundedText(entry.at("name"), ExportLimits::nameBytes, "style name");
    const JsonArray& color = entry.at("color").asArray();
    if (color.size() != 3) bad("a colour is not three numbers");
    for (std::size_t k = 0; k < 3; ++k) {
      style.color[k] = finite(color[k], "colour");
      if (style.color[k] < 0 || style.color[k] > 1) bad("a colour is outside 0 to 1");
    }
    style.opacity = finite(entry.at("opacity"), "opacity");
    if (style.opacity < 0 || style.opacity > 1) bad("an opacity is outside 0 to 1");
    style.material = entry.at("material").asBool();
    package.styles.push_back(std::move(style));
  }

  std::uint64_t cursor = 0;
  const auto take = [&](std::uint64_t count, std::uint64_t width) {
    if (count > meshBytes || width == 0 || count > (meshBytes - cursor) / width) bad("the mesh chunk is too short");
    const unsigned char* at = mesh + cursor;
    cursor += count * width;
    return at;
  };
  for (const JsonValue& entry : manifest.at("meshes").asArray()) {
    if (package.meshes.size() >= ExportLimits::meshes) bad("too many meshes");
    const long long vertices = entry.at("vertices").asInteger(), triangles = entry.at("triangles").asInteger(),
                    colors = entry.at("colors").asInteger();
    if (vertices < 3 || triangles < 1 || colors < 0 || vertices > 3 * 100'000 || triangles > 1'000'000 || colors > 1'000'000)
      bad("a mesh count is out of range");
    ExportMesh m;
    const unsigned char* v = take(static_cast<std::uint64_t>(vertices) * 3, 8);
    m.vertices.resize(static_cast<std::size_t>(vertices) * 3);
    for (std::size_t i = 0; i < m.vertices.size(); ++i) {
      m.vertices[i] = f64(v + 8 * i);
      if (!std::isfinite(m.vertices[i]) || std::abs(m.vertices[i]) > 1e7) bad("a vertex coordinate is not finite or is beyond 1e7 m");
    }
    const unsigned char* t = take(static_cast<std::uint64_t>(triangles) * 3, 4);
    m.triangles.resize(static_cast<std::size_t>(triangles) * 3);
    for (std::size_t i = 0; i < m.triangles.size(); ++i) {
      m.triangles[i] = u32(t + 4 * i);
      if (m.triangles[i] >= static_cast<std::uint64_t>(vertices)) bad("a triangle index is out of range");
    }
    if (colors > 0) {
      const unsigned char* index = take(static_cast<std::uint64_t>(triangles), 4);
      m.colorIndex.resize(static_cast<std::size_t>(triangles));
      for (std::size_t i = 0; i < m.colorIndex.size(); ++i) {
        m.colorIndex[i] = u32(index + 4 * i);
        if (m.colorIndex[i] >= static_cast<std::uint64_t>(colors)) bad("a colour index is out of range");
      }
      const unsigned char* table = take(static_cast<std::uint64_t>(colors) * 3, 4);
      m.colors.resize(static_cast<std::size_t>(colors));
      for (std::size_t i = 0; i < m.colors.size(); ++i)
        for (std::size_t k = 0; k < 3; ++k) {
          m.colors[i][k] = f32(table + 4 * (3 * i + k));
          if (!(m.colors[i][k] >= 0 && m.colors[i][k] <= 1)) bad("a mesh colour is outside 0 to 1");
        }
    }
    m.closed = entry.at("closed").asBool();
    package.meshes.push_back(std::move(m));
  }
  if (cursor != meshBytes) bad("the mesh chunk is longer than its meshes");

  std::set<std::string> guids;
  std::vector<std::size_t> depth;
  std::size_t projects = 0, properties = 0;
  for (const JsonValue& entry : manifest.at("nodes").asArray()) {
    if (package.nodes.size() >= ExportLimits::nodes) bad("too many nodes");
    const std::size_t index = package.nodes.size();
    ExportNode node;
    node.id = entry.at("id").asInteger();
    if (node.id < 0) bad("a negative body ID");
    node.role = entry.at("role").asString();
    if (!knownRole(node.role)) bad("an unknown role");
    node.requestedClass = optionalText(entry, "class", 128);
    node.guid = entry.at("guid").asString();
    if (!validGlobalId(node.guid)) bad("a GlobalId is not 22 characters of the compressed alphabet");
    if (!guids.insert(node.guid).second) bad("a GlobalId is used twice");
    node.name = boundedText(entry.at("name"), ExportLimits::nameBytes, "name");
    node.longName = optionalText(entry, "longName", ExportLimits::nameBytes);
    node.objectType = optionalText(entry, "objectType", ExportLimits::nameBytes);
    node.predefinedType = optionalText(entry, "predefinedType", 128);
    node.parent = entry.at("parent").asInteger();
    if (node.parent < -1 || node.parent >= static_cast<long long>(index)) bad("a parent is not an earlier node");
    if ((node.role == "project") != (node.parent < 0)) bad("only the project has no parent");
    if (node.role == "project" && ++projects > 1) bad("more than one project");
    if (node.parent >= 0) {
      const std::string& parentRole = package.nodes[static_cast<std::size_t>(node.parent)].role;
      const bool spatialParent = parentRole == "site" || parentRole == "building" || parentRole == "storey" || parentRole == "space";
      bool allowed = false;
      if (node.role == "site") allowed = parentRole == "project" || parentRole == "site";
      else if (node.role == "building") allowed = parentRole == "project" || parentRole == "site" || parentRole == "building";
      else if (node.role == "storey") allowed = parentRole == "building" || parentRole == "storey";
      else if (node.role == "space") allowed = spatialParent;
      else allowed = spatialParent || parentRole == "assembly" || parentRole == "element";
      if (!allowed) bad("a " + node.role + " cannot be under a " + parentRole);
    }
    depth.push_back(node.parent < 0 ? 1 : depth[static_cast<std::size_t>(node.parent)] + 1);
    if (depth.back() > ExportLimits::depth) bad("the tree is too deep");
    const JsonArray& placement = entry.at("placement").asArray();
    if (placement.size() != 9) bad("a placement is not nine numbers");
    for (std::size_t k = 0; k < 9; ++k) {
      node.placement[k] = finite(placement[k], "placement");
      if (k < 3 && std::abs(node.placement[k]) > 1e7) bad("a placement is beyond 1e7 m");
    }
    for (std::size_t k = 0; k < 2; ++k) {
      const double* axis = node.placement.data() + 3 + 3 * k;
      const double length = std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
      if (!(std::abs(length - 1) < 1e-6)) bad("a placement axis is not a unit vector");
    }
    {
      const double* z = node.placement.data() + 3;
      const double* x = node.placement.data() + 6;
      if (std::abs(z[0] * x[0] + z[1] * x[1] + z[2] * x[2]) > 1e-6) bad("a placement's axes are not perpendicular");
    }
    node.mesh = entry.at("mesh").asInteger();
    node.style = entry.at("style").asInteger();
    if (node.mesh < -1 || node.mesh >= static_cast<long long>(package.meshes.size())) bad("a mesh index is out of range");
    if (node.style < -1 || node.style >= static_cast<long long>(package.styles.size())) bad("a style index is out of range");
    if (node.mesh < 0 && node.style >= 0) bad("a style on a node without a mesh");
    node.hidden = entry.at("hidden").asBool();
    if (const JsonValue* elevation = entry.find("elevation"); elevation != nullptr && !elevation->isNull()) {
      node.hasElevation = true;
      node.elevation = finite(*elevation, "elevation");
    }
    const JsonArray& props = entry.at("props").asArray();
    if (props.size() > ExportLimits::nodeProperties) bad("too many properties on a node");
    std::set<std::pair<std::string, std::string>> seen;
    for (const JsonValue& p : props) {
      const JsonArray& row = p.asArray();
      if (row.size() != 4) bad("a property is not [set, name, type, value]");
      ExportProperty property;
      property.set = boundedText(row[0], 255, "property set");
      property.name = boundedText(row[1], 255, "property name");
      if (property.set.empty() || property.name.empty()) bad("a property set or name is empty");
      if (!seen.insert({property.set, property.name}).second) bad("a property is repeated in its set");
      const std::string& type = row[2].asString();
      if (type == "b") property.value = row[3].asBool();
      else if (type == "d") property.value = finite(row[3], "property value");
      else if (type == "s") property.value = boundedText(row[3], ExportLimits::valueBytes, "property value");
      else bad("an unknown property type");
      if (++properties > ExportLimits::properties) bad("too many properties");
      node.properties.push_back(std::move(property));
    }
    package.nodes.push_back(std::move(node));
  }
  if (package.nodes.empty() || package.nodes.front().role != "project") bad("the first node is not the project");
  return package;
}

}  // namespace sketchyup::ifc
