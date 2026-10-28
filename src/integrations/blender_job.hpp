#pragma once
#include "integrations/glb_export.hpp"
#include "sandbox/sandbox.hpp"
#include <QImage>
#include <QObject>
#include <QStringList>
#include <QTemporaryDir>
#include <memory>
#include <optional>
#include <vector>
namespace sketchy {
class PreparedRender {
  public:
    // Expensive preparation is safe on a worker with an already captured value.
    static std::shared_ptr<const PreparedRender> prepare(const RenderSnapshot &snapshot);
    static std::shared_ptr<const PreparedRender> open(const QString &directory,
                                                      const QString &manifestHash);
    void copyTo(const QString &directory) const;
    const QJsonObject &manifest() const { return manifest_; }
    const QString &manifestHash() const { return manifestHash_; }
    QString directory() const { return root_->path(); }
    QString sourceDirectory() const { return directory() + "/scene"; }

  private:
    PreparedRender() = default;
    std::shared_ptr<QTemporaryDir> root_;
    QJsonObject manifest_;
    QString manifestHash_;
};
struct BlenderResult {
    QJsonObject manifest;
    QByteArray png;
    QImage image;
};
struct BlenderSceneResult {
    QJsonObject manifest;
    QByteArray blend;
};

// One verified frame of a video, in playback order: its file, its SHA-256 and its byte count.
struct BlenderVideoFrame {
    QString path, sha256;
    qint64 bytes{};
};

// The input of one video encode (ADR 0173 §4, decision 7). The worker re-hashes every frame.
// The codec is the worker's FFmpeg identifier: "H264" (the default) or "MPEG4" (MPEG-4 Part 2).
struct BlenderVideoRequest {
    std::vector<BlenderVideoFrame> frames;
    int width{}, height{}, framesPerSecond{};
    QString codec{"H264"};
};

// The encoded MP4 and its manifest record. Written to the output only after verification.
struct BlenderVideoResult {
    QJsonObject manifest;
    QByteArray mp4;
};
void saveBlenderScene(const BlenderSceneResult &scene, const QString &path);

// Test seam, like setPresentationVideoProbe: the directories a job's child may also read and write,
// so a test's stand-in can count its launches (tests/blender_stand_in.hpp). Empty in the
// application, and no Blender run is given it otherwise. Set it before the jobs start.
void setBlenderTestWritableDirectories(QStringList directories);
class BlenderJob : public QObject {
    Q_OBJECT
  public:
    enum class Phase {
        Idle,
        Probing,
        Rendering,
        Verifying,
        Canceling,
        Succeeded,
        Failed,
        Unavailable,
        Canceled,
        TimedOut
    };
    Q_ENUM(Phase)
    struct Options {
        QString executable; // Empty discovers blender on PATH; otherwise an absolute file.
        QString backend{"CPU"}, deviceId{"CPU"};
        QString scratchParent; // Optional existing private parent for disposable worker files.
        bool allowCpuFallback{true};
        int timeoutMs{180000};
        // Test injection, never set by the application (ADR 0183 section 6.7): the child's sandbox
        // layer fails as if the kernel refused it. A Landlock failure refuses the job before the
        // fork.
        std::optional<sketchyup::sandbox::Failure> sandboxSimulation;
    };
    explicit BlenderJob(QObject *parent = nullptr);
    ~BlenderJob() override;
    void probe(Options options);
    void start(std::shared_ptr<const PreparedRender> input, Options options);
    void handoff(std::shared_ptr<const PreparedRender> input, Options options);
    // Encodes verified frames into one MP4 in the worker's private scratch, with the codec the
    // request names. Refused with `codec_unavailable` when this Blender has no such encoder; the
    // codec is never switched.
    void encode(BlenderVideoRequest request, Options options);
    // The FFmpeg codec identifiers of the last probe, validated. Unset when the probe did not report
    // them (an absent field); a malformed field fails the probe.
    std::optional<QStringList> videoCodecs() const;
    void cancel();
    Phase phase() const;
    bool done() const;
    QString progress() const;
    qint64 processId() const;
    QJsonObject report() const;
    std::shared_ptr<const BlenderResult> result() const;
    std::shared_ptr<const BlenderSceneResult> sceneResult() const;
    std::shared_ptr<const BlenderVideoResult> videoResult() const;
  signals:
    void changed();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
std::shared_ptr<const BlenderResult> loadBlenderResult(const PreparedRender &input,
                                                       const QString &directory,
                                                       const QJsonObject &manifest,
                                                       const BlenderJob::Options &requested);
} // namespace sketchy
