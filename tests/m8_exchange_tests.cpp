#include "automation/commands.hpp"
#include "automation/extension_worker.hpp"
#include "core/components.hpp"
#include "core/entity_measure.hpp"
#include "core/materials.hpp"
#include "integrations/glb_export.hpp"
#include "io/component_library.hpp"
#include "io/dxf_export.hpp"
#include "io/gltf_import.hpp"
#include "io/measured_export.hpp"
#include "io/obj_export.hpp"
#include "io/stl_export.hpp"
#include "io/texture_image.hpp"
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void near(double actual, double expected) {
    check(std::isfinite(actual) && std::abs(actual - expected) < 1e-5,
          "Exchange study measured geometry differs");
}
void shape(const Document &document) {
    Vec3 low{1e9, 1e9, 1e9}, high{-1e9, -1e9, -1e9};
    double area{};
    size_t triangles{};
    for (const auto &[id, body] : document.bodies()) {
        const auto world = document.worldTransform(id);
        for (const auto &[vertex, local] : body->surface.vertices) {
            (void)vertex;
            const auto point = world.point(local);
            low = {std::min(low.x, point.x), std::min(low.y, point.y), std::min(low.z, point.z)};
            high = {std::max(high.x, point.x), std::max(high.y, point.y),
                    std::max(high.z, point.z)};
        }
        for (const auto &[face, record] : body->surface.faces) {
            (void)record;
            area += document.worldArea(id, face);
        }
        triangles += body->surface.triangles().size();
    }
    near(low.x, 0);
    near(low.y, 0);
    near(low.z, 0);
    near(high.x, 2);
    near(high.y, 3);
    near(high.z, 4);
    near(area, 2 * (2 * 3 + 2 * 4 + 3 * 4));
    check(triangles == 12, "Box remains twelve real surface triangles");
}
QByteArray read(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Read study artifact");
    return file.readAll();
}
} // namespace
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    try {
        QTemporaryDir scratch;
        check(scratch.isValid(), "Exchange study scratch");
        QString root = scratch.path();
        if (argc == 2) {
            root = QString::fromLocal8Bit(argv[1]);
            check(!QFileInfo::exists(root) && QDir().mkpath(root), "Study destination must be new");
        }
        const auto path = [&](const QString &name) { return root + "/" + name; };
        Document document(DisplayUnit::Millimeters);
        const auto body = document.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}});
        document.extrude(body, document.bodies().at(body)->surface.faces.begin()->first, 4);
        const auto material = createMaterial(document, "Study blue", {.2f, .4f, .8f}, 1);
        assignMaterial(document, body, {}, material);
        const auto component = createComponent(document, body, "Exchange block");
        const auto source = encodeContainer(document);
        const auto history = document.history().total;
        shape(document);
        const auto thumb = encodeTexturePng(TextureImage(1, 1, {51, 102, 204, 255}));
        const auto templateBytes =
            encodeTemplateBundle(document, {"Exchange study", "2 × 3 × 4 m", {"M8"}, 0}, thumb);
        writeTemplateBundle(templateBytes, path("study.sketchylib"));
        auto fromTemplate = instantiateTemplate(loadTemplateBundle(path("study.sketchylib")));
        shape(fromTemplate);
        check(fromTemplate.identity() != document.identity() && fromTemplate.dirty() &&
                  !fromTemplate.canUndo(),
              "Template opens as a fresh unsaved native model");
        const auto componentBytes = encodeComponentBundle(
            document, component.definition,
            {"Exchange block", "Contained reusable geometry", {"M8"}, 0}, thumb);
        writeComponentBundle(componentBytes, path("block.sketchylib"));
        Document inserted;
        insertLibraryComponent(inserted, loadComponentBundle(path("block.sketchylib")));
        shape(inserted);
        check(inserted.materials().size() == 1 && inserted.history().total == 1,
              "Library insertion carries appearance in one edit");
        inserted.undo();
        check(inserted.bodies().empty(), "Library insertion undoes completely");
        inserted.redo();
        shape(inserted);
        const auto snapshot = RenderSnapshot::capture(fromTemplate);
        writeGlbExport(exportGlb(snapshot), path("glb"));
        shape(loadGltf(path("glb/scene.glb")).document);
        writeObjExport(exportObj(fromTemplate, {.001, ObjUpAxis::Z}), path("obj"));
        shape(loadObj(path("obj/model.obj"), {.001, ObjUpAxis::Z}).document);
        for (const auto encoding : {StlEncoding::Binary, StlEncoding::Ascii}) {
            const auto name = encoding == StlEncoding::Binary ? "binary.stl" : "ascii.stl";
            writeStlExport(exportStl(fromTemplate, {.001, StlUpAxis::Z}, encoding), path(name));
            auto imported = loadStl(path(name), {.001, StlUpAxis::Z}, {StlWeld::Exact});
            shape(imported.document);
            const auto geometry = imported.report["geometry"].toArray().first().toObject();
            check(geometry["solidStatus"] == "solid", "Exact STL import reconstructs a solid");
            near(geometry["materialVolume"].toDouble(), 2 * 3 * 4);
        }
        Document footprint;
        footprint.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}});
        writeDxfExport(exportDxf(footprint, .001), path("footprint.dxf"));
        auto wires = loadDxf(path("footprint.dxf"));
        check(!wires.document.bodies().empty(), "DXF footprint becomes editable native wires");
        double perimeter{};
        for (const auto &[id, record] : wires.document.bodies()) {
            (void)record;
            perimeter += measureEntity(wires.document, {id, SelectionKind::Body, 0}).world.length;
        }
        near(perimeter, 2 * (2 + 3));
        RenderOptions options;
        options.camera = RenderCamera{{1, 1.5, 10}, {1, 1.5, 0}, {0, 1, 0}, true};
        MeasuredPage page;
        page.scaleDenominator = 50;
        const auto drawing =
            captureMeasuredDrawing(RenderSnapshot::capture(fromTemplate, options), page);
        check(!drawing.geometry.lines.empty(), "Measured view contains visible geometry");
        double minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
        for (const auto &line : drawing.geometry.lines)
            for (const auto point : {line.a, line.b}) {
                minX = std::min(minX, point.x);
                maxX = std::max(maxX, point.x);
                minY = std::min(minY, point.y);
                maxY = std::max(maxY, point.y);
            }
        near(maxX - minX, 40);
        near(maxY - minY, 60);
        writeMeasuredExport(exportMeasuredDrawing(drawing, MeasuredFormat::Svg), path("plan.svg"));
        writeMeasuredExport(exportMeasuredDrawing(drawing, MeasuredFormat::Pdf), path("plan.pdf"));
        check(read(path("plan.svg")).contains("<svg") && read(path("plan.pdf")).startsWith("%PDF"),
              "Both measured output formats publish real content");
        const auto manifest = parseExtensionManifest(
            read(QStringLiteral(SOURCE_DIR "/examples/extensions/panel.sketchyext")));
        const auto commands = runExtensionWorker(
            manifest, "create-panel", {{"width", 2}, {"height", 3}, {"name", "Study panel"}});
        const auto beforeExtension = encodeContainer(fromTemplate);
        executeBatch(fromTemplate, {{"apiVersion", 1},
                                    {"documentId", QString::fromStdString(fromTemplate.identity())},
                                    {"expectedRevision", QString::number(fromTemplate.revision())},
                                    {"commands", commands}});
        double extendedArea{};
        for (const auto &[id, record] : fromTemplate.bodies())
            for (const auto &[face, geometry] : record->surface.faces) {
                (void)geometry;
                extendedArea += fromTemplate.worldArea(id, face);
            }
        near(extendedArea, 52 + 6);
        check(fromTemplate.canUndo(), "Extension produces an ordinary undoable edit");
        fromTemplate.undo();
        check(encodeContainer(fromTemplate) == beforeExtension,
              "Extension undo restores the template model exactly");
        check(encodeContainer(document) == source && document.history().total == history,
              "All exchange paths preserve the original native model and history");
        std::cout << "M8 exchange study: libraries, glTF, OBJ, both STL encodings, DXF, 1:50 "
                     "PDF/SVG and extension undo passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
