// R101.f (ADR 0175 V7, §5 and §12): the Quantities section on a large model, through the main
// window offscreen. The model is the §12 re-run model: 8,333 placements of the 12-face part, 99,996
// faces, no terrain. An edit starts a run. The test checks that the "computing" line appears, that
// the UI thread never stops for 250 ms or more while the run computes (the longest gap between
// ticks of a 10 ms timer, from the first tick after the edit until the result shows), that the
// section adds less than 250 ms to the edit's own synchronous work (the same edit timed with the
// section hidden and shown), and that Cancel leaves the computing state within 100 ms of the
// click. The edit's own synchronous work (the viewport and every other panel on the change signal)
// is printed but is not V7's subject: it is the same with the section hidden, and the R101.f record
// carries it as a finding. It prints every measurement; no bound is changed to make a run pass.
// quantities_input_tests covers the panel's behaviour on a small model; this suite is the large
// one, so its run time is its own.
#include "app/quantities_panel.hpp"
#include "app/surface_format.hpp"
#include "app/window.hpp"
#include "core/model.hpp"
#include "core/transform.hpp"
#include "report_fixtures.hpp"
#include <QAction>
#include <QApplication>
#include <QElapsedTimer>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QWidget>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace sketchy;

namespace {

// The two bounds of V7. The 250 ms gap is the brief's bound for a responsive UI; the 100 ms cancel
// is ADR 0175 §5 and §11 V7.
constexpr double kGapBoundMs = 250;
constexpr double kCancelBoundMs = 100;

void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}

QString text(QWidget *parent, const char *name) {
    auto *widget = parent->findChild<QWidget *>(name);
    if (!widget)
        return {};
    if (auto *label = qobject_cast<QLabel *>(widget))
        return label->text();
    if (auto *button = qobject_cast<QPushButton *>(widget))
        return button->text();
    return {};
}

template <class T> T *child(QWidget *parent, const char *name) {
    auto *widget = parent->findChild<T *>(name);
    check(widget != nullptr, std::string("Missing control ") + name);
    return widget;
}

// Pumps the event loop until `done` holds. Returns the milliseconds since `since` when it held, or
// -1 when `timeoutMs` passed first.
template <class Done> double pumpUntil(const QElapsedTimer &since, Done done, int timeoutMs) {
    while (!done()) {
        if (since.elapsed() > timeoutMs)
            return -1;
        QTest::qWait(1);
    }
    return double(since.nsecsElapsed()) / 1e6;
}

// The points of a measured window, in milliseconds from the trigger: the trigger (0), each tick,
// and the end. The gap between two neighbouring points is a stretch in which the UI thread ran
// nothing else but the work between them.
struct Stall {
    double ms{}, from{}, to{};
};
// The longest gap of `points` that starts at point `first` or later.
Stall longestGap(const std::vector<double> &points, std::size_t first) {
    Stall worst;
    for (std::size_t i = first; i + 1 < points.size(); ++i)
        if (points[i + 1] - points[i] > worst.ms)
            worst = {points[i + 1] - points[i], points[i], points[i + 1]};
    return worst;
}

QString revisionLine(std::uint64_t revision) {
    return QString("Computed at revision %1").arg(revision);
}

} // namespace

int main(int argc, char **argv) {
    // Every path the feature uses lives under this folder: the schedule store, the settings and
    // HOME.
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
        report_fixtures::buildPartsModel(doc, 8333, false);
        QMetaObject::invokeMethod(view, "changed");
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "The window is exposed");
        window.activateWindow();
        check(QTest::qWaitForWindowActive(&window), "The window is active for shortcuts");

        std::size_t faces = 0;
        for (const auto &[id, record] : doc.bodies())
            faces += record->surface.faces.size();
        std::vector<Id> placements;
        for (const auto &[id, instance] : doc.instances())
            placements.push_back(id);
        std::cout << "model: " << doc.bodies().size() << " records, " << faces << " faces, "
                  << placements.size() << " placements" << std::endl;
        check(faces >= 99000, "The model has about 100,000 faces: " + std::to_string(faces));

        window.findChild<QAction *>("view.quantities")->trigger();
        auto *panel = window.findChild<QuantitiesPanel *>("quantitiesPanel");
        check(panel && panel->isVisible(), "View > Quantities shows the section");
        const QString modelFile = home.path() + "/large-model.sketchyup";
        panel->setModelPath([modelFile] { return modelFile; });

        const auto resultShown = [&](std::uint64_t revision) {
            return text(panel, "quantitiesRevision") == revisionLine(revision);
        };
        const auto computingShown = [&] {
            return child<QWidget>(panel, "quantitiesComputing")->isVisible();
        };

        // The first run covers the whole model. Its time is reported, and nothing is measured on
        // it.
        QElapsedTimer first;
        first.start();
        const auto firstAt = pumpUntil(first, [&] { return resultShown(doc.revision()); }, 240000);
        check(firstAt >= 0,
              "The first run on the large model shows its revision; the line reads \"" +
                  text(panel, "quantitiesRevision").toStdString() + "\"");
        std::cout << "first run (whole model): result after " << firstAt << " ms" << std::endl;

        // The control edits: the same kind of edit with the section hidden, so the section neither
        // refreshes nor runs. Their synchronous time is the edit's own work, shared by every panel.
        // Each side of the comparison takes the faster of two edits, so a load spike on a shared
        // runner does not decide it.
        const auto placement = placements.front();
        const auto timedEdit = [&](double dx) {
            QElapsedTimer edit;
            edit.start();
            doc.transform(placement, Transform::translation({dx, 0, 0}));
            QMetaObject::invokeMethod(view, "changed");
            return double(edit.nsecsElapsed()) / 1e6;
        };
        panel->hide();
        check(!panel->isVisible(), "The section is hidden for the control edits");
        const double hiddenSync = std::min(timedEdit(0.0005), timedEdit(0.0005));
        panel->show();
        QElapsedTimer reshown;
        reshown.start();
        check(pumpUntil(
                  reshown, [&] { return resultShown(doc.revision()); }, 240000) >= 0,
              "Showing the section again computes the current revision");

        // The trigger is an edit and the window's sync. Its clock starts before the edit, so the
        // edit's own work is inside the measured window.
        std::vector<double> ticks;
        bool measuring = false;
        QElapsedTimer clock;
        QTimer pulse;
        pulse.setInterval(10);
        QObject::connect(&pulse, &QTimer::timeout, &window, [&] {
            if (measuring)
                ticks.push_back(double(clock.nsecsElapsed()) / 1e6);
        });
        pulse.start();

        clock.start();
        measuring = true;
        doc.transform(placement, Transform::translation({0.001, 0, 0}));
        const double edited = double(clock.nsecsElapsed()) / 1e6;
        QMetaObject::invokeMethod(view, "changed");
        const double synced = double(clock.nsecsElapsed()) / 1e6;
        const auto revision = doc.revision();
        const double computingAt =
            pumpUntil(clock, [&] { return computingShown() || resultShown(revision); }, 120000);
        check(computingAt >= 0 && computingShown(),
              "The run on the large model shows the computing line (the run finished first)");
        check(text(panel, "quantitiesComputingText")
                  .contains(QString("Computing revision %1").arg(revision)),
              "The computing line names the revision it computes");
        const double resultAt = pumpUntil(clock, [&] { return resultShown(revision); }, 120000);
        measuring = false;
        check(resultAt >= 0, "The run on the large model shows its result; the line reads \"" +
                                 text(panel, "quantitiesRevision").toStdString() + "\"");
        std::vector<double> points{0};
        points.insert(points.end(), ticks.begin(), ticks.end());
        points.push_back(resultAt);
        const auto stall = longestGap(points, 0);
        // Point 0 is the trigger, so starting at point 1 leaves out the trigger's own stretch.
        const auto afterTrigger = longestGap(points, 1);
        const char *where =
            stall.from < synced
                ? "in the trigger, before the event loop ran (the edit and the window's sync)"
                : "after the trigger, while the run computed";
        std::cout << "edit run: edit " << edited << " ms, window sync done at " << synced
                  << " ms, computing line at " << computingAt << " ms, result at " << resultAt
                  << " ms, " << ticks.size() << " ticks" << std::endl;
        std::cout << "UI thread, longest gap: " << stall.ms << " ms, from " << stall.from << " to "
                  << stall.to << " ms after the trigger, " << where
                  << " (information: the edit's own work is not bounded here)" << std::endl;
        std::cout << "UI thread, longest gap after the first tick: " << afterTrigger.ms
                  << " ms, from " << afterTrigger.from << " to " << afterTrigger.to << " ms"
                  << " (bound < " << kGapBoundMs << " ms)" << std::endl;

        // Cancel: an edit starts a run, and Cancel is pressed while its computing line is shown.
        const auto shownRevision = revision;
        doc.transform(placement, Transform::translation({0.002, 0, 0}));
        QMetaObject::invokeMethod(view, "changed");
        const auto cancelRevision = doc.revision();
        QElapsedTimer trigger;
        trigger.start();
        const double cancelRunAt = pumpUntil(
            trigger, [&] { return computingShown() || resultShown(cancelRevision); }, 120000);
        check(cancelRunAt >= 0 && computingShown(),
              "The run before Cancel is still computing (the run finished first)");
        auto *cancel = child<QPushButton>(panel, "quantitiesCancel");
        check(cancel->isVisible(), "Cancel is shown while the run computes");
        QElapsedTimer click;
        click.start();
        cancel->click();
        const double hidden = pumpUntil(click, [&] { return !computingShown(); }, 1000);
        const double status = pumpUntil(
            click,
            [&] { return text(panel, "quantitiesStatus").startsWith("Not updated: cancelled"); },
            1000);
        std::cout << "cancel run: computing line at " << cancelRunAt << " ms; Cancel: computing "
                  << "line gone " << hidden << " ms after the click, status \"Not updated\" "
                  << status << " ms after (bound <= " << kCancelBoundMs << " ms)" << std::endl;
        check(hidden >= 0, "Cancel leaves the computing state");
        check(status >= 0, "Cancel reports that the totals were not updated: the status reads \"" +
                               text(panel, "quantitiesStatus").toStdString() + "\"");
        check(text(panel, "quantitiesRevision") == revisionLine(shownRevision),
              "The cancelled run keeps the last complete result and its revision");

        // A second shown edit, timed the same way, after which its run finishes.
        const double shownAgain = timedEdit(0.003);
        QElapsedTimer settled;
        settled.start();
        check(pumpUntil(
                  settled, [&] { return resultShown(doc.revision()); }, 240000) >= 0,
              "The run after the second shown edit shows its result");
        const double shownSync = std::min(synced, shownAgain);
        const double panelShare = shownSync - hiddenSync;
        std::cout << "edit's synchronous work, faster of two: section hidden " << hiddenSync
                  << " ms, section shown " << shownSync << " ms (" << synced << " and "
                  << shownAgain << "); the section's share " << panelShare << " ms (bound < "
                  << kGapBoundMs << " ms)" << std::endl;

        check(afterTrigger.ms < kGapBoundMs,
              "The UI thread stops for less than 250 ms while the run computes; the longest stall "
              "after the edit is " +
                  std::to_string(afterTrigger.ms) + " ms");
        check(panelShare < kGapBoundMs,
              "The section adds less than 250 ms to an edit's synchronous work; it added " +
                  std::to_string(panelShare) + " ms (hidden " + std::to_string(hiddenSync) +
                  " ms, shown " + std::to_string(shownSync) + " ms)");
        check(hidden <= kCancelBoundMs,
              "Cancel leaves the computing state within 100 ms of the click; it took " +
                  std::to_string(hidden) + " ms");
        std::cout << "quantities_large_input_tests: all checks passed" << std::endl;
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "quantities_large_input_tests: " << error.what() << std::endl;
        return 1;
    }
}
