#include "core/annotations.hpp"
#include "io/document_io.hpp"
#include "io/measured_request.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Expected measured settings rejection");
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir scratch;
        check(scratch.isValid(), "Scratch folder");
        Document doc;
        doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 1, 0}, {0, 1, 0}}});
        AnnotationRecord annotation;
        annotation.name = "Length";
        annotation.anchors = {pointAnchor({0, 0, 0}), pointAnchor({2, 0, 0})};
        annotation.offset = {0, -.5, 0};
        createAnnotation(doc, annotation);
        const auto native = scratch.filePath("source.sketchyup");
        saveDocument(doc, native);
        auto read = [](const QString &path) {
            QFile f(path);
            check(f.open(QIODevice::ReadOnly), "Read fixture");
            return f.readAll();
        };
        const auto before = read(native);
        QJsonObject request{{"apiVersion", 1},
                            {"mode", "technical-lines"},
                            {"format", "svg"},
                            {"page", QJsonObject{{"scaleDenominator", 50},
                                                 {"widthMm", 254},
                                                 {"heightMm", 203.2},
                                                 {"marginMm", 10},
                                                 {"includeHidden", false}}},
                            {"camera", QJsonObject{{"projection", "orthographic"},
                                                   {"position", QJsonArray{0, 0, 10}},
                                                   {"target", QJsonArray{0, 0, 0}},
                                                   {"up", QJsonArray{0, 1, 0}}}}};
        const auto settings = scratch.filePath("settings.json");
        auto writeSettings = [&](const QJsonObject &value) {
            QFile file(settings);
            check(file.open(QIODevice::WriteOnly), "Settings fixture");
            const auto bytes = QJsonDocument(value).toJson();
            check(file.write(bytes) == bytes.size(), "Settings bytes");
        };
        writeSettings(request);
        const auto cli = QCoreApplication::applicationDirPath() + "/sketchyup-cli";
        auto run = [&](QStringList args, bool success = true) {
            QProcess p;
            auto env = QProcessEnvironment::systemEnvironment();
            env.remove("DISPLAY");
            env.remove("WAYLAND_DISPLAY");
            env.insert("QT_QPA_PLATFORM", "invalid-user-platform");
            env.insert("QT_QPA_PLATFORMTHEME", "gtk3");
            p.setProcessEnvironment(env);
            p.start(cli, args);
            check(p.waitForStarted(10000) && p.waitForFinished(30000) &&
                      p.exitStatus() == QProcess::NormalExit,
                  "Headless measured CLI completion");
            const auto bytes = success ? p.readAllStandardOutput() : p.readAllStandardError();
            if ((p.exitCode() == 0) != success)
                std::cerr << bytes.toStdString();
            check((p.exitCode() == 0) == success, "Measured CLI exit status");
            const auto json = QJsonDocument::fromJson(bytes);
            check(json.isObject(), "Structured measured CLI result");
            return json.object();
        };
        const auto svg = scratch.filePath("view.svg");
        const auto result =
            run({"--export-view", svg, "--input", native, "--view-settings", settings});
        check(result["status"] == "exported" &&
                  result["exportReport"].toObject()["scaleDenominator"] == 50 &&
                  read(svg).contains("width=\"254mm\""),
              "Headless SVG physical export");
        const auto svgBefore = read(svg);
        run({"--export-view", svg, "--input", native, "--view-settings", settings}, false);
        check(read(svg) == svgBefore, "Existing measured output protected");
        run({"--export-view", native, "--input", native, "--view-settings", settings}, false);
        check(read(native) == before, "Native source protected");
        auto pdfRequest = request;
        pdfRequest["format"] = "pdf";
        writeSettings(pdfRequest);
        const auto pdf = scratch.filePath("view.pdf");
        run({"--export-view=" + pdf, "--input", native, "--view-settings", settings});
        check(read(pdf).startsWith("%PDF-"), "Headless PDF font initialization with equals option");
        const auto absent = scratch.filePath("absent.svg");
        run({"--export-view", absent, "--input", native, "--view-settings", settings, "--script",
             "unused"},
            false);
        run({"--export-view", absent, "--input", native, "--view-settings", settings, "-platform",
             "xcb"},
            false);
        run({"--export-view", absent, "--input", native}, false);
        run({"--view-settings", settings}, false);
        check(!QFile::exists(absent), "Rejected options leave no destination");
        for (const auto &field :
             {QString("mode"), QString("camera"), QString("page"), QString("format")}) {
            auto bad = request;
            bad.remove(field);
            rejects([&] { parseMeasuredRequest(bad); });
        }
        for (const QJsonValue &badValue : {QJsonValue(0), QJsonValue(-1), QJsonValue("50"),
                                           QJsonValue(true), QJsonValue(QJsonValue::Null)}) {
            auto bad = request;
            auto page = bad["page"].toObject();
            page["scaleDenominator"] = badValue;
            bad["page"] = page;
            rejects([&] { parseMeasuredRequest(bad); });
        }
        auto bad = request;
        bad["unexpected"] = 1;
        rejects([&] { parseMeasuredRequest(bad); });
        bad = request;
        bad["mode"] = "raster";
        writeSettings(bad);
        run({"--export-view", absent, "--input", native, "--view-settings", settings}, false);
        bad = request;
        auto camera = bad["camera"].toObject();
        camera["projection"] = "perspective";
        bad["camera"] = camera;
        rejects([&] { parseMeasuredRequest(bad); });
        QFile oversized(settings);
        check(oversized.open(QIODevice::WriteOnly), "Oversized settings");
        oversized.write(QByteArray(32769, ' '));
        oversized.close();
        run({"--export-view", absent, "--input", native, "--view-settings", settings}, false);
        check(!QFile::exists(absent) && read(native) == before,
              "All failures preserve source and destination absence");
        std::cout << "Measured CLI: headless SVG/PDF, explicit settings, bounds, source and output "
                     "protection passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
