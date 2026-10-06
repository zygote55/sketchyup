#pragma once
#include "app/viewport.hpp"
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
    void showJobs();
    void refreshProvenance();
    void start(RenderOptions settings, BlenderJob::Options worker, bool currentView);
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
