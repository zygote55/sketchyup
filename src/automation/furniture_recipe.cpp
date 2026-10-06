#include "automation/furniture_recipe.hpp"
#include "automation/recipe_builder.hpp"
namespace sketchy {
namespace {
QString id(Id value) { return QString::number(value); }
QJsonArray point(Vec3 p) { return {p.x, p.y, p.z}; }
QJsonArray matrix(Vec3 position) {
    QJsonArray result;
    for (double value : Transform::translation(position).m)
        result.append(value);
    return result;
}
struct Member {
    Id body;
    QString role;
    Vec3 dimensions, position;
};
QJsonObject assertion(Id body, QString metric, QJsonValue expected, double epsilon) {
    return {{"command", "assert.measurement"},
            {"body", id(body)},
            {"space", "local"},
            {"metric", metric},
            {"expected", expected},
            {"tolerance", epsilon}};
}
Id box(RecipeBuilder &build, Vec3 size, QString name, Id material) {
    const auto body = build.created(
        {{"command", "geometry.face"},
         {"name", name},
         {"loops", QJsonArray{QJsonArray{point({}), point({size.x, 0, 0}),
                                         point({size.x, size.y, 0}), point({0, size.y, 0})}}}});
    build.run({{"command", "geometry.extrude_isolated"},
               {"body", id(body)},
               {"face", id(build.doc.bodies().at(body)->surface.faces.begin()->first)},
               {"distance", size.z}});
    build.paint(body, material);
    return body;
}
void place(RecipeBuilder &build, Id body, Vec3 position) {
    if (position != Vec3{})
        build.run({{"command", "scene.transform"},
                   {"body", id(body)},
                   {"matrix", matrix(position)},
                   {"parent", "0"}});
}
void family(RecipeBuilder &build, Vec3 size, QString name, Id material,
            const std::vector<std::pair<QString, Vec3>> &placements, std::vector<Member> &members,
            QJsonArray &definitions) {
    const auto geometry = box(build, size, name + " solid", material);
    const auto first = build.created(
        {{"command", "group.create"}, {"members", QJsonArray{id(geometry)}}, {"name", name}});
    const auto operation = build
                               .run({{"command", "component.create"},
                                     {"body", id(first)},
                                     {"name", name}})["componentOperations"]
                               .toArray()[0]
                               .toObject();
    const auto definition = operation["definition"].toString();
    definitions.append(definition);
    for (size_t i = 0; i < placements.size(); ++i) {
        const auto &[role, position] = placements[i];
        Id body = first;
        if (i == 0)
            place(build, first, position);
        else
            body = build
                       .run({{"command", "component.instance"},
                             {"definition", definition},
                             {"matrix", matrix(position)},
                             {"parent", "0"},
                             {"name", role}})["componentOperations"]
                       .toArray()[0]
                       .toObject()["instance"]
                       .toString()
                       .toULongLong();
        members.push_back({body, role, size, position});
    }
}
} // namespace
ModelRecipeResult executeFurnitureRecipe(Document &doc, const QJsonObject &command) {
    const bool cabinet = command.value("command") == "assembly.cabinet";
    const double w = command.value("width").toDouble(cabinet ? .9 : 1.2),
                 d = command.value("depth").toDouble(cabinet ? .4 : .8),
                 h = command.value("height").toDouble(cabinet ? 1.2 : .75),
                 thickness = command.value(cabinet ? "panelThickness" : "topThickness")
                                 .toDouble(cabinet ? .018 : .04),
                 leg = command.value("legSize").toDouble(.05),
                 inset = command.value("legInset").toDouble(.06);
    const int shelves = command.value("shelves").toInt(2);
    const double shelfGap = (h - (shelves + 2) * thickness) / (shelves + 1);
    if (cabinet) {
        if (w - 2 * thickness < .05 - tolerance || d - thickness < .05 - tolerance ||
            shelfGap < .05 - tolerance)
            throw InspectionError(
                "INVALID_RECIPE",
                "Cabinet interior width, depth and shelf openings must each be at least 0.05 m");
    } else if (w - 2 * (inset + leg) < .05 - tolerance || d - 2 * (inset + leg) < .05 - tolerance ||
               h - thickness < .05 - tolerance) {
        throw InspectionError("INVALID_RECIPE",
                              "Table leg height and clear spans must each be at least 0.05 m");
    }
    const auto values = command.value("origin").toArray({0, 0, 0});
    const Vec3 origin{values[0].toDouble(), values[1].toDouble(), values[2].toDouble()};
    checkPoint(origin);
    checkPoint(origin + Vec3{w, d, h});
    RecipeBuilder build(doc);
    const auto material =
        build.material(cabinet ? "Cabinet finish" : "Table finish",
                       cabinet ? QJsonArray{.59, .67, .62} : QJsonArray{.63, .43, .25});
    std::vector<Member> members;
    QJsonArray definitions;
    if (cabinet) {
        family(build, {thickness, d, h}, "Cabinet side panel", material,
               {{"side-left", {}}, {"side-right", {w - thickness, 0, 0}}}, members, definitions);
        std::vector<std::pair<QString, Vec3>> levels{{"bottom", {thickness, 0, 0}},
                                                     {"top", {thickness, 0, h - thickness}}};
        for (int i = 1; i <= shelves; ++i)
            levels.push_back(
                {QString("shelf-%1").arg(i), {thickness, 0, i * (shelfGap + thickness)}});
        family(build, {w - 2 * thickness, d - thickness, thickness}, "Cabinet horizontal panel",
               material, levels, members, definitions);
        const Vec3 size{w - 2 * thickness, thickness, h}, position{thickness, d - thickness, 0};
        const auto back = box(build, size, "Cabinet back panel", material);
        place(build, back, position);
        members.push_back({back, "back", size, position});
    } else {
        const Vec3 size{w, d, thickness}, position{0, 0, h - thickness};
        const auto top = box(build, size, "Table top", material);
        place(build, top, position);
        members.push_back({top, "top", size, position});
        family(build, {leg, leg, h - thickness}, "Table leg", material,
               {{"leg-1", {inset, inset, 0}},
                {"leg-2", {w - inset - leg, inset, 0}},
                {"leg-3", {w - inset - leg, d - inset - leg, 0}},
                {"leg-4", {inset, d - inset - leg, 0}}},
               members, definitions);
    }
    QJsonArray ids;
    for (const auto &member : members)
        ids.append(id(member.body));
    const auto root = build.created({{"command", "group.create"},
                                     {"members", ids},
                                     {"name", cabinet ? "Open cabinet" : "Four-leg table"}});
    place(build, root, origin);
    for (const auto &member : members)
        build.properties(member.body,
                         {{"recipe.role", member.role}, {"recipe.assembly", id(root)}});
    QJsonObject properties{{"recipe.kind", cabinet ? "cabinet" : "table"},
                           {"recipe.version", 1},
                           {"recipe.width", w},
                           {"recipe.depth", d},
                           {"recipe.height", h}};
    if (cabinet) {
        properties["recipe.panelThickness"] = thickness;
        properties["recipe.shelves"] = shelves;
        properties["recipe.clearOpening"] = shelfGap;
    } else {
        properties["recipe.topThickness"] = thickness;
        properties["recipe.legSize"] = leg;
        properties["recipe.legInset"] = inset;
    }
    QJsonArray commands{
        QJsonObject{{"command", "entity.properties"}, {"body", id(root)}, {"values", properties}},
        assertion(root, "dimensions", point({w, d, h}), 1e-6),
        assertion(root, "minimum", point({}), 1e-6),
        assertion(root, "maximum", point({w, d, h}), 1e-6)};
    for (const auto &member : members) {
        const auto s = member.dimensions;
        const auto area = 2 * (s.x * s.y + s.y * s.z + s.x * s.z);
        commands.append(assertion(member.body, "volume", s.x * s.y * s.z,
                                  std::max(1e-7, 8 * tolerance * area)));
        commands.append(assertion(member.body, "dimensions", point(s), 1e-6));
    }
    const auto verified = build.runBatch(commands);
    QJsonArray assertions;
    for (const auto &value : verified["assertions"].toArray()) {
        auto record = value.toObject();
        record["evaluation"] = "recipe_completion";
        assertions.append(record);
    }
    QJsonArray details;
    double volumeSum{};
    for (size_t i = 0; i < members.size(); ++i) {
        const auto &member = members[i];
        const auto volume = assertions[3 + 2 * i].toObject()["actual"].toDouble();
        volumeSum += volume;
        details.append(QJsonObject{{"body", id(member.body)},
                                   {"role", member.role},
                                   {"dimensions", point(member.dimensions)},
                                   {"position", point(member.position)},
                                   {"volume", volume}});
    }
    QJsonObject report{{"recipe", cabinet ? "cabinet-v1" : "table-v1"},
                       {"assembly", id(root)},
                       {"width", w},
                       {"depth", d},
                       {"height", h},
                       {"origin", point(origin)},
                       {"material", id(material)},
                       {"definitions", definitions},
                       {"members", details},
                       {"memberVolumeSum", volumeSum},
                       {"assertions", assertions}};
    if (cabinet) {
        report["panelThickness"] = thickness;
        report["shelves"] = shelves;
        report["clearOpening"] = shelfGap;
    } else {
        report["topThickness"] = thickness;
        report["legSize"] = leg;
        report["legInset"] = inset;
    }
    return build.finish(report);
}
} // namespace sketchy
