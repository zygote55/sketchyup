#include "app/viewport.hpp"
#include "automation/component_scope.hpp"
#include "automation/reference_image_commands.hpp"
namespace sketchy {
std::vector<Triangle> Viewport::displayTriangles(const Body &body) {
    if (!body.referenceImage)
        return body.surface.triangles();
    const auto corners = referenceImageCorners(*body.referenceImage, {});
    return {{corners[0], corners[1], corners[2], 0}, {corners[0], corners[2], corners[3], 0}};
}
void Viewport::referenceVertices(const Body &body, const Triangle &local,
                                 std::array<Vertex, 3> &vertices) const {
    textureVertices(body, local, false, vertices);
    for (auto &vertex : vertices) {
        vertex.reference = 1;
        vertex.light = vertex.backLight = 1;
        vertex.a = vertex.ba = float(body.referenceImage->opacity);
        // Missing or undecodable pixels have a visible, intentionally distinct placeholder.
        vertex.r = vertex.br = vertex.image ? 1.f : .65f;
        vertex.g = vertex.bg = vertex.image ? 1.f : .25f;
        vertex.b = vertex.bb = vertex.image ? 1.f : .7f;
    }
}
void Viewport::editReferenceImages(const QJsonArray &commands) {
    for (auto value : commands)
        if (!isReferenceImageCommand(value.toObject()["command"].toString()))
            throw std::runtime_error("Expected reference image edit");
    cancel();
    commitCommands(commands);
    refresh();
    emit changed();
}
void Viewport::importReferenceImage(const QByteArray &data, const QString &mediaType,
                                    const QString &name, double width, double height) {
    const auto scope = componentScope();
    const QJsonArray create{QJsonObject{{"command", "reference_image.create"},
                                        {"name", name},
                                        {"asset", QString::number(doc_.nextAssetId())},
                                        {"width", width},
                                        {"height", height},
                                        {"parent", QString::number(selection_.context())}}};
    const QJsonArray commands{QJsonObject{{"command", "asset.import"},
                                          {"name", name},
                                          {"mediaType", mediaType},
                                          {"data", QString::fromLatin1(data.toBase64())}},
                              scope ? componentScopeCommand(doc_, scope, create)
                                    : create[0].toObject()};
    cancel();
    const auto result = componentScopeResult(commitCommands(commands, false), scope);
    refresh();
    const auto created = result["created"].toArray();
    if (!created.empty())
        setSelection(created.last().toString().toULongLong());
    emit changed();
}
} // namespace sketchy
