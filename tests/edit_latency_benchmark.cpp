// R101.f finding 22: the UI thread's time for one placement edit on a large model, through the main
// window offscreen. Build it in a Release tree and run it alone:
//   cmake --build build/release --target edit_latency_benchmark
//   edit_latency_benchmark [placements] [quantities]
// The model is report_fixtures::buildPartsModel: 12 faces per placement, so 8,333 placements is the
// 99,996-face model of the R101.f record and 4,166 is about 50,000 faces. An edit is what the app
// does for one placement move: the transform, then the Viewport::changed signal, whose slots run
// synchronously. The benchmark times the two steps and the event loop that follows. Offscreen the
// viewport's GL is not ready, so that loop returns before the model is drawn. It prints every
// sample and the p50, p95 and max of 20 edits, and asserts nothing.
#include "app/surface_format.hpp"
#include "app/viewport.hpp"
#include "app/window.hpp"
#include "core/model.hpp"
#include "core/transform.hpp"
#include "report_fixtures.hpp"
#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QWidget>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace sketchy;

namespace {

constexpr int kEdits = 20;

void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}

// Nearest-rank percentile of `samples` (copied, then sorted).
double percentile(std::vector<double> samples, double fraction) {
    std::sort(samples.begin(), samples.end());
    const auto rank =
        static_cast<std::size_t>(std::max(1., std::ceil(fraction * double(samples.size()))));
    return samples[rank - 1];
}

void summary(const char *label, const std::vector<double> &samples) {
    std::cout << "  " << label << ": p50 " << percentile(samples, 0.50) << " ms, p95 "
              << percentile(samples, 0.95) << " ms, max " << percentile(samples, 1.0) << " ms"
              << std::endl;
}

std::string loadAverage() {
    std::ifstream loads("/proc/loadavg");
    std::string one;
    loads >> one;
    return one;
}

} // namespace

int main(int argc, char **argv) {
    const std::size_t placements = argc > 1 ? std::stoul(argv[1]) : 8333;
    const bool quantities = argc > 2 && std::string(argv[2]) == "quantities";
    // Every path the window uses lives under this folder, as in quantities_large_input_tests.
    QTemporaryDir home;
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + "/data").toUtf8());
    qputenv("XDG_CONFIG_HOME", (home.path() + "/config").toUtf8());
    QSurfaceFormat format;
    format.setDepthBufferSize(24);
    sketchy::setDefaultViewportFormat(format);
    QApplication app(argc, argv);
    QApplication::setOrganizationName("SketchyUp");
    QApplication::setApplicationName("SketchyUp");

    try {
        Window window;
        window.resize(1280, 900);
        auto &doc = window.document();
        auto *view = window.viewport();
        doc.setDisplayPrecision(2);
        report_fixtures::buildPartsModel(doc, placements, false);
        QMetaObject::invokeMethod(view, "changed");
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "The window is exposed");

        std::size_t faces = 0;
        for (const auto &[id, record] : doc.bodies())
            faces += record->surface.faces.size();
        std::vector<Id> instances;
        for (const auto &[id, instance] : doc.instances())
            instances.push_back(id);
#ifdef NDEBUG
        const char *build = "Release (NDEBUG)";
#else
        const char *build = "Debug";
#endif
        std::cout << "build: " << build
                  << "; platform: " << QGuiApplication::platformName().toStdString()
                  << "; load average (1 min) before the run: " << loadAverage() << std::endl;
        std::cout << "model: " << doc.bodies().size() << " records, " << faces << " faces, "
                  << instances.size()
                  << " placements; section: " << (quantities ? "shown" : "hidden") << std::endl;
        check(!instances.empty(), "The model has placements");

        if (quantities) {
            window.findChild<QAction *>("view.quantities")->trigger();
            check(window.findChild<QWidget *>("quantitiesPanel")->isVisible(),
                  "View > Quantities shows the section");
        }

        // The Outliner is the first tab of the model tray. The edits below rebuild it, and the user
        // sees that rebuild only when it is on screen.
        const auto *tabs = window.findChild<QTabWidget *>("organizationTabs");
        check(tabs != nullptr, "The organization tabs exist");
        std::cout << "outliner on screen: "
                  << (tabs->isVisible() && tabs->currentIndex() == 0 ? "yes" : "no") << std::endl;

        const auto placement = instances.front();
        // The app's own idle between two edits: the inference index for the last revision is
        // built and the event loop drains, so no earlier work runs inside a measured edit.
        const auto settle = [&] {
            QElapsedTimer waited;
            waited.start();
            while (!view->inferenceReady() && waited.elapsed() < 240000)
                QTest::qWait(5);
            QTest::qWait(50);
        };
        const auto edit = [&](double dx, double &editMs, double &changedMs, double &afterMs) {
            QElapsedTimer clock;
            clock.start();
            doc.transform(placement, Transform::translation({dx, 0, 0}));
            editMs = double(clock.nsecsElapsed()) / 1e6;
            QMetaObject::invokeMethod(view, "changed");
            changedMs = double(clock.nsecsElapsed()) / 1e6 - editMs;
            QCoreApplication::processEvents(QEventLoop::AllEvents, 0);
            afterMs = double(clock.nsecsElapsed()) / 1e6 - editMs - changedMs;
        };

        // One untimed edit warms the code paths and the caches the first edit fills.
        double editMs{}, changedMs{}, afterMs{};
        settle();
        edit(0.001, editMs, changedMs, afterMs);
        std::cout << "warm-up edit: edit " << editMs << " ms, changed " << changedMs
                  << " ms, event loop " << afterMs << " ms" << std::endl;

        std::vector<double> total, sync, edits, after;
        for (int i = 0; i < kEdits; ++i) {
            settle();
            edit(0.001, editMs, changedMs, afterMs);
            total.push_back(editMs + changedMs);
            sync.push_back(changedMs);
            edits.push_back(editMs);
            after.push_back(afterMs);
            std::cout << "edit " << i + 1 << ": edit " << editMs << " ms, changed (synchronous "
                      << "slots) " << changedMs << " ms, event loop " << afterMs << " ms"
                      << std::endl;
        }
        std::cout << "Over " << kEdits << " edits (" << build << ", section "
                  << (quantities ? "shown" : "hidden") << ", " << faces << " faces):" << std::endl;
        summary("edit + Viewport::changed (the UI thread's synchronous work)", total);
        summary("Viewport::changed (the slots)", sync);
        summary("edit (transform)", edits);
        summary("event loop after the edit", after);
        std::cout << "load average (1 min) after the run: " << loadAverage() << std::endl;
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "edit_latency_benchmark: " << error.what() << std::endl;
        return 1;
    }
}
