#include "app/sheet_canvas.hpp"
#include "app/sheet_text.hpp"
#include "core/drawing_sheets.hpp"
#include <QAccessible>
#include <QAccessibleWidget>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <QWheelEvent>
#include <QWindow>
#include <algorithm>
#include <cmath>
#include <map>
namespace sketchy {
namespace {
constexpr double actualSizePixelsPerMm = 96 / 25.4;
constexpr double minimumZoom = .05, maximumZoom = 16;
constexpr int fitPadding = 24;
constexpr int rasterSideLimit = 2048;
constexpr double hitTolerancePixels = 4;
QColor mix(const QColor &a, const QColor &b, double t) {
    return QColor::fromRgbF(float(a.redF() + (b.redF() - a.redF()) * t),
                            float(a.greenF() + (b.greenF() - a.greenF()) * t),
                            float(a.blueF() + (b.blueF() - a.blueF()) * t));
}
// The "⟳" refresh icon of the Changed chip, in a 12-unit box.
QPainterPath refreshIcon() {
    QPainterPath path;
    path.arcMoveTo(QRectF(2, 2, 8, 8), 0);
    path.arcTo(QRectF(2, 2, 8, 8), 0, 315);
    path.moveTo(10, 2);
    path.lineTo(10, 4.5);
    path.lineTo(7.5, 4.5);
    return path;
}
class SheetEntryAccessible;
// The canvas: role Canvas, the page as its name, the selected view or item as its description,
// and one child per view and item in paint order (ADR 0164 §§1, 4).
class SheetCanvasAccessible final : public QAccessibleWidget {
  public:
    explicit SheetCanvasAccessible(QWidget *widget)
        : QAccessibleWidget(widget, QAccessible::Canvas) {}
    SheetCanvas *canvas() const { return static_cast<SheetCanvas *>(widget()); }
    int childCount() const override { return int(canvas()->entries().size()); }
    QAccessibleInterface *child(int index) const override;
    int indexOfChild(const QAccessibleInterface *child) const override;
    QAccessibleInterface *childAt(int x, int y) const override {
        const auto point = canvas()->mapFromGlobal(QPoint(x, y));
        const auto entries = canvas()->entries();
        for (int i = int(entries.size()) - 1; i >= 0; --i) {
            const auto &entry = entries[size_t(i)];
            const auto rect =
                entry.item ? canvas()->itemRect(entry.id) : canvas()->viewRect(entry.id);
            if (rect.contains(point))
                return child(i);
        }
        return nullptr;
    }
    QAccessibleInterface *focusChild() const override {
        const auto entries = canvas()->entries();
        const SheetCanvas::Entry selected{canvas()->selectedItem() != 0,
                                          canvas()->selectedItem() ? canvas()->selectedItem()
                                                                   : canvas()->selectedView()};
        const auto at = std::find(entries.begin(), entries.end(), selected);
        return canvas()->hasFocus() && selected.id && at != entries.end()
                   ? child(int(at - entries.begin()))
                   : nullptr;
    }
};
class SheetEntryAccessible final : public QAccessibleInterface {
  public:
    SheetEntryAccessible(SheetCanvas *canvas, SheetCanvas::Entry entry)
        : canvas_(canvas), entry_(entry) {}
    SheetCanvas::Entry entry() const { return entry_; }
    bool isValid() const override {
        if (!canvas_)
            return false;
        const auto entries = canvas_->entries();
        return std::find(entries.begin(), entries.end(), entry_) != entries.end();
    }
    QObject *object() const override { return nullptr; }
    QWindow *window() const override {
        return canvas_ ? canvas_->window()->windowHandle() : nullptr;
    }
    QAccessibleInterface *focusChild() const override { return nullptr; }
    QAccessibleInterface *childAt(int, int) const override { return nullptr; }
    QAccessibleInterface *parent() const override {
        return canvas_ ? QAccessible::queryAccessibleInterface(canvas_.data()) : nullptr;
    }
    QAccessibleInterface *child(int) const override { return nullptr; }
    int childCount() const override { return 0; }
    int indexOfChild(const QAccessibleInterface *) const override { return -1; }
    QString text(QAccessible::Text kind) const override {
        if (!isValid())
            return {};
        if (kind == QAccessible::Name)
            return entry_.item ? canvas_->describeItem(entry_.id)
                               : canvas_->describeView(entry_.id);
        return kind == QAccessible::Description && !entry_.item ? canvas_->brokenMessage(entry_.id)
                                                                : QString{};
    }
    void setText(QAccessible::Text, const QString &) override {}
    QRect rect() const override {
        if (!isValid())
            return {};
        const auto local =
            (entry_.item ? canvas_->itemRect(entry_.id) : canvas_->viewRect(entry_.id))
                .toAlignedRect();
        return QRect(canvas_->mapToGlobal(local.topLeft()), local.size());
    }
    QAccessible::Role role() const override { return QAccessible::Graphic; }
    QAccessible::State state() const override {
        QAccessible::State state;
        if (!isValid()) {
            state.invalid = true;
            return state;
        }
        state.selectable = true;
        state.selected = entry_.item ? canvas_->selectedItem() == entry_.id
                                     : canvas_->selectedView() == entry_.id;
        state.focused = state.selected && canvas_->hasFocus();
        return state;
    }

  private:
    QPointer<SheetCanvas> canvas_;
    SheetCanvas::Entry entry_;
};
// Child interfaces live as long as their view or item is on the page; ids are stable per canvas.
using ChildKey = std::tuple<const SheetCanvas *, bool, Id>;
std::map<ChildKey, QAccessible::Id> &childIds() {
    static std::map<ChildKey, QAccessible::Id> ids;
    return ids;
}
QAccessibleInterface *SheetCanvasAccessible::child(int index) const {
    const auto entries = canvas()->entries();
    if (index < 0 || index >= int(entries.size()))
        return nullptr;
    const auto &entry = entries[size_t(index)];
    const ChildKey key{canvas(), entry.item, entry.id};
    auto &ids = childIds();
    if (const auto found = ids.find(key); found != ids.end())
        if (auto *existing = QAccessible::accessibleInterface(found->second))
            return existing;
    auto *created = new SheetEntryAccessible(canvas(), entry);
    ids[key] = QAccessible::registerAccessibleInterface(created);
    return created;
}
int SheetCanvasAccessible::indexOfChild(const QAccessibleInterface *child) const {
    const auto *entry = dynamic_cast<const SheetEntryAccessible *>(child);
    if (!entry)
        return -1;
    const auto entries = canvas()->entries();
    const auto at = std::find(entries.begin(), entries.end(), entry->entry());
    return at == entries.end() ? -1 : int(at - entries.begin());
}
void dropChildren(const SheetCanvas *canvas, const std::vector<SheetCanvas::Entry> &keep) {
    auto &ids = childIds();
    for (auto it = ids.begin(); it != ids.end();) {
        const auto &[owner, item, id] = it->first;
        if (owner == canvas &&
            std::find(keep.begin(), keep.end(), SheetCanvas::Entry{item, id}) == keep.end()) {
            QAccessible::deleteAccessibleInterface(it->second);
            it = ids.erase(it);
        } else
            ++it;
    }
}
QAccessibleInterface *canvasFactory(const QString &className, QObject *object) {
    if (className == QLatin1String("sketchy::SheetCanvas") && object && object->isWidgetType())
        return new SheetCanvasAccessible(static_cast<QWidget *>(object));
    return nullptr;
}
} // namespace
SheetCanvas::SheetCanvas(const Document &doc, DrawingViewRenderCache &cache, QWidget *parent)
    : QWidget(parent), doc_(doc), cache_(cache) {
    static const bool factory = [] {
        QAccessible::installFactory(canvasFactory);
        return true;
    }();
    (void)factory;
    setObjectName("sheetCanvas");
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(160, 120);
    setAttribute(Qt::WA_OpaquePaintEvent);
    // Hover previews the Model view frame and a Move pick-up between clicks.
    setMouseTracking(true);
    rasterTimer_.setSingleShot(true);
    rasterTimer_.setInterval(120);
    connect(&rasterTimer_, &QTimer::timeout, this, &SheetCanvas::renderRasters);
    // The edit-view banner (design §7): real buttons, so Tab reaches them after the canvas.
    banner_ = new QWidget(this);
    banner_->setObjectName("sheetEditBanner");
    banner_->setAttribute(Qt::WA_StyledBackground);
    banner_->setAccessibleName("Editing a view");
    auto *bannerLayout = new QHBoxLayout(banner_);
    bannerLayout->setContentsMargins(12, 6, 12, 6);
    bannerLayout->setSpacing(10);
    bannerText_ = new QLabel("Pan and zoom inside the frame. Changes stay on this sheet; the "
                             "model and its scene don't change.");
    bannerText_->setObjectName("sheetEditBannerText");
    bannerText_->setWordWrap(true);
    bannerLayout->addWidget(bannerText_, 1);
    saveScene_ = new QPushButton("Save as new scene…");
    saveScene_->setObjectName("sheetSaveScene");
    saveScene_->setAutoDefault(false);
    saveScene_->setAccessibleName("Save as new scene…");
    saveScene_->setToolTip("Save this view's framing as a new scene in the model");
    bannerLayout->addWidget(saveScene_);
    done_ = new QPushButton("Done · Esc");
    done_->setObjectName("sheetEditDone");
    done_->setAutoDefault(false);
    done_->setAccessibleName("Done editing the view");
    done_->setToolTip("Leave edit-view (Esc)");
    bannerLayout->addWidget(done_);
    connect(saveScene_, &QPushButton::clicked, this, [this] {
        if (editView_)
            emit saveSceneRequested(editView_);
    });
    connect(done_, &QPushButton::clicked, this, [this] {
        leaveEditView();
        setFocus(Qt::OtherFocusReason);
    });
    banner_->hide();
    themeBanner();
    announce();
}
SheetCanvas::~SheetCanvas() { dropChildren(this, {}); }
const DrawingSheetRecord *SheetCanvas::record() const {
    const auto found = doc_.drawingSheets().find(sheet_);
    return found == doc_.drawingSheets().end() ? nullptr : found->second.get();
}
void SheetCanvas::setSheet(Id sheet) {
    if (sheet == sheet_ && (record() || !sheet))
        return;
    if (textEditing())
        endTextEdit();
    cancelOperation();
    leaveEditView();
    endRevision();
    sheet_ = sheet;
    selectedView_ = 0;
    dropItemSelection();
    fitPage();
    contentChanged();
    emit selectionChanged(0);
}
void SheetCanvas::setTheme(const ThemeColors &colors) {
    colors_ = colors;
    themeBanner();
    update();
}
void SheetCanvas::setGrid(bool shown) {
    grid_ = shown;
    update();
}
void SheetCanvas::setTool(Tool tool) {
    if (tool_ == tool)
        return;
    if (textEditing())
        endTextEdit();
    cancelOperation();
    leaveEditView();
    endRevision();
    tool_ = tool;
    drag_ = Drag::None;
    setCursor(tool == Tool::Pan                            ? Qt::OpenHandCursor
              : tool == Tool::Zoom                         ? Qt::SizeVerCursor
              : tool == Tool::Text                         ? Qt::IBeamCursor
              : tool == Tool::Select || tool == Tool::Move ? Qt::ArrowCursor
                                                           : Qt::CrossCursor);
    emit toolChanged(int(tool));
    emit stepChanged();
}
QRectF SheetCanvas::paperRect() const {
    const auto *sheet = record();
    if (!sheet)
        return {};
    return paperToWidget().mapRect(QRectF(0, 0, sheet->paper.widthMm, sheet->paper.heightMm));
}
QTransform SheetCanvas::paperToWidget() const {
    QTransform transform;
    transform.translate(origin_.x(), origin_.y());
    transform.scale(pixelsPerMm_, pixelsPerMm_);
    return transform;
}
void SheetCanvas::fitPage() {
    fitted_ = true;
    const auto *sheet = record();
    if (!sheet) {
        update();
        return;
    }
    const auto width = std::max(1., double(this->width() - 2 * fitPadding));
    const auto height = std::max(1., double(this->height() - 2 * fitPadding));
    pixelsPerMm_ =
        std::clamp(std::min(width / sheet->paper.widthMm, height / sheet->paper.heightMm),
                   minimumZoom * actualSizePixelsPerMm, maximumZoom * actualSizePixelsPerMm);
    origin_ = {(this->width() - sheet->paper.widthMm * pixelsPerMm_) / 2,
               (this->height() - sheet->paper.heightMm * pixelsPerMm_) / 2};
    rasterTimer_.start();
    placeEditor();
    update();
    emit zoomChanged(zoom());
}
double SheetCanvas::zoom() const { return pixelsPerMm_ / actualSizePixelsPerMm; }
void SheetCanvas::setPixelsPerMm(double value, QPointF anchor) {
    value =
        std::clamp(value, minimumZoom * actualSizePixelsPerMm, maximumZoom * actualSizePixelsPerMm);
    // Keep the paper point under the anchor fixed.
    const auto paper = (anchor - origin_) / pixelsPerMm_;
    pixelsPerMm_ = value;
    origin_ = anchor - paper * pixelsPerMm_;
    fitted_ = false;
    rasterTimer_.start();
    placeEditor();
    update();
    emit zoomChanged(zoom());
}
void SheetCanvas::setZoom(double zoom, std::optional<QPointF> anchor) {
    setPixelsPerMm(zoom * actualSizePixelsPerMm, anchor.value_or(QPointF(rect().center())));
}
void SheetCanvas::panBy(QPointF delta) {
    if (!record())
        return;
    origin_ += delta;
    // Keep at least a quarter of the paper, or 48 px, on screen.
    const auto paper = paperRect();
    const auto keepX = std::min(48., paper.width() / 4), keepY = std::min(48., paper.height() / 4);
    origin_.setX(std::clamp(origin_.x(), keepX - paper.width(), width() - keepX));
    origin_.setY(std::clamp(origin_.y(), keepY - paper.height(), height() - keepY));
    fitted_ = false;
    placeEditor();
    update();
}
void SheetCanvas::setSelectedView(Id view) {
    if (view &&
        (!doc_.drawingViews().contains(view) || doc_.drawingViews().at(view)->sheet != sheet_))
        view = 0;
    if (view == selectedView_ && !selectedItem_)
        return;
    if (textEditing())
        endTextEdit();
    cancelReattach();
    if (editView_)
        leaveEditView();
    endRevision();
    // One selection: a view clears the selected item.
    dropItemSelection();
    selectedView_ = view;
    update();
    announce();
    emit selectionChanged(view);
    emit stepChanged();
}
std::vector<Id> SheetCanvas::views() const {
    return record() ? drawingViewsOnSheet(doc_, sheet_) : std::vector<Id>{};
}
QRectF SheetCanvas::viewRect(Id view) const {
    const auto *live = liveView(view);
    if (!live)
        return {};
    const auto frame = effectiveView(view).frame;
    return paperToWidget().mapRect(QRectF(frame.xMm, frame.yMm, frame.widthMm, frame.heightMm));
}
QRectF SheetCanvas::chooseSceneButton(Id view) const {
    const auto found = chooseButtons_.find(view);
    return found == chooseButtons_.end() ? QRectF() : found->second;
}
QString SheetCanvas::pageName() const {
    const auto *sheet = record();
    if (!sheet)
        return "No page";
    const auto &paper = sheet->paper;
    const auto named = QString::fromStdString(drawingPaperName(paper));
    return "Page " + sheetTitle(*sheet) + ", " +
           (named.isEmpty()
                ? paperMillimetres(paper.widthMm) + " × " + paperMillimetres(paper.heightMm) + " mm"
                : named + (paper.widthMm > paper.heightMm ? " landscape" : " portrait"));
}
QString SheetCanvas::brokenMessage(Id view) const {
    const auto status = cache_.status(view);
    const auto found = doc_.drawingViews().find(view);
    if (!status || found == doc_.drawingViews().end())
        return {};
    if (status->state == DrawingViewState::Failed)
        return "Could not draw this view";
    if (status->state != DrawingViewState::Broken)
        return {};
    const auto sceneId = found->second->source.scene;
    QString scene;
    if (const auto live = doc_.scenes().find(sceneId); live != doc_.scenes().end())
        scene = QString::fromStdString(live->second->name);
    else if (const auto remembered = cache_.sceneName(sceneId))
        scene = QString::fromStdString(*remembered);
    const auto reason = status->reasons.empty() ? std::string{} : status->reasons.front();
    if (reason == "sceneMissing")
        // A file opened with the scene already gone has no name to show.
        return scene.isEmpty() ? QString("The scene for this view was deleted")
                               : "Scene “" + scene + "” was deleted";
    if (reason == "sceneHasNoCamera")
        return "Scene “" + scene + "” has no camera";
    return "Scene “" + scene + "” cannot be drawn: " + brokenReasonText(reason);
}
QString SheetCanvas::describeView(Id view) const {
    const auto found = doc_.drawingViews().find(view);
    if (found == doc_.drawingViews().end())
        return {};
    const auto views = this->views();
    const auto at = std::find(views.begin(), views.end(), view);
    const auto number = at == views.end() ? 0 : int(at - views.begin()) + 1;
    const auto status = cache_.status(view);
    QStringList parts{"View " + QString::number(number),
                      QString::fromStdString(found->second->name),
                      viewScaleText(*found->second, status && status->notToScale)};
    if (!status)
        parts << "Updating…";
    else
        parts << viewStateSentence(*status);
    return parts.join(" · ");
}
void SheetCanvas::announce() {
    setAccessibleName(pageName());
    const auto views = this->views();
    const auto items = this->items();
    QString counts = QString("%1 view%2").arg(views.size()).arg(views.size() == 1 ? "" : "s");
    if (!items.empty())
        counts += QString(" · %1 item%2").arg(items.size()).arg(items.size() == 1 ? "" : "s");
    setAccessibleDescription(editView_                        ? "Editing " + describeView(editView_)
                             : selectedItem_                  ? describeItem(selectedItem_)
                             : selectedView_                  ? describeView(selectedView_)
                             : views.empty() && items.empty() ? QString("No views on this page")
                                                              : counts + " · none selected");
}
void SheetCanvas::contentChanged() {
    if (fitted_)
        fitPage();
    if (preview_ && !liveView(preview_->id))
        cancelOperation();
    if (editView_ && !liveView(editView_))
        leaveEditView();
    if (selectedView_ && (!doc_.drawingViews().contains(selectedView_) ||
                          doc_.drawingViews().at(selectedView_)->sheet != sheet_)) {
        selectedView_ = 0;
        emit selectionChanged(0);
        emit stepChanged();
    }
    // R091.g: an item that is gone (deleted, undone) leaves the selection and its operations.
    if (previewItem_ && !liveItem(previewItem_->id))
        cancelOperation();
    if (reattach_.item && !liveItem(reattach_.item))
        cancelReattach();
    if (editor_ && editing_.item && !liveItem(editing_.item)) {
        editor_->removeEventFilter(this);
        editor_->hide();
        editor_->deleteLater();
        editor_ = nullptr;
        editing_ = {};
        setFocus(Qt::OtherFocusReason);
    }
    if (selectedItem_ && !liveItem(selectedItem_)) {
        dropItemSelection();
        emit stepChanged();
    }
    dropChildren(this, entries());
    announce();
    QAccessibleEvent event(this, QAccessible::NameChanged);
    QAccessible::updateAccessibility(&event);
    rasterTimer_.start();
    update();
}
void SheetCanvas::renderRasters() {
    const auto evaluation = cache_.sheet(sheet_);
    if (!renderer_ || !evaluation || !cache_.current(sheet_) || !evaluation->capture)
        return;
    const auto ratio = devicePixelRatioF();
    bool rendered = false;
    for (const auto &view : evaluation->prepared.views) {
        const auto &status = view.evaluation.status;
        if (!view.raster || status.fingerprint.empty() ||
            status.state == DrawingViewState::Broken || status.state == DrawingViewState::Failed)
            continue;
        // Screen pixels for the frame at the current zoom, capped for display.
        auto size =
            QSizeF(view.record.frame.widthMm, view.record.frame.heightMm) * pixelsPerMm_ * ratio;
        if (size.width() > rasterSideLimit || size.height() > rasterSideLimit)
            size.scale(rasterSideLimit, rasterSideLimit, Qt::KeepAspectRatio);
        const QSize pixels(std::max(16, qRound(size.width())), std::max(16, qRound(size.height())));
        if (cache_.hasRaster(view.record.id, status.fingerprint, pixels))
            continue;
        const auto input = std::find_if(
            evaluation->capture->views.begin(), evaluation->capture->views.end(),
            [&](const DrawingViewEvaluationInput &entry) { return entry.view == view.record.id; });
        if (input == evaluation->capture->views.end())
            continue;
        try {
            auto image = renderer_(*input, pixels);
            if (!image.isNull()) {
                cache_.storeRaster(view.record.id, status.fingerprint, image);
                rendered = true;
            }
        } catch (const std::exception &) {
            // The capture went stale or the viewport is busy; the next evaluation retries.
        }
    }
    if (rendered)
        update();
}
void SheetCanvas::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), colors_.pasteboard);
    const auto *sheet = record();
    paintedRasters_ = 0;
    if (!sheet) {
        painter.setPen(colors_.muted);
        painter.drawText(rect().adjusted(24, 24, -24, -24), Qt::AlignCenter | Qt::TextWordWrap,
                         "This model has no pages yet. Add a page to start a drawing set.");
    } else {
        const auto paper = paperRect();
        // Paper shadow: a tight contact shadow and a soft drop shadow.
        const QColor shadow = colors_.dark ? QColor(0, 0, 0) : QColor(38, 58, 50);
        painter.setPen(Qt::NoPen);
        for (int step = 12; step >= 1; --step) {
            auto colour = shadow;
            colour.setAlphaF(float((colors_.dark ? .45 : .12) / 12 * (13 - step) / 6.5));
            painter.setBrush(colour);
            painter.drawRoundedRect(
                paper.translated(0, 8).adjusted(-step * 2, -step * 2, step * 2, step * 2), step * 2,
                step * 2);
        }
        auto contact = shadow;
        contact.setAlphaF(colors_.dark ? .5f : .25f);
        painter.setBrush(contact);
        painter.drawRect(paper.translated(0, 1).adjusted(-1, -1, 1, 1));
        painter.fillRect(paper, colors_.paper);
        const auto transform = paperToWidget();
        if (grid_ && sheet->marginMm > 0 && 5 * pixelsPerMm_ >= 6) {
            // The 5 mm grid inside the page margin (screen only).
            const auto margin = sheet->marginMm;
            const QRectF area(margin, margin, sheet->paper.widthMm - 2 * margin,
                              sheet->paper.heightMm - 2 * margin);
            QPen pen(QColor("#eef1ea"));
            pen.setCosmetic(true);
            pen.setWidthF(1);
            painter.save();
            painter.setClipRect(transform.mapRect(area));
            painter.setPen(pen);
            for (double x = std::ceil(area.left() / 5) * 5; x <= area.right(); x += 5)
                painter.drawLine(transform.map(QLineF(x, area.top(), x, area.bottom())));
            for (double y = std::ceil(area.top() / 5) * 5; y <= area.bottom(); y += 5)
                painter.drawLine(transform.map(QLineF(area.left(), y, area.right(), y)));
            painter.restore();
        }
        painted_ = cache_.sheet(sheet_);
        chooseButtons_.clear();
        if (painted_ && painted_->error.empty()) {
            // The last evaluation, moved to the views' current frames and to a drag in
            // progress, so a committed or previewed change never jumps back.
            const auto moved = adjusted(painted_->prepared);
            const auto &shown = moved ? *moved : painted_->prepared;
            DrawingSheetPaintOptions options;
            options.paperToDevice = transform;
            for (const auto &view : shown.views)
                if (view.raster && !view.evaluation.status.fingerprint.empty()) {
                    auto image = cache_.raster(view.record.id, view.evaluation.status.fingerprint);
                    if (!image.isNull()) {
                        options.rasters[view.record.id] = image;
                        ++paintedRasters_;
                    }
                }
            painter.save();
            paintDrawingSheet(painter, shown, options);
            painter.restore();
            for (const auto &view : shown.views)
                if (doc_.drawingViews().contains(view.record.id))
                    paintDecorations(painter, view);
        } else if (sheet->marginMm > 0) {
            // Before the first evaluation: the printed border only, never an empty view.
            QPen pen(QColor("#263a32"));
            pen.setCosmetic(true);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(transform.mapRect(
                QRectF(sheet->marginMm, sheet->marginMm, sheet->paper.widthMm - 2 * sheet->marginMm,
                       sheet->paper.heightMm - 2 * sheet->marginMm)));
        }
        // Selection, handles, snapping guides and hints, the placement frame and the
        // edit-view surround (sheet_tools.cpp).
        paintEditing(painter);
    }
    if (hasFocus()) {
        QPen ring(colors_.accent, 2);
        painter.setPen(ring);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRectF(rect()).adjusted(1, 1, -1, -1));
    }
}
void SheetCanvas::paintDecorations(QPainter &painter, const PreparedDrawingView &view) {
    // Decorations are drawn on paper, which is white in both themes.
    const auto paper = themeColors(false);
    const auto &status = view.evaluation.status;
    const auto frame = viewRect(view.record.id);
    if (frame.isEmpty())
        return;
    painter.save();
    if (status.state == DrawingViewState::Stale) {
        // The amber "⟳ Changed" chip in the frame's top-right corner; it never prints.
        QFont font = this->font();
        font.setPixelSize(11);
        font.setWeight(QFont::DemiBold);
        painter.setFont(font);
        const QFontMetricsF metrics(font);
        const QString text = "Changed";
        const QSizeF size(6 + 11 + 4 + metrics.horizontalAdvance(text) + 6,
                          std::max(15., metrics.height() + 2));
        QRectF chip(frame.right() - 4 - size.width(), frame.top() + 4, size.width(), size.height());
        painter.setClipRect(frame);
        painter.setPen(QPen(paper.warningBorder, 1));
        painter.setBrush(paper.warningSurface);
        painter.drawRoundedRect(chip.adjusted(.5, .5, -.5, -.5), 4, 4);
        painter.save();
        painter.translate(chip.left() + 6, chip.center().y() - 5.5);
        painter.scale(11. / 12, 11. / 12);
        painter.setPen(QPen(paper.warning, 1.5 * 12 / 11, Qt::SolidLine, Qt::RoundCap));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(refreshIcon());
        painter.restore();
        painter.setPen(paper.warning);
        painter.drawText(chip.adjusted(6 + 11 + 4, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft,
                         text);
    } else if (status.state == DrawingViewState::Broken ||
               status.state == DrawingViewState::Failed) {
        // Red border, diagonal cross-hatch and a centred card; frame, scale and caption stay.
        painter.setClipRect(frame.adjusted(-1, -1, 1, 1));
        painter.fillRect(frame, paper.failureSurface);
        auto hatch = paper.failure;
        hatch.setAlphaF(.18f);
        painter.setPen(QPen(hatch, 1));
        const auto span = frame.width() + frame.height();
        for (double offset = 0; offset < span; offset += 9) {
            painter.drawLine(QPointF(frame.left() + offset, frame.top()),
                             QPointF(frame.left() + offset - frame.height(), frame.bottom()));
            painter.drawLine(QPointF(frame.right() - offset, frame.top()),
                             QPointF(frame.right() - offset + frame.height(), frame.bottom()));
        }
        painter.setPen(QPen(paper.failure, 1.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(frame);
        const auto message = brokenMessage(view.record.id);
        const auto detail = status.state == DrawingViewState::Failed
                                ? QString::fromStdString(status.detail)
                                : QString{};
        // Relinking (Choose scene…) applies to a scene problem, never to a failed capture.
        const bool choose = status.state == DrawingViewState::Broken;
        QFont strong = this->font();
        strong.setWeight(QFont::DemiBold);
        QFont small = this->font();
        small.setPixelSize(
            std::max(9, this->font().pixelSize() > 0 ? this->font().pixelSize() - 1 : 11));
        const QFontMetricsF strongMetrics(strong), smallMetrics(small);
        const auto maxWidth = std::max(40., frame.width() - 32);
        const auto messageRect = strongMetrics.boundingRect(
            QRectF(0, 0, maxWidth - 20, 1000), Qt::AlignHCenter | Qt::TextWordWrap, message);
        const auto detailRect =
            detail.isEmpty()
                ? QRectF()
                : smallMetrics.boundingRect(QRectF(0, 0, maxWidth - 20, 1000),
                                            Qt::AlignHCenter | Qt::TextWordWrap, detail);
        const QString button = "Choose scene…";
        const QSizeF buttonSize(smallMetrics.horizontalAdvance(button) + 16,
                                smallMetrics.height() + 4);
        const auto contentWidth =
            std::max({messageRect.width(), detailRect.width(), choose ? buttonSize.width() : 0.});
        const auto contentHeight = messageRect.height() +
                                   (detail.isEmpty() ? 0 : 6 + detailRect.height()) +
                                   (choose ? 6 + buttonSize.height() : 0);
        QRectF card(0, 0, std::min(maxWidth, contentWidth + 20), contentHeight + 16);
        card.moveCenter(frame.center());
        painter.setPen(QPen(mix(paper.failure, Qt::white, .62), 1));
        painter.setBrush(paper.paper);
        painter.drawRoundedRect(card, 6, 6);
        auto y = card.top() + 8;
        painter.setFont(strong);
        painter.setPen(paper.failure);
        painter.drawText(QRectF(card.left() + 10, y, card.width() - 20, messageRect.height()),
                         Qt::AlignHCenter | Qt::TextWordWrap, message);
        y += messageRect.height();
        painter.setFont(small);
        if (!detail.isEmpty()) {
            y += 6;
            painter.setPen(paper.muted);
            painter.drawText(QRectF(card.left() + 10, y, card.width() - 20, detailRect.height()),
                             Qt::AlignHCenter | Qt::TextWordWrap, detail);
            y += detailRect.height();
        }
        if (choose) {
            // Choose scene… relinks the view (R091.f); a click on it selects the view first.
            y += 6;
            QRectF buttonRect(0, y, buttonSize.width(), buttonSize.height());
            buttonRect.moveLeft(card.center().x() - buttonSize.width() / 2);
            painter.setPen(QPen(paper.border, 1));
            painter.setBrush(paper.paper);
            painter.drawRoundedRect(buttonRect, 6, 6);
            painter.setPen(paper.ink);
            painter.drawText(buttonRect, Qt::AlignCenter, button);
            chooseButtons_[view.record.id] = buttonRect;
        }
    }
    painter.restore();
}
void SheetCanvas::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    if (fitted_)
        fitPage();
    layoutBanner();
}
void SheetCanvas::wheelEvent(QWheelEvent *event) {
    if (!record())
        return;
    const auto steps = event->angleDelta().y() / 120.;
    if (steps == 0) {
        // Horizontal trackpad scrolling pans.
        panBy(QPointF(event->pixelDelta().x(), 0));
        event->accept();
        return;
    }
    if (editView_ && !(event->modifiers() & Qt::ControlModifier)) {
        // In edit-view the wheel zooms the view: the crop changes and the scale stays.
        zoomCrop(std::pow(1.1, -steps), toPaper(event->position()));
        event->accept();
        return;
    }
    setPixelsPerMm(pixelsPerMm_ * std::pow(1.15, steps), event->position());
    event->accept();
}
void SheetCanvas::mousePressEvent(QMouseEvent *event) {
    if (textEditing()) {
        // Decision 11: a click outside the in-place editor ends editing (one entry).
        endTextEdit();
        setFocus(Qt::MouseFocusReason);
        event->accept();
        return;
    }
    setFocus(Qt::MouseFocusReason);
    dragStart_ = dragLast_ = event->position();
    if (event->button() == Qt::MiddleButton ||
        (event->button() == Qt::LeftButton && tool_ == Tool::Pan)) {
        drag_ = Drag::Pan;
        setCursor(Qt::ClosedHandCursor);
    } else if (event->button() == Qt::LeftButton && tool_ == Tool::Zoom) {
        drag_ = Drag::Zoom;
        dragZoom_ = pixelsPerMm_;
    } else if (event->button() == Qt::LeftButton)
        pressEditing(event);
    event->accept();
}
void SheetCanvas::mouseMoveEvent(QMouseEvent *event) {
    if (drag_ == Drag::Pan) {
        panBy(event->position() - dragLast_);
    } else if (drag_ == Drag::Zoom) {
        // Drag up to zoom in, down to zoom out, about the press point.
        setPixelsPerMm(dragZoom_ * std::pow(1.01, dragStart_.y() - event->position().y()),
                       dragStart_);
    } else
        moveEditing(event);
    dragLast_ = event->position();
    event->accept();
}
void SheetCanvas::mouseReleaseEvent(QMouseEvent *event) {
    if (drag_ == Drag::Pan || drag_ == Drag::Zoom) {
        setCursor(tool_ == Tool::Pan    ? Qt::OpenHandCursor
                  : tool_ == Tool::Zoom ? Qt::SizeVerCursor
                  : tool_ == Tool::View ? Qt::CrossCursor
                                        : Qt::ArrowCursor);
        rasterTimer_.start();
        drag_ = Drag::None;
    } else
        releaseEditing(event);
    event->accept();
}
bool SheetCanvas::event(QEvent *event) {
    if (event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        // With Select, Tab and Shift+Tab cycle the page's views, as the viewport cycles
        // entities (ADR 0164 §4); F6 leaves the canvas. An empty page lets Tab move on.
        if ((key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab) &&
            !(key->modifiers() & (Qt::ControlModifier | Qt::AltModifier)) &&
            tool_ == Tool::Select && !editView_ && !entries().empty() && !operationActive()) {
            cycleSelection(key->key() == Qt::Key_Backtab ||
                           key->modifiers().testFlag(Qt::ShiftModifier));
            event->accept();
            return true;
        }
    }
    if (event->type() == QEvent::ShortcutOverride) {
        // In edit-view, + and - zoom the crop; they are the canvas's, not a shortcut's.
        auto *key = static_cast<QKeyEvent *>(event);
        if (editView_ && (key->key() == Qt::Key_Plus || key->key() == Qt::Key_Minus ||
                          key->key() == Qt::Key_Equal)) {
            event->accept();
            return true;
        }
    }
    return QWidget::event(event);
}
void SheetCanvas::keyPressEvent(QKeyEvent *event) {
    const auto key = event->key();
    const bool shift = event->modifiers() & Qt::ShiftModifier;
    if (keyEditing(event)) {
        event->accept();
        return;
    }
    if (key == Qt::Key_Escape) {
        // Back out one step: cancel a drag, then leave the tool, then leave edit-view, then
        // clear the selection (UX_DESIGN §4.1).
        if (operationActive())
            cancelOperation();
        else if (tool_ != Tool::Select)
            emit escapeToSelect();
        else if (editView_)
            leaveEditView();
        else
            setSelectedView(0);
        event->accept();
        return;
    }
    if (tool_ == Tool::Pan &&
        (key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down)) {
        const double step = shift ? 160 : 40;
        panBy(QPointF(key == Qt::Key_Left    ? step
                      : key == Qt::Key_Right ? -step
                                             : 0,
                      key == Qt::Key_Up     ? step
                      : key == Qt::Key_Down ? -step
                                            : 0));
        rasterTimer_.start();
        event->accept();
        return;
    }
    if (tool_ == Tool::Zoom &&
        (key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_Plus || key == Qt::Key_Minus)) {
        setPixelsPerMm(pixelsPerMm_ * (key == Qt::Key_Up || key == Qt::Key_Plus ? 1.25 : 1 / 1.25),
                       QPointF(rect().center()));
        event->accept();
        return;
    }
    if (!measurementLabel().isEmpty() && !event->text().isEmpty() &&
        !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier)) &&
        QRegularExpression(R"(^[0-9.,;:%<\[]$)").match(event->text()).hasMatch()) {
        // Typing starts the Measurements box, as in the model.
        emit measurementsRequested(event->text());
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}
void SheetCanvas::focusInEvent(QFocusEvent *event) {
    QWidget::focusInEvent(event);
    update();
}
void SheetCanvas::focusOutEvent(QFocusEvent *event) {
    QWidget::focusOutEvent(event);
    update();
}
} // namespace sketchy
