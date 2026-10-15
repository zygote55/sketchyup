// R091.f: model-view layout and editing on the page canvas (ADR 0164 §§4, 5; design §§3, 5, 7,
// 8). Placing (W), selecting with corner handles, moving (drag, M, arrows), cropping, snapping
// with hint text, Tab cycling, edit-view and the Measurements grammar. Every change goes
// through the committer as drawing_view.* commands, one labelled sheet-only entry; previews
// are local canvas state and Esc cancels them without an entry.
#include "app/sheet_canvas.hpp"
#include "app/sheet_text.hpp"
#include "automation/measurements.hpp"
#include "core/drawing_sheets.hpp"
#include "integrations/render_snapshot.hpp"
#include <QAccessible>
#include <QApplication>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <set>
namespace sketchy {
namespace {
constexpr double dragThresholdPixels = 4;
constexpr double snapPixels = 8;
constexpr double handleHitPixels = 7;
constexpr double handleSize = 8;
constexpr double minimumFrameMm = 10;
constexpr double gridMm = 5;
constexpr double placementBorderMm = 10;
int serial = 0;
QRectF rectOf(const DrawingFrame &frame) {
    return {frame.xMm, frame.yMm, frame.widthMm, frame.heightMm};
}
double tidy(double value, double step = .1) { return std::round(value / step) * step; }
DrawingFrame frameOf(const QRectF &rect, double step = .1) {
    // Pointer frames are kept to 0.1 mm on paper; typed values keep their own precision.
    return {tidy(rect.x(), step), tidy(rect.y(), step), tidy(rect.width(), step),
            tidy(rect.height(), step)};
}
QJsonObject frameJson(const DrawingFrame &frame) {
    return {{"xMm", frame.xMm},
            {"yMm", frame.yMm},
            {"widthMm", frame.widthMm},
            {"heightMm", frame.heightMm}};
}
QString key(const QString &kind, Id view) {
    return QString("%1:%2#%3").arg(kind).arg(view).arg(++serial);
}
bool keyOf(const QString &key, const QString &kind, Id view) {
    return key.startsWith(QString("%1:%2#").arg(kind).arg(view));
}
// Paper millimetres typed in the Measurements box: "176,112", "17.6cm;11.2cm" (";" in
// comma-decimal locales), each value a length whose bare unit is millimetres.
std::vector<double> lengthsMm(QString text) {
    const QLocale locale;
    const auto separator = locale.decimalPoint() == "," ? ';' : ',';
    const auto pieces = text.split(text.contains(';') ? ';' : separator, Qt::KeepEmptyParts);
    std::vector<double> values;
    for (const auto &piece : pieces) {
        if (piece.trimmed().isEmpty())
            throw std::runtime_error("Type a value on each side of the separator");
        values.push_back(std::round(parseLength(piece.trimmed(), "mm", locale) * 1e9) / 1e6);
    }
    return values;
}
QFont uiFont(const QWidget *widget, int pixels, bool bold = false, bool fixed = false) {
    QFont font = fixed ? QFontDatabase::systemFont(QFontDatabase::FixedFont) : widget->font();
    font.setPixelSize(pixels);
    if (bold)
        font.setWeight(QFont::DemiBold);
    return font;
}
QColor mixed(const QColor &a, const QColor &b, double t) {
    return QColor::fromRgbF(float(a.redF() + (b.redF() - a.redF()) * t),
                            float(a.greenF() + (b.greenF() - a.greenF()) * t),
                            float(a.blueF() + (b.blueF() - a.blueF()) * t));
}
} // namespace

DrawingViewRecord croppedView(const DrawingViewRecord &view, const DrawingFrame &frame) {
    auto result = view;
    const auto before = rectOf(view.frame).center(), after = rectOf(frame).center();
    result.frame = frame;
    // The scene target is drawn at the frame centre plus the offset; keep it on its paper point.
    result.offsetMm = {std::round((view.offsetMm[0] + before.x() - after.x()) * 1e6) / 1e6,
                       std::round((view.offsetMm[1] + before.y() - after.y()) * 1e6) / 1e6};
    return result;
}
SheetCanvas::Placement sheetPlacement(const Document &doc, Id scene, double scaleDenominator) {
    SheetCanvas::Placement placement;
    placement.scene = scene;
    placement.scaleDenominator = scaleDenominator;
    const auto found = doc.scenes().find(scene);
    if (found == doc.scenes().end())
        return placement;
    placement.name = QString::fromStdString(found->second->name);
    const auto &snapshot = found->second->snapshot;
    if (!snapshot.camera)
        return placement;
    const auto camera = renderSceneCamera(*snapshot.camera);
    if (!camera.orthographic) {
        placement.perspective = true;
        placement.naturalMm = {160, 120};
        return placement;
    }
    // The extent of the visible geometry at this scale, around the scene target.
    DrawingViewRecord probe;
    probe.frame = {0, 0, 100, 100};
    probe.scaleDenominator = scaleDenominator;
    const auto projection = drawingViewProjection(probe, camera);
    QRectF box;
    size_t points = 0;
    for (const auto &[id, body] : doc.bodies()) {
        bool visible = !body->hidden && !body->referenceImage;
        if (snapshot.visibility)
            if (const auto shown = snapshot.visibility->bodyVisible.find(id);
                shown != snapshot.visibility->bodyVisible.end())
                visible = shown->second && !body->referenceImage;
        if (!visible)
            continue;
        const auto world = doc.worldTransform(id);
        for (const auto &[vertex, point] : body->surface.vertices) {
            (void)vertex;
            if (++points > 200000)
                break;
            const auto sheet = projection.worldToSheet(world.point(point));
            const QPointF at(sheet.x - 50, sheet.y - 50);
            box = box.isNull() ? QRectF(at, QSizeF(1e-9, 1e-9)) : box.united(QRectF(at, at));
        }
    }
    if (box.isNull())
        return placement;
    placement.contentCentreMm = box.center();
    auto side = [](double extent) {
        return std::max(minimumFrameMm,
                        std::ceil((extent + 2 * placementBorderMm) / gridMm) * gridMm);
    };
    placement.naturalMm = {side(box.width()), side(box.height())};
    return placement;
}

const DrawingViewRecord *SheetCanvas::liveView(Id view) const {
    const auto found = doc_.drawingViews().find(view);
    return found == doc_.drawingViews().end() || found->second->sheet != sheet_
               ? nullptr
               : found->second.get();
}
DrawingViewRecord SheetCanvas::effectiveView(Id view) const {
    if (preview_ && preview_->id == view)
        return *preview_;
    const auto *live = liveView(view);
    return live ? *live : DrawingViewRecord{};
}
QPointF SheetCanvas::toPaper(QPointF widget) const {
    return paperToWidget().inverted().map(widget);
}
double SheetCanvas::tolerance() const { return snapPixels / pixelsPerMm_; }
Id SheetCanvas::viewAt(QPointF paperMm) const {
    const auto slack = 4 / pixelsPerMm_;
    Id hit = 0;
    for (const auto view : views()) {
        const auto frame = rectOf(effectiveView(view).frame);
        if (frame.adjusted(-slack, -slack, slack, slack).contains(paperMm))
            hit = view; // Later views paint on top.
    }
    return hit;
}
int SheetCanvas::handleAt(QPointF widget) const {
    if (!selectedView_ || editView_ || tool_ != Tool::Select)
        return -1;
    const auto frame = viewRect(selectedView_);
    const QPointF corners[] = {frame.topLeft(), frame.topRight(), frame.bottomRight(),
                               frame.bottomLeft()};
    for (int i = 0; i < 4; ++i)
        if (QLineF(corners[i], widget).length() <= handleHitPixels)
            return i;
    return -1;
}
QRectF SheetCanvas::clampFrame(QRectF frame) const {
    const auto *sheet = record();
    if (!sheet)
        return frame;
    const auto width = std::clamp(frame.width(), minimumFrameMm, sheet->paper.widthMm);
    const auto height = std::clamp(frame.height(), minimumFrameMm, sheet->paper.heightMm);
    frame.setSize({width, height});
    frame.moveLeft(std::clamp(frame.left(), 0., sheet->paper.widthMm - width));
    frame.moveTop(std::clamp(frame.top(), 0., sheet->paper.heightMm - height));
    return frame;
}

// ---- Snapping ------------------------------------------------------------------------
// Targets on one axis: the paper edges, the margins, and the edges and centres of the other
// views (alignment guides). The 5 mm grid quantizes when it is shown and nothing closer
// applies. The tolerance is a fixed number of logical pixels at any zoom (UX_DESIGN §4.4).
namespace {
struct Target {
    double value{};
    QString name;
};
struct AxisSnap {
    bool snapped{}, grid{};
    double shift{};
    QString name;
    double at{};
};
} // namespace
static std::vector<Target> snapTargets(const SheetCanvas &canvas, const Document &doc,
                                       const DrawingSheetRecord &sheet, bool x, Id exclude) {
    std::vector<Target> targets;
    const auto extent = x ? sheet.paper.widthMm : sheet.paper.heightMm;
    targets.push_back({0, x ? "the left edge of the paper" : "the top edge of the paper"});
    targets.push_back({extent, x ? "the right edge of the paper" : "the bottom edge of the paper"});
    if (sheet.marginMm > 0) {
        targets.push_back({sheet.marginMm, x ? "the left margin" : "the top margin"});
        targets.push_back({extent - sheet.marginMm, x ? "the right margin" : "the bottom margin"});
    }
    const auto views = canvas.views();
    for (size_t i = 0; i < views.size(); ++i) {
        if (views[i] == exclude)
            continue;
        const auto &frame = doc.drawingViews().at(views[i])->frame;
        const auto number = QString::number(i + 1);
        const auto low = x ? frame.xMm : frame.yMm;
        const auto size = x ? frame.widthMm : frame.heightMm;
        targets.push_back({low, (x ? "the left edge of view " : "the top edge of view ") + number});
        targets.push_back(
            {low + size, (x ? "the right edge of view " : "the bottom edge of view ") + number});
        targets.push_back(
            {low + size / 2, (x ? "the centre of view " : "the middle of view ") + number});
    }
    return targets;
}
static AxisSnap snapAxis(const std::vector<Target> &targets, const std::vector<double> &features,
                         double tolerance, bool grid) {
    AxisSnap best;
    double distance = tolerance;
    for (const auto feature : features)
        for (const auto &target : targets)
            if (const auto d = std::abs(target.value - feature); d <= distance) {
                distance = d;
                best = {true, false, target.value - feature, target.name, target.value};
            }
    if (!best.snapped && grid && !features.empty()) {
        const auto value = std::round(features.front() / gridMm) * gridMm;
        best = {true, true, value - features.front(), "the 5 mm grid", value};
    }
    return best;
}
static QString hintText(const AxisSnap &x, const AxisSnap &y) {
    QStringList parts;
    // Named targets first, then the grid: "the top edge of view 1 and the 5 mm grid".
    for (const auto *axis : {&y, &x})
        if (axis->snapped && !axis->grid && !parts.contains(axis->name))
            parts << axis->name;
    for (const auto *axis : {&y, &x})
        if (axis->snapped && axis->grid && !parts.contains(axis->name))
            parts << axis->name;
    return parts.isEmpty() ? QString{} : "Snapped to " + parts.join(" and ");
}
QPointF SheetCanvas::snapPoint(QPointF point, bool snapping) {
    snapHint_.clear();
    guides_.clear();
    const auto *sheet = record();
    if (!sheet || !snapping)
        return point;
    const auto exclude = preview_ ? preview_->id : Id{};
    const auto x =
        snapAxis(snapTargets(*this, doc_, *sheet, true, exclude), {point.x()}, tolerance(), grid_);
    const auto y =
        snapAxis(snapTargets(*this, doc_, *sheet, false, exclude), {point.y()}, tolerance(), grid_);
    snapHint_ = hintText(x, y);
    if (x.snapped && !x.grid)
        guides_.push_back({x.at, 0, x.at, sheet->paper.heightMm});
    if (y.snapped && !y.grid)
        guides_.push_back({0, y.at, sheet->paper.widthMm, y.at});
    return point + QPointF(x.shift, y.shift);
}
QRectF SheetCanvas::snapMove(QRectF frame, bool snapping) {
    snapHint_.clear();
    guides_.clear();
    const auto *sheet = record();
    if (!sheet || !snapping)
        return frame;
    const auto exclude = preview_ ? preview_->id : Id{};
    const auto x = snapAxis(snapTargets(*this, doc_, *sheet, true, exclude),
                            {frame.left(), frame.center().x(), frame.right()}, tolerance(), grid_);
    const auto y = snapAxis(snapTargets(*this, doc_, *sheet, false, exclude),
                            {frame.top(), frame.center().y(), frame.bottom()}, tolerance(), grid_);
    snapHint_ = hintText(x, y);
    if (x.snapped && !x.grid)
        guides_.push_back({x.at, 0, x.at, sheet->paper.heightMm});
    if (y.snapped && !y.grid)
        guides_.push_back({0, y.at, sheet->paper.widthMm, y.at});
    return frame.translated(x.shift, y.shift);
}

// ---- Frames being placed and cropped -------------------------------------------------
QRectF SheetCanvas::placeFrame(QPointF corner, bool proportional) const {
    const auto *sheet = record();
    if (sheet)
        corner = {std::clamp(corner.x(), 0., sheet->paper.widthMm),
                  std::clamp(corner.y(), 0., sheet->paper.heightMm)};
    auto delta = corner - anchorMm_;
    if (proportional && placement_.naturalMm.height() > 0 && delta.y() != 0 && delta.x() != 0) {
        // Shift keeps the view's proportion: the content's extent (4:3 for a perspective).
        const auto aspect = placement_.naturalMm.width() / placement_.naturalMm.height();
        auto width = std::abs(delta.x()), height = std::abs(delta.y());
        if (width / height > aspect)
            width = height * aspect;
        else
            height = width / aspect;
        delta = {std::copysign(width, delta.x()), std::copysign(height, delta.y())};
    }
    return QRectF(anchorMm_, anchorMm_ + delta).normalized();
}
QRectF SheetCanvas::cropFrame(QPointF corner) const {
    const auto *sheet = record();
    const auto *live = preview_ ? liveView(preview_->id) : nullptr;
    if (!sheet || !live)
        return {};
    const auto frame = rectOf(live->frame);
    const QPointF corners[] = {frame.topLeft(), frame.topRight(), frame.bottomRight(),
                               frame.bottomLeft()};
    const auto fixed = corners[(cropCorner_ + 2) % 4];
    corner = {std::clamp(corner.x(), 0., sheet->paper.widthMm),
              std::clamp(corner.y(), 0., sheet->paper.heightMm)};
    // The dragged corner stays on its side of the fixed one, at least 10 mm away.
    const bool right = cropCorner_ == 1 || cropCorner_ == 2;
    const bool bottom = cropCorner_ == 2 || cropCorner_ == 3;
    const auto x = right ? std::max(corner.x(), fixed.x() + minimumFrameMm)
                         : std::min(corner.x(), fixed.x() - minimumFrameMm);
    const auto y = bottom ? std::max(corner.y(), fixed.y() + minimumFrameMm)
                          : std::min(corner.y(), fixed.y() - minimumFrameMm);
    return clampFrame(QRectF(fixed, QPointF(x, y)).normalized());
}
std::optional<QRectF> SheetCanvas::previewFrame() const {
    if (tool_ == Tool::View && anchored_)
        return placeFrame(cornerMm_, false);
    if (preview_)
        return rectOf(preview_->frame);
    return std::nullopt;
}
void SheetCanvas::setPlacement(const Placement &placement) {
    placement_ = placement;
    update();
    emit stepChanged();
}
void SheetCanvas::clearPreview() {
    preview_.reset();
    snapHint_.clear();
    guides_.clear();
    update();
}
void SheetCanvas::cancelOperation() {
    const bool active = drag_ != Drag::None || anchored_ || preview_;
    if (drag_ == Drag::Pan || drag_ == Drag::Zoom)
        rasterTimer_.start();
    drag_ = Drag::None;
    anchored_ = false;
    dragMoved_ = false;
    clearPreview();
    if (active)
        emit stepChanged();
}

// ---- Commits ---------------------------------------------------------------------------
QJsonObject SheetCanvas::commit(const QJsonArray &commands, const QString &label,
                                const QString &amendKey) {
    if (!committer_)
        return {};
    return committer_(commands, label, amendKey);
}
bool SheetCanvas::commitView(const DrawingViewRecord &before, const DrawingViewRecord &after,
                             const QString &label, const QString &amendKey) {
    QJsonObject command{{"command", "drawing_view.update"}, {"view", QString::number(after.id)}};
    if (after.frame != before.frame)
        command["frame"] = frameJson(after.frame);
    if (after.offsetMm != before.offsetMm)
        command["offsetMm"] = QJsonArray{after.offsetMm[0], after.offsetMm[1]};
    if (after.scaleDenominator != before.scaleDenominator)
        command["scaleDenominator"] = after.scaleDenominator;
    if (command.size() == 2)
        return false; // Nothing changes: no entry.
    const auto result = commit({command}, label, amendKey);
    if (result.isEmpty())
        return false;
    revision_.key = amendKey;
    return true;
}
bool SheetCanvas::commitPlacement(QRectF frame, bool amend) {
    const auto *sheet = record();
    if (!sheet)
        return false;
    // Measurements re-entry right after a placement revises it: same entry, new frame.
    amend = amend && revision_.key.startsWith("place:") && !revision_.create.isEmpty();
    auto create = amend ? revision_.create : QJsonObject{};
    if (!amend) {
        if (!placement_.scene) {
            emit message("Choose a scene for the new view.");
            return false;
        }
        // View names are unique in the document: "North elevation", "North elevation 2"…
        std::set<std::string> names;
        for (const auto &[id, view] : doc_.drawingViews())
            names.insert(view->name);
        const auto base = placement_.name.isEmpty() ? QString("View") : placement_.name;
        auto name = base;
        for (int n = 2; names.contains(name.toStdString()); ++n)
            name = base + " " + QString::number(n);
        create = {{"command", "drawing_view.create"},
                  {"name", name},
                  {"sheet", QString::number(sheet_)},
                  {"source",
                   QJsonObject{{"kind", "scene"}, {"scene", QString::number(placement_.scene)}}},
                  {"scaleDenominator", placement_.scaleDenominator},
                  {"content", placement_.perspective ? "rasterAppearance" : "technicalLines"}};
        // The drawing starts centred in the frame.
        if (!placement_.perspective)
            create["offsetMm"] = QJsonArray{-tidy(placement_.contentCentreMm.x(), .001),
                                            -tidy(placement_.contentCentreMm.y(), .001)};
    }
    create["frame"] = frameJson(frameOf(frame, amend ? 1e-6 : .1));
    const auto amendKey = amend ? revision_.key : key("place", 0);
    const auto result = commit({create}, "Place view", amendKey);
    if (result.isEmpty())
        return false;
    anchored_ = false;
    drag_ = Drag::None;
    clearPreview();
    const auto created = result["createdDrawingViews"].toArray();
    const Id view = created.isEmpty() ? 0 : created.first().toString().toULongLong();
    setSelectedView(view);
    // Selecting ends revision; this placement stays revisable by typing a new size.
    revision_ = {};
    revision_.key = amendKey;
    revision_.create = create;
    emit message("Placed “" + create["name"].toString() + "” at " +
                 (placement_.perspective && !amend
                      ? QString("Not to scale")
                      : "1:" + QString::number(create["scaleDenominator"].toDouble(), 'g', 10)));
    emit stepChanged();
    return true;
}
bool SheetCanvas::placeView(std::optional<QSizeF> size) {
    const auto *sheet = record();
    if (!sheet) {
        emit message("Add a page before placing a view.");
        return false;
    }
    QRectF frame;
    if (anchored_ && !size) {
        frame = placeFrame(cornerMm_, false);
        if (frame.width() < minimumFrameMm || frame.height() < minimumFrameMm)
            frame = QRectF(anchorMm_, placement_.naturalMm);
    } else {
        auto extent = size.value_or(placement_.naturalMm);
        if (!size) {
            // The natural size, within the printable area.
            extent.setWidth(std::min(extent.width(), sheet->paper.widthMm - 2 * sheet->marginMm));
            extent.setHeight(
                std::min(extent.height(), sheet->paper.heightMm - 2 * sheet->marginMm));
        }
        QPointF at(sheet->marginMm, sheet->marginMm);
        if (anchored_) {
            // Typed after the first corner: from that corner towards the pointer.
            at = anchorMm_;
            if (cornerMm_.x() < anchorMm_.x())
                at.rx() -= extent.width();
            if (cornerMm_.y() < anchorMm_.y())
                at.ry() -= extent.height();
        } else if (hoverMm_ &&
                   QRectF(0, 0, sheet->paper.widthMm, sheet->paper.heightMm).contains(*hoverMm_))
            at = *hoverMm_;
        frame = QRectF(at, extent);
    }
    if (frame.width() < minimumFrameMm || frame.height() < minimumFrameMm) {
        emit message("A view frame must be at least 10 × 10 mm.");
        return false;
    }
    if (frame.width() > sheet->paper.widthMm + 1e-9 ||
        frame.height() > sheet->paper.heightMm + 1e-9) {
        emit message("The frame must fit on the paper (" + paperMillimetres(sheet->paper.widthMm) +
                     " × " + paperMillimetres(sheet->paper.heightMm) + " mm).");
        return false;
    }
    return commitPlacement(clampFrame(frame));
}
bool SheetCanvas::nudge(QPointF delta) {
    const auto *live = liveView(selectedView_);
    if (!live)
        return false;
    auto after = *live;
    after.frame = frameOf(clampFrame(rectOf(live->frame).translated(delta)), 1e-6);
    if (after.frame == live->frame) {
        emit message("The view is already at the edge of the paper.");
        return false;
    }
    // Consecutive nudges of the same view amend one "Nudge" entry (ADR 0164 §5).
    const auto amendKey =
        keyOf(revision_.key, "nudge", live->id) ? revision_.key : key("nudge", live->id);
    return commitView(*live, after, "Nudge", amendKey);
}
bool SheetCanvas::panContent(QPointF delta, const QString &amendKey) {
    const auto *live = liveView(editView_);
    if (!live)
        return false;
    auto after = *live;
    after.offsetMm = {std::round((live->offsetMm[0] + delta.x()) * 1e6) / 1e6,
                      std::round((live->offsetMm[1] + delta.y()) * 1e6) / 1e6};
    return commitView(*live, after, "Pan view", amendKey);
}
bool SheetCanvas::zoomCrop(double factor, std::optional<QPointF> about) {
    const auto *live = liveView(editView_);
    const auto *sheet = record();
    if (!live || !sheet || !(factor > 0))
        return false;
    const auto frame = rectOf(live->frame);
    const auto centre = about.value_or(frame.center());
    QRectF next(centre + (frame.topLeft() - centre) * factor,
                centre + (frame.bottomRight() - centre) * factor);
    // At least 10 mm, about the same point; never off the paper.
    if (next.width() < minimumFrameMm || next.height() < minimumFrameMm) {
        const auto grow = std::max(minimumFrameMm / next.width(), minimumFrameMm / next.height());
        next = QRectF(centre + (next.topLeft() - centre) * grow,
                      centre + (next.bottomRight() - centre) * grow);
    }
    next = next.intersected(QRectF(0, 0, sheet->paper.widthMm, sheet->paper.heightMm));
    if (next.width() < minimumFrameMm - 1e-9 || next.height() < minimumFrameMm - 1e-9)
        return false;
    const auto after = croppedView(*live, frameOf(next));
    const auto amendKey =
        keyOf(revision_.key, "zoom", live->id) ? revision_.key : key("zoom", live->id);
    return commitView(*live, after, "Crop view", amendKey);
}
void SheetCanvas::cycleSelection(bool backward) {
    const auto list = views();
    if (list.empty())
        return;
    auto at = std::find(list.begin(), list.end(), selectedView_);
    size_t index;
    if (at == list.end())
        index = backward ? list.size() - 1 : 0;
    else {
        const auto current = size_t(at - list.begin());
        index = backward ? (current + list.size() - 1) % list.size() : (current + 1) % list.size();
    }
    setSelectedView(list[index]);
    QAccessibleEvent event(this, QAccessible::Focus);
    event.setChild(int(index));
    QAccessible::updateAccessibility(&event);
}

// ---- Edit-view -------------------------------------------------------------------------
bool SheetCanvas::enterEditView() {
    const auto *live = liveView(selectedView_);
    if (!live)
        return false;
    const auto scene = doc_.scenes().find(live->source.scene);
    if (scene == doc_.scenes().end() || !scene->second->snapshot.camera) {
        emit message("This view has no scene to draw. Choose a scene for it first.");
        return false;
    }
    if (!scene->second->snapshot.camera->orthographic) {
        emit message("A perspective view is not to scale and fills its frame; crop it with the "
                     "corner handles.");
        return false;
    }
    if (const auto status = cache_.status(live->id);
        status && status->state == DrawingViewState::Failed) {
        emit message("This view could not be drawn, so it cannot be edited.");
        return false;
    }
    cancelOperation();
    endRevision();
    editView_ = live->id;
    banner_->show();
    layoutBanner();
    banner_->raise();
    announce();
    update();
    emit editViewChanged(editView_);
    emit stepChanged();
    return true;
}
void SheetCanvas::leaveEditView() {
    if (!editView_)
        return;
    if (drag_ == Drag::EditPan)
        cancelOperation();
    const bool bannerFocus = banner_->isAncestorOf(QApplication::focusWidget());
    editView_ = 0;
    endRevision();
    banner_->hide();
    if (bannerFocus)
        setFocus(Qt::OtherFocusReason);
    announce();
    update();
    emit editViewChanged(0);
    emit stepChanged();
}
void SheetCanvas::layoutBanner() {
    if (!banner_)
        return;
    // As wide as its sentence and buttons, within the canvas.
    const auto margins = banner_->layout()->contentsMargins();
    const auto natural =
        int(std::ceil(bannerText_->fontMetrics().horizontalAdvance(bannerText_->text()))) +
        saveScene_->sizeHint().width() + done_->sizeHint().width() +
        2 * banner_->layout()->spacing() + margins.left() + margins.right() + 4;
    const auto width = std::max(120, std::min(this->width() - 32, natural));
    banner_->setFixedWidth(width);
    const auto height = banner_->layout()->hasHeightForWidth()
                            ? banner_->layout()->heightForWidth(width)
                            : banner_->sizeHint().height();
    banner_->setFixedHeight(std::max(height, banner_->sizeHint().height()));
    banner_->move((this->width() - width) / 2, 14);
}
void SheetCanvas::themeBanner() {
    if (!banner_)
        return;
    // An inverse surface, so the banner reads as a context bar over the page in both themes.
    const auto other = themeColors(!colors_.dark);
    const auto border = mixed(colors_.ink, colors_.surface, .45);
    banner_->setStyleSheet(
        QString("#sheetEditBanner {background:%1;border-radius:6px;}"
                "#sheetEditBanner QLabel {color:%2;background:transparent;}"
                "#sheetEditBanner QPushButton {color:%2;background:transparent;border:1px solid "
                "%3;border-radius:6px;padding:2px 8px;}"
                "#sheetEditBanner QPushButton#sheetEditDone {background:%4;color:%5;border:1px "
                "solid %4;font-weight:600;padding:2px 10px;}"
                "#sheetEditBanner QPushButton:focus {border:2px solid %2;}")
            .arg(colors_.ink.name(), colors_.surface.name(), border.name(), other.accent.name(),
                 colors_.dark ? "#ffffff" : "#172019"));
    layoutBanner();
}

// ---- Pointer ---------------------------------------------------------------------------
void SheetCanvas::pressEditing(QMouseEvent *event) {
    if (!record())
        return;
    const auto point = toPaper(event->position());
    const bool snapping = !(event->modifiers() & Qt::AltModifier);
    dragMoved_ = false;
    if (editView_) {
        // Inside the frame a drag pans the drawing; a click outside closes the context.
        if (!viewRect(editView_).contains(event->position())) {
            leaveEditView();
            return;
        }
        preview_ = *liveView(editView_);
        drag_ = Drag::EditPan;
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    switch (tool_) {
    case Tool::View:
        if (anchored_) {
            // Click–move–click: the second click places.
            cornerMm_ = snapPoint(point, snapping);
            commitPlacement(placeFrame(cornerMm_, event->modifiers() & Qt::ShiftModifier));
            return;
        }
        endRevision();
        anchorMm_ = cornerMm_ = snapPoint(point, snapping);
        anchored_ = true;
        drag_ = Drag::Place;
        update();
        emit stepChanged();
        return;
    case Tool::Move:
        if (anchored_ && preview_) {
            // The second click drops the picked-up view.
            const auto *live = liveView(preview_->id);
            if (live) {
                const auto after = *preview_;
                const auto delta = rectOf(after.frame).topLeft() - rectOf(live->frame).topLeft();
                const auto before = *live;
                const auto amendKey = key("move", live->id);
                anchored_ = false;
                clearPreview();
                if (commitView(before, after, "Move view", amendKey)) {
                    revision_.base = before;
                    const auto length = std::hypot(delta.x(), delta.y());
                    revision_.direction = length > 0 ? delta / length : QPointF();
                }
            }
            emit stepChanged();
            return;
        }
        [[fallthrough]];
    case Tool::Select: {
        if (tool_ == Tool::Select) {
            for (const auto &[view, button] : chooseButtons_)
                if (button.contains(event->position())) {
                    setSelectedView(view);
                    emit chooseSceneRequested(view);
                    return;
                }
            if (const auto corner = handleAt(event->position()); corner >= 0) {
                preview_ = *liveView(selectedView_);
                cropCorner_ = corner;
                drag_ = Drag::Crop;
                return;
            }
        }
        const auto hit = viewAt(point);
        setSelectedView(hit);
        if (hit) {
            preview_ = *liveView(hit);
            anchorMm_ = point;
            drag_ = Drag::Move;
        }
        return;
    }
    case Tool::Pan:
    case Tool::Zoom:
        return;
    }
}
void SheetCanvas::moveEditing(QMouseEvent *event) {
    const auto point = toPaper(event->position());
    hoverMm_ = point;
    const bool snapping = !(event->modifiers() & Qt::AltModifier);
    if (drag_ != Drag::None && !dragMoved_ &&
        QLineF(event->position(), dragStart_).length() >= dragThresholdPixels)
        dragMoved_ = true;
    if (tool_ == Tool::View && anchored_ && !editView_) {
        cornerMm_ = snapPoint(point, snapping);
        update();
        emit stepChanged();
        return;
    }
    if (preview_ && ((drag_ == Drag::Move && dragMoved_) ||
                     (drag_ == Drag::None && anchored_ && tool_ == Tool::Move))) {
        const auto *live = liveView(preview_->id);
        if (!live)
            return;
        auto frame = rectOf(live->frame).translated(point - anchorMm_);
        frame = clampFrame(snapMove(clampFrame(frame), snapping));
        preview_->frame = frameOf(frame);
        update();
        emit stepChanged();
        return;
    }
    if (preview_ && drag_ == Drag::Crop && dragMoved_) {
        const auto *live = liveView(preview_->id);
        if (!live)
            return;
        const auto frame = cropFrame(snapPoint(point, snapping));
        preview_ = croppedView(*live, frameOf(frame));
        update();
        emit stepChanged();
        return;
    }
    if (preview_ && drag_ == Drag::EditPan && dragMoved_) {
        const auto *live = liveView(preview_->id);
        if (!live)
            return;
        const auto delta = point - toPaper(dragStart_);
        preview_->offsetMm = {tidy(live->offsetMm[0] + delta.x()),
                              tidy(live->offsetMm[1] + delta.y())};
        update();
        return;
    }
    if (drag_ == Drag::None && tool_ == Tool::Select && !editView_) {
        // Handles show which way they crop.
        const auto corner = handleAt(event->position());
        setCursor(corner == 0 || corner == 2   ? Qt::SizeFDiagCursor
                  : corner == 1 || corner == 3 ? Qt::SizeBDiagCursor
                                               : Qt::ArrowCursor);
    }
}
void SheetCanvas::releaseEditing(QMouseEvent *event) {
    const auto kind = drag_;
    drag_ = Drag::None;
    switch (kind) {
    case Drag::Place:
        // Press–drag–release places; a click without a drag anchors the first corner.
        if (dragMoved_)
            commitPlacement(placeFrame(cornerMm_, event->modifiers() & Qt::ShiftModifier));
        return;
    case Drag::Move: {
        const auto *live = preview_ ? liveView(preview_->id) : nullptr;
        if (!live) {
            clearPreview();
            return;
        }
        if (!dragMoved_) {
            // Move tool: a click picks the view up; the next click drops it.
            if (tool_ == Tool::Move) {
                anchored_ = true;
                emit stepChanged();
            } else
                clearPreview();
            return;
        }
        const auto before = *live, after = *preview_;
        const auto delta = rectOf(after.frame).topLeft() - rectOf(before.frame).topLeft();
        clearPreview();
        if (commitView(before, after, "Move view", key("move", before.id))) {
            // A typed distance revises this move along its direction.
            revision_.base = before;
            const auto length = std::hypot(delta.x(), delta.y());
            revision_.direction = length > 0 ? delta / length : QPointF();
        }
        emit stepChanged();
        return;
    }
    case Drag::Crop: {
        const auto *live = preview_ ? liveView(preview_->id) : nullptr;
        const auto after = preview_;
        clearPreview();
        if (live && dragMoved_ && after)
            commitView(*live, *after, "Crop view", key("crop", live->id));
        emit stepChanged();
        return;
    }
    case Drag::EditPan: {
        setCursor(Qt::ArrowCursor);
        const auto *live = preview_ ? liveView(preview_->id) : nullptr;
        const auto after = preview_;
        clearPreview();
        // One entry per gesture (ADR 0164 §5).
        if (live && dragMoved_ && after)
            commitView(*live, *after, "Pan view", key("pan", live->id));
        return;
    }
    default:
        return;
    }
}

// ---- Keys ----------------------------------------------------------------------------
bool SheetCanvas::keyEditing(QKeyEvent *event) {
    const auto key = event->key();
    const bool shift = event->modifiers() & Qt::ShiftModifier;
    const bool arrow =
        key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down;
    if (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
        return false;
    const double step = shift ? 10 : 1;
    const QPointF delta(key == Qt::Key_Left    ? -step
                        : key == Qt::Key_Right ? step
                                               : 0,
                        key == Qt::Key_Up     ? -step
                        : key == Qt::Key_Down ? step
                                              : 0);
    if (editView_) {
        if (arrow) {
            // Arrows pan the drawing inside the frame; consecutive steps amend one entry.
            const auto amendKey = keyOf(revision_.key, "panstep", editView_)
                                      ? revision_.key
                                      : ::sketchy::key("panstep", editView_);
            panContent(delta, amendKey);
            return true;
        }
        if (key == Qt::Key_Plus || key == Qt::Key_Equal || key == Qt::Key_Minus) {
            zoomCrop(key == Qt::Key_Minus ? 1.1 : 1 / 1.1);
            return true;
        }
        return false;
    }
    if (arrow && (tool_ == Tool::Select || tool_ == Tool::Move) && selectedView_ &&
        !operationActive()) {
        nudge(delta);
        return true;
    }
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        if (tool_ == Tool::View) {
            placeView();
            return true;
        }
        if (tool_ == Tool::Select && selectedView_ && !operationActive()) {
            enterEditView();
            return true;
        }
    }
    return false;
}

// ---- Measurements ----------------------------------------------------------------------
QString SheetCanvas::measurementLabel() const {
    if (tool_ == Tool::Zoom)
        return "Zoom";
    if (editView_ || tool_ == Tool::View)
        return "Size";
    if (tool_ == Tool::Select && selectedView_)
        return "Scale";
    if (tool_ == Tool::Move && selectedView_)
        return "Distance";
    return {};
}
QString SheetCanvas::measurementValue() const {
    const auto separator = QLocale().decimalPoint() == "," ? QString(";") : QString(",");
    if (tool_ == Tool::Zoom)
        return QString::number(qRound(zoom() * 100)) + "%";
    if (editView_) {
        const auto frame = effectiveView(editView_).frame;
        return paperMillimetres(frame.widthMm) + separator + paperMillimetres(frame.heightMm) +
               " mm";
    }
    if (tool_ == Tool::View) {
        if (!anchored_)
            return {};
        const auto frame = placeFrame(cornerMm_, false);
        return paperMillimetres(tidy(frame.width())) + separator +
               paperMillimetres(tidy(frame.height())) + " mm";
    }
    if (tool_ == Tool::Select && selectedView_) {
        const auto status = cache_.status(selectedView_);
        const auto view = effectiveView(selectedView_);
        return viewScaleText(view, status && status->notToScale);
    }
    return {};
}
bool SheetCanvas::measurements(const QString &input, QString &error) {
    if (tool_ == Tool::Zoom) {
        const auto match =
            QRegularExpression(R"(^\s*([0-9]+(?:[.,][0-9]+)?)\s*(%?)\s*$)").match(input);
        if (!match.hasMatch()) {
            error = "Type a zoom level such as 150% or 1.5";
            return false;
        }
        auto value = match.captured(1).replace(',', '.').toDouble();
        if (!match.captured(2).isEmpty())
            value /= 100;
        if (!(value >= .05 && value <= 16)) {
            error = "Zoom must be between 5% and 1600%";
            return false;
        }
        setZoom(value);
        return true;
    }
    const auto label = measurementLabel();
    if (label.isEmpty()) {
        error = tool_ == Tool::Select || tool_ == Tool::Move
                    ? "Select a view to type its scale, size or position"
                    : "This tool takes no typed values";
        return false;
    }
    auto text = input.trimmed();
    text.remove(QRegularExpression(R"(\s*mm\s*$)"));
    try {
        const auto *sheet = record();
        if (!sheet)
            throw std::runtime_error("Add a page first");
        auto requireFrame = [&](QRectF frame) {
            if (frame.width() < minimumFrameMm - 1e-9 || frame.height() < minimumFrameMm - 1e-9)
                throw std::runtime_error("A view frame must be at least 10 × 10 mm");
            if (frame.left() < -1e-9 || frame.top() < -1e-9 ||
                frame.right() > sheet->paper.widthMm + 1e-9 ||
                frame.bottom() > sheet->paper.heightMm + 1e-9)
                throw std::runtime_error(
                    "The frame must stay on the paper (" +
                    paperMillimetres(sheet->paper.widthMm).toStdString() + " × " +
                    paperMillimetres(sheet->paper.heightMm).toStdString() + " mm)");
        };
        // Size: w,h.
        if (label == "Size") {
            const auto values = lengthsMm(text);
            if (values.size() != 2)
                throw std::runtime_error("Type the frame size as width,height in millimetres");
            if (editView_) {
                const auto *live = liveView(editView_);
                const bool again = keyOf(revision_.key, "size", editView_);
                const auto base = again ? revision_.base : *live;
                const QRectF frame(base.frame.xMm, base.frame.yMm, values[0], values[1]);
                requireFrame(frame);
                const auto amendKey = again ? revision_.key : key("size", editView_);
                if (!commitView(*live, croppedView(base, frameOf(frame, 1e-6)), "Crop view",
                                amendKey))
                    throw std::runtime_error("The view already has this size");
                revision_.base = base;
                return true;
            }
            const QSizeF size(values[0], values[1]);
            if (size.width() < minimumFrameMm || size.height() < minimumFrameMm)
                throw std::runtime_error("A view frame must be at least 10 × 10 mm");
            if (revision_.key.startsWith("place:") && !anchored_) {
                // Re-entry after a placement: the same view, the new size.
                const auto frame = revision_.create["frame"].toObject();
                const QRectF revised(frame["xMm"].toDouble(), frame["yMm"].toDouble(), size.width(),
                                     size.height());
                requireFrame(revised);
                if (!commitPlacement(revised, true))
                    throw std::runtime_error("The view could not be resized");
                return true;
            }
            if (!placeView(size))
                throw std::runtime_error("The view could not be placed");
            return true;
        }
        const auto *live = liveView(selectedView_);
        if (!live)
            throw std::runtime_error("Select a view first");
        const bool absolute = text.startsWith('[');
        const bool relative = text.startsWith('<');
        if (absolute || relative) {
            if (!text.endsWith(absolute ? ']' : '>'))
                throw std::runtime_error("Coordinate brackets do not match");
            text = text.mid(1, text.size() - 2);
        }
        // Scale: 1:N, or N with the Select tool. The decimal mark follows the locale.
        const auto point = QRegularExpression::escape(QLocale().decimalPoint());
        const QRegularExpression ratio(R"(^1\s*:\s*([0-9]+(?:)" + point + R"([0-9]+)?)$)");
        const QRegularExpression bare(R"(^([0-9]+(?:)" + point + R"([0-9]+)?)$)");
        const auto ratioMatch = ratio.match(text), bareMatch = bare.match(text);
        if (!absolute && !relative &&
            (ratioMatch.hasMatch() || (label == "Scale" && bareMatch.hasMatch()))) {
            if (const auto status = cache_.status(live->id); status && status->notToScale)
                throw std::runtime_error("A perspective view is not to scale");
            bool ok = false;
            const auto value = QLocale().toDouble(
                (ratioMatch.hasMatch() ? ratioMatch : bareMatch).captured(1), &ok);
            if (!ok || !(value >= .001 && value <= 1e9))
                throw std::runtime_error("Type the scale as 1:N, with N from 0.001 to 1000000000");
            const bool again = keyOf(revision_.key, "scale", live->id);
            const auto base = again ? revision_.base : *live;
            auto after = base;
            after.scaleDenominator = value;
            const auto amendKey = again ? revision_.key : key("scale", live->id);
            if (!commitView(*live, after, "Change scale", amendKey))
                throw std::runtime_error("The view already has this scale");
            revision_.base = base;
            return true;
        }
        const auto values = lengthsMm(text);
        // Move: <dx,dy> or [x,y]; the Move tool also takes dx,dy and a distance.
        const bool moving = absolute || relative || (label == "Distance");
        if (!moving) {
            // Select: w,h sets the crop size, keeping the top-left corner and the drawing.
            if (values.size() != 2)
                throw std::runtime_error("Type 1:N for the scale, width,height for the crop, "
                                         "<dx,dy> or [x,y] to move");
            const bool again = keyOf(revision_.key, "size", live->id);
            const auto base = again ? revision_.base : *live;
            const QRectF frame(base.frame.xMm, base.frame.yMm, values[0], values[1]);
            requireFrame(frame);
            const auto amendKey = again ? revision_.key : key("size", live->id);
            if (!commitView(*live, croppedView(base, frameOf(frame, 1e-6)), "Crop view", amendKey))
                throw std::runtime_error("The view already has this size");
            revision_.base = base;
            return true;
        }
        const bool again = keyOf(revision_.key, "move", live->id);
        const auto base = again ? revision_.base : *live;
        QPointF delta;
        if (absolute) {
            if (values.size() != 2)
                throw std::runtime_error("Type the top-left corner as [x,y] in millimetres");
            delta = QPointF(values[0], values[1]) - rectOf(base.frame).topLeft();
        } else if (values.size() == 2)
            delta = {values[0], values[1]};
        else if (values.size() == 1 && !relative) {
            if (!again || revision_.direction.isNull())
                throw std::runtime_error("Drag the view first to type a distance, or type dx,dy");
            delta = revision_.direction * values[0];
        } else
            throw std::runtime_error("Type the move as dx,dy in millimetres");
        auto after = base;
        const auto frame = rectOf(base.frame).translated(delta);
        requireFrame(frame);
        after.frame = frameOf(frame, 1e-6);
        const auto amendKey = again ? revision_.key : key("move", live->id);
        const auto direction = revision_.direction;
        if (!commitView(*live, after, "Move view", amendKey))
            throw std::runtime_error("The view is already there");
        revision_.base = base;
        revision_.direction = again && values.size() == 1 ? direction : QPointF();
        if (!again || values.size() != 1) {
            const auto length = std::hypot(delta.x(), delta.y());
            revision_.direction = length > 0 ? delta / length : QPointF();
        }
        return true;
    } catch (const std::exception &failure) {
        error = QString::fromUtf8(failure.what());
        return false;
    }
}
QString SheetCanvas::toolName() const {
    if (editView_)
        return "Edit view";
    switch (tool_) {
    case Tool::Pan:
        return "Pan";
    case Tool::Zoom:
        return "Zoom";
    case Tool::View:
        return "Model view";
    case Tool::Move:
        return "Move";
    case Tool::Select:
        break;
    }
    return "Select";
}
QString SheetCanvas::instruction() const {
    if (editView_)
        return "Drag to pan the drawing · Wheel zooms the crop, the scale stays · Arrows pan "
               "1 mm · Shift+Arrows 10 mm · Esc leaves";
    switch (tool_) {
    case Tool::Pan:
        return "Drag to pan · Arrows pan · Esc returns to Select";
    case Tool::Zoom:
        return "Drag up to zoom in, down to zoom out · Type a zoom such as 150% · Esc returns "
               "to Select";
    case Tool::View:
        return "Drag the frame, or type width,height · Shift keeps the view's proportion · Alt "
               "ignores snapping · Esc cancels";
    case Tool::Move:
        return selectedView_ ? "Drag the view, or click it and click its new place · Type "
                               "dx,dy · Alt ignores snapping · Esc cancels"
                             : "Click a view to pick it up, then click its new place · Esc "
                               "returns to Select";
    case Tool::Select:
        break;
    }
    if (selectedView_)
        return "Drag a handle to crop · Arrows nudge 1 mm · Shift+Arrows 10 mm · Alt ignores "
               "snapping · U accepts a changed view";
    return "Click a view to select it · Wheel zooms · Middle drag pans · Shift+Z fits the page";
}

// ---- Painting ----------------------------------------------------------------------------
std::optional<PreparedDrawingSheet>
SheetCanvas::adjusted(const PreparedDrawingSheet &prepared) const {
    struct Change {
        QTransform content, frame, caption;
        QRectF to;
        bool whole{};
        DrawingViewRecord record;
    };
    std::map<Id, Change> changes;
    for (const auto &view : prepared.views) {
        if (!liveView(view.record.id))
            continue;
        const auto now = effectiveView(view.record.id);
        const auto &old = view.record;
        if (now.frame == old.frame && now.offsetMm == old.offsetMm &&
            now.scaleDenominator == old.scaleDenominator)
            continue;
        Change change;
        const auto from = rectOf(old.frame), to = rectOf(now.frame);
        const auto oldTarget = from.center() + QPointF(old.offsetMm[0], old.offsetMm[1]);
        const auto newTarget = to.center() + QPointF(now.offsetMm[0], now.offsetMm[1]);
        const auto k = old.scaleDenominator / now.scaleDenominator;
        change.content.translate(newTarget.x(), newTarget.y());
        change.content.scale(k, k);
        change.content.translate(-oldTarget.x(), -oldTarget.y());
        change.frame.translate(to.left(), to.top());
        change.frame.scale(to.width() / from.width(), to.height() / from.height());
        change.frame.translate(-from.left(), -from.top());
        change.caption =
            QTransform::fromTranslate(to.left() - from.left(), to.bottom() - from.bottom());
        change.to = to;
        const auto &status = view.evaluation.status;
        // Content that fills the frame (perspective, broken and failed views) follows it.
        change.whole = status.notToScale || status.fingerprint.empty() ||
                       status.state == DrawingViewState::Broken ||
                       status.state == DrawingViewState::Failed || !view.pathError.empty();
        change.record = now;
        changes[view.record.id] = change;
    }
    if (changes.empty())
        return std::nullopt;
    auto result = prepared;
    for (auto &view : result.views)
        if (const auto found = changes.find(view.record.id); found != changes.end())
            view.record = found->second.record;
    for (auto &primitive : result.primitives) {
        const auto found = changes.find(primitive.source);
        if (found == changes.end() || !primitive.source)
            continue;
        const auto &change = found->second;
        if (primitive.layer == DrawingSheetLayer::Caption) {
            primitive.path = change.caption.map(primitive.path);
            primitive.textBoxMm = change.caption.mapRect(primitive.textBoxMm);
            continue;
        }
        if (primitive.layer != DrawingSheetLayer::View)
            continue;
        const auto &transform =
            change.whole || primitive.placeholderFor ? change.frame : change.content;
        primitive.path = transform.map(primitive.path);
        primitive.textBoxMm = transform.mapRect(primitive.textBoxMm);
        if (primitive.rasterView)
            primitive.rasterRectMm =
                change.whole ? change.to : change.content.mapRect(primitive.rasterRectMm);
        primitive.clipMm = change.to;
    }
    return result;
}
void SheetCanvas::paintEditing(QPainter &painter) {
    const auto paper = themeColors(false);
    const auto stroke = paper.selectionStroke;
    const auto hintColour = stroke.darker(125);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    if (editView_) {
        // Everything outside the view dims; the view is outlined (design §7).
        const auto frame = viewRect(editView_);
        QPainterPath outside;
        outside.addRect(QRectF(rect()));
        QPainterPath inside;
        inside.addRect(frame);
        auto dim = colors_.pasteboard;
        dim.setAlphaF(.62f);
        painter.fillPath(outside.subtracted(inside), dim);
        painter.setPen(QPen(paper.accent, 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(frame.adjusted(-1, -1, 1, 1));
    } else if (selectedView_ && liveView(selectedView_)) {
        // Selection: blue-violet outline offset from the frame, with corner handles.
        const auto frame = viewRect(selectedView_);
        painter.setPen(QPen(stroke, 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(frame.adjusted(-3, -3, 3, 3));
        if (tool_ == Tool::Select) {
            painter.setPen(QPen(stroke, 1.5));
            painter.setBrush(Qt::white);
            for (const auto &corner :
                 {frame.topLeft(), frame.topRight(), frame.bottomRight(), frame.bottomLeft()}) {
                QRectF handle(0, 0, handleSize, handleSize);
                handle.moveCenter(corner);
                painter.drawRect(handle);
            }
        }
    }
    const auto transform = paperToWidget();
    // Alignment guides of the active snap.
    if (!guides_.empty() && (operationActive() || preview_)) {
        QPen guide(stroke, 1, Qt::DashLine);
        guide.setCosmetic(true);
        painter.setPen(guide);
        for (const auto &line : guides_)
            painter.drawLine(transform.map(line));
    }
    auto sizeLabel = [&](const QRectF &frame, double widthMm, double heightMm) {
        const auto text = paperMillimetres(tidy(widthMm)) + " × " +
                          paperMillimetres(tidy(heightMm)) + " mm on paper";
        const auto font = uiFont(this, 11, false, true);
        const QFontMetricsF metrics(font);
        QRectF pill(0, 0, metrics.horizontalAdvance(text) + 12, metrics.height() + 4);
        pill.moveCenter(frame.center());
        painter.setPen(Qt::NoPen);
        painter.setBrush(stroke);
        painter.drawRoundedRect(pill, 4, 4);
        painter.setFont(font);
        painter.setPen(Qt::white);
        painter.drawText(pill, Qt::AlignCenter, text);
    };
    auto hint = [&](const QRectF &frame, double below) {
        if (snapHint_.isEmpty())
            return;
        const auto font = uiFont(this, 10);
        painter.setFont(font);
        painter.setPen(hintColour);
        const QFontMetricsF metrics(font);
        painter.drawText(QPointF(frame.left(), frame.bottom() + below + metrics.ascent()),
                         snapHint_);
    };
    if (tool_ == Tool::View && anchored_ && !editView_) {
        // The frame being placed: dashed, with its size on paper and the first corner.
        const auto paperFrame = placeFrame(cornerMm_, false);
        const auto frame = transform.mapRect(paperFrame);
        QPen dashed(stroke, 1.5, Qt::DashLine);
        painter.setPen(dashed);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(frame);
        sizeLabel(frame, paperFrame.width(), paperFrame.height());
        painter.setPen(QPen(stroke, 1.5));
        painter.setBrush(Qt::white);
        painter.drawEllipse(transform.map(anchorMm_), 4.5, 4.5);
        hint(frame, 6);
    } else if (preview_ && (drag_ == Drag::Move || drag_ == Drag::Crop || anchored_) &&
               (dragMoved_ || anchored_)) {
        const auto frame = viewRect(preview_->id);
        if (drag_ == Drag::Crop)
            sizeLabel(frame, preview_->frame.widthMm, preview_->frame.heightMm);
        // Below the caption.
        hint(frame, 20);
    }
    painter.restore();
}
} // namespace sketchy
