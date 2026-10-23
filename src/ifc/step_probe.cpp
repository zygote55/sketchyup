// The STEP probe (ADR 0172, R098.a). Reports what the OpenCASCADE STEP stack really does with
// a file: the header, FILE_SCHEMA (raw text and the model's), units (raw text and the
// model's), entities, root shapes, bounding boxes, and a write-read round trip through the
// basic writer (once with the default schema, once with the AP242 option), which settles on
// every run what schema the writer actually writes.
//
// The OpenCASCADE API the probe uses (verified against the V7_9_3 source; the 7.8 differences
// are the version spelling and the writer parameter's name, both spelt out below):
//  - STEPControl_Reader: ReadFile -> IFSelect_ReturnStatus; StepModel() ->
//    StepData_StepModel (LocalLengthUnit: the file's local length unit as a factor of metres,
//    1.0 when the file declares no unit); FileUnits (the unit names); SystemLengthUnit() ->
//    that same factor. There is no Status() method and no FileUnitsDeclared(): the
//    "declared" fact comes from the raw text, the model's from its local unit.
//  - XSControl_Reader adds Transfer / NbShapes / Shape / OneShape; TransferRoots() returns
//    the number of shapes transferred (not a status) and leaves them un-scaled, in the
//    file's local unit.
//  - The schema lives in the header's HeaderSection_FileSchema (no static GetID() in 7.9);
//    the identifiers are TCollection_HAsciiString handles, read with ToCString.
//  - The writer takes a value-type parameter (DESTEP_Parameters in 7.9; renamed from
//    StepData_ConfParameters in 7.8) whose WriteSchema / WriteUnit are public fields; the
//    AP242 option is WriteMode_StepSchema_AP242DIS and emits the
//    AP242_..._MIM_LF {1 0 10303 442 ...} schema (part 442, not 242).
//  - The version: 7.9 removed the OCC_VERSION_* macros; the runtime functions of
//    Standard_VersionInfo.hxx are the only source there and do not exist in 7.8. The build
//    selects the spelling (SKETCHYUP_IFC_OCCT_79) and records the configured version.
#include "ifc/step_probe.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <sstream>
#include <utility>

// The OpenCASCADE spelling (7.8 and 7.9+ differ in three places, all selected by what the
// installed headers provide, so the build needs no version flag): the version, the writer
// parameters and the AP242 write option.
//   - 7.9+ installs Standard_VersionInfo.hxx (runtime version functions) and not
//     Standard_Version.hxx; 7.8 is the other way round.
//   - The writer takes DESTEP_Parameters (7.9+, whose AP242 option is
//     WriteMode_StepSchema_AP242DIS) or, in 7.8, StepData_ConfParameters (no AP242 option).
//   - In 7.9.3 StepData_ConfParameters is a deprecated alias of DESTEP_Parameters.
#if __has_include("Standard_VersionInfo.hxx")
#include "Standard_VersionInfo.hxx"
#define SKETCHYUP_STEP_OCCT_79 1
#else
#include "Standard_Version.hxx"
#endif
#include "Bnd_Box.hxx"
#include "BRepBndLib.hxx"
#include "BRep_Builder.hxx"
#include "BRepPrimAPI_MakeBox.hxx"
#include "BRepPrimAPI_MakeCylinder.hxx"
#include "HeaderSection_FileSchema.hxx"
#include "Interface_EntityIterator.hxx"
#include "STEPControl_Reader.hxx"
#include "STEPControl_Writer.hxx"
#include "Standard_Failure.hxx"
#include "StepData_StepModel.hxx"
#include "TColStd_SequenceOfAsciiString.hxx"
#include "TopExp_Explorer.hxx"
#include "TopoDS_Compound.hxx"
#include "TopoDS_Shape.hxx"
#include "UnitsMethods.hxx"

#if defined(SKETCHYUP_STEP_OCCT_79)
#include "DESTEP_Parameters.hxx"
using StepWriterParams = DESTEP_Parameters;
#else
#include "StepData_ConfParameters.hxx"
using StepWriterParams = StepData_ConfParameters;
#endif

#include "ifc/json_out.hpp"
#include "sketchyup/version.hpp"

namespace sketchyup::ifc {
namespace {

namespace fs = std::filesystem;

long long fileBytes(const fs::path& path) {
  std::error_code error;
  const auto size = fs::file_size(path, error);
  return error ? 0 : static_cast<long long>(size);
}

std::optional<std::string> readFile(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  std::ostringstream buffer;
  buffer << in.rdbuf();
  std::string text = buffer.str();
  if (text.size() > 128ull * 1024 * 1024) return std::nullopt;  // the application's input bound
  return text;
}

// The raw-text header facts. std::regex was not used on purpose: on the R098.a build
// toolchain (GCC 16, C++20) it returned different results for byte-identical strings
// (matched for one copy of the file, refused another), so the header is scanned by hand.
// That is also what the ADR 0172 policy needs: the file read as written.

// The first quoted string at or after `from` (the FILE_SCHEMA name lives in the first quotes
// of the FILE_SCHEMA entry).
std::optional<std::string> firstQuoted(const std::string& text, std::size_t from) {
  const std::size_t open = text.find('\'', from);
  if (open == std::string::npos) return std::nullopt;
  const std::size_t close = text.find('\'', open + 1);
  if (close == std::string::npos || close - open > 256) return std::nullopt;
  return text.substr(open + 1, close - open - 1);
}

// A dot-delimited literal such as `.MILLI.` or `$` (the arguments of SI_UNIT):
// `.MILLI.` -> MILLI, `$` -> "".
std::string dotLiteral(const std::string& arg) {
  std::string value = arg;
  while (!value.empty() && value.front() == '.') value.erase(value.begin());
  while (!value.empty() && value.back() == '.') value.pop_back();
  return value;
}

// SI_UNIT( prefixed . base . ): the two arguments as dot literals.
bool siUnitArgs(const std::string& text, std::size_t from, std::string& prefix, std::string& base) {
  const std::size_t open = text.find("SI_UNIT(", from);
  if (open == std::string::npos) return false;
  std::size_t pos = open + 8;
  const std::size_t comma = text.find(',', pos);
  if (comma == std::string::npos || comma - pos > 64) return false;
  prefix = dotLiteral(text.substr(pos, comma - pos));
  const std::size_t close = text.find(')', comma + 1);
  if (close == std::string::npos || close - comma > 64) return false;
  base = dotLiteral(text.substr(comma + 1, close - comma - 1));
  return true;
}

// ap203 / ap214 / ap242 / other, from the FILE_SCHEMA name's ISO 10303 part number (AP242's
// identifiers use part 442, not 242).
std::string classifySchema(const std::string& raw) {
  const std::size_t at = raw.find("10303");
  if (at == std::string::npos) return "other";
  std::size_t pos = at + 5;
  while (pos < raw.size() && !std::isdigit(static_cast<unsigned char>(raw[pos]))) ++pos;
  const std::size_t digits = pos;
  while (pos < raw.size() && std::isdigit(static_cast<unsigned char>(raw[pos]))) ++pos;
  if (pos == digits || pos - digits > 5) return "other";
  const std::string part = raw.substr(digits, pos - digits);
  if (part == "203") return "ap203";
  if (part == "214") return "ap214";
  if (part == "442") return "ap242";
  return "other";
}

// The raw-text unit declaration (the unit policy, ADR 0172 decision 4, reads the file as
// written): an SI unit with an optional prefix, or a conversion-based named unit. Each unit
// type is looked for in a bounded window after its marker, so a unit's own entity is what is
// read, not some later entity that merely mentions the same word.
struct RawUnits {
  bool lengthDeclared = false;
  std::string length;
  std::string angle;
  std::string solidAngle;
  double lengthMetres = 1.0;
};

const std::size_t kWindow = 300;  // a STEP entity is well under this long

// The unit the marker points at: the SI_UNIT or CONVERSION_BASED_UNIT in the window, as
// (prefix, base-or-name); a bare NAMED_UNIT() has no unit to read.
std::optional<std::pair<std::string, std::string>> unitAfter(const std::string& text, std::size_t from) {
  if (from + kWindow > text.size()) return std::nullopt;
  const std::string window = text.substr(from, kWindow);
  std::size_t si = window.find("SI_UNIT(");
  std::size_t conv = window.find("CONVERSION_BASED_UNIT(");
  const std::size_t named = window.find("NAMED_UNIT");
  auto isOwnEntity = [&](std::size_t at) { return at != std::string::npos && (named == std::string::npos || at < named); };
  if (isOwnEntity(si)) {
    std::string prefix, base;
    if (siUnitArgs(window, si, prefix, base)) return {prefix, base};
  }
  if (isOwnEntity(conv)) {
    const std::size_t close = window.find(')', conv);
    if (close == std::string::npos) return std::nullopt;
    const std::pair<std::string, std::string> unit{"", dotLiteral(window.substr(conv + 20, close - conv - 20))};
    return unit;
  }
  return std::nullopt;
}

RawUnits rawUnits(const std::string& text) {
  RawUnits units;
  static const std::map<std::string, double> prefix = {
      {"MILLI", 1e-3}, {"CENTI", 1e-2}, {"DECI", 1e-1}, {"KILO", 1e3}, {"", 1.0}};
  static const std::map<std::string, double> named = {
      {"INCH", 0.0254}, {"FOOT", 0.3048}, {"MILLIMETRE", 1e-3}, {"CENTIMETRE", 1e-2}, {"METRE", 1.0}};
  if (const std::size_t at = text.find("LENGTH_UNIT()")) {
    if (const auto unit = unitAfter(text, at)) {
      units.lengthDeclared = true;
      if (unit->second == "METRE") {
        units.length = unit->first == "MILLI" ? "millimetre" : unit->first == "CENTI" ? "centimetre" : unit->first == "KILO" ? "kilometre" : unit->first.empty() ? "metre" : unit->first;
        const auto entry = prefix.find(unit->first);
        // 0.0: declared but not convertible from the raw text; the model's factor stands.
        units.lengthMetres = entry != prefix.end() ? entry->second : 0.0;
      } else {
        const auto base = named.find(unit->second);
        units.length = unit->second;
        units.lengthMetres = base != named.end() ? base->second : 0.0;
      }
    }
  }
  if (const std::size_t at = text.find("PLANE_ANGLE_UNIT()")) {
    if (const auto unit = unitAfter(text, at)) {
      if (unit->second == "RADIAN" || unit->second == "DEGREE") units.angle = unit->second;
    }
  }
  if (const std::size_t at = text.find("SOLID_ANGLE_UNIT()")) {
    if (const auto unit = unitAfter(text, at)) {
      if (unit->second == "STERADIAN") units.solidAngle = unit->second;
    }
  }
  return units;
}

StepShapeFacts shapeFacts(const TopoDS_Shape& shape) {
  StepShapeFacts facts;
  TopExp_Explorer solid(shape, TopAbs_SOLID);
  while (solid.More()) {
    facts.solids++;
    solid.Next();
  }
  TopExp_Explorer face(shape, TopAbs_FACE);
  while (face.More()) {
    facts.faces++;
    face.Next();
  }
  try {
    Bnd_Box box;
    BRepBndLib::Add(shape, box);
    if (!box.IsVoid()) {
      Standard_Real x1, y1, z1, x2, y2, z2;
      box.Get(x1, y1, z1, x2, y2, z2);
      facts.box = {static_cast<double>(x1), static_cast<double>(y1), static_cast<double>(z1),
                   static_cast<double>(x2), static_cast<double>(y2), static_cast<double>(z2)};
      facts.ok = true;
    }
  } catch (const Standard_Failure&) {
  }
  return facts;
}

}  // namespace

StepModelFacts readStepModel(const std::string& path) {
  StepModelFacts facts;
  // The "declared" fact: the reader has no API for it (no FileUnitsDeclared in 7.9.3), so
  // the raw text is asked: a LENGTH_UNIT entity is a declared unit, whatever the reader
  // made of it.
  const std::optional<std::string> text = readFile(path);
  facts.unitsDeclared = text.has_value() && text->find("LENGTH_UNIT(") != std::string::npos;
  STEPControl_Reader reader;
  try {
    const IFSelect_ReturnStatus status = reader.ReadFile(path.c_str());
    facts.readStatus = status == IFSelect_RetDone ? "done" : status == IFSelect_RetFail ? "fail" : "error";
    facts.read = status == IFSelect_RetDone;
  } catch (const Standard_Failure& failure) {
    facts.readStatus = "exception";
    facts.error = failure.GetMessageString();
    return facts;
  }
  if (!facts.read) return facts;
  try {
    const Handle(StepData_StepModel) model = Handle(StepData_StepModel)::DownCast(reader.Model());
    if (model.IsNull()) {
      facts.read = false;
      facts.readStatus = "no-model";
      return facts;
    }
    // The schema: the header's FILE_SCHEMA entity (7.9 has no static GetID; walk the header).
    Handle(HeaderSection_FileSchema) fileSchema;
    Interface_EntityIterator header = model->Header();
    while (header.More()) {
      fileSchema = Handle(HeaderSection_FileSchema)::DownCast(header.Value());
      if (!fileSchema.IsNull()) break;
      header.Next();
    }
    if (!fileSchema.IsNull()) {
      for (int i = 1; i <= fileSchema->NbSchemaIdentifiers(); ++i) {
        if (i > 1) facts.schema += " ";
        facts.schema += fileSchema->SchemaIdentifiersValue(i)->ToCString();
      }
    }
    TColStd_SequenceOfAsciiString length, angle, solid;
    reader.FileUnits(length, angle, solid);
    if (length.Length() > 0) facts.lengthUnit = length.Value(1).ToCString();
    if (angle.Length() > 0) facts.angleUnit = angle.Value(1).ToCString();
    if (solid.Length() > 0) facts.solidAngleUnit = solid.Value(1).ToCString();
    // The model's unit, un-touched: LocalLengthUnit as a factor of metres (SetSystemLengthUnit
    // would overwrite it, and the shapes are read in the file's own unit).
    facts.lengthMetres = reader.SystemLengthUnit();
    facts.entities = reader.Model()->NbEntities();
    facts.roots = reader.NbRootsForTransfer();
    facts.transferred = reader.TransferRoots();
    for (int i = 1; i <= reader.NbShapes(); ++i) {
      StepShapeFacts shape = shapeFacts(reader.Shape(i));
      shape.index = i;
      facts.shapes.push_back(std::move(shape));
    }
  } catch (const Standard_Failure& failure) {
    facts.error = failure.GetMessageString();
  }
  return facts;
}

StepFileFacts stepFileFacts(const std::string& path) {
  StepFileFacts facts;
  facts.bytes = fileBytes(path);
  const std::optional<std::string> text = readFile(path);
  if (!text) return facts;
  facts.readable = true;
  facts.isoHeader = text->rfind("ISO-10303-21", 0) == 0;
  const std::size_t schemaAt = text->find("FILE_SCHEMA(");
  if (const auto name = schemaAt == std::string::npos ? std::nullopt : firstQuoted(*text, schemaAt)) {
    facts.schemaRaw = *name;
  }
  facts.schema = classifySchema(facts.schemaRaw);
  const RawUnits raw = rawUnits(*text);
  facts.lengthDeclared = raw.lengthDeclared;
  facts.length = raw.length;
  facts.angle = raw.angle;
  facts.solidAngle = raw.solidAngle;
  facts.lengthMetres = raw.lengthMetres;
  return facts;
}

namespace {

// The round trip: a 10 x 20 x 30 box through the basic writer and back. The file's
// FILE_SCHEMA is what the writer really wrote, whatever the schema option says.
void writeRoundTrip(const std::string& directory, const std::string& label, const StepWriterParams& parameters, JsonOut& json) {
  const fs::path file = fs::path(directory) / ("sketchyup-probe-roundtrip-" + label + ".step");
  json.field("file", file.string());
  bool written = false;
  {
    try {
      STEPControl_Writer writer;
      if (writer.Transfer(BRepPrimAPI_MakeBox(10, 20, 30).Shape(), STEPControl_AsIs, parameters) != IFSelect_RetDone ||
          writer.Write(file.c_str()) != IFSelect_RetDone) {
        json.field("written", false).field("reason", "the writer refused the box");
        return;
      }
      written = true;
    } catch (const Standard_Failure& failure) {
      json.field("written", false).field("reason", failure.GetMessageString());
      return;
    }
  }
  json.field("written", written);
  if (!written) return;
  json.field("bytes", fileBytes(file));
  const StepFileFacts raw = stepFileFacts(file);
  json.field("fileSchemaRaw", raw.schemaRaw);
  json.field("schema", raw.schema);
  json.key("units").beginObject()
      .field("declared", raw.lengthDeclared)
      .field("length", raw.length)
      .field("lengthMetres", raw.lengthDeclared ? raw.lengthMetres : 0.0)
      .endObject();
  const StepModelFacts back = readStepModel(file);
  json.field("readStatus", back.readStatus);
  json.field("modelSchema", back.schema);
  json.field("entities", back.entities);
  json.field("shapes", static_cast<long long>(back.shapes.size()));
  if (!back.shapes.empty() && back.shapes.front().ok) json.key("boundingBox").doubles(back.shapes.front().box);
}

}  // namespace

std::string probeStep(const StepProbeOptions& options) {
  JsonOut json;
  json.beginObject();
  json.key("worker").beginObject()
      .field("name", "sketchyup-ifc-worker")
      .field("version", SKETCHYUP_VERSION)
      .field("stepProbeVersion", 1)
      .endObject();
  // The runtime version (the loaded library) and the version the build was configured against
  // (the headers): a mismatch is a build problem the test should see.
#if defined(SKETCHYUP_STEP_OCCT_79)
  const std::string version = OCCT_Version_String_Complete();
#else
  const std::string version = std::to_string(OCC_VERSION_MAJOR) + "." + std::to_string(OCC_VERSION_MINOR) + "." +
                              std::to_string(OCC_VERSION_MAINTENANCE);
#endif
  json.key("occt").beginObject()
      .field("version", version)
      .field("versionConfigured", SKETCHYUP_IFC_OCCT_VERSION_STRING)
      .endObject();
  const StepFileFacts file = stepFileFacts(options.file);
  json.key("file").beginObject()
      .field("path", options.file)
      .field("bytes", file.bytes)
      .field("isoHeader", file.isoHeader)
      .field("fileSchemaRaw", file.schemaRaw)
      .field("schema", file.schema)
      .endObject();
  if (file.readable) {
    json.key("units").beginObject()
        .field("declared", file.lengthDeclared)
        .field("length", file.length)
        .field("angle", file.angle)
        .field("solidAngle", file.solidAngle)
        .field("lengthMetres", file.lengthDeclared ? file.lengthMetres : 0.0)
        .endObject();
  }
  const StepModelFacts model = readStepModel(options.file);
  json.field("modelSchema", model.schema);
  json.field("modelRead", model.read);
  json.field("modelReadStatus", model.readStatus);
  if (!model.error.empty()) json.field("modelError", model.error);
  if (model.read) {
    json.field("modelUnitsDeclared", model.unitsDeclared);
    json.field("modelLengthUnit", model.lengthUnit);
    json.field("modelLengthMetres", model.lengthMetres);
    json.field("entities", model.entities);
    json.field("roots", model.roots);
    json.field("transferred", model.transferred);
    json.key("shapes").beginArray();
    for (const StepShapeFacts& shape : model.shapes) {
      json.beginObject()
          .field("index", shape.index)
          .field("solids", shape.solids)
          .field("faces", shape.faces);
      if (shape.ok) json.key("boundingBox").doubles(shape.box);
      json.endObject();
    }
    json.endArray();
  }
  if (!options.roundTripDirectory.empty()) {
    json.key("writeRoundTrip").beginObject();
    // The ADR 0172 §2 default (AP214, DIS schema version) and the AP242 option, which the
    // OCCT 7.9 user guide does not document as writer output: the file's FILE_SCHEMA is
    // what the writer really wrote.
    {
      StepWriterParams ap214Params;
      ap214Params.WriteSchema = StepWriterParams::WriteMode_StepSchema_AP214DIS;  // the ADR 0172 §2 default
      ap214Params.WriteUnit = UnitsMethods_LengthUnit_Millimeter;  // explicit: the document's unit
      json.key("ap214").beginObject();
      writeRoundTrip(options.roundTripDirectory, "ap214", ap214Params, json);
      json.endObject();
    }
#if defined(SKETCHYUP_STEP_OCCT_79)
    {
      StepWriterParams ap242Params;
      ap242Params.WriteSchema = StepWriterParams::WriteMode_StepSchema_AP242DIS;
      ap242Params.WriteUnit = UnitsMethods_LengthUnit_Millimeter;
      json.key("ap242").beginObject();
      writeRoundTrip(options.roundTripDirectory, "ap242", ap242Params, json);
      json.endObject();
    }
#endif
    json.endObject();
  }
  json.endObject();
  return json.str();
}

int runStepProbe(const StepProbeOptions& options) {
  std::cout << probeStep(options) << std::endl;
  // 0 when the file was read and at least one shape transferred, 1 otherwise: the only
  // success the import can build on.
  const StepModelFacts model = readStepModel(options.file);
  return model.read && model.transferred > 0 ? 0 : 1;
}

std::string writeStep(const std::string& directory) {
  JsonOut json;
  json.beginArray();
  // The ADR 0172 §7 self-generated corpus: box 10 x 20 x 30, cylinder r 5 h 20, and the
  // two together, all in millimetres through the basic writer. The corpus is written with
  // the same OCCT writer the import reads, with the ADR 0172 §2 default schema (AP214, DIS
  // schema version) and an explicit millimetre document unit.
  auto writeOne = [&](const char* name, const std::function<TopoDS_Shape()>& make) {
    const fs::path file = fs::path(directory) / name;
    json.beginObject().field("file", std::string(name));
    try {
      STEPControl_Writer writer;
      StepWriterParams params;
      params.WriteSchema = StepWriterParams::WriteMode_StepSchema_AP214DIS;
      params.WriteUnit = UnitsMethods_LengthUnit_Millimeter;  // explicit: the document's unit
      if (writer.Transfer(make(), STEPControl_AsIs, params) == IFSelect_RetDone &&
          writer.Write(file.c_str()) == IFSelect_RetDone) {
        json.field("written", true);
        const StepFileFacts written = stepFileFacts(file);
        json.field("bytes", written.bytes);
        json.field("schema", written.schema);
      } else {
        json.field("written", false).field("reason", "the writer refused the shape");
      }
    } catch (const Standard_Failure& failure) {
      json.field("written", false).field("reason", failure.GetMessageString());
    }
    json.endObject();
  };
  writeOne("sketchyup-step-box.step", [] { return BRepPrimAPI_MakeBox(10, 20, 30).Shape(); });
  writeOne("sketchyup-step-cylinder.step", [] { return BRepPrimAPI_MakeCylinder(5, 20).Shape(); });
  writeOne("sketchyup-step-assembly.step", [] {
    TopoDS_Compound assembly;
    BRep_Builder builder;
    builder.MakeCompound(assembly);
    builder.Add(assembly, BRepPrimAPI_MakeBox(10, 20, 30).Shape());
    builder.Add(assembly, BRepPrimAPI_MakeCylinder(5, 20).Shape());
    return assembly;
  });
  json.endArray();
  return json.str();
}

}  // namespace sketchyup::ifc
