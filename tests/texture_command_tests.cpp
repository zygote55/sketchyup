#include "automation/commands.hpp"
#include "automation/component_scope.hpp"
#include "automation/inspection.hpp"
#include "automation/native_assistant_session.hpp"
#include "automation/texture_commands.hpp"
#include "core/components.hpp"
#include "core/face_textures.hpp"
#include "core/groups.hpp"
#include "io/document_io.hpp"
#include "io/texture_image.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <iostream>
#include <numbers>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
QString sid(Id id) { return QString::number(id); }
QJsonArray point(Vec3 p) { return {p.x, p.y, p.z}; }
QJsonObject batch(const Document &doc, QJsonArray commands) {
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(doc.identity())},
            {"expectedRevision", sid(doc.revision())},
            {"commands", commands}};
}
Id square(Document &doc) { return doc.addFace({{{0, 0, 0}, {4, 0, 0}, {4, 3, 0}, {0, 3, 0}}}); }
Id face(const Document &doc, Id body) {
    return doc.bodies().at(body)->surface.faces.begin()->first;
}
QJsonObject planar() {
    return {{"type", "planar"},
            {"origin", QJsonArray{1, 2, 0}},
            {"normal", QJsonArray{0, 0, 1}},
            {"tangent", QJsonArray{1, 0, 0}},
            {"width", 2},
            {"height", -.5},
            {"rotationRadians", std::numbers::pi / 2},
            {"offset", QJsonArray{.25, -.5}}};
}
QJsonObject command(const Document &doc, Id body, QJsonValue projection, QString side = "front",
                    QString space = "local") {
    return {{"command", "material.map_texture"},
            {"body", sid(body)},
            {"face", sid(face(doc, body))},
            {"side", side},
            {"space", space},
            {"projection", projection}};
}
template <class F> void rejects(Document &doc, F run) {
    const auto bytes = encodeContainer(doc);
    const auto history = doc.historyBytes();
    try {
        run();
    } catch (const std::exception &) {
        check(encodeContainer(doc) == bytes && doc.historyBytes() == history,
              "Rejected texture command preserves exact model and history");
        return;
    }
    throw std::runtime_error("Expected texture command rejection");
}
void near(TextureCoordinate actual, TextureCoordinate expected, const char *message) {
    check(std::abs(actual.u - expected.u) < 1e-8 && std::abs(actual.v - expected.v) < 1e-8,
          message);
}
void lifecycle() {
    Document doc;
    const auto body = square(doc), id = face(doc, body);
    const auto original = doc.bodies().at(body);
    const auto bytes = encodeContainer(doc);
    const auto history = doc.history().total;
    const auto request = batch(doc, {command(doc, body, planar())});
    const auto preview = previewBatch(doc, request);
    check(encodeContainer(doc) == bytes, "Mapping preview never mutates live state");
    const auto receipt = executeBatch(doc, request);
    check(receipt["changes"] == preview["changes"] && doc.history().total == history + 1,
          "Mapping preview and committed lineage agree in one Undo item");
    const auto mapped = doc.bodies().at(body);
    const auto sides = faceTextureMappings(*mapped, id);
    check(sides.front && !sides.back && mapped->surface == original->surface &&
              mapped->topology == original->topology &&
              mapped->faceMaterials == original->faceMaterials,
          "Front mapping leaves back, geometry and materials untouched");
    near(sides.front->coordinates({1, 2, 0}), {.25, -.5}, "Origin carries repeat offset");
    near(sides.front->coordinates({1, 4, 0}), {1.25, -.5}, "Rotation turns U toward V");
    near(sides.front->coordinates({2, 2, 0}), {.25, 1.5}, "Signed size mirrors V");
    check(receipt["changes"]
                  .toObject()[sid(body)]
                  .toObject()["faces"]
                  .toObject()["modified"]
                  .toArray() == QJsonArray{sid(id)},
          "Receipt reports mapping-only face modification");
    rejects(doc, [&] { executeBatch(doc, batch(doc, {command(doc, body, planar())})); });
    check(doc.history().total == history + 1, "No-effect mapping batch cannot add history");
    rejects(doc, [&] { executeBatch(doc, request); });
    const auto description = faceTextureDescription(*mapped, id);
    check(executeQuery(doc, {{"query", "material.sample"},
                             {"body", sid(body)},
                             {"face", sid(id)}})["textureMapping"] == description,
          "Legacy material sampling exposes stored and effective projections");
    const auto detail =
        inspectDocument(doc, {{"apiVersion", 1},
                              {"documentId", QString::fromStdString(doc.identity())},
                              {"expectedRevision", sid(doc.revision())},
                              {"query", "entity.describe"},
                              {"target", inspectionReference(doc, body, "face", id)}});
    check(detail["data"].toObject()["textureMapping"] == description,
          "Bounded typed inspection exposes the same body-local projection");
    check(description["back"].toObject()["stored"].isNull() &&
              description["back"].toObject()["effective"].isObject(),
          "Implicit mapping is distinguished from an authored projection");
    doc.undo();
    check(*doc.bodies().at(body) == *original, "Undo restores exact body");
    doc.redo();
    check(*doc.bodies().at(body) == *mapped, "Redo restores exact projection");
    const QJsonObject pins{
        {"type", "pins"},
        {"points", QJsonArray{QJsonArray{0, 0, 0}, QJsonArray{2, 0, 0}, QJsonArray{1, 2, 0}}},
        {"coordinates", QJsonArray{QJsonArray{-1, .25}, QJsonArray{1, .25}, QJsonArray{.5, 2.25}}}};
    executeBatch(doc, batch(doc, {command(doc, body, pins, "back")}));
    const auto back = faceTextureMappings(*doc.bodies().at(body), id).back;
    near(back->coordinates({3, 1, 0}), {2.25, 1.25},
         "Three pins define affine shear independently");
    const auto beforeReset = faceTextureMappings(*doc.bodies().at(body), id);
    executeBatch(doc, batch(doc, {command(doc, body, QJsonValue::Null, "front", "world")}));
    const auto reset = faceTextureMappings(*doc.bodies().at(body), id);
    check(!reset.front && reset.back == beforeReset.back,
          "Explicit null resets only the chosen side");
    near(effectiveFaceTextureMapping(*doc.bodies().at(body), id).coordinates({3, 1, 0}), {3, 1},
         "Reset restores canonical one-metre implicit mapping");
    const auto saved = encodeContainer(doc);
    check(encodeContainer(decodeContainer(saved)) == saved, "Command mapping round-trips exactly");
    auto affine = description["front"].toObject()["stored"].toObject();
    affine["type"] = "affine";
    executeBatch(doc, batch(doc, {command(doc, body, affine, "both")}));
    const auto equal = faceTextureMappings(*doc.bodies().at(body), id);
    check(equal.front == sides.front && equal.back == sides.front,
          "Inspection covectors can be explicitly reassigned to both sides");
}
Transform placement() {
    auto shear = Transform::scaling({-2, 3, .75});
    shear.m[4] = .4;
    shear.m[8] = .2;
    return Transform::translation({1000, -2000, 30}) * Transform::rotation({0, 0, 1}, .4) * shear;
}
QJsonObject worldPins(const Transform &world) {
    return {{"type", "pins"},
            {"points", QJsonArray{point(world.point({.1, .2, 0})), point(world.point({1.1, .2, 0})),
                                  point(world.point({.1, 1.2, 0}))}},
            {"coordinates", QJsonArray{QJsonArray{0, 0}, QJsonArray{2, 0}, QJsonArray{0, 3}}}};
}
void verifyWorldMapping(const Body &body, Id id) {
    const auto mapping = effectiveFaceTextureMapping(body, id);
    for (int x = 0; x < 7; ++x)
        for (int y = 0; y < 7; ++y) {
            const Vec3 p{.1 + x * .35, .2 + y * .25, 0};
            near(mapping.coordinates(p), {2 * (p.x - .1), 3 * (p.y - .2)},
                 "World projection has analytic local UV under reflection, shear and scale");
        }
}
void framesAndScopes() {
    Document doc;
    const auto body = square(doc), id = face(doc, body);
    const auto group = createGroup(doc, {body});
    doc.transform(group, placement());
    const auto world = doc.worldTransform(body);
    executeBatch(doc, batch(doc, {command(doc, body, worldPins(world), "front", "world")}));
    verifyWorldMapping(*doc.bodies().at(body), id);

    Document shared;
    const auto source = square(shared), sourceFace = face(shared, source);
    const auto made = createComponent(shared, source, "Panel");
    const auto other = placeComponent(shared, made.definition, placement());
    const auto member = made.movedGeometry.at(source);
    const auto first = shared.instances().at(made.instance)->members.at(member);
    const auto second = shared.instances().at(other.instance)->members.at(member);
    const auto map =
        command(shared, second, worldPins(shared.worldTransform(second)), "front", "world");
    rejects(shared, [&] { executeBatch(shared, batch(shared, {map})); });
    const auto history = shared.history().total;
    executeBatch(shared, batch(shared, {componentScopeCommand(shared, other.instance, {map})}));
    verifyWorldMapping(*shared.bodies().at(first), sourceFace);
    verifyWorldMapping(*shared.bodies().at(second), sourceFace);
    check(shared.history().total == history + 1,
          "Shared mapping publishes all placements in one Undo");
    const auto firstMapping = faceTextureMappings(*shared.bodies().at(first), sourceFace);
    const auto definition = shared.instances().at(made.instance)->definition;
    rejects(shared, [&] {
        executeBatch(shared,
                     batch(shared, {componentScopeCommand(shared, other.instance,
                                                          {command(shared, first, planar())})}));
    });
    executeBatch(
        shared,
        batch(shared, {QJsonObject{{"command", "component.edit_instance"},
                                   {"body", sid(other.instance)},
                                   {"commands", QJsonArray{command(shared, second, planar())}}}}));
    check(shared.instances().at(other.instance)->definition != definition &&
              faceTextureMappings(*shared.bodies().at(first), sourceFace) == firstMapping &&
              faceTextureMappings(*shared.bodies().at(second), sourceFace) != firstMapping,
          "Instance-scoped mapping makes unique and preserves the other placement");
    shared.undo();
    check(shared.instances().at(other.instance)->definition == definition &&
              faceTextureMappings(*shared.bodies().at(second), sourceFace) == firstMapping,
          "Undo restores shared definition identity and projection together");
}
void malformed() {
    Document doc;
    const auto body = square(doc);
    const auto good = command(doc, body, planar());
    std::vector<QJsonObject> bad;
    auto alter = [&](const char *key, QJsonValue value) {
        auto next = good;
        next[key] = value;
        bad.push_back(next);
    };
    alter("body", "01");
    alter("face", "0");
    alter("face", "9999");
    alter("side", "visible");
    alter("space", "parent");
    alter("projection", false);
    alter("extra", 1);
    for (const auto *key : {"body", "face", "side", "space", "projection"}) {
        auto next = good;
        next.remove(key);
        bad.push_back(next);
    }
    auto projected = [&](const char *key, QJsonValue value) {
        auto next = good;
        auto projection = planar();
        projection[key] = value;
        next["projection"] = projection;
        bad.push_back(next);
    };
    projected("type", "perspective");
    projected("normal", QJsonArray{0, 0, 0});
    projected("tangent", QJsonArray{0, 0, 1});
    projected("offset", QJsonArray{0});
    projected("offset", QJsonArray{0, 1e10});
    projected("origin", QJsonArray{1e7, 0, 0});
    projected("rotationRadians", "90 degrees");
    projected("extra", 0);
    for (double size : {0., 1e-7, -1e-7, 1e7})
        projected("width", size);
    auto missing = good;
    auto projection = planar();
    projection.remove("height");
    missing["projection"] = projection;
    bad.push_back(missing);
    auto degenerate = command(doc, body,
                              QJsonObject{{"type", "affine"},
                                          {"origin", QJsonArray{0, 0, 0}},
                                          {"uGradient", QJsonArray{1, 0, 0}},
                                          {"vGradient", QJsonArray{2, 0, 0}},
                                          {"offset", QJsonArray{0, 0}}});
    bad.push_back(degenerate);
    auto pins = worldPins(Transform{});
    pins["points"] = QJsonArray{QJsonArray{0, 0, 0}, QJsonArray{1, 0, 0}, QJsonArray{2, 0, 0}};
    bad.push_back(command(doc, body, pins));
    for (const auto &input : bad)
        rejects(doc, [&] { executeBatch(doc, batch(doc, {good, input})); });
    std::cout << bad.size() << " malformed mapping batches rejected atomically\n";
}
void stagedNative() {
    for (int scenario = 0; scenario < 3; ++scenario) {
        Document doc;
        const auto body = square(doc);
        Selection selection;
        if (scenario == 1)
            selection.lock(doc, body, true);
        QTemporaryDir files;
        NativeAssistantSession session(doc, files.path(), &selection);
        auto send = [&](QString name, QJsonObject args) {
            args["apiVersion"] = 1;
            args["documentId"] = QString::fromStdString(doc.identity());
            args["operation"] = name;
            return session.execute(args);
        };
        const auto bytes = encodeContainer(doc);
        const auto history = doc.history().total;
        const QJsonValue draft =
            send("transaction.begin", {{"expectedRevision", sid(doc.revision())}})["transactionId"];
        send("transaction.apply",
             {{"transactionId", draft},
              {"expectedVersion", 0},
              {"operationId", "map"},
              {"commands", QJsonArray{command(doc, body, planar(), "both"),
                                      command(doc, body, QJsonValue::Null, "back")}}});
        if (scenario == 1) {
            rejects(doc, [&] {
                send("transaction.preview", {{"transactionId", draft}, {"expectedVersion", 1}});
            });
            continue;
        }
        const auto sealed =
            send("transaction.preview", {{"transactionId", draft}, {"expectedVersion", 1}});
        check(encodeContainer(doc) == bytes && session.previewEdit(sealed)
                                                       ->snapshot()
                                                       .bodies()
                                                       .at(body)
                                                       ->faceTextureMappings.size() == 1,
              "Durable native preview contains private mapping-only change");
        const QJsonObject commit{{"requestId", sealed["requestId"]},
                                 {"payloadHash", sealed["payloadHash"]}};
        if (scenario == 2) {
            selection.lock(doc, body, true);
            rejects(doc, [&] { send("transaction.commit", commit); });
            continue;
        }
        send("transaction.commit", commit);
        send("transaction.commit", commit);
        check(doc.history().total == history + 1 &&
                  doc.bodies().at(body)->faceTextureMappings.size() == 1,
              "Durable mapping commit is idempotent and one Undo item");
        doc.undo();
        check(doc.bodies().at(body)->faceTextureMappings.empty(),
              "Native mapping is ordinarily undoable");
    }
}
void packagedExample() {
    QFile input(QStringLiteral(SOURCE_DIR "/examples/texture-mapping.json"));
    check(input.open(QIODevice::ReadOnly), "Packaged texture example opens");
    const auto commands = QJsonDocument::fromJson(input.readAll()).array();
    check(commands.size() == 6, "Complete texture example command list");
    Document doc;
    executeBatch(doc, batch(doc, commands));
    check(doc.history().total == 1 && doc.assets().size() == 1 && doc.materials().size() == 1,
          "Packaged image, material and projections form one modeling batch");
    const auto image = decodeTextureImage(*doc.assets().at(1));
    check(image.status == TextureImageStatus::Ready && image.image->width() == 2 &&
              image.image->height() == 2 && image.image->rgba()[7] == 0 &&
              image.image->rgba()[15] == 128,
          "Example contains a supported packaged image with zero and fractional alpha");
    const auto projections = faceTextureMappings(*doc.bodies().at(1), 5);
    check(projections.front && projections.back && projections.front != projections.back,
          "Example retains independent side projections");
    near(projections.front->coordinates({.25, .25, 1}), {.125, -.25},
         "Example planar origin and offset agree");
    near(projections.back->coordinates({.5, .75, 1}), {.5, .75},
         "Example world pins produce the expected mirrored back");
    const auto bytes = encodeContainer(doc);
    check(encodeContainer(decodeContainer(bytes)) == bytes,
          "Packaged example reopens without external resource resolution");
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        lifecycle();
        std::cout << "Mapping lifecycle passed\n";
        framesAndScopes();
        std::cout << "Frames and component scopes passed\n";
        malformed();
        stagedNative();
        packagedExample();
        std::cout << "Texture commands, inspection, affine frames, shared/unique scopes and "
                     "durable locks passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
