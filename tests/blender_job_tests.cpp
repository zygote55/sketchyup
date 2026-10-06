#include "core/assets.hpp"
#include "core/face_textures.hpp"
#include "core/materials.hpp"
#include "integrations/blender_job.hpp"
#include "io/assets.hpp"
#include "io/texture_image.hpp"
#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QThread>
#include <QTimer>
#include <future>
#include <iostream>
using namespace sketchy;
using Phase = BlenderJob::Phase;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
QByteArray read(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Read test artifact");
    return file.readAll();
}
void write(const QString &path, const QByteArray &data) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size())
        throw std::runtime_error(
            ("Write test artifact " + path + ": " + file.errorString()).toStdString());
}
QString hash(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
int fake(QCoreApplication &app, const QStringList &args) {
    const auto mode = args[2];
    const auto requestPath = args.last();
    const auto root = QFileInfo(requestPath).absolutePath();
    const auto request = QJsonDocument::fromJson(read(requestPath)).object();
    if (mode == "hang")
        return app.exec();
    if (mode == "flood") {
        std::cout << std::string(3 * 1024 * 1024, 'x') << std::flush;
        return app.exec();
    }
    if (mode == "crash")
        return 7;
    QJsonObject result{{"apiVersion", 1},
                       {"adapter", "sketchyup-blender-v1"},
                       {"blenderVersion", QJsonArray{5, 2, 1}}};
    if (mode == "unsupported") {
        result["status"] = "failed";
        result["code"] = "unsupported_version";
    } else if ((mode == "fallback" && request.value("backend") != "CPU") || mode == "fail") {
        result["status"] = "failed";
        result["code"] = "device_unavailable";
    } else if (request.value("operation") == "probe") {
        result["status"] = "available";
        result["backends"] = QJsonArray{"CPU"};
        result["devices"] =
            QJsonArray{QJsonObject{{"id", "CPU"}, {"name", "CPU"}, {"backend", "CPU"}}};
    } else {
        const auto source =
            QJsonDocument::fromJson(
                read(request.value("sourceDirectory").toString() + "/manifest.json"))
                .object();
        const auto settings = source.value("settings").toObject();
        QImage image(settings.value("width").toInt(), settings.value("height").toInt(),
                     QImage::Format_RGBA8888);
        image.fill(Qt::red);
        QByteArray bytes;
        QBuffer buffer(&bytes);
        check(buffer.open(QIODevice::WriteOnly) && image.save(&buffer, "PNG"), "Encode fixture");
        if (mode == "invalid-png")
            bytes = "not a PNG";
        write(root + "/image.png", bytes);
        result["status"] = "succeeded";
        result["documentId"] = source.value("documentId");
        result["revision"] = source.value("revision");
        result["settings"] = settings;
        result["manifestSha256"] = request.value("manifestSha256");
        result["sceneSha256"] = source.value("scene").toObject().value("sha256");
        result["device"] =
            QJsonObject{{"backend", request.value("backend")}, {"id", request.value("deviceId")}};
        result["preset"] = QJsonObject{{"name", "studio-v1"}, {"engine", "CYCLES"}, {"threads", 4}};
        result["image"] = QJsonObject{{"file", "image.png"},
                                      {"width", image.width()},
                                      {"height", image.height()},
                                      {"bytes", bytes.size()},
                                      {"sha256", hash(bytes)}};
        if (mode == "source")
            result["revision"] = -1;
        if (mode == "device")
            result["device"] = QJsonObject{{"backend", "CPU"}, {"id", "wrong"}};
        if (mode == "preset")
            result["preset"] = QJsonObject{};
        auto description = result.value("image").toObject();
        if (mode == "hash")
            description["sha256"] = "wrong";
        if (mode == "dimensions")
            description["width"] = image.width() + 1;
        result["image"] = description;
        if (mode == "missing")
            QFile::remove(root + "/image.png");
        if (mode == "fractional-version")
            result["blenderVersion"] = QJsonArray{5, 2, .5};
    }
    std::cout << "SKETCHYUP_PROGRESS {\"phase\":\"rendering\"}\n" << std::flush;
    write(root + "/result.json", QJsonDocument(result).toJson());
    return 0;
}
void wait(BlenderJob &job, int limit = 15000) {
    QElapsedTimer timer;
    timer.start();
    while (!job.done() && timer.elapsed() < limit) {
        QCoreApplication::processEvents();
        QThread::msleep(2);
    }
    if (!job.done())
        job.cancel();
    check(job.done(), "Job terminated before test deadline");
}
QString quote(QString value) { return "'" + value.replace("'", "'\\''") + "'"; }
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        const auto args = app.arguments();
        if (args.size() > 2 && args[1] == "--fake")
            return fake(app, args);
        QTemporaryDir files;
        check(files.isValid(), "Test directory");
        Document document;
        const auto body = document.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}});
        document.extrude(body, document.bodies().at(body)->surface.faces.begin()->first, 4);
        Id texture{};
        if (args.contains("--real")) {
            texture = createAsset(
                document, "Packaged render checker", "image/png",
                assetPayload(encodeTexturePng(TextureImage(
                    2, 2, {255, 0, 0, 255, 0, 255, 0, 128, 0, 0, 255, 0, 255, 255, 255, 255}))));
            const auto faces = document.bodies().at(body)->surface.faces;
            for (const auto &[face, record] : faces) {
                auto back = effectiveFaceTextureMapping(*document.bodies().at(body), face, true);
                back.offset = {.25, .5};
                assignTextureMapping(document, body, face, back, false, true);
            }
        }
        const auto front = createMaterial(document, "Render front", {.8f, .2f, .1f}, .7f, texture);
        const auto back = createMaterial(document, "Render back", {.1f, .2f, .8f}, 1, texture);
        assignMaterial(document, body, {}, front, true, false);
        assignMaterial(document, body, {}, back, false, true);
        RenderOptions render;
        render.settings = {128, 128, 4, 0};
        auto snapshot = RenderSnapshot::capture(document, render);
        auto input = std::async(std::launch::async, [snapshot] {
                         return PreparedRender::prepare(snapshot);
                     }).get();
        const auto sourceBytes = read(input->sourceDirectory() + "/scene.glb");
        int launcherSequence{};
        auto options = [&](const QString &mode) {
            // A launcher may still be entering exec when the next job starts.
            // Never truncate a path that another process may be executing.
            const auto path = files.path() + "/" + mode + "-" + QString::number(++launcherSequence);
            write(path, ("#!/bin/sh\nexec " + quote(app.applicationFilePath()) + " --fake " +
                         quote(mode) + " \"$@\"\n")
                            .toUtf8());
            check(
                QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner),
                "Executable fixture");
            BlenderJob::Options options;
            options.executable = path;
            options.timeoutMs = 5000;
            return options;
        };
        if (args.contains("--real")) {
            const auto executable = qEnvironmentVariable("SKETCHYUP_BLENDER_TEST");
            if (executable.isEmpty())
                return 77;
            check(input->manifest()["textureImages"].toArray().size() == 1 &&
                      input->manifest()["losses"]
                              .toObject()["textureAssetsPreservedWithoutUVMapping"] == 0,
                  "Real worker receives a packaged image with independent side UVs");
            BlenderJob::Options real;
            real.executable = executable;
            real.timeoutMs = 60000;
            BlenderJob probe;
            probe.probe(real);
            wait(probe, 20000);
            check(probe.phase() == Phase::Succeeded, "Real Blender CPU capability probe");
            BlenderJob job;
            job.start(input, real);
            document.addFace({{{10, 0, 0}, {11, 0, 0}, {11, 1, 0}, {10, 1, 0}}});
            wait(job, 65000);
            if (!job.result())
                std::cerr << QJsonDocument(job.report()).toJson().constData();
            check(job.phase() == Phase::Succeeded && job.result(),
                  "Real CPU render and verification");
            const auto result = job.result();
            check(result->manifest.value("revision") == input->manifest().value("revision"),
                  "Render retains captured revision while document changes");
            bool variation = false;
            for (int y = 0; y < result->image.height(); ++y)
                for (int x = 0; x < result->image.width(); ++x)
                    variation |= result->image.pixel(x, y) != result->image.pixel(0, 0);
            check(variation, "Real render contains scene pixels");
            if (const auto output = qEnvironmentVariable("SKETCHYUP_RENDER_EVIDENCE");
                !output.isEmpty()) {
                write(output + ".png", result->png);
                write(output + ".json", QJsonDocument(result->manifest).toJson());
            }
            real.backend = "METAL";
            real.deviceId = "deliberately-unavailable-test-device";
            BlenderJob fallback;
            fallback.start(input, real);
            wait(fallback, 65000);
            check(fallback.result() &&
                      fallback.result()->manifest.value("cpuFallbackUsed") == true &&
                      fallback.report().value("attempts").toArray().size() == 2,
                  "Real unavailable GPU falls back once to CPU");
            real.allowCpuFallback = false;
            BlenderJob noFallback;
            noFallback.start(input, real);
            wait(noFallback, 65000);
            check(noFallback.phase() == Phase::Failed && !noFallback.result(),
                  "Real GPU failure without fallback");
            real.backend = "CPU";
            BlenderJob canceled;
            canceled.start(input, real);
            QTimer::singleShot(50, &canceled, [&] { canceled.cancel(); });
            wait(canceled);
            check(canceled.phase() == Phase::Canceled && !canceled.result(),
                  "Real process cancellation");
            std::cout << "Real Blender probe, CPU render, fallback, and cancellation passed\n";
            return 0;
        }
        BlenderJob unavailable;
        auto missing = options("success");
        missing.executable = files.path() + "/absent";
        unavailable.probe(missing);
        check(unavailable.phase() == Phase::Unavailable, "Missing executable is explicit");
        BlenderJob probe;
        probe.probe(options("success"));
        wait(probe);
        check(probe.phase() == Phase::Succeeded && !probe.result(),
              "Probe publishes capabilities only");
        for (const auto &mode :
             {"success", "unsupported", "crash", "invalid-png", "hash", "dimensions", "source",
              "device", "preset", "missing", "fractional-version"}) {
            BlenderJob job;
            job.start(input, options(mode));
            wait(job);
            if (QString(mode) == "success")
                check(job.result() && job.phase() == Phase::Succeeded,
                      "Verified image publication");
            else
                check(!job.result() &&
                          (job.phase() == Phase::Failed || job.phase() == Phase::Unavailable),
                      "Bad worker result never publishes an image");
        }
        for (const auto &mode : {"fallback", "fail", "crash"}) {
            BlenderJob job;
            auto gpu = options(mode);
            gpu.backend = "CUDA";
            gpu.deviceId = "fixture-gpu";
            job.start(input, gpu);
            wait(job);
            check(job.report().value("attempts").toArray().size() == 2, "At most one CPU retry");
            check(bool(job.result()) == (QString(mode) == "fallback"),
                  "Fallback success/failure is honest");
        }
        for (const auto &mode : {"hang", "flood"}) {
            BlenderJob job;
            auto bounded = options(mode);
            // The flood test measures bytes, not sanitizer/process startup speed.
            bounded.timeoutMs = QString(mode) == "hang" ? 200 : 5000;
            job.start(input, bounded);
            wait(job);
            check(!job.result() &&
                      (QString(mode) == "hang" ? job.phase() == Phase::TimedOut
                                               : job.report().value("code") == "OUTPUT_LIMIT"),
                  "Process runtime/output is bounded");
        }
        for (const auto phase : {Phase::Rendering, Phase::Verifying}) {
            BlenderJob job;
            QObject::connect(&job, &BlenderJob::changed, &job, [&] {
                if (job.phase() == phase)
                    job.cancel();
            });
            job.start(input, options("success"));
            wait(job);
            check(job.phase() == Phase::Canceled && !job.result(),
                  "Reentrant cancellation never publishes output");
        }
        {
            BlenderJob a, b, c;
            a.start(input, options("hang"));
            b.start(input, options("hang"));
            c.start(input, options("hang"));
            check(c.done() && c.report().value("code") == "BUSY", "Active worker bound");
            a.cancel();
            b.cancel();
            wait(a);
            wait(b);
        }
        {
            BlenderJob destroyed;
            destroyed.start(input, options("hang"));
            QCoreApplication::processEvents();
        }
        BlenderJob final;
        final.start(input, options("success"));
        wait(final);
        check(final.result() && read(input->sourceDirectory() + "/scene.glb") == sourceBytes,
              "Destruction releases worker slot; shared immutable inputs are reusable");
        auto foreign = std::async(std::launch::async, [&] {
            try {
                final.phase();
            } catch (const std::exception &) {
                return true;
            }
            return false;
        });
        check(foreign.get(), "Owner-thread guard");
        std::cout << "Blender worker lifecycle and artifact validation passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
