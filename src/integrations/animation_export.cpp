#include "integrations/animation_export.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QSaveFile>
#include <QThread>
#include <QTimer>
#include <future>
namespace sketchy {
namespace {
constexpr qint64 outputLimit = 2LL * 1024 * 1024 * 1024;
void write(const QString &path, const QByteArray &bytes) {
    if (QFileInfo::exists(path) || QFileInfo(path).isSymLink())
        throw std::runtime_error("Animation output already exists");
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        throw std::runtime_error("Could not save animation output");
}
QString digest(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
} // namespace
struct AnimationExport::Impl {
    AnimationExport &owner;
    State state{State::Idle};
    QString directory, message;
    std::optional<AnimationCapture> capture;
    BlenderJob::Options options;
    QPointer<BlenderJob> worker;
    std::future<std::shared_ptr<const PreparedRender>> preparing;
    std::shared_ptr<const PreparedRender> input;
    QJsonArray frames;
    QTimer timer;
    qint64 bytes{};
    bool canceling{};
    explicit Impl(AnimationExport &owner) : owner(owner) {
        timer.setInterval(10);
        QObject::connect(&timer, &QTimer::timeout, &owner, [this] { poll(); });
    }
    void manifest(const QString &status) {
        QJsonObject value{{"apiVersion", 1},
                          {"adapter", "sketchyup-animation-v1"},
                          {"state", status},
                          {"message", message},
                          {"documentId", QString::fromStdString(capture->sourceIdentity())},
                          {"revision", QString::number(capture->sourceRevision())},
                          {"framesPerSecond", capture->framesPerSecond()},
                          {"totalFrames", int(capture->frames().size())},
                          {"frames", frames}};
        QSaveFile file(directory + "/animation.json");
        const auto data = QJsonDocument(value).toJson();
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
            throw std::runtime_error("Could not save animation progress manifest");
    }
    void terminal(State next, QString detail) {
        timer.stop();
        state = next;
        message = std::move(detail);
        try {
            manifest(next == State::Completed  ? "completed"
                     : next == State::Canceled ? "canceled"
                                               : "failed");
        } catch (const std::exception &error) {
            state = State::Failed;
            message = QString::fromUtf8(error.what());
        }
        input.reset();
        emit owner.changed();
    }
    void prepare() {
        if (canceling) {
            terminal(State::Canceled, "Animation canceled; finished frames are retained");
            return;
        }
        if (frames.size() == qsizetype(capture->frames().size())) {
            terminal(State::Completed, "Animation frame sequence completed");
            return;
        }
        message = QString("Preparing frame %1 of %2")
                      .arg(frames.size() + 1)
                      .arg(capture->frames().size());
        const auto index = size_t(frames.size());
        preparing = std::async(std::launch::async, [this, index] {
            return PreparedRender::prepare(capture->frame(index));
        });
        timer.start();
        emit owner.changed();
    }
    void poll() {
        if (!preparing.valid() ||
            preparing.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        timer.stop();
        try {
            input = preparing.get();
            if (canceling) {
                terminal(State::Canceled, "Animation canceled; finished frames are retained");
                return;
            }
            worker = new BlenderJob(&owner);
            QObject::connect(worker, &BlenderJob::changed, &owner, [this] { workerChanged(); });
            worker->start(input, options);
        } catch (const std::exception &error) {
            terminal(State::Failed, QString::fromUtf8(error.what()));
        }
    }
    void workerChanged() {
        if (!worker)
            return;
        if (!worker->done()) {
            message = QString("Frame %1 of %2 · %3")
                          .arg(frames.size() + 1)
                          .arg(capture->frames().size())
                          .arg(worker->progress());
            emit owner.changed();
            return;
        }
        const auto finished = worker;
        worker.clear();
        finished->deleteLater();
        if (canceling) {
            terminal(State::Canceled, "Animation canceled; finished frames are retained");
            return;
        }
        if (!finished->result()) {
            terminal(
                State::Failed,
                QString("Frame %1 failed: %2").arg(frames.size() + 1).arg(finished->progress()));
            return;
        }
        try {
            const auto result = finished->result();
            const auto name = QString("frame-%1").arg(frames.size() + 1, 4, 10, QChar('0'));
            QJsonObject record{
                {"index", frames.size()},
                {"file", name + ".png"},
                {"bytes", result->png.size()},
                {"sha256", digest(result->png)},
                {"scene", QString::number(capture->scenes().at(
                              capture->frames().at(size_t(frames.size())).sceneIndex))},
                {"camera", input->manifest()["camera"]},
                {"sourceManifestSha256", input->manifestHash()},
                {"result", result->manifest},
                {"diagnostics", finished->report()}};
            const auto metadata = QJsonDocument(record).toJson();
            if (metadata.size() > 1024 * 1024 ||
                bytes + result->png.size() + metadata.size() > outputLimit)
                throw std::runtime_error("Animation output exceeds its resource budget");
            write(directory + "/" + name + ".png", result->png);
            write(directory + "/" + name + ".json", metadata);
            bytes += result->png.size() + metadata.size();
            frames.append(QJsonObject{{"index", frames.size()},
                                      {"file", name + ".png"},
                                      {"sha256", digest(result->png)},
                                      {"manifest", name + ".json"}});
            manifest("running");
            input.reset();
            // Let Blender release its shared process slot before the next frame.
            QTimer::singleShot(0, &owner, [this] { prepare(); });
            emit owner.changed();
        } catch (const std::exception &error) {
            terminal(State::Failed, QString::fromUtf8(error.what()));
        }
    }
};
AnimationExport::AnimationExport(QObject *parent)
    : QObject(parent), impl_(std::make_unique<Impl>(*this)) {}
AnimationExport::~AnimationExport() {
    impl_->timer.stop();
    if (impl_->worker) {
        QObject::disconnect(impl_->worker, nullptr, this, nullptr);
        delete impl_->worker;
    }
    // The preparation only owns immutable data; keep that data alive until it ends.
    if (impl_->preparing.valid())
        impl_->preparing.wait();
}
void AnimationExport::start(AnimationCapture capture, QString directory,
                            BlenderJob::Options options) {
    if (QThread::currentThread() != thread() || impl_->state != State::Idle)
        throw std::runtime_error("Animation export starts once on its owner thread");
    const auto settings = capture.frame(0).settings();
    const qint64 perFrame =
        std::min<qint64>(64LL * 1024 * 1024,
                         qint64(settings.width) * settings.height * 5 + 1024 * 1024) +
        1024 * 1024;
    if (perFrame * qint64(capture.frames().size()) > outputLimit)
        throw std::runtime_error(
            "Animation exceeds the 2 GiB output budget; reduce size or frame count");
    QFileInfo info(directory);
    if (directory.isEmpty() || info.exists() || info.isSymLink() || !info.dir().exists() ||
        !info.dir().mkdir(info.fileName()))
        throw std::runtime_error("Choose a new animation folder within an existing directory");
    impl_->directory = info.absoluteFilePath();
    impl_->capture = std::move(capture);
    impl_->options = std::move(options);
    impl_->state = State::Running;
    try {
        impl_->manifest("running");
        impl_->prepare();
    } catch (const std::exception &error) {
        impl_->terminal(State::Failed, QString::fromUtf8(error.what()));
    }
}
void AnimationExport::cancel() {
    if (impl_->state != State::Running && impl_->state != State::Canceling)
        return;
    impl_->canceling = true;
    impl_->state = State::Canceling;
    impl_->message = "Canceling animation; finished frames are retained";
    if (impl_->worker)
        impl_->worker->cancel();
    else if (!impl_->preparing.valid())
        impl_->terminal(State::Canceled, impl_->message);
    emit changed();
}
AnimationExport::State AnimationExport::state() const { return impl_->state; }
int AnimationExport::completedFrames() const { return int(impl_->frames.size()); }
int AnimationExport::totalFrames() const {
    return impl_->capture ? int(impl_->capture->frames().size()) : 0;
}
QString AnimationExport::message() const { return impl_->message; }
QString AnimationExport::directory() const { return impl_->directory; }
bool AnimationExport::done() const {
    return impl_->state == State::Completed || impl_->state == State::Canceled ||
           impl_->state == State::Failed;
}
} // namespace sketchy
