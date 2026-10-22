// --probe-api: drives the geometry iterator through the settings API the import will use and
// reports, per element, what the library returns (ADR 0171 section 1.2 claims).
#include <cmath>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>

#include "ifc/json_out.hpp"
#include "ifc/probe.hpp"
#include "ifcgeom/conversion_settings.h"
#include "ifcgeom/element.h"
#include "ifcgeom/filter.h"
#include "ifcgeom/iterator.h"
#include "ifcgeom/kernel_registry.h"
#include "ifcgeom/representation.h"
#include "ifcparse/file.h"
#include "ifcparse/logger.h"
#include "ifcparse/schema.h"

namespace sketchyup::ifc {
namespace {

std::vector<std::string> split(const std::string& text, char separator) {
  std::vector<std::string> parts;
  std::string current;
  for (char c : text) {
    if (c == separator) {
      parts.push_back(current);
      current.clear();
    } else {
      current += c;
    }
  }
  parts.push_back(current);
  return parts;
}

// Sets one named setting from text, by the setting's declared type.
void applySetting(ifcopenshell::geom::settings& settings, const std::string& name, const std::string& text) {
  using Value = ifcopenshell::geom::settings::value_variant_t;
  const std::string type = settings.get_type(name);
  Value value;
  if (type == "bool") {
    value = (text == "true" || text == "1");
  } else if (type == "int") {
    value = std::stoi(text);
  } else if (type == "double") {
    value = std::stod(text);
  } else if (type == "std::string") {
    value = text;
  } else if (type == "std::set<int>") {
    std::set<int> numbers;
    for (const std::string& part : split(text, ',')) numbers.insert(std::stoi(part));
    value = numbers;
  } else if (type == "std::set<std::string>") {
    const auto parts = split(text, ';');
    value = std::set<std::string>(parts.begin(), parts.end());
  } else if (type == "std::vector<std::string>") {
    value = split(text, ';');
  } else if (type == "std::vector<double>") {
    std::vector<double> numbers;
    for (const std::string& part : split(text, ',')) numbers.push_back(std::stod(part));
    value = numbers;
  } else if (type == "IteratorOutputOptions" || type == "OutputDimensionalityTypes" ||
             type == "FunctionStepMethod" || type == "TriangulationMethod") {
    value = std::stoi(text);  // an enumeration, given by its number (the setting converts it)
  } else {
    throw std::runtime_error("setting " + name + " has unsupported type " + type);
  }
  settings.set(name, value);
}

const char* severityName(int severity) {
  switch (severity) {
    case ifcopenshell::logger::LOG_PERF: return "perf";
    case ifcopenshell::logger::LOG_DEBUG: return "debug";
    case ifcopenshell::logger::LOG_NOTICE: return "notice";
    case ifcopenshell::logger::LOG_WARNING: return "warning";
    case ifcopenshell::logger::LOG_ERROR: return "error";
    default: return "unknown";
  }
}

void writeMessages(JsonOut& json, const char* key, const ifcopenshell::logger& log) {
  json.key(key).beginArray();
  for (const auto& message : log.log_messages()) {
    json.beginObject()
        .field("severity", severityName(message.severity))
        .field("code", std::string(message.code))
        .field("message", message.message)
        .field("instance", message.instance)
        .field("product", message.product)
        .endObject();
  }
  json.endArray();
}

void writeElement(JsonOut& json, const ifcopenshell::geom::triangulation_element& element) {
  const auto& mesh = element.geometry();
  const auto& verts = mesh.verts();
  const auto& faces = mesh.faces();
  double lo[3] = {INFINITY, INFINITY, INFINITY};
  double hi[3] = {-INFINITY, -INFINITY, -INFINITY};
  for (std::size_t i = 0; i + 2 < verts.size(); i += 3)
    for (int axis = 0; axis < 3; ++axis) {
      lo[axis] = std::min(lo[axis], verts[i + axis]);
      hi[axis] = std::max(hi[axis], verts[i + axis]);
    }
  // Volume (divergence theorem) and area of the triangle soup, in the mesh's own frame.
  double volume = 0;
  double area = 0;
  for (std::size_t t = 0; t + 2 < faces.size(); t += 3) {
    const double* a = &verts[3 * static_cast<std::size_t>(faces[t])];
    const double* b = &verts[3 * static_cast<std::size_t>(faces[t + 1])];
    const double* c = &verts[3 * static_cast<std::size_t>(faces[t + 2])];
    const double cx = (b[1] - a[1]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[1] - a[1]);
    const double cy = (b[2] - a[2]) * (c[0] - a[0]) - (b[0] - a[0]) * (c[2] - a[2]);
    const double cz = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
    area += 0.5 * std::sqrt(cx * cx + cy * cy + cz * cz);
    volume += (a[0] * (b[1] * c[2] - b[2] * c[1]) - a[1] * (b[0] * c[2] - b[2] * c[0]) +
               a[2] * (b[0] * c[1] - b[1] * c[0])) / 6.0;
  }
  json.beginObject();
  json.field("step", element.id());
  json.field("guid", element.guid());
  json.field("type", element.type());
  json.field("name", element.name());
  json.field("context", element.context());
  json.field("vertices", verts.size() / 3);
  json.field("triangles", faces.size() / 3);
  json.field("edges", mesh.edges().size() / 2);
  json.field("normals", mesh.normals().size() / 3);
  json.key("bboxMin").doubles({lo[0], lo[1], lo[2]});
  json.key("bboxMax").doubles({hi[0], hi[1], hi[2]});
  json.field("volume", std::abs(volume));
  json.field("signedVolume", volume);
  json.field("area", area);
  std::vector<double> matrix;
  if (const auto& placement = element.transformation().data()) {
    const auto& components = placement->ccomponents();
    for (int row = 0; row < 4; ++row)
      for (int column = 0; column < 4; ++column) matrix.push_back(components(row, column));
  }
  json.key("transform").doubles(matrix);
  json.key("materials").beginArray();
  std::vector<int> perMaterial(mesh.materials().size(), 0);
  for (int id : mesh.material_ids())
    if (id >= 0 && static_cast<std::size_t>(id) < perMaterial.size()) ++perMaterial[static_cast<std::size_t>(id)];
  for (std::size_t i = 0; i < mesh.materials().size(); ++i) {
    const auto& style = mesh.materials()[i];
    json.beginObject();
    if (style) {
      json.field("name", style->name);
      const auto& colour = style->diffuse.ccomponents();
      json.key("diffuse").doubles({colour(0), colour(1), colour(2)});
      json.field("hasTransparency", style->has_transparency());
      json.field("transparency", style->has_transparency() ? style->transparency : 0.0);
    }
    json.field("triangles", perMaterial[i]);
    json.endObject();
  }
  json.endArray();
  std::set<int> items(mesh.item_ids().begin(), mesh.item_ids().end());
  json.key("itemSteps").beginArray();
  for (int item : items) json.value(item);
  json.endArray();
  json.endObject();
}

}  // namespace

std::string probeGeometry(const GeometryRunOptions& options) {
  JsonOut json;
  json.beginObject();
  json.field("file", options.file);
  json.field("kernel", options.kernel);
  json.field("threads", options.threads);
  try {
    // Parse with its own logger so the file's messages stay apart from the geometry's.
    ifcopenshell::logger parseLog;
    parseLog.output_format(ifcopenshell::logger::FMT_INMEMORY);
    parseLog.verbosity(ifcopenshell::logger::LOG_NOTICE);
    ifcopenshell::file file(options.file, ifcopenshell::FT_AUTODETECT, true, parseLog);
    json.field("fileGood", static_cast<bool>(file.good()));
    if (!file.good()) {
      json.field("error", "the file did not open");
      writeMessages(json, "parseMessages", parseLog);
      json.endObject();
      return json.str();
    }
    json.field("schema", file.schema()->name());
    std::size_t entities = 0;
    for (auto it = file.begin(); it != file.end(); ++it) ++entities;
    json.field("entities", entities);

    ifcopenshell::logger geometryLog;
    geometryLog.output_format(ifcopenshell::logger::FMT_INMEMORY);
    geometryLog.verbosity(ifcopenshell::logger::LOG_NOTICE);
    ifcopenshell::geom::settings settings;
    json.key("settingsApplied").beginArray();
    for (const auto& [name, value] : options.settings) {
      applySetting(settings, name, value);
      json.beginObject().field("name", name).field("value", value).endObject();
    }
    json.endArray();

    std::vector<ifcopenshell::geom::filter_function> filters;
    if (!options.excludeSteps.empty()) {
      const std::set<int> steps(options.excludeSteps.begin(), options.excludeSteps.end());
      filters.emplace_back(ifcopenshell::geom::instance_id_filter(false, false, steps));
    }
    ifcopenshell::geom::iterator iterator(
        ifcopenshell::geom::kernels::construct(&file, options.kernel, settings, geometryLog), settings, &file,
        filters, options.threads, geometryLog);
    const bool initialized = iterator.initialize();
    json.field("initialized", initialized);
    json.field("unitName", iterator.unit_name());
    json.field("unitMagnitude", iterator.unit_magnitude());
    json.field("lengthUnitSetting", settings.get<ifcopenshell::geom::settings::LengthUnit>().get());
    json.key("elements").beginArray();
    std::size_t emitted = 0;
    if (initialized) {
      while (true) {
        auto object = iterator.get();
        if (const auto* triangles = dynamic_cast<const ifcopenshell::geom::triangulation_element*>(object.get())) {
          writeElement(json, *triangles);
          ++emitted;
        }
        if (!iterator.next()) break;
      }
    }
    json.endArray();
    json.field("emitted", emitted);
    json.field("hadErrorProcessingElements", iterator.had_error_processing_elements());
    json.field("maxSeverity", severityName(geometryLog.max_severity()));
    writeMessages(json, "parseMessages", parseLog);
    writeMessages(json, "geometryMessages", geometryLog);
  } catch (const std::exception& problem) {
    json.unwindTo(1).field("exception", problem.what());
  } catch (...) {
    json.unwindTo(1).field("exception", "unknown exception");
  }
  json.endObject();
  return json.str();
}

}  // namespace sketchyup::ifc
