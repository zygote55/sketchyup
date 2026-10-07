#include "app/surface_format.hpp"
#include "app/window.hpp"
#include "core/assets.hpp"
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QFile>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
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
    try {
        check(argc == 1 || argc == 2, "Optional argument is the large native fixture path");
        const auto path = files.filePath("Model.sketchyup");
        if (argc == 2) {
            check(QFile::copy(QString::fromLocal8Bit(argv[1]), path), "Copy large native fixture");
        } else {
            Document original;
            original.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}});
            createAsset(original, "Local data", "application/octet-stream",
                        std::make_shared<const AssetPayload>(
                            std::vector<std::uint8_t>(2 * 1024 * 1024, 42)));
            saveDocument(original, path);
        }
        const auto originalBytes = read(path);
        Window window;
        window.resize(1280, 850);
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "File-operation window exposed");
        const auto prior = window.document().identity();
        bool loadPulse = false, reentrantOpenRejected = false, closeRejected = false;
        QTimer::singleShot(0, &window, [&] {
            loadPulse = window.findChild<QDialog *>("fileOperationDialog") != nullptr;
            check(window.document().identity() == prior, "Old document remains active during load");
            try {
                window.openPath(path);
            } catch (const std::exception &) {
                reentrantOpenRejected = true;
            }
            window.close();
            closeRejected = window.isVisible();
        });
        window.openPath(path);
        check(loadPulse && reentrantOpenRejected && closeRejected,
              "Load delivers events and fences reentrant open/close");
        check(encodeContainer(window.document()) == originalBytes && read(path) == originalBytes,
              "Open validates exact native bytes without changing source");
        bool failurePulse = false, failed = false;
        QTimer::singleShot(0, &window, [&] { failurePulse = true; });
        try {
            window.openPath(files.filePath("missing.sketchyup"));
        } catch (const std::exception &) {
            failed = true;
        }
        check(failed && failurePulse && encodeContainer(window.document()) == originalBytes,
              "Failed asynchronous load preserves the active document");
        auto *save = window.findChild<QAction *>("file.save");
        auto *newFile = window.findChild<QAction *>("file.new");
        check(save && newFile, "Native file actions exist");
        window.document().move(1, {.5, 0, 0});
        const auto captured = encodeContainer(window.document());
        bool savePulse = false;
        QTimer::singleShot(0, &window, [&] {
            savePulse = window.findChild<QDialog *>("fileOperationDialog") != nullptr;
            window.document().move(1, {.25, 0, 0});
            save->trigger(); // Reentrant save must not publish another snapshot.
        });
        save->trigger();
        check(savePulse && read(path) == captured && window.document().dirty() &&
                  encodeContainer(window.document()) != captured,
              "Save delivers events, writes the captured state and keeps later edits dirty");
        save->trigger();
        check(!window.document().dirty() && read(path) == encodeContainer(window.document()),
              "A subsequent save durably records the newer state");
        window.document().move(1, {.125, 0, 0});
        const auto session = window.document().identity();
        bool choiceHandled = false, lateEdit = false;
        QTimer chooseSave;
        QObject::connect(&chooseSave, &QTimer::timeout, &window, [&] {
            if (auto *dialog = window.findChild<QMessageBox *>(); dialog && dialog->isVisible()) {
                chooseSave.stop();
                choiceHandled = true;
                QTimer::singleShot(0, &window, [&] {
                    lateEdit = true;
                    window.document().move(1, {.0625, 0, 0});
                });
                dialog->button(QMessageBox::Save)->click();
            }
        });
        chooseSave.start(10);
        newFile->trigger();
        chooseSave.stop();
        check(choiceHandled && lateEdit && window.document().identity() == session &&
                  window.document().dirty(),
              "Save-before-replace never discards edits arriving during the worker save");
        save->trigger();
        check(!window.document().dirty(), "Final save completes after replacement is declined");
        window.close();
        std::cout << "Native file workers: responsive load/save, stale-save preservation, "
                     "reentrant action fences and failed-load retention passed; bytes="
                  << originalBytes.size() << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
