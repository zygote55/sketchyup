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
QJsonObject roof(QJsonObject fields = {}) {
    fields["command"] = "assembly.roof";
    return fields;
}
QJsonObject report(QJsonObject result) {
    return result["recipeOperations"].toArray()[0].toObject();
}
void preserved(const Document &before, const Document &after) {
    for (const auto &[id, body] : before.bodies())
        check(*after.bodies().at(id) == *body &&
                  after.worldTransform(id) == before.worldTransform(id),
              "Roof creation preserves existing body identities, geometry, paint and poses");
    for (const auto &[id, material] : before.materials())
        check(after.materials().at(id) == material, "Existing materials remain unchanged");
    check(before.definitions() == after.definitions() && before.instances() == after.instances() &&
              before.tags() == after.tags() && before.assets() == after.assets() &&
              before.hostedComponents() == after.hostedComponents() &&
              before.displayUnits() == after.displayUnits(),
          "Roof creation preserves existing component, attachment, resource and unit records");
}
void verify(const Document &doc, const QJsonObject &result, double width, double depth,
            double plate, double pitch, double overhang, double thickness) {
    const auto root = result["roof"].toString().toULongLong();
    const auto shell = result["shell"].toString().toULongLong();
    const auto &body = *doc.bodies().at(shell);
    check(doc.bodies().at(root)->kind == BodyKind::Group && body.parent == root &&
              body.name == "Roof shell",
          "Roof is an editable named group and native solid");
    const auto angle = pitch * std::numbers::pi / 180;
    const auto eave = plate - overhang * std::tan(angle);
    const auto ridge = plate + width / 2 * std::tan(angle);
    const auto shells = analyzeSolidShells(body.surface, body.topology);
    check(shells.shells.size() == 1 && shells.shells.front().signedVolume > 0,
          "Roof has one consistently outward-oriented shell");
    const auto measured = measureEntity(doc, {root, SelectionKind::Body, 0});
    check(measured.solid.status == "solid" && measured.local.volume && measured.local.bounds,
          "Roof passes independent closed-material-solid validation");
    near(*measured.local.volume, (width + 2 * overhang) * (depth + 2 * overhang) * thickness, 1e-4,
         "Roof has analytic material volume rather than its enclosing box volume");
    check(length(measured.local.bounds->low - Vec3{-overhang, -overhang, eave}) < 1e-6 &&
              length(measured.local.bounds->high -
                     Vec3{width + overhang, depth + overhang, ridge + thickness}) < 1e-6,
          "Exact footprint overhang, eave and ridge bounds");
    bool lowerRidge{}, upperRidge{}, leftEave{}, rightEave{};
    std::optional<Vec3> measuredRidge, measuredEave;
    for (const auto &[id, p] : body.surface.vertices) {
        if (std::abs(p.y + overhang) < 1e-7) {
            if (std::abs(p.x - width / 2) < 1e-7 && (!measuredRidge || p.z < measuredRidge->z))
                measuredRidge = p;
            if (std::abs(p.x + overhang) < 1e-7 && (!measuredEave || p.z < measuredEave->z))
                measuredEave = p;
        }
        if (std::abs(p.x - width / 2) < 1e-7) {
            lowerRidge |= std::abs(p.z - ridge) < 1e-7;
            upperRidge |= std::abs(p.z - ridge - thickness) < 1e-7;
        }
        leftEave |= length(p - Vec3{-overhang, -overhang, eave}) < 1e-7;
        rightEave |= length(p - Vec3{width + overhang, -overhang, eave}) < 1e-7;
    }
    check(lowerRidge && upperRidge && leftEave && rightEave,
          "Actual vertices encode requested pitch and explicit vertical thickness");
    check(measuredRidge && measuredEave, "Roof exposes actual ridge and eave endpoints");
    near(std::atan2(measuredRidge->z - measuredEave->z, measuredRidge->x - measuredEave->x) * 180 /
             std::numbers::pi,
         pitch, 1e-7, "Actual vertex rise/run relation yields requested pitch");
    check(result["assertions"].toArray().size() >= 4,
          "Roof reports measured completion postconditions");
    for (const auto &value : result["assertions"].toArray())
        check(value.toObject()["passed"] == true &&
                  value.toObject()["evaluation"] == "recipe_completion",
              "Recipe postconditions distinguish completion from outer final-batch assertions");
}
void run(const QString &capture) {
    Document doc;
    const auto room = executeBatch(doc, batch(doc, {QJsonObject{{"command", "assembly.room"}}}));
    const QJsonValue roomId = report(room)["room"];
    executeBatch(doc, batch(doc, {QJsonObject{{"command", "assembly.room.adopt_hosted"},
                                              {"body", roomId}}}));
    check(doc.hostedComponents().attachments.size() == 2,
          "Roof fixture starts with general hosted windows");
    const auto original = doc.readSnapshot();
    const auto before = encodeContainer(doc);
    const auto depth = doc.history().total;
    const auto preview = previewBatch(doc, batch(doc, {roof()}));
    check(encodeContainer(doc) == before, "Roof preview is private");
    const auto made = executeBatch(doc, batch(doc, {roof()}));
    check(preview["changes"] == made["changes"] && doc.history().total == depth + 1,
          "Roof creation agrees with preview and uses one Undo");
    verify(doc, report(made), 6, 4, 2.7, 30, .3, .15);
    preserved(original, doc);
    const auto bytes = encodeContainer(doc);
    check(encodeContainer(decodeContainer(bytes)) == bytes, "Roof and room save/reopen exactly");
    if (!capture.isEmpty()) {
        QDir().mkpath(capture);
        auto starting = original.readSnapshot();
        saveDocument(starting, capture + "/room-before.sketchyup");
        saveDocument(doc, capture + "/room-with-roof.sketchyup");
    }
    doc.undo();
    preserved(original, doc);
    check(doc.bodies().size() == original.bodies().size() &&
              doc.materials().size() == original.materials().size(),
          "One Undo removes only the complete roof task");
    doc.redo();
    const auto root = report(made)["roof"].toString().toULongLong();
    doc.transform(root, Transform::translation({50, -10, 2}) * Transform::scaling({-2, .5, 1}));
    const auto transformed = measureEntity(doc, {root, SelectionKind::Body, 0});
    near(*transformed.world.volume, 4.554, 1e-5,
         "Roof remains editable as a reflected, scaled assembly after creation");
    for (double pitch : {5., 45., 75.}) {
        Document custom;
        const auto made =
            executeBatch(custom, batch(custom, {roof({{"width", 3},
                                                      {"depth", 5},
                                                      {"plateHeight", 4},
                                                      {"pitchDegrees", pitch},
                                                      {"overhang", .1},
                                                      {"verticalThickness", .08},
                                                      {"origin", QJsonArray{100, 200, 3}}})}));
        verify(custom, report(made), 3, 5, 4, pitch, .1, .08);
        const auto root = report(made)["roof"].toString().toULongLong();
        check(custom.worldTransform(root).point({}) == Vec3{100, 200, 3},
              "Explicit origin sets the roof group placement");
    }
    for (const auto large : {false, true}) {
        Document bounded;
        const double span = large ? 100 : 2, plate = large ? 20 : .5, pitch = large ? 75 : 5,
                     overhang = large ? 2 : 0;
        const auto made =
            executeBatch(bounded, batch(bounded, {roof({{"width", span},
                                                        {"depth", span},
                                                        {"plateHeight", plate},
                                                        {"pitchDegrees", pitch},
                                                        {"overhang", overhang},
                                                        {"verticalThickness", .02}})}));
        verify(bounded, report(made), span, span, plate, pitch, overhang, .02);
    }
    for (auto invalid :
         {roof({{"pitchDegrees", 0}}), roof({{"verticalThickness", 0}}),
          roof({{"plateHeight", .5}, {"overhang", 2}, {"pitchDegrees", 75}}),
          roof({{"origin", QJsonArray{999999, 0, 0}}}), roof({{"guessHost", true}})}) {
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
              "Invalid roof dimensions/fields roll back earlier batch edits");
    }
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        run(argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString{});
        std::cout << "Gable roof pitch, eaves, thickness, closed volume, preserved room, "
                     "preview/Undo, persistence and rejection passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
