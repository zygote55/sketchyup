#include "automation/commands.hpp"
#include "automation/inspection.hpp"
#include "automation/reference_image_commands.hpp"
#include "automation/staging.hpp"
#include "core/components.hpp"
#include "io/document_io.hpp"
#include "io/texture_image.hpp"
#include <QCoreApplication>
#include <QJsonDocument>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F operation) {
    try {
        operation();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Invalid reference image request accepted");
}
QJsonObject batch(const Document &doc, QJsonArray commands) {
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(doc.identity())},
            {"expectedRevision", QString::number(doc.revision())},
            {"commands", commands}};
}
QJsonObject query(const Document &doc, QString name, QJsonObject arguments = {}) {
    arguments["apiVersion"] = 1;
    arguments["documentId"] = QString::fromStdString(doc.identity());
    arguments["expectedRevision"] = QString::number(doc.revision());
    arguments["query"] = name;
    return arguments;
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        Document doc;
        const auto png = encodeTexturePng(TextureImage(2, 1, {255, 0, 0, 255, 0, 255, 0, 128}));
        const QJsonObject import{{"command", "asset.import"},
                                 {"name", "Reference pixels"},
                                 {"mediaType", "image/png"},
                                 {"data", QString::fromLatin1(png.toBase64())}};
        const QJsonObject create{{"command", "reference_image.create"},
                                 {"name", "Plan"},
                                 {"asset", "1"},
                                 {"width", 4},
                                 {"height", 2},
                                 {"opacity", .8},
                                 {"position", QJsonArray{10, 20, 0}}};
        const auto original = encodeContainer(doc);
        const auto request = batch(doc, {import, create});
        previewBatch(doc, request);
        check(encodeContainer(doc) == original,
              "Image and asset preview leaves live state unchanged");
        StagingSession staging;
        const auto prepared = staging.prepare(doc, request);
        const auto token = prepared["stageId"].toString();
        const auto proposal = staging.proposal(doc, token);
        check(doc.assets().empty() && proposal->snapshot().assets().size() == 1 &&
                  proposal->snapshot().bodies().at(1)->referenceImage->width == 4,
              "Private image proposal retains pixels and placement separately from live state");
        check(staging.changes(doc, token)["changes"].toArray().size() >= 2,
              "Stage reports image and asset changes");
        doc.applyPrepared(*proposal);
        check(doc.history().total == 1, "Image plus managed asset publish as one history entry");
        doc.undo();
        check(doc.assets().empty() && doc.bodies().empty() && !doc.dirty(),
              "One Undo removes both image and imported pixels");
        doc.redo();
        const auto frozen = encodeContainer(doc);
        auto detailRequest = query(doc, "reference_image.describe", {{"body", "1"}});
        const auto detail = inspectDocument(doc, detailRequest)["data"].toObject();
        check(detail["pixels"].toObject()["status"] == "ready" &&
                  detail["pixels"].toObject()["width"] == 2 &&
                  detail["pixels"].toObject()["height"] == 1,
              "Image inspection decodes bounded original pixels");
        const auto page =
            inspectDocument(doc, query(doc, "reference_images.query"))["data"].toObject();
        check(page["total"] == 1 && !page["items"].toArray()[0].toObject().contains("pixels"),
              "Reference listing is bounded and does not decode every asset");
        check(inspectDocument(doc,
                              query(doc, "entities.query", {{"kind", "reference_image"}}))["data"]
                      .toObject()["total"] == 1,
              "Entity kind filter distinguishes image planes from geometry");
        check(encodeContainer(doc) == frozen, "Image inspection is read-only");
        const QJsonObject calibrate{{"command", "reference_image.calibrate"},
                                    {"body", "1"},
                                    {"first", QJsonArray{.25, .5}},
                                    {"second", QJsonArray{.75, .5}},
                                    {"knownLength", 5}};
        executeBatch(doc, batch(doc, {calibrate}));
        check(doc.bodies().at(1)->referenceImage->width == 10 &&
                  doc.worldTransform(1).point({2.5, 2.5, 0}) == Vec3{11, 21, 0},
              "Shared calibration preserves the first world anchor");
        rejects([&] { inspectDocument(doc, detailRequest); });
        const auto stable = encodeContainer(doc);
        for (const auto &change : {QJsonObject{{"opacity", 1.1}}, QJsonObject{{"width", 0}},
                                   QJsonObject{{"asset", "999"}}, QJsonObject{{"name", ""}},
                                   QJsonObject{{"path", "/external.png"}}}) {
            auto invalid = change;
            invalid["command"] = "reference_image.update";
            invalid["body"] = "1";
            rejects([&] { executeBatch(doc, batch(doc, {invalid})); });
            check(encodeContainer(doc) == stable, "Invalid image edits reject atomically");
        }
        rejects([&] {
            executeBatch(doc, batch(doc, {QJsonObject{{"command", "reference_image.update"},
                                                      {"body", "1"}}}));
        });
        rejects([&] {
            executeBatch(doc, batch(doc, {QJsonObject{{"command", "reference_image.update"},
                                                      {"body", "1"},
                                                      {"opacity", .8}}}));
        });
        auto invalidCalibration = calibrate;
        invalidCalibration["second"] = invalidCalibration["first"];
        rejects([&] { executeBatch(doc, batch(doc, {invalidCalibration})); });
        rejects([&] {
            executeBatch(
                doc, batch(doc, {QJsonObject{{"command", "reference_image.update"},
                                             {"body", "1"},
                                             {"opacity", .5}},
                                 QJsonObject{{"command", "geometry.delete"}, {"body", "999"}}}));
        });
        check(encodeContainer(doc) == stable, "Later command failure rolls back image changes");
        const auto component = createComponent(doc, 1, "Image component");
        const auto second =
            placeComponent(doc, component.definition, Transform::translation({20, 0, 0}));
        Id firstImage{}, otherImage{};
        for (const auto &[id, body] : doc.bodies())
            if (body->referenceImage) {
                if (body->parent == component.instance)
                    firstImage = id;
                if (body->parent == second.instance)
                    otherImage = id;
            }
        check(firstImage && otherImage, "Both image placements exist");
        executeBatch(doc,
                     batch(doc, {QJsonObject{{"command", "component.edit_instance"},
                                             {"body", QString::number(component.instance)},
                                             {"commands", QJsonArray{QJsonObject{
                                                              {"command", "reference_image.update"},
                                                              {"body", QString::number(firstImage)},
                                                              {"opacity", .2}}}}}}));
        check(doc.bodies().at(firstImage)->referenceImage->opacity == .2 &&
                  doc.bodies().at(otherImage)->referenceImage->opacity == .8,
              "Instance reference editing leaves other component placements unchanged");
        doc.undo();
        check(doc.bodies().at(firstImage)->referenceImage->opacity == .8,
              "Instance reference edit Undo restores the shared placement");
        const auto encoded = encodeContainer(doc);
        check(encodeContainer(decodeContainer(encoded)) == encoded,
              "Command-authored component images roundtrip exactly");
        std::cout
            << "Reference image commands, inspection, atomic staging and instance scopes passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
