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
QJsonObject fakeRasterDevice() {
    const QJsonObject graphics{{"backend_type", "OPENGL"},
                               {"device_type", "SOFTWARE"},
                               {"renderer", "Explicit test renderer"},
                               {"vendor", "Test"},
                               {"version", "4.6"}};
    return {{"backend", "OPENGL"},
            {"name", graphics.value("renderer")},
            {"graphics", graphics},
            {"id", "opengl:" + hash(QJsonDocument(graphics).toJson(QJsonDocument::Compact))}};
}
int fake(QCoreApplication &app, const QStringList &args) {
    const auto mode = args[2];
    const auto requestPath = args.last();
    const auto root = QFileInfo(requestPath).absolutePath();
    const auto request = QJsonDocument::fromJson(read(requestPath)).object();
    if (mode == "hang") {
        std::cout << "Retained worker log before interruption\n" << std::flush;
        return app.exec();
    }
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
        result["engines"] = QJsonArray{request.value("backend") == "OPENGL" ? "eevee" : "cycles"};
        result["backends"] = QJsonArray{"CPU"};
        result["devices"] =
            QJsonArray{QJsonObject{{"id", "CPU"}, {"name", "CPU"}, {"backend", "CPU"}}};
        if (request.value("backend") == "OPENGL") {
            result["backends"] = QJsonArray{"OPENGL"};
            result["devices"] = QJsonArray{fakeRasterDevice()};
        }
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
        result["losses"] = source.value("losses");
        result["manifestSha256"] = request.value("manifestSha256");
        result["sceneSha256"] = source.value("scene").toObject().value("sha256");
        result["device"] =
            QJsonObject{{"backend", request.value("backend")}, {"id", request.value("deviceId")}};
        const bool eevee = settings.value("engine") == "eevee";
        result["preset"] =
            QJsonObject{{"name", "studio-v1"},
                        {"engine", eevee ? "BLENDER_EEVEE" : "CYCLES"},
                        {"threads", 4},
                        {"samplingPolicy", eevee ? "eevee-preview-v1" : "cycles-fixed-v1"},
                        {"samples", settings.value("samples")}};
        if (eevee) {
            result["device"] = fakeRasterDevice();
            auto losses = result["losses"].toObject();
            losses["indirectLightingApproximated"] = 1;
            if (settings.value("seed").toInt())
                losses["samplingSeedNotApplied"] = 1;
            result["losses"] = losses;
        }
        if (mode == "graphics") {
            auto device = result["device"].toObject();
            device["graphics"] = QJsonObject{};
            result["device"] = device;
        }

        result["lighting"] = QJsonObject{{"mode", "studio-v1"}, {"worldStrength", .25}};
        if (source["solar"].toObject()["enabled"].toBool()) {
            const auto position = source["solarPosition"].toObject();
            result["lighting"] =
                QJsonObject{{"mode", "solar-v1"},
                            {"worldStrength", .25},
                            {"energy", position["directLightActive"].toBool() ? 3. : 0.},
                            {"angle", .00935},
                            {"shadows", position["shadowsActive"]},
                            {"settings", source["solar"]},
                            {"position", position}};
            auto preset = result["preset"].toObject();
            preset["name"] = "solar-v1";
            result["preset"] = preset;
            auto losses = result["losses"].toObject();
            losses["solarLightingOmitted"] = 0;
            result["losses"] = losses;
        }
        if (source.contains("environment")) {
            const auto environment = source["environment"].toObject();
            auto lighting = result["lighting"].toObject();
            lighting["mode"] =
                source["solar"].toObject()["enabled"].toBool() ? "solar-hdri-v1" : "hdri-v1";
            lighting["worldStrength"] = environment["strength"];
            lighting["environment"] = environment;
            result["lighting"] = lighting;
            auto preset = result["preset"].toObject();
            preset["name"] = lighting["mode"];
            result["preset"] = preset;
            auto losses = result["losses"].toObject();
            losses["environmentLightingOmitted"] = 0;
            result["losses"] = losses;
        }
        if (mode == "lighting")
            result["lighting"] = QJsonObject{};
        if (mode == "losses")
            result["losses"] = QJsonObject{};
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
        const auto hdrPath = files.filePath("environment.hdr");
        QByteArray hdr = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 2\n";
        for (int i = 0; i < 2; ++i)
            hdr.append(char(128)).append(char(64)).append(char(32)).append(char(129));
        write(hdrPath, hdr);
        auto environment = readRenderEnvironment(hdrPath, .5, 90);
        check(QFile::remove(hdrPath), "Remove original environment after capture");
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
            auto sun = document.solar();
            sun.enabled = true;
            sun.latitude = 40;
            sun.longitude = -105;
            sun.time = {2010, 6, 21, 8, 0, 0, -420};
            document.setSolar(sun);
            const auto solarInput =
                PreparedRender::prepare(RenderSnapshot::capture(document, render));
            const auto solarSource = read(solarInput->sourceDirectory() + "/scene.glb");
            BlenderJob solarJob;
            solarJob.start(solarInput, real);
            sun.time.hour = 20;
            document.setSolar(sun);
            wait(solarJob, 65000);
            if (!solarJob.result())
                std::cerr << QJsonDocument(solarJob.report()).toJson().constData();
            check(solarJob.result() &&
                      solarJob.result()->manifest["lighting"].toObject()["settings"] ==
                          solarInput->manifest()["solar"] &&
                      solarJob.result()->manifest["losses"].toObject()["solarLightingOmitted"] ==
                          0 &&
                      read(solarInput->sourceDirectory() + "/scene.glb") == solarSource,
                  "Real sun render uses immutable captured settings and preserves GLB source");
            render.environment = environment;
            const auto environmentInput =
                PreparedRender::prepare(RenderSnapshot::capture(document, render));
            BlenderJob environmentJob;
            environmentJob.start(environmentInput, real);
            wait(environmentJob, 65000);
            if (!environmentJob.result())
                std::cerr << QJsonDocument(environmentJob.report()).toJson().constData();
            check(
                environmentJob.result() &&
                    environmentJob.result()->manifest["lighting"].toObject()["environment"] ==
                        environmentInput->manifest()["environment"] &&
                    environmentJob.result()
                            ->manifest["losses"]
                            .toObject()["environmentLightingOmitted"] == 0 &&
                    read(environmentInput->sourceDirectory() + "/environment.hdr") == hdr,
                "Real worker renders packaged HDR after original removal and verifies conversion");
            auto previewOptions = real;
            previewOptions.backend = "OPENGL";
            previewOptions.deviceId = "probe-only";
            previewOptions.allowCpuFallback = false;
            BlenderJob previewProbe;
            previewProbe.probe(previewOptions);
            wait(previewProbe, 20000);
            check(previewProbe.phase() == Phase::Succeeded, "Real Eevee OpenGL capability probe");
            const auto graphics = previewProbe.report()["worker"].toObject()["devices"].toArray();
            check(graphics.size() == 1, "Eevee reports the active OpenGL renderer explicitly");
            previewOptions.deviceId = graphics[0].toObject()["id"].toString();
            render.settings.engine = RenderEngine::Eevee;
            const auto previewInput =
                PreparedRender::prepare(RenderSnapshot::capture(document, render));
            BlenderJob preview;
            preview.start(previewInput, previewOptions);
            wait(preview, 65000);
            if (!preview.result())
                std::cerr << QJsonDocument(preview.report()).toJson().constData();
            check(preview.result() &&
                      preview.result()->manifest["preset"].toObject()["engine"] ==
                          "BLENDER_EEVEE" &&
                      preview.result()
                              ->manifest["losses"]
                              .toObject()["indirectLightingApproximated"] == 1,
                  "Actual Eevee renders captured sun/HDR with explicit renderer and approximation "
                  "report");
            previewOptions.deviceId = "opengl:changed-device";
            previewOptions.allowCpuFallback = true;
            BlenderJob changedDevice;
            changedDevice.start(previewInput, previewOptions);
            wait(changedDevice, 65000);
            check(!changedDevice.result() &&
                      changedDevice.report()["attempts"].toArray().size() == 1,
                  "Changed OpenGL renderer fails without switching engine to Cycles CPU");
            render.settings.engine = RenderEngine::Cycles;
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
              "device", "preset", "lighting", "losses", "missing", "fractional-version"}) {
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
        auto sun = document.solar();
        sun.enabled = true;
        sun.latitude = 40;
        sun.longitude = -105;
        sun.time = {2010, 6, 21, 8, 0, 0, -420};
        document.setSolar(sun);
        const auto sunInput = PreparedRender::prepare(RenderSnapshot::capture(document, render));
        for (const auto &mode : {"success", "lighting", "losses"}) {
            BlenderJob job;
            job.start(sunInput, options(mode));
            wait(job);
            check(bool(job.result()) == (QString(mode) == "success"),
                  "Sun renders require exact frozen lighting and converted loss reports");
            if (job.result())
                check(job.result()->manifest["losses"].toObject()["solarLightingOmitted"] == 0 &&
                          job.result()->manifest["lighting"].toObject()["settings"] ==
                              sunInput->manifest()["solar"],
                      "Verified result retains explicit sun settings and applied-lighting report");
        }
        render.environment = environment;
        const auto environmentInput =
            PreparedRender::prepare(RenderSnapshot::capture(document, render));
        for (const auto &mode : {"success", "lighting", "losses"}) {
            BlenderJob job;
            job.start(environmentInput, options(mode));
            wait(job);
            check(bool(job.result()) == (QString(mode) == "success"),
                  "HDR renders require captured environment and converted loss report");
        }
        auto previewSettings = render;
        previewSettings.settings.engine = RenderEngine::Eevee;
        previewSettings.settings.seed = 7;
        const auto previewInput =
            PreparedRender::prepare(RenderSnapshot::capture(document, previewSettings));
        for (const auto &mode : {"success", "lighting", "losses", "graphics", "fail"}) {
            auto selected = options(mode);
            selected.backend = "OPENGL";
            selected.deviceId = fakeRasterDevice()["id"].toString();
            BlenderJob job;
            job.start(previewInput, selected);
            wait(job);
            check(bool(job.result()) == (QString(mode) == "success") &&
                      job.report()["attempts"].toArray().size() == 1,
                  "Eevee verifies renderer and conversion without implicit CPU fallback");
        }
        bool mismatchRejected{};
        try {
            BlenderJob mismatch;
            mismatch.start(previewInput, options("success"));
        } catch (const std::exception &) {
            mismatchRejected = true;
        }
        check(mismatchRejected, "Engine/device mismatch rejects before launching worker");
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
