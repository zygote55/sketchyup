// The STEP import skeleton (ADR 0172, R098.a): the file's metadata through the basic OCCT
// reader and the unit policy of §2. The tessellated import builds on this; nothing is
// silently rescaled and a unit is never guessed.
#include "ifc/step_import.hpp"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "ifc/json_out.hpp"
#include "ifc/step_probe.hpp"

namespace sketchyup::ifc {
namespace {

const long long kMaxInputBytes = 128ll * 1024 * 1024;  // the application's input bound

// ADR 0172 §2: the --step-length-unit option values as a factor of metres.
bool optionFactor(const std::string& unit, double& metres) {
  if (unit == "mm") {
    metres = 0.001;
    return true;
  }
  if (unit == "cm") {
    metres = 0.01;
    return true;
  }
  if (unit == "m") {
    metres = 1.0;
    return true;
  }
  if (unit == "in") {
    metres = 0.0254;
    return true;
  }
  if (unit == "ft") {
    metres = 0.3048;
    return true;
  }
  return false;
}

// The two factors disagree (the raw text is what the file says; the model's is what the
// reader made of it).
bool factorMismatch(const double raw, const double model) {
  return std::abs(raw - model) > 1e-9 * std::max(1.0, std::abs(model));
}

}  // namespace

std::string importStep(const StepImportOptions& options) {
  JsonOut json;
  json.beginObject();
  json.field("apiVersion", 1);
  const StepFileFacts file = stepFileFacts(options.file);
  json.key("file").beginObject()
      .field("path", options.file)
      .field("bytes", file.bytes)
      .endObject();
  json.field("schemaRaw", file.schemaRaw);
  json.field("schema", file.schema);
  std::vector<std::string> notices;
  std::string status;
  double mmPerUnit = 0.0;  // 0: no conversion (refused or failed)
  std::string unitsApplied;  // "declared" | "option"
  bool modelRead = false;
  StepModelFacts model;

  if (file.bytes > kMaxInputBytes) {
    status = "refused-size";
    notices.push_back("the file is over 128 MiB; it is refused (ADR 0172 §6)");
  } else if (!file.readable) {
    status = "failed-read";
    notices.push_back("the file could not be read");
  } else if (!file.isoHeader) {
    status = "refused-header";
    notices.push_back("the file does not start with an ISO-10303-21 header; it is refused");
  } else if (file.schema == "other") {
    status = "refused-schema";
    notices.push_back("the FILE_SCHEMA is not AP203, AP214 or AP242; it is refused");
  } else {
    // The unit the import applies (ADR 0172 decision 4: the file's own unit is authoritative,
    // whatever the reader made of it): the unit the file's unit context applies - for a
    // conversion-based file the conversion unit (the inch), not the base unit the raw text's
    // first LENGTH_UNIT declares (the conversion's millimetre base); the raw text's first
    // declaration stands only for a file the model resolves no unit for (no shape or unit
    // context the reader follows), and is then flagged.
    model = readStepModel(options.file);
    modelRead = model.read;
    if (file.lengthDeclared) {
      unitsApplied = "declared";
      if (model.lengthUnitResolved) {
        // The reader applied the file's unit: TransferRoots normalized the geometry to the
        // cascade unit (millimetre by default), so no further scale.
        mmPerUnit = 1.0;
        if (model.lengthMetres > 0.0) {
          if (file.lengthMetres > 0.0 && factorMismatch(file.lengthMetres, model.lengthMetres)) {
            notices.push_back("the file applies a conversion-based unit: the raw text's first "
                              "declaration is 1 unit = " + std::to_string(file.lengthMetres) + " m, the "
                              "unit the file's context applies (" + model.lengthUnit + ") is 1 unit = " +
                              std::to_string(model.lengthMetres) + " m; the geometry comes out in mm");
          }
        } else {
          notices.push_back("the file's length unit (" + model.lengthUnit + ") could not be "
                            "converted from the name; the reader's conversion stands (the geometry "
                            "is in mm)");
        }
      } else if (file.lengthMetres > 0.0) {
        // The reader resolved no unit: the geometry is in the file's units; the raw text's
        // first declaration converts it, flagged.
        mmPerUnit = 1000.0 * file.lengthMetres;
        notices.push_back("the reader resolved no length unit for the file; the raw text's "
                          "declaration (" + file.length + ", 1 unit = " + std::to_string(file.lengthMetres) +
                          " m) is applied to the geometry");
      } else if (model.read) {
        // The raw text cannot convert the declaration and the model resolved no unit:
        // a unit is never guessed. (A failed model read is not a refusal: the failed-read
        // status below says why.)
        status = "refused-units";
        notices.push_back("the file's length unit cannot be converted; pass --step-length-unit "
                          "to import it as a named unit");
      }
    } else if (!options.lengthUnitOverride.empty()) {
      double factor = 0.0;
      if (!optionFactor(options.lengthUnitOverride, factor)) {
        status = "refused-option";
        notices.push_back("--step-length-unit must be one of mm, cm, m, in or ft");
      } else {
        unitsApplied = "option";
        mmPerUnit = 1000.0 * factor;
        notices.push_back("the file declares no length unit; the option " + options.lengthUnitOverride +
                          " (1 unit = " + std::to_string(factor) + " m) is used");
      }
    } else {
      status = "refused-units";
      notices.push_back("the file declares no length unit and no --step-length-unit was given; it is "
                        "refused (use --step-length-unit mm|cm|m|in|ft; a unit is never guessed)");
    }
  }

  if (!modelRead) model = readStepModel(options.file);
  json.field("readStatus", model.readStatus);
  if (!model.error.empty()) json.field("error", model.error);
  json.field("modelSchema", model.schema);
  json.field("modelLengthUnit", model.lengthUnit);
  json.field("modelLengthResolved", model.lengthUnitResolved);
  json.field("modelLengthMetres", model.lengthMetres);
  json.key("units").beginObject()
      .field("declared", file.lengthDeclared)
      .field("length", file.length)
      .field("angle", file.angle)
      .field("solidAngle", file.solidAngle)
      .field("rawLengthMetres", file.lengthDeclared ? file.lengthMetres : 0.0)
      .field("modelLengthMetres", model.lengthMetres)
      .field("applied", unitsApplied)
      .field("mmPerUnit", mmPerUnit)
      .endObject();
  if (status.empty()) {
    if (!model.read) {
      status = "failed-read";
      notices.push_back("the STEP reader failed to read the file");
    } else {
      json.field("entities", model.entities);
      json.field("roots", model.roots);
      json.field("transferred", model.transferred);
      if (model.transferred == 0) {
        status = "no-shapes";
        notices.push_back("the file read, but no shape could be transferred");
      } else {
        status = "ok";
        json.key("shapes").beginArray();
        for (const StepShapeFacts& shape : model.shapes) {
          json.beginObject()
              .field("index", shape.index)
              .field("solids", shape.solids)
              .field("faces", shape.faces);
          if (shape.ok) {
            std::vector<double> boxMm;
            boxMm.reserve(shape.box.size());
            for (const double v : shape.box) boxMm.push_back(v * mmPerUnit);
            json.key("boundingBoxMm").doubles(boxMm);
          }
          json.endObject();
        }
        json.endArray();
      }
    }
  }
  json.field("status", status);
  json.key("notices").strings(notices);
  json.endObject();
  return json.str();
}

int runStepImport(const StepImportOptions& options) {
  const std::string report = importStep(options);
  std::cout << report << std::endl;
  // The status is carried in the report; the exit code is a compact version of it: 0 when
  // the file was read (with or without transferred shapes), 2 when it was refused, 1 when
  // the read itself failed. Nothing is created any way, and the code says why.
  size_t begin = report.rfind("\"status\"");
  if (begin == std::string::npos) return 1;
  begin = report.find('"', begin + 8);
  const size_t end = report.find('"', begin + 1);
  if (begin == std::string::npos || end == std::string::npos) return 1;
  const std::string status = report.substr(begin + 1, end - begin - 1);
  if (status == "ok" || status == "no-shapes") return 0;
  if (status.rfind("refused", 0) == 0) return 2;
  return 1;
}

}  // namespace sketchyup::ifc
