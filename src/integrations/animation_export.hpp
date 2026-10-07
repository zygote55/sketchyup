#pragma once
#include "integrations/animation_capture.hpp"
#include "integrations/blender_job.hpp"
#include <QObject>
#include <memory>
namespace sketchy {
// One immutable frame worker at a time; finished files survive cancellation.
class AnimationExport : public QObject {
    Q_OBJECT
  public:
    enum class State { Idle, Running, Canceling, Completed, Canceled, Failed };
    explicit AnimationExport(QObject *parent = nullptr);
    ~AnimationExport() override;
    void start(AnimationCapture capture, QString directory, BlenderJob::Options options);
    void cancel();
    State state() const;
    int completedFrames() const;
    int totalFrames() const;
    QString message() const;
    QString directory() const;
    bool done() const;
  signals:
    void changed();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace sketchy
