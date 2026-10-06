#include "integrations/blender_job.hpp"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <future>
#ifdef Q_OS_LINUX
#include <csignal>
#include <sys/prctl.h>
#include <unistd.h>
#endif
static void initializeBlenderResource() { Q_INIT_RESOURCE(blender_worker); }
namespace sketchy {
namespace {
std::atomic<int> activeJobs{};
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
std::shared_ptr<const BlenderResult> verify(const PreparedRender &input, const QString &directory,
                                            const QJsonObject &result,
                                            const QString &requestedBackend,
                                            const QString &requestedDevice) {
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
struct BlenderJob::Impl {
    BlenderJob &owner;
    QProcess process;
    QTimer deadline, killTimer, poll;
    Phase phase{Phase::Idle}, stopPhase{Phase::Failed};
    Options options;
    bool probing{}, slot{}, stopping{};
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
            if (!verification.valid() ||
                verification.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                return;
            poll.stop();
            try {
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
    void begin(bool probe, std::shared_ptr<const PreparedRender> prepared, Options requested) {
        checkOwner();
        require(phase == Phase::Idle, "A Blender job can start only once");
        require(backend(requested.backend) && requested.deviceId.size() <= 512 &&
                    requested.timeoutMs >= 1 && requested.timeoutMs <= 3600000,
                "Invalid Blender job options");
        require(requested.backend == "CPU" || !requested.deviceId.isEmpty(),
                "Select an explicit GPU device");
        probing = probe;
        input = std::move(prepared);
        options = std::move(requested);
        if (!probing)
            require(bool(input), "Render requires a prepared immutable snapshot");
        if (!probing)
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
            initializeBlenderResource();
            QFile script(":/sketchyup/blender_worker.py");
            require(script.open(QIODevice::ReadOnly), "Embedded Blender adapter is unavailable");
            write(attemptDirectory + "/worker.py", script.readAll());
            QJsonObject request{{"apiVersion", 1},
                                {"operation", probing ? "probe" : "render"},
                                {"backend", currentBackend},
                                {"deviceId", currentDevice}};
            if (input) {
                request["sourceDirectory"] = input->sourceDirectory();
                request["manifestSha256"] = input->manifestHash();
            }
            write(attemptDirectory + "/request.json",
                  QJsonDocument(request).toJson(QJsonDocument::Compact));
            auto environment = QProcessEnvironment::systemEnvironment();
            for (const auto &key : environment.keys())
                if (key.startsWith("BLENDER_") || key == "PYTHONPATH" || key == "PYTHONHOME")
                    environment.remove(key);
            for (const auto *key : {"DISPLAY", "WAYLAND_DISPLAY"})
                environment.remove(key);
            require(QDir(attemptDirectory).mkdir("config") &&
                        QDir(attemptDirectory).mkdir("scripts"),
                    "Cannot isolate Blender preferences");
            environment.insert("BLENDER_USER_CONFIG", attemptDirectory + "/config");
            environment.insert("BLENDER_USER_SCRIPTS", attemptDirectory + "/scripts");
            process.setProcessEnvironment(environment);
            process.setWorkingDirectory(attemptDirectory);
            process.setProcessChannelMode(QProcess::MergedChannels);
            process.setProgram(executable);
            process.setArguments({"--background", "--factory-startup", "--disable-autoexec",
                                  "--threads", "4", "--python-exit-code", "1", "--python",
                                  attemptDirectory + "/worker.py", "--",
                                  attemptDirectory + "/request.json"});
            progressText = probing ? "Checking Blender" : "Starting Blender";
            changed(probing ? Phase::Probing : Phase::Rendering);
            if (stopping) {
                finish(stopPhase, errorCode, errorMessage);
                return;
            }
#ifdef Q_OS_LINUX
            const auto ownerPid = getpid();
            process.setChildProcessModifier([ownerPid] {
                if (prctl(PR_SET_PDEATHSIG, SIGKILL) != 0 || getppid() != ownerPid)
                    _exit(127);
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
        } else if (!verification.valid())
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
            if (row.startsWith("SKETCHYUP_PROGRESS ") && row.size() < 1024 && !stopping) {
                const auto phase =
                    QJsonDocument::fromJson(row.mid(19)).object().value("phase").toString();
                if (QStringList{"loading", "rendering", "verifying-output"}.contains(phase)) {
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
                finish(Phase::Succeeded);
                return;
            }
            progressText = "Verifying image";
            verification =
                std::async(std::launch::async,
                           [captured = input, directory = attemptDirectory, report = workerReport,
                            kind = currentBackend, device = currentDevice] {
                               return verify(*captured, directory, report, kind, device);
                           });
            poll.start();
            changed(Phase::Verifying);
        } catch (const std::exception &error) {
            finish(Phase::Failed, "INVALID_RESULT", QString::fromUtf8(error.what()));
        }
    }
};
BlenderJob::BlenderJob(QObject *parent) : QObject(parent), impl_(std::make_unique<Impl>(*this)) {}
BlenderJob::~BlenderJob() = default;
void BlenderJob::probe(Options options) { impl_->begin(true, {}, std::move(options)); }
void BlenderJob::start(std::shared_ptr<const PreparedRender> input, Options options) {
    impl_->begin(false, std::move(input), std::move(options));
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
