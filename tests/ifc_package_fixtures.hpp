#pragma once
// R097.b: hand-built IFC conversion packages (src/io/ifc_package.hpp) for the decoder tests, the
// conversion tests and the fuzz seeds. No worker is involved: these are the package contract.
#include "io/ifc_package.hpp"
#include <QJsonArray>
namespace ifc_fixture {
using namespace sketchy;
// A unit cube [0,1]^3 in metres: 8 vertices, 12 triangles, outward winding, two per face.
inline IfcPackageMesh cubeMesh(double size = 1, std::uint32_t material = IfcPackageMesh::noMaterial) {
    IfcPackageMesh mesh;
    for (int i = 0; i < 8; ++i)
        mesh.vertices.push_back({(i & 1) ? size : 0, (i & 2) ? size : 0, (i & 4) ? size : 0});
    // Faces as quads of vertex indices counter-clockwise seen from outside.
    const std::array<std::array<std::uint32_t, 4>, 6> quads{{{0, 2, 3, 1}, {4, 5, 7, 6}, {0, 1, 5, 4},
                                                              {2, 6, 7, 3}, {0, 4, 6, 2}, {1, 3, 7, 5}}};
    for (const auto &q : quads) {
        mesh.triangles.push_back({q[0], q[1], q[2]});
        mesh.triangles.push_back({q[0], q[2], q[3]});
    }
    mesh.materials.assign(mesh.triangles.size(), material);
    return mesh;
}
inline IfcPackageNode node(std::uint32_t step, const char *className, const char *name, std::int64_t parent,
                           IfcNodeRole role, std::int64_t mesh = -1) {
    IfcPackageNode n;
    n.step = step;
    n.className = className;
    n.guid = "0000000000000000000" + std::to_string(100 + step);
    n.name = name;
    n.parent = parent;
    n.role = role;
    n.mesh = mesh;
    n.provenance = {{"ifc.Class", std::string(className)}, {"ifc.Step", double(step)}};
    return n;
}
inline std::array<double, 16> translation(double x, double y, double z) {
    return {1, 0, 0, x, 0, 1, 0, y, 0, 0, 1, z, 0, 0, 0, 1};
}
// project > building > storey > { two walls sharing one mesh, a hidden space }.
inline IfcPackage samplePackage() {
    IfcPackage p;
    p.engine = QJsonObject{{"library", "IfcOpenShell"}, {"version", "0.9.0"}, {"kernel", "opencascade"},
                           {"linearDeflectionMetres", 0.001}, {"angularDeflectionRadians", 0.5},
                           {"completed", true}};
    p.source = QJsonObject{{"schema", "IFC4"}, {"entities", 40}, {"bytes", 4096}};
    p.units = QJsonObject{{"lengthUnit", "MILLIMETRE"}, {"metresPerUnit", 0.001}, {"metresPerUnitExact", true},
                          {"override", false}};
    p.report = QJsonObject{{"accounting", QJsonObject{{"entities", 40}, {"converted", 6}, {"supporting", 30}, {"omitted", 4}}},
                           {"omittedEntities", QJsonObject{{"IfcRelDeclares", 1}, {"IfcProjectLibrary", 1}, {"IfcPostalAddress", 2}}}};
    p.materials = {{"Concrete", {0.6f, 0.6f, 0.62f}, 1}, {"Glass", {0.3f, 0.5f, 0.9f}, 0.4f}};
    auto wall = cubeMesh(1, 0);
    wall.colors.assign(wall.triangles.size(), std::nullopt);
    wall.colors[0] = std::array<float, 3>{1.0f, 0.0f, 0.5f};
    wall.featureEdges = {{0, 1}, {1, 3}};
    p.meshes = {wall, cubeMesh(2, 1)};
    auto project = node(1, "IfcProject", "Project", -1, IfcNodeRole::Project);
    auto building = node(2, "IfcBuilding", "Building", 0, IfcNodeRole::Building);
    auto storey = node(3, "IfcBuildingStorey", "Level 1", 1, IfcNodeRole::Storey);
    storey.transform = translation(0, 0, 3);
    auto first = node(10, "IfcWall", "Wall A", 2, IfcNodeRole::Element, 0);
    first.properties = {{"Pset_WallCommon", "FireRating", 0, std::string("EI60")},
                        {"Pset_WallCommon", "LoadBearing", 0, true},
                        {"Qto_WallBaseQuantities", "Length", 1, 4.5}};
    auto second = node(11, "IfcWall", "Wall B", 2, IfcNodeRole::Element, 0);
    second.transform = translation(5, 0, 0);
    auto space = node(12, "IfcSpace", "Room", 2, IfcNodeRole::Space, 1);
    space.hidden = true;
    p.nodes = {project, building, storey, first, second, space};
    return p;
}
} // namespace ifc_fixture
