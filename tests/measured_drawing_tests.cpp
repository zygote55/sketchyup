#include "core/annotations.hpp"
#include "core/materials.hpp"
#include "core/sections.hpp"
#include "io/document_io.hpp"
#include "io/measured_drawing.hpp"
#include <QCoreApplication>
#include <QJsonArray>
#include <iostream>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void near(double a, double b) {
    check(std::isfinite(a) && std::abs(a - b) < 1e-7, "Physical drawing oracle");
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Expected drawing rejection");
}
RenderSnapshot capture(const Document &doc) {
    RenderCamera camera;
    camera.orthographic = true;
    camera.position = {0, 0, 10};
    camera.target = {0, 0, 0};
    camera.up = {0, 1, 0};
    RenderOptions options;
    options.camera = camera;
    return RenderSnapshot::capture(doc, options);
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        Document doc;
        const auto face = doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 1, 0}, {0, 1, 0}}});
        AnnotationRecord dimension;
        dimension.name = "Two metres";
        dimension.kind = AnnotationKind::Distance;
        dimension.anchors = {pointAnchor({0, 0, 0}), pointAnchor({2, 0, 0})};
        dimension.offset = {0, -.5, 0};
        createAnnotation(doc, dimension);
        const auto before = encodeDocument(doc);
        const auto revision = doc.revision();
        MeasuredPage page;
        page.scaleDenominator = 50;
        const auto snapshot = capture(doc);
        const auto drawing = captureMeasuredDrawing(snapshot, page);
        check(drawing.geometry.lines.size() == 4 && drawing.annotations.size() == 1,
              "Rectangle and associative dimension");
        double total{};
        for (const auto &line : drawing.geometry.lines)
            total += length(line.b - line.a);
        near(total, 120);
        near(*drawing.annotations[0].measurement.distance, 2);
        near(drawing.annotations[0].anchors[1].x - drawing.annotations[0].anchors[0].x, 40);
        near(drawing.annotations[0].textPoint.y, 115);
        check(encodeDocument(doc) == before && doc.revision() == revision,
              "Capture preserves model and history");
        doc.move(face, {50, 0, 0});
        check(captureMeasuredDrawing(snapshot, page).geometry.lines.size() == 4,
              "Snapshot remains immutable after live edits");
        doc.undo();
        const auto plane = createSection(doc, "Half", 0, {{1, 0, 0}, -1});
        setActiveSection(doc, 0, plane);
        const auto cut = captureMeasuredDrawing(capture(doc), page);
        check(!cut.geometry.lines.empty(), "Section retains part of source face");
        for (const auto &line : cut.geometry.lines)
            check(line.a.x >= 168.5 - 1e-7 && line.b.x >= 168.5 - 1e-7,
                  "Section clips edges at exact printed millimetres");
        setActiveSection(doc, 0, {});
        const auto cover =
            doc.addFace({{{.5, -.5, 1}, {1.5, -.5, 1}, {1.5, 1.5, 1}, {.5, 1.5, 1}}});
        const auto occluded = captureMeasuredDrawing(capture(doc), page);
        check(occluded.geometry.hiddenIntervals > 0, "Foreground face occludes rectangle edges");
        page.includeHidden = true;
        const auto hidden = captureMeasuredDrawing(capture(doc), page);
        bool hasHidden = false;
        for (const auto &line : hidden.geometry.lines)
            hasHidden |= line.hidden;
        check(hasHidden, "Optional hidden segments preserved for dashed output");
        const auto material = createMaterial(doc, "Glass", {.5F, .5F, .5F}, .5F);
        assignMaterial(doc, cover, {}, material);
        const auto transparent = captureMeasuredDrawing(capture(doc), page);
        check(transparent.report["rasterAppearanceReasons"].toArray().contains("transparency"),
              "Transparency identifies raster appearance requirement");
        check(transparent.geometry.hiddenIntervals == 0,
              "Transparent foreground is not treated as an opaque occluder");
        Document solid;
        const auto box = solid.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}}});
        solid.extrude(box, solid.bodies().at(box)->surface.faces.begin()->first, 2);
        const auto section = createSection(solid, "Cut box", 0, {{0, 0, -1}, 1});
        setActiveSection(solid, 0, section);
        auto withCap = captureMeasuredDrawing(capture(solid), page);
        size_t capEdges{};
        for (const auto &source : withCap.sources)
            if (source.section == section)
                ++capEdges;
        check(capEdges >= 4, "Closed cut box contains section contour sources");
        auto record = *solid.sections().at(section);
        record.fill = false;
        updateSection(solid, section, record);
        auto noFill = captureMeasuredDrawing(capture(solid), page);
        check(noFill.geometry.occluders < withCap.geometry.occluders,
              "Section fill controls cap occlusion");
        record.edges = false;
        updateSection(solid, section, record);
        const auto noContour = captureMeasuredDrawing(capture(solid), page);
        for (const auto &source : noContour.sources)
            check(source.section == 0, "Section edge flag respected");
        check(noContour.annotations.empty(), "Annotations never invented for geometry");
        rejects([&] { captureMeasuredDrawing(RenderSnapshot::capture(doc), page); });
        page.scaleDenominator = 0;
        rejects([&] { captureMeasuredDrawing(capture(doc), page); });
        std::cout << "Measured document drawing: scale, snapshot, section, dimension, occlusion "
                     "and transparency passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
