#include "app/render_panel.hpp"
#include "app/window.hpp"
#include "automation/commands.hpp"
#include "core/entity_measure.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QFile>
#include <QLabel>
#include <QJsonDocument>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QSurfaceFormat>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <iostream>
#include <numbers>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
QByteArray read(QString path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Read image");
    return file.readAll();
}
QString quote(QString value) { return "'" + value.replace("'", "'\\''") + "'"; }
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(format);
    QTemporaryDir files;
    qputenv("XDG_CONFIG_HOME", files.path().toUtf8());
    QApplication app(argc, argv);
    QSettings preferences("SketchyUp", "SketchyUp");
    preferences.setValue("recoverySeconds", 0);
    preferences.setValue("render/blenderPath", files.path() + "/absent");
    try {
        Window window;
        window.resize(1200, 850);
        window.show();
        auto &doc = window.document();
        auto &view = *window.viewport();
        auto &panel = *window.renderPanel();
        check(QTest::qWaitFor([&] { return view.rendererReady(); }), "Native GL renderer ready");
        auto *tabs = window.findChild<QTabWidget *>("modelTabs");
        check(tabs && tabs->count() == 1, "Model tab initially alone");
        const auto sourceModel = qEnvironmentVariable("SKETCHYUP_RENDER_MODEL");
        if (!sourceModel.isEmpty())
            doc = loadDocument(sourceModel);
        else {
            executeBatch(doc,
                         {{"apiVersion", 1},
                          {"documentId", QString::fromStdString(doc.identity())},
                          {"expectedRevision", QString::number(doc.revision())},
                          {"commands", QJsonArray{QJsonObject{{"command", "assembly.room"}}}}});
        }
        view.refresh();
        emit view.changed();
        view.fit();
        if (const auto focus = qEnvironmentVariable("SKETCHYUP_RENDER_FOCUS"); !focus.isEmpty()) {
            bool valid{};
            const auto body = focus.toULongLong(&valid);
            check(valid && doc.bodies().contains(body), "Explicit render fixture focus exists");
            const auto bounds = measureEntity(doc, {body, SelectionKind::Body, 0}).world.bounds;
            check(bounds.has_value(), "Render fixture focus has finite bounds");
            view.frameBounds(bounds->low, bounds->high);
        }
        auto *action = window.findChild<QAction *>("camera.render");
        check(action, "Render is in action registry");
        action->trigger();
        auto *setup = window.findChild<QDialog *>("renderSetup");
        check(setup && setup->isVisible(), "Render opens setup");
        auto *start = setup->findChild<QPushButton *>("startRender");
        auto *probe = setup->findChild<QPushButton *>("probeBlender");
        auto *status = setup->findChild<QLabel *>("renderSetupStatus");
        check(QTest::qWaitFor([&] { return probe->isEnabled(); }),
              "Absent Blender check completes");
        check(!start->isEnabled() && status->text().contains("unavailable"),
              "Missing executable gives setup state");
        QTest::keyClick(setup, Qt::Key_Escape);
        check(!setup->isVisible(), "Setup closes by keyboard");
        const auto before = encodeDocument(doc);
        const auto capturedSections = doc.activeSections();
        const auto history = doc.history().total;
        const auto camera = view.renderCamera();
        check(!camera.orthographic &&
                  std::abs(camera.verticalFov - view.fieldOfView() * std::numbers::pi / 180) < 1e-8,
              "Perspective camera captured");
        view.standardView(1);
        const auto top = view.renderCamera();
        check(top.orthographic && top.up.y == 1 && top.up.z == 0,
              "Top-view orthographic camera has stable up vector");
        view.standardView(6);
        check(view.renderCamera().up.y == -1, "Bottom-view up vector");
        view.standardView(0);
        auto executable = [&](QString mode) {
            const auto path = files.path() + "/blender-" + mode;
            QFile file(path);
            check(file.open(QIODevice::WriteOnly), "Create fake worker launcher");
            const auto script =
                ("#!/bin/sh\nexec " +
                 quote(QCoreApplication::applicationDirPath() + "/blender_job_tests") + " --fake " +
                 quote(mode) + " \"$@\"\n")
                    .toUtf8();
            check(file.write(script) == script.size(), "Write worker launcher");
            file.close();
            check(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner),
                  "Worker executable");
            return path;
        };
        action->trigger();
        auto *path = setup->findChild<QLineEdit *>("blenderPath");
        auto *backend = setup->findChild<QComboBox *>("renderBackend");
        path->setText(executable("success"));
        probe->click();
        check(QTest::qWaitFor([&] { return start->isEnabled(); }),
              "Valid CPU probe enables Render");
        backend->setCurrentText("METAL");
        check(!start->isEnabled(), "Backend change invalidates previous device proof");
        probe->click();
        check(QTest::qWaitFor([&] { return probe->isEnabled(); }),
              "Unsupported device probe completes");
        check(!start->isEnabled(), "Unsupported/mismatched device cannot start from setup");
        backend->setCurrentText("CPU");
        setup->resize(340, 350);
        QTest::qWait(20);
        check(setup->rect().contains(start->mapTo(setup, start->rect().bottomRight())),
              "Primary controls remain visible in short setup sheet");
        setup->hide();
        RenderOptions settings;
        settings.settings = {128, 128, 4, 0};
        if (!qEnvironmentVariableIsEmpty("SKETCHYUP_RENDER_FOCUS"))
            settings.camera = camera;
        BlenderJob::Options worker;
        worker.executable = executable("success");
        panel.start(settings, worker, true);
        check(panel.active(), "Preparation is asynchronous");
        panel.cancel();
        check(QTest::qWaitFor([&] { return !panel.active(); }, 10000),
              "Preparation cancellation completes");
        check(!panel.latest() && panel.status().contains("canceled"),
              "Canceled preparation publishes no image");
        for (auto mode : {"unsupported", "invalid-png", "fail"}) {
            worker.executable = executable(mode);
            panel.start(settings, worker, true);
            check(QTest::qWaitFor([&] { return !panel.active(); }, 10000),
                  "Failed worker terminates");
            check(!panel.latest() && !panel.status().contains("ready"),
                  "Failure never creates result tab");
        }
        worker.executable = executable("hang");
        panel.start(settings, worker, false);
        check(QTest::qWaitFor([&] { return panel.status() == "Rendering"; }, 10000),
              "Background worker started");
        panel.cancel();
        check(QTest::qWaitFor([&] { return !panel.active(); }, 10000), "Running job canceled");
        check(encodeDocument(doc) == before && doc.history().total == history,
              "Render and cancellation preserve document and undo");
        worker.executable = executable("success");
        const auto real = qEnvironmentVariable("SKETCHYUP_BLENDER_TEST");
        if (!real.isEmpty()) {
            worker.executable = real;
            if (!qEnvironmentVariableIsEmpty("SKETCHYUP_RENDER_UI_EVIDENCE"))
                settings.settings = {512, 512, 32, 0};
        }
        panel.start(settings, worker, false);
        const auto captured = doc.revision();
        doc.addFace({{{10, 0, 0}, {11, 0, 0}, {11, 1, 0}, {10, 1, 0}}});
        view.refresh();
        emit view.changed();
        check(QTest::qWaitFor([&] { return !panel.active(); }, 60000),
              "Render finishes while modeling continues");
        check(panel.latest() && panel.latest()->manifest.value("revision").toString() ==
                                    QString::number(captured),
              "Verified result belongs to captured revision");
        check(doc.activeSections() == capturedSections,
              "Native render leaves captured section activation unchanged");
        if (qEnvironmentVariableIsSet("SKETCHYUP_RENDER_SECTION_FIXTURE")) {
            check(panel.latest()->manifest["losses"].toObject()["sectionCutEdgesOmitted"].toInt() > 0,
                  "Native section result reports omitted cut-edge lines");
            if (!real.isEmpty()) {
                int green{};
                const auto &image = panel.latest()->image;
                for (int y = 0; y < image.height(); ++y)
                    for (int x = 0; x < image.width(); ++x) {
                        const auto color = image.pixelColor(x, y);
                        green += color.green() > 50 && color.green() > color.red() * 2 &&
                                 color.green() > color.blue() * 2;
                    }
                check(green > 10, "Actual native Blender result contains green section caps");
            }
        }
        check(tabs->count() == 2 && tabs->currentIndex() == 1,
              "Result is beside model in native window");
        auto *provenance = window.findChild<QLabel *>("renderProvenance");
        check(provenance && provenance->text().contains("model has changed since"),
              "Changed source is labeled");
        if (qEnvironmentVariableIsSet("SKETCHYUP_RENDER_SECTION_FIXTURE"))
            check(provenance->text().contains("section cut edges omitted"),
                  "Native result labels section line-transfer limitation");
        const auto imagePath = files.path() + "/result.png";
        panel.saveLatest(imagePath);
        check(read(imagePath) == panel.latest()->png, "Saved PNG matches verified bytes");
        window.resize(640, 480);
        QTest::qWait(30);
        check(tabs->currentWidget()->width() > 120, "Result usable in narrow window");
        window.resize(1200, 850);
        QTest::qWait(30);
        if (const auto evidence = qEnvironmentVariable("SKETCHYUP_RENDER_UI_EVIDENCE");
            !evidence.isEmpty()) {
            check(window.grab().save(evidence + ".png"), "Capture native render UI");
            panel.saveLatest(evidence + "-image.png");
            QFile manifest(evidence + "-manifest.json");
            const auto bytes = QJsonDocument(panel.latest()->manifest).toJson();
            check(manifest.open(QIODevice::WriteOnly | QIODevice::NewOnly) &&
                      manifest.write(bytes) == bytes.size(),
                  "Retain verified native render manifest");
        }
        worker.executable = executable("success");
        for (int i = 0; i < 2; ++i) {
            panel.start(settings, worker, false);
            check(QTest::qWaitFor([&] { return !panel.active(); }, 10000),
                  "Subsequent render completes");
        }
        check(tabs->count() == 3, "Only two result tabs are retained");
        provenance = tabs->currentWidget()->findChild<QLabel *>("renderProvenance");
        doc = Document();
        panel.refreshProvenance();
        check(provenance->text().contains("another model session"),
              "Replacement document never adopts old render provenance");
        tabs->setCurrentIndex(0);
        while (tabs->count() > 1)
            tabs->tabCloseRequested(1);
        check(tabs->count() == 1 && !panel.latest(), "Result close releases result");
        tabs->tabCloseRequested(0);
        check(tabs->count() == 1, "Model tab cannot close");
        window.close();
        std::cout << "Native render setup, cancellation, revision capture, result and image save "
                     "passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
