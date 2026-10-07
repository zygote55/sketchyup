#include "app/surface_format.hpp"
#include "app/window.hpp"
#include "core/groups.hpp"
#include "io/document_io.hpp"
#include <QApplication>
#include <QTemporaryDir>
#include <QTest>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
QColor sample(Viewport &view, const QImage &image, Vec3 point) {
    const auto p = view.project(point) * view.devicePixelRatioF();
    const QPoint pixel(qRound(p.x()), qRound(p.y()));
    check(image.rect().contains(pixel), "Probe stays in the framebuffer");
    return image.pixelColor(pixel);
}
} // namespace
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(0);
    sketchy::setDefaultViewportFormat(format);
    QTemporaryDir settings;
    qputenv("XDG_CONFIG_HOME", settings.path().toUtf8());
    qputenv("XDG_DATA_HOME", settings.path().toUtf8());
    QApplication app(argc, argv);
    Window window;
    auto &doc = window.document();
    auto &view = *window.viewport();
    try {
        const auto base = doc.addFace({{{0, 0, 0}, {4, 0, 0}, {4, 3, 0}, {0, 3, 0}}});
        const auto front =
            doc.addFace({{{.3, .3, .02}, {3.7, .3, .02}, {3.7, 2.7, .02}, {.3, 2.7, .02}}});
        doc.paint(base, {.1f, .2f, .9f});
        doc.paint(front, {.9f, .1f, .1f});
        const auto root = createGroup(doc, {base, front}, "Precision study");
        view.refresh();
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Precision window exposed");
        check(QTest::qWaitFor([&] { return view.rendererReady(); }), "Precision renderer ready");
        view.setBodyOpacity(front, .5);
        std::vector<Vec3> probes;
        for (int y = 1; y < 20; ++y)
            for (int x = 1; x < 20; ++x)
                probes.push_back({.4 + x * .16, .4 + y * .11, .02});
        for (bool orthographic : {false, true}) {
            view.standardView(orthographic ? 1 : 0);
            doc.transform(root, Transform{});
            view.refresh();
            view.frameBounds({0, 0, 0}, {4, 3, .02});
            const auto near = view.grabFramebuffer();
            std::vector<QPointF> pixels;
            std::vector<QColor> colors;
            for (const auto probe : probes) {
                pixels.push_back(view.project(probe));
                colors.push_back(sample(view, near, probe));
            }
            for (const Vec3 origin :
                 {Vec3{100000.125, 200000.25, 12.5}, Vec3{-800000.25, 700000.125, 600000.5}}) {
                doc.transform(root, Transform::translation(origin));
                view.refresh();
                view.frameBounds(origin, origin + Vec3{4, 3, .02});
                const auto far = view.grabFramebuffer();
                const auto camera = view.inferenceCamera();
                check(length(view.renderCamera().target - (origin + Vec3{2, 1.5, .01})) < 1e-9,
                      "Exported render camera retains the exact double world target");
                for (size_t i = 0; i < probes.size(); ++i) {
                    const auto projected = view.project(origin + probes[i]);
                    const auto error = projected - pixels[i];
                    if (std::hypot(error.x(), error.y()) >= .02)
                        std::cerr << "projection error " << error.x() << ',' << error.y() << '\n';
                    check(std::hypot(error.x(), error.y()) < .02,
                          "Translation preserves projection within 0.02 logical pixels");
                    const auto actual = sample(view, far, origin + probes[i]);
                    check(std::abs(actual.red() - colors[i].red()) <= 2 &&
                              std::abs(actual.green() - colors[i].green()) <= 2 &&
                              std::abs(actual.blue() - colors[i].blue()) <= 2,
                          "Distant opaque and transparent surfaces retain their raster colors");
                    check(view.pick(projected).first == front,
                          "Distant CPU rays hit the nearest face");
                    const auto inferred = camera.project(origin + probes[i]);
                    check(inferred && std::hypot(inferred->x - projected.x(),
                                                 inferred->y - projected.y()) < .02,
                          "Public world inference matrices match native projection");
                    const auto [rayOrigin, rayDirection] = camera.ray(projected.x(), projected.y());
                    check(length(cross(origin + probes[i] - rayOrigin, rayDirection)) < .001,
                          "Public inference ray passes within one millimetre of the target");
                }
                view.setSelection(0);
                QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier,
                                  view.project(origin + Vec3{2, 1.5, .02}).toPoint());
                check(view.selectedBody() == root, "Distant GPU selection resolves the group");
                view.setSelection(0);
                view.setClipPlane(std::array<double, 4>{0, 0, -1, origin.z + .01});
                const auto clipped = view.grabFramebuffer();
                const auto color = sample(view, clipped, origin + Vec3{2, 1.5, 0});
                check(color.blue() > color.red() * 3,
                      "World clip plane retains its distant offset");
                check(view.pick(view.project(origin + Vec3{2, 1.5, 0})).first == base,
                      "CPU clip plane matches rendered clipping");
                view.setClipPlane(std::nullopt);
                view.grabFramebuffer();
                const auto stamp = encodeContainer(doc);
                const auto builds = view.renderStats().bodyMeshBuilds;
                view.frameBounds(origin + Vec3{32, 0, 0}, origin + Vec3{36, 3, .02});
                view.grabFramebuffer();
                view.frameBounds(origin, origin + Vec3{4, 3, .02});
                const auto restored = view.grabFramebuffer();
                check(view.renderStats().bodyMeshBuilds == builds && encodeContainer(doc) == stamp,
                      "Camera rebasing reuses triangulation and leaves the model unchanged");
                const auto uploads = view.renderStats().geometryUploads;
                view.grabFramebuffer();
                check(view.renderStats().geometryUploads == uploads,
                      "An unchanged rebased camera reuses GPU buffers");
                const auto restoredColor = sample(view, restored, origin + probes[100]);
                check(std::abs(restoredColor.red() - colors[100].red()) <= 2 &&
                          std::abs(restoredColor.blue() - colors[100].blue()) <= 2,
                      "Cached geometry is uploaded against the new rendering origin");
            }
        }
        const auto origin = doc.worldTransform(root).point({});
        const auto beforePreview = encodeContainer(doc);
        auto preview =
            std::make_shared<const Document::PreparedEdit>(doc.prepareEdit([&](Document &draft) {
                draft.addFace({{origin + Vec3{1, 1, .1}, origin + Vec3{3, 1, .1},
                                origin + Vec3{3, 2, .1}, origin + Vec3{1, 2, .1}}});
            }));
        view.setAssistantPreview(preview);
        auto greenPixels = [&](const QImage &frame) {
            const auto a = view.project(origin + Vec3{1.1, 1.1, .1}) * view.devicePixelRatioF();
            const auto b = view.project(origin + Vec3{2.9, 1.9, .1}) * view.devicePixelRatioF();
            const auto region =
                QRect(a.toPoint(), b.toPoint()).normalized().intersected(frame.rect());
            int count{};
            for (int y = region.top(); y <= region.bottom(); ++y)
                for (int x = region.left(); x <= region.right(); ++x) {
                    const auto c = frame.pixelColor(x, y);
                    count += c.green() > 130 && c.red() < 70 && c.blue() < 100;
                }
            return count;
        };
        check(greenPixels(view.grabFramebuffer()) > 100,
              "Private assistant geometry appears at its distant world position");
        view.frameBounds(origin + Vec3{32, 0, 0}, origin + Vec3{36, 3, .02});
        view.grabFramebuffer();
        view.frameBounds(origin, origin + Vec3{4, 3, .02});
        check(greenPixels(view.grabFramebuffer()) > 100 && encodeContainer(doc) == beforePreview,
              "Rebased assistant preview retains placement without publishing geometry");
        view.setAssistantPreview({});
        check(view.renderStats().glError == 0, "Precision rendering leaves clean GL state");
        std::cout
            << "Near/far projection, raster colors, CPU/GPU selection and clipping passed; DPR "
            << view.devicePixelRatioF() << '\n';
        doc = Document{};
        view.refresh();
        window.close();
        check(QTest::qWaitFor([&] { return !window.isVisible(); }), "Precision window closes");
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
