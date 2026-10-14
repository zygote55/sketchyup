#include "core/annotation_anchors.hpp"
#include "core/drawing_item_records.hpp"
#include "core/drawing_sheets.hpp"
#include "core/scenes.hpp"
#include "io/drawing_sheet_paint.hpp"
#include <QBuffer>
#include <QFontDatabase>
#include <QGuiApplication>
#include <algorithm>
#include <QPaintEngine>
#include <QPainter>
#include <QPdfWriter>
#include <QRegularExpression>
#include <cmath>
#include <iostream>
#include <string>
#include <thread>
using namespace sketchy;
// The sheet compositor (ADR 0164 §§1–2, R091.d). Checked structurally: the display
// list in paper millimetres, and a recording paint engine that captures what the
// painter actually receives in device units. No pixel comparison.
namespace {
void check(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}
bool near(double a, double b, double tolerance = 1e-6) { return std::abs(a - b) <= tolerance; }
bool near(QPointF a, QPointF b, double tolerance = 1e-6) {
    return near(a.x(), b.x(), tolerance) && near(a.y(), b.y(), tolerance);
}
bool near(const QRectF &a, const QRectF &b, double tolerance = 1e-6) {
    return near(a.topLeft(), b.topLeft(), tolerance) &&
           near(a.bottomRight(), b.bottomRight(), tolerance);
}

// Records each path, image and clip QPainter hands the engine, in device units.
struct Recorded {
    enum Kind { Path, Image } kind{Path};
    QPainterPath device; // Path mapped to device units.
    QRectF imageRect;    // Image target in device units.
    QRectF clip;         // Clip bounds in device units.
    QColor color;
    bool filled{};
};
class RecordingEngine final : public QPaintEngine {
  public:
    std::vector<Recorded> records;
    RecordingEngine() : QPaintEngine(QPaintEngine::AllFeatures) {}
    bool begin(QPaintDevice *) override { return true; }
    bool end() override { return true; }
    Type type() const override { return QPaintEngine::User; }
    void updateState(const QPaintEngineState &state) override {
        if (state.state() & DirtyTransform)
            transform_ = state.transform();
        if (state.state() & DirtyPen)
            pen_ = state.pen();
        if (state.state() & DirtyBrush)
            brush_ = state.brush();
        if (state.state() & DirtyClipPath)
            clip_ = transform_.map(state.clipPath()).boundingRect();
        if (state.state() & DirtyClipRegion)
            clip_ = transform_.mapRect(QRectF(state.clipRegion().boundingRect()));
    }
    void drawPath(const QPainterPath &path) override {
        const bool filled = brush_.style() != Qt::NoBrush;
        records.push_back({Recorded::Path, transform_.map(path), {}, clip_,
                           filled ? brush_.color() : pen_.color(), filled});
    }
    void drawPixmap(const QRectF &r, const QPixmap &, const QRectF &) override { image(r); }
    void drawImage(const QRectF &r, const QImage &, const QRectF &,
                   Qt::ImageConversionFlags) override {
        image(r);
    }

  private:
    void image(const QRectF &r) {
        records.push_back({Recorded::Image, {}, transform_.mapRect(r), clip_, {}, false});
    }
    QTransform transform_;
    QPen pen_;
    QBrush brush_;
    QRectF clip_;
};
// 10 device units per millimetre.
class RecordingDevice final : public QPaintDevice {
  public:
    mutable RecordingEngine engine;
    QPaintEngine *paintEngine() const override { return &engine; }

  protected:
    int metric(PaintDeviceMetric metric) const override {
        switch (metric) {
        case PdmWidth:
            return 4200;
        case PdmHeight:
            return 2970;
        case PdmWidthMM:
            return 420;
        case PdmHeightMM:
            return 297;
        case PdmDpiX:
        case PdmDpiY:
        case PdmPhysicalDpiX:
        case PdmPhysicalDpiY:
            return 254;
        case PdmDepth:
            return 32;
        case PdmNumColors:
            return INT_MAX;
        case PdmDevicePixelRatio:
            return 1;
        case PdmDevicePixelRatioScaled:
            return int(QPaintDevice::devicePixelRatioFScale());
        default:
            return 0;
        }
    }
};
std::vector<Recorded> record(const PreparedDrawingSheet &sheet, DrawingSheetPaintOptions options = {},
                             DrawingSheetPaintResult *result = nullptr) {
    RecordingDevice device;
    QPainter painter;
    check(painter.begin(&device), "Recording painter starts");
    const auto painted = paintDrawingSheet(painter, sheet, options);
    painter.end();
    if (result)
        *result = painted;
    return device.engine.records;
}

SceneSnapshot planScene(bool orthographic = true) {
    SceneSnapshot snapshot;
    snapshot.camera = SceneCamera{};
    snapshot.camera->target = {1, .5, 0};
    snapshot.camera->yaw = 0;
    snapshot.camera->pitch = 90;
    snapshot.camera->distance = 20;
    snapshot.camera->orthographic = orthographic;
    return snapshot;
}
DrawingViewRecord viewRecord(Id sheet, Id scene, std::string name, DrawingFrame frame) {
    DrawingViewRecord record;
    record.name = std::move(name);
    record.sheet = sheet;
    record.source = {DrawingViewSourceKind::Scene, scene};
    record.frame = frame;
    record.scaleDenominator = 50;
    return record;
}
Id vertexAt(const Document &doc, Id body, Vec3 point) {
    for (const auto &[id, p] : doc.bodies().at(body)->surface.vertices)
        if (length(p - point) < 1e-9)
            return id;
    throw std::runtime_error("No vertex at the point");
}
// A3 landscape, margin 10 mm. View 1: a 2 m x 1 m slab in plan at 1:50 in frame
// (20, 20, 200, 150), its scene target (1, 0.5) at the frame centre (120, 95). View
// 2: a perspective scene. View 3: a deleted scene (broken). Items: text, label,
// dimension, line, rectangle and title block, in that z order.
struct Fixture {
    Document doc;
    Id face{}, plan{}, sheet{}, view{}, perspective{}, broken{};
    Id text{}, label{}, dimension{}, line{}, rectangle{}, titleBlock{};
    const QRectF planFrame{20, 20, 200, 150};
    Fixture() {
        face = doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 1, 0}, {0, 1, 0}}}, "Slab");
        plan = createScene(doc, "Plan", planScene());
        sheet = createDrawingSheet(doc, "Ground floor", {420, 297}, "A-101", 10);
        view = createDrawingView(doc, viewRecord(sheet, plan, "Ground floor plan", {20, 20, 200, 150}));
        perspective = createDrawingView(
            doc, viewRecord(sheet, createScene(doc, "Eye", planScene(false)), "Perspective",
                            {240, 20, 150, 100}));
        const auto gone = createScene(doc, "South elevation", planScene());
        broken = createDrawingView(doc, viewRecord(sheet, gone, "South", {240, 140, 150, 60}));
        eraseScene(doc, gone);

        DrawingTextItem t;
        t.frame = {30, 200, 80, 20};
        t.text = "General notes";
        text = createDrawingItem(doc, sheet, t);
        DrawingLabelItem l;
        l.text = "Slab";
        l.textAt = {60, 180};
        l.target = {DrawingLabelTargetKind::Paper, {110, 95}, 0, {}, {}};
        label = createDrawingItem(doc, sheet, l);
        DrawingDimensionItem d;
        d.view = view;
        d.anchors = {vertexAnchor(doc, face, vertexAt(doc, face, {0, 0, 0})),
                     vertexAnchor(doc, face, vertexAt(doc, face, {2, 0, 0}))};
        d.orientation = DrawingDimensionOrientation::Horizontal;
        d.offsetMm = -10;
        d.lastMm = {DrawingPointMm{100, 105}, DrawingPointMm{140, 105}};
        dimension = createDrawingItem(doc, sheet, d);
        DrawingLineItem ln;
        ln.from = {30, 230};
        ln.to = {130, 230};
        ln.dash = DrawingStrokeDash::Dashed;
        line = createDrawingItem(doc, sheet, ln);
        DrawingRectangleItem r;
        r.frame = {25, 195, 110, 40};
        rectangle = createDrawingItem(doc, sheet, r);
        DrawingTitleBlockItem b;
        b.frame = {300, 220, 110, 70};
        b.fields = {"House", "Ground floor plan", "2026-10-14", "B"};
        titleBlock = createDrawingItem(doc, sheet, b);
    }
};
std::vector<const DrawingSheetPrimitive *> of(const PreparedDrawingSheet &s, Id source,
                                              DrawingSheetLayer layer) {
    std::vector<const DrawingSheetPrimitive *> result;
    for (const auto &p : s.primitives)
        if (p.source == source && p.layer == layer)
            result.push_back(&p);
    return result;
}
std::vector<std::pair<QPointF, QPointF>> segments(const QPainterPath &path) {
    std::vector<std::pair<QPointF, QPointF>> result;
    for (int i = 1; i < path.elementCount(); ++i)
        if (path.elementAt(i).isLineTo())
            result.push_back({path.elementAt(i - 1), path.elementAt(i)});
    return result;
}
bool hasSegment(const QPainterPath &path, QPointF a, QPointF b, double tolerance = 1e-6) {
    for (const auto &[p, q] : segments(path))
        if ((near(p, a, tolerance) && near(q, b, tolerance)) ||
            (near(p, b, tolerance) && near(q, a, tolerance)))
            return true;
    return false;
}
bool samePrimitives(const PreparedDrawingSheet &a, const PreparedDrawingSheet &b) {
    if (a.primitives.size() != b.primitives.size())
        return false;
    for (size_t i = 0; i < a.primitives.size(); ++i) {
        const auto &x = a.primitives[i], &y = b.primitives[i];
        if (x.layer != y.layer || x.source != y.source || x.path != y.path || x.color != y.color ||
            x.strokeMm != y.strokeMm || x.fill != y.fill || x.dashed != y.dashed ||
            x.clipMm != y.clipMm || x.text != y.text || x.textBoxMm != y.textBoxMm ||
            x.rasterView != y.rasterView || x.rasterRectMm != y.rasterRectMm ||
            x.placeholderFor != y.placeholderFor)
            return false;
    }
    return true;
}

void scaleAndPositions() {
    Fixture f;
    const auto sheet = prepareDrawingSheet(f.doc, f.sheet);
    // The 2 m bottom edge at 1:50 is 40 mm, at paper (100,105)-(140,105).
    const auto lines = of(sheet, f.view, DrawingSheetLayer::View);
    check(!lines.empty(), "Plan view paints");
    bool found = false;
    for (const auto *p : lines) {
        check(near(p->clipMm, f.planFrame), "View content is clipped to its frame");
        found = found || (!p->fill && hasSegment(p->path, {100, 105}, {140, 105}));
    }
    check(found, "2 m edge at 1:50 paints 40 mm at the projected position");
    // The recording engine receives the same line in device units (10 per mm), with
    // the frame as the clip.
    const auto recorded = record(sheet);
    bool device = false;
    for (const auto &r : recorded)
        if (r.kind == Recorded::Path && !r.filled &&
            hasSegment(r.device, {1000, 1050}, {1400, 1050}, 1e-3)) {
            device = true;
            check(near(r.clip, QRectF(200, 200, 2000, 1500), 1e-3), "Device clip is the frame");
        }
    check(device, "Painter receives the 40 mm line in device units");
    // Dimension: horizontal, offset -10 places the line below both points at y = 115;
    // its text is the measured value at the line's midpoint.
    const auto evaluation = evaluateDrawingItem(f.doc, f.dimension);
    check(evaluation.paperLengthMm && near(*evaluation.paperLengthMm, 40), "Dimension 40 mm");
    const auto dimension = of(sheet, f.dimension, DrawingSheetLayer::Item);
    check(dimension.size() == 3, "Dimension lines, text background and text");
    check(hasSegment(dimension[0]->path, {100, 115}, {140, 115}), "Dimension line at y = 115");
    check(hasSegment(dimension[0]->path, {100, 106}, {100, 116.5}), "Extension line with gap");
    check(dimension[2]->text == QString::fromStdString(evaluation.text) && !evaluation.text.empty(),
          "Dimension text is the evaluated value");
    check(near(dimension[2]->textBoxMm.center(), {120, 115}), "Dimension text centred on line");
    check(dimension[2]->path.boundingRect().intersects(dimension[2]->textBoxMm),
          "Glyphs lie in the text box");
    // Text and label positions, line and rectangle in paper millimetres.
    const auto text = of(sheet, f.text, DrawingSheetLayer::Item);
    check(text.size() == 1 && text[0]->text == "General notes" &&
              near(text[0]->textBoxMm.topLeft(), {30, 200}) && near(text[0]->textBoxMm.width(), 80),
          "Text box at its frame");
    const auto label = of(sheet, f.label, DrawingSheetLayer::Item);
    check(label.size() == 2 && label[1]->text == "Slab" &&
              near(label[1]->textBoxMm.topLeft(), {60, 180}),
          "Label text at textAt, painted over its leader");
    const auto tip = segments(label[0]->path).front().second;
    check(near(tip, {110, 95}), "Leader ends at the paper target");
    const auto line = of(sheet, f.line, DrawingSheetLayer::Item);
    check(line.size() == 1 && line[0]->dashed && hasSegment(line[0]->path, {30, 230}, {130, 230}) &&
              near(line[0]->strokeMm, .35),
          "Line item");
    const auto rectangle = of(sheet, f.rectangle, DrawingSheetLayer::Item);
    check(rectangle.size() == 1 && near(rectangle[0]->path.boundingRect(), {25, 195, 110, 40}),
          "Rectangle item");
    // Title block: automatic fields.
    QStringList rows;
    for (const auto *p : of(sheet, f.titleBlock, DrawingSheetLayer::Item))
        if (!p->text.isEmpty())
            rows << p->text;
    check(rows.contains("Sheet: A-101") && rows.contains("Page: 1 of 1") &&
              rows.contains("Scale: 1:50 @ A3") && rows.contains("Project: House") &&
              rows.contains("Revision: B"),
          "Title block automatic fields: " + rows.join(" | ").toStdString());
    // Margin frame.
    const auto margin = of(sheet, 0, DrawingSheetLayer::MarginFrame);
    check(margin.size() == 1 && near(margin[0]->path.boundingRect(), {10, 10, 400, 277}),
          "Margin frame");
    std::cout << "scale and positions: ok\n";
}

void orderAndCaptions() {
    Fixture f;
    auto sheet = prepareDrawingSheet(f.doc, f.sheet);
    // Paint order: margin frame, views (ID order), captions, items (z order).
    std::vector<std::pair<DrawingSheetLayer, Id>> order;
    for (const auto &p : sheet.primitives)
        if (order.empty() || order.back() != std::pair{p.layer, p.source})
            order.push_back({p.layer, p.source});
    const std::vector<std::pair<DrawingSheetLayer, Id>> expected{
        {DrawingSheetLayer::MarginFrame, 0},     {DrawingSheetLayer::View, f.view},
        {DrawingSheetLayer::View, f.perspective}, {DrawingSheetLayer::View, f.broken},
        {DrawingSheetLayer::Caption, f.view},     {DrawingSheetLayer::Caption, f.perspective},
        {DrawingSheetLayer::Caption, f.broken},   {DrawingSheetLayer::Item, f.text},
        {DrawingSheetLayer::Item, f.label},       {DrawingSheetLayer::Item, f.dimension},
        {DrawingSheetLayer::Item, f.line},        {DrawingSheetLayer::Item, f.rectangle},
        {DrawingSheetLayer::Item, f.titleBlock}};
    check(order == expected, "Margin, views, captions, then items in z order");
    const auto caption = of(sheet, f.view, DrawingSheetLayer::Caption);
    check(caption.size() == 1 && caption[0]->text == "1 · Ground floor plan 1:50" &&
              near(caption[0]->textBoxMm.topLeft(), {20, 172}),
          "Caption below the frame: " + caption[0]->text.toStdString());
    check(of(sheet, f.perspective, DrawingSheetLayer::Caption)[0]->text ==
              "2 · Perspective Not to scale",
          "Perspective caption");
    check(of(sheet, f.broken, DrawingSheetLayer::Caption)[0]->text == "3 · South 1:50",
          "Broken view keeps its caption");
    // Reordering items changes the paint order.
    reorderDrawingItems(f.doc, f.sheet,
                        {f.titleBlock, f.text, f.label, f.dimension, f.line, f.rectangle});
    sheet = prepareDrawingSheet(f.doc, f.sheet);
    std::vector<Id> items;
    for (const auto &p : sheet.primitives)
        if (p.layer == DrawingSheetLayer::Item && (items.empty() || items.back() != p.source))
            items.push_back(p.source);
    check(items.front() == f.titleBlock && items.back() == f.rectangle, "Items follow z order");
    // The recording engine sees primitives in the same order.
    DrawingSheetPaintResult result;
    const auto recorded = record(sheet, {}, &result);
    check(result.primitives == recorded.size(), "Every painted primitive reaches the engine");
    std::cout << "order and captions: ok\n";
}

void noEditorDecorations() {
    Fixture f;
    // The plan view is stale (never accepted); accepting it changes nothing painted.
    const auto stale = prepareDrawingSheet(f.doc, f.sheet);
    check(stale.views[0].evaluation.status.state == DrawingViewState::Stale, "Stale view");
    acceptDrawingViews(f.doc, {{f.view, {f.doc.revision(), stale.views[0].evaluation.status.fingerprint}}});
    const auto current = prepareDrawingSheet(f.doc, f.sheet);
    check(current.views[0].evaluation.status.state == DrawingViewState::Current, "Current view");
    check(samePrimitives(stale, current), "Stale and current views paint identically: no chip");
    for (const auto &p : stale.primitives)
        check(!p.text.contains("Changed") && !p.text.contains(QChar(0x27f3)),
              "No Changed chip text");
    std::cout << "no editor decorations: ok\n";
}

void brokenAndRaster() {
    Fixture f;
    const auto sheet = prepareDrawingSheet(f.doc, f.sheet);
    // Broken view: a labelled empty frame and nothing else.
    const auto broken = of(sheet, f.broken, DrawingSheetLayer::View);
    check(broken.size() == 2 && !broken[0]->fill &&
              near(broken[0]->path.boundingRect(), {240, 140, 150, 60}) &&
              broken[1]->text == "Scene was deleted" &&
              QRectF(240, 140, 150, 60).contains(broken[1]->textBoxMm.center()),
          "Broken view paints a labelled empty frame");
    // Perspective: a raster slot plus a placeholder painted only without an image.
    const auto perspective = of(sheet, f.perspective, DrawingSheetLayer::View);
    check(perspective.size() == 3 && perspective[0]->rasterView == f.perspective &&
              near(perspective[0]->rasterRectMm, {240, 20, 150, 100}) &&
              perspective[1]->placeholderFor == f.perspective &&
              perspective[2]->text.startsWith("Raster view not captured"),
          "Perspective view is a raster slot with a placeholder");
    DrawingSheetPaintResult result;
    auto recorded = record(sheet, {}, &result);
    check(result.rasterPlaceholders ==
              std::vector<DrawingSheetRasterPlaceholder>{{f.perspective, "rasterNotSupplied"}} &&
              result.rasterViews.empty() && result.brokenViews == std::vector<Id>{f.broken} &&
              result.brokenItems.empty(),
          "Placeholder and broken view reported");
    const auto withoutImage = recorded.size();
    DrawingSheetPaintOptions options;
    QImage image(300, 200, QImage::Format_ARGB32);
    image.fill(Qt::gray);
    options.rasters[f.perspective] = image;
    recorded = record(sheet, options, &result);
    check(result.rasterViews == std::vector<Id>{f.perspective} && result.rasterPlaceholders.empty(),
          "Supplied raster is painted");
    check(recorded.size() == withoutImage - 2 + 1, "Image replaces the placeholder frame and label");
    bool imageAt = false;
    for (const auto &r : recorded)
        if (r.kind == Recorded::Image)
            imageAt = near(r.imageRect, QRectF(2400, 200, 1500, 1000), 1e-3) &&
                      near(r.clip, QRectF(2400, 200, 1500, 1000), 1e-3);
    check(imageAt, "Raster fills and is clipped to the frame");
    options.rasters[f.perspective] = QImage();
    record(sheet, options, &result);
    check(result.rasterPlaceholders ==
              std::vector<DrawingSheetRasterPlaceholder>{{f.perspective, "rasterImageInvalid"}},
          "Null image is reported");
    // A raster-appearance view at scale is a raster slot too.
    auto rendered = viewRecord(f.sheet, f.plan, "Rendered", {20, 190, 60, 40});
    rendered.content = DrawingViewContent::RasterAppearance;
    const auto renderedView = createDrawingView(f.doc, rendered);
    const auto again = prepareDrawingSheet(f.doc, f.sheet);
    const auto slot = of(again, renderedView, DrawingSheetLayer::View);
    check(!slot.empty() && slot[0]->rasterView == renderedView &&
              of(again, renderedView, DrawingSheetLayer::Caption)[0]->text == "4 · Rendered 1:50",
          "Raster appearance view keeps its scale caption");
    // A broken dimension paints red crosses at lastMm and "[Missing reference]".
    f.doc.erase(f.face);
    const auto erased = prepareDrawingSheet(f.doc, f.sheet);
    const auto dimension = of(erased, f.dimension, DrawingSheetLayer::Item);
    check(dimension.size() == 3 && dimension[0]->color == QColor(0xbc, 0x43, 0x43) &&
              dimension[2]->text.startsWith("[Missing reference]") &&
              near(dimension[2]->textBoxMm.center(), {120, 105}),
          "Broken dimension at lastMm");
    check(record(erased, {}, &result).size() > 0 &&
              result.brokenItems == std::vector<Id>{f.dimension},
          "Broken item reported");
    std::cout << "broken and raster: ok\n";
}

void fonts() {
    Fixture f;
    DrawingLabelItem l;
    l.text = "Missing face";
    l.font = {"No Such Family 9f3c", 12};
    l.leader = false;
    l.textAt = {200, 250};
    const auto item = createDrawingItem(f.doc, f.sheet, l);
    const auto sheet = prepareDrawingSheet(f.doc, f.sheet);
    const auto missing = std::find_if(
        sheet.fontSubstitutions.begin(), sheet.fontSubstitutions.end(),
        [](const auto &s) { return s.requested == "No Such Family 9f3c"; });
    check(missing != sheet.fontSubstitutions.end() && missing->items == std::vector<Id>{item} &&
              !missing->used.empty() && missing->used != missing->requested,
          "Substitution reported");
    // With the R078 face installed (as in CI) it is the only substitution and the
    // replacement; without it, every item falls back to the platform font.
    if (QFontDatabase::hasFamily("DejaVu Sans"))
        check(sheet.fontSubstitutions.size() == 1 && missing->used == "DejaVu Sans",
              "DejaVu Sans is the fallback");
    else
        check(sheet.fontSubstitutions.size() == 2 &&
                  sheet.fontSubstitutions[0].requested == "DejaVu Sans" &&
                  sheet.fontSubstitutions[0].used == missing->used,
              "Platform font fallback is reported");
    DrawingSheetPaintResult result;
    record(sheet, {}, &result);
    check(result.fontSubstitutions == sheet.fontSubstitutions, "Paint result carries the report");
    // Deterministic: the same substitute and the same outlines every time.
    check(samePrimitives(sheet, prepareDrawingSheet(f.doc, f.sheet)), "Deterministic fallback");
    // Point size sets the text height: 12 pt text is 1.2x as tall as 10 pt.
    const auto box = of(sheet, item, DrawingSheetLayer::Item)[0]->textBoxMm;
    const auto ten = of(sheet, f.label, DrawingSheetLayer::Item)[1]->textBoxMm;
    check(near(box.height() / ten.height(), 1.2, .05), "Point size scales text");
    std::cout << "fonts: substituted with " << missing->used << ": ok\n";
}

void workerMatchesSingleThread() {
    Fixture f;
    const auto single = prepareDrawingSheet(f.doc, f.sheet);
    auto capture = captureDrawingSheet(f.doc, f.sheet);
    // The live document changes before the worker runs; the capture is isolated.
    f.doc.move(f.face, {.5, 0, 0});
    PreparedDrawingSheet worker;
    std::thread thread([&] { worker = prepareDrawingSheet(capture); });
    thread.join();
    check(worker.revision == single.revision && samePrimitives(worker, single),
          "Worker display list equals the single-thread path");
    for (size_t i = 0; i < single.views.size(); ++i)
        check(worker.views[i].evaluation.status == single.views[i].evaluation.status,
              "Worker view status and fingerprint equal");
    check(!samePrimitives(prepareDrawingSheet(f.doc, f.sheet), single), "The live edit differs");
    std::cout << "worker matches single thread: ok\n";
}

void pdfSmoke() {
    Fixture f;
    const auto sheet = prepareDrawingSheet(f.doc, f.sheet);
    QByteArray bytes;
    {
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        QPdfWriter pdf(&buffer);
        pdf.setResolution(1200);
        pdf.setPageLayout(QPageLayout(QPageSize(QPageSize::A3), QPageLayout::Landscape, QMarginsF{},
                                      QPageLayout::Millimeter));
        QPainter painter;
        check(painter.begin(&pdf), "PDF painter starts");
        const auto result = paintDrawingSheet(painter, sheet);
        check(painter.end(), "PDF painter finishes");
        check(result.primitives > 0, "PDF primitives painted");
    }
    check(bytes.startsWith("%PDF-") && bytes.trimmed().endsWith("%%EOF") && bytes.size() > 1000,
          "Valid nonempty PDF");
    // A3 landscape is 420 x 297 mm = 1190.55 x 841.89 pt (Qt writes whole points).
    const QRegularExpression box(R"(/MediaBox\s*\[\s*0\s+0\s+([0-9.]+)\s+([0-9.]+)\s*\])");
    const auto match = box.match(QString::fromLatin1(bytes));
    check(match.hasMatch() && near(match.captured(1).toDouble(), 1190.55, 1) &&
              near(match.captured(2).toDouble(), 841.89, 1),
          "PDF page is A3 landscape");
    std::cout << "pdf smoke (" << bytes.size() << " bytes): ok\n";
}
} // namespace

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    try {
        scaleAndPositions();
        orderAndCaptions();
        noEditorDecorations();
        brokenAndRaster();
        fonts();
        workerMatchesSingleThread();
        pdfSmoke();
    } catch (const std::exception &error) {
        std::cerr << "drawing_sheet_paint_tests: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
