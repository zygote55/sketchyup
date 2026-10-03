#include "automation/commands.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <QJsonDocument>
#include <iostream>
using namespace sketchy;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F fn) {
    bool rejected = false;
    try {
        fn();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected, "Invalid command must reject");
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        Document source;
        source.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
        const auto beforeQueries = encodeDocument(source);
        check(executeQuery(source, {{"query", "document.describe"}}).contains("bodies"),
              "Document query registered");
        check(executeQuery(source, {{"query", "capabilities"}}).contains("commandSchemas"),
              "Capabilities query registered");
        check(executeQuery(source, {{"query", "commands.describe"}, {"name", "geometry.translate"}})
                  .contains("parameters"),
              "Command schema query registered");
        check(encodeDocument(source) == beforeQueries, "Queries never mutate model or revision");
        rejects(
            [&] { executeQuery(source, {{"query", "document.describe"}, {"mutation", true}}); });
        QJsonArray matrix;
        for (auto value : Transform::translation({2, 0, 0}).m)
            matrix.append(value);
        const QJsonArray cases{
            QJsonObject{{"command", "geometry.face"},
                        {"loops", QJsonArray{QJsonArray{QJsonArray{0, 0, 0}, QJsonArray{1, 0, 0},
                                                        QJsonArray{0, 1, 0}}}}},
            QJsonObject{{"command", "geometry.extrude_isolated"},
                        {"body", "1"},
                        {"face", "5"},
                        {"distance", 2}},
            QJsonObject{
                {"command", "geometry.translate"}, {"body", "1"}, {"delta", QJsonArray{1, 0, 0}}},
            QJsonObject{{"command", "geometry.delete"}, {"body", "1"}},
            QJsonObject{
                {"command", "material.color"}, {"body", "1"}, {"color", QJsonArray{.1, .2, .3}}},
            QJsonObject{{"command", "scene.transform"}, {"body", "1"}, {"matrix", matrix}}};
        check(commandCatalog().size() == cases.size(),
              "All published commands have executable cases");
        for (auto value : cases) {
            auto command = value.toObject();
            auto descriptor = commandDescription(command["command"].toString());
            check(!descriptor["label"].toString().isEmpty(), "Human command label exists");
            auto schema = descriptor["parameters"].toObject();
            auto request = [&](const Document &doc, QJsonObject item) {
                return QJsonObject{{"apiVersion", 1},
                                   {"documentId", QString::fromStdString(doc.identity())},
                                   {"expectedRevision", QString::number(doc.revision())},
                                   {"commands", QJsonArray{item}}};
            };
            Document doc = source;
            const auto original = encodeDocument(doc);
            for (auto key : schema["required"].toArray()) {
                auto missing = command;
                missing.remove(key.toString());
                rejects([&] { executeBatch(doc, request(doc, missing)); });
                check(encodeDocument(doc) == original, "Missing field never mutates document");
            }
            auto unknown = command;
            unknown["unrecognized"] = true;
            rejects([&] { executeBatch(doc, request(doc, unknown)); });
            check(encodeDocument(doc) == original, "Unknown field never mutates document");
            const auto revision = doc.revision();
            executeBatch(doc, request(doc, command));
            check(doc.revision() == revision + 1, "Registered command commits once");
            doc.undo();
            auto expectedBody = *source.bodies().at(1);
            check(doc.bodies().at(1)->surface.nextId >= expectedBody.surface.nextId,
                  "Undo retains surface allocator high-water mark");
            expectedBody.surface.nextId = doc.bodies().at(1)->surface.nextId;
            check(doc.bodies().size() == source.bodies().size() &&
                      *doc.bodies().at(1) == expectedBody,
                  "Registered command undo restores source geometry and metadata");
        }
        Document color = source;
        rejects([&] {
            executeBatch(
                color,
                {{"apiVersion", 1},
                 {"documentId", QString::fromStdString(color.identity())},
                 {"expectedRevision", QString::number(color.revision())},
                 {"commands", QJsonArray{QJsonObject{{"command", "material.color"},
                                                     {"body", "1"},
                                                     {"color", QJsonArray{1.00000001, 0, 0}}}}}});
        });
        check(color.revision() == source.revision(),
              "Out-of-range double color cannot round into range");
        rejects([] { commandDescription("internal.commit"); });
        std::cout << "Command catalog, required/unknown fields, handlers, single-step undo and "
                     "color bounds passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
