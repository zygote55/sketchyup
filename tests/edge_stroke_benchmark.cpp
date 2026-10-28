// R100.f2 (ADR 0174 section 9, UX_DESIGN §18.3): the dotted-edge rendering cost R100.f1 left
// unmeasured. It builds a model of many quads with profiles on, then for each edge stroke setting
// (no stroke, solid, dashed, dotted and dash-dot) reports the frame time, the profile quads drawn per
// frame and the bytes the style change uploads. A manual tool, like native_persistence_benchmark, so
// it is not a CI line. Run it under a display (Xvfb with QT_QPA_PLATFORM=xcb), with the quad count
// as the argument: edge_stroke_benchmark [quads-per-side], default 60 (about 3600 quads).
#include "app/surface_format.hpp"
#include "app/viewport.hpp"
#include "app/window.hpp"
#include "io/document_io.hpp"
#include "io/model_style_io.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QGuiApplication>
#include <QTest>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>

using namespace sketchy;

namespace {

void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}

// The frames the viewport has rendered so far.
std::uint64_t frames(Viewport &view) { return view.renderStats().frames; }

// Waits for `count` more rendered frames, keeping the viewport repainting.
void waitFrames(Viewport &view, std::uint64_t count) {
    const auto target = frames(view) + count;
    check(QTest::qWaitFor(
              [&] {
                  view.update();
                  return frames(view) >= target;
              },
              60000),
          "The viewport rendered the frames");
}

// Mean frame time over `samples` frames after a warm-up, in milliseconds.
double meanFrameMs(Viewport &view, int samples) {
    waitFrames(view, 5);
    double total = 0;
    for (int i = 0; i < samples; ++i) {
        waitFrames(view, 1);
        total += view.lastFrameMs();
    }
    return total / samples;
}

} // namespace

int main(int argc, char **argv) {
    const int side = argc > 1 ? std::max(1, std::atoi(argv[1])) : 60;
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    sketchy::setDefaultViewportFormat(format);
    QTemporaryDir files;
    qputenv("XDG_CONFIG_HOME", files.path().toUtf8());
    qputenv("XDG_DATA_HOME", files.path().toUtf8());
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    try {
        // A grid of separate quads, each with four hard edges, with profiles on.
        Document source;
        for (int y = 0; y < side; ++y)
            for (int x = 0; x < side; ++x)
                source.addFace({{{x * 1.2, y * 1.2, 0},
                                 {x * 1.2 + 1, y * 1.2, 0},
                                 {x * 1.2 + 1, y * 1.2 + 1, 0},
                                 {x * 1.2, y * 1.2 + 1, 0}}});
        auto style = source.style();
        style.profiles = true;
        style.profileWidth = 2;
        source.setStyle(style);
        const auto path = files.filePath("grid.sketchyup");
        QFile file(path);
        check(file.open(QIODevice::WriteOnly) && file.write(encodeContainer(source)) > 0,
              "Write the benchmark model");
        file.close();

        Window window;
        window.resize(1200, 900);
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Window exposed");
        window.openPath(path);
        auto &view = *window.viewport();
        check(QTest::qWaitFor([&] { return view.rendererReady(); }, 30000), "Renderer ready");
        view.fit();
        std::cout << "quads " << side * side << ", platform "
                  << QGuiApplication::platformName().toStdString() << ", scale "
                  << window.devicePixelRatioF() << ", " << view.graphicsDescription().toStdString()
                  << "\n";
        std::cout << "setting, frame ms, profile quads per frame, bytes uploaded by the change\n";

        auto report = [&](const std::string &name) {
            const auto before = view.renderStats().uploadedBytes;
            // The first frame after a change uploads the profile quads of the new setting.
            waitFrames(view, 1);
            const auto uploaded = view.renderStats().uploadedBytes - before;
            const auto ms = meanFrameMs(view, 30);
            std::cout << name << ", " << ms << ", " << view.renderStats().profileSegments << ", "
                      << uploaded << "\n";
        };

        window.viewport()->applyStyleEdit(StyleEdit{[&] {
            auto off = window.document().style();
            off.edgeStroke.reset();
            return off;
        }(), "Turn off edge stroke"});
        report("no stroke");

        for (auto dash : {EdgeDash::Solid, EdgeDash::Dashed, EdgeDash::Dotted, EdgeDash::DashDot}) {
            if (!window.document().style().edgeStroke)
                view.applyStyleEdit(turnOnEdgeStroke(window.document().style()));
            view.applyStyleEdit(setEdgeStrokeDash(window.document().style(), dash));
            report(std::string(edgeDashCode(dash)));
        }
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "edge_stroke_benchmark: " << error.what() << '\n';
        return 1;
    }
}
