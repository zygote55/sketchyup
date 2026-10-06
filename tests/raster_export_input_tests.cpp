#include "app/window.hpp"
#include "core/annotations.hpp"
#include "core/assets.hpp"
#include "core/reference_images.hpp"
#include "core/sections.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QFile>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Invalid raster export accepted");
}
void settled(Viewport &view) {
    view.refresh();
    QCoreApplication::processEvents();
    view.grabFramebuffer();
    QElapsedTimer timer;
    timer.start();
    while (view.texturesPending() && timer.elapsed() < 10000)
        QTest::qWait(10);
    QTest::qWait(50);
    view.grabFramebuffer();
}
int redPixels(const QImage &image) {
    int count{};
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) {
            const auto c = image.pixelColor(x, y);
            if (c.red() > 200 && c.green() < 60 && c.blue() < 60)
                ++count;
        }
    return count;
}
} // namespace
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(0);
    QSurfaceFormat::setDefaultFormat(format);
    QTemporaryDir isolated;
    qputenv("XDG_CONFIG_HOME", isolated.path().toUtf8());
    QApplication app(argc, argv);
    try {
        Window window;
        window.resize(1000, 800);
        auto &doc = window.document();
        auto &view = *window.viewport();
        const auto png = encodeTexturePng(
            TextureImage(2, 2, {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 0}));
        const auto asset = createAsset(
            doc, "Embedded image", "image/png",
            std::make_shared<AssetPayload>(std::vector<std::uint8_t>(png.begin(), png.end())));
        const auto image = createReferenceImage(doc, {asset, 4, 4, 1});
        auto style = doc.style();
        style.axesVisible = style.gridVisible = style.groundVisible = false;
        style.background = {1, 1, 1};
        doc.setStyle(style);
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Export window exposed");
        view.standardView(1);
        view.fit();
        settled(view);
        check(window.findChild<QAction *>("file.exportRaster"), "File menu exposes PNG export");
        bool opened{};
        QTimer timer;
        timer.setInterval(10);
        QObject::connect(&timer, &QTimer::timeout, &window, [&] {
            auto *dialog = window.findChild<QDialog *>("rasterExportDialog");
            if (dialog && dialog->isVisible()) {
                opened = true;
                timer.stop();
                dialog->reject();
            }
        });
        timer.start();
        window.findChild<QAction *>("file.exportRaster")->trigger();
        check(opened, "Exact pixel dimensions dialog opens");
        view.setSelection(image);
        view.setFocus();
        settled(view);
        doc.markSaved();
        const auto before = encodeContainer(doc),
                   camera = describeRenderCamera(view.renderCamera());
        const auto selection = view.selectionState().entities();
        const auto projected = view.project({1, 3, 0});
        const auto extent = view.size();
        const auto history = doc.history().total;
        const auto tool = view.tool();
        const auto screen = view.grabFramebuffer();
        const auto a = view.renderRaster({901, 607});
        const auto b = view.renderRaster({1802, 1214});
        check(a.size() == QSize(901, 607) && b.size() == QSize(1802, 1214) &&
                  b.devicePixelRatio() == 1,
              "Requested physical pixels are independent of desktop scale");
        check(redPixels(a) > 500 && redPixels(b) > 2000,
              "Embedded raster image is actually rendered");
        check(b != a.scaled(b.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation),
              "Larger image is rendered anew rather than resized");
        check(b.pixelColor(5, 5) == Qt::white && b.pixelColor(20, 28) == Qt::white,
              "Focus outline and view HUD omitted");
        view.setSelection(0);
        const auto noSelection = view.renderRaster(a.size());
        check(noSelection == a, "Selection does not appear in exported pixels");
        view.setSelection(image);
        check(encodeContainer(doc) == before && !doc.dirty() && doc.history().total == history &&
                  describeRenderCamera(view.renderCamera()) == camera &&
                  view.selectionState().entities() == selection &&
                  view.project({1, 3, 0}) == projected && view.size() == extent &&
                  view.tool() == tool,
              "Export preserves document, camera, widget, tool and selection state");
        check(view.grabFramebuffer() == screen,
              "Native framebuffer restored after offscreen export");
        const auto path = isolated.filePath("view.png");
        view.exportRaster(path, {713, 419});
        QImage saved(path);
        check(saved.size() == QSize(713, 419), "PNG stores exact requested dimensions");
        QFile file(path);
        check(file.open(QIODevice::ReadOnly), "Read PNG");
        const auto stable = file.readAll();
        file.close();
        for (const auto invalid : {QSize(0, 10), QSize(-1, 100), QSize(8193, 1), QSize(8192, 8192)})
            rejects([&] { view.exportRaster(path, invalid); });
        file.open(QIODevice::ReadOnly);
        check(file.readAll() == stable, "Invalid export retains destination");
        file.close();
        rejects([&] { view.exportRaster(isolated.filePath("missing/directory.png"), {200, 100}); });
        check(view.grabFramebuffer() == screen, "Failed save preserves live view");
        const auto section =
            createSection(doc, "Image crop", 0, SectionPlane::through({2, 0, 0}, {1, 0, 0}));
        setActiveSection(doc, 0, section);
        settled(view);
        const auto clipped = view.renderRaster(a.size());
        check(redPixels(clipped) < redPixels(a) / 10,
              "Named sections clip reference pixels in export");
        setActiveSection(doc, 0, {});
        settled(view);
        AnnotationRecord label;
        label.kind = AnnotationKind::Label;
        label.name = "Raster label";
        label.text = "Exact pixels";
        label.anchors = {pointAnchor({2, 2, 0})};
        label.offset = {0, 0, 0};
        label.color = {1, 0, 0};
        label.textSize = 24;
        label.leader = false;
        // Measure annotation-only red pixels on a blank background at two resolutions.
        auto reference = *doc.bodies().at(image)->referenceImage;
        reference.opacity = 0;
        setReferenceImage(doc, image, reference);
        createAnnotation(doc, label);
        settled(view);
        const auto small = view.renderRaster({640, 400}), large = view.renderRaster({1280, 800});
        const auto smallInk = redPixels(small), largeInk = redPixels(large);
        check(smallInk > 30 && std::abs(smallInk - largeInk) < 20,
              "Annotations retain pixel text size at exact target resolution");
        const auto missing = createAsset(doc, "Missing", "image/png", {});
        reference.asset = missing;
        reference.opacity = 1;
        setReferenceImage(doc, image, reference);
        settled(view);
        rejects([&] { view.exportRaster(path, {320, 200}); });
        file.open(QIODevice::ReadOnly);
        check(file.readAll() == stable,
              "Unavailable pixels cannot silently replace an exported file");
        file.close();
        check(view.renderStats().glError == 0, "Raster export has no GL errors");
        std::cout << "Exact raster dimensions, fresh rendering, annotations, clipping, state "
                     "preservation and atomic publication passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
