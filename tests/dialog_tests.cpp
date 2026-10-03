#include "app/window.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QAbstractButton>
#include <QApplication>
#include <QFileDialog>
#include <QFile>
#include <QMessageBox>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <iostream>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QTemporaryDir directory;
    QDir::setCurrent(directory.path());
    Window window;
    window.show();
    if (!QTest::qWaitForWindowExposed(&window, 5000)) return 2;
    try {
        window.demo();
        const auto original = encodeDocument(window.document());
        const auto target = directory.filePath("model.sketchyup");
        bool cancelDialog = true;
        bool replace = false;
        int dialogs = 0, confirmations = 0;
        QTimer driver;
        driver.setInterval(200);
        QObject::connect(&driver, &QTimer::timeout, [&] {
            for (auto *widget : QApplication::topLevelWidgets()) {
                if (!widget->isVisible()) continue;
                if (auto *dialog = qobject_cast<QFileDialog *>(widget)) {
                    ++dialogs;
                    if (cancelDialog) dialog->reject();
                    else {
                        dialog->setDirectory(directory.path());
                        dialog->selectFile("model");
                        QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
                    }
                } else if (auto *box = qobject_cast<QMessageBox *>(widget)) {
                    ++confirmations;
                    box->button(replace ? QMessageBox::Yes : QMessageBox::Cancel)->click();
                }
            }
        });
        driver.start();
        auto saveAs = [&] {
            for (auto *action : window.findChildren<QAction *>()) {
                if (action->text() == "Save as…") { action->trigger(); return; }
            }
            throw std::runtime_error("Save as action missing");
        };
        saveAs();
        check(dialogs == 1, "Native save dialog opened and canceled");
        check(window.document().dirty() && encodeDocument(window.document()) == original,
              "Cancel preserves dirty model");
        check(!QFile::exists(target), "Cancel creates no file");
        cancelDialog = false;
        saveAs();
        check(QFile::exists(target) && !window.document().dirty(), "Suffix save succeeded");
        check(encodeDocument(loadDocument(target)) == original, "Saved native model roundtrip");
        QFile sentinel(target);
        check(sentinel.open(QIODevice::WriteOnly), "Open replacement fixture");
        sentinel.write("existing file"); sentinel.close();
        saveAs();
        check(confirmations == 1, "Suffix-normalized collision requires confirmation");
        check(sentinel.open(QIODevice::ReadOnly), "Read replacement fixture");
        check(sentinel.readAll() == "existing file", "Cancel preserves existing file");
        sentinel.close();
        replace = true;
        saveAs();
        check(confirmations == 2, "Explicit replacement confirmed");
        check(encodeDocument(loadDocument(target)) == original, "Replacement writes complete model");
        cancelDialog = true;
        for (auto *action : window.findChildren<QAction *>())
            if (action->text() == "Open…") action->trigger();
        check(encodeDocument(window.document()) == original, "Open cancellation preserves model");
        window.document().move(window.document().bodies().begin()->first, {1, 0, 0});
        const auto changed = encodeDocument(window.document());
        replace = false;
        window.openPath(target);
        check(window.document().dirty() && encodeDocument(window.document()) == changed,
              "Cancel unsaved replacement preserves edits");
        const auto invalid = directory.filePath("invalid.sketchyup");
        QFile bad(invalid);
        check(bad.open(QIODevice::WriteOnly), "Create invalid fixture");
        bad.write("broken"); bad.close();
        const auto beforeInvalid = confirmations;
        bool rejected = false;
        try { window.openPath(invalid); } catch (const std::exception &) { rejected = true; }
        check(rejected && confirmations == beforeInvalid &&
              encodeDocument(window.document()) == changed,
              "Invalid open rejected before asking to discard edits");
        window.document().markSaved();
        driver.stop();
        std::cout << "Native dialog adapter: cancel, default suffix, replace cancel/accept passed; platform="
                  << QGuiApplication::platformName().toStdString() << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        window.document().markSaved();
        return 1;
    }
}
