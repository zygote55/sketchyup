#include "app/window.hpp"
#include "automation/extension_store.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QJsonDocument>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <iostream>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
QByteArray read(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Read extension fixture");
    return file.readAll();
}
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(format);
    QTemporaryDir files;
    qputenv("XDG_CONFIG_HOME", files.path().toUtf8());
    qputenv("XDG_DATA_HOME", files.path().toUtf8());
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    try {
        const auto samplePath = qEnvironmentVariable("SKETCHYUP_EXTENSION_SAMPLE",
                                                     QStringLiteral(SOURCE_DIR) +
                                                         "/examples/extensions/panel.sketchyext");
        const auto source = read(samplePath);
        const auto directory =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/extensions";
        Window window;
        window.resize(1180, 850);
        auto &doc = window.document();
        auto &view = *window.viewport();
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Extension window exposed");
        check(QTest::qWaitFor([&] { return view.rendererReady(); }), "Extension viewport ready");
        int mode{}, stage{};
        bool handling{}, installedDisabled{}, completed{}, stale{}, canceled{}, failed{}, removed{};
        QString failure;
        QElapsedTimer deadline;
        deadline.start();
        QTimer timer;
        timer.setInterval(10);
        QObject::connect(&timer, &QTimer::timeout, &window, [&] {
            if (handling)
                return;
            handling = true;
            try {
                check(deadline.elapsed() < 15000, "Extension native dialog timed out");
                if (auto *file = window.findChild<QFileDialog *>(); file && file->isVisible()) {
                    if (!file->property("handled").toBool()) {
                        file->setProperty("handled", true);
                        auto *name = file->findChild<QLineEdit *>("fileNameEdit");
                        check(name, "Extension file chooser");
                        name->setText(samplePath);
                        QMetaObject::invokeMethod(file, "accept", Qt::QueuedConnection);
                    }
                } else if (auto *dialog = window.findChild<QDialog *>("extensionsDialog");
                           dialog && dialog->isVisible()) {
                    auto *list = dialog->findChild<QListWidget *>("extensionsList");
                    auto *run = dialog->findChild<QPushButton *>("extensionRun");
                    auto *enable = dialog->findChild<QPushButton *>("extensionEnable");
                    const auto status = dialog->findChild<QLabel *>("extensionStatus")->text();
                    if (mode == 0 && stage == 0) {
                        check(list->count() == 0 && !run->isEnabled(), "Empty extension manager");
                        ++stage;
                        QMetaObject::invokeMethod(
                            dialog->findChild<QPushButton *>("extensionInstall"), "click",
                            Qt::QueuedConnection);
                    } else if (mode == 0 && stage == 1 && list->count() == 1) {
                        installedDisabled =
                            !run->isEnabled() &&
                            !ExtensionStore(directory).entries().at("org.sketchyup.panel").enabled;
                        enable->click();
                        dialog->findChild<QDoubleSpinBox *>("extensionParameter-width")
                            ->setValue(3);
                        dialog->findChild<QDoubleSpinBox *>("extensionParameter-height")
                            ->setValue(2);
                        check(dialog->findChild<QPlainTextEdit *>("extensionDetails")
                                  ->toPlainText()
                                  .contains("geometry.face"),
                              "Declared commands shown before execution");
                        if (const auto evidence =
                                qEnvironmentVariable("SKETCHYUP_EXTENSION_EVIDENCE");
                            !evidence.isEmpty()) {
                            QCoreApplication::processEvents();
                            check(dialog->grab().save(evidence), "Extension management screenshot");
                        }
                        ++stage;
                        run->click();
                    } else if (mode == 0 && stage == 2 && status.startsWith("Action applied")) {
                        completed = true;
                        dialog->reject();
                    } else if (mode == 1 && stage == 0) {
                        ++stage;
                        run->click();
                        // Same event callback changes the model before queued completion can
                        // publish.
                        doc.addFace({{{10, 0, 0}, {11, 0, 0}, {10, 1, 0}}});
                        view.refresh();
                    } else if (mode == 1 && stage == 1 && status.contains("model changed")) {
                        stale =
                            !ExtensionStore(directory).entries().at("org.sketchyup.panel").enabled;
                        dialog->reject();
                    } else if (mode == 2 && stage == 0) {
                        enable->click();
                        ++stage;
                        run->click();
                        dialog->findChild<QPushButton *>("extensionCancel")->click();
                    } else if (mode == 2 && stage == 1 && status.startsWith("Action canceled")) {
                        canceled = true;
                        dialog->reject();
                    } else if (mode == 3 && stage == 0) {
                        for (int i = 0; i < list->count(); ++i)
                            if (list->item(i)->data(Qt::UserRole).toString() ==
                                "org.sketchyup.invalid")
                                list->setCurrentRow(i);
                        ++stage;
                        run->click();
                    } else if (mode == 3 && stage == 1 && !run->isEnabled() &&
                               !dialog->findChild<QPushButton *>("extensionCancel")->isEnabled()) {
                        const auto entry =
                            ExtensionStore(directory).entries().at("org.sketchyup.invalid");
                        if (!entry.error.isEmpty()) {
                            failed = !entry.enabled;
                            dialog->findChild<QPushButton *>("extensionRemove")->click();
                            removed = !ExtensionStore(directory).entries().contains(
                                "org.sketchyup.invalid");
                            dialog->reject();
                        }
                    }
                }
            } catch (const std::exception &error) {
                failure = error.what();
                if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
                    dialog->reject();
            }
            handling = false;
        });
        timer.start();
        auto open = [&] {
            deadline.restart();
            window.findChild<QAction *>("extensions.manage")->trigger();
            check(failure.isEmpty(), qPrintable(failure));
        };
        open();
        check(installedDisabled && completed && doc.bodies().size() == 1 &&
                  doc.history().total == 1,
              "Native installation and asynchronous single-edit action");
        const auto body = doc.bodies().begin();
        check(std::abs(doc.worldArea(body->first, body->second->surface.faces.begin()->first) - 6) <
                  1e-9,
              "Native sample parameters produce six square metres");
        doc.undo();
        view.refresh();
        check(doc.bodies().empty(), "Native extension undo");
        doc.redo();
        view.refresh();
        check(doc.bodies().size() == 1, "Native extension redo");
        mode = 1;
        stage = 0;
        open();
        check(stale && doc.bodies().size() == 2 && doc.history().total == 2,
              "Stale completion preserves intervening user edit");
        const auto beforeCancel = encodeContainer(doc);
        const auto history = doc.history().total;
        mode = 2;
        stage = 0;
        open();
        check(canceled && encodeContainer(doc) == beforeCancel && doc.history().total == history,
              "Canceled worker does not publish an edit");
        auto invalid = QJsonDocument::fromJson(source).object();
        invalid["id"] = "org.sketchyup.invalid";
        invalid["name"] = "Invalid geometry sample";
        auto actions = invalid["actions"].toArray();
        auto action = actions[0].toObject();
        const QJsonArray repeatedPoint{0, 0, 0};
        const QJsonArray degenerateLoop{repeatedPoint, repeatedPoint, repeatedPoint};
        action["commands"] = QJsonArray{
            QJsonObject{{"command", "geometry.face"}, {"loops", QJsonArray{degenerateLoop}}}};
        actions[0] = action;
        invalid["actions"] = actions;
        ExtensionStore store(directory);
        store.install(QJsonDocument(invalid).toJson());
        store.setEnabled("org.sketchyup.invalid", true);
        mode = 3;
        stage = 0;
        open();
        check(failed && removed && encodeContainer(doc) == beforeCancel &&
                  doc.history().total == history,
              "Invalid geometry disables extension atomically and removal preserves the model");
        timer.stop();
        check(read(samplePath) == source, "Original extension package remains unchanged");
        doc.markSaved();
        window.close();
        std::cout << "Native extensions: install, explicit enable, asynchronous sample, "
                     "undo/redo, stale/canceled/invalid actions and removal passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
