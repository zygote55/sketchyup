#include "automation/commands.hpp"
#include "core/entity_measure.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <QDir>
#include <iostream>
#include <numbers>
using namespace sketchy;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void near(double actual, double expected, double epsilon, const char *message) {
    check(std::abs(actual - expected) <= epsilon, message);
}
QJsonObject batch(const Document &doc, QJsonArray commands) {
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(doc.identity())},
            {"expectedRevision", QString::number(doc.revision())},
            {"commands", commands}};
}
QJsonObject stairs(QJsonObject fields = {}) {
    fields["command"] = "assembly.stairs";
    return fields;
}
QJsonObject report(QJsonObject result) {
    return result["recipeOperations"].toArray()[0].toObject();
}
void preserved(const Document &before, const Document &after) {
    for (const auto &[id, body] : before.bodies())
        check(*after.bodies().at(id) == *body &&
                  after.worldTransform(id) == before.worldTransform(id),
              "Stair creation preserves existing body identities, geometry, paint and poses");
    for (const auto &[id, material] : before.materials())
        check(after.materials().at(id) == material, "Existing materials remain unchanged");
    check(before.definitions() == after.definitions() && before.instances() == after.instances() &&
              before.tags() == after.tags() && before.assets() == after.assets() &&
              before.hostedComponents() == after.hostedComponents() &&
              before.displayUnits() == after.displayUnits(),
          "Stair creation preserves existing component, attachment, resource and unit records");
}
void verify(const Document &doc, const QJsonObject &result, int count, double width,
            double totalRise, double going) {
    const auto root = result["stairs"].toString().toULongLong();
    const auto solid = result["solid"].toString().toULongLong();
    const auto &body = *doc.bodies().at(solid);
    check(doc.bodies().at(root)->kind == BodyKind::Group && body.parent == root,
          "Stairs are an editable group containing a native solid");
    const auto measured = measureEntity(doc, {root, SelectionKind::Body, 0});
    const double rise = totalRise / count, run = count * going;
    const double volume = width * going * totalRise * (count + 1) / 2;
    const double area = going * totalRise * (count + 1) + 2 * width * (run + totalRise);
    check(measured.solid.status == "solid" && measured.local.volume && measured.local.bounds,
          "Stair flight passes independent closed-material-solid validation");
    near(*measured.local.volume, volume, 1e-4, "Stair material volume matches summed step prisms");
    near(measured.local.area, area, 1e-4, "Stair area includes all actual treads and risers");
    check(length(measured.local.bounds->low) < 1e-6 &&
              length(measured.local.bounds->high - Vec3{run, width, totalRise}) < 1e-6,
          "Stair footprint, width and final elevation match requested dimensions");
    const auto shells = analyzeSolidShells(body.surface, body.topology);
    check(shells.shells.size() == 1 && shells.shells.front().signedVolume > 0,
          "Stair shell is consistently outward oriented");
    // Find each top segment from actual profile vertices, independently of metadata.
    for (int step = 0; step < count; ++step) {
        std::optional<Vec3> start, end, below;
        for (const auto &[id, p] : body.surface.vertices) {
            if (std::abs(p.y) > 1e-7)
                continue;
            if (std::abs(p.x - step * going) < 1e-7 && std::abs(p.z - (step + 1) * rise) < 1e-7)
                start = p;
            if (std::abs(p.x - (step + 1) * going) < 1e-7 &&
                std::abs(p.z - (step + 1) * rise) < 1e-7)
                end = p;
            if (std::abs(p.x - step * going) < 1e-7 && std::abs(p.z - step * rise) < 1e-7)
                below = p;
        }
        check(start && end && below, "Every actual tread and riser has its required endpoints");
        near(length(*end - *start), going, 1e-7, "Every tread has the requested going");
        near(length(*start - *below), rise, 1e-7, "Every riser has the derived equal rise");
    }
    check(result["assertions"].toArray().size() == 5,
          "Stairs report measured volume, area and bounds postconditions");
    for (const auto &value : result["assertions"].toArray())
        check(value.toObject()["passed"] == true &&
                  value.toObject()["evaluation"] == "recipe_completion",
              "Completion postconditions pass with an explicit evaluation stage");
}
void run(const QString &capture) {
    Document doc;
    const auto room = executeBatch(doc, batch(doc, {QJsonObject{{"command", "assembly.room"}}}));
    const QJsonValue roomId = report(room)["room"];
    executeBatch(
        doc, batch(doc, {QJsonObject{{"command", "assembly.room.adopt_hosted"}, {"body", roomId}},
                         QJsonObject{{"command", "assembly.roof"}}}));
    const auto original = doc.readSnapshot();
    const auto before = encodeContainer(doc);
    const auto depth = doc.history().total;
    const auto command = stairs({{"origin", QJsonArray{7, 0, 0}}});
    const auto preview = previewBatch(doc, batch(doc, {command}));
    check(encodeContainer(doc) == before, "Stair preview is private");
    const auto made = executeBatch(doc, batch(doc, {command}));
    check(preview["changes"] == made["changes"] && doc.history().total == depth + 1,
          "Stair commit agrees with preview and uses one Undo");
    verify(doc, report(made), 12, 1, 2.4, .28);
    preserved(original, doc);
    const auto bytes = encodeContainer(doc);
    check(encodeContainer(decodeContainer(bytes)) == bytes,
          "Stairs and existing scene reopen exactly");
    if (!capture.isEmpty()) {
        QDir().mkpath(capture);
        auto starting = original.readSnapshot();
        saveDocument(starting, capture + "/stairs-before.sketchyup");
        saveDocument(doc, capture + "/stairs-after.sketchyup");
    }
    doc.undo();
    preserved(original, doc);
    check(doc.bodies().size() == original.bodies().size() &&
              doc.materials().size() == original.materials().size(),
          "One Undo removes only the entire stair flight and its material");
    doc.redo();
    const auto root = report(made)["stairs"].toString().toULongLong();
    doc.transform(root, Transform::translation({20, -10, 2}) * Transform::scaling({-2, .5, 1}));
    const auto transformed = measureEntity(doc, {root, SelectionKind::Body, 0});
    near(*transformed.world.volume, 4.368, 1e-5,
         "Stairs remain editable under reflected and nonuniform group transforms");
    for (const auto large : {false, true}) {
        Document bounded;
        const int count = large ? 32 : 2;
        const double width = large ? 5 : .4, height = large ? 10 : .2, going = large ? 1 : .1;
        const auto made =
            executeBatch(bounded, batch(bounded, {stairs({{"stepCount", count},
                                                          {"width", width},
                                                          {"totalRise", height},
                                                          {"going", going},
                                                          {"origin", QJsonArray{100, 200, 3}}})}));
        verify(bounded, report(made), count, width, height, going);
        const auto root = report(made)["stairs"].toString().toULongLong();
        check(bounded.worldTransform(root).point({}) == Vec3{100, 200, 3},
              "Explicit origin places the stair group without baking vertices");
    }
    for (const int count : {2, 24}) {
        Document boundary;
        const double height = count == 24 ? 1.2 : 1;
        const auto made = executeBatch(
            boundary, batch(boundary, {stairs({{"stepCount", count}, {"totalRise", height}})}));
        verify(boundary, report(made), count, 1, height, .28);
    }
    for (auto invalid :
         {stairs({{"stepCount", 1}}), stairs({{"stepCount", 2.5}}), stairs({{"going", 0}}),
          stairs({{"stepCount", 24}, {"totalRise", 1.1999999}}),
          stairs({{"stepCount", 2}, {"totalRise", 1.0000001}}),
          stairs({{"totalRise", .2}, {"stepCount", 32}}),
          stairs({{"totalRise", 10}, {"stepCount", 2}}),
          stairs({{"origin", QJsonArray{999999, 0, 0}}}), stairs({{"guessLanding", true}})}) {
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
              "Invalid stair dimensions and fields roll back preceding batch edits");
    }
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        run(argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString{});
        std::cout << "Stair rise/run, closed volume, preserved scene, preview/Undo, persistence "
                     "and rejection passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
