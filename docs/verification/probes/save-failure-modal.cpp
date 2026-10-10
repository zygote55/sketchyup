// R083.f / D-002 product probe (not a registered test). A native save that fails
// for lack of disk space must report through a dismissible modal while the event
// loop keeps running, keep the edit in memory, leave the saved file intact and
// succeed once space is freed. Open and save must then complete while the main
// window is hidden or minimized. Run with TMPDIR on a small filesystem of its own
// (the probe fills it after writing its 2 MB fixture) and at least 120 MB free in
// /tmp for the large model. D002_UNEXPOSED selects the unexposed phases;
// D002_REMAP_ONLY=hide|minimize runs only a hide/show control without file work.
// See R083f-file-worker-hang.md for the build hook and container commands.
#include "app/surface_format.hpp"
#include "app/window.hpp"
#include "core/assets.hpp"
#include <QAction>
#include <QApplication>
#include <QFile>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QWindow>
#include <cstdlib>
#include <iostream>
#include <string>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
QByteArray read(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Read native fixture");
    return file.readAll();
}
} // namespace
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setDepthBufferSize(24);
    setDefaultViewportFormat(format);
    QTemporaryDir files;
    qputenv("XDG_CONFIG_HOME", files.path().toUtf8());
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    QSettings("SketchyUp", "SketchyUp").setValue("recoverySeconds", 0);
    // Fail rather than hang if the product ever blocks without a dismissible modal.
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, [] {
        std::cerr << "D-002 probe: deadline reached without completing\n";
        std::_Exit(2);
    });
    deadline.start(40000);
    try {
        const auto path = files.filePath("Model.sketchyup");
        Document original;
        original.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}});
        createAsset(original, "Local data", "application/octet-stream",
                    std::make_shared<const AssetPayload>(
                        std::vector<std::uint8_t>(2 * 1024 * 1024, 42)));
        saveDocument(original, path);
        const auto originalBytes = read(path);
        // Consume the remaining space so the next 2 MB write cannot fit.
        const auto fillerPath = files.filePath("filler.bin");
        {
            QFile filler(fillerPath);
            check(filler.open(QIODevice::WriteOnly), "Open filler");
            const QByteArray block(64 * 1024, 'x');
            while (filler.write(block) == block.size() && filler.flush()) {
            }
        }
        std::cerr << "D-002 probe: filesystem filled\n";
        Window window;
        window.resize(1280, 850);
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Probe window exposed");
        if (const auto remap = qEnvironmentVariable("D002_REMAP_ONLY"); !remap.isEmpty()) {
            // Platform isolation: no file operation, just hide (optionally after
            // minimizing) and remap the main window.
            if (remap == "minimize") {
                window.showMinimized();
                QTest::qWait(300);
            }
            window.hide();
            QTest::qWait(300);
            window.show();
            check(QTest::qWaitForWindowExposed(&window), "Window exposed after remap");
            std::cerr << "D-002 probe: " << remap.toStdString()
                      << " remap without file operations passed\n";
            return 0;
        }
        window.openPath(path);
        window.document().move(1, {.5, 0, 0});
        const auto edited = encodeContainer(window.document());
        auto *save = window.findChild<QAction *>("file.save");
        check(save, "Save action exists");

        int ticks = 0, ticksAtModal = -1, ticksAtDismiss = -1;
        QString modalText;
        bool modalExposed = false;
        QTimer heartbeat;
        QObject::connect(&heartbeat, &QTimer::timeout, &window, [&] { ++ticks; });
        heartbeat.start(5);
        QTimer dismiss;
        QObject::connect(&dismiss, &QTimer::timeout, &window, [&] {
            auto *box = window.findChild<QMessageBox *>();
            if (!box || !box->isVisible())
                return;
            if (ticksAtModal < 0) {
                ticksAtModal = ticks;
                modalText = box->windowTitle() + ": " + box->text();
                std::cerr << "D-002 probe: modal shown: " << modalText.toStdString() << '\n';
                return;
            }
            // Dismiss once the compositor maps the modal and the heartbeat proves the
            // loop stays live under it; give up waiting for the mapping after 5 s.
            modalExposed = box->windowHandle() && box->windowHandle()->isExposed();
            if (ticks - ticksAtModal < 20 || (!modalExposed && ticks - ticksAtModal < 1000))
                return;
            ticksAtDismiss = ticks;
            dismiss.stop();
            box->button(QMessageBox::Ok)->click();
        });
        dismiss.start(10);
        std::cerr << "D-002 probe: save on full filesystem started\n";
        save->trigger();
        std::cerr << "D-002 probe: save on full filesystem returned\n";
        heartbeat.stop();
        auto *banner = window.findChild<QWidget *>("saveFailureBanner");
        check(modalText.startsWith("Save failed: ") && modalText.contains("Could not write save"),
              "Disk exhaustion is reported through the Save failed modal");
        check(modalExposed, "Save failed modal is mapped by the compositor");
        check(ticksAtDismiss - ticksAtModal >= 20, "Event loop keeps running under the modal");
        check(window.document().dirty() && encodeContainer(window.document()) == edited,
              "Failed save keeps the edit in memory");
        check(read(path) == originalBytes, "Failed save leaves the saved file intact");
        check(banner && banner->isVisible(), "Failed save leaves the persistent banner");

        check(QFile::remove(fillerPath), "Free filesystem space");
        save->trigger();
        check(!window.document().dirty() && read(path) == edited,
              "Save succeeds once space is available");
        check(!banner->isVisible(), "Successful save clears the failure banner");

        // Open and save must not depend on the main surface being exposed: repeat
        // both while minimized and while hidden, with a model large enough that
        // the retained progress window is shown during each operation.
        QTemporaryDir large("/tmp/d002-large-XXXXXX");
        const auto largePath = large.filePath("Large.sketchyup");
        {
            Document model;
            model.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}});
            for (std::uint8_t fill : {7, 9}) // Assets are limited to 16 MiB each.
                createAsset(model, "Large data " + std::to_string(fill),
                            "application/octet-stream",
                            std::make_shared<const AssetPayload>(
                                std::vector<std::uint8_t>(12 * 1024 * 1024, fill)));
            saveDocument(model, largePath);
        }
        auto *progress = window.findChild<QWidget *>("fileOperationDialog");
        // D002_UNEXPOSED selects the phases (default "hidden,minimized") so each
        // can run in its own compositor session.
        const auto phases = qEnvironmentVariable("D002_UNEXPOSED", "hidden,minimized")
                                .split(',', Qt::SkipEmptyParts);
        for (const auto &phase : phases) {
            const auto state = phase.toStdString();
            if (state == "minimized")
                window.showMinimized();
            else
                window.hide();
            QTest::qWait(300);
            const bool exposed = window.windowHandle() && window.windowHandle()->isExposed();
            bool progressShown = false;
            QTimer watch;
            QObject::connect(&watch, &QTimer::timeout, &window, [&] {
                progressShown = progressShown || (progress && progress->isVisible());
            });
            watch.start(5);
            std::cerr << "D-002 probe: " << state << " open/save started; exposed=" << exposed
                      << '\n';
            window.openPath(largePath);
            check(read(largePath) == encodeContainer(window.document()),
                  "Unexposed open completes");
            window.document().move(1, {.25, 0, 0});
            save->trigger();
            watch.stop();
            check(!window.document().dirty() &&
                      read(largePath) == encodeContainer(window.document()),
                  "Unexposed save completes");
            check(progressShown, "Progress window was shown during the unexposed operations");
            std::cerr << "D-002 probe: " << state << " open/save finished with progress shown\n";
            if (state == "hidden") {
                window.show();
                check(QTest::qWaitForWindowExposed(&window), "Window exposed again");
            }
        }
        window.close();
        std::cerr << "D-002 probe: passed; heartbeat ticks under modal="
                  << ticksAtDismiss - ticksAtModal << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "D-002 probe failed: " << error.what() << '\n';
        return 1;
    }
}
