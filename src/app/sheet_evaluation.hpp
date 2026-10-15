#pragma once
#include "io/drawing_sheet_paint.hpp"
#include <QImage>
#include <QObject>
#include <QThreadPool>
#include <QTimer>
#include <list>
#include <map>
#include <memory>
#include <optional>
namespace sketchy {
// One evaluated sheet (ADR 0164 §2). The capture is kept so the GUI thread can render raster
// content from the same snapshot the worker evaluated.
struct SheetEvaluation {
    std::shared_ptr<const DrawingSheetCapture> capture;
    PreparedDrawingSheet prepared;
    std::string error; // Preparing threw; `prepared` is empty.
};
// Desktop evaluation of drawing sheets (R091.e). The capture runs on the GUI thread
// (captureDrawingSheet reads the document into immutable values); the compute half
// (prepareDrawingSheet) runs on a worker, so typing and dragging never wait on it.
// - Revision changes are debounced; the visible page is evaluated first, then the others for
//   the References and Pages counts.
// - A result whose revision is older than the document when it arrives is dropped; until a
//   newer one arrives the last result stays available, so a view never draws empty.
// - Statuses are kept in a DrawingViewStatusCache keyed by (document identity, view,
//   revision). Raster content is keyed by view fingerprint and pixel size, so a
//   revision-only change, an undo back to equal content or moving a frame reuses it. Rasters
//   are bounded (64 MiB, least recently used first) and pruned when views disappear.
class DrawingViewRenderCache : public QObject {
    Q_OBJECT
  public:
    explicit DrawingViewRenderCache(const Document &doc, QObject *parent = nullptr);
    ~DrawingViewRenderCache() override;
    static constexpr int defaultDebounceMs = 150;
    static constexpr qsizetype rasterBudgetBytes = 64 * 1024 * 1024;
    void setDebounce(int milliseconds) { debounceMs_ = milliseconds; }
    // Call after every document change and when the visible page changes.
    void refresh(Id visibleSheet);
    // Records scene names, so a view whose scene is deleted later can still name it. Cheap;
    // call after every document change, also while evaluation is paused.
    void rememberScenes();
    // The newest result for a sheet, possibly from an older revision; null before the first.
    std::shared_ptr<const SheetEvaluation> sheet(Id sheet) const;
    // The sheet's result is for the document's current identity and revision.
    bool current(Id sheet) const;
    // Nothing is scheduled, running or out of date.
    bool idle() const;
    // A view's status from the newest evaluation of its sheet.
    std::optional<DrawingViewStatus> status(Id view) const;
    // The last name this session saw for a scene, for "Scene “…” was deleted".
    std::optional<std::string> sceneName(Id scene) const;
    // The largest cached raster of a view's content with this fingerprint; null when none.
    QImage raster(Id view, const std::string &fingerprint) const;
    bool hasRaster(Id view, const std::string &fingerprint, QSize pixels) const;
    void storeRaster(Id view, const std::string &fingerprint, const QImage &image);
    qsizetype rasterBytes() const { return rasterBytes_; }
    size_t evaluations() const { return evaluations_; }

  signals:
    // A current result for the sheet is available.
    void evaluated(qulonglong sheet);

  private:
    const Document &doc_;
    QThreadPool pool_;
    QTimer timer_;
    int debounceMs_{defaultDebounceMs};
    Id visible_{};
    bool running_{};
    size_t evaluations_{};
    std::map<Id, std::shared_ptr<const SheetEvaluation>> results_;
    DrawingViewStatusCache statuses_;
    std::map<Id, std::pair<std::string, std::uint64_t>> statusRevisions_; // view -> identity, rev
    std::map<std::pair<std::string, Id>, std::string> sceneNames_; // (document, scene) -> name
    struct Raster {
        Id view{};
        std::string fingerprint;
        QImage image;
    };
    std::list<Raster> rasters_; // Most recently used first.
    qsizetype rasterBytes_{};
    std::vector<Id> outdated() const;
    void startNext();
    void finished(Id sheet, std::shared_ptr<const SheetEvaluation> result);
    void prune();
};
} // namespace sketchy
