#include "app/inspection_service.hpp"
#include "app/native_mcp.hpp"
#include "app/surface_format.hpp"
#include "app/window.hpp"
#include "io/model_style_io.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRawFont>
#include <QTimer>
#include <iostream>
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(4);
    sketchy::setDefaultViewportFormat(format);
    QApplication app(argc, argv);
    QApplication::setApplicationName("SketchyUp");
    QGuiApplication::setDesktopFileName("io.sketchyup.SketchyUp");
    QApplication::setOrganizationName("SketchyUp");
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption(
        {"mcp-inspection-capabilities", "Print native MCP inspection schemas and exit"});
    parser.addOption({"mcp-inspection-socket",
                      "Expose this model through a private native MCP inspection socket",
                      "socket"});
    parser.addOption({"inspection-capabilities", "Print native inspection schemas and exit"});
    parser.addOption({"demo", "Open original courtyard example"});
    parser.addOption({"smoke", "Run native graphics/picking smoke check and exit"});
    parser.addOption({"capture", "Save application screenshot", "path"});
    parser.addOption({"benchmark",
                      "Render independent triangle buffers and report GPU frame timing",
                      "triangles"});
    parser.addOption(
        {"instanced", "Use repeated-triangle instancing instead of independent triangles"});
    parser.addPositionalArgument("model", "Optional .sketchyup file");
    parser.process(app);
    if (parser.isSet("inspection-capabilities") || parser.isSet("mcp-inspection-capabilities")) {
        if (parser.optionNames().size() != 1 || !parser.positionalArguments().empty()) {
            std::cerr
                << "Inspection discovery cannot be combined with model or rendering options\n";
            return 2;
        }
        std::cout << QJsonDocument(parser.isSet("mcp-inspection-capabilities")
                                       ? sketchy::nativeMcpCapabilities()
                                       : sketchy::desktopInspectionCapabilities())
                         .toJson(QJsonDocument::Compact)
                         .toStdString()
                  << '\n';
        return 0;
    }
    if (parser.isSet("mcp-inspection-socket") &&
        (parser.optionNames().size() != 1 || parser.positionalArguments().size() != 1)) {
        std::cerr
            << "Native MCP requires exactly one model and no rendering or discovery options\n";
        return 2;
    }
    sketchy::Window window;
    std::unique_ptr<sketchy::NativeMcpService> mcp;
    try {
        if (parser.isSet("demo") || parser.isSet("smoke"))
            window.demo();
        if (!parser.positionalArguments().empty())
            window.openPath(parser.positionalArguments()[0]);
        if (parser.isSet("mcp-inspection-socket"))
            mcp = std::make_unique<sketchy::NativeMcpService>(
                *window.viewport(), parser.value("mcp-inspection-socket"));
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    window.show();
    if (!parser.isSet("smoke") && !parser.isSet("capture") && !parser.isSet("benchmark") && !mcp) {
        QTimer::singleShot(0, &window, [&window] {
            window.startUnits();
            window.startRecovery();
            window.showRecovery(true);
        });
    }
    if (mcp)
        QTimer::singleShot(0, &window, [&window] { window.startRecovery(); });
    if (parser.isSet("benchmark")) {
        bool ok = false;
        int n = parser.value("benchmark").toInt(&ok);
        if (!ok || n < 1 || n > 1000000)
            return 2;
        window.viewport()->benchmark(n, parser.isSet("instanced"));
    }
    if (parser.isSet("smoke") || parser.isSet("capture") || parser.isSet("benchmark")) {
        auto *timer = new QTimer(&window);
        auto frames = std::make_shared<int>(0);
        auto total = std::make_shared<double>(0);
        auto ticks = std::make_shared<int>(0);
        auto lastRenderedFrame = std::make_shared<std::uint64_t>(0);
        QObject::connect(
            timer, &QTimer::timeout, &window, [&, frames, total, ticks, lastRenderedFrame, timer] {
                window.viewport()->update();
                auto stats = window.viewport()->renderStats();
                if (++*ticks > 250) {
                    timer->stop();
                    std::cerr << "Timed out waiting for rendered frames\n";
                    app.exit(1);
                    return;
                }
                if (stats.frames == *lastRenderedFrame)
                    return;
                *lastRenderedFrame = stats.frames;
                ++*frames;
                if (*frames > 5)
                    *total += window.viewport()->lastFrameMs();
                if (*frames < 25)
                    return;
                timer->stop();
                const auto font = QRawFont::fromFont(QApplication::font());
                const bool textReady =
                    font.isValid() && font.supportsCharacter('A') && font.supportsCharacter(0x2026);
                bool valid = window.viewport()->rendererReady() && stats.glError == 0 && textReady;
                // Face center selected through logical-pixel projection at the actual device scale.
                auto hit = window.viewport()->pick(window.viewport()->project({0, -.05, .8}));
                if (parser.isSet("smoke"))
                    valid = valid && hit.first == 6;
                bool capture = true;
                if (parser.isSet("capture"))
                    capture = window.grab().save(parser.value("capture"));
                QJsonObject result{
                    {"documentId", QString::fromStdString(window.document().identity())},
                    {"revision", QString::number(window.document().revision())},
                    {"bodies", int(window.document().bodies().size())},
                    {"platform", QGuiApplication::platformName()},
                    {"devicePixelRatio", window.devicePixelRatioF()},
                    {"graphics", window.viewport()->graphicsDescription()},
                    {"pickingBody", int(hit.first)},
                    {"rendererReady", window.viewport()->rendererReady()},
                    {"textReady", textReady},
                    {"meanFrameMs", *total / 20},
                    {"sampledFrames", 20},
                    {"warmupFrames", 5},
                    {"timing",
                     parser.isSet("benchmark")
                         ? (parser.isSet("instanced") ? "GPU-complete repeated-triangle instancing"
                                                      : "GPU-complete independent triangles")
                         : "CPU submission only"},
                    {"benchmarkTriangles", parser.value("benchmark").toInt()},
                    {"modelStyle", sketchy::encodeModelStyle(window.document().style())},
                    {"profileEdges", double(stats.profileEdges)},
                    {"geometryUploads", double(stats.geometryUploads)},
                    {"uploadedBytes", double(stats.uploadedBytes)},
                    {"frames", double(stats.frames)},
                    {"contextGeneration", int(stats.contextGeneration)},
                    {"glError", int(stats.glError)},
                    {"captureSaved", capture},
                    {"passed", valid && capture}};
                std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).toStdString()
                          << '\n';
                app.exit(valid && capture ? 0 : 1);
            });
        timer->start(40);
    }
    return app.exec();
}
