#include "app/window.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSurfaceFormat>
#include <QTimer>
#include <iostream>
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(4);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication app(argc, argv);
    QApplication::setApplicationName("SketchyUp");
    QGuiApplication::setDesktopFileName("io.sketchyup.SketchyUp");
    QApplication::setOrganizationName("SketchyUp");
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"demo", "Open original courtyard example"});
    parser.addOption({"smoke", "Run native graphics/picking smoke check and exit"});
    parser.addOption({"capture", "Save application screenshot", "path"});
    parser.addOption(
        {"benchmark", "Render repeated triangles and report frame timing", "triangles"});
    parser.addPositionalArgument("model", "Optional .sketchyup file");
    parser.process(app);
    sketchy::Window window;
    try {
        if (parser.isSet("demo") || parser.isSet("smoke"))
            window.demo();
        if (!parser.positionalArguments().empty())
            window.openPath(parser.positionalArguments()[0]);
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    window.show();
    if (parser.isSet("benchmark")) {
        bool ok = false;
        int n = parser.value("benchmark").toInt(&ok);
        if (!ok || n < 1 || n > 1000000)
            return 2;
        window.viewport()->benchmark(n);
    }
    if (parser.isSet("smoke") || parser.isSet("capture") || parser.isSet("benchmark")) {
        auto *timer = new QTimer(&window);
        auto frames = std::make_shared<int>(0);
        auto total = std::make_shared<double>(0);
        QObject::connect(timer, &QTimer::timeout, &window, [&, frames, total, timer] {
            window.viewport()->update();
            ++*frames;
            if (*frames > 5)
                *total += window.viewport()->lastFrameMs();
            if (*frames < 25)
                return;
            timer->stop();
            bool valid = window.viewport()->rendererReady();
            // Face center selected through logical-pixel projection at the actual device scale.
            auto hit = window.viewport()->pick(window.viewport()->project({0, -.05, .8}));
            if (parser.isSet("smoke"))
                valid = valid && hit.first == 6;
            bool capture = true;
            if (parser.isSet("capture"))
                capture = window.grab().save(parser.value("capture"));
            QJsonObject result{{"platform", QGuiApplication::platformName()},
                               {"devicePixelRatio", window.devicePixelRatioF()},
                               {"graphics", window.viewport()->graphicsDescription()},
                               {"pickingBody", int(hit.first)},
                               {"rendererReady", window.viewport()->rendererReady()},
                               {"meanFrameMs", *total / 20},
                               {"timing", parser.isSet("benchmark")
                                              ? "GPU-complete repeated-triangle instancing"
                                              : "CPU submission only"},
                               {"benchmarkTriangles", parser.value("benchmark").toInt()},
                               {"captureSaved", capture},
                               {"passed", valid && capture}};
            std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).toStdString() << '\n';
            app.exit(valid && capture ? 0 : 1);
        });
        timer->start(40);
    }
    return app.exec();
}
