#include "automation/commands.hpp"
#include "automation/component_scope.hpp"
#include "core/entity_measure.hpp"
#include "core/groups.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <iostream>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void near(double actual, double expected, const char *message) {
    check(std::abs(actual - expected) < 1e-6, message);
}
QJsonObject run(Document &doc, QJsonArray commands) {
    return executeBatch(doc, {{"apiVersion", 1},
                              {"documentId", QString::fromStdString(doc.identity())},
                              {"expectedRevision", QString::number(doc.revision())},
                              {"commands", commands}});
}
QJsonObject report(const QJsonObject &result) {
    return result.value("recipeOperations").toArray()[0].toObject();
}
QJsonObject resize(Id body, double width = 1.4) {
    return {{"command", "assembly.window.resize"},
            {"body", QString::number(body)},
            {"scope", "instance"},
            {"width", width}};
}
QJsonArray matrix(Transform transform) {
    QJsonArray result;
    for (auto v : transform.m)
        result.append(v);
    return result;
}
void transform(Document &doc, Id body, Transform placement) {
    run(doc, {QJsonObject{{"command", "scene.transform"},
                          {"body", QString::number(body)},
                          {"matrix", matrix(placement)},
                          {"parent", QString::number(doc.bodies().at(body)->parent)}}});
}
void unchanged(const Document &a, const Document &b) {
    check(encodeBodies(a.bodies()) == encodeBodies(b.bodies()), "Body fingerprints preserved");
    const auto left = QJsonDocument::fromJson(encodeDocument(a)).object();
    const auto right = QJsonDocument::fromJson(encodeDocument(b)).object();
    for (const auto *field : {"definitions", "instances", "materials", "assets", "tags", "units"})
        check(left.value(field) == right.value(field),
              "Scene and appearance fingerprints preserved");
}
void rejects(Document &doc, QJsonArray commands) {
    const auto bytes = encodeDocument(doc);
    const auto stamp = doc.saveStamp();
    const auto history = doc.history().total;
    bool rejected = false;
    try {
        run(doc, commands);
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected, "Invalid recipe rejected");
    check(encodeDocument(doc) == bytes && doc.isCurrentSnapshot(stamp) &&
              doc.history().total == history,
          "Failure leaves live state and history unchanged");
}
struct Room {
    Document doc;
    Id room{}, wall{}, first{}, second{}, frame{}, glass{};
    Room() {
        const auto r = report(run(doc, {QJsonObject{{"command", "assembly.room"}}}));
        room = r["room"].toString().toULongLong();
        wall = r["wall"].toString().toULongLong();
        const auto windows = r["windows"].toArray();
        first = windows[0].toObject()["body"].toString().toULongLong();
        second = windows[1].toObject()["body"].toString().toULongLong();
        for (const auto &[id, record] : doc.bodies())
            if (record->parent == first) {
                const auto role = std::get<std::string>(record->properties.at("recipe.role"));
                if (role == "frame")
                    frame = id;
                else if (role == "glass")
                    glass = id;
            }
        check(frame && glass, "Authored members discovered from explicit roles");
        check(r["expandedCommands"].toArray().size() > 20, "Room expansion is inspectable");
        near(r["wallVolume"].toDouble(), 9.888, "Through openings remove actual wall volume");
    }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        Room fixture;
        auto &doc = fixture.doc;
        const auto original = doc;
        check(doc.revision() == 1 && doc.history().total == 1, "Room is one edit");
        check(doc.bodies().size() == 9 && doc.definitions().size() == 1 &&
                  doc.instances().size() == 2,
              "Room has shared reusable windows");
        const auto roomBounds =
            measureEntity(doc, {fixture.room, SelectionKind::Body, 0}).local.bounds;
        check(roomBounds && length(roomBounds->dimensions() - Vec3{6, 4, 2.7}) < tolerance,
              "Exact 6 by 4 room dimensions");
        const auto firstBefore = *doc.bodies().at(fixture.first);
        const auto wallBefore = *doc.bodies().at(fixture.wall);
        const auto r = report(run(doc, {resize(fixture.first)}));
        check(r["madeUnique"] == true && doc.definitions().size() == 2,
              "Explicit instance scope makes only selected window unique");
        check(doc.revision() == 2 && doc.history().total == 2, "Resize is one additional edit");
        near(r["outerWidth"].toDouble(), 1.4, "Exact widened frame width");
        near(r["clearWidth"].toDouble(), 1.24, "Clear opening accounts for unchanged members");
        near(r["wallVolume"].toDouble(), 9.848, "Wider hole removes expected volume");
        for (auto owner : {fixture.first, fixture.second}) {
            const auto bounds = measureEntity(doc, {owner, SelectionKind::Body, 0}).local.bounds;
            near(bounds->dimensions().x, owner == fixture.first ? 1.4 : 1.2,
                 "Only selected window widened");
            near(bounds->dimensions().y, .1, "Frame depth preserved");
            near(bounds->dimensions().z, 1, "Window height preserved");
        }
        check(firstBefore.transform == doc.bodies().at(fixture.first)->transform,
              "Center and sill placement unchanged");
        for (const auto &[key, record] : original.bodies())
            if (key != fixture.wall && key != fixture.first && key != fixture.frame &&
                key != fixture.glass)
                check(*record == *doc.bodies().at(key),
                      "Every sibling and unrelated record fingerprint unchanged");
        size_t moved{};
        for (const auto &[key, p] : wallBefore.surface.vertices) {
            const auto q = doc.bodies().at(fixture.wall)->surface.vertices.at(key);
            if (p != q) {
                ++moved;
                near(std::abs(q.x - p.x), .1, "Host jamb moved 100 mm");
                near(q.y, p.y, "Host depth preserved");
                near(q.z, p.z, "Host sill and header preserved");
            }
        }
        check(moved == 8, "Only selected host opening corners move");
        const auto &frame = doc.bodies().at(fixture.frame)->surface;
        for (const auto &[_, p] : frame.vertices) {
            check(std::abs(std::abs(p.x) - .7) < tolerance ||
                      std::abs(std::abs(p.x) - .62) < tolerance,
                  "80 mm jambs preserved");
            check(std::abs(p.z) < tolerance || std::abs(p.z - .08) < tolerance ||
                      std::abs(p.z - .92) < tolerance || std::abs(p.z - 1) < tolerance,
                  "80 mm rails preserved");
        }
        const auto widened = doc;
        doc.undo();
        unchanged(doc, original);
        doc.redo();
        unchanged(doc, widened);
        const auto again = report(run(doc, {resize(fixture.first, 1.6)}));
        check(again["madeUnique"] == false && doc.definitions().size() == 2,
              "Repeated unique resize does not add unused definitions");
        run(doc, {resize(fixture.first, 1.2)});
        for (const auto &[vertex, p] : original.bodies().at(fixture.frame)->surface.vertices)
            near(length(doc.bodies().at(fixture.frame)->surface.vertices.at(vertex) - p), 0,
                 "Shrink restores original geometry");
        QTemporaryDir files;
        check(files.isValid(), "Temporary persistence fixture");
        saveDocument(doc, files.path() + "/room.sketchyup");
        auto reopened = loadDocument(files.path() + "/room.sketchyup");
        run(reopened, {resize(fixture.first)});
        near(measureEntity(reopened, {fixture.first, SelectionKind::Body, 0})
                 .local.bounds->dimensions()
                 .x,
             1.4, "Recipe bindings survive save and reopen");
        for (bool mirror : {false, true}) {
            Room rotated;
            transform(rotated.doc, rotated.room,
                      Transform::translation({3, -2, 1}) * Transform::rotation({0, 0, 1}, .7) *
                          Transform::scaling({mirror ? -1. : 1., 1, 1}));
            run(rotated.doc, {resize(rotated.first)});
            near(measureEntity(rotated.doc, {rotated.first, SelectionKind::Body, 0})
                     .local.bounds->dimensions()
                     .x,
                 1.4, "Rotated and reflected room resize uses local frame");
        }
        Room bad;
        rejects(bad.doc, {QJsonObject{{"command", "assembly.room"}}});
        for (auto width : {.15, 0., -1., 2.8, 10., 1.2})
            rejects(bad.doc, {resize(bad.first, width)});
        for (auto value : {QJsonValue("1.4"), QJsonValue(QJsonValue::Null), QJsonValue(true)}) {
            auto command = resize(bad.first);
            command["width"] = value;
            rejects(bad.doc, {command});
        }
        auto missing = resize(bad.first);
        missing.remove("scope");
        rejects(bad.doc, {missing});
        missing["scope"] = "definition";
        rejects(bad.doc, {missing});
        missing = resize(bad.first);
        missing["surprise"] = true;
        rejects(bad.doc, {missing});
        rejects(bad.doc, {resize(99999)});
        // A valid preceding edit must also roll back when a later recipe fails.
        rejects(bad.doc, {QJsonObject{{"command", "document.units"}, {"units", "mm"}},
                          resize(bad.first, 4)});
        for (int variant = 0; variant < 7; ++variant) {
            Room altered;
            if (variant == 0)
                setEntityProperties(altered.doc, altered.first, {});
            if (variant == 1)
                transform(altered.doc, altered.room, Transform::scaling({2, 1, 1}));
            if (variant == 2)
                transform(altered.doc, altered.first, Transform::translation({1.6, 0, .9}));
            if (variant == 3)
                setEntityState(altered.doc, altered.wall, {}, true);
            if (variant == 4)
                setEntityState(altered.doc, altered.room, {}, true);
            if (variant == 5) {
                auto props = altered.doc.bodies().at(altered.first)->properties;
                props["recipe.wall"] = std::string("99999");
                setEntityProperties(altered.doc, altered.first, props);
            }
            if (variant == 6) {
                run(altered.doc, {componentScopeCommand(
                                     altered.doc, altered.first,
                                     {QJsonObject{{"command", "scene.transform"},
                                                  {"body", QString::number(altered.frame)},
                                                  {"matrix", matrix(Transform::scaling({1, 2, 1}))},
                                                  {"parent", QString::number(altered.first)}}})});
            }
            rejects(altered.doc, {resize(altered.first)});
        }
        Document empty;
        for (auto command : QJsonArray{
                 QJsonObject{{"command", "assembly.room"}, {"width", 1}},
                 QJsonObject{{"command", "assembly.room"}, {"wallThickness", 1}, {"depth", 2}},
                 QJsonObject{{"command", "assembly.room"}, {"windowHeight", 3}},
                 QJsonObject{{"command", "assembly.room"}, {"memberThickness", .5}}})
            rejects(empty, {command});
        Document custom;
        run(custom, {QJsonObject{{"command", "assembly.room"},
                                 {"width", 8},
                                 {"depth", 5},
                                 {"wallThickness", .3},
                                 {"height", 3},
                                 {"windowWidth", 1.5},
                                 {"windowHeight", 1.2},
                                 {"memberThickness", .1},
                                 {"sill", 1},
                                 {"frameDepth", .15}}});
        near(measureEntity(custom, {1, SelectionKind::Body, 0}).local.volume.value(), 21.24,
             "Parameterized room volume is verified");
        std::cout << "Model recipe checks passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
