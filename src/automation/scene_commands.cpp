#include "automation/scene_commands.hpp"
#include "io/scenes_io.hpp"
#include <QJsonArray>
namespace sketchy {
namespace {
Id id(const QJsonValue &value) {
    bool ok{};
    const auto text = value.toString();
    const auto result = text.toULongLong(&ok);
    if (!value.isString() || !ok || !result || QString::number(result) != text)
        throw std::runtime_error("Saved scene requires a canonical nonzero decimal identity");
    return result;
}
std::string name(const QJsonValue &value) {
    if (!value.isString())
        throw std::runtime_error("Saved scene name requires text");
    return value.toString().toStdString();
}
QJsonObject object(QJsonObject properties) {
    QJsonArray required;
    for (const auto &key : properties.keys())
        required.append(key);
    return {{"type", "object"},
            {"properties", properties},
            {"required", required},
            {"additionalProperties", false}};
}
QJsonObject list(QJsonObject items, int minimum, int maximum) {
    return {{"type", "array"}, {"items", items}, {"minItems", minimum}, {"maxItems", maximum}};
}
QJsonObject number(double minimum, double maximum) {
    return {{"type", "number"}, {"minimum", minimum}, {"maximum", maximum}};
}
} // namespace
bool isSavedSceneCommand(const QString &name) {
    return name == "saved_scene.create" || name == "saved_scene.rename" ||
           name == "saved_scene.update" || name == "saved_scene.reorder" ||
           name == "saved_scene.delete" || name == "saved_scene.recall";
}
void executeSavedSceneCommand(Document &doc, const QJsonObject &command) {
    const auto operation = command["command"].toString();
    if (operation == "saved_scene.create")
        createScene(doc, name(command["name"]), decodeSceneSnapshot(command["snapshot"]));
    else if (operation == "saved_scene.rename")
        renameScene(doc, id(command["scene"]), name(command["name"]));
    else if (operation == "saved_scene.update")
        updateScene(doc, id(command["scene"]), decodeSceneSnapshot(command["snapshot"]));
    else if (operation == "saved_scene.delete")
        eraseScene(doc, id(command["scene"]));
    else if (operation == "saved_scene.recall")
        recallSceneModel(doc, id(command["scene"]));
    else if (operation == "saved_scene.reorder") {
        const auto value = command["order"];
        if (!value.isArray() || value.toArray().size() > qsizetype(sceneCountLimit))
            throw std::runtime_error("Saved scene order requires a bounded identity array");
        std::vector<Id> order;
        for (const auto &entry : value.toArray())
            order.push_back(id(entry));
        reorderScenes(doc, order);
    } else
        throw std::runtime_error("Unknown saved scene command");
}
QJsonObject savedSceneSnapshotSchema(const QJsonObject &styleSchema) {
    const QJsonObject identity{{"type", "string"}, {"pattern", "^[1-9][0-9]*$"}, {"maxLength", 20}};
    const QJsonObject flag{{"type", "boolean"}};
    const auto visible = object({{"id", identity}, {"visible", flag}});
    const auto body = object({{"body", identity},
                              {"kind", QJsonObject{{"const", "body"}}},
                              {"entity", QJsonObject{{"type", "null"}}}});
    const auto entity = object(
        {{"body", identity},
         {"kind", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"face", "edge", "guide"}}}},
         {"entity", identity}});
    auto hidden = list(QJsonObject{{"oneOf", QJsonArray{body, entity}}}, 0, 50000);
    hidden["uniqueItems"] = true;
    const auto visibility = object({{"bodies", list(visible, 0, 10000)},
                                    {"tags", list(visible, 0, 1024)},
                                    {"hidden", hidden},
                                    {"showHidden", flag}});
    auto result = object(
        {{"camera", object({{"target", list(number(-1e9, 1e9), 3, 3)},
                            {"yaw", number(-180, 180)},
                            {"pitch", number(-90, 90)},
                            {"distance", number(.05, 1e7)},
                            {"fieldOfView", number(5, 120)},
                            {"orthographic", flag}})},
         {"visibility", visibility},
         {"style", styleSchema},
         {"section",
          object({{"plane", QJsonObject{{"oneOf", QJsonArray{QJsonObject{{"type", "null"}},
                                                             list(number(-1e9, 1e9), 4, 4)}}}}})}});
    result["required"] = QJsonArray{};
    result["minProperties"] = 1;
    return result;
}
QJsonObject savedSceneSummary(const Document &doc, Id id) {
    if (!doc.scenes().contains(id))
        throw std::runtime_error("Saved scene does not exist");
    const auto &scene = *doc.scenes().at(id);
    const auto &snapshot = scene.snapshot;
    QJsonArray properties;
    if (snapshot.camera)
        properties.append("camera");
    if (snapshot.visibility)
        properties.append("visibility");
    if (snapshot.style)
        properties.append("style");
    if (snapshot.section)
        properties.append("section");
    const auto missing = missingSceneReferences(doc, snapshot);
    return {{"id", QString::number(id)},
            {"name", QString::fromStdString(scene.name)},
            {"position", int(scene.position)},
            {"properties", properties},
            {"missingReferences", QJsonObject{{"bodies", int(missing.bodies.size())},
                                              {"tags", int(missing.tags.size())},
                                              {"entities", int(missing.entities.size())}}}};
}
QJsonObject savedSceneDescription(const Document &doc, Id id) {
    auto result = savedSceneSummary(doc, id);
    auto snapshot = doc.scenes().at(id)->snapshot;
    const auto visibility = snapshot.visibility;
    snapshot.visibility.reset();
    result["snapshot"] = snapshot.camera || snapshot.style || snapshot.section
                             ? encodeSceneSnapshot(snapshot)
                             : QJsonObject{};
    if (visibility)
        result["visibility"] = QJsonObject{{"bodies", int(visibility->bodyVisible.size())},
                                           {"tags", int(visibility->tagVisible.size())},
                                           {"hidden", int(visibility->hiddenEntities.size())},
                                           {"showHidden", visibility->showHidden}};
    result["recall"] = QJsonObject{
        {"model", "saved_scene.recall applies opted-in style and intrinsic visibility as one "
                  "undoable batch"},
        {"editor",
         "Camera, editor-hidden entities, show-hidden and section are navigation state; native "
         "recall applies these separately without adding navigation to model history"}};
    return result;
}
} // namespace sketchy
