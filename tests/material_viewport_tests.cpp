#include "app/viewport.hpp"
#include "core/components.hpp"
#include "core/materials.hpp"
#include "io/document_io.hpp"
#include <QApplication>
#include <QSurfaceFormat>
#include <QTest>
#include <QVBoxLayout>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
QImage frame(Viewport &view) {
    QCoreApplication::processEvents();
    auto image = view.grabFramebuffer();
    check(!image.isNull() && view.rendererReady(), "Material framebuffer available");
    return image;
}
QColor sample(Viewport &view, Vec3 point) {
    const auto image = frame(view);
    const auto p = view.project(point);
    const QPoint pixel(qRound(p.x() * image.width() / view.width()),
                       qRound(p.y() * image.height() / view.height()));
    check(image.rect().contains(pixel), "Material probe within viewport");
    return image.pixelColor(pixel);
}
void near(QColor actual, QColor expected, const char *message) {
    if (std::abs(actual.red() - expected.red()) >= 6 ||
        std::abs(actual.green() - expected.green()) >= 6 ||
        std::abs(actual.blue() - expected.blue()) >= 6) {
        std::cerr << message << ": " << actual.name().toStdString() << " expected "
                  << expected.name().toStdString() << '\n';
        throw std::runtime_error(message);
    }
}
QColor blend(QColor front, QColor back, double alpha) {
    return QColor(qRound(front.red() * alpha + back.red() * (1 - alpha)),
                  qRound(front.green() * alpha + back.green() * (1 - alpha)),
                  qRound(front.blue() * alpha + back.blue() * (1 - alpha)));
}
} // namespace
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(0);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication app(argc, argv);
    try {
        Document doc;
        auto plane = [&](double z) {
            return doc.addFace({{{-2, -2, z}, {2, -2, z}, {2, 2, z}, {-2, 2, z}}});
        };
        const auto face = plane(1), behind = plane(.4);
        doc.paint(behind, {.1f, .8f, .1f});
        const auto red = createMaterial(doc, "Red front", {.9f, .1f, .1f});
        const auto blue = createMaterial(doc, "Blue back", {.1f, .1f, .9f}, .5f);
        assignMaterial(doc, face, {}, red, true, false);
        assignMaterial(doc, face, {}, blue, false, true);
        QWidget host;
        host.resize(760, 640);
        QVBoxLayout layout(&host);
        auto *view = new Viewport(doc);
        layout.addWidget(view);
        host.show();
        check(QTest::qWaitForWindowExposed(&host), "Material viewport exposed");
        view->standardView(1);
        view->fit();
        const Vec3 probe{-.63, .43, 1};
        const auto frontColor = sample(*view, probe);
        check(frontColor.red() > 3 * frontColor.blue(), "Front swatch renders");
        // A translucent back must not make the visible opaque front lose depth writes.
        const auto above = plane(1.5);
        const auto glass = createMaterial(doc, "Glass", {.1f, .1f, .9f}, .5f);
        assignMaterial(doc, above, {}, glass);
        const auto combined = sample(*view, probe);
        editMaterial(doc, glass, {}, {}, 1.f);
        const auto glassOpaque = sample(*view, probe);
        near(combined, blend(glassOpaque, frontColor, .5),
             "Mixed-sided opaque front occludes lower layer");
        doc.erase(above);
        frame(*view);
        const auto before = view->renderStats();
        const auto record = doc.bodies().at(face);
        editMaterial(doc, red, {}, std::array<float, 3>{.9f, .7f, .1f}, {});
        const auto changed = sample(*view, probe);
        check(changed.green() > frontColor.green() * 3 && doc.bodies().at(face) == record &&
                  view->renderStats().bodyUploads == before.bodyUploads + 1 &&
                  view->renderStats().bodyMeshBuilds == before.bodyMeshBuilds,
              "Swatch edits refresh only dependent appearance without rebuilding meshes");
        const auto unusedBefore = view->renderStats();
        createMaterial(doc, "Unused", {.1f, .8f, .8f});
        frame(*view);
        check(view->renderStats().bodyUploads == unusedBefore.bodyUploads,
              "Unused swatch does not upload geometry");
        editMaterial(doc, red, {}, {}, 0.f);
        check(view->pick(view->project(probe)).first == behind,
              "Zero-opacity material passes CPU picking through before repaint");
        auto selected = view->selectionAt(view->project(probe));
        check(selected && selected->body == behind,
              "Zero-opacity material passes GPU selection through");
        const auto backing = sample(*view, probe);
        editMaterial(doc, red, {}, {}, .5f);
        near(sample(*view, probe), blend(changed, backing, .5), "Persisted front opacity blends");
        check(view->pick(view->project(probe)).first == face,
              "Nonzero transparent material remains pickable");
        selected = view->selectionAt(view->project(probe));
        check(selected && selected->body == face, "Transparent face remains selectable");
        doc.undo();
        near(sample(*view, probe), backing, "Undo updates material appearance cache");
        doc.redo();
        near(sample(*view, probe), blend(changed, backing, .5),
             "Redo restores material appearance");
        // From below the other swatch is visible, with the green plane moved behind it.
        doc.move(behind, {0, 0, 1.2});
        view->standardView(6);
        editMaterial(doc, blue, {}, {}, 0.f);
        const auto belowBacking = sample(*view, probe);
        check(view->pick(view->project(probe)).first == behind,
              "Transparent back passes CPU pick through");
        selected = view->selectionAt(view->project(probe));
        check(selected && selected->body == behind, "Transparent back passes GPU pick through");
        editMaterial(doc, blue, {}, {}, 1.f);
        const auto backColor = sample(*view, probe);
        check(backColor.blue() > backColor.red() * 3, "Back swatch renders independently");
        editMaterial(doc, blue, {}, {}, .5f);
        near(sample(*view, probe), blend(backColor, belowBacking, .5),
             "Back opacity blends independently");
        doc.transform(face, Transform::scaling({-1, 1, 1}));
        near(sample(*view, probe), blend(backColor, belowBacking, .5),
             "Mirror preserves physical back material");
        editMaterial(doc, blue, {}, {}, 0.f);
        check(view->pick(view->project(probe)).first == behind,
              "Mirrored back CPU picking matches rendering");
        selected = view->selectionAt(view->project(probe));
        check(selected && selected->body == behind, "Mirrored back GPU picking matches rendering");
        editMaterial(doc, blue, {}, {}, .5f);
        // Material references also follow shared, reflected instance placements.
        const auto component = createComponent(doc, face, "Panel");
        const auto mirror =
            placeComponent(doc, component.definition,
                           Transform::translation({5, 0, 0}) * Transform::scaling({-1, 1, 1}));
        view->fit();
        frame(*view);
        const auto member = component.movedGeometry.at(face);
        const auto mirrorMember = doc.instances().at(mirror.instance)->members.at(member);
        editMaterial(doc, blue, {}, {}, 1.f);
        frame(*view);
        const auto sharedStats = view->renderStats();
        editMaterial(doc, blue, {}, std::array<float, 3>{.7f, .1f, .9f}, {});
        const auto firstPixel = sample(*view, doc.worldTransform(member).point(probe));
        const auto secondPixel = sample(*view, doc.worldTransform(mirrorMember).point(probe));
        near(firstPixel, secondPixel, "Shared and mirrored instances use the same back swatch");
        check(view->renderStats().bodyUploads == sharedStats.bodyUploads + 2 &&
                  view->renderStats().bodyMeshBuilds == sharedStats.bodyMeshBuilds,
              "Shared material change uploads both dependent instances without remeshing");
        const auto saved = encodeContainer(doc);
        doc = decodeContainer(saved);
        near(sample(*view, doc.worldTransform(member).point(probe)), firstPixel,
             "Reopened material renders identically");
        check(encodeContainer(doc) == saved && view->renderStats().glError == 0,
              "Rendering remains read-only and free of GL errors");
        std::cout << "Material sides, mixed opacity depth, reflection, cache invalidation, CPU/GPU "
                     "picking, undo and reopen passed; DPR="
                  << view->devicePixelRatioF() << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
