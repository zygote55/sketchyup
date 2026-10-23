// The STEP probe (ADR 0172, R098.a). Reports what the OpenCASCADE 7.9 STEP stack really does
// with a file: header, FILE_SCHEMA (raw text and the model's), units (raw text and the
// model's), entities, root shapes, bounding boxes, and a write-read round trip through the
// basic writer, which settles on every run what schema the writer actually writes.
//
// The OpenCASCADE 7.9 API (verified against the V7_9_3 source):
//  - STEPControl_Reader: ReadFile -> IFSelect_RetDone/RetFail; Model() -> StepData_StepModel
//    (LocalLengthUnit); FileUnits(len, ang, solid); SetSystemLengthUnit; SystemLengthUnit;
//    TransferRoots() returns the number of shapes transferred (7.9; not a status), then
//    NbShapes() / Shape(i).
//  - The schema lives in the header's HeaderSection_FileSchema (no static GetID() in 7.9);
//    the identifiers are TCollection_HAsciiString, read with ToCString.
//  - The basic writer (STEPControl_Writer + DESTEP_Parameters, whose WriteSchema / WriteUnit
//    are public fields in 7.9) writes AP214 in every schema mode, AP242 included.
#include "ifc/step_probe.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

#include "Standard_Version.hxx"
#include "Standard_VersionInfo.hxx"
#include "BRepPrimAPI_MakeBox.hxx"
#include "BRepBndLib.hxx"
#include "Bnd_Box.hxx"
#include "DESTEP_Parameters.hxx"
#include "HeaderSection_FileSchema.hxx"
#include "Interface_EntityIterator.hxx"
#include "STEPControl_Reader.hxx"
#include "STEPControl_Writer.hxx"
#include "Standard_Fail.hxx"
#include "StepData_StepModel.hxx"
#include "TCollection_HAsciiString.hxx"
#include "TColStd_SequenceOfAsciiString.hxx"
#include "TopExp_Explorer.hxx"
#include "TopoDS_Shape.hxx"
#include "UnitsMethods.hxx"

#include "ifc/json_out.hpp"
#include "sketchyup/version.hpp"

namespace sketchyup::ifc {
namespace {

namespace fs = std::filesystem;

std::string toAscii(const TCollection_HAsciiString& s) { return s.ToCString(); }

std::size_t fileBytes(const fs::path& path) {
  std::error_code error;
  const auto size = fs::file_size(path, error);
  return error ? 0 : size;
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

// ap203 / ap214 / ap242 / other, from the FILE_SCHEMA name (the ISO 10303-xxx part number).
std::string classifySchema(const std::string& raw) {
  if (raw.find("10303 242") != std::string::npos) return "ap242";
  if (raw.find("10303 214") != std::string::npos) return "ap214";
  if (raw.find("10303 203") != std::string::npos) return "ap203";
  return "other";
}

// The raw-text unit declaration (the unit policy, ADR 0172 decision 4, reads the file as
// written): an SI unit with an optional prefix, or a conversion-based named unit.
struct RawUnits {
  bool lengthDeclared = false;
  std::string length;
  std::string angle;
  std::string solidAngle;
  double lengthMetres = 1.0;
};

RawUnits rawUnits(const std::string& text) {
  RawUnits units;
  static const std::map<std::string, double> prefix = {
      {"MILLI", 1e-3}, {"CENTI", 1e-2}, {"DECI", 1e-1}, {"KILO", 1e3}, {"", 1.0}};
  static const std::map<std::string, double> named = {
      {"INCH", 0.0254}, {"FOOT", 0.3048}, {"MILLIMETRE", 1e-3}, {"CENTIMETRE", 1e-2}, {"METRE", 1.0}};
  {
    static const std::regex re("LENGTH_UNIT\\(\\)\\s+NAMED_UNIT\\(\\*?\\)\\s+SI_UNIT\\(\\.(\\w+)\\.,\\.(METRE)\\.(\\w+)\\)");
    std::smatch m;
    if (std::regex_search(text, m, re)) {
      units.lengthDeclared = true;
      units.length = m[1] == "MILLI" ? "millimetre" : m[1] == "CENTI" ? "centimetre" : m[1] == "KILO" ? "kilometre" : "metre";
      units.lengthMetres = prefix.count(m[1].str()) ? prefix.at(m[1].str()) : 1.0;
    }
  }
  {
    static const std::regex re("LENGTH_UNIT\\(\\)\\s+NAMED_UNIT\\(\\*?\\)\\s+SI_UNIT\\(\\$,(INCH|FOOT|MILLIMETRE|CENTIMETRE|METRE)\\.(\\w+)\\)");
    std::smatch m;
    if (std::regex_search(text, m, re)) {
      units.lengthDeclared = true;
      units.length = m[1].str();
      units.lengthMetres = named.count(m[1].str()) ? named.at(m[1].str()) : 1.0;
    }
  }
  {
    static const std::regex re("PLANE_ANGLE_UNIT\\(\\)\\s+SI_UNIT\\(\\$,(RADIAN|DEGREE)\\.(\\w+)\\)");
    std::smatch m;
    if (std::regex_search(text, m, re)) units.angle = m[1].str();
  }
  {
    static const std::regex re("SOLID_ANGLE_UNIT\\(\\)\\s+SI_UNIT\\(\\$,(STERADIAN)\\.(\\w+)\\)");
    std::smatch m;
    if (std::regex_search(text, m, re)) units.solidAngle = m[1].str();
  }
  return units;
}

struct ShapeFacts {
  int index = 0;
  long long solids = 0;
  long long faces = 0;
  std::vector<double> box;  // x1 y1 z1 x2 y2 z2
  bool ok = false;
};

ShapeFacts shapeFacts(const TopoDS_Shape& shape) {
  ShapeFacts facts;
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
  } catch (const Standard_Fail& failure) {
    facts.box = {0, 0, 0, 0, 0, 0};
  }
  return facts;
}

// What the model says about the file (the import's source of truth).
struct ModelFacts {
  bool read = false;
  std::string readStatus;
  std::string schema;  // the model's FILE_SCHEMA
  bool unitsDeclared = false;
  std::string lengthUnit;
  std::string angleUnit;
  std::string solidAngleUnit;
  double lengthMetres = 1.0;
  long long entities = 0;
  long long roots = 0;
  long long transferred = 0;
  std::vector<ShapeFacts> shapes;
  std::string error;
};

ModelFacts readModel(const fs::path& path) {
  ModelFacts facts;
  STEPControl_Reader reader;
  try {
    const IFSelect_ReturnStatus status = reader.ReadFile(path.c_str());
    facts.readStatus = status == IFSelect_RetDone ? "done" : status == IFSelect_RetFail ? "fail" : "error";
    facts.read = status == IFSelect_RetDone;
  } catch (const Standard_Fail& failure) {
    facts.readStatus = "exception";
    facts.error = failure.GetMessageString();
    return facts;
  }
  if (!facts.read) return facts;
  try {
    auto model = Handle(StepData_StepModel)::DownCast(reader.Model());
    if (model.IsNull()) {
      facts.read = false;
      facts.readStatus = "no model";
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
        facts.schema += toAscii(*fileSchema->SchemaIdentifiersValue(i));
      }
    }
    facts.unitsDeclared = reader.FileUnitsDeclared();
    TColStd_SequenceOfAsciiString length, angle, solid;
    reader.FileUnits(length, angle, solid);
    if (length.Length() > 0) facts.lengthUnit = toAscii(length.Value(1));
    if (angle.Length() > 0) facts.angleUnit = toAscii(angle.Value(1));
    if (solid.Length() > 0) facts.solidAngleUnit = toAscii(solid.Value(1));
    reader.SetSystemLengthUnit(1e-3);  // the system unit is a millimetre
    facts.lengthMetres = 1e-3 * reader.SystemLengthUnit();
    facts.entities = reader.Model()->NbEntities();
    facts.roots = reader.NbRootsForTransfer();
    facts.transferred = reader.TransferRoots();
    for (int i = 1; i <= reader.NbShapes(); ++i) {
      ShapeFacts shape = shapeFacts(reader.Shape(i));
      shape.index = i;
      facts.shapes.push_back(std::move(shape));
    }
  } catch (const Standard_Fail& failure) {
    facts.error = failure.GetMessageString();
  }
  return facts;
}

// The round trip: a 10 x 20 x 30 box through the basic writer and back. The file's
// FILE_SCHEMA is what the writer really wrote, whatever the WriteSchema option says.
void writeRoundTrip(const std::string& directory, JsonOut& json) {
  json.key("writeRoundTrip").beginObject();
  const fs::path file = fs::path(directory) / "sketchyup-probe-roundtrip.step";
  json.field("file", file.string());
  bool written = false;
  {
    try {
      STEPControl_Writer writer;
      DESTEP_Parameters parameters;
      parameters.WriteUnit = UnitsMethods_LengthUnit_Millimeter;  // explicit: the document's unit
      if (writer.Transfer(BRepPrimAPI_MakeBox(10, 20, 30).Shape(), STEPControl_AsIs, &parameters) != IFSelect_RetDone ||
          writer.Write(file.c_str()) != IFSelect_RetDone) {
        json.field("written", false).field("reason", "the writer refused the box");
        json.endObject();
        return;
      }
      written = true;
    } catch (const Standard_Fail& failure) {
      json.field("written", false).field("reason", failure.GetMessageString());
      json.endObject();
      return;
    }
  }
  json.field("written", written);
  json.field("bytes", fileBytes(file));
  const std::optional<std::string> text = readFile(file);
  std::string schemaRaw;
  static const std::regex schemaRe("FILE_SCHEMA\\(\\('([^']*)'\\)\\)");
  if (text) {
    std::smatch m;
    if (std::regex_search(*text, m, schemaRe)) schemaRaw = m[1].str();
  }
  json.field("fileSchemaRaw", schemaRaw);
  json.field("schema", classifySchema(schemaRaw));
  if (text) {
    const RawUnits raw = rawUnits(*text);
    json.key("units").beginObject()
        .field("declared", raw.lengthDeclared)
        .field("length", raw.length)
        .field("lengthMetres", raw.lengthDeclared ? raw.lengthMetres : 0.0)
        .endObject();
  }
  const ModelFacts back = readModel(file);
  json.field("readStatus", back.readStatus);
  json.field("entities", back.entities);
  json.field("shapes", static_cast<long long>(back.shapes.size()));
  if (!back.shapes.empty()) json.doubles("boundingBox", back.shapes.front().box);
  json.endObject();
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
  // The runtime version (the loaded library) and the compile-time macros (the headers): a
  // mismatch is a build problem the test should see.
  std::string macros = std::to_string(OCC_VERSION_MAJOR) + "." + std::to_string(OCC_VERSION_MINOR) + "." +
                       std::to_string(OCC_VERSION_MAINTENANCE);
  json.key("occt").beginObject()
      .field("version", OCCT_Version_String_Complete())
      .field("versionMacros", macros)
      .endObject();
  json.key("file").beginObject();
  json.field("path", options.file);
  json.field("bytes", fileBytes(options.file));
  std::optional<std::string> text = readFile(options.file);
  json.field("isoHeader", text && text->rfind("ISO-10303-21", 0) == 0);
  std::string schemaRaw;
  static const std::regex schemaRe("FILE_SCHEMA\\(\\('([^']*)'\\)\\)");
  if (text) {
    std::smatch m;
    if (std::regex_search(*text, m, schemaRe)) schemaRaw = m[1].str();
  }
  json.field("fileSchemaRaw", schemaRaw);
  json.field("schema", classifySchema(schemaRaw));
  if (text) {
    const RawUnits raw = rawUnits(*text);
    json.key("units").beginObject()
        .field("declared", raw.lengthDeclared)
        .field("length", raw.length)
        .field("angle", raw.angle)
        .field("solidAngle", raw.solidAngle)
        .field("lengthMetres", raw.lengthDeclared ? raw.lengthMetres : 0.0)
        .endObject();
  }
  const ModelFacts model = readModel(options.file);
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
    for (const ShapeFacts& shape : model.shapes) {
      json.beginObject()
          .field("index", shape.index)
          .field("solids", shape.solids)
          .field("faces", shape.faces);
      if (shape.ok) json.doubles("boundingBox", shape.box);
      json.endObject();
    }
    json.endArray();
  }
  if (!options.roundTripDirectory.empty()) writeRoundTrip(options.roundTripDirectory, json);
  json.endObject();
  return json.str();
}

int runStepProbe(const StepProbeOptions& options) {
  std::cout << probeStep(options) << std::endl;
  // 0 when the file was read and at least one shape transferred, 1 otherwise.
  std::string output;
  {
    std::istringstream probe(probeStep(options));
    std::getline(probe, output);
  }
  // Re-derive the exit code without parsing the JSON: a file that read and transferred is
  // the only success the import can build on.
  ModelFacts model = readModel(options.file);
  return model.read && model.transferred > 0 ? 0 : 1;
}

}  // namespace sketchyup::ifc
