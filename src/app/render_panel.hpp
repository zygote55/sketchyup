#pragma once
#include "app/presentation_batches.hpp"
#include "app/viewport.hpp"
#include "core/render_settings.hpp"
#include "integrations/animation_capture.hpp"
#include "integrations/blender_job.hpp"
#include <QObject>
class QTabWidget;
class QPushButton;
namespace sketchy {
class RenderPanel : public QObject {
    Q_OBJECT
  public:
    RenderPanel(Document &document, Viewport &view, QTabWidget &tabs, QPushButton &chip,
                QWidget &window);
    ~RenderPanel() override;
    void showSetup();
    // R099.d2: the render sheet's current settings, for a presentation preset.
    RenderSettings setupSettings() const;
    void showJobs();
    // R099.d3 (UX_DESIGN §17.4, §17.7, §17.12): the batch dialog, a batch's results sheet, and the
    // running batch the quit prompt stops.
    void showBatchDialog();
    void showBatchResults(const QString &id);
    bool batchRunning() const;
    void stopBatch();
    PresentationBatches &batches();
    void refreshProvenance();
    void start(RenderOptions settings, BlenderJob::Options worker, bool currentView);
    void exportAnimation(std::vector<Id> scenes, CameraPathTiming timing, RenderOptions settings,
                         BlenderJob::Options worker, const QString &directory);
    void cancel();
    bool active() const;
    QString status() const;
    std::shared_ptr<const BlenderResult> latest() const;
    void saveLatest(const QString &path);
    void handoffLatest(const QString &path, bool launchBlender);
  signals:
    void changed();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace sketchy
