#include "app/sheet_evaluation.hpp"
#include "core/drawing_sheets.hpp"
#include <algorithm>
namespace sketchy {
DrawingViewRenderCache::DrawingViewRenderCache(const Document &doc, QObject *parent)
    : QObject(parent), doc_(doc) {
    // One sheet at a time: results arrive in request order and a burst of edits never
    // occupies more than one core.
    pool_.setMaxThreadCount(1);
    timer_.setSingleShot(true);
    connect(&timer_, &QTimer::timeout, this, &DrawingViewRenderCache::startNext);
}
DrawingViewRenderCache::~DrawingViewRenderCache() {
    timer_.stop();
    // The worker posts back to this object; wait so it never outlives it.
    pool_.waitForDone();
}
std::shared_ptr<const SheetEvaluation> DrawingViewRenderCache::sheet(Id sheet) const {
    const auto found = results_.find(sheet);
    return found == results_.end() ? nullptr : found->second;
}
bool DrawingViewRenderCache::current(Id sheet) const {
    const auto found = results_.find(sheet);
    return found != results_.end() && found->second->capture->identity == doc_.identity() &&
           found->second->capture->revision == doc_.revision();
}
std::vector<Id> DrawingViewRenderCache::outdated() const {
    std::vector<Id> result;
    if (visible_ && doc_.drawingSheets().contains(visible_) && !current(visible_))
        result.push_back(visible_);
    for (const auto id : orderedDrawingSheets(doc_))
        if (id != visible_ && !current(id))
            result.push_back(id);
    return result;
}
bool DrawingViewRenderCache::idle() const {
    return !running_ && !timer_.isActive() && outdated().empty();
}
void DrawingViewRenderCache::refresh(Id visibleSheet) {
    visible_ = visibleSheet;
    rememberScenes();
    prune();
    const auto work = outdated();
    if (work.empty())
        return;
    // The first content for a page is not debounced; later changes are.
    const bool first = work.front() == visible_ && !results_.contains(visible_);
    timer_.start(first ? 0 : debounceMs_);
}
void DrawingViewRenderCache::rememberScenes() {
    for (const auto &[id, scene] : doc_.scenes())
        sceneNames_[{doc_.identity(), id}] = scene->name;
}
void DrawingViewRenderCache::prune() {
    for (auto it = results_.begin(); it != results_.end();)
        it = doc_.drawingSheets().contains(it->first) ? std::next(it) : results_.erase(it);
    statuses_.prune(doc_);
    for (auto it = statusRevisions_.begin(); it != statusRevisions_.end();)
        it = doc_.drawingViews().contains(it->first) ? std::next(it) : statusRevisions_.erase(it);
    for (auto it = rasters_.begin(); it != rasters_.end();) {
        if (doc_.drawingViews().contains(it->view)) {
            ++it;
            continue;
        }
        rasterBytes_ -= it->image.sizeInBytes();
        it = rasters_.erase(it);
    }
}
void DrawingViewRenderCache::startNext() {
    if (running_)
        return;
    const auto work = outdated();
    if (work.empty())
        return;
    const auto id = work.front();
    std::shared_ptr<const DrawingSheetCapture> capture;
    try {
        capture = std::make_shared<const DrawingSheetCapture>(captureDrawingSheet(doc_, id));
    } catch (const std::exception &error) {
        // Capture only throws for a missing sheet; prune drops it on the next refresh.
        auto failed = std::make_shared<SheetEvaluation>();
        DrawingSheetCapture empty{doc_.identity(), doc_.revision(), id, Document{}, {}};
        failed->capture = std::make_shared<const DrawingSheetCapture>(std::move(empty));
        failed->error = error.what();
        results_[id] = failed;
        QTimer::singleShot(0, this, &DrawingViewRenderCache::startNext);
        return;
    }
    running_ = true;
    pool_.start([this, capture, id] {
        auto result = std::make_shared<SheetEvaluation>();
        result->capture = capture;
        try {
            result->prepared = prepareDrawingSheet(*capture);
        } catch (const std::exception &error) {
            result->error = error.what();
        }
        QMetaObject::invokeMethod(
            this, [this, id, result] { finished(id, result); }, Qt::QueuedConnection);
    });
}
void DrawingViewRenderCache::finished(Id sheet, std::shared_ptr<const SheetEvaluation> result) {
    running_ = false;
    ++evaluations_;
    const bool live = result->capture->identity == doc_.identity() &&
                      result->capture->revision == doc_.revision() &&
                      doc_.drawingSheets().contains(sheet);
    if (live) {
        results_[sheet] = result;
        for (const auto &view : result->prepared.views) {
            statuses_.store(result->capture->identity, view.evaluation.status);
            statusRevisions_[view.record.id] = {result->capture->identity,
                                                view.evaluation.status.revision};
        }
        emit evaluated(sheet);
    }
    // A dropped result means the document moved on; refresh() has scheduled that work.
    if (!timer_.isActive())
        QTimer::singleShot(0, this, &DrawingViewRenderCache::startNext);
}
std::optional<DrawingViewStatus> DrawingViewRenderCache::status(Id view) const {
    const auto found = statusRevisions_.find(view);
    if (found == statusRevisions_.end())
        return std::nullopt;
    if (const auto *status = statuses_.find(found->second.first, view, found->second.second))
        return *status;
    return std::nullopt;
}
std::optional<std::string> DrawingViewRenderCache::sceneName(Id scene) const {
    const auto found = sceneNames_.find({doc_.identity(), scene});
    return found == sceneNames_.end() ? std::nullopt : std::optional(found->second);
}
QImage DrawingViewRenderCache::raster(Id view, const std::string &fingerprint) const {
    QImage best;
    for (const auto &entry : rasters_)
        if (entry.view == view && entry.fingerprint == fingerprint &&
            entry.image.width() > best.width())
            best = entry.image;
    return best;
}
bool DrawingViewRenderCache::hasRaster(Id view, const std::string &fingerprint,
                                       QSize pixels) const {
    return std::any_of(rasters_.begin(), rasters_.end(), [&](const Raster &entry) {
        return entry.view == view && entry.fingerprint == fingerprint &&
               entry.image.size() == pixels;
    });
}
void DrawingViewRenderCache::storeRaster(Id view, const std::string &fingerprint,
                                         const QImage &image) {
    if (image.isNull() || image.sizeInBytes() > rasterBudgetBytes)
        return;
    for (auto it = rasters_.begin(); it != rasters_.end(); ++it)
        if (it->view == view && it->fingerprint == fingerprint &&
            it->image.size() == image.size()) {
            rasterBytes_ -= it->image.sizeInBytes();
            rasters_.erase(it);
            break;
        }
    rasters_.push_front({view, fingerprint, image});
    rasterBytes_ += image.sizeInBytes();
    while (rasterBytes_ > rasterBudgetBytes && rasters_.size() > 1) {
        rasterBytes_ -= rasters_.back().image.sizeInBytes();
        rasters_.pop_back();
    }
}
} // namespace sketchy
