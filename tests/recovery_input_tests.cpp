#include "app/surface_format.hpp"
#include "app/window.hpp"
#include "core/assets.hpp"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QWindow>
#include <iostream>
using namespace sketchy;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
QByteArray read(const QString &path) {
    QFile f(path);
    check(f.open(QIODevice::ReadOnly), "Read fixture");
    return f.readAll();
}
void focus(QWidget *widget) {
    widget->window()->activateWindow();
    check(QTest::qWaitFor(
              [&] { return QGuiApplication::focusWindow() == widget->window()->windowHandle(); }),
          "Recovery native focus settled");
    widget->setFocus();
}
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setDepthBufferSize(24);
    sketchy::setDefaultViewportFormat(format);
    QTemporaryDir files;
    qputenv("XDG_CONFIG_HOME", files.path().toUtf8());
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    QSettings("SketchyUp", "SketchyUp").setValue("recoverySeconds", 0);
    Window window;
    window.resize(1280, 850);
    try {
        const auto root = files.filePath("recovery"),
                   source = files.filePath("Saved model.sketchyup");
        Document original;
        original.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}}, "Recovered box");
        original.extrude(1, original.bodies().at(1)->surface.faces.begin()->first, 2);
        saveDocument(original, source);
        const auto sourceBytes = read(source);
        RecoveryContext context{source, original.revision(), QDateTime::currentDateTimeUtc()};
        original.move(1, {1, 2, 0});
        createAsset(original, "Missing glass.png", "image/png");
        QString oldKey;
        {
            RecoveryWriter writer(root, QString::fromStdString(original.identity()));
            oldKey = writer.key();
            writer.write(captureRecovery(original, context));
        }
        const auto expected = encodeDocument(original);
        Document bad;
        QString corruptKey;
        {
            RecoveryWriter writer(root, QString::fromStdString(bad.identity()));
            corruptKey = writer.key();
            writer.write(captureRecovery(bad));
        }
        QFile corrupt(QDir(root).filePath(corruptKey + "/CURRENT"));
        check(corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate) && corrupt.write("bad") == 3,
              "Corrupt candidate");
        corrupt.close();
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Recovery window exposed");
        window.startRecovery(root);
        auto *controller = window.findChild<RecoveryController *>("recoveryController");
        auto *status = window.findChild<QLabel *>("recoveryStatus");
        check(controller && status->isVisible() && controller->interval() == 0,
              "Explicit disabled preference shown");
        enum class Mode {
            Settings,
            Saved,
            Recovered,
            Corrupt,
            SaveCopy,
            CancelClose,
            DiscardClose,
            SaveFailure
        };
        Mode mode = Mode::Settings;
        bool handling = false, handled = false;
        const auto copy = files.filePath("Recovered copy");
        QTimer timer;
        timer.setInterval(10);
        QObject::connect(&timer, &QTimer::timeout, &window, [&] {
            if (handling || handled)
                return;
            handling = true;
            if (auto *message = window.findChild<QMessageBox *>();
                message && message->isVisible()) {
                focus(message);
                const auto choice = mode == Mode::CancelClose   ? QMessageBox::Cancel
                                    : mode == Mode::SaveFailure ? QMessageBox::Ok
                                                                : QMessageBox::Discard;
                message->button(choice)->click();
                handled = true;
            } else if (auto *settings = window.findChild<QDialog *>("recoverySettings");
                       settings && settings->isVisible()) {
                focus(settings);
                settings->findChild<QCheckBox *>("recoveryEnabled")->setChecked(true);
                settings->findChild<QSpinBox *>("recoveryInterval")->setValue(5);
                settings->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
                handled = true;
            } else if (auto *chooser = window.findChild<QFileDialog *>("saveModelDialog");
                       chooser && chooser->isVisible()) {
                focus(chooser);
                auto *filename = chooser->findChild<QLineEdit *>("fileNameEdit");
                check(filename, "Native save field");
                filename->setFocus();
                filename->selectAll();
                QTest::keyClicks(filename, copy);
                QMetaObject::invokeMethod(chooser, "accept", Qt::DirectConnection);
                handled = true;
            } else if (auto *dialog = window.findChild<QDialog *>("recoveryDialog");
                       dialog && dialog->isVisible()) {
                focus(dialog);
                auto *list = dialog->findChild<QTreeWidget *>("recoveryList");
                QTreeWidgetItem *selected = nullptr;
                for (int i = 0; i < list->topLevelItemCount(); ++i) {
                    auto *item = list->topLevelItem(i);
                    if (item->text(0) ==
                        (mode == Mode::Corrupt ? "Unreadable recovery" : "Saved model.sketchyup"))
                        selected = item;
                }
                check(selected, "Expected recovery candidate listed");
                list->setCurrentItem(selected);
                if (mode == Mode::Corrupt) {
                    check(!dialog->findChild<QPushButton *>("openRecovered")->isEnabled() &&
                              !dialog->findChild<QPushButton *>("openLastSaved")->isEnabled(),
                          "Unverified recovery cannot be opened");
                    dialog->findChild<QPushButton *>("discardRecovery")->click();
                    check(!QFileInfo::exists(QDir(root).filePath(corruptKey)),
                          "Selected corrupt recovery discarded");
                    dialog->reject();
                } else {
                    check(dialog->findChild<QPlainTextEdit *>("recoveryDetails")
                              ->toPlainText()
                              .contains("Missing glass.png"),
                          "Missing resources are explicit");
                    if (app.arguments().contains("--capture") && mode == Mode::Recovered)
                        dialog->grab().save("/capture/R038-recovery-dialog.png");
                    dialog
                        ->findChild<QPushButton *>(mode == Mode::Saved ? "openLastSaved"
                                                                       : "openRecovered")
                        ->click();
                }
                handled = true;
            }
            handling = false;
        });
        timer.start();
        window.findChild<QAction *>("file.recoverySettings")->trigger();
        check(handled && controller->interval() == 5 &&
                  QSettings("SketchyUp", "SketchyUp").value("recoverySeconds").toInt() == 5,
              "Recovery preference persists");
        controller->setInterval(0);
        mode = Mode::Saved;
        handled = false;
        window.showRecovery();
        check(handled && !window.document().dirty() &&
                  encodeContainer(window.document()) == sourceBytes &&
                  QFileInfo::exists(QDir(root).filePath(oldKey)),
              "Open last saved preserves recovery alternative");
        mode = Mode::Recovered;
        handled = false;
        window.showRecovery();
        check(handled && window.document().dirty() && !window.document().canUndo() &&
                  encodeDocument(window.document()) == expected && read(source) == sourceBytes,
              "Recovered model opens Edited with exact identity and original source untouched");
        mode = Mode::SaveCopy;
        handled = false;
        window.findChild<QAction *>("file.save")->trigger();
        check(handled && !window.document().dirty() && read(source) == sourceBytes &&
                  QFileInfo::exists(copy + ".sketchyup") &&
                  !QFileInfo::exists(QDir(root).filePath(oldKey)),
              "First save chooses new native path and retires adopted recovery");
        mode = Mode::Corrupt;
        handled = false;
        window.showRecovery();
        check(handled && listRecoveries(root).empty(),
              "Corrupt data requires explicit discard and has no verified claim");
        timer.stop();
        window.document().move(1, {.2, 0, 0});
        QMetaObject::invokeMethod(window.viewport(), "changed");
        window.findChild<QAction *>("file.recoveryNow")->trigger();
        window.document().move(1, {.3, 0, 0});
        QMetaObject::invokeMethod(window.viewport(), "changed");
        check(QTest::qWaitFor([&] { return !controller->busy(); }, 10000) && controller->durable(),
              "Native manual recovery acknowledges durable copy");
        check(status->text().contains("1 newer changes") && window.document().dirty(),
              "Status distinguishes captured recovery from newer in-memory edits");
        mode = Mode::CancelClose;
        handled = false;
        timer.start();
        window.close();
        check(handled && window.isVisible() && controller->durable() && window.document().dirty(),
              "Close cancellation keeps edits and recovery");
        // A retained-copy obstruction produces a persistent save failure, independent of recovery.
        QDir().mkdir(copy + ".sketchyup.bak");
        mode = Mode::SaveFailure;
        handled = false;
        window.findChild<QAction *>("file.save")->trigger();
        check(handled && window.findChild<QWidget *>("saveFailureBanner")->isVisible() &&
                  controller->durable() && window.document().dirty(),
              "Save failure remains visible without inventing or discarding recovery protection");
        QDir().rmdir(copy + ".sketchyup.bak");
        timer.stop();
        window.findChild<QPushButton *>("retrySave")->click();
        check(!window.document().dirty() &&
                  !window.findChild<QWidget *>("saveFailureBanner")->isVisible() &&
                  !controller->durable(),
              "Retry Save clears failure and old recovery after durable explicit save");
        window.document().move(1, {.1, 0, 0});
        controller->checkpoint();
        check(QTest::qWaitFor([&] { return !controller->busy(); }, 10000) && controller->durable(),
              "Recovery available before discard close");
        mode = Mode::DiscardClose;
        handled = false;
        timer.start();
        window.close();
        timer.stop();
        check(handled && !window.isVisible() && listRecoveries(root).empty(),
              "Explicit discard close retires only current recovery");
        std::cout
            << "Native recovery selection, missing assets, preferences, source-safe save, verified "
               "status, close cancellation, persistent save failure/retry and discard passed; DPR="
            << window.devicePixelRatioF() << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        window.document().markSaved();
        return 1;
    }
}
