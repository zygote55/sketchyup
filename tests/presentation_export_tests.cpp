#include "core/scenes.hpp"
#include "integrations/presentation_batch.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QThread>
#include <functional>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F action, const char *message) {
    try {
        action();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error(message);
}
QByteArray read(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Read batch artifact");
    return file.readAll();
}
void write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "Write fixture");
}
QString digest(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
QString quote(QString text) { return "'" + text.replace("'", "'\\''") + "'"; }
void waitUntil(const std::function<bool()> &condition, const char *message,
               int milliseconds = 120000) {
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < milliseconds) {
        QCoreApplication::processEvents();
        QThread::msleep(2);
    }
    check(condition(), message);
}
void wait(PresentationExport &batch) {
    waitUntil([&] { return batch.done(); }, "Batch finishes within its deadline");
}
// A fake Blender that counts its launches. The launch numbered `hang` never returns, so a test can
// stop a frame while its worker runs; zero never hangs.
QString launcher(const QTemporaryDir &files, const QString &name, const QString &binary,
                 const QString &counter, int hang) {
    const auto path = files.filePath(name);
    write(path, ("#!/bin/sh\nn=0\n[ -f " + quote(counter) + " ] && n=$(cat " + quote(counter) +
                 ")\nn=$((n+1))\necho $n > " + quote(counter) + "\nif [ \"$n\" = \"" +
                 QString::number(hang) + "\" ]; then exec " + quote(binary) +
                 " --fake hang \"$@\"; fi\nexec " + quote(binary) + " --fake success \"$@\"\n")
                    .toUtf8());
    check(QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                          QFileDevice::ExeOwner),
          "Executable fixture");
    return path;
}
int launches(const QString &counter) {
    return QFileInfo::exists(counter) ? read(counter).trimmed().toInt() : 0;
}
PresentationPlan plan(const std::vector<Id> &keyframes, CameraPathTiming timing = {24, 1, 1}) {
    PresentationPlan result;
    result.path = "Orbit";
    result.keyframes = keyframes;
    result.timing = timing;
    RenderSettings draft;
    draft.width = 64;
    draft.height = 64;
    draft.samples = 1;
    RenderSettings check;
    check.width = 80;
    check.height = 64;
    check.samples = 1;
    check.seed = 2;
    result.presets = {{"Draft", draft}, {"Check", check}};
    return result;
}
// Every manifest frame entry is checked against its image and its metadata.
void verifyManifest(const QString &output, int expectedFrames) {
    const auto manifest = QJsonDocument::fromJson(read(output + "/manifest.json")).object();
    int frames = 0;
    for (const auto &presetValue : manifest.value("presets").toArray()) {
        const auto preset = presetValue.toObject();
        const auto directory = output + "/" + preset.value("directory").toString();
        for (const auto &entryValue : preset.value("frames").toArray()) {
            const auto entry = entryValue.toObject();
            const auto name = entry.value("file").toString();
            const auto png = read(directory + "/frames/" + name);
            check(digest(png) == entry.value("sha256").toString(),
                  "Manifest hash matches frame bytes");
            const auto metadata =
                QJsonDocument::fromJson(
                    read(directory + "/frames/" + name.left(name.size() - 4) + ".json"))
                    .object();
            check(metadata.value("sha256") == entry.value("sha256") &&
                      metadata.value("bytes").toInteger() == png.size() &&
                      metadata.value("sourceManifestSha256").isString() &&
                      metadata.value("result").isObject(),
                  "Per-frame metadata agrees with the manifest");
            ++frames;
        }
    }
    check(frames == expectedFrames, "Manifest lists every verified frame");
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const bool real = app.arguments().contains("--real");
    if (real && qEnvironmentVariableIsEmpty("SKETCHYUP_BLENDER_TEST"))
        return 77;
    try {
        QTemporaryDir files;
        check(files.isValid(), "Batch fixture directory");
        const auto binary = app.applicationDirPath() + "/blender_job_tests";
        const PresentationJobStore::Limits limits{8, 16, 256LL * 1024 * 1024};
        const auto store = files.filePath("store");
        Document doc;
        doc.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
        SceneSnapshot view;
        view.camera = SceneCamera{};
        const auto first = createScene(doc, "A", view);
        view.camera->yaw = 90;
        const auto second = createScene(doc, "B", view);
        view.camera->yaw = 180;
        const auto third = createScene(doc, "C", view);
        const std::vector<Id> keyframes{first, second, third};
        BlenderJob::Options options;
        options.timeoutMs = 60000;
        auto blender = [&](const QString &counter, int hang = 0) {
            BlenderJob::Options launched = options;
            launched.executable = launcher(files, "blender-" + QFileInfo(counter).fileName(),
                                           binary, counter, hang);
            return launched;
        };

        // One capture: both presets render from the frozen document, and a live edit made while
        // the batch runs changes none of its frames.
        {
            PresentationJobStore jobs(store, limits);
            const auto capture = PresentationCapture::capture(doc, plan(keyframes));
            const auto baseline = PresentationCapture::capture(doc, plan(keyframes));
            check(capture.sha256() == digest(capture.encoded()) && capture.frames().size() == 6,
                  "Capture plans six frames for three keyframes at one hold and one transition");
            int keyframeFrames = 0;
            for (const auto &frame : capture.frames())
                keyframeFrames += frame.keyframe;
            check(keyframeFrames == 3, "Every keyframe pose is one flagged planned frame");
            PresentationExport batch(jobs);
            const auto output = files.filePath("complete");
            const auto id = batch.start(capture, output, blender(files.filePath("count-complete")));
            doc.addFace({{{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}});
            const auto edited = encodeDocument(doc);
            wait(batch);
            check(batch.state() == PresentationJobState::Completed && batch.completedFrames() == 12 &&
                      batch.totalFrames() == 12,
                  "Both presets complete every planned frame");
            check(jobs.jobs().at(id).state == PresentationJobState::Completed,
                  "The store records the completed batch");
            const auto manifest = QJsonDocument::fromJson(read(output + "/manifest.json")).object();
            check(manifest.value("state") == "completed" &&
                      manifest.value("captureSha256") == capture.sha256() &&
                      manifest.value("framesPerSecond") == 24 &&
                      manifest.value("framesPerPreset") == 6 &&
                      manifest.value("presets").toArray().size() == 2,
                  "The batch manifest pins the capture, timing and preset layout");
            verifyManifest(output, 12);
            check(QFileInfo(output + "/01-Draft/frames/frame-0001.png").isFile() &&
                      QFileInfo(output + "/02-Check/frames/frame-0006.png").isFile(),
                  "Each preset writes its own numbered frame folder");

            PresentationExport twin(jobs);
            const auto twinOutput = files.filePath("baseline");
            twin.start(baseline, twinOutput, blender(files.filePath("count-baseline")));
            wait(twin);
            check(twin.state() == PresentationJobState::Completed, "Baseline batch completes");
            for (const auto *preset : {"01-Draft", "02-Check"})
                for (int number = 1; number <= 6; ++number) {
                    const auto name = QString("/%1/frames/frame-%2.json")
                                          .arg(preset)
                                          .arg(number, 4, 10, QChar('0'));
                    const auto live = QJsonDocument::fromJson(read(output + name)).object();
                    const auto frozen = QJsonDocument::fromJson(read(twinOutput + name)).object();
                    check(live.value("sourceManifestSha256") == frozen.value("sourceManifestSha256") &&
                              live.value("camera") == frozen.value("camera") &&
                              live.value("scene") == frozen.value("scene") &&
                              live.value("keyframe") == frozen.value("keyframe"),
                          "A live edit during a batch leaves its frames unchanged");
                }
            check(encodeDocument(doc) == edited, "The engine leaves the live document unchanged");
        }

        // Cancel at a frame boundary: finished frames stay, nothing past the boundary is written.
        const auto canceled = files.filePath("canceled");
        QString canceledId;
        {
            PresentationJobStore jobs(store, limits);
            PresentationExport batch(jobs);
            canceledId = batch.start(PresentationCapture::capture(doc, plan(keyframes)), canceled,
                                     blender(files.filePath("count-canceled")));
            bool requested = false;
            QObject::connect(&batch, &PresentationExport::changed, &app, [&] {
                if (!requested && batch.completedFrames() == 2) {
                    requested = true;
                    batch.cancel();
                }
            });
            wait(batch);
            check(batch.state() == PresentationJobState::Canceled && batch.completedFrames() == 2,
                  "Cancel at a frame boundary keeps the two finished frames");
            check(!QFileInfo::exists(canceled + "/01-Draft/frames/frame-0003.png") &&
                      !QFileInfo::exists(canceled + "/01-Draft/frames/frame-0003.json") &&
                      QFileInfo::exists(canceled + "/01-Draft/frames/frame-0002.png"),
                  "A canceled batch writes no frame past the boundary");
            const auto manifest = QJsonDocument::fromJson(read(canceled + "/manifest.json")).object();
            check(manifest.value("state") == "canceled" && manifest.value("completedFrames") == 2,
                  "The canceled manifest records the frames kept");
            verifyManifest(canceled, 2);
            check(jobs.jobs().at(canceledId).state == PresentationJobState::Canceled,
                  "The store keeps the canceled batch with its state");
        }

        // Cancel while a frame runs: the in-flight frame is discarded and the finished one stays.
        {
            PresentationJobStore jobs(store, limits);
            PresentationExport batch(jobs);
            const auto midFrame = files.filePath("mid-frame");
            const auto counter = files.filePath("count-mid-frame");
            batch.start(PresentationCapture::capture(doc, plan(keyframes)), midFrame,
                        blender(counter, 2));
            waitUntil([&] { return launches(counter) == 2; }, "The second frame worker starts");
            batch.cancel();
            wait(batch);
            check(batch.state() == PresentationJobState::Canceled && batch.completedFrames() == 1,
                  "Cancel while a frame runs keeps only the finished frame");
            check(!QFileInfo::exists(midFrame + "/01-Draft/frames/frame-0002.png") &&
                      !QFileInfo::exists(midFrame + "/01-Draft/frames/frame-0002.json"),
                  "The in-flight frame writes no file");
            verifyManifest(midFrame, 1);
        }

        // Retry resumes from the first unverified frame and never re-renders a verified frame.
        {
            PresentationJobStore jobs(store, limits);
            PresentationExport batch(jobs);
            const auto counter = files.filePath("count-resume");
            const auto kept = read(canceled + "/01-Draft/frames/frame-0001.png");
            batch.retry(canceledId, blender(counter));
            wait(batch);
            check(batch.state() == PresentationJobState::Completed && batch.completedFrames() == 12,
                  "Retry completes the canceled batch");
            check(launches(counter) == 10,
                  "Retry renders only the ten frames that were not verified");
            check(read(canceled + "/01-Draft/frames/frame-0001.png") == kept,
                  "A verified frame is byte-identical after retry");
            verifyManifest(canceled, 12);
        }

        // A damaged verified frame is the only one that re-renders besides the unrendered frames.
        {
            PresentationJobStore jobs(store, limits);
            const auto corrupt = files.filePath("corrupt");
            PresentationExport batch(jobs);
            bool requested = false;
            QObject::connect(&batch, &PresentationExport::changed, &app, [&] {
                if (!requested && batch.completedFrames() == 4) {
                    requested = true;
                    batch.cancel();
                }
            });
            const auto id = batch.start(PresentationCapture::capture(doc, plan(keyframes)), corrupt,
                                        blender(files.filePath("count-corrupt")));
            wait(batch);
            check(batch.state() == PresentationJobState::Canceled && batch.completedFrames() == 4,
                  "Canceled after four frames");
            write(corrupt + "/01-Draft/frames/frame-0002.png", "damaged image");
            const auto counter = files.filePath("count-corrupt-retry");
            batch.retry(id, blender(counter));
            wait(batch);
            check(batch.state() == PresentationJobState::Completed && launches(counter) == 9,
                  "Only the damaged frame and the unrendered frames run on retry");
            verifyManifest(corrupt, 12);
        }

        // A stopped worker: the engine closes, the batch reopens interrupted and resumes.
        {
            const auto stopped = files.filePath("stopped");
            const auto counter = files.filePath("count-stopped");
            QString id;
            {
                PresentationJobStore jobs(store, limits);
                PresentationExport batch(jobs);
                id = batch.start(PresentationCapture::capture(doc, plan(keyframes)), stopped,
                                 blender(counter, 2));
                waitUntil([&] { return launches(counter) == 2; }, "The second frame worker starts");
                waitUntil([&] { return batch.completedFrames() == 1; }, "The first frame is kept");
            }
            PresentationJobStore reopened(store, limits);
            check(reopened.jobs().at(id).state == PresentationJobState::Interrupted,
                  "A stopped batch reopens as interrupted");
            PresentationExport batch(reopened);
            batch.retry(id, blender(files.filePath("count-stopped-retry")));
            wait(batch);
            check(batch.state() == PresentationJobState::Completed && batch.completedFrames() == 12,
                  "An interrupted batch resumes to completion");
            verifyManifest(stopped, 12);
        }

        // A damaged manifest refuses the retry and keeps every finished frame.
        {
            PresentationJobStore jobs(store, limits);
            PresentationExport batch(jobs);
            const auto damaged = files.filePath("damaged-manifest");
            bool requested = false;
            QObject::connect(&batch, &PresentationExport::changed, &app, [&] {
                if (!requested && batch.completedFrames() == 2) {
                    requested = true;
                    batch.cancel();
                }
            });
            const auto id = batch.start(PresentationCapture::capture(doc, plan(keyframes)), damaged,
                                        blender(files.filePath("count-damaged")));
            wait(batch);
            write(damaged + "/manifest.json", "{ broken");
            const auto kept = read(damaged + "/01-Draft/frames/frame-0001.png");
            rejects([&] { batch.retry(id, blender(files.filePath("count-damaged-retry"))); },
                    "A damaged manifest refuses retry");
            check(batch.state() == PresentationJobState::Canceled &&
                      read(damaged + "/01-Draft/frames/frame-0001.png") == kept,
                  "A refused retry leaves the finished frames as they were");
        }

        // Refusals: each one names its reason, and leaves no folder and no job behind.
        {
            PresentationJobStore jobs(store, limits);
            const auto count = jobs.jobs().size();
            PresentationExport batch(jobs);
            const auto refused = files.filePath("refused");
            Document plain;
            plain.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
            SceneSnapshot noCamera;
            noCamera.visibility = SceneVisibility{};
            const auto withoutCamera = createScene(plain, "Plain", noCamera);
            rejects([&] { PresentationCapture::capture(plain, plan({withoutCamera, withoutCamera})); },
                    "A keyframe without a camera is refused");
            rejects([&] { PresentationCapture::capture(doc, plan({first, 999999})); },
                    "A keyframe scene that is gone is refused");
            rejects([&] { PresentationCapture::capture(doc, plan({first})); },
                    "A path needs two keyframes");
            auto five = plan(keyframes);
            five.presets.push_back({"Extra", {}});
            five.presets.push_back({"Again", {}});
            five.presets.push_back({"Third", {}});
            rejects([&] { PresentationCapture::capture(doc, five); }, "Over four presets is refused");
            rejects([&] { PresentationCapture::capture(doc, plan(keyframes, {24, 24, 80})); },
                    "A plan over 240 frames is refused");

            const auto overBudget = plan({first, second}, {24, 25, 107}); // 1 + 2 x 107 + 25 frames.
            auto large = overBudget;
            for (auto &preset : large.presets)
                preset.settings = {4096, 4096, 32, 0, RenderEngine::Cycles};
            check(PresentationCapture::capture(doc, overBudget).frames().size() == 240,
                  "A two-keyframe path plans 240 frames");
            rejects([&] {
                batch.start(PresentationCapture::capture(doc, large), refused,
                            blender(files.filePath("count-budget")));
            }, "A batch over the output budget is refused");
            check(!QFileInfo::exists(refused) && jobs.jobs().size() == count,
                  "An output budget refusal leaves no folder and no job");

            batch.setFreeSpaceProbe([](const QString &) { return qint64(0); });
            rejects([&] {
                batch.start(PresentationCapture::capture(doc, plan(keyframes)), refused,
                            blender(files.filePath("count-disk")));
            }, "A batch without free disk space is refused");
            check(!QFileInfo::exists(refused) && jobs.jobs().size() == count,
                  "A disk refusal leaves no folder and no job");
            batch.setFreeSpaceProbe([](const QString &) { return qint64(1) << 50; });

            const auto existing = files.filePath("existing");
            check(QDir().mkpath(existing), "Existing folder fixture");
            write(existing + "/keep.txt", "keep");
            rejects([&] {
                batch.start(PresentationCapture::capture(doc, plan(keyframes)), existing,
                            blender(files.filePath("count-existing")));
            }, "An existing destination is refused");
            check(read(existing + "/keep.txt") == "keep" && jobs.jobs().size() == count,
                  "An existing destination is left as it was");

            const auto target = files.filePath("link-target");
            check(QDir().mkpath(target) && QFile::link(target, files.filePath("linked-output")),
                  "Symbolic link fixture");
            rejects([&] {
                batch.start(PresentationCapture::capture(doc, plan(keyframes)),
                            files.filePath("linked-output"), blender(files.filePath("count-link")));
            }, "A symbolic link destination is refused");
            rejects([&] {
                batch.start(PresentationCapture::capture(doc, plan(keyframes)),
                            files.filePath("missing/parent/out"),
                            blender(files.filePath("count-parent")));
            }, "A destination needs an existing parent");
            rejects([&] {
                batch.start(PresentationCapture::capture(doc, plan(keyframes)), "relative/out",
                            blender(files.filePath("count-relative")));
            }, "A destination must be absolute");

            BlenderJob::Options missing = options;
            missing.executable = files.filePath("no-such-blender");
            rejects([&] {
                batch.start(PresentationCapture::capture(doc, plan(keyframes)), refused, missing);
            }, "A missing Blender executable is refused");

            auto eevee = plan(keyframes);
            eevee.presets[0].settings.engine = RenderEngine::Eevee;
            rejects([&] {
                batch.start(PresentationCapture::capture(doc, eevee), refused,
                            blender(files.filePath("count-backend")));
            }, "Eevee presets need an OpenGL device");
            check(!QFileInfo::exists(refused) && jobs.jobs().size() == count,
                  "Each refusal leaves no folder and no job");
        }

        // One batch at a time: a second start while one runs is refused.
        {
            PresentationJobStore jobs(files.filePath("single"), limits);
            PresentationExport batch(jobs);
            const auto id = batch.start(PresentationCapture::capture(doc, plan(keyframes)),
                                        files.filePath("single-running"),
                                        blender(files.filePath("count-single"), 1));
            rejects([&] {
                batch.start(PresentationCapture::capture(doc, plan(keyframes)),
                            files.filePath("single-second"),
                            blender(files.filePath("count-single-2")));
            }, "A second batch cannot start while one runs");
            check(!QFileInfo::exists(files.filePath("single-second")),
                  "A refused second batch creates no folder");
            batch.cancel();
            wait(batch);
            check(jobs.jobs().at(id).state == PresentationJobState::Canceled,
                  "A canceled running batch ends canceled");
        }

        // Real Blender, when the explicit opt-in is set: one batch from one capture.
        if (real) {
            PresentationJobStore jobs(files.filePath("real-store"), limits);
            PresentationExport batch(jobs);
            BlenderJob::Options real = options;
            real.executable = qEnvironmentVariable("SKETCHYUP_BLENDER_TEST");
            real.timeoutMs = 180000;
            const auto before = encodeDocument(doc);
            batch.start(PresentationCapture::capture(doc, plan(keyframes)), files.filePath("real"),
                        real);
            wait(batch);
            check(batch.state() == PresentationJobState::Completed && batch.completedFrames() == 12,
                  "Real Blender completes every planned frame");
            verifyManifest(files.filePath("real"), 12);
            check(encodeDocument(doc) == before, "Real batch leaves the live document unchanged");
            std::cout << "Real Blender presentation batch verified\n";
            return 0;
        }
        std::cout << "Presentation batch capture, frame sequence, cancellation, resume, damage and "
                     "refusal checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
