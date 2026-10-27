#include "import.hpp"

#include "json_out.hpp"
#include "report.hpp"
#include "sha256.hpp"

#include <openskp/parser.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <climits>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sys/resource.h>
#include <thread>
#include <unistd.h>

namespace sketchyup::skp {
namespace {

// On failure `code` is "tooLarge" for a file over the limit and "unreadable" for one that cannot
// be opened or read, so a denied open is never reported as an oversize file.
bool readFile(const std::string& path, std::vector<std::uint8_t>& out, std::uint64_t sizeLimit,
              std::string& problem, std::string& code) {
  code = "unreadable";
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    problem = "The file could not be opened: " + path;
    return false;
  }
  in.seekg(0, std::ios::end);
  const std::uint64_t size = static_cast<std::uint64_t>(in.tellg());
  in.seekg(0, std::ios::beg);
  if (size > sizeLimit) {
    code = "tooLarge";
    problem = "The file is " + std::to_string(size) + " bytes; the limit is " +
              std::to_string(sizeLimit) + " bytes.";
    return false;
  }
  out.resize(size);
  if (size) in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size));
  if (!in) {
    problem = "The file could not be read: " + path;
    return false;
  }
  return true;
}

// The parser's raw coordinates are inches (the parser's own glTF exporter converts them to
// metres with the fixed 0.0254, whatever the file's unit system says: the unit system is
// display metadata, and the file's internal unit is inches, VFF and legacy alike).
constexpr double kInchesToMetres = 0.0254;

int versionMajor(const std::string&);

// The major version written in the file header (UTF-16LE, before any parsing), or -1.
int headerVersionMajor(const std::vector<std::uint8_t>& d) {
  std::size_t marker = std::string::npos;
  for (std::size_t i = 4; i + 2 < d.size() && i < 512; ++i)
    if (d[i] == 0xFF && d[i + 1] == 0xFE && d[i + 2] == 0xFF) {
      marker = i;
      break;
    }
  if (marker == std::string::npos) return -1;
  std::string s;
  for (std::size_t i = marker + 4; i + 1 < d.size() && i < 512; i += 2)
    if (d[i]) s.push_back(static_cast<char>(d[i]));
  const std::size_t a = s.find('{');
  if (a == std::string::npos || a + 1 >= s.size() || s.find('}', a) < a) return -1;
  return versionMajor(s);
}
// The version string ("{24.0.553}") to its major number, or -1.
int versionMajor(const std::string& version) {
  std::size_t start = version.find_first_not_of('{');
  if (start == std::string::npos) return -1;
  const std::size_t end = version.find('.', start + 1);
  if (end == std::string::npos) return -1;
  int major = 0;
  for (std::size_t i = start; i < end; ++i)
    if (std::isdigit(static_cast<unsigned char>(version[i]))) major = major * 10 + (version[i] - '0');
  else return -1;
  return major;
}

std::vector<std::uint8_t> readWholeFile(const std::string& path, std::uint64_t limit) {
  std::vector<std::uint8_t> out;
  std::string problem, code;
  if (!readFile(path, out, limit, problem, code)) {
    // A missing file is a refusal the same way a bad header is.
    JsonOut json;
    json.beginObject().field("status", "refused").field("code", code).field("message", clip(problem, 256)).endObject();
    std::fputs(("R " + json.str() + "\n").c_str(), stdout);
    std::fflush(stdout);
    std::exit(3);
  }
  return out;
}

void applyLimits(const SkpImportOptions& options) {
  struct rlimit as{options.rlimitAs, options.rlimitAs};
  if (options.rlimitAs) setrlimit(RLIMIT_AS, &as);
  struct rlimit cpu{static_cast<rlim_t>(options.rlimitCpu), static_cast<rlim_t>(options.rlimitCpu)};
  if (options.rlimitCpu > 0) setrlimit(RLIMIT_CPU, &cpu);
  struct rlimit fsize{options.rlimitFsize, options.rlimitFsize};
  if (options.rlimitFsize) setrlimit(RLIMIT_FSIZE, &fsize);
}

// The dynamic_attributes dictionary of a Dynamic Component (ADR 0178 section 5.2, R104.a).
constexpr const char* kDynamicDictionary = "dynamic_attributes";

enum class Packed { Kept, Unsupported, Dropped };

// One attribute value for the package (R102.b, ADR 0176 section 4; R104.a for dynamic_attributes). Outside
// dynamic_attributes a value the package does not carry is Dropped, with `why`. Inside it a Length is kept in
// metres, and a value the package cannot carry (a vector, an array, a null, a number that is not finite) is
// kept Unsupported, with the kind of value in `out.text`, so the import can count it. `out` is filled for
// Kept and Unsupported.
Packed packAttribute(const std::string& dictionary, const std::string& key, const openskp::ParsedAttribute& value,
                     SkpPackageAttribute& out, std::string& why) {
  const bool dynamic = dictionary == kDynamicDictionary;
  out.dictionary = clip(dictionary, SkpPackageLimits::nameBytes);
  out.key = clip(key, SkpPackageLimits::nameBytes);
  std::string kind, reason;
  switch (value.kind) {
    case openskp::ParsedAttribute::Kind::String:
      out.kind = SkpPackageAttribute::Kind::String;
      out.text = clip(value.text, SkpPackageLimits::stringBytes);
      out.truncated = value.text.size() > SkpPackageLimits::stringBytes;
      return Packed::Kept;
    case openskp::ParsedAttribute::Kind::Integer:
      out.kind = SkpPackageAttribute::Kind::Number;
      out.number = static_cast<double>(value.integer);
      return Packed::Kept;
    case openskp::ParsedAttribute::Kind::Float:
      if (std::isfinite(value.number)) {
        out.kind = SkpPackageAttribute::Kind::Number;
        out.number = value.number;
        return Packed::Kept;
      }
      kind = "notFinite";
      reason = "a number that is not finite is not imported";
      break;
    case openskp::ParsedAttribute::Kind::Boolean:
      out.kind = SkpPackageAttribute::Kind::Boolean;
      out.flag = value.integer != 0;
      return Packed::Kept;
    case openskp::ParsedAttribute::Kind::Length:
      if (!dynamic) {
        why = "a length is not imported";
        return Packed::Dropped;
      }
      if (std::isfinite(value.number)) {
        out.kind = SkpPackageAttribute::Kind::Length;
        out.number = value.number * kInchesToMetres;  // the parser's inches, as metres
        return Packed::Kept;
      }
      kind = "notFinite";
      reason = "a number that is not finite is not imported";
      break;
    case openskp::ParsedAttribute::Kind::Vec:
      kind = "vector";
      reason = "a vector is not imported";
      break;
    case openskp::ParsedAttribute::Kind::Array:
      kind = "array";
      reason = "a nested array is not imported";
      break;
    default:
      kind = "null";
      reason = "an empty value is not imported";
      break;
  }
  why = reason;
  if (!dynamic)
    return Packed::Dropped;
  out.kind = SkpPackageAttribute::Kind::Unsupported;
  out.text = kind;
  return Packed::Unsupported;
}

}  // namespace

double unitScale(const std::string& units) {
  (void)units;
  return kInchesToMetres;
}

int runProbe(const std::string& input, Progress& progress) {
  progress.phase("read");
  const std::vector<std::uint8_t> bytes = readWholeFile(input, 1024 * 1024 * 1024);
  if (bytes.size() < 4 || bytes[0] != 0xFF || bytes[1] != 0xFE || bytes[2] != 0xFF || bytes[3] != 0x0E) {
    JsonOut json;
    json.beginObject().field("status", "refused").field("code", "notSkp")
        .field("message", "The file does not start with a SketchUp header.")
        .endObject();
    progress.line("R " + json.str());
    return 3;
  }
  // ADR 0176 section 2: before 2013 (major < 13) is refused from the header version before the
  // parser runs, because some pre-2013 files the parser cannot read at all.
  {
    const int headerMajor = headerVersionMajor(bytes);
    if (headerMajor > 0 && headerMajor < 13) {
      JsonOut json;
      json.beginObject()
          .field("status", "refused")
          .field("code", "pre2013")
          .field("message", "This SketchUp file is from before 2013. Save it from SketchUp 2021 "
                            "or later and try again.")
          .endObject();
      progress.line("R " + json.str());
      return 3;
    }
  }
  progress.phase("parse");
  try {
    const openskp::SkpModel model = openskp::parse_skp(bytes);
    // ADR 0176 section 2: before 2013 (major < 13) is refused with the advice; a version the
    // reader could not read (-1) is reported, not refused.
    const int major = versionMajor(model.version);
    if (major > 0 && major < 13) {
      JsonOut json;
      json.beginObject()
          .field("status", "refused")
          .field("code", "pre2013")
          .field("message", "This SketchUp file is from before 2013. Save it from SketchUp 2021 "
                            "or later and try again.")
          .endObject();
      progress.line("R " + json.str());
      return 3;
    }
    SkpReport report;
    // The probe counts (ADR 0176 section 4): every definition, the model's root included.
    const auto countOne = [&](const openskp::Definition& definition) {
      report.counts_.definitions++;
      report.counts_.vertices += definition.vertices.size();
      report.counts_.edges += definition.edges.size();
      report.counts_.faces += definition.faces.size();
      report.counts_.instances += definition.instances.size();
      for (const auto& entry : definition.faces)
        if (entry.second.loops.size() > 1)
          report.counts_.holes += static_cast<long long>(entry.second.loops.size() - 1);
    };
    countOne(model.root());
    for (const auto& [id, definition] : model.definitions) (void)id, countOne(definition);
    report.counts_.materials = static_cast<long long>(model.materials.size());
    report.counts_.tags = static_cast<long long>(model.layers.size());
    JsonOut json;
    json.beginObject()
        .field("version", model.version)
        .field("container", versionMajor(model.version) > 20 ? "vff" : "mfc");
    if (model.units) json.field("units", *model.units);
    json.key("counts");
    json.beginObject()
        .field("definitions", report.counts_.definitions)
        .field("vertices", report.counts_.vertices)
        .field("edges", report.counts_.edges)
        .field("faces", report.counts_.faces)
        .field("holes", report.counts_.holes)
        .field("instances", report.counts_.instances)
        .field("materials", report.counts_.materials)
        .field("tags", report.counts_.tags)
        .endObject()
        .endObject();
    progress.line("R " + json.str());
    return 0;
  } catch (const std::exception& problem) {
    JsonOut json;
    json.beginObject().field("status", "refused").field("code", "parse")
        .field("message", clip(problem.what(), 256))
        .endObject();
    progress.line("R " + json.str());
    return 3;
  }
}

int runImport(const SkpImportOptions& options, Progress& progress) {
  // The 120 s deadline, watched from a thread: when it fires the worker reports a refusal and
  // exits 3, whatever the parser is doing.
  if (options.deadlineSeconds > 0) {
    std::thread([seconds = options.deadlineSeconds] {
      for (int i = 0; i < seconds * 10; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
      JsonOut json;
      json.beginObject().field("status", "refused").field("code", "deadline")
          .field("message", "The import took " + std::to_string(seconds) + " s; the deadline is " +
                           std::to_string(seconds) + " s.")
          .endObject();
      std::fputs(("R " + json.str() + "\n").c_str(), stdout);
      std::fflush(stdout);
      std::quick_exit(3);
    }).detach();
  }
  applyLimits(options);

  progress.phase("read");
  std::vector<std::uint8_t> bytes;
  std::string problem, code;
  if (!readFile(options.input, bytes, options.sizeLimitBytes, problem, code)) {
    JsonOut json;
    json.beginObject().field("status", "refused").field("code", code).field("message", clip(problem, 256)).endObject();
    progress.line("R " + json.str());
    return 3;
  }
  if (bytes.size() < 4 || bytes[0] != 0xFF || bytes[1] != 0xFE || bytes[2] != 0xFF || bytes[3] != 0x0E) {
    JsonOut json;
    json.beginObject().field("status", "refused").field("code", "notSkp")
        .field("message", "The file does not start with a SketchUp header.")
        .endObject();
    progress.line("R " + json.str());
    return 3;
  }
  // ADR 0176 section 2: before 2013 (major < 13) is refused from the header version before the
  // parser runs, because some pre-2013 files the parser cannot read at all.
  {
    const int headerMajor = headerVersionMajor(bytes);
    if (headerMajor > 0 && headerMajor < 13) {
      JsonOut json;
      json.beginObject()
          .field("status", "refused")
          .field("code", "pre2013")
          .field("message", "This SketchUp file is from before 2013. Save it from SketchUp 2021 "
                            "or later and try again.")
          .endObject();
      progress.line("R " + json.str());
      return 3;
    }
  }
  // The file's digest and size, recorded in the manifest (the bytes move into the parser).
  const std::uint64_t fileSize = bytes.size();
  const std::string fileDigest = sha256_hex(bytes.data(), bytes.size());
  progress.phase("parse");
  openskp::SkpModel model;
  try {
    model = openskp::parse_skp(std::move(bytes));
  } catch (const std::exception& e) {
    JsonOut json;
    json.beginObject().field("status", "refused").field("code", "parse")
        .field("message", clip(e.what(), 256))
        .endObject();
    progress.line("R " + json.str());
    return 3;
  }

  // ADR 0176 section 2: before 2013 (major < 13) is refused with the advice; a version the
  // reader could not read (-1) is attempted as-is, and one newer than the corpus is labelled.
  const int major = versionMajor(model.version);
  if (major > 0 && major < 13) {
    JsonOut json;
    json.beginObject()
        .field("status", "refused")
        .field("code", "pre2013")
        .field("message", "This SketchUp file is from before 2013. Save it from SketchUp 2021 "
                          "or later and try again.")
        .endObject();
    progress.line("R " + json.str());
    return 3;
  }

  // The unit system: display metadata, kept in the manifest for the user. The conversion
  // below is the fixed inches-to-metres scale (the parser's raw is inches, always).
  std::string units = model.units ? *model.units : std::string();

  progress.phase("package");
  SkpPackage package;
  SkpReport report;
  if (major > 25) report.notice("newerThanTested");
  package.formatVersion = model.version;
  package.container = major > 20 ? "vff" : "mfc";
  package.units = units;
  package.unitScale = kInchesToMetres;
  package.sourceName = options.input;
  package.sourceSize = fileSize;
  package.sourceSha256 = fileDigest;
  if (model.definitions.empty() && model.root().vertices.empty()) {
    // A file the parser read as an empty model: recorded, not an error.
    report.notice("emptyModel");
  }

  // Materials: the file's sequence, by its TLV id.
  std::map<openskp::EntityId, long long> materialIndex;
  for (long long i = 0; i < static_cast<long long>(model.materials.size()); ++i) {
    const auto& material = model.materials[static_cast<std::size_t>(i)];
    SkpPackageMaterial out;
    out.name = clip(material.name, SkpPackageLimits::nameBytes);
    out.color = material.color;
    out.opacity = material.transparency;
    if (material.id) materialIndex[*material.id] = i;
    if (material.texture && material.texture->data) {
      const auto& texture = *material.texture;
      // Deduplicate a texture used by several materials; the report keeps the total.
      auto existing = std::find_if(package.textures.begin(), package.textures.end(),
                                [&](const SkpPackageTexture& t) {
                                  return t.filename == texture.filename && t.bytes.size() == texture.data->size();
                                });
      if (existing != package.textures.end()) {
        out.texture = static_cast<long long>(existing - package.textures.begin());
        report.notice("texturesDeduplicated");
      } else {
        out.texture = static_cast<long long>(package.textures.size());
        SkpPackageTexture t;
        t.filename = clip(texture.filename, SkpPackageLimits::nameBytes);
        t.width = texture.width;
        t.height = texture.height;
        t.bytes = *texture.data;
        package.textures.push_back(std::move(t));
      }
    }
    package.materials.push_back(std::move(out));
  }
  report.counts_.materials = static_cast<long long>(package.materials.size());
  report.counts_.textures = static_cast<long long>(package.textures.size());

  // Tags: the model's layers.
  std::map<std::string, long long> tagIndex;
  for (const auto& layer : model.layers) {
    tagIndex[layer.name] = static_cast<long long>(package.tags.size());
    SkpPackageTag tag;
    tag.name = clip(layer.name, SkpPackageLimits::nameBytes);
    tag.color = layer.color;
    tag.hidden = layer.hidden;
    package.tags.push_back(std::move(tag));
  }
  report.counts_.tags = static_cast<long long>(package.tags.size());

  // Every attribute value the file holds (ADR 0176 section 4): attributes read = kept on instances
  // + omitted (the kinds below). A value is counted once, in the scope that holds it.
  long long attributesPresent = 0;
  const auto noteNotCarried = [&](const char *code, const char *kind, long long count) {
      if (count <= 0)
          return;
      report.notice(code, count);
      report.omitted(kind, count);
  };
  // Layers (tags) hold attribute dictionaries only in legacy files; the import carries none of
  // them.
  long long layerValues = 0;
  for (const auto &layer : model.layers)
      for (const auto &[dictionary, values] : layer.attribute_dictionaries)
          for (const auto &[key, value] : values) {
              (void)value;
              attributesPresent++;
              layerValues++;
              report.locate("layerAttributesNotCarried", clip(layer.name, 64),
                            "dictionary \"" + clip(dictionary, 64) + "\", key \"" + clip(key, 64) +
                                "\": not imported");
          }
  noteNotCarried("layerAttributesNotCarried", "layerAttribute", layerValues);
  // The model's own attribute dictionaries: not mapped in this version, so every value is reported
  // (the samples name the dictionary and the key).
  long long modelValues = 0;
  for (const auto &[dictionary, values] : model.attribute_dictionaries)
      for (const auto &[key, value] : values) {
          (void)value;
          attributesPresent++;
          modelValues++;
          report.locate("modelAttributesNotCarried", "Model",
                        "dictionary \"" + clip(dictionary, 64) + "\", key \"" + clip(key, 64) +
                            "\": not imported");
      }
  noteNotCarried("modelAttributesNotCarried", "modelAttribute", modelValues);
  // What the file holds that the package does not carry, counted (ADR 0176 sections 4 and 5:
  // "counted in the report"). A part the file does not hold is no notice.
  const auto countOf = [&](auto member) {
      long long total = static_cast<long long>((model.root().*member).size());
      for (const auto &[id, definition] : model.definitions) {
          (void)id;
          total += static_cast<long long>((definition.*member).size());
      }
      return total;
  };
  const auto noteCount = [&](const char *code, long long count) {
      if (count > 0)
          report.notice(code, count);
  };
  noteCount("omittedScenes", static_cast<long long>(model.pages.size()));
  noteCount("omittedStyles", static_cast<long long>(model.styles.size()));
  noteCount("omittedSectionPlanes", countOf(&openskp::Definition::section_planes));
  noteCount("omittedTexts", countOf(&openskp::Definition::texts));
  noteCount("omittedDimensions", countOf(&openskp::Definition::dimensions) +
                                     static_cast<long long>(model.dimensions.size()));
  noteCount("omittedConstructionLines", countOf(&openskp::Definition::construction_lines));
  noteCount("omittedConstructionPoints", countOf(&openskp::Definition::construction_points));
  if (model.camera)
      report.notice("omittedCamera");

  // Definitions: the model's root first, then the file's definitions in id order. Every id is
  // resolvable before any instance is read, so an instance's reference is never dropped on the
  // way to a definition that is added later.
  std::map<openskp::EntityId, long long> definitionIndex;
  definitionIndex[model.root().id] = 0;
  {
    long long index = 1;
    for (const auto& [id, definition] : model.definitions) {
      (void)id;
      definitionIndex[definition.id] = index++;
    }
  }
  // The file's totals, for the report's accounting identity (imported + omitted = counted).
  long long sourceEdges = 0, sourceFaces = 0, sourceInstances = 0;
  const auto totalOf = [&](const openskp::Definition& definition) {
    sourceEdges += static_cast<long long>(definition.edges.size());
    sourceFaces += static_cast<long long>(definition.faces.size());
    sourceInstances += static_cast<long long>(definition.instances.size());
  };
  totalOf(model.root());
  for (const auto& [id, definition] : model.definitions) {
    (void)id;
    totalOf(definition);
  }
  const long long sourceMaterials = static_cast<long long>(model.materials.size());
  long long definitionValues = 0;
  auto addDefinition = [&](const openskp::Definition& source, const std::string& name, const std::string& guid,
                           bool isRoot) {
    SkpPackageDefinition definition;
    definition.name = clip(name, SkpPackageLimits::nameBytes);
    definition.guid = clip(guid, SkpPackageLimits::stringBytes);
    // The definition's own attribute dictionaries (R104.a). A Dynamic Component's dynamic_attributes values are
    // kept on the definition, for the placements of it (the import attaches them). Any other dictionary is not
    // mapped, so each value is reported. The root is the model, not a component: its values are reported.
    for (const auto& [dictionary, values] : source.attribute_dictionaries)
        for (const auto& [key, value] : values) {
            attributesPresent++;
            const std::string named =
                "dictionary \"" + clip(dictionary, 64) + "\", key \"" + clip(key, 64) + "\"";
            if (isRoot || dictionary != kDynamicDictionary) {
                definitionValues++;
                report.locate("definitionAttributesNotCarried", definition.name, named + ": not imported");
                continue;
            }
            const auto dropDefinition = [&](const char *code, const std::string &why) {
                report.notice(code);
                report.omitted("definitionAttribute");
                report.locate(code, definition.name, named + ": " + why);
            };
            if (key.empty()) {
                dropDefinition("attributeNotImported", "the key is empty, so it is not imported");
                continue;
            }
            if (definition.attributes.size() >= SkpPackageLimits::attributesPerInstance) {
                dropDefinition("attributesOverLimit", "the definition holds more attributes than the package keeps");
                continue;
            }
            SkpPackageAttribute attribute;
            std::string why;
            if (packAttribute(dictionary, key, value, attribute, why) == Packed::Dropped)
                dropDefinition("attributeNotImported", why);
            else
                definition.attributes.push_back(std::move(attribute));
        }
    definitionIndex[source.id] = static_cast<long long>(package.definitions.size());
    const auto materialOf = [&](std::optional<openskp::EntityId> id) {
      if (!id) return -1LL;
      const auto it = materialIndex.find(*id);
      if (it == materialIndex.end()) {
        report.locate("notice", definition.name, "A face references a material the file does not record.");
        return -1LL;
      }
      return it->second;
    };
    const auto tagOf = [&](const std::string& layer) {
      if (layer.empty()) return -1LL;
      const auto it = tagIndex.find(layer);
      if (it == tagIndex.end()) {
        // A layer the file recorded on a face but does not list: record it, do not drop the face.
        const long long index = static_cast<long long>(package.tags.size());
        tagIndex.emplace(layer, index);
        SkpPackageTag tag;
        tag.name = clip(layer, SkpPackageLimits::nameBytes);
        package.tags.push_back(std::move(tag));
        return index;
      }
      return it->second;
    };
    // The vertex id -> its position, once: the edge and face-loop reads are O(1) each. The
    // bounding boxes (metres and the parser's raw inches) over every vertex the package
    // keeps; the metres one is the inches one x the unit scale, exactly.
    std::map<openskp::EntityId, std::uint32_t> vertexIndex;
    for (const auto& [id, vertex] : source.vertices) {
      (void)id;
      vertexIndex[vertex.id] = static_cast<std::uint32_t>(definition.vertices.size());
      const std::array<double, 3> raw{vertex.x, vertex.y, vertex.z};
      const std::array<double, 3> point{raw[0] * kInchesToMetres, raw[1] * kInchesToMetres,
                                       raw[2] * kInchesToMetres};
      definition.vertices.push_back(point);
      if (!package.hasBounding) {
        package.hasBounding = true;
        package.boundingMin = point;
        package.boundingMax = point;
        package.boundingMinInches = raw;
        package.boundingMaxInches = raw;
      } else {
        for (int axis = 0; axis < 3; ++axis) {
          package.boundingMin[axis] = std::min(package.boundingMin[axis], point[axis]);
          package.boundingMax[axis] = std::max(package.boundingMax[axis], point[axis]);
          package.boundingMinInches[axis] = std::min(package.boundingMinInches[axis], raw[axis]);
          package.boundingMaxInches[axis] = std::max(package.boundingMaxInches[axis], raw[axis]);
        }
      }
    }
    const auto indexOf = [&](openskp::EntityId id, const char* what) -> std::uint32_t {
      const auto it = vertexIndex.find(id);
      if (it == vertexIndex.end()) {
        report.locate("notImported", definition.name,
                     std::string("A ") + what + " references a vertex the definition does not record.");
        return 0;  // a placeholder; the caller drops the entry that needs it
      }
      return it->second;
    };
    for (const auto& [id, edge] : source.edges) {
      (void)id;
      const auto v1 = indexOf(edge.v1_id, "edge");
      const auto v2 = indexOf(edge.v2_id, "edge");
      const auto e1 = vertexIndex.find(edge.v1_id);
      const auto e2 = vertexIndex.find(edge.v2_id);
      if (e1 == vertexIndex.end() || e2 == vertexIndex.end()) {
        report.omitted("edge");
        continue;
      }
      SkpPackageEdge out;
      out.v1 = v1;
      out.v2 = v2;
      out.soft = edge.soft;
      out.smooth = edge.smooth;
      out.hidden = edge.hidden;
      // A construction line: the parser records it as an edge with both vertices the same;
      // the conversion keeps it as a plain edge (the wire flag is the caller's reading).
      definition.edges.push_back(std::move(out));
    }
    for (const auto& [id, face] : source.faces) {
      (void)id;
      SkpPackageFace out;
      out.hidden = face.hidden;
      out.materialFront = materialOf(face.material_id);
      out.materialBack = materialOf(face.back_material_id);
      out.tag = tagOf(face.layer);
      for (const auto& loop : face.loops) {
        std::vector<std::uint32_t> indices;
        indices.reserve(loop.size());
        bool complete = true;
        for (const auto& coedge : loop) {
          const auto edgeIt = source.edges.find(coedge.edge_id);
          if (edgeIt == source.edges.end()) {
            report.locate("notImported", definition.name, "A face loop references an edge the definition does not have.");
            complete = false;
            break;
          }
          const auto& edge = edgeIt->second;
          const auto end = coedge.orientation == 1 ? edge.v2_id : edge.v1_id;
          const auto vIt = vertexIndex.find(end);
          if (vIt == vertexIndex.end()) {
            report.locate("notImported", definition.name, "A face loop references a vertex the definition does not have.");
            complete = false;
            break;
          }
          indices.push_back(vIt->second);
        }
        if (!complete) continue;
        if (indices.size() >= 3) {
          if (!out.loops.empty() && out.loops.size() >= SkpPackageLimits::loopsPerFace) {
            report.locate("notImported", definition.name, "A face has more loops than the package allows.");
            continue;
          }
          if (indices.size() > SkpPackageLimits::loopVertices) continue;
          out.loops.push_back(std::move(indices));
        } else {
          report.locate("notImported", definition.name, "A face loop has fewer than 3 vertices after the read.");
        }
      }
      // The stored UV transform, as stored (R102.b decides how the mapping applies it).
      out.hasUvFront = face.uv_transform.has_value();
      out.hasUvBack = face.uv_transform_back.has_value();
      out.uvFrontProjected = face.uv_projected;
      out.uvBackProjected = face.uv_projected_back;
      if (face.uv_transform) out.uvFront = *face.uv_transform;
      if (face.uv_transform_back) out.uvBack = *face.uv_transform_back;
      if (out.hasUvFront) report.counts_.uvTransforms++;
      if (out.hasUvBack) report.counts_.uvTransforms++;
      // A missing mapping matters only on a side whose material has a texture: an untextured face has
      // nothing to map, so it is not a notice (an untextured model would otherwise report every face).
      const auto textured = [&](long long material) {
        return material >= 0 && material < static_cast<long long>(package.materials.size()) &&
               package.materials[static_cast<std::size_t>(material)].texture >= 0;
      };
      if ((textured(out.materialFront) && !out.hasUvFront) || (textured(out.materialBack) && !out.hasUvBack))
        report.notice("missingUv");
      if (out.loops.empty()) {
        report.omitted("face");
        report.locate("notImported", definition.name, "A face has no readable loop and is left out.");
        continue;
      }
      if (out.loops.size() > 1) report.counts_.holes += static_cast<long long>(out.loops.size() - 1);
      definition.faces.push_back(std::move(out));
    }
    for (const auto& instance : source.instances) {
      SkpPackageInstance out;
      out.definition = -1;
      if (instance.ref_idx) {
        const auto it = definitionIndex.find(*instance.ref_idx);
        if (it != definitionIndex.end()) out.definition = it->second;
        else report.locate("notImported", definition.name, "An instance references a definition the file does not record.");
      }
      out.hidden = instance.hidden;
      out.tag = tagOf(instance.layer);
      out.material = materialOf(instance.material_id);
      // R102.b: the instance's name, and its attribute dictionaries of the three kinds the import keeps
      // (a string, a number or a boolean; a Length, a vector, an array or a null is not kept).
      out.name = clip(instance.name, SkpPackageLimits::nameBytes);
      // R102.b, and ADR 0176 section 4 (the follow-up "attribute reporting"): the instance's
      // attributes the import keeps (a string, a number or a boolean, at most attributesPerInstance
      // of them), and each one it does not, counted as omitted and named in the samples (the
      // dictionary and the key). A string longer than the package bound is cut and flagged; the
      // mapping cuts it again to the 2,048-byte property cap and counts that too.
      for (const auto& [dictionary, values] : instance.attribute_dictionaries) {
        for (const auto& [key, value] : values) {
            attributesPresent++;
            const std::string named =
                "dictionary \"" + clip(dictionary, 64) + "\", key \"" + clip(key, 64) + "\"";
            const auto dropInstance = [&](const char *code, const std::string &why) {
                report.notice(code);
                report.omitted("attribute");
                report.locate(code, definition.name, named + ": " + why);
            };
            if (key.empty()) {
                dropInstance("attributeNotImported", "the key is empty, so it is not imported");
                continue;
            }
            if (out.attributes.size() >= SkpPackageLimits::attributesPerInstance) {
                dropInstance("attributesOverLimit", "the instance holds more attributes than the package keeps");
                continue;
            }
            SkpPackageAttribute attribute;
            std::string why;
            if (packAttribute(dictionary, key, value, attribute, why) == Packed::Dropped)
                dropInstance("attributeNotImported", why);
            else
                out.attributes.push_back(std::move(attribute));
        }
      }
      // The 4x4: the parser's 3x3 (unitless), the translation in metres. The parser's own
      // transform ignores the 13th value, w; ADR 0176 section 4 divides the translation by w
      // when it is not 1 and records the scaled placement.
      double wScale = 1.0;
      if (instance.matrix.size() >= 13) {
        const double w = instance.matrix[12];
        if (std::abs(w - 1.0) > 1e-9) {
          report.notice("scaledPlacement");
          if (w != 0.0) wScale = 1.0 / w;
        }
      }
      const auto value = [&](int i) { return instance.matrix.size() > static_cast<std::size_t>(i) ? instance.matrix[static_cast<std::size_t>(i)] : 0.0; };
      for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) out.transform[r * 4 + c] = value(r * 3 + c);
      out.transform[12] = value(9) * kInchesToMetres * wScale;
      out.transform[13] = value(10) * kInchesToMetres * wScale;
      out.transform[14] = value(11) * kInchesToMetres * wScale;
      out.transform[15] = 1.0;
      definition.instances.push_back(std::move(out));
    }
    report.counts_.definitions++;
    report.counts_.vertices += static_cast<long long>(definition.vertices.size());
    report.counts_.edges += static_cast<long long>(definition.edges.size());
    report.counts_.faces += static_cast<long long>(definition.faces.size());
    report.counts_.instances += static_cast<long long>(definition.instances.size());
    if (definition.vertices.size() < 3) {
      // A definition the file recorded but with no usable geometry: recorded, not an error.
      report.locate("notice", definition.name, "A definition has fewer than 3 vertices and no geometry of its own.");
    }
    package.definitions.push_back(std::move(definition));
  };
  addDefinition(model.root(), "ROOT", "", true);
  for (const auto& [id, definition] : model.definitions) (void)id, addDefinition(definition, definition.name, definition.guid, false);

  // The definitions' own attribute dictionaries are counted before the report block is copied out.
  noteNotCarried("definitionAttributesNotCarried", "definitionAttribute", definitionValues);

  // The report block.
  for (const auto& [code, count] : report.notices_) package.notices[code] = count;
  package.omitted = report.omitted_;
  package.sourceTotals = {{"edges", sourceEdges},
                          {"faces", sourceFaces},
                          {"instances", sourceInstances},
                          {"materials", sourceMaterials},
                          {"attributes", attributesPresent}};
  package.locations = report.locations_;
  package.locationTotals = report.locationsPerKind_;
  package.counts = report.counts_;
  package.counts.definitions = static_cast<long long>(package.definitions.size());

  if (options.dryRun) {
    JsonOut json;
    json.beginObject().field("status", "ok").field("dryRun", true)
        .field("definitions", report.counts_.definitions)
        .field("vertices", report.counts_.vertices)
        .field("edges", report.counts_.edges)
        .field("faces", report.counts_.faces)
        .endObject();
    progress.line("R " + json.str());
    return 0;
  }

  std::string problem2;
  const std::string serialized = serializeSkpPackage(package, problem2);
  if (serialized.empty()) {
    JsonOut json;
    json.beginObject().field("status", "refused").field("code", "packageTooLarge").field("message", clip(problem2, 256)).endObject();
    progress.line("R " + json.str());
    return 3;
  }
  {
    std::ofstream out(options.output, std::ios::binary | std::ios::trunc);
    out.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
    out.flush();
    if (!out) {
      JsonOut json;
      json.beginObject().field("status", "refused").field("code", "writeFailed")
          .field("message", "The package could not be written.")
          .endObject();
      progress.line("R " + json.str());
      return 3;
    }
  }
  JsonOut json;
  json.beginObject().field("status", "ok").field("bytes", serialized.size())
      .field("definitions", report.counts_.definitions)
      .field("vertices", report.counts_.vertices)
      .field("edges", report.counts_.edges)
      .field("faces", report.counts_.faces)
      .field("instances", report.counts_.instances)
      .endObject();
  progress.line("R " + json.str());
  return 0;
}

}  // namespace sketchyup::skp
