#include "integrations/blender_job.hpp"
#include "integrations/blender_sandbox_policy.hpp"
#include "sandbox/sandbox.hpp"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <future>
#include <optional>
#ifdef Q_OS_LINUX
#include <csignal>
#include <sys/prctl.h>
#include <unistd.h>
#endif
static void initializeBlenderResource() { Q_INIT_RESOURCE(blender_worker); }
namespace sketchy {
namespace {
std::atomic<int> activeJobs{};
// The test seam of setBlenderTestWritableDirectories (empty in the application).
QStringList testWritableDirectories;
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
QString hash(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
QByteArray read(const QString &path, qsizetype limit) {
    const QFileInfo info(path);
    require(info.isFile() && !info.isSymLink() && info.size() <= limit,
            "Expected a bounded regular worker artifact");
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot read worker artifact");
    const auto bytes = file.read(limit + 1);
    require(bytes.size() <= limit && file.error() == QFileDevice::NoError,
            "Worker artifact read failed or exceeded bound");
    return bytes;
}
QJsonObject object(const QByteArray &bytes) {
    QJsonParseError error;
    const auto json = QJsonDocument::fromJson(bytes, &error);
    require(error.error == QJsonParseError::NoError && json.isObject(),
            "Worker result is not a JSON object");
    return json.object();
}
void write(const QString &path, const QByteArray &bytes) {
    QSaveFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit(),
            "Cannot prepare Blender worker input");
}
bool terminal(BlenderJob::Phase phase) {
    return phase == BlenderJob::Phase::Succeeded || phase == BlenderJob::Phase::Failed ||
           phase == BlenderJob::Phase::Unavailable || phase == BlenderJob::Phase::Canceled ||
           phase == BlenderJob::Phase::TimedOut;
}
bool backend(const QString &name) {
    return QStringList{"CPU", "CUDA", "OPTIX", "HIP", "ONEAPI", "METAL", "OPENGL"}.contains(name);
}
// ADR 0183 sections 4.6 and 7. The reason names the layer ("sandboxUnavailable: landlock (...)").
QString sandboxRefusalMessage(const QString &reason) {
    return "Blender was not started: " + reason +
           ". Check that the kernel offers Landlock and seccomp (Linux 5.13 or later).";
}
// A child killed by its seccomp filter (SIGSYS) is a violation, not a crash (ADR 0183 section 4.6).
QString sandboxViolationMessage() {
    return "Blender was stopped by its sandbox, because it tried an action its policy does not "
           "allow. Nothing was rendered.";
}
void verifyRasterDevice(const QJsonObject &device) {
    const auto graphics = device.value("graphics").toObject();
    require(graphics.size() == 5 && graphics.value("backend_type") == "OPENGL",
            "Malformed OpenGL renderer identity");
    for (const auto key : {"backend_type", "device_type", "renderer", "vendor", "version"})
        require(graphics.value(key).isString() && !graphics.value(key).toString().isEmpty() &&
                    graphics.value(key).toString().size() <= 512,
                "Malformed OpenGL renderer description");
    require(device.value("name") == graphics.value("renderer") &&
                device.value("id") ==
                    "opengl:" + hash(QJsonDocument(graphics).toJson(QJsonDocument::Compact)),
            "OpenGL renderer fingerprint mismatch");
}
void version(const QJsonObject &result) {
    require(result.value("apiVersion") == 1 && result.value("adapter") == "sketchyup-blender-v1",
            "Worker protocol mismatch");
    const auto v = result.value("blenderVersion").toArray();
    require(v.size() == 3 && v[0] == 5 && v[1] == 2 && v[2].isDouble() &&
                v[2].toDouble() == v[2].toInt(-1) && v[2].toInt(-1) >= 0,
            "Unsupported Blender version in result");
}
QJsonObject lightingReport(const QJsonObject &source) {
    const auto solar = source.value("solar").toObject();
    const auto environment = source.value("environment").toObject();
    const bool hdri = !environment.isEmpty(), sun = solar.value("enabled").toBool();
    QJsonObject report{
        {"mode", sun ? (hdri ? "solar-hdri-v1" : "solar-v1") : (hdri ? "hdri-v1" : "studio-v1")},
        {"worldStrength", hdri ? environment.value("strength").toDouble() : .25}};
    if (hdri)
        report["environment"] = environment;
    if (sun) {
        const auto position = source.value("solarPosition").toObject();
        report["energy"] = position.value("directLightActive").toBool() ? 3. : 0.;
        report["angle"] = .00935;
        report["shadows"] = position.value("shadowsActive");
        report["settings"] = solar;
        report["position"] = position;
    }
    return report;
}
void verifyMetadata(const PreparedRender &input, const QJsonObject &result,
                    const QString &requestedBackend, const QString &requestedDevice) {
    version(result);
    const auto &source = input.manifest();
    require(result.value("status") == "succeeded" &&
                result.value("documentId") == source.value("documentId") &&
                result.value("revision") == source.value("revision") &&
                result.value("settings") == source.value("settings") &&
                result.value("manifestSha256") == input.manifestHash() &&
                result.value("sceneSha256") == source.value("scene").toObject().value("sha256"),
            "Worker result does not match captured source");
    const auto device = result.value("device").toObject();
    require(device.value("backend") == requestedBackend && device.value("id") == requestedDevice,
            "Worker did not use the explicitly requested device");
    if (requestedBackend == "OPENGL")
        verifyRasterDevice(device);
    const auto preset = result.value("preset").toObject();
    const auto expectedLighting = lightingReport(source);
    const auto settings = source.value("settings").toObject();
    const bool eevee = settings.value("engine") == "eevee";
    require(preset.value("name") == expectedLighting.value("mode") &&
                preset.value("engine") == (eevee ? "BLENDER_EEVEE" : "CYCLES") &&
                preset.value("threads") == 4 &&
                preset.value("samplingPolicy") ==
                    (eevee ? "eevee-preview-v1" : "cycles-fixed-v1") &&
                preset.value("samples") == settings.value("samples"),
            "Worker render preset mismatch");
    auto losses = source.value("losses").toObject();
    if (eevee) {
        losses["indirectLightingApproximated"] = 1;
        if (settings.value("seed").toInt())
            losses["samplingSeedNotApplied"] = 1;
    }
    if (source.value("solar").toObject().value("enabled").toBool())
        losses["solarLightingOmitted"] = 0;
    if (source.contains("environment"))
        losses["environmentLightingOmitted"] = 0;
    require(result.value("lighting") == expectedLighting && result.value("losses") == losses,
            "Worker lighting or transfer report does not match the captured source");
}
std::shared_ptr<const BlenderSceneResult>
verifyScene(const PreparedRender &input, const QString &directory, const QJsonObject &result,
            const QString &requestedBackend, const QString &requestedDevice) {
    verifyMetadata(input, result, requestedBackend, requestedDevice);
    const auto file = result.value("sceneFile").toObject();
    require(file.value("file") == "scene.blend" && file.value("assetsPacked") == true &&
                file.value("oneWay") == true,
            "Worker scene transfer descriptor mismatch");
    const auto bytes = read(directory + "/scene.blend", 512 * 1024 * 1024);
    require(bytes.size() > 17 && bytes.startsWith("BLENDER17-01v0502") &&
                file.value("bytes").toInteger(-1) == bytes.size() &&
                file.value("sha256") == hash(bytes),
            "Worker scene header, size or hash mismatch");
    auto verified = std::make_shared<BlenderSceneResult>();
    verified->manifest = result;
    verified->blend = bytes;
    return verified;
}
std::shared_ptr<const BlenderResult> verify(const PreparedRender &input, const QString &directory,
                                            const QJsonObject &result,
                                            const QString &requestedBackend,
                                            const QString &requestedDevice) {
    verifyMetadata(input, result, requestedBackend, requestedDevice);
    const auto settings = input.manifest().value("settings").toObject();
    const auto image = result.value("image").toObject();
    require(image.value("file") == "image.png" && image.value("width") == settings.value("width") &&
                image.value("height") == settings.value("height"),
            "Worker image dimensions or filename mismatch");
    const auto bytes = read(directory + "/image.png", 64 * 1024 * 1024);
    require(image.value("bytes").toInteger(-1) == bytes.size() &&
                image.value("sha256") == hash(bytes),
            "Worker PNG hash or size mismatch");
    QBuffer buffer;
    buffer.setData(bytes);
    require(buffer.open(QIODevice::ReadOnly), "Cannot decode verified image bytes");
    QImageReader reader(&buffer);
    reader.setDecideFormatFromContent(true);
    const QSize expected(settings.value("width").toInt(), settings.value("height").toInt());
    require(reader.format() == "png" && reader.size() == expected,
            "Worker output is not the expected PNG");
    const auto decoded = reader.read();
    require(!decoded.isNull() && decoded.size() == expected, "Worker PNG failed full decoding");
    auto verified = std::make_shared<BlenderResult>();
    verified->manifest = result;
    verified->png = bytes;
    verified->image = decoded;
    return verified;
}
} // namespace
std::shared_ptr<const PreparedRender> PreparedRender::prepare(const RenderSnapshot &snapshot) {
    auto result = std::shared_ptr<PreparedRender>(new PreparedRender);
    result->root_ = std::make_shared<QTemporaryDir>(QDir::tempPath() + "/sketchyup-render-XXXXXX");
    require(result->root_->isValid(), "Cannot create private render directory");
    const auto scene = exportGlb(snapshot);
    writeGlbExport(scene, result->sourceDirectory());
    result->manifest_ = scene.manifest;
    result->manifestHash_ =
        hash(read(result->sourceDirectory() + "/manifest.json", 16 * 1024 * 1024));
    return result;
}
void PreparedRender::copyTo(const QString &directory) const {
    const auto bytes = read(sourceDirectory() + "/manifest.json", 16 * 1024 * 1024);
    require(hash(bytes) == manifestHash_, "Captured render manifest changed");
    GlbExport package{read(sourceDirectory() + "/scene.glb", 256 * 1024 * 1024), object(bytes), {}};
    if (package.manifest.contains("environment"))
        package.environment = read(sourceDirectory() + "/environment.hdr", 40 * 1024 * 1024);
    writeGlbExport(package, directory);
}
std::shared_ptr<const PreparedRender> PreparedRender::open(const QString &directory,
                                                           const QString &manifestHash) {
    require(!QFileInfo(directory).isSymLink() && QFileInfo(directory).isDir(),
            "Invalid retained render directory");
    const auto bytes = read(directory + "/manifest.json", 16 * 1024 * 1024);
    require(hash(bytes) == manifestHash, "Retained render manifest changed");
    auto result = std::shared_ptr<PreparedRender>(new PreparedRender);
    result->root_ = std::make_shared<QTemporaryDir>(QDir::tempPath() + "/sketchyup-render-XXXXXX");
    require(result->root_->isValid(), "Cannot reopen retained render snapshot");
    result->manifest_ = object(bytes);
    require(result->manifest_.value("apiVersion") == 1 &&
                result->manifest_.value("adapter") == "sketchyup-glb-v1",
            "Unsupported retained render snapshot");
    auto settings = result->manifest_.value("settings").toObject();
    settings["apiVersion"] = 1;
    parseRenderOptions(settings);
    GlbExport package{read(directory + "/scene.glb", 256 * 1024 * 1024), result->manifest_, {}};
    if (package.manifest.contains("environment"))
        package.environment = read(directory + "/environment.hdr", 40 * 1024 * 1024);
    writeGlbExport(package, result->sourceDirectory());
    result->manifestHash_ =
        hash(read(result->sourceDirectory() + "/manifest.json", 16 * 1024 * 1024));
    require(result->manifestHash_ == manifestHash, "Retained manifest is not canonical");
    return result;
}
std::shared_ptr<const BlenderResult> loadBlenderResult(const PreparedRender &input,
                                                       const QString &directory,
                                                       const QJsonObject &manifest,
                                                       const BlenderJob::Options &requested) {
    const auto device = manifest.value("device").toObject();
    const auto actualBackend = device.value("backend").toString();
    const auto actualDevice = device.value("id").toString();
    const bool fallback =
        requested.backend != "CPU" && requested.backend != "OPENGL" && actualBackend == "CPU";
    require((actualBackend == requested.backend &&
             actualDevice == (requested.backend == "CPU" ? "CPU" : requested.deviceId)) ||
                (fallback && requested.allowCpuFallback && actualDevice == "CPU"),
            "Retained render did not use the requested device or permitted CPU fallback");
    require(manifest.value("cpuFallbackUsed").isBool() &&
                manifest.value("cpuFallbackUsed").toBool() == fallback,
            "Retained CPU fallback provenance mismatch");
    return verify(input, directory, manifest, actualBackend, actualDevice);
}
namespace {
constexpr qsizetype videoLimit = 512 * 1024 * 1024;

quint64 bigEndian(const QByteArray &bytes, qsizetype at, int width) {
    quint64 value = 0;
    for (int i = 0; i < width; ++i)
        value = (value << 8) | quint8(bytes[at + i]);
    return value;
}

// What the verifier reads from an MP4: the first sample entry of the video track (its type and its
// bytes) and its sample count from the stsz table. Only top-level boxes and the movie box are
// walked, so media bytes never match.
struct Mp4Summary {
    QByteArray entryType, entry;
    qint64 frames{-1};
};

Mp4Summary summarizeMp4(const QByteArray &bytes) {
    bool ftyp = false;
    QByteArray movie;
    qsizetype offset = 0;
    while (offset + 8 <= bytes.size()) {
        auto size = bigEndian(bytes, offset, 4);
        const auto type = bytes.mid(offset + 4, 4);
        qsizetype header = 8;
        if (size == 1) {
            require(offset + 16 <= bytes.size(), "Malformed MP4 box");
            size = bigEndian(bytes, offset + 8, 8);
            header = 16;
        } else if (size == 0) {
            size = quint64(bytes.size() - offset);
        }
        require(size >= quint64(header) && size <= quint64(bytes.size() - offset),
                "Malformed MP4 box");
        if (type == "ftyp")
            ftyp = true;
        if (type == "moov")
            movie = bytes.mid(offset + header, qsizetype(size) - header);
        offset += qsizetype(size);
    }
    require(ftyp && !movie.isEmpty(), "Video is not an MP4 file");
    const auto table = movie.indexOf("stsz");
    require(table >= 4 && table + 16 <= movie.size(), "Video has no sample table");
    // stsd holds the type, then the version and flags, the entry count and the first sample entry,
    // which starts with its own size and type.
    const auto description = movie.indexOf("stsd");
    require(description >= 4 && description + 20 <= movie.size(),
            "Video has no sample description");
    const auto first = description + 12;
    const auto size = qsizetype(bigEndian(movie, first, 4));
    require(size >= 8 && first + size <= movie.size(), "Video sample description is malformed");
    return {movie.mid(first + 4, 4), movie.mid(first, size),
            qint64(bigEndian(movie, table + 12, 4))};
}

// Verifies the worker's video against the frames it was asked to encode: the file hash and size,
// the codec, the container and the frame count. Only a verified video is published.
std::shared_ptr<const BlenderVideoResult> verifyVideo(const QString &directory,
                                                      const QJsonObject &report,
                                                      const BlenderVideoRequest &request) {
    require(report.value("status") == "succeeded", "Worker did not report a video");
    const auto video = report.value("video").toObject();
    const auto mp4 = read(directory + "/video.mp4", videoLimit);
    const auto digest = hash(mp4);
    require(video.value("file") == "video.mp4" &&
                video.value("bytes").toInteger(-1) == mp4.size() &&
                video.value("sha256") == digest && video.value("codec") == request.codec &&
                video.value("container") == "MP4" &&
                video.value("frames").toInteger(-1) == qint64(request.frames.size()) &&
                video.value("framesPerSecond").toInteger(-1) == request.framesPerSecond,
            "Video does not match the worker report");
    // The sample entry names the codec: avc1 with an avcC configuration for H.264, and mp4v with an
    // esds configuration for MPEG-4 Part 2, which never carries avc1 or avcC.
    const auto summary = summarizeMp4(mp4);
    const bool h264 = request.codec == "H264";
    const bool entry = h264 ? summary.entryType == "avc1" && summary.entry.contains("avcC")
                            : summary.entryType == "mp4v" && summary.entry.contains("esds") &&
                                  !summary.entry.contains("avcC");
    require(entry && summary.frames == qint64(request.frames.size()),
            "Video is not an MP4 of the chosen codec with every verified frame");
    QJsonArray inputs;
    for (const auto &frame : request.frames)
        inputs.append(frame.sha256);
    QStringList version;
    for (const auto part : report.value("blenderVersion").toArray())
        version.append(QString::number(part.toInt()));
    auto result = std::make_shared<BlenderVideoResult>();
    result->mp4 = mp4;
    result->manifest = QJsonObject{{"file", "video.mp4"},
                                   {"bytes", qint64(mp4.size())},
                                   {"sha256", digest},
                                   {"encoder", "blender-" + version.join('.')},
                                   {"container", "mp4"},
                                   {"codec", h264 ? "h264" : "mpeg4"},
                                   {"frames", int(request.frames.size())},
                                   {"framesPerSecond", request.framesPerSecond},
                                   {"inputSha256", inputs}};
    return result;
}
// The FFmpeg codec identifiers of a probe: at most 64 names, each of 1 to 32 capitals, digits or
// underscores. A value that is not such a list fails the probe.
std::optional<QStringList> videoCodecList(const QJsonValue &value) {
    require(value.isArray() && value.toArray().size() <= 64, "Malformed Blender video codec list");
    QStringList codecs;
    for (const auto item : value.toArray()) {
        require(item.isString() &&
                    QRegularExpression("^[A-Z0-9_]{1,32}$").match(item.toString()).hasMatch(),
                "Malformed Blender video codec list");
        codecs.append(item.toString());
    }
    return codecs;
}
} // namespace

struct BlenderJob::Impl {
    BlenderJob &owner;
    QProcess process;
    QTimer deadline, killTimer, poll;
    Phase phase{Phase::Idle}, stopPhase{Phase::Failed};
    Options options;
    bool probing{}, sceneOnly{}, encoding{}, slot{}, stopping{};
    int attempt{}, outputBytes{};
    QString executable, currentBackend, currentDevice, progressText, attemptDirectory, errorCode,
        errorMessage;
    QByteArray log, line;
    QJsonArray attempts;
    QJsonObject workerReport;
    std::shared_ptr<const PreparedRender> input;
    std::shared_ptr<QTemporaryDir> jobRoot;
    std::shared_ptr<const BlenderResult> result;
    std::future<std::shared_ptr<const BlenderResult>> verification;
    std::future<std::shared_ptr<const BlenderSceneResult>> sceneVerification;
    std::shared_ptr<const BlenderSceneResult> sceneResult;
    std::optional<BlenderVideoRequest> video;
    std::optional<QStringList> codecs; // The last probe's validated codec list, when reported.
    std::future<std::shared_ptr<const BlenderVideoResult>> videoVerification;
    std::shared_ptr<const BlenderVideoResult> videoResult;
    std::unique_ptr<sketchyup::sandbox::PreparedPolicy> childPolicy; // the child's sandbox, prepared before its fork
    QString sandboxRefusal; // the child's "sandboxUnavailable" line, when a layer could not be installed
    explicit Impl(BlenderJob &owner)
        : owner(owner), process(&owner), deadline(&owner), killTimer(&owner), poll(&owner) {
        deadline.setSingleShot(true);
        killTimer.setSingleShot(true);
        poll.setInterval(10);
        QObject::connect(&deadline, &QTimer::timeout, &owner, [this] {
            stop(Phase::TimedOut, "TIMED_OUT", "Blender exceeded the job deadline");
        });
        QObject::connect(&killTimer, &QTimer::timeout, &owner, [this] {
            if (process.state() != QProcess::NotRunning)
                process.kill();
        });
        QObject::connect(&process, &QProcess::readyReadStandardOutput, &owner, [this] { drain(); });
        QObject::connect(&process, &QProcess::errorOccurred, &owner,
                         [this](QProcess::ProcessError error) {
                             if (error == QProcess::FailedToStart)
                                 finish(Phase::Unavailable, "START_FAILED", process.errorString());
                         });
        QObject::connect(&process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                         &owner,
                         [this](int code, QProcess::ExitStatus status) { finished(code, status); });
        QObject::connect(&poll, &QTimer::timeout, &owner, [this] {
            if (encoding) {
                if (!videoVerification.valid() || videoVerification.wait_for(std::chrono::seconds(
                                                      0)) != std::future_status::ready)
                    return;
            } else if (sceneOnly) {
                if (!sceneVerification.valid() || sceneVerification.wait_for(std::chrono::seconds(
                                                      0)) != std::future_status::ready)
                    return;
            } else if (!verification.valid() ||
                       verification.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                return;
            poll.stop();
            try {
                if (encoding) {
                    auto candidate = videoVerification.get();
                    if (stopping) {
                        finish(stopPhase, errorCode, errorMessage);
                        return;
                    }
                    auto published = std::make_shared<BlenderVideoResult>(*candidate);
                    published->manifest["attempts"] = attempts;
                    videoResult = std::move(published);
                    finish(Phase::Succeeded);
                    return;
                }
                if (sceneOnly) {
                    auto candidate = sceneVerification.get();
                    if (stopping) {
                        finish(stopPhase, errorCode, errorMessage);
                        return;
                    }
                    auto published = std::make_shared<BlenderSceneResult>(*candidate);
                    published->manifest["attempts"] = attempts;
                    published->manifest["cpuFallbackUsed"] = attempt > 1;
                    sceneResult = std::move(published);
                    finish(Phase::Succeeded);
                    return;
                }
                auto candidate = verification.get();
                if (stopping) {
                    finish(stopPhase, errorCode, errorMessage);
                    return;
                }
                auto published = std::make_shared<BlenderResult>(*candidate);
                published->manifest["attempts"] = attempts;
                published->manifest["cpuFallbackUsed"] = attempt > 1;
                result = std::move(published);
                finish(Phase::Succeeded);
            } catch (const std::exception &error) {
                finish(stopping ? stopPhase : Phase::Failed,
                       stopping ? errorCode : "INVALID_RESULT",
                       stopping ? errorMessage : QString::fromUtf8(error.what()));
            }
        });
    }
    ~Impl() {
        QObject::disconnect(&process, nullptr, &owner, nullptr);
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished(2000);
        }
        if (slot)
            --activeJobs;
    }
    void checkOwner() const {
        require(QThread::currentThread() == owner.thread(),
                "Blender job requires its owner thread");
    }
    void changed(Phase next) {
        phase = next;
        emit owner.changed();
    }
    void finish(Phase next, QString code = {}, QString message = {}) {
        if (terminal(phase))
            return;
        deadline.stop();
        killTimer.stop();
        poll.stop();
        errorCode = std::move(code);
        errorMessage = std::move(message);
        if (slot) {
            --activeJobs;
            slot = false;
        }
        changed(next);
    }
    QString root() const { return jobRoot->path(); }
    void begin(bool probe, std::shared_ptr<const PreparedRender> prepared, Options requested,
               bool scene = false) {
        checkOwner();
        require(phase == Phase::Idle, "A Blender job can start only once");
        require(backend(requested.backend) && requested.deviceId.size() <= 512 &&
                    requested.timeoutMs >= 1 && requested.timeoutMs <= 3600000,
                "Invalid Blender job options");
        require(requested.backend == "CPU" || !requested.deviceId.isEmpty(),
                "Select an explicit GPU device");
        probing = probe;
        sceneOnly = scene;
        input = std::move(prepared);
        options = std::move(requested);
        if (!probing && !encoding)
            require(bool(input), "Render requires a prepared immutable snapshot");
        if (!probing && !encoding)
            require((input->manifest().value("settings").toObject().value("engine") == "eevee") ==
                        (options.backend == "OPENGL"),
                    "Render engine and selected device backend do not match");
        executable = options.executable.isEmpty() ? QStandardPaths::findExecutable("blender")
                                                  : options.executable;
        const QFileInfo file(executable);
        if (executable.isEmpty() || !file.isAbsolute() || !file.isFile() || !file.isExecutable()) {
            finish(Phase::Unavailable, "BLENDER_UNAVAILABLE",
                   "Choose an installed Blender 5.2 LTS executable");
            return;
        }
        executable = file.canonicalFilePath();
        const int previous = activeJobs.fetch_add(1);
        if (previous >= 2) {
            --activeJobs;
            finish(Phase::Failed, "BUSY", "Two Blender jobs are already active");
            return;
        }
        slot = true;
        const auto scratchParent =
            options.scratchParent.isEmpty() ? QDir::tempPath() : options.scratchParent;
        require(QFileInfo(scratchParent).isAbsolute() && QFileInfo(scratchParent).isDir() &&
                    !QFileInfo(scratchParent).isSymLink(),
                "Invalid private worker scratch directory");
        jobRoot = std::make_shared<QTemporaryDir>(scratchParent + (options.scratchParent.isEmpty()
                                                                       ? "/sketchyup-worker-XXXXXX"
                                                                       : "/scratch-XXXXXX"));
        if (!jobRoot->isValid()) {
            finish(Phase::Failed, "PREPARE_FAILED", "Cannot create private worker directory");
            return;
        }
        currentBackend = options.backend;
        currentDevice = currentBackend == "CPU" ? "CPU" : options.deviceId;
        deadline.start(probing ? std::min(options.timeoutMs, 15000) : options.timeoutMs);
        launch();
    }
    void launch() {
        try {
            ++attempt;
            outputBytes = 0;
            log.clear();
            line.clear();
            workerReport = {};
            attemptDirectory = root() + "/attempt-" + QString::number(attempt);
            require(QDir().mkdir(attemptDirectory),
                    "Cannot create fresh Blender attempt directory");
            require(QFile::setPermissions(attemptDirectory, QFileDevice::ReadOwner |
                                                                QFileDevice::WriteOwner |
                                                                QFileDevice::ExeOwner),
                    "Cannot protect Blender attempt directory");
            // HOME, the XDG directories and TMPDIR of the child live here (ADR 0183 section 5).
            for (const auto &directory : blender_sandbox::sandboxDirectories())
                require(QDir(attemptDirectory).mkpath(directory),
                        "Cannot isolate Blender's home and temporary directories");
            require(QFile::setPermissions(attemptDirectory + "/xdg/runtime",
                                          QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                              QFileDevice::ExeOwner),
                    "Cannot protect Blender's runtime directory");
            initializeBlenderResource();
            QFile script(":/sketchyup/blender_worker.py");
            require(script.open(QIODevice::ReadOnly), "Embedded Blender adapter is unavailable");
            write(attemptDirectory + "/worker.py", script.readAll());
            QJsonObject request{{"apiVersion", 1},
                                {"operation", probing     ? "probe"
                                              : encoding  ? "encode"
                                              : sceneOnly ? "handoff"
                                                          : "render"},
                                {"backend", currentBackend},
                                {"deviceId", currentDevice}};
            if (input) {
                request["sourceDirectory"] = input->sourceDirectory();
                request["manifestSha256"] = input->manifestHash();
            }
            if (encoding) {
                QJsonArray frames;
                for (const auto &frame : video->frames)
                    frames.append(QJsonObject{
                        {"path", frame.path}, {"sha256", frame.sha256}, {"bytes", frame.bytes}});
                request["video"] = QJsonObject{{"frames", frames},
                                               {"width", video->width},
                                               {"height", video->height},
                                               {"framesPerSecond", video->framesPerSecond},
                                               {"codec", video->codec}};
            }
            write(attemptDirectory + "/request.json",
                  QJsonDocument(request).toJson(QJsonDocument::Compact));
            require(QDir(attemptDirectory).mkdir("config") &&
                        QDir(attemptDirectory).mkdir("scripts"),
                    "Cannot isolate Blender preferences");
            auto environment = blender_sandbox::environment(QProcessEnvironment::systemEnvironment(),
                                                            attemptDirectory);
            environment.insert("BLENDER_USER_CONFIG", attemptDirectory + "/config");
            environment.insert("BLENDER_USER_SCRIPTS", attemptDirectory + "/scripts");
            process.setProcessEnvironment(environment);
            process.setWorkingDirectory(attemptDirectory);
            process.setProcessChannelMode(QProcess::MergedChannels);
            process.setProgram(executable);
            // --offline-mode: Blender makes no network connection of its own (ADR 0183 decision 6).
            process.setArguments({"--background", "--factory-startup", "--disable-autoexec",
                                  "--offline-mode", "--threads", "4", "--python-exit-code", "1",
                                  "--python", attemptDirectory + "/worker.py", "--",
                                  attemptDirectory + "/request.json"});
            // The child's policy is built here, before the fork: the rule paths are opened and the
            // BPF program is built in this process (ADR 0183 section 4.1). A layer that cannot be
            // prepared refuses the job now, and Blender does not start.
            blender_sandbox::Layout layout{executable, attemptDirectory,
                                           input ? input->sourceDirectory() : QString(), {}};
            if (encoding)
                for (const auto &frame : video->frames)
                    layout.inputFiles.append(frame.path);
            auto rules = blender_sandbox::policy(layout);
            for (const auto &directory : testWritableDirectories)
                rules.files.push_back({directory.toStdString(), sketchyup::sandbox::FileAccess::readWrite});
            childPolicy = std::make_unique<sketchyup::sandbox::PreparedPolicy>();
            if (const auto failure = childPolicy->prepare(
                    rules, sketchyup::sandbox::ApplyOptions{options.sandboxSimulation})) {
                finish(Phase::Unavailable, "sandboxUnavailable",
                       sandboxRefusalMessage(QString::fromStdString(sketchyup::sandbox::unavailableReason(*failure))));
                return;
            }
            progressText = probing    ? "Checking Blender"
                           : encoding ? "Starting video encoding"
                                      : "Starting Blender";
            changed(probing ? Phase::Probing : Phase::Rendering);
            if (stopping) {
                finish(stopPhase, errorCode, errorMessage);
                return;
            }
#ifdef Q_OS_LINUX
            const auto ownerPid = getpid();
            process.setChildProcessModifier([ownerPid, policy = childPolicy.get()] {
                // Syscalls only from here to exec: no allocation and no lock (ADR 0183 section 4.1).
                if (prctl(PR_SET_PDEATHSIG, SIGKILL) != 0 || getppid() != ownerPid)
                    _exit(127);
                if (const auto failure = policy->enter()) {
                    sketchyup::sandbox::writeUnavailableLine(*failure);
                    _exit(blender_sandbox::kRefusedExit);
                }
            });
#endif
            process.start();
            process.closeWriteChannel();
        } catch (const std::exception &error) {
            finish(Phase::Failed, "PREPARE_FAILED", QString::fromUtf8(error.what()));
        }
    }
    void stop(Phase target, QString code, QString message) {
        if (terminal(phase) || stopping)
            return;
        stopping = true;
        stopPhase = target;
        errorCode = std::move(code);
        errorMessage = std::move(message);
        deadline.stop();
        changed(Phase::Canceling);
        if (process.state() != QProcess::NotRunning) {
            process.terminate();
            killTimer.start(1000);
        } else if (!verification.valid() && !sceneVerification.valid() &&
                   !videoVerification.valid())
            finish(stopPhase, errorCode, errorMessage);
    }
    void drain() {
        const auto bytes = process.readAllStandardOutput();
        log.append(bytes);
        if (log.size() > 64 * 1024)
            log = log.right(64 * 1024);
        try {
            write(attemptDirectory + "/worker.log", log);
        } catch (const std::exception &error) {
            stop(Phase::Failed, "LOG_WRITE_FAILED", QString::fromUtf8(error.what()));
            return;
        }
        if (bytes.size() > 2 * 1024 * 1024 - outputBytes) {
            stop(Phase::Failed, "OUTPUT_LIMIT", "Blender output exceeded 2 MiB");
            return;
        }
        outputBytes += bytes.size();
        line.append(bytes);
        while (true) {
            const auto newline = line.indexOf('\n');
            if (newline < 0)
                break;
            const auto row = line.left(newline);
            line.remove(0, newline + 1);
            if (row.startsWith("sandboxUnavailable: ") && row.size() < 512)
                sandboxRefusal = QString::fromUtf8(row);
            if (row.startsWith("SKETCHYUP_PROGRESS ") && row.size() < 1024 && !stopping) {
                const auto phase =
                    QJsonDocument::fromJson(row.mid(19)).object().value("phase").toString();
                if (QStringList{"loading", "rendering", "verifying-output", "saving-scene",
                                "encoding-video"}
                        .contains(phase)) {
                    progressText = phase;
                    emit owner.changed();
                }
            }
        }
        if (line.size() > 16 * 1024)
            line.clear();
    }
    void finished(int code, QProcess::ExitStatus status) {
        if (terminal(phase))
            return;
        drain();
        killTimer.stop();
        if (stopping) {
            finish(stopPhase, errorCode, errorMessage);
            return;
        }
        try {
            workerReport = object(read(attemptDirectory + "/result.json", 64 * 1024));
        } catch (const std::exception &) {
            workerReport = {};
        }
        attempts.append(QJsonObject{{"backend", currentBackend},
                                    {"deviceId", currentDevice},
                                    {"exitCode", code},
                                    {"normalExit", status == QProcess::NormalExit},
                                    {"workerStatus", workerReport.value("status")},
                                    {"code", workerReport.value("code")},
                                    {"logTail", QString::fromUtf8(log)}});
        const auto failure = workerReport.value("code").toString();
        if (code != 0 || status != QProcess::NormalExit ||
            workerReport.value("status") == "failed") {
            // ADR 0183 sections 4.6 and 7: a sandbox kill and a sandbox refusal are named, and neither is
            // retried on the CPU, because the same policy would refuse the same call again.
            if (status == QProcess::CrashExit && code == SIGSYS) {
                finish(Phase::Failed, "sandboxViolation", sandboxViolationMessage());
                return;
            }
            if (status == QProcess::NormalExit && code == blender_sandbox::kRefusedExit &&
                !sandboxRefusal.isEmpty()) {
                finish(Phase::Unavailable, "sandboxUnavailable", sandboxRefusalMessage(sandboxRefusal));
                return;
            }
            const bool mayRetry =
                failure.isEmpty() || failure == "render_error" || failure == "device_unavailable";
            if (!probing && attempt == 1 && currentBackend != "CPU" && currentBackend != "OPENGL" &&
                options.allowCpuFallback && mayRetry) {
                currentBackend = "CPU";
                currentDevice = "CPU";
                progressText = "Retrying on CPU";
                emit owner.changed();
                if (!stopping && !terminal(phase))
                    launch();
                return;
            }
            const bool unavailable =
                failure == "unsupported_version" || failure == "cycles_unavailable";
            finish(unavailable ? Phase::Unavailable : Phase::Failed,
                   failure.isEmpty() ? "BLENDER_FAILED" : failure,
                   workerReport.value("message").toString("Blender did not complete successfully"));
            return;
        }
        try {
            version(workerReport);
            if (probing) {
                require(workerReport.value("status") == "available" &&
                            workerReport.value("backends").isArray() &&
                            workerReport.value("devices").isArray(),
                        "Malformed Blender capabilities");
                const auto backends = workerReport.value("backends").toArray(),
                           devices = workerReport.value("devices").toArray();
                require(backends.size() <= 7 && devices.size() <= 64,
                        "Blender capabilities exceed bounds");
                const auto engines = workerReport.value("engines").toArray();
                require(engines == QJsonArray{currentBackend == "OPENGL" ? "eevee" : "cycles"},
                        "Worker capabilities do not match the requested engine");
                for (auto name : backends)
                    require(name.isString() && backend(name.toString()), "Unknown Blender backend");
                for (auto value : devices) {
                    const auto device = value.toObject();
                    if (currentBackend == "OPENGL")
                        verifyRasterDevice(device);
                    require(device.value("id").isString() && device.value("name").isString() &&
                                device.value("id").toString().size() <= 512 &&
                                device.value("name").toString().size() <= 512 &&
                                device.value("backend") == currentBackend,
                            "Malformed Blender device");
                }
                // An absent field is an absent codec list; a present one must be well formed.
                codecs = workerReport.contains("videoCodecs")
                             ? videoCodecList(workerReport.value("videoCodecs"))
                             : std::optional<QStringList>{};
                finish(Phase::Succeeded);
                return;
            }
            progressText = sceneOnly ? "Verifying scene" : "Verifying image";
            if (encoding) {
                progressText = "Verifying video";
                videoVerification =
                    std::async(std::launch::async, [request = *video, directory = attemptDirectory,
                                                    report = workerReport] {
                        return verifyVideo(directory, report, request);
                    });
                poll.start();
                changed(Phase::Verifying);
                return;
            }
            if (sceneOnly)
                sceneVerification =
                    std::async(std::launch::async, [captured = input, directory = attemptDirectory,
                                                    report = workerReport, kind = currentBackend,
                                                    device = currentDevice] {
                        return verifyScene(*captured, directory, report, kind, device);
                    });
            else
                verification =
                    std::async(std::launch::async, [captured = input, directory = attemptDirectory,
                                                    report = workerReport, kind = currentBackend,
                                                    device = currentDevice] {
                        return verify(*captured, directory, report, kind, device);
                    });
            poll.start();
            changed(Phase::Verifying);
        } catch (const std::exception &error) {
            finish(Phase::Failed, "INVALID_RESULT", QString::fromUtf8(error.what()));
        }
    }
};
void saveBlenderScene(const BlenderSceneResult &scene, const QString &path) {
    const QFileInfo destination(path);
    require(!path.isEmpty() && destination.suffix().compare("blend", Qt::CaseInsensitive) == 0 &&
                !destination.isSymLink(),
            "Choose a regular .blend destination for the one-way scene");
    const auto file = scene.manifest.value("sceneFile").toObject();
    require(scene.blend.size() > 17 && scene.blend.size() <= 512 * 1024 * 1024 &&
                scene.blend.startsWith("BLENDER17-01v0502") &&
                file.value("sha256") == hash(scene.blend) &&
                file.value("bytes").toInteger(-1) == scene.blend.size(),
            "Scene integrity changed before saving");
    QSaveFile output(path);
    require(output.open(QIODevice::WriteOnly) && output.write(scene.blend) == scene.blend.size() &&
                output.commit(),
            "Could not save the Blender scene");
}
void setBlenderTestWritableDirectories(QStringList directories) {
    testWritableDirectories = std::move(directories);
}
BlenderJob::BlenderJob(QObject *parent) : QObject(parent), impl_(std::make_unique<Impl>(*this)) {}
BlenderJob::~BlenderJob() = default;
void BlenderJob::probe(Options options) { impl_->begin(true, {}, std::move(options)); }
void BlenderJob::start(std::shared_ptr<const PreparedRender> input, Options options) {
    impl_->begin(false, std::move(input), std::move(options));
}
void BlenderJob::handoff(std::shared_ptr<const PreparedRender> input, Options options) {
    impl_->begin(false, std::move(input), std::move(options), true);
}
void BlenderJob::encode(BlenderVideoRequest request, Options options) {
    impl_->checkOwner();
    require(request.codec == "H264" || request.codec == "MPEG4", "Unsupported video codec");
    impl_->video = std::move(request);
    impl_->encoding = true;
    impl_->begin(false, nullptr, std::move(options));
}
std::shared_ptr<const BlenderVideoResult> BlenderJob::videoResult() const {
    impl_->checkOwner();
    return impl_->videoResult;
}
std::optional<QStringList> BlenderJob::videoCodecs() const {
    impl_->checkOwner();
    return impl_->codecs;
}
std::shared_ptr<const BlenderSceneResult> BlenderJob::sceneResult() const {
    impl_->checkOwner();
    return impl_->sceneResult;
}
void BlenderJob::cancel() {
    impl_->checkOwner();
    impl_->stop(Phase::Canceled, "CANCELED", "Render canceled");
}
BlenderJob::Phase BlenderJob::phase() const {
    impl_->checkOwner();
    return impl_->phase;
}
bool BlenderJob::done() const { return terminal(phase()); }
qint64 BlenderJob::processId() const {
    impl_->checkOwner();
    return impl_->process.processId();
}
QString BlenderJob::progress() const {
    impl_->checkOwner();
    return impl_->progressText;
}
QJsonObject BlenderJob::report() const {
    impl_->checkOwner();
    return {{"phase", int(impl_->phase)},
            {"code", impl_->errorCode},
            {"message", impl_->errorMessage},
            {"progress", impl_->progressText},
            {"executable", impl_->executable},
            {"attempts", impl_->attempts},
            {"logTail", QString::fromUtf8(impl_->log)},
            {"worker", impl_->workerReport},
            {"cpuFallbackUsed", impl_->attempt > 1}};
}
std::shared_ptr<const BlenderResult> BlenderJob::result() const {
    impl_->checkOwner();
    return impl_->result;
}
} // namespace sketchy
