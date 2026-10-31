#include "blender_stand_in.hpp"
#include "core/assets.hpp"
#include "core/face_textures.hpp"
#include "core/materials.hpp"
#include "dem_export_fixture.hpp"
#include "integrations/blender_job.hpp"
#include "integrations/render_snapshot.hpp"
#include "io/assets.hpp"
#include "io/texture_image.hpp"
#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <mutex>
#include <optional>
#include <sys/ptrace.h>
#include <unistd.h>

#ifdef SKP_WORKER_NO_ADDRESS_LIMIT
// The sanitizer builds. This binary is also the stand-in Blender, which runs under the Blender
// policy. LeakSanitizer checks for leaks at exit by stopping the threads with ptrace, which that
// policy refuses (SIGSYS), and the stand-in's environment is scrubbed, so ASAN_OPTIONS cannot turn
// the check off there. The setting is built into the whole binary, so its leak check is off in
// sanitizer builds. Test code only (ADR 0183, R109 S5).
extern "C" const char *__lsan_default_options() { return "detect_leaks=0"; }
#endif

using namespace sketchy;
using Phase = BlenderJob::Phase;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
// The artifacts are read and written with the standard library. The stand-in Blender runs under the
// sandbox, whose policy is Blender's trace (ADR 0183, R109 S5), and Qt's file layer makes calls
// that trace does not contain (statx, for one).
QByteArray read(const QString &path) {
    std::ifstream file(QFile::encodeName(path).constData(), std::ios::binary);
    check(bool(file), "Read test artifact");
    const std::string bytes((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
    return QByteArray::fromStdString(bytes);
}
void write(const QString &path, const QByteArray &data) {
    std::ofstream file(QFile::encodeName(path).constData(), std::ios::binary | std::ios::trunc);
    if (!(file.write(data.constData(), data.size()) && file.flush()))
        throw std::runtime_error(("Write test artifact " + path).toStdString());
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
QByteArray bigEndian32(quint32 value) {
    QByteArray out(4, '\0');
    for (int i = 0; i < 4; ++i)
        out[i] = char((value >> (24 - 8 * i)) & 0xff);
    return out;
}
QByteArray mp4Box(const char *type, const QByteArray &payload) {
    return bigEndian32(quint32(8 + payload.size())) + QByteArray(type, 4) + payload;
}
// A synthetic MP4 with the layout the verifier reads: ftyp, moov with a sample entry and an stsz
// table of `frames` samples, then mdat. The entry is avc1 with avcC for H.264, or mp4v with esds
// for MPEG-4 Part 2. It is not playable; ffprobe checks the real encoder's output in the real
// Blender test.
// A solid RGBA PNG, written without QImage: its writer loads Qt's logging configuration, a file
// check the Blender policy does not allow (ADR 0183, R109 S5). The verifier decodes it with
// QImageReader.
QByteArray bytesOf(std::initializer_list<quint8> values) {
    QByteArray result;
    for (const quint8 value : values)
        result.append(char(value));
    return result;
}
QByteArray pngOfColour(int width, int height, quint8 red, quint8 green, quint8 blue, quint8 alpha) {
    const auto crc = [](const QByteArray &bytes) {
        quint32 value = 0xFFFFFFFFU;
        for (const char byte : bytes) {
            value ^= quint8(byte);
            for (int bit = 0; bit < 8; ++bit)
                value = (value >> 1) ^ (0xEDB88320U & (0U - (value & 1U)));
        }
        return value ^ 0xFFFFFFFFU;
    };
    const auto be32 = [](quint32 value) {
        return bytesOf(
            {quint8(value >> 24), quint8(value >> 16), quint8(value >> 8), quint8(value)});
    };
    const auto chunk = [&](const char *type, const QByteArray &data) {
        const QByteArray body = QByteArray(type, 4) + data;
        return be32(quint32(data.size())) + body + be32(crc(body));
    };
    QByteArray header = be32(quint32(width)) + be32(quint32(height)) + bytesOf({8, 6, 0, 0, 0});
    QByteArray scanlines;
    const QByteArray pixel = bytesOf({red, green, blue, alpha});
    for (int row = 0; row < height; ++row) {
        scanlines.append(char(0)); // filter type None
        for (int column = 0; column < width; ++column)
            scanlines.append(pixel);
    }
    // qCompress prefixes the uncompressed length; a PNG IDAT is the bare zlib stream.
    const QByteArray zlib = qCompress(scanlines, 9).mid(4);
    return bytesOf({0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'}) + chunk("IHDR", header) +
           chunk("IDAT", zlib) + chunk("IEND", QByteArray());
}
QByteArray syntheticMp4(int frames, bool mpeg4) {
    const auto entry =
        mpeg4 ? mp4Box("mp4v", QByteArray(78, '\0') + mp4Box("esds", QByteArray(7, '\0')))
              : mp4Box("avc1", QByteArray(78, '\0') + mp4Box("avcC", QByteArray(7, '\0')));
    const auto stsd = QByteArray(4, '\0') + bigEndian32(1) + entry;
    const auto stsz = QByteArray(4, '\0') + bigEndian32(0) + bigEndian32(quint32(frames));
    const auto stbl = mp4Box("stbl", mp4Box("stsd", stsd) + mp4Box("stsz", stsz));
    const auto moov = mp4Box("moov", mp4Box("trak", mp4Box("mdia", mp4Box("minf", stbl))));
    return mp4Box("ftyp", QByteArray("isom") + QByteArray(4, '\0') + QByteArray("isom")) + moov +
           mp4Box("mdat", QByteArray(16, 'x'));
}
// The codecs the fake Blender's FFmpeg build has. SKETCHYUP_FAKE_CODECS=no-h264 or no-mpeg4 drops
// one of them; the probe and the encode both read it, as the worker does.
QStringList fakeCodecs() {
    const auto build = qEnvironmentVariable("SKETCHYUP_FAKE_CODECS");
    if (build == "no-h264")
        return {"MPEG4"};
    if (build == "no-mpeg4")
        return {"H264"};
    return {"H264", "MPEG4"};
}

// The encode fake re-hashes each frame as the worker does. A codec the build lacks is refused as
// codec_unavailable, and SKETCHYUP_FAKE_ENCODE=codec_unavailable refuses it at run time even though
// the probe listed it. SKETCHYUP_FAKE_ENTRY (mpeg4 or h264) writes that sample entry whatever codec
// the report names, so a test can check the verifier. SKETCHYUP_FAKE_DAMAGE names a file that one
// byte is appended to before the frames are read, so a test can damage a frame while the batch
// runs.
int fakeEncode(const QString &root, const QJsonObject &request) {
    const auto damage = qEnvironmentVariable("SKETCHYUP_FAKE_DAMAGE");
    if (!damage.isEmpty()) {
        std::ofstream file(QFile::encodeName(damage).constData(), std::ios::binary | std::ios::app);
        check(bool(file.write("x", 1)) && file.flush(), "Damage fixture");
    }
    const auto video = request.value("video").toObject();
    const auto frames = video.value("frames").toArray();
    // Like the worker, the fake requires the codec in the request; a missing one is not H.264.
    const auto codec = video.value("codec").toString();
    QJsonObject encoded{{"apiVersion", 1},
                        {"adapter", "sketchyup-blender-v1"},
                        {"blenderVersion", QJsonArray{5, 2, 2}}};
    if (codec != "H264" && codec != "MPEG4") {
        encoded["status"] = "failed";
        encoded["code"] = "invalid_request";
        encoded["message"] = "Unsupported video codec";
    } else if (!fakeCodecs().contains(codec) ||
               qEnvironmentVariable("SKETCHYUP_FAKE_ENCODE") == "codec_unavailable") {
        encoded["status"] = "failed";
        encoded["code"] = "codec_unavailable";
        encoded["message"] = codec == "MPEG4"
                                 ? "This Blender's FFmpeg build has no MPEG-4 Part 2 encoder"
                                 : "This Blender's FFmpeg build has no H.264 encoder";
    } else {
        bool intact = true;
        for (const auto &value : frames) {
            const auto frame = value.toObject();
            const auto bytes = read(frame.value("path").toString());
            intact = intact && hash(bytes) == frame.value("sha256").toString() &&
                     bytes.size() == frame.value("bytes").toInteger(-1);
        }
        if (!intact) {
            encoded["status"] = "failed";
            encoded["code"] = "invalid_frame";
            encoded["message"] = "A frame changed before encoding";
        } else {
            const auto entry = qEnvironmentVariable("SKETCHYUP_FAKE_ENTRY");
            const bool mpeg4 = entry.isEmpty() ? codec == "MPEG4" : entry == "mpeg4";
            const auto mp4 = syntheticMp4(int(frames.size()), mpeg4);
            write(root + "/video.mp4", mp4);
            encoded["status"] = "succeeded";
            encoded["video"] = QJsonObject{{"file", "video.mp4"},
                                           {"bytes", mp4.size()},
                                           {"sha256", hash(mp4)},
                                           {"codec", codec},
                                           {"container", "MP4"},
                                           {"frames", int(frames.size())},
                                           {"framesPerSecond", video.value("framesPerSecond")}};
        }
    }
    write(root + "/result.json", QJsonDocument(encoded).toJson());
    return 0;
}

// Blocks until the parent stops the process. The stand-in needs no event loop for that, and a
// QCoreApplication would load Qt's logging configuration, whose file check is a call the Blender
// policy does not allow (ADR 0183, R109 S5).
int blockUntilStopped() {
    std::mutex mutex;
    std::condition_variable never;
    std::unique_lock lock(mutex);
    never.wait(lock, [] { return false; });
    return 0;
}
// The camera the fake Blender reports as rendered: the camera the job gave it (the snapshot
// manifest's camera), in the form blender_worker.py's rendered_camera reports. Each value is what
// that camera's transform and lens give, as renderCameraTransform places it, so the echo is of the
// given camera and nothing else.
QJsonObject fakeRenderedCamera(const QJsonObject &source) {
    const auto given =
        parseRenderOptions(QJsonObject{{"apiVersion", 1}, {"camera", source.value("camera")}})
            .camera;
    QJsonArray matrix;
    for (const auto value : renderCameraTransform(*given).m)
        matrix.append(value);
    return {{"projection", given->orthographic ? "orthographic" : "perspective"},
            {"matrix", matrix},
            {"angleY", given->verticalFov},
            {"clipStart", given->nearClip},
            {"clipEnd", given->farClip}};
}

int fake(const QString &stated, const QStringList &args) {
    auto mode = stated;
    const auto requestPath = args.last();
    if (requestPath.endsWith(".blend") && args.contains("--disable-autoexec")) {
        const auto proof = qEnvironmentVariable("SKETCHYUP_HANDOFF_LAUNCH_PROOF");
        if (!proof.isEmpty()) {
            // The detached-process fixture uses visibility of this path as its
            // completion signal. Publish all bytes together so the parent cannot
            // observe an empty file between open and write.
            // Written beside the proof and renamed into place, so the path appears complete.
            const auto bytes = requestPath.toUtf8();
            write(proof + ".tmp", bytes);
            check(std::rename(QFile::encodeName(proof + ".tmp").constData(),
                              QFile::encodeName(proof).constData()) == 0,
                  "Publish complete external-launch proof");
        }
        return 0;
    }
    // counted-<hang>: the launch numbered <hang> hangs, the others succeed. The counter is the file
    // named in <stand-in>.counter, which the test wrote beside the stand-in
    // (tests/blender_stand_in.hpp).
    if (mode.startsWith("counted-")) {
        const int hang = mode.mid(8).toInt();
        const auto counter = QString::fromLocal8Bit(read(args.value(0) + ".counter")).trimmed();
        int launches = 0;
        if (std::ifstream probe(QFile::encodeName(counter).constData()); probe)
            launches = QByteArray::fromStdString(std::string(std::istreambuf_iterator<char>(probe),
                                                             std::istreambuf_iterator<char>()))
                           .trimmed()
                           .toInt();
        write(counter, QByteArray::number(++launches));
        mode = launches == hang ? "hang" : "success";
    }
    const auto root = QFileInfo(requestPath).absolutePath();
    const auto request = QJsonDocument::fromJson(read(requestPath)).object();
    // slow: every launch waits a few seconds, then succeeds.
    if (mode == "slow") {
        std::mutex mutex;
        std::condition_variable wake;
        std::unique_lock lock(mutex);
        wake.wait_for(lock, std::chrono::seconds(3), [] { return false; });
        mode = "success";
    }
    // hold-handoff: a handoff hangs until it is stopped; any other operation succeeds.
    if (mode == "hold-handoff")
        mode = request.value("operation") == "handoff" ? "hang" : "success";
    if (mode == "hang") {
        std::cout << "Retained worker log before interruption\n" << std::flush;
        return blockUntilStopped();
    }
    if (mode == "flood") {
        std::cout << std::string(3 * 1024 * 1024, 'x') << std::flush;
        return blockUntilStopped();
    }
    if (mode == "crash")
        return 7;
    // violate: an action the Blender policy does not name (ptrace), so the seccomp filter kills the
    // stand-in with SIGSYS, which the job reports as sandboxViolation. abort: an ordinary crash,
    // which stays a crash.
    if (mode == "violate") {
        ptrace(PTRACE_TRACEME, 0, nullptr, nullptr);
        return 0;
    }
    if (mode == "abort")
        std::abort();
    if (request.value("operation") == "encode")
        return fakeEncode(root, request);
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
        // The version the batch dialog names in its Blender line (UX_DESIGN 17.4).
        result["blenderVersionString"] = "5.2.2";
        result["backends"] = QJsonArray{"CPU"};
        result["devices"] =
            QJsonArray{QJsonObject{{"id", "CPU"}, {"name", "CPU"}, {"backend", "CPU"}}};
        // SKETCHYUP_FAKE_CODECS=absent omits the field; malformed and lowercase report a bad one.
        const auto build = qEnvironmentVariable("SKETCHYUP_FAKE_CODECS");
        if (build == "malformed") {
            result["videoCodecs"] = "H264";
        } else if (build == "lowercase") {
            result["videoCodecs"] = QJsonArray{"h264"};
        } else if (build != "absent") {
            QJsonArray codecs;
            for (const auto &name : fakeCodecs())
                codecs.append(name);
            result["videoCodecs"] = codecs;
        }
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
        QByteArray bytes = pngOfColour(settings.value("width").toInt(),
                                       settings.value("height").toInt(), 255, 0, 0, 255);
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
                                      {"width", settings.value("width")},
                                      {"height", settings.value("height")},
                                      {"bytes", bytes.size()},
                                      {"sha256", hash(bytes)}};
        // A still render reports the camera it renders with (the worker's renderedCamera).
        if (request.value("operation") != "handoff" && source.contains("camera"))
            result["renderedCamera"] = fakeRenderedCamera(source);
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
            description["width"] = settings.value("width").toInt() + 1;
        result["image"] = description;
        if (mode == "missing")
            QFile::remove(root + "/image.png");
        if (request.value("operation") == "handoff") {
            QByteArray scene =
                mode == "invalid-scene" ? "invalid scene" : "BLENDER17-01v0502fixture";
            write(root + "/scene.blend", scene);
            result.remove("image");
            result["sceneFile"] =
                QJsonObject{{"file", mode == "scene-path" ? "../scene.blend" : "scene.blend"},
                            {"bytes", scene.size()},
                            {"sha256", mode == "scene-hash" ? QString("wrong") : hash(scene)},
                            {"assetsPacked", mode != "unpacked-scene"},
                            {"oneWay", true}};
        }
        if (mode == "fractional-version")
            result["blenderVersion"] = QJsonArray{5, 2, .5};
    }
    // SKETCHYUP_FAKE_LOG=64k prints a log long enough to reach the attempt log's 64 KiB cap.
    if (qEnvironmentVariable("SKETCHYUP_FAKE_LOG") == "64k")
        std::cout << std::string(70000, 'x') << '\n';
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
// One prepared frame, rendered by the real worker and verified (R107.c2 reads its pixels).
std::shared_ptr<const BlenderResult> renderFrame(std::shared_ptr<const PreparedRender> input,
                                                 const BlenderJob::Options &real) {
    BlenderJob job;
    job.start(input, real);
    wait(job, 65000);
    check(job.phase() == Phase::Succeeded && job.result(), "Real frame render");
    return job.result();
}
// Text-bright pixels in a frame's top 24 rows, where the credit note is burned in. The note is
// light grey on a translucent box (lightness up to 204); the grey scene behind it is 138.
int brightTopRows(const QImage &image) {
    int count = 0;
    for (int y = 0; y < std::min(24, image.height()); ++y)
        for (int x = 0; x < image.width(); ++x) {
            const auto c = image.pixelColor(x, y);
            count += c.red() > 180 && c.green() > 180 && c.blue() > 180;
        }
    return count;
}

int main(int argc, char **argv) {
    QStringList raw;
    for (int i = 0; i < argc; ++i)
        raw.append(QString::fromLocal8Bit(argv[i]));
    // The stand-in: the job runs it under the sandbox as it runs Blender, so it is dispatched
    // before any Qt application exists. Its name carries the mode (tests/blender_stand_in.hpp).
    if (raw.size() > 2 && raw[1] == "--fake")
        return fake(raw[2], raw);
    if (const auto name = QFileInfo(raw.value(0)).fileName(); name.startsWith("sketchyup-fake-"))
        return fake(name.mid(15).section('-', 0, -2), raw);
    QCoreApplication app(argc, argv);
    try {
        const auto args = app.arguments();
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
                auto back = effectiveFaceTextureMapping(*document.bodies().at(body), face, true,
                                                        document.materials());
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
        auto options = [&](const QString &mode) {
            // A stand-in of its own, so a launcher still entering exec is never rewritten
            // (tests/blender_stand_in.hpp).
            BlenderJob::Options options;
            options.executable = blender_stand_in::make(files.path(), mode);
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
            // R107.c2 (ADR 0181 §3.3): a frame that shows DEM terrain has the notice burned in at
            // its top, by Blender's stamp. A scene without DEM terrain has none, and the same scene
            // renders the same bytes.
            {
                Document terrain;
                importDemFixture(terrain, std::string(SOURCE_DIR) + "/tests/fixtures/geotiff/",
                                 "plane-f32-strip-none-le.tif", DemProduct::Dep3, "NAVD88");
                RenderOptions demOptions;
                demOptions.settings = {512, 384, 4, 0};
                const auto demInput =
                    PreparedRender::prepare(RenderSnapshot::capture(terrain, demOptions));
                const auto demFrame = renderFrame(demInput, real);
                const auto demAgain = renderFrame(demInput, real);
                Document plainScene;
                plainScene.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 1, 0}, {0, 1, 0}}});
                const auto plainInput =
                    PreparedRender::prepare(RenderSnapshot::capture(plainScene, demOptions));
                const auto plainFrame = renderFrame(plainInput, real);
                const auto credits = demInput->manifest().value("credits").toArray();
                check(credits.size() == 1 &&
                          credits[0].toString() == QString::fromStdString(demCreditUsgs),
                      "The DEM frame's scene manifest lists the 3DEP notice");
                check(!plainInput->manifest().contains("credits"),
                      "A frame without DEM terrain lists no notice");
                check(demAgain->png == demFrame->png,
                      "The same DEM frame renders the same PNG bytes");
                check(brightTopRows(demFrame->image) >= 50 && brightTopRows(plainFrame->image) < 50,
                      "The notice is burned into the top of the DEM frame");
            }
            // R107.c3 (ADR 0181 §3.3; UX_DESIGN 17.13): the notice is burned in only where the
            // terrain is in the frame's view, and not at all when the batch's box is off. A frame
            // looking away has the same bytes whatever the box says, since neither stamps it.
            {
                Document terrain;
                importDemFixture(terrain, std::string(SOURCE_DIR) + "/tests/fixtures/geotiff/",
                                 "plane-f32-strip-none-le.tif", DemProduct::Dep3, "NAVD88");
                RenderOptions demOptions;
                demOptions.settings = {512, 384, 4, 0};
                const auto fitted = RenderSnapshot::capture(terrain, demOptions).camera();
                const Vec3 toEye = fitted.position - fitted.target;
                const double radius =
                    std::sqrt(dot(toEye, toEye)) * std::sin(fitted.verticalFov / 2) / 1.2;
                // The fit camera holds the terrain. This one stands ten radii off along +X and
                // looks further along +X, so the terrain is behind it.
                RenderOptions awayOptions = demOptions;
                RenderCamera away;
                away.position = fitted.target + Vec3{10 * radius, 0, 0};
                away.target = away.position + Vec3{14, 0, 0};
                awayOptions.camera = away;
                const QString notice = QString::fromStdString(demCreditUsgs);
                const auto inView = PreparedRender::prepare(
                    RenderSnapshot::capture(terrain, demOptions), CreditStamp::ViewNotices);
                const auto inViewOff = PreparedRender::prepare(
                    RenderSnapshot::capture(terrain, demOptions), CreditStamp::None);
                const auto looking = PreparedRender::prepare(
                    RenderSnapshot::capture(terrain, awayOptions), CreditStamp::ViewNotices);
                const auto lookingOff = PreparedRender::prepare(
                    RenderSnapshot::capture(terrain, awayOptions), CreditStamp::None);
                check(inView->manifest().value("stampCredits") == QJsonArray{notice} &&
                          inViewOff->manifest().value("viewCredits") == QJsonArray{notice} &&
                          inViewOff->manifest().value("stampCredits").toArray().isEmpty(),
                      "A frame in view is stamped only while the box is on");
                check(!looking->manifest().contains("viewCredits") &&
                          looking->manifest().value("stampCredits").toArray().isEmpty() &&
                          looking->manifest().value("credits") == QJsonArray{notice},
                      "A frame looking away lists and stamps no notice, though its scene shows the "
                      "terrain");
                const auto inViewFrame = renderFrame(inView, real);
                const auto inViewOffFrame = renderFrame(inViewOff, real);
                const auto lookingFrame = renderFrame(looking, real);
                const auto lookingOffFrame = renderFrame(lookingOff, real);
                check(brightTopRows(inViewFrame->image) >= 50 &&
                          brightTopRows(inViewOffFrame->image) < 50,
                      "The notice is burned into the frame in view, and not when the box is off");
                check(inViewFrame->png != inViewOffFrame->png,
                      "The box changes the bytes of a frame in view");
                check(brightTopRows(lookingFrame->image) < 50 &&
                          lookingFrame->png == lookingOffFrame->png,
                      "A frame looking away has no notice and the same bytes whether the box is on "
                      "or off");
                // A manifest written before R107.c3 names only the scene's credits. The worker
                // burns them, as 1.5.0 did, so a retained render from 1.5.0 keeps its note when it
                // is rendered again.
                const auto legacyDirectory = files.filePath("legacy-render");
                writeGlbExport(exportGlb(RenderSnapshot::capture(terrain, demOptions)),
                               legacyDirectory);
                const auto legacyBytes = read(legacyDirectory + "/manifest.json");
                const auto legacy = PreparedRender::open(
                    legacyDirectory,
                    QString::fromLatin1(
                        QCryptographicHash::hash(legacyBytes, QCryptographicHash::Sha256).toHex()));
                check(!legacy->manifest().contains("stampCredits") &&
                          legacy->manifest().value("credits") == QJsonArray{notice},
                      "A manifest from before R107.c3 names only the scene's credits");
                const auto legacyFrame = renderFrame(legacy, real);
                check(brightTopRows(legacyFrame->image) >= 50,
                      "A manifest from before R107.c3 burns its scene's notice, as 1.5.0 did");
                std::cout << "R107.c3 text-bright pixels in the top 24 rows: in view "
                          << brightTopRows(inViewFrame->image) << ", in view with the box off "
                          << brightTopRows(inViewOffFrame->image) << ", looking away "
                          << brightTopRows(lookingFrame->image)
                          << ", a manifest from before R107.c3 "
                          << brightTopRows(legacyFrame->image) << '\n';
            }
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

        // The probe's codec field: a present list is read, an absent field is reported as absent,
        // and a malformed one fails the probe. A refused codec list never reads as an empty one.
        {
            qputenv("SKETCHYUP_FAKE_CODECS", "no-h264");
            BlenderJob listed;
            listed.probe(options("success"));
            wait(listed);
            qunsetenv("SKETCHYUP_FAKE_CODECS");
            check(listed.phase() == Phase::Succeeded && listed.videoCodecs() &&
                      *listed.videoCodecs() == QStringList{"MPEG4"},
                  "A probe reports the codec list of its Blender, here without H.264");
            qputenv("SKETCHYUP_FAKE_CODECS", "absent");
            BlenderJob absent;
            absent.probe(options("success"));
            wait(absent);
            qunsetenv("SKETCHYUP_FAKE_CODECS");
            check(absent.phase() == Phase::Succeeded && !absent.videoCodecs(),
                  "A probe without a codec field reports no codec list");
            for (const char *build : {"malformed", "lowercase"}) {
                qputenv("SKETCHYUP_FAKE_CODECS", build);
                BlenderJob malformed;
                malformed.probe(options("success"));
                wait(malformed);
                qunsetenv("SKETCHYUP_FAKE_CODECS");
                check(malformed.phase() == Phase::Failed && !malformed.videoCodecs() &&
                          malformed.report().value("code") == "INVALID_RESULT",
                      "A malformed codec field fails the probe");
            }
        }

        // The encode names its codec and verifies the sample entry that codec writes: avc1 with
        // avcC for H.264, mp4v with esds for MPEG-4 Part 2. Nothing switches codecs silently.
        QByteArray frameBytes;
        {
            QImage image(64, 64, QImage::Format_RGBA8888);
            image.fill(Qt::blue);
            QBuffer buffer(&frameBytes);
            check(buffer.open(QIODevice::WriteOnly) && image.save(&buffer, "PNG"),
                  "Encode frame fixture");
        }
        const auto framePath = files.path() + "/encode-frame.png";
        write(framePath, frameBytes);
        const auto encodeRequest = [&](const char *codec) {
            BlenderVideoRequest request;
            request.width = 64;
            request.height = 64;
            request.framesPerSecond = 24;
            request.codec = codec;
            request.frames.push_back({framePath, hash(frameBytes), qint64(frameBytes.size())});
            return request;
        };
        const auto encodeWith = [&](const char *codec, const char *build, const char *entry,
                                    BlenderJob &encoder) {
            if (build)
                qputenv("SKETCHYUP_FAKE_CODECS", build);
            if (entry)
                qputenv("SKETCHYUP_FAKE_ENTRY", entry);
            encoder.encode(encodeRequest(codec), options("success"));
            wait(encoder);
            qunsetenv("SKETCHYUP_FAKE_CODECS");
            qunsetenv("SKETCHYUP_FAKE_ENTRY");
        };
        {
            BlenderJob encoder;
            encodeWith("MPEG4", nullptr, nullptr, encoder);
            check(encoder.phase() == Phase::Succeeded && encoder.videoResult() &&
                      encoder.videoResult()->manifest.value("codec") == "mpeg4" &&
                      encoder.videoResult()->manifest.value("frames") == 1,
                  "An explicit MPEG-4 encode is verified and recorded as mpeg4");
            BlenderJob defaults;
            encodeWith("H264", nullptr, nullptr, defaults);
            check(defaults.phase() == Phase::Succeeded && defaults.videoResult() &&
                      defaults.videoResult()->manifest.value("codec") == "h264",
                  "An H.264 encode is verified and recorded as h264");
        }
        {
            BlenderJob missing;
            encodeWith("MPEG4", "no-mpeg4", nullptr, missing);
            check(missing.phase() == Phase::Failed && !missing.videoResult() &&
                      missing.report().value("code") == "codec_unavailable" &&
                      missing.report().value("message").toString().contains("MPEG-4 Part 2"),
                  "A build without MPEG-4 refuses the MPEG-4 encode and names no code in its text");
            BlenderJob noFallback;
            encodeWith("H264", "no-h264", nullptr, noFallback);
            check(noFallback.phase() == Phase::Failed && !noFallback.videoResult() &&
                      noFallback.report().value("code") == "codec_unavailable",
                  "A build without H.264 refuses the H.264 encode; it never switches to MPEG-4");
            BlenderJob wrongEntry;
            encodeWith("H264", nullptr, "mpeg4", wrongEntry);
            check(wrongEntry.phase() == Phase::Failed && !wrongEntry.videoResult() &&
                      wrongEntry.report().value("code") == "INVALID_RESULT",
                  "An H.264 claim over an MPEG-4 sample entry is refused by the verifier");
            BlenderJob wrongCodec;
            encodeWith("MPEG4", nullptr, "h264", wrongCodec);
            check(wrongCodec.phase() == Phase::Failed && !wrongCodec.videoResult(),
                  "An MPEG-4 claim over an avc1 sample entry is refused by the verifier");
            qputenv("SKETCHYUP_FAKE_ENCODE", "codec_unavailable");
            BlenderJob refused;
            encodeWith("H264", nullptr, nullptr, refused);
            qunsetenv("SKETCHYUP_FAKE_ENCODE");
            check(refused.phase() == Phase::Failed && !refused.videoResult() &&
                      refused.report().value("code") == "codec_unavailable",
                  "A codec refused at encode time fails the encode with its code");
            bool rejected = false;
            try {
                BlenderJob unknown;
                auto request = encodeRequest("H264");
                request.codec = "AV1";
                unknown.encode(request, options("success"));
            } catch (const std::exception &) {
                rejected = true;
            }
            check(rejected, "An unknown video codec is refused before any worker starts");
        }
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
