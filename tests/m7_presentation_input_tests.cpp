#include "app/window.hpp"
#include "m7_fixture.hpp"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTest>
#include <iostream>
using namespace sketchy;
using m7::check;
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(0);
    QSurfaceFormat::setDefaultFormat(format);
    QTemporaryDir temporary;
    qputenv("XDG_CONFIG_HOME", temporary.path().toUtf8());
    qputenv("XDG_DATA_HOME", temporary.path().toUtf8());
    QApplication app(argc, argv);
    try {
        auto study = m7::study();
        auto report = m7::verify(study);
        Window window;
        window.resize(1280, 900);
        auto &doc = window.document();
        auto &view = *window.viewport();
        doc = std::move(study.document);
        view.setReducedMotion(true);
        view.refresh();
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Presentation study native window exposed");
        const auto retained = qEnvironmentVariable("SKETCHYUP_M7_EVIDENCE");
        const auto folder = retained.isEmpty() ? temporary.path() : retained;
        check(QDir().mkpath(folder), "Native study output");
        QImage previous;
        for (const auto scene : {study.perspective, study.plan, study.section}) {
            view.recallSavedScene(scene);
            view.refresh();
            QCoreApplication::processEvents();
            view.grabFramebuffer();
            check(QTest::qWaitFor([&] { return !view.texturesPending(); }, 10000),
                  "Study textures ready");
            const auto model = encodeDocument(doc);
            const auto history = doc.history().total;
            const auto image = view.renderRaster({1200, 900});
            check(image.size() == QSize(1200, 900) && !image.isNull(),
                  "Study exports exact-resolution view");
            check(previous.isNull() || image != previous,
                  "Saved study views produce distinct images");
            previous = image;
            check(encodeDocument(doc) == model && doc.history().total == history,
                  "View export preserves model and history");
            const auto camera = renderSceneCamera(*doc.scenes().at(scene)->snapshot.camera);
            const auto actual = view.renderCamera();
            check(length(actual.position - camera.position) < 1e-6 &&
                      length(actual.target - camera.target) < 1e-6 &&
                      actual.orthographic == camera.orthographic,
                  "Native recall matches saved study camera");
            for (const auto &[id, annotation] : doc.annotations()) {
                (void)id;
                check(measureAnnotation(doc, *annotation).state == AnchorState::Resolved,
                      "Study annotations stay resolved across views");
            }
            check(image.save(folder + "/native-view-" + QString::number(scene) + ".png"),
                  "Write study native PNG");
        }
        saveDocument(doc, folder + "/native-study.sketchyup");
        check(encodeDocument(loadDocument(folder + "/native-study.sketchyup")) ==
                  encodeDocument(doc),
              "Native study reopens with active section and scenes");
        QFile file(folder + "/native-study.json");
        check(file.open(QIODevice::WriteOnly), "Native study report");
        report["nativeExactSize"] = QJsonArray{1200, 900};
        report["devicePixelRatio"] = view.devicePixelRatioF();
        file.write(QJsonDocument(report).toJson());
        std::cout << "M7 native views, resolved dimensions, textures, camera recall, section state "
                     "and exact PNG export pass; DPR="
                  << view.devicePixelRatioF() << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
