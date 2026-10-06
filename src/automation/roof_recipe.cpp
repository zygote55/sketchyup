#include "automation/roof_recipe.hpp"
#include "automation/recipe_builder.hpp"
#include <cmath>
#include <numbers>
namespace sketchy {
namespace {
QJsonArray point(Vec3 p) { return {p.x, p.y, p.z}; }
QString id(Id value) { return QString::number(value); }
QJsonObject assertion(Id body, QString metric, QJsonValue expected, double epsilon) {
    return {{"command", "assert.measurement"},
            {"body", id(body)},
            {"space", "local"},
            {"metric", metric},
            {"expected", expected},
            {"tolerance", epsilon}};
}
} // namespace
ModelRecipeResult executeRoofRecipe(Document &doc, const QJsonObject &command) {
    const double width = command.value("width").toDouble(6),
                 depth = command.value("depth").toDouble(4),
                 plate = command.value("plateHeight").toDouble(2.7),
                 pitch = command.value("pitchDegrees").toDouble(30),
                 overhang = command.value("overhang").toDouble(.3),
                 thickness = command.value("verticalThickness").toDouble(.15);
    const auto angle = pitch * std::numbers::pi / 180;
    const auto eave = plate - overhang * std::tan(angle);
    const auto ridge = plate + width / 2 * std::tan(angle);
    if (eave <= 0)
        throw InspectionError("INVALID_RECIPE",
                              "Roof overhang and pitch put the eave at or below its origin plane");
    const auto values = command.value("origin").toArray({0, 0, 0});
    const Vec3 origin{values[0].toDouble(), values[1].toDouble(), values[2].toDouble()};
    const Vec3 low{-overhang, -overhang, eave},
        high{width + overhang, depth + overhang, ridge + thickness};
    checkPoint(origin + low);
    checkPoint(origin + high);
    RecipeBuilder build(doc);
    const auto shell = build.created(
        {{"command", "geometry.face"},
         {"name", "Roof shell"},
         {"loops", QJsonArray{QJsonArray{point({-overhang, -overhang, eave + thickness}),
                                         point({width / 2, -overhang, ridge + thickness}),
                                         point({width + overhang, -overhang, eave + thickness}),
                                         point({width + overhang, -overhang, eave}),
                                         point({width / 2, -overhang, ridge}),
                                         point({-overhang, -overhang, eave})}}}});
    build.run({{"command", "geometry.extrude_isolated"},
               {"body", id(shell)},
               {"face", id(doc.bodies().at(shell)->surface.faces.begin()->first)},
               {"distance", depth + 2 * overhang}});
    const auto root = build.created(
        {{"command", "group.create"}, {"members", QJsonArray{id(shell)}}, {"name", "Gable roof"}});
    if (origin != Vec3{}) {
        QJsonArray matrix;
        for (const auto value : Transform::translation(origin).m)
            matrix.append(value);
        build.run({{"command", "scene.transform"},
                   {"body", id(root)},
                   {"matrix", matrix},
                   {"parent", "0"}});
    }
    const auto material = build.material("Roof finish", {.25, .29, .31});
    build.paint(shell, material);
    build.properties(shell, {{"recipe.role", "roof-shell"}, {"recipe.roof", id(root)}});
    const auto span = width + 2 * overhang, length = depth + 2 * overhang;
    const auto slopeLength = span / 2 / std::cos(angle);
    const auto volume = span * length * thickness;
    const auto area = (4 * slopeLength + 2 * thickness) * length + 2 * span * thickness;
    const auto edgeLength = 8 * slopeLength + 4 * thickness + 6 * length;
    // Linear tessellation precision induces area/volume error proportional to
    // edge length / surface area. Publish the explicit tolerances in receipts.
    const auto areaTolerance = std::max(1e-7, 8 * tolerance * edgeLength);
    const auto volumeTolerance = std::max(1e-7, 8 * tolerance * area);
    const QJsonObject properties{
        {"recipe.kind", "gable-roof"}, {"recipe.version", 1},
        {"recipe.width", width},       {"recipe.depth", depth},
        {"recipe.plateHeight", plate}, {"recipe.pitchDegrees", pitch},
        {"recipe.overhang", overhang}, {"recipe.verticalThickness", thickness},
        {"recipe.shell", id(shell)}};
    const auto verified = build.runBatch(
        {QJsonObject{{"command", "entity.properties"}, {"body", id(root)}, {"values", properties}},
         assertion(root, "volume", volume, volumeTolerance),
         assertion(root, "area", area, areaTolerance),
         assertion(root, "dimensions", point(high - low), 1e-6),
         assertion(root, "minimum", point(low), 1e-6),
         assertion(root, "maximum", point(high), 1e-6)});
    QJsonArray assertions;
    for (const auto &value : verified["assertions"].toArray()) {
        auto record = value.toObject();
        record["evaluation"] = "recipe_completion";
        assertions.append(record);
    }
    const QJsonValue measuredVolume = verified["assertions"].toArray()[0].toObject()["actual"];
    return build.finish({{"recipe", "gable-roof-v1"},
                         {"roof", id(root)},
                         {"shell", id(shell)},
                         {"material", id(material)},
                         {"width", width},
                         {"depth", depth},
                         {"plateHeight", plate},
                         {"pitchDegrees", pitch},
                         {"overhang", overhang},
                         {"verticalThickness", thickness},
                         {"eaveHeight", eave},
                         {"ridgeHeight", ridge},
                         {"topRidgeHeight", ridge + thickness},
                         {"origin", point(origin)},
                         {"materialVolume", measuredVolume},
                         {"assertions", assertions}});
}
} // namespace sketchy
