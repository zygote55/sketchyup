#include "automation/stair_recipe.hpp"
#include "automation/recipe_builder.hpp"
#include <limits>
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
ModelRecipeResult executeStairRecipe(Document &doc, const QJsonObject &command) {
    const int count = command.value("stepCount").toInt(12);
    const double width = command.value("width").toDouble(1),
                 height = command.value("totalRise").toDouble(2.4),
                 going = command.value("going").toDouble(.28);
    const double rise = height / count, run = going * count;
    // Decimal inputs such as 1.2 / 24 round just below the inclusive 0.05 m bound.
    constexpr double roundoff = 16 * std::numeric_limits<double>::epsilon();
    if (rise < .05 * (1 - roundoff) || rise > .5 * (1 + roundoff))
        throw InspectionError("INVALID_RECIPE",
                              "Derived stair rise must be between 0.05 and 0.5 m");
    const auto values = command.value("origin").toArray({0, 0, 0});
    const Vec3 origin{values[0].toDouble(), values[1].toDouble(), values[2].toDouble()};
    const Vec3 high{run, width, height};
    checkPoint(origin);
    checkPoint(origin + high);
    // Ascending profile in X/Z has outward normal +Y for positive-width extrusion.
    QJsonArray profile{point({0, 0, 0})};
    for (int step = 0; step < count; ++step) {
        profile.append(point({step * going, 0, (step + 1) * rise}));
        profile.append(point({(step + 1) * going, 0, (step + 1) * rise}));
    }
    profile.append(point({run, 0, 0}));
    RecipeBuilder build(doc);
    const auto solid = build.created({{"command", "geometry.face"},
                                      {"name", "Solid stair flight"},
                                      {"loops", QJsonArray{profile}}});
    build.run({{"command", "geometry.extrude_isolated"},
               {"body", id(solid)},
               {"face", id(doc.bodies().at(solid)->surface.faces.begin()->first)},
               {"distance", width}});
    const auto root = build.created({{"command", "group.create"},
                                     {"members", QJsonArray{id(solid)}},
                                     {"name", "Straight stairs"}});
    if (origin != Vec3{}) {
        QJsonArray matrix;
        for (const auto value : Transform::translation(origin).m)
            matrix.append(value);
        build.run({{"command", "scene.transform"},
                   {"body", id(root)},
                   {"matrix", matrix},
                   {"parent", "0"}});
    }
    const auto material = build.material("Stair finish", {.62, .63, .61});
    build.paint(solid, material);
    build.properties(solid, {{"recipe.role", "stair-solid"}, {"recipe.stairs", id(root)}});
    const double volume = width * going * height * (count + 1) / 2;
    const double area = going * height * (count + 1) + 2 * width * (run + height);
    const double edgeLength = 4 * (run + height) + (2 * count + 2) * width;
    const QJsonObject properties{{"recipe.kind", "straight-stairs"},
                                 {"recipe.version", 1},
                                 {"recipe.stepCount", count},
                                 {"recipe.width", width},
                                 {"recipe.totalRise", height},
                                 {"recipe.going", going},
                                 {"recipe.rise", rise},
                                 {"recipe.totalRun", run},
                                 {"recipe.solid", id(solid)}};
    const auto verified = build.runBatch(
        {QJsonObject{{"command", "entity.properties"}, {"body", id(root)}, {"values", properties}},
         assertion(root, "volume", volume, std::max(1e-7, 8 * tolerance * area)),
         assertion(root, "area", area, std::max(1e-7, 8 * tolerance * edgeLength)),
         assertion(root, "dimensions", point(high), 1e-6),
         assertion(root, "minimum", point({}), 1e-6),
         assertion(root, "maximum", point(high), 1e-6)});
    QJsonArray assertions;
    for (const auto &value : verified["assertions"].toArray()) {
        auto record = value.toObject();
        record["evaluation"] = "recipe_completion";
        assertions.append(record);
    }
    const QJsonValue measuredVolume = verified["assertions"].toArray()[0].toObject()["actual"];
    return build.finish({{"recipe", "straight-stairs-v1"},
                         {"stairs", id(root)},
                         {"solid", id(solid)},
                         {"material", id(material)},
                         {"stepCount", count},
                         {"width", width},
                         {"totalRise", height},
                         {"going", going},
                         {"rise", rise},
                         {"totalRun", run},
                         {"origin", point(origin)},
                         {"materialVolume", measuredVolume},
                         {"assertions", assertions}});
}
} // namespace sketchy
