#include "text/text_worker.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F operation) {
    try {
        operation();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Invalid worker operation accepted");
}
} // namespace
int main(int argc, char **argv) {
    qunsetenv("DISPLAY");
    qunsetenv("WAYLAND_DISPLAY");
    qputenv("QT_QPA_PLATFORMTHEME", "gtk3");
    QCoreApplication app(argc, argv);
    try {
        check(!qEnvironmentVariableIsSet("DISPLAY") &&
                  !qEnvironmentVariableIsSet("WAYLAND_DISPLAY"),
              "Worker test runs without a display");
        TextGeometrySettings settings;
        settings.text = QString::fromUtf8("O café\nلا");
        settings.allowSubstitution = true;
        settings.height = .2;
        settings.depth = .03;
        const auto result = runTextWorker(settings);
        check(!result.regions.empty() && result.holes >= 1 && result.lines == 2 &&
                  !result.fonts.empty(),
              "Real headless worker returns shaped native geometry and provenance");
        const auto encoded = encodeTextGeometry(result);
        const auto restored = decodeTextGeometry(encoded);
        check(restored.regions == result.regions && restored.fonts.size() == result.fonts.size(),
              "Geometry transport roundtrip exact");
        const auto repeat = runTextWorker(settings);
        check(repeat.regions == result.regions, "Fresh worker processes reproduce geometry");
        auto invalid = settings;
        invalid.height = 0;
        rejects([&] { runTextWorker(invalid); });
        invalid = settings;
        invalid.family = "SketchyUp missing font 591d";
        invalid.allowSubstitution = false;
        rejects([&] { runTextWorker(invalid); });
        auto malformed = encoded;
        malformed["extra"] = true;
        rejects([&] { decodeTextGeometry(malformed); });
        malformed = encoded;
        malformed["points"] = "1";
        rejects([&] { decodeTextGeometry(malformed); });
        malformed = encoded;
        auto fonts = malformed["fonts"].toArray();
        auto font = fonts[0].toObject();
        font["fingerprint"] = "not-a-hash";
        fonts[0] = font;
        malformed["fonts"] = fonts;
        rejects([&] { decodeTextGeometry(malformed); });
        malformed = encoded;
        auto document = malformed["document"].toObject();
        document["units"] = "mm";
        malformed["document"] = document;
        rejects([&] { decodeTextGeometry(malformed); });
        malformed = encoded;
        document = malformed["document"].toObject();
        auto bodies = document["bodies"].toArray();
        auto body = bodies[0].toObject();
        body["hidden"] = true;
        bodies[0] = body;
        document["bodies"] = bodies;
        malformed["document"] = document;
        rejects([&] { decodeTextGeometry(malformed); });
        QTemporaryDir files;
        check(files.isValid(), "Worker test temporary directory");
        auto script = [&](const QByteArray &body) {
            const auto path = files.filePath("worker.py");
            QFile f(path);
            check(f.open(QIODevice::WriteOnly), "Fake worker file");
            f.write("#!/usr/bin/python3\nimport sys,time\n" + body);
            f.close();
            f.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
            return path;
        };
        TextWorkerOptions options;
        options.executable = script("time.sleep(5)\n");
        options.timeoutMs = 100;
        rejects([&] { runTextWorker(settings, options); });
        options.timeoutMs = 5000;
        options.executable = script("sys.stdout.write('x'*10000)\n");
        options.responseBytes = 1024;
        rejects([&] { runTextWorker(settings, options); });
        options.responseBytes = textWorkerResponseLimit;
        options.executable = script("sys.stdout.write('{bad json}')\n");
        rejects([&] { runTextWorker(settings, options); });
        options.executable = script("sys.stdout.write('{\"protocol\":2,\"ok\":true}')\n");
        rejects([&] { runTextWorker(settings, options); });
        options.executable = files.filePath("missing-worker");
        rejects([&] { runTextWorker(settings, options); });
        std::cout << "Headless font worker, exact geometry transport, deadline and malformed "
                     "responses passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
