#include "core/assets.hpp"
#include "core/materials.hpp"
#include "integrations/blender_job.hpp"
#include "io/assets.hpp"
#include "io/texture_image.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QThread>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(),
          "Write handoff fixture");
}
QString quote(QString value) { return "'" + value.replace("'", "'\\''") + "'"; }
void wait(BlenderJob &job) {
    QElapsedTimer timer;
    timer.start();
    while (!job.done() && timer.elapsed() < 65000) {
        QCoreApplication::processEvents();
        QThread::msleep(2);
    }
    check(job.done(), "Handoff completes within bound");
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir files;
        Document document;
        const auto body = document.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}});
        document.extrude(body, document.bodies().at(body)->surface.faces.begin()->first, 4);
        const auto texture = createAsset(
            document, "Packed checker", "image/png",
            assetPayload(encodeTexturePng(TextureImage(
                2, 2, {255, 0, 0, 255, 0, 255, 0, 128, 0, 0, 255, 0, 255, 255, 255, 255}))));
        const auto material =
            createMaterial(document, "Textured side", {.8f, .2f, .1f}, .7f, texture);
        assignMaterial(document, body, {}, material, true, true);
        auto sun = document.solar();
        sun.enabled = true;
        sun.latitude = 40;
        sun.longitude = -105;
        sun.time = {2010, 6, 21, 8, 0, 0, -420};
        document.setSolar(sun);
        RenderOptions render;
        render.settings = {128, 96, 4, 0};
        const auto hdr = files.filePath("source.hdr");
        QByteArray pixels = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 2\n";
        for (int i = 0; i < 2; ++i)
            pixels.append(char(128)).append(char(64)).append(char(32)).append(char(129));
        write(hdr, pixels);
        render.environment = readRenderEnvironment(hdr, .5, 90);
        auto input = PreparedRender::prepare(RenderSnapshot::capture(document, render));
        check(QFile::remove(hdr), "Original HDR removed after capture");
        const auto capturedRevision = document.revision();
        document.addFace({{{10, 0, 0}, {11, 0, 0}, {11, 1, 0}, {10, 1, 0}}});
        const auto liveRevision = document.revision();
        int sequence{};
        auto options = [&](const QString &mode) {
            BlenderJob::Options options;
            options.timeoutMs = 60000;
            options.executable = files.filePath(mode + QString::number(++sequence));
            write(options.executable,
                  ("#!/bin/sh\nexec " + quote(app.applicationDirPath() + "/blender_job_tests") +
                   " --fake " + quote(mode) + " \"$@\"\n")
                      .toUtf8());
            check(QFile::setPermissions(options.executable,
                                        QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner),
                  "Executable handoff fixture");
            return options;
        };
        if (app.arguments().contains("--real")) {
            const auto executable = qEnvironmentVariable("SKETCHYUP_BLENDER_TEST");
            if (executable.isEmpty())
                return 77;
            BlenderJob::Options real;
            real.executable = executable;
            real.timeoutMs = 60000;
            BlenderJob job;
            job.handoff(input, real);
            wait(job);
            if (!job.sceneResult())
                std::cerr << QJsonDocument(job.report()).toJson().constData();
            check(job.sceneResult() && !job.result(),
                  "Actual Blender creates a verified scene without image rendering");
            const auto scene = job.sceneResult();
            check(scene->manifest["revision"] == QString::number(capturedRevision),
                  "Handoff retains captured revision");
            const auto source = input->directory();
            input.reset();
            check(!QDir(source).exists() || QDir(source).removeRecursively(),
                  "Remove original capture before reopening handoff");
            const auto relocated = files.filePath("relocated");
            check(QDir().mkdir(relocated), "Relocated scene directory");
            const auto blend = relocated + "/scene.blend";
            write(blend, scene->blend);
            const auto inspector = files.filePath("inspect.py");
            write(inspector, R"PY(import bpy,json
s=bpy.context.scene
assert s.camera and s.camera.data.type=='PERSP'
assert (s.render.resolution_x,s.render.resolution_y)==(128,96)
assert s.render.engine=='CYCLES' and s.cycles.samples==4
assert s.view_settings.view_transform=='Standard'
assert len([o for o in s.objects if o.type=='MESH'])>=1
assert len([o for o in s.objects if o.type=='LIGHT' and o.data.type=='SUN'])==1
images=[i for i in bpy.data.images if i.type=='IMAGE']
assert len(images)>=2 and all(i.packed_file for i in images)
assert all(not t.use_module for t in bpy.data.texts)
p=json.loads(bpy.data.texts['SketchyUp transfer.json'].as_string())
assert p['oneWay'] and p['lighting']['environment']['strength']==.5
assert p['losses']['solarLightingOmitted']==0 and p['losses']['environmentLightingOmitted']==0
assert s.render.filepath=='//render.png'
print('SKETCHYUP_HANDOFF_REOPEN '+json.dumps(dict(packedImages=len(images),camera=s.camera.data.type,engine=s.render.engine,revision=p['revision'],oneWay=p['oneWay'])))
)PY");
            QProcess inspect;
            inspect.setProcessChannelMode(QProcess::MergedChannels);
            inspect.start(executable, {"--background", "--factory-startup", "--disable-autoexec",
                                       blend, "--python-exit-code", "1", "--python", inspector});
            check(inspect.waitForStarted(5000) && inspect.waitForFinished(60000),
                  "Reopen relocated Blender scene");
            const auto log = inspect.readAll();
            if (inspect.exitCode() != 0)
                std::cerr << log.constData();
            check(inspect.exitStatus() == QProcess::NormalExit && inspect.exitCode() == 0 &&
                      log.contains("SKETCHYUP_HANDOFF_REOPEN"),
                  "Reopened scene contains packed textures/HDR, camera, sun and settings");
            if (const auto evidence = qEnvironmentVariable("SKETCHYUP_HANDOFF_EVIDENCE");
                !evidence.isEmpty()) {
                write(evidence + ".json", QJsonDocument(scene->manifest).toJson());
                write(evidence + ".log", log);
            }
        } else {
            for (const auto mode : {"success", "invalid-scene", "scene-hash", "scene-path",
                                    "unpacked-scene", "source", "device", "lighting", "losses"}) {
                BlenderJob job;
                job.handoff(input, options(mode));
                wait(job);
                check(bool(job.sceneResult()) == (QString(mode) == "success"),
                      "Only valid scene output publishes");
                check(!job.result(), "Scene handoff never publishes a rendered image");
                if (job.sceneResult()) {
                    const auto saved = files.filePath("atomic.blend");
                    saveBlenderScene(*job.sceneResult(), saved);
                    QFile actual(saved);
                    check(actual.open(QIODevice::ReadOnly) &&
                              actual.readAll() == job.sceneResult()->blend,
                          "Atomic scene save preserves verified bytes");
                    actual.close();
                    auto corrupt = *job.sceneResult();
                    corrupt.blend.append('x');
                    bool rejected{};
                    try {
                        saveBlenderScene(corrupt, saved);
                    } catch (const std::exception &) {
                        rejected = true;
                    }
                    check(rejected, "Changed scene integrity rejects before overwrite");
                    rejected = false;
                    try {
                        saveBlenderScene(*job.sceneResult(), files.filePath("model.sketchyup"));
                    } catch (const std::exception &) {
                        rejected = true;
                    }
                    check(rejected && !QFileInfo::exists(files.filePath("model.sketchyup")),
                          "Handoff cannot overwrite a native model destination");
                }
            }
            BlenderJob canceled;
            canceled.handoff(input, options("hang"));
            canceled.cancel();
            wait(canceled);
            check(canceled.phase() == BlenderJob::Phase::Canceled && !canceled.sceneResult(),
                  "Canceled handoff publishes no scene");
            auto fallback = options("fallback");
            fallback.backend = "METAL";
            fallback.deviceId = "unavailable";
            BlenderJob retry;
            retry.handoff(input, fallback);
            wait(retry);
            check(retry.sceneResult() && retry.sceneResult()->manifest["cpuFallbackUsed"] == true,
                  "Scene handoff records explicit CPU fallback");
        }
        check(document.revision() == liveRevision, "Handoff never changes live document");
        std::cout << "Bounded immutable scene handoff and packed-asset reopening passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
