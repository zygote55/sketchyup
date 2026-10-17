#include "io/assembly_source.hpp"
#include "automation/commands.hpp"
#include "core/component_parameters.hpp"
#include "io/measured_drawing.hpp"
#include <QBuffer>
#include <QCryptographicHash>
#include <QImage>
#include <QImageWriter>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPainterPath>
#include <cmath>
#include <set>
namespace sketchy {
namespace {
void require(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}
QString text(const QJsonObject &object, const char *name, qsizetype limit) {
    const auto value = object.value(name);
    require(value.isString() && value.toString().toUtf8().size() <= limit,
            std::string("Assembly source field ") + name + " must be text");
    return value.toString();
}
} // namespace
AssemblySource parseAssemblySource(const QByteArray &json) {
    require(json.size() <= 1024 * 1024, "Assembly source exceeds 1 MiB");
    QJsonParseError error;
    const auto parsed = QJsonDocument::fromJson(json, &error);
    require(error.error == QJsonParseError::NoError && parsed.isObject(),
            "Assembly source is not a JSON object");
    const auto root = parsed.object();
    const QStringList fields{"format", "version", "license", "assembly", "name",
                             "description", "labels", "units", "commands"};
    for (auto it = root.begin(); it != root.end(); ++it)
        require(fields.contains(it.key()),
                "Assembly source has an unknown field " + it.key().toStdString());
    require(root["format"] == "sketchyup-assembly-source" && root["version"] == 1,
            "Unsupported assembly source format or version");
    // The shipped assemblies are authored by the project and licensed like the repository.
    require(root["license"] == "MIT", "Assembly sources must be MIT-licensed");
    AssemblySource result;
    require(root["assembly"].isObject(), "Assembly source field assembly must be an object");
    const auto assembly = root["assembly"].toObject();
    const auto id = text(assembly, "id", 129).toStdString();
    require(assembly.size() == 2 && assembly["version"].isDouble() &&
                validComponentAssembly(id, assembly["version"].toDouble()),
            "Assembly identity must be {id, version} with a well-formed id");
    result.identity = {id, std::uint32_t(assembly["version"].toDouble())};
    result.metadata.name = text(root, "name", 256);
    result.metadata.description = text(root, "description", 4096);
    require(root["labels"].isArray(), "Assembly source field labels must be an array");
    for (const auto &label : root["labels"].toArray()) {
        require(label.isString(), "Assembly labels must be text");
        result.metadata.labels.append(label.toString());
    }
    const auto units = root["units"].toString();
    require(units == "mm" || units == "m", "Assembly source units must be mm or m");
    result.units = units == "mm" ? DisplayUnit::Millimeters : DisplayUnit::Meters;
    require(root["commands"].isArray() && !root["commands"].toArray().isEmpty(),
            "Assembly source needs a commands array");
    result.commands = root["commands"].toArray();
    return result;
}
Document buildAssemblyDocument(const AssemblySource &source, Id &definition) {
    Document doc(source.units);
    executeBatch(doc, {{"apiVersion", 1},
                       {"documentId", QString::fromStdString(doc.identity())},
                       {"expectedRevision", QString::number(doc.revision())},
                       {"commands", source.commands},
                       {"history", QJsonObject{{"label", "Build " + source.metadata.name}}}});
    Id placement = 0;
    for (const auto &[id, body] : doc.bodies()) {
        if (body->parent)
            continue;
        require(!placement && doc.instances().contains(id),
                "An assembly source must build exactly one top-level component placement");
        placement = id;
    }
    require(placement != 0, "An assembly source must build one component placement");
    definition = doc.instances().at(placement)->definition;
    require(doc.definitions().at(definition)->parameters != nullptr,
            "An assembly's definition must have parameters");
    setComponentAssembly(doc, definition, source.identity);
    return doc;
}
QByteArray drawAssemblyThumbnail(const Document &document) {
    constexpr int width = 320, height = 240;
    constexpr double margin = 16;
    // An isometric view from the front left, above: the faces at -y face the viewer.
    Vec3 low{1e9, 1e9, 1e9}, high{-1e9, -1e9, -1e9};
    for (const auto &[id, body] : document.bodies()) {
        if (body->kind != BodyKind::Geometry)
            continue;
        const auto world = document.worldTransform(id);
        for (const auto &[vertex, point] : body->surface.vertices) {
            (void)vertex;
            const auto p = world.point(point);
            low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
            high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
        }
    }
    require(low.x <= high.x, "An assembly has no geometry to draw");
    const auto centre = (low + high) * .5;
    const auto extent = std::max({high.x - low.x, high.y - low.y, high.z - low.z, .01});
    RenderCamera camera;
    camera.orthographic = true;
    camera.target = centre;
    camera.position = centre + normalized(Vec3{-1, -1.4, 1}) * (extent * 4);
    camera.up = {0, 0, 1};
    camera.yMag = extent;
    camera.farClip = extent * 20;
    RenderOptions options;
    options.settings.width = width;
    options.settings.height = height;
    options.camera = camera;
    const auto snapshot = RenderSnapshot::capture(document, options);
    MeasuredPage page;
    page.widthMm = page.heightMm = 2000;
    page.marginMm = 0;
    page.scaleDenominator = std::max(1., extent * 1000 / 500);
    const auto drawing = captureMeasuredDrawing(snapshot, page);
    double minX = 1e18, minY = 1e18, maxX = -1e18, maxY = -1e18;
    for (const auto &line : drawing.geometry.lines) {
        if (line.hidden)
            continue;
        for (const auto &p : {line.a, line.b}) {
            minX = std::min(minX, p.x);
            maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y);
            maxY = std::max(maxY, p.y);
        }
    }
    require(minX <= maxX, "An assembly drawing has no visible lines");
    const auto scale = std::min((width - 2 * margin) / std::max(maxX - minX, 1e-9),
                                (height - 2 * margin) / std::max(maxY - minY, 1e-9));
    const auto offsetX = (width - (maxX - minX) * scale) / 2,
               offsetY = (height - (maxY - minY) * scale) / 2;
    // Page millimetres grow downward, as the image's y does.
    auto map = [&](Vec3 p) {
        return QPointF(offsetX + (p.x - minX) * scale, offsetY + (p.y - minY) * scale);
    };
    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(Qt::white);
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        QPainterPath path;
        for (const auto &line : drawing.geometry.lines)
            if (!line.hidden) {
                path.moveTo(map(line.a));
                path.lineTo(map(line.b));
            }
        painter.setPen(QPen(QColor("#263a32"), 1.25, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(path);
    }
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, "png");
    require(writer.write(image), "The assembly thumbnail could not be encoded");
    return png;
}
std::string assemblyDocumentIdentity(const ComponentAssembly &identity) {
    const auto key = QByteArray::fromStdString(identity.id + "#" + std::to_string(identity.version));
    return QCryptographicHash::hash("sketchyup-assembly:" + key, QCryptographicHash::Sha256)
        .toHex()
        .left(32)
        .toStdString();
}
QByteArray buildAssemblyBundle(const QByteArray &sourceJson) {
    const auto source = parseAssemblySource(sourceJson);
    Id definition = 0;
    const auto document = buildAssemblyDocument(source, definition);
    // The thumbnail shows the assembly at its defaults, as the bundle stores it.
    const auto captured =
        captureLibraryComponent(document, definition, assemblyDocumentIdentity(source.identity));
    const auto thumbnail = drawAssemblyThumbnail(captured);
    return encodeComponentBundle(document, definition, source.metadata, thumbnail,
                                 assemblyDocumentIdentity(source.identity));
}
} // namespace sketchy
