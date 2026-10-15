#include "io/drawing_sheet_paint.hpp"
#include "core/drawing_sheets.hpp"
#include "io/measured_export.hpp"
#include <QCoreApplication>
#include <QFont>
#include <QFontDatabase>
#include <QGlyphRun>
#include <QGuiApplication>
#include <QPaintDevice>
#include <QPainter>
#include <QRawFont>
#include <QTextLayout>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
namespace sketchy {
namespace {
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
// The failure red of ADR 0164 (theme token "failure"); broken items print in it.
const QColor failureColor{0xbc, 0x43, 0x43};
const QColor placeholderColor{0x80, 0x80, 0x80};
constexpr double millimetresPerPoint = 25.4 / 72;
constexpr double captionGapMm = 2, captionSizePt = 10;
constexpr double annotationStrokeMm = .18, frameStrokeMm = .25, marginStrokeMm = .35;
constexpr double arrowLengthMm = 7 * 25.4 / 96, arrowHalfWidthMm = 3 * 25.4 / 96;
constexpr double extensionGapMm = 1, extensionOvershootMm = 1.5, crossHalfMm = 4 * 25.4 / 96;
// Dimension text sits this far above its line, turned to read along it (R091.g design).
constexpr double dimensionTextGapMm = 1;
// Title-block cells: padding and the project row's larger, bold type.
constexpr double cellPaddingMm = 1.5, projectScale = 1.3;
const char *const fallbackFamily = "DejaVu Sans";

std::string formatDenominator(double denominator) {
    if (denominator == std::round(denominator) && std::abs(denominator) < 1e15)
        return std::to_string(static_cast<long long>(denominator));
    return QString::number(denominator, 'g', 12).toStdString();
}
QPointF qpoint(const DrawingPointMm &p) { return {p[0], p[1]}; }

// Deterministic family resolution with a record of every substitution.
class FontResolver {
  public:
    explicit FontResolver(std::vector<DrawingFontSubstitution> &substitutions)
        : substitutions_(substitutions) {}
    QString family(const std::string &requested, Id item) {
        auto it = cache_.find(requested);
        if (it == cache_.end()) {
            const auto name = QString::fromStdString(requested);
            QString used;
            if (QFontDatabase::hasFamily(name))
                used = name;
            else if (QFontDatabase::hasFamily(fallbackFamily))
                used = fallbackFamily;
            else
                used = QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
            it = cache_.emplace(requested, used).first;
        }
        const auto used = it->second.toStdString();
        if (used != requested) {
            auto entry = std::find_if(substitutions_.begin(), substitutions_.end(),
                                      [&](const auto &s) { return s.requested == requested; });
            if (entry == substitutions_.end())
                entry = substitutions_.insert(substitutions_.end(), {requested, used, {}});
            if (std::find(entry->items.begin(), entry->items.end(), item) == entry->items.end())
                entry->items.push_back(item);
        }
        return it->second;
    }

  private:
    std::map<std::string, QString> cache_;
    std::vector<DrawingFontSubstitution> &substitutions_;
};

// Text shaped into glyph outlines in millimetres, origin at the box's top-left.
// Outlines rather than device text keep the result identical on every device and
// independent of its DPI, as R078's export does.
struct ShapedText {
    QPainterPath path;
    QSizeF sizeMm;
    qsizetype missingGlyphs{};
    QList<QGlyphRun> glyphs; // In layout units; unitTransform maps them to millimetres.
    QTransform unitTransform;
};
ShapedText shapeText(const QString &input, const QString &family, double sizePt,
                     std::optional<double> wrapMm, Qt::Alignment align, bool bold = false) {
    constexpr int reference = 100; // Layout units per em.
    require(std::isfinite(sizePt) && sizePt > 0, "Text size must be positive");
    const double unit = sizePt * millimetresPerPoint / reference;
    QFont font(family);
    font.setPixelSize(reference);
    font.setHintingPreference(QFont::PreferNoHinting);
    font.setStyleStrategy(QFont::PreferOutline);
    // One glyph per character: no contextual alternates or ligatures, so text written to
    // a PDF as real text maps back to its characters (R092: selectable and searchable).
    // Some faces (Inter, Adwaita Sans) otherwise swap ':' and '-' between digits for
    // glyphs outside their character map.
    font.setFeature(QFont::Tag("calt"), 0);
    font.setFeature(QFont::Tag("liga"), 0);
    font.setFeature(QFont::Tag("clig"), 0);
    if (bold)
        font.setWeight(QFont::Bold);
    auto text = input;
    text.replace('\n', QChar::LineSeparator);
    QTextLayout layout(text, font);
    QTextOption option;
    option.setWrapMode(wrapMm ? QTextOption::WrapAtWordBoundaryOrAnywhere : QTextOption::NoWrap);
    option.setAlignment(wrapMm ? align : Qt::AlignLeft);
    option.setUseDesignMetrics(true);
    layout.setTextOption(option);
    const double width = wrapMm ? std::max(*wrapMm, 0.) / unit : 1e7;
    layout.beginLayout();
    double y = 0, natural = 0;
    int count = 0;
    while (true) {
        auto line = layout.createLine();
        if (!line.isValid())
            break;
        require(++count <= 1024, "Text exceeds 1024 lines");
        line.setLineWidth(width);
        line.setPosition({0, y});
        y += line.height();
        natural = std::max(natural, line.naturalTextWidth());
    }
    layout.endLayout();
    ShapedText result;
    QPainterPath glyphs;
    for (const auto &run : layout.glyphRuns()) {
        const auto indexes = run.glyphIndexes();
        const auto positions = run.positions();
        const auto raw = run.rawFont();
        for (qsizetype i = 0; i < indexes.size(); ++i) {
            if (!indexes[i])
                ++result.missingGlyphs;
            glyphs.addPath(raw.pathForGlyph(indexes[i]).translated(positions[i]));
        }
    }
    glyphs.setFillRule(Qt::WindingFill);
    result.unitTransform = QTransform::fromScale(unit, unit);
    result.path = result.unitTransform.map(glyphs);
    result.glyphs = layout.glyphRuns();
    result.sizeMm = {wrapMm ? *wrapMm : natural * unit, y * unit};
    return result;
}

void arrow(QPainterPath &path, QPointF tip, QPointF toward) {
    const auto size = std::hypot(toward.x(), toward.y());
    if (size < 1e-9)
        return;
    toward /= size;
    const QPointF normal{-toward.y(), toward.x()};
    path.moveTo(tip);
    path.lineTo(tip + toward * arrowLengthMm + normal * arrowHalfWidthMm);
    path.moveTo(tip);
    path.lineTo(tip + toward * arrowLengthMm - normal * arrowHalfWidthMm);
}
void cross(QPainterPath &path, QPointF p) {
    const auto d = crossHalfMm;
    path.moveTo(p + QPointF(-d, -d));
    path.lineTo(p + QPointF(d, d));
    path.moveTo(p + QPointF(-d, d));
    path.lineTo(p + QPointF(d, -d));
}
std::string reasonText(const std::string &reason) {
    if (reason == "sceneMissing")
        return "Scene was deleted";
    if (reason == "sceneHasNoCamera")
        return "Scene has no camera";
    if (reason == "temporaryClipPlane")
        return "Scene uses a temporary section plane";
    if (reason == "showHidden")
        return "Scene shows hidden geometry";
    if (reason == "captureFailed")
        return "Could not draw this view";
    return reason;
}

struct Builder {
    PreparedDrawingSheet &out;
    FontResolver fonts;
    QRectF paper;
    explicit Builder(PreparedDrawingSheet &prepared)
        : out(prepared), fonts(prepared.fontSubstitutions),
          paper(0, 0, prepared.sheet.paper.widthMm, prepared.sheet.paper.heightMm) {}

    void add(DrawingSheetPrimitive primitive) {
        if (primitive.clipMm.isNull())
            primitive.clipMm = paper;
        else
            primitive.clipMm = primitive.clipMm.intersected(paper);
        out.primitives.push_back(std::move(primitive));
    }
    DrawingSheetPrimitive stroke(DrawingSheetLayer layer, Id source, QPainterPath path,
                                 double width, QColor color = Qt::black, bool dashed = false) {
        DrawingSheetPrimitive p;
        p.layer = layer;
        p.source = source;
        p.path = std::move(path);
        p.strokeMm = width;
        p.color = color;
        p.dashed = dashed;
        return p;
    }
    // Shapes text and places its box's top-left at origin (or centres it on origin).
    DrawingSheetPrimitive text(DrawingSheetLayer layer, Id source, const std::string &value,
                               const DrawingFont &font, QPointF origin, bool centred = false,
                               std::optional<double> wrapMm = {},
                               Qt::Alignment align = Qt::AlignLeft, QColor color = Qt::black,
                               bool bold = false) {
        const auto qtext = QString::fromStdString(value);
        const auto shaped =
            shapeText(qtext, fonts.family(font.family, source), font.sizePt, wrapMm, align, bold);
        out.missingGlyphs += shaped.missingGlyphs;
        if (centred)
            origin -= QPointF(shaped.sizeMm.width() / 2, shaped.sizeMm.height() / 2);
        DrawingSheetPrimitive p;
        p.layer = layer;
        p.source = source;
        p.path = shaped.path.translated(origin);
        p.fill = true;
        p.color = color;
        p.text = qtext;
        p.textBoxMm = QRectF(origin, shaped.sizeMm);
        p.glyphs = shaped.glyphs;
        p.glyphTransform = shaped.unitTransform * QTransform::fromTranslate(origin.x(), origin.y());
        return p;
    }
    void textBackground(DrawingSheetLayer layer, Id source, const QRectF &box,
                        const QRectF &clip = {}, const QTransform &turn = {}) {
        DrawingSheetPrimitive p;
        p.layer = layer;
        p.source = source;
        p.path.addPolygon(turn.map(QPolygonF(box.adjusted(-.6, -.3, .6, .3))));
        p.path.closeSubpath();
        p.fill = true;
        p.color = Qt::white;
        p.clipMm = clip;
        add(std::move(p));
    }
    // A labelled empty frame: prints honestly where content cannot be drawn.
    void labelledFrame(Id view, const QRectF &frame, const std::string &label, Id placeholderFor) {
        auto outline = QPainterPath();
        outline.addRect(frame);
        auto border = stroke(DrawingSheetLayer::View, view, outline, frameStrokeMm,
                             placeholderColor, placeholderFor != 0);
        border.clipMm = frame;
        border.placeholderFor = placeholderFor;
        add(std::move(border));
        DrawingFont font{fallbackFamily, captionSizePt};
        auto words = text(DrawingSheetLayer::View, view, label, font, frame.center(), false,
                          std::max(frame.width() - 4, 1.), Qt::AlignHCenter);
        // Centre the wrapped block vertically and horizontally on the frame.
        const auto box = words.textBoxMm;
        const QPointF shift(frame.center().x() - box.width() / 2 - box.left(),
                            frame.center().y() - box.height() / 2 - box.top());
        words.path.translate(shift);
        words.textBoxMm.translate(shift);
        words.glyphTransform *= QTransform::fromTranslate(shift.x(), shift.y());
        words.clipMm = frame;
        words.placeholderFor = placeholderFor;
        add(std::move(words));
    }

    void view(PreparedDrawingView &prepared) {
        const auto &record = prepared.record;
        const auto &evaluation = prepared.evaluation;
        const QRectF frame(record.frame.xMm, record.frame.yMm, record.frame.widthMm,
                           record.frame.heightMm);
        const auto state = evaluation.status.state;
        if (state == DrawingViewState::Broken || state == DrawingViewState::Failed) {
            std::string label;
            for (const auto &reason : evaluation.status.reasons)
                label += (label.empty() ? "" : "; ") + reasonText(reason);
            if (!evaluation.status.detail.empty())
                label += "\n" + evaluation.status.detail;
            labelledFrame(record.id, frame, label, 0);
            return;
        }
        if (prepared.raster) {
            DrawingSheetPrimitive slot;
            slot.layer = DrawingSheetLayer::View;
            slot.source = record.id;
            slot.rasterView = record.id;
            slot.rasterRectMm = frame;
            slot.clipMm = frame;
            add(std::move(slot));
            labelledFrame(record.id, frame,
                          "Raster view not captured\nRendered and perspective views are drawn "
                          "from the desktop viewport's raster capture",
                          record.id);
            return;
        }
        MeasuredDrawingPaths paths;
        try {
            require(evaluation.drawing.has_value(), "The view has no measured drawing");
            paths = measuredDrawingPaths(*evaluation.drawing);
        } catch (const std::exception &error) {
            prepared.pathError = error.what();
            labelledFrame(record.id, frame, "Could not draw this view\n" + prepared.pathError, 0);
            return;
        }
        out.missingGlyphs += paths.missingGlyphs;
        for (auto &value : paths.values) {
            DrawingSheetPrimitive p;
            p.layer = DrawingSheetLayer::View;
            p.source = record.id;
            p.path = value.path.translated(frame.topLeft());
            p.color = value.color;
            p.strokeMm = value.width;
            p.fill = value.fill;
            p.dashed = value.dashed;
            p.text = value.label;
            if (!value.label.isEmpty())
                p.textBoxMm = p.path.boundingRect();
            p.glyphs = value.glyphs;
            p.glyphTransform =
                value.glyphTransform * QTransform::fromTranslate(frame.left(), frame.top());
            p.clipMm = frame;
            add(std::move(p));
        }
    }
    void caption(const PreparedDrawingView &prepared) {
        const auto &frame = prepared.record.frame;
        add(text(DrawingSheetLayer::Caption, prepared.record.id, prepared.caption,
                 DrawingFont{fallbackFamily, captionSizePt},
                 {frame.xMm, frame.yMm + frame.heightMm + captionGapMm}));
    }

    void item(const PreparedDrawingItem &prepared) {
        const auto &record = prepared.record;
        const auto &evaluation = prepared.evaluation;
        const auto id = record.id;
        const bool broken = evaluation.state == DrawingItemState::Broken;
        constexpr auto layer = DrawingSheetLayer::Item;
        if (const auto *t = std::get_if<DrawingTextItem>(&record.data)) {
            const Qt::Alignment align = t->align == DrawingTextAlign::Center  ? Qt::AlignHCenter
                                        : t->align == DrawingTextAlign::Right ? Qt::AlignRight
                                                                              : Qt::AlignLeft;
            add(text(layer, id, t->text, t->font, {t->frame.xMm, t->frame.yMm}, false,
                     t->frame.widthMm, align));
        } else if (const auto *l = std::get_if<DrawingLabelItem>(&record.data)) {
            auto words = text(layer, id, l->text, l->font, qpoint(l->textAt));
            if (l->leader) {
                std::optional<QPointF> tip;
                if (l->target.kind == DrawingLabelTargetKind::Paper)
                    tip = qpoint(l->target.pointMm);
                else if (broken)
                    tip = qpoint(l->target.lastMm);
                else if (evaluation.tipMm)
                    tip = qpoint(*evaluation.tipMm);
                const auto box = words.textBoxMm;
                if (tip && !box.contains(*tip)) {
                    const QPointF start(std::clamp(tip->x(), box.left(), box.right()),
                                        std::clamp(tip->y(), box.top(), box.bottom()));
                    QPainterPath leader;
                    leader.moveTo(start);
                    leader.lineTo(*tip);
                    arrow(leader, *tip, start - *tip);
                    if (broken)
                        cross(leader, *tip);
                    add(stroke(layer, id, leader, annotationStrokeMm,
                               broken ? failureColor : QColor(Qt::black)));
                }
            }
            add(std::move(words));
        } else if (const auto *d = std::get_if<DrawingDimensionItem>(&record.data)) {
            const auto color = broken ? failureColor : QColor(Qt::black);
            QPainterPath lines;
            QPointF textAt;
            if (broken || !evaluation.lineMm || !evaluation.measuredMm || !evaluation.textMm) {
                // ADR 0081 cross markers where the anchors were last drawn.
                const auto a = qpoint(d->lastMm[0]), b = qpoint(d->lastMm[1]);
                cross(lines, a);
                cross(lines, b);
                textAt = (a + b) / 2;
            } else {
                const auto a = qpoint((*evaluation.lineMm)[0]), b = qpoint((*evaluation.lineMm)[1]);
                for (size_t i = 0; i < 2; ++i) {
                    const auto from = qpoint((*evaluation.measuredMm)[i]);
                    const auto to = i == 0 ? a : b;
                    const auto delta = to - from;
                    const auto length = std::hypot(delta.x(), delta.y());
                    if (length < 1e-9)
                        continue;
                    const auto unit = delta / length;
                    lines.moveTo(from + unit * std::min(extensionGapMm, length));
                    lines.lineTo(to + unit * extensionOvershootMm);
                }
                lines.moveTo(a);
                lines.lineTo(b);
                arrow(lines, a, b - a);
                arrow(lines, b, a - b);
                textAt = qpoint(*evaluation.textMm);
                // The value reads along the line, left to right or top to bottom, just
                // above it (the R091 design: "6000" over a plan, "4000" turned on its side).
                auto angle = std::atan2(b.y() - a.y(), b.x() - a.x()) * 180 / std::numbers::pi;
                if (angle > 90 + 1e-9)
                    angle -= 180;
                else if (angle <= -90 + 1e-9)
                    angle += 180;
                add(stroke(layer, id, lines, annotationStrokeMm, color));
                auto words = text(layer, id, evaluation.text, d->font, {0, 0}, false, {},
                                  Qt::AlignLeft, color);
                const auto size = words.textBoxMm.size();
                QTransform turn;
                turn.translate(textAt.x(), textAt.y());
                turn.rotate(angle);
                turn.translate(-size.width() / 2, -size.height() - dimensionTextGapMm);
                words.path = turn.map(words.path);
                words.glyphTransform *= turn;
                const QRectF local(QPointF(0, 0), size);
                words.textBoxMm = turn.mapRect(local);
                textBackground(layer, id, local, {}, turn);
                add(std::move(words));
                return;
            }
            add(stroke(layer, id, lines, annotationStrokeMm, color));
            auto words =
                text(layer, id, evaluation.text, d->font, textAt, true, {}, Qt::AlignLeft, color);
            textBackground(layer, id, words.textBoxMm);
            add(std::move(words));
        } else if (const auto *line = std::get_if<DrawingLineItem>(&record.data)) {
            QPainterPath path;
            path.moveTo(qpoint(line->from));
            path.lineTo(qpoint(line->to));
            add(stroke(layer, id, path, line->strokeMm, Qt::black,
                       line->dash == DrawingStrokeDash::Dashed));
        } else if (const auto *r = std::get_if<DrawingRectangleItem>(&record.data)) {
            QPainterPath path;
            path.addRect(r->frame.xMm, r->frame.yMm, r->frame.widthMm, r->frame.heightMm);
            add(stroke(layer, id, path, r->strokeMm, Qt::black,
                       r->dash == DrawingStrokeDash::Dashed));
        } else if (const auto *b = std::get_if<DrawingTitleBlockItem>(&record.data)) {
            titleBlock(id, *b, evaluation.titleBlock.value_or(DrawingTitleBlockDerived{}));
        }
    }
    void titleBlock(Id id, const DrawingTitleBlockItem &block,
                    const DrawingTitleBlockDerived &derived) {
        // The R091 design's title block: a four-column grid, the project across the top,
        // then the drawing title and sheet, then scale, date, revision and page. Each cell
        // holds its caption over its value, which is bold.
        const QRectF frame(block.frame.xMm, block.frame.yMm, block.frame.widthMm,
                           block.frame.heightMm);
        QPainterPath outline;
        outline.addRect(frame);
        add(stroke(DrawingSheetLayer::Item, id, outline, marginStrokeMm));
        const auto cells = drawingTitleBlockCells(block.frame);
        QPainterPath dividers;
        for (const auto &cell : cells) {
            // Each cell draws its bottom and right edges inside the outline.
            if (cell.rectMm.bottom() < frame.bottom() - 1e-6) {
                dividers.moveTo(cell.rectMm.bottomLeft());
                dividers.lineTo(cell.rectMm.bottomRight());
            }
            if (cell.rectMm.right() < frame.right() - 1e-6) {
                dividers.moveTo(cell.rectMm.topRight());
                dividers.lineTo(cell.rectMm.bottomRight());
            }
        }
        auto lines = stroke(DrawingSheetLayer::Item, id, dividers, frameStrokeMm);
        lines.clipMm = frame;
        add(std::move(lines));
        for (const auto &cell : cells) {
            const auto value = cell.field == "project"        ? block.fields.project
                               : cell.field == "drawingTitle" ? block.fields.drawingTitle
                               : cell.field == "sheet"        ? derived.sheetNumber
                               : cell.field == "scale"        ? derived.scale
                               : cell.field == "date"         ? block.fields.date
                               : cell.field == "revision"     ? block.fields.revision
                                                              : derived.page;
            const auto left = cell.rectMm.left() + cellPaddingMm;
            std::vector<DrawingSheetPrimitive> words;
            if (cell.caption.empty()) {
                if (!value.empty())
                    words.push_back(text(
                        DrawingSheetLayer::Item, id, value,
                        DrawingFont{block.font.family,
                                    std::min(block.font.sizePt * projectScale, drawingFontMaxPt)},
                        {left, cell.rectMm.top()}, false, {}, Qt::AlignLeft, Qt::black, true));
            } else {
                words.push_back(text(DrawingSheetLayer::Item, id, cell.caption, block.font,
                                     {left, cell.rectMm.top()}));
                if (!value.empty()) {
                    const auto below = words.front().textBoxMm.bottom();
                    words.push_back(text(DrawingSheetLayer::Item, id, value, block.font,
                                         {left, below}, false, {}, Qt::AlignLeft, Qt::black, true));
                }
            }
            if (words.empty())
                continue;
            // The cell's lines are centred vertically.
            QRectF box;
            for (const auto &word : words)
                box = box.isNull() ? word.textBoxMm : box.united(word.textBoxMm);
            const auto shift = cell.rectMm.center().y() - box.center().y();
            for (auto &word : words) {
                word.path.translate(0, shift);
                word.textBoxMm.translate(0, shift);
                word.glyphTransform *= QTransform::fromTranslate(0, shift);
                word.clipMm = cell.rectMm;
                add(std::move(word));
            }
        }
    }
};
} // namespace

std::vector<DrawingTitleBlockCell> drawingTitleBlockCells(const DrawingItemFrame &frame) {
    // Rows: the project (shorter, one line) and two rows of caption-over-value cells.
    const auto x = frame.xMm, y = frame.yMm, w = frame.widthMm, h = frame.heightMm;
    const auto project = h * .28, row = (h - project) / 2, column = w / 4;
    auto cell = [&](const char *field, const char *caption, bool stored, double column0,
                    double columns, double top, double height) {
        return DrawingTitleBlockCell{field, caption, stored,
                                     QRectF(x + column0 * column, top, columns * column, height)};
    };
    return {cell("project", "", true, 0, 4, y, project),
            cell("drawingTitle", "Drawing", true, 0, 3, y + project, row),
            cell("sheet", "Sheet", false, 3, 1, y + project, row),
            cell("scale", "Scale", false, 0, 1, y + project + row, row),
            cell("date", "Date", true, 1, 1, y + project + row, row),
            cell("revision", "Revision", true, 2, 1, y + project + row, row),
            cell("page", "Page", false, 3, 1, y + project + row, row)};
}
QSizeF drawingTextSizeMm(const std::string &text, const DrawingFont &font,
                         std::optional<double> wrapMm) {
    std::vector<DrawingFontSubstitution> substitutions;
    FontResolver fonts(substitutions);
    return shapeText(QString::fromStdString(text), fonts.family(font.family, 0), font.sizePt,
                     wrapMm, Qt::AlignLeft)
        .sizeMm;
}
std::vector<DrawingSheetPrimitive> drawingItemPrimitives(const DrawingSheetRecord &sheet,
                                                         const PreparedDrawingItem &item) {
    PreparedDrawingSheet prepared;
    prepared.sheet = sheet;
    Builder builder(prepared);
    builder.item(item);
    return std::move(prepared.primitives);
}
std::string drawingViewCaption(int number, const DrawingViewRecord &view, bool notToScale) {
    return std::to_string(number) + " · " + view.name + " " +
           (notToScale ? std::string("Not to scale")
                       : "1:" + formatDenominator(view.scaleDenominator));
}

DrawingSheetCapture captureDrawingSheet(const Document &doc, Id sheet) {
    require(doc.drawingSheets().contains(sheet), "Drawing sheet does not exist");
    DrawingSheetCapture capture;
    capture.identity = doc.identity();
    capture.revision = doc.revision();
    capture.sheet = sheet;
    capture.document = doc.readSnapshot();
    for (const auto view : drawingViewsOnSheet(doc, sheet))
        capture.views.push_back(captureDrawingViewEvaluation(doc, view));
    return capture;
}

PreparedDrawingSheet prepareDrawingSheet(const DrawingSheetCapture &capture) {
    require(qobject_cast<QGuiApplication *>(QCoreApplication::instance()),
            "Drawing sheet composition requires the graphical font worker");
    const auto &doc = capture.document;
    const auto sheet = doc.drawingSheets().find(capture.sheet);
    require(sheet != doc.drawingSheets().end(), "Drawing sheet does not exist");
    PreparedDrawingSheet prepared;
    prepared.identity = capture.identity;
    prepared.revision = capture.revision;
    prepared.sheet = *sheet->second;
    int number = 0;
    for (const auto &input : capture.views) {
        PreparedDrawingView view;
        view.record = input.record;
        view.evaluation = computeDrawingViewEvaluation(input);
        view.number = ++number;
        const auto &status = view.evaluation.status;
        view.caption = drawingViewCaption(view.number, view.record, status.notToScale);
        view.raster =
            status.state != DrawingViewState::Broken && status.state != DrawingViewState::Failed &&
            (status.notToScale || view.record.content == DrawingViewContent::RasterAppearance);
        prepared.views.push_back(std::move(view));
    }
    for (const auto item : drawingItemsOnSheet(doc, capture.sheet))
        prepared.items.push_back({*doc.drawingItems().at(item), evaluateDrawingItem(doc, item)});

    Builder builder(prepared);
    const auto &record = prepared.sheet;
    if (record.marginMm > 0) {
        QPainterPath margin;
        margin.addRect(record.marginMm, record.marginMm, record.paper.widthMm - 2 * record.marginMm,
                       record.paper.heightMm - 2 * record.marginMm);
        builder.add(builder.stroke(DrawingSheetLayer::MarginFrame, 0, margin, marginStrokeMm));
    }
    for (auto &view : prepared.views)
        builder.view(view);
    for (const auto &view : prepared.views)
        builder.caption(view);
    for (const auto &item : prepared.items)
        builder.item(item);
    return prepared;
}

PreparedDrawingSheet prepareDrawingSheet(const Document &doc, Id sheet) {
    return prepareDrawingSheet(captureDrawingSheet(doc, sheet));
}

bool drawingFontEmbeddable(const QRawFont &font) {
    if (!font.isValid() || font.fontTable("glyf").isEmpty())
        return false;
    const auto os2 = font.fontTable("OS/2");
    if (os2.size() < 10)
        return true; // No OS/2 table: no embedding restriction is declared.
    const auto fsType = quint16((quint8(os2[8]) << 8) | quint8(os2[9]));
    // Restricted-license, no-subsetting (PDF embedding always subsets) or bitmap-only.
    return (fsType & 0x000f) != 0x0002 && !(fsType & 0x0100) && !(fsType & 0x0200);
}

QTransform drawingSheetDeviceTransform(const QPaintDevice &device) {
    return QTransform::fromScale(device.logicalDpiX() / 25.4, device.logicalDpiY() / 25.4);
}

DrawingSheetPaintResult paintDrawingSheet(QPainter &painter, const PreparedDrawingSheet &sheet,
                                          const DrawingSheetPaintOptions &options) {
    require(painter.isActive() && painter.device(), "Drawing sheet painter is not active");
    DrawingSheetPaintResult result;
    result.fontSubstitutions = sheet.fontSubstitutions;
    result.missingGlyphs = sheet.missingGlyphs;
    for (const auto &view : sheet.views)
        if (view.evaluation.status.state == DrawingViewState::Broken ||
            view.evaluation.status.state == DrawingViewState::Failed || !view.pathError.empty())
            result.brokenViews.push_back(view.record.id);
    for (const auto &item : sheet.items)
        if (item.evaluation.state == DrawingItemState::Broken)
            result.brokenItems.push_back(item.record.id);
    auto image = [&](Id view) -> const QImage * {
        const auto it = options.rasters.find(view);
        return it != options.rasters.end() && !it->second.isNull() ? &it->second : nullptr;
    };
    painter.save();
    painter.setWorldTransform(
        options.paperToDevice.value_or(drawingSheetDeviceTransform(*painter.device())), true);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setRenderHint(QPainter::LosslessImageRendering);
    for (const auto &p : sheet.primitives) {
        if (p.layer == DrawingSheetLayer::MarginFrame && !options.marginFrame)
            continue;
        if (p.placeholderFor && image(p.placeholderFor))
            continue;
        painter.setClipRect(p.clipMm, Qt::ReplaceClip);
        if (p.rasterView) {
            if (const auto *raster = image(p.rasterView)) {
                painter.drawImage(p.rasterRectMm, *raster);
                result.rasterViews.push_back(p.rasterView);
                ++result.primitives;
            } else {
                result.rasterPlaceholders.push_back(
                    {p.rasterView, options.rasters.contains(p.rasterView) ? "rasterImageInvalid"
                                                                          : "rasterNotSupplied"});
            }
            continue;
        }
        if (options.text == DrawingTextMode::Text && !p.glyphs.isEmpty()) {
            bool embeddable = true;
            for (const auto &run : p.glyphs)
                if (!drawingFontEmbeddable(run.rawFont())) {
                    embeddable = false;
                    const auto family = run.rawFont().familyName().toStdString();
                    if (std::find(result.outlinedFonts.begin(), result.outlinedFonts.end(),
                                  family) == result.outlinedFonts.end())
                        result.outlinedFonts.push_back(family);
                }
            if (embeddable) {
                // Real text: the PDF engine embeds a subset of each font and writes the
                // characters, so the text is selectable and searchable. The pen colours it.
                painter.save();
                painter.setPen(QPen(p.color));
                painter.setBrush(Qt::NoBrush);
                painter.setTransform(p.glyphTransform, true);
                for (const auto &run : p.glyphs)
                    painter.drawGlyphRun(QPointF(), run);
                painter.restore();
                ++result.primitives;
                ++result.textPrimitives;
                continue;
            }
        }
        if (p.fill) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(p.color);
        } else {
            painter.setPen(QPen(p.color, p.strokeMm, p.dashed ? Qt::DashLine : Qt::SolidLine,
                                Qt::RoundCap, Qt::RoundJoin));
            painter.setBrush(Qt::NoBrush);
        }
        painter.drawPath(p.path);
        ++result.primitives;
    }
    painter.restore();
    return result;
}
} // namespace sketchy
