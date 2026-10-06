#pragma once
#include "integrations/glb_export.hpp"
#include <QImage>
#include <QObject>
#include <QTemporaryDir>
#include <memory>
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
        bool allowCpuFallback{true};
        int timeoutMs{180000};
    };
    explicit BlenderJob(QObject *parent = nullptr);
    ~BlenderJob() override;
    void probe(Options options);
    void start(std::shared_ptr<const PreparedRender> input, Options options);
    void cancel();
    Phase phase() const;
    bool done() const;
    QString progress() const;
    QJsonObject report() const;
    std::shared_ptr<const BlenderResult> result() const;
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
