// --probe-write: builds a small IFC4 file with the typed writer API (hierarchy_helper<Ifc4>),
// writes it with operator<<, and reads it back through the same iterator path as --probe-api.
#include <fstream>
#include <sstream>

#include "ifc/json_out.hpp"
#include "ifc/probe.hpp"
#include "ifcparse/macros.h"

#define IfcSchema Ifc4
#include INCLUDE_SCHEMA(ifcparse/schemas, IfcSchema)
#include INCLUDE_SCHEMA_DEFINITIONS(ifcparse/schemas, IfcSchema)

#include "ifcparse/hierarchy_helper.h"
#include "ifcparse/parse.h"

namespace sketchyup::ifc {

std::string probeWriter(const std::string& path) {
  JsonOut json;
  json.beginObject();
  json.field("path", path);
  try {
    hierarchy_helper<Ifc4> file;
    file.header().file_name().setname("probe.ifc");

    // A wall 4000 x 300 x 2500 and an opening 1000 x 300 x 1200 through it, in millimetres.
    auto wall = file.create<Ifc4::IfcWall>();
    wall.setGlobalId(ifcopenshell::global_id());
    wall.setName("probe wall");
    file.addBuildingProduct(wall);
    auto storeyPlacement = file.getSingle<Ifc4::IfcBuildingStorey>().ObjectPlacement();
    wall.setObjectPlacement(file.addLocalPlacement(storeyPlacement));
    wall.setRepresentation(file.addBox(4000, 300, 2500));

    auto opening = file.create<Ifc4::IfcOpeningElement>();
    opening.setGlobalId(ifcopenshell::global_id());
    opening.setName("probe opening");
    opening.setObjectPlacement(file.addLocalPlacement(wall.ObjectPlacement(), 0, 0, 400));
    opening.setRepresentation(file.addBox(1000, 300, 1200));
    auto voids = file.create<Ifc4::IfcRelVoidsElement>();
    voids.setGlobalId(ifcopenshell::global_id());
    voids.setRelatingBuildingElement(wall);
    voids.setRelatedOpeningElement(opening);

    {
      std::ofstream out(path, std::ios::binary);
      out << file;
    }
    json.field("schema", file.schema()->name());

    std::ifstream in(path, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    const std::string written = text.str();
    json.field("bytes", written.size());
    json.field("startsWithStepHeader", written.rfind("ISO-10303-21;", 0) == 0);
    json.field("endsWithStepFooter", written.find("END-ISO-10303-21;") != std::string::npos);
    json.field("fileSchemaDeclared", written.find("FILE_SCHEMA(('IFC4'))") != std::string::npos);
    std::size_t lines = 0;
    for (std::size_t at = 0; (at = written.find("\n#", at)) != std::string::npos; ++at) ++lines;
    json.field("instanceLines", lines);

    json.key("reread").raw(probeGeometry({.file = path,
                                          .kernel = "opencascade",
                                          .threads = 1,
                                          .settings = {},
                                          .excludeSteps = {}}));
  } catch (const std::exception& problem) {
    json.unwindTo(1).field("exception", problem.what());
  } catch (...) {
    json.unwindTo(1).field("exception", "unknown exception");
  }
  json.endObject();
  return json.str();
}

}  // namespace sketchyup::ifc
