#include "automation/commands.hpp"
#include "core/entity_measure.hpp"
#include "core/groups.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <QDir>
#include <iostream>
#include <numbers>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
QString id(Id value) { return QString::number(value); }
QJsonArray point(Vec3 p) { return {p.x, p.y, p.z}; }
QJsonObject batch(const Document &doc, QJsonArray commands) {
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(doc.identity())},
            {"expectedRevision", id(doc.revision())},
            {"commands", commands}};
}
QJsonObject report(QJsonObject reply) { return reply["recipeOperations"].toArray()[0].toObject(); }
QJsonObject place(Id body, Vec3 position, QString unit = "m", QString frame = "world",
                  double yaw = 0) {
    return {{"command", "assembly.site_place"},
            {"body", id(body)},
            {"position", point(position)},
            {"positionUnit", unit},
            {"frame", frame},
            {"yawDeltaRadians", yaw}};
}
void preserved(const Document &before, const Document &after, Id target) {
    check(before.bodies().size() == after.bodies().size(),
          "Site placement preserves every body ID");
    for (const auto &[bodyId, body] : before.bodies()) {
        auto expected = *body;
        if (bodyId == target)
            expected.transform = after.bodies().at(target)->transform;
        check(*after.bodies().at(bodyId) == expected,
              "Only the selected placement matrix changes; all local geometry and metadata remain "
              "exact");
    }
    check(before.definitions() == after.definitions() && before.instances() == after.instances() &&
              before.hostedComponents() == after.hostedComponents() &&
              before.materials() == after.materials() && before.tags() == after.tags() &&
              before.assets() == after.assets() && before.displayUnits() == after.displayUnits(),
          "Site placement preserves canonical components, hosts, materials, resources and display "
          "units");
}
Transform desired(Transform before, Vec3 position, double yaw) {
    auto result = Transform::rotation({0, 0, 1}, yaw) * before;
    result.m[12] = position.x;
    result.m[13] = position.y;
    result.m[14] = position.z;
    return result;
}
void sameTransform(Transform actual, Transform expected) {
    for (size_t i = 0; i < 16; ++i)
        check(std::abs(actual.m[i] - expected.m[i]) < 1e-7,
              "Placement retains the requested frame, origin, rotation, scale and shear");
}
void run(const QString &capture) {
    Document doc;
    const auto room =
        report(executeBatch(doc, batch(doc, {QJsonObject{{"command", "assembly.room"}}})));
    const auto assemblies = executeBatch(
        doc,
        batch(doc,
              {QJsonObject{{"command", "assembly.room.adopt_hosted"}, {"body", room["room"]}},
               QJsonObject{{"command", "assembly.roof"}},
               QJsonObject{{"command", "assembly.stairs"}, {"origin", QJsonArray{7, 0, 0}}},
               QJsonObject{{"command", "assembly.table"}, {"origin", QJsonArray{0, -2, 0}}},
               QJsonObject{{"command", "assembly.cabinet"}, {"origin", QJsonArray{2, -2, 0}}}}));
    std::set<Id> roots;
    for (const auto &[bodyId, body] : doc.bodies())
        if (!body->parent)
            roots.insert(bodyId);
    const auto site = createGroup(doc, roots, "Site study");
    const auto unrelated = doc.addFace({{{-20, -20, 0}, {-19, -20, 0}, {-20, -19, 0}}});
    const auto original = doc.readSnapshot();
    const auto before = encodeContainer(doc);
    const auto history = doc.history().total;
    const Vec3 position{100000.125, 200000.25, 12.5};
    const double yaw = std::numbers::pi / 6;
    const auto request =
        batch(doc, {place(site, {100000125, 200000250, 12500}, "mm", "world", yaw)});
    const auto preview = previewBatch(doc, request);
    check(encodeContainer(doc) == before, "Site preview is private");
    const auto made = executeBatch(doc, request);
    const auto result = report(made);
    check(preview["changes"] == made["changes"] && doc.history().total == history + 1,
          "Site placement agrees with preview and uses one Undo");
    const auto expected = desired(original.worldTransform(site), position, yaw);
    sameTransform(doc.worldTransform(site), expected);
    preserved(original, doc, site);
    check(result["positionMeters"].toArray() == point(position) && result["frame"] == "world",
          "Receipt reports explicit canonical coordinates and frame");
    check(result["assertions"].toArray().size() == 3,
          "Site placement reports world bounds and unchanged local dimensions");
    for (const auto &value : result["assertions"].toArray())
        check(value.toObject()["passed"] == true &&
                  value.toObject()["evaluation"] == "recipe_completion",
              "Site completion assertions pass");
    const auto delta = expected * original.worldTransform(site).inverse();
    for (const auto &[bodyId, body] : original.bodies())
        if (bodyId != unrelated) {
            const auto oldFrame = original.worldTransform(bodyId),
                       newFrame = doc.worldTransform(bodyId);
            for (const auto &[vertex, p] : body->surface.vertices)
                check(length(newFrame.point(p) - delta.point(oldFrame.point(p))) < 1e-7,
                      "Every actual world vertex retains sub-micrometre precision far from the "
                      "origin");
        }
    const auto bytes = encodeContainer(doc);
    check(encodeContainer(decodeContainer(bytes)) == bytes,
          "Far-site placements and attachments reopen exactly");
    if (!capture.isEmpty()) {
        QDir().mkpath(capture);
        auto start = original.readSnapshot();
        saveDocument(start, capture + "/site-before.sketchyup");
        saveDocument(doc, capture + "/site-after.sketchyup");
    }
    doc.undo();
    sameTransform(doc.worldTransform(site), original.worldTransform(site));
    preserved(original, doc, site);
    doc.redo();
    sameTransform(doc.worldTransform(site), expected);
    const auto roof =
        assemblies["recipeOperations"].toArray()[1].toObject()["roof"].toString().toULongLong();
    const auto oldVolume = measureEntity(original, {roof, SelectionKind::Body, 0}).world.volume;
    const auto newVolume = measureEntity(doc, {roof, SelectionKind::Body, 0}).world.volume;
    check(oldVolume && newVolume && std::abs(*oldVolume - *newVolume) < 1e-7,
          "Rigid site placement preserves validated member material volume");
    for (const auto &unit :
         {QString("m"), QString("mm"), QString("cm"), QString("in"), QString("ft")}) {
        Document one;
        const auto created =
            report(executeBatch(one, batch(one, {QJsonObject{{"command", "assembly.roof"}}})));
        const auto target = created["roof"].toString().toULongLong();
        const double factor = unit == "m"    ? 1
                              : unit == "mm" ? .001
                              : unit == "cm" ? .01
                              : unit == "in" ? .0254
                                             : .3048;
        executeBatch(one, batch(one, {place(target, {10, 20, 30}, unit)}));
        check(length(one.worldTransform(target).point({}) -
                     Vec3{10 * factor, 20 * factor, 30 * factor}) < 1e-10,
              "Explicit input units convert to independently expected metres");
    }
    for (const bool world : {false, true}) {
        Document nested;
        const auto created = report(
            executeBatch(nested, batch(nested, {QJsonObject{{"command", "assembly.roof"}}})));
        const auto target = created["roof"].toString().toULongLong();
        const auto parent = createGroup(nested, {target}, "Rotated site frame");
        auto parentFrame = Transform::translation({100, 200, 4}) *
                           Transform::rotation({0, 0, 1}, .4) * Transform::scaling({-2, .5, 1.5});
        parentFrame.m[4] += .2;
        nested.transform(parent, parentFrame);
        nested.transform(
            target, Transform::translation({1, 2, 3}) * Transform::scaling({.8, 1.2, .5}), parent);
        const auto original = nested.readSnapshot();
        const auto beforeFrame =
            world ? nested.worldTransform(target) : nested.bodies().at(target)->transform;
        const auto want = desired(beforeFrame, {10, 20, 30}, -.3);
        executeBatch(nested, batch(nested, {place(target, {10, 20, 30}, "m",
                                                  world ? "world" : "parent", -.3)}));
        sameTransform(world ? nested.worldTransform(target) : nested.bodies().at(target)->transform,
                      want);
        preserved(original, nested, target);
    }
    auto missingUnit = place(site, {1, 2, 3});
    missingUnit.remove("positionUnit");
    for (auto invalid :
         {missingUnit, place(999999, {1, 2, 3}), place(site, {999999, 999999, 0}),
          place(site, {1, 2, 3}, "yards"), place(site, {1, 2, 3}, "m", "guess"),
          place(site, {1, 2, 3}, "m", "world", 10),
          place(room["wall"].toString().toULongLong(), {1, 2, 3}),
          place(room["windows"].toArray()[0].toObject()["body"].toString().toULongLong(),
                {1, 2, 3})}) {
        const auto unchanged = encodeContainer(doc);
        bool failed{};
        try {
            executeBatch(
                doc,
                batch(doc, {QJsonObject{{"command", "document.units"}, {"units", "mm"}}, invalid}));
        } catch (const std::exception &) {
            failed = true;
        }
        check(failed && encodeContainer(doc) == unchanged,
              "Invalid site requests and partial hosted assemblies roll back earlier commands");
    }
    const auto wall = room["wall"].toString().toULongLong();
    setEntityState(doc, wall, {}, true);
    const auto locked = encodeContainer(doc);
    bool rejected{};
    try {
        executeBatch(doc, batch(doc, {place(site, {100, 200, 3})}));
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected && encodeContainer(doc) == locked,
          "Site placement cannot indirectly move a locked descendant");
    setEntityState(doc, wall, {}, false);
    const auto window = room["windows"].toArray()[0].toObject()["body"].toString().toULongLong();
    Id member{};
    for (const auto &[canonical, scene] : doc.instances().at(window)->members)
        if (!doc.bodies().at(scene)->surface.vertices.empty())
            member = scene;
    const auto scoped = encodeContainer(doc);
    rejected = false;
    try {
        executeBatch(doc, batch(doc, {place(member, {100, 200, 3})}));
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected && encodeContainer(doc) == scoped,
          "Site placement cannot bypass canonical component edit scope");
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        run(argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString{});
        std::cout << "Site frames, units, far precision, preserved geometry/hosts, Undo and "
                     "persistence passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
