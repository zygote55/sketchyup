#include "app/viewport.hpp"
#include "core/edge_appearance.hpp"
#include "io/document_io.hpp"
#include <QApplication>
#include <QDir>
#include <QSurfaceFormat>
#include <QTemporaryDir>
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
    check(!image.isNull() && view.rendererReady(), "Smooth framebuffer available");
    return image;
}
QColor sample(Viewport &view, Vec3 point) {
    const auto image = frame(view);
    const auto p = view.project(point);
    const QPoint pixel(qRound(p.x() * image.width() / view.width()),
                       qRound(p.y() * image.height() / view.height()));
    check(image.rect().contains(pixel), "Shading probe within viewport");
    return image.pixelColor(pixel);
}
float intensity(Vec3 normal, const Transform &transform) {
    const auto inverse = transform.inverse();
    const auto n =
        normalized({inverse.m[0] * normal.x + inverse.m[1] * normal.y + inverse.m[2] * normal.z,
                    inverse.m[4] * normal.x + inverse.m[5] * normal.y + inverse.m[6] * normal.z,
                    inverse.m[8] * normal.x + inverse.m[9] * normal.y + inverse.m[10] * normal.z});
    return .64f + .36f * std::abs(dot(n, normalized({.3, -.5, .8})));
}
void near(QColor actual, double gray) {
    const auto expected = qRound(gray * 255);
    if (std::abs(actual.red() - expected) > 4 || std::abs(actual.green() - expected) > 4 ||
        std::abs(actual.blue() - expected) > 4) {
        std::cerr << "Actual " << actual.name().toStdString() << " expected gray " << expected
                  << '\n';
        throw std::runtime_error(
            "Framebuffer corner lighting disagrees with independent analytic normals");
    }
}
} // namespace
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(0);
    QSurfaceFormat::setDefaultFormat(format);
    QTemporaryDir preferences;
    qputenv("XDG_CONFIG_HOME", preferences.path().toUtf8());
    QApplication app(argc, argv);
    try {
        Document doc;
        QWidget host;
        host.resize(760, 640);
        QVBoxLayout layout(&host);
        auto *view = new Viewport(doc);
        layout.addWidget(view);
        host.show();
        check(QTest::qWaitForWindowExposed(&host), "Smooth viewport exposed");
        const auto capture = qEnvironmentVariable("SKETCHYUP_SMOOTH_CAPTURE");
        if (!capture.isEmpty())
            QDir().mkpath(capture);
        for (bool mirrored : {false, true}) {
            doc = Document{};
            const auto body = doc.addFace({{{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}});
            doc.extrude(body, 5, 1);
            doc.paint(body, {.8f, .8f, .8f});
            const auto transform = Transform::translation({0, 0, 1}) *
                                   Transform::rotation({0, 0, 1}, .31) *
                                   Transform::scaling({mirrored ? -1.5 : 1.5, .75, 2});
            doc.transform(body, transform);
            view->refresh();
            view->standardView(1);
            view->fit();
            const auto original = doc.bodies().at(body);
            Id top{};
            for (const auto &[face, record] : original->surface.faces)
                if (original->surface.normal(face).z > .99)
                    top = face;
            check(top, "Cube top face exists");
            const auto triangles = original->surface.triangulate(top);
            for (const auto &triangle : triangles)
                near(sample(*view,
                            transform.point((triangle.a + triangle.b + triangle.c) * (1. / 3.))),
                     .8 * intensity({0, 0, 1}, transform));
            const auto before = view->renderStats();
            SelectionSet edges;
            for (const auto &[edge, record] : original->topology.edges)
                edges.insert({body, SelectionKind::Edge, edge});
            setEdgeAppearance(doc, edges, 0, {}, {}, true);
            const auto saved = encodeContainer(doc);
            for (const auto &triangle : triangles) {
                double light = 0;
                for (auto p : {triangle.a, triangle.b, triangle.c})
                    light += intensity(normalized(p - Vec3{.5, .5, 1.5}), transform) / 3.;
                // The centroid has equal barycentric weights, independent of the
                // triangulator's diagonal choice. Gouraud lighting interpolates
                // the three analytically known unit-cube corner intensities.
                near(sample(*view,
                            transform.point((triangle.a + triangle.b + triangle.c) * (1. / 3.))),
                     .8 * light);
            }
            check(view->renderStats().bodyMeshBuilds == before.bodyMeshBuilds &&
                      view->renderStats().bodyWorldUpdates == before.bodyWorldUpdates &&
                      view->renderStats().bodyUploads == before.bodyUploads + 1,
                  "Edge metadata changes only appearance uploads, preserving tessellation cache");
            check(doc.bodies().at(body)->surface == original->surface &&
                      doc.bodies().at(body)->topology == original->topology,
                  "Smooth shading never changes topology");
            if (!capture.isEmpty())
                check(frame(*view).save(capture +
                                        (mirrored ? "/smooth-mirrored.png" : "/smooth.png")),
                      "Save smooth framebuffer evidence");
            doc.undo();
            near(sample(*view, transform.point((triangles[0].a + triangles[0].b + triangles[0].c) *
                                               (1. / 3.))),
                 .8 * intensity({0, 0, 1}, transform));
            doc = decodeContainer(saved);
            view->refresh();
            frame(*view);
            check(view->renderStats().glError == 0,
                  "Smooth Undo and reopen render without GL errors");
        }
        std::cout << "Native analytic smooth lighting, reflection, cache reuse, Undo and reopen "
                     "passed; DPR="
                  << host.devicePixelRatioF() << '\n';
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
