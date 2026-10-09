#include "automation/extension.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <iostream>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F run) {
    try {
        run();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Expected extension rejection");
}
QByteArray encode(const QJsonObject &object) {
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QFile sample(QStringLiteral(SOURCE_DIR) + "/examples/extensions/panel.sketchyext");
        check(sample.open(QIODevice::ReadOnly), "Installed sample source");
        QFile catalog(QStringLiteral(SOURCE_DIR) + "/docs/api/extensions-v1.json");
        check(catalog.open(QIODevice::ReadOnly) &&
                  QJsonDocument::fromJson(catalog.readAll()).object() == extensionCapabilities(),
              "Installed extension capability catalog matches the public registry");
        const auto bytes = sample.readAll();
        const auto manifest = parseExtensionManifest(bytes);
        check(manifest.id == "org.sketchyup.panel" &&
                  manifest.commands == QStringList{"geometry.face"} && manifest.actions.size() == 1,
              "Sample declares public command capabilities and one action");
        const auto commands = resolveExtensionAction(
            manifest, "create-panel", {{"width", 3}, {"height", 2}, {"name", "My panel"}});
        Document doc;
        const auto result =
            executeBatch(doc, {{"apiVersion", 1},
                               {"documentId", QString::fromStdString(doc.identity())},
                               {"expectedRevision", QString::number(doc.revision())},
                               {"commands", commands}});
        check(
            !result.empty() && doc.bodies().size() == 1 && doc.history().total == 1 &&
                doc.bodies().begin()->second->name == "My panel" &&
                std::abs(doc.worldArea(doc.bodies().begin()->first,
                                       doc.bodies().begin()->second->surface.faces.begin()->first) -
                         6) < 1e-9,
            "Sample uses public atomic commands and produces a measured six-square-metre face");
        doc.undo();
        check(doc.bodies().empty(), "One undo removes extension output");
        doc.redo();
        check(doc.bodies().size() == 1, "Redo restores extension output");
        check(resolveExtensionAction(manifest, "create-panel", {})[0].toObject()["name"] == "Panel",
              "Typed defaults resolved");
        rejects([&] { resolveExtensionAction(manifest, "absent", {}); });
        for (const auto input :
             {QJsonObject{{"width", 0}}, QJsonObject{{"width", "3"}}, QJsonObject{{"extra", 1}},
              QJsonObject{{"name", QString(257, 'x')}}})
            rejects([&] { resolveExtensionAction(manifest, "create-panel", input); });
        const auto original = QJsonDocument::fromJson(bytes).object();
        for (const auto field : {"manifestVersion", "commandApiVersion"}) {
            auto future = original;
            future[field] = 2;
            rejects([&] { parseExtensionManifest(encode(future)); });
        }
        auto altered = original;
        altered["entrypoint"] = "run.sh";
        rejects([&] { parseExtensionManifest(encode(altered)); });
        altered = original;
        altered["execution"] = "shell";
        rejects([&] { parseExtensionManifest(encode(altered)); });
        altered = original;
        altered["capabilities"] = QJsonObject{{"commands", QJsonArray{"missing.command"}}};
        rejects([&] { parseExtensionManifest(encode(altered)); });
        altered = original;
        altered["capabilities"] =
            QJsonObject{{"commands", QJsonArray{"geometry.face", "geometry.face"}}};
        rejects([&] { parseExtensionManifest(encode(altered)); });
        auto action = original["actions"].toArray()[0].toObject();
        auto hiddenCommand = action;
        hiddenCommand["commands"] = QJsonArray{QJsonObject{
            {"command", "geometry.face"}, {"nested", QJsonObject{{"command", "document.units"}}}}};
        altered = original;
        altered["actions"] = QJsonArray{hiddenCommand};
        rejects([&] { parseExtensionManifest(encode(altered)); });
        auto dynamic = action;
        dynamic["commands"] =
            QJsonArray{QJsonObject{{"command", QJsonObject{{"$parameter", "name"}}}}};
        altered = original;
        altered["actions"] = QJsonArray{dynamic};
        rejects([&] { parseExtensionManifest(encode(altered)); });
        rejects([&] { parseExtensionManifest(QByteArray(extensionManifestLimit + 1, ' ')); });
        rejects([&] { parseExtensionManifest("{}"); });
        // Struct fields cannot replace the validated, retained manifest source.
        auto forged = manifest;
        forged.actions = QJsonArray{hiddenCommand};
        forged.commands.append("document.units");
        check(resolveExtensionAction(forged, "create-panel", {}) ==
                  resolveExtensionAction(manifest, "create-panel", {}),
              "Resolution uses authoritative validated manifest bytes");
        const auto before = encodeContainer(doc);
        const auto history = doc.history().total;
        auto failing = commands;
        failing.append(
            QJsonObject{{"command", "geometry.face"},
                        {"loops", QJsonArray{QJsonArray{QJsonArray{0, 0, 0}, QJsonArray{0, 0, 0},
                                                        QJsonArray{0, 0, 0}}}}});
        rejects([&] {
            executeBatch(doc, {{"apiVersion", 1},
                               {"documentId", QString::fromStdString(doc.identity())},
                               {"expectedRevision", QString::number(doc.revision())},
                               {"commands", failing}});
        });
        check(encodeContainer(doc) == before && doc.history().total == history,
              "Failed extension geometry batch cannot partially edit the model");
        check(extensionCapabilities()["externalCode"] == false &&
                  extensionCapabilities()["atomicEdits"] == true,
              "Declared extension execution limits are discoverable");
        std::cout << "Extension manifests: typed parameters, capability checks, version rejection "
                     "and atomic public sample passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
