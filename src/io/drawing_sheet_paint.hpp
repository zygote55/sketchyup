#pragma once
#include "core/drawing_item_records.hpp"
#include "core/model.hpp"
#include "io/drawing_items.hpp"
#include "io/drawing_view.hpp"
#include <QColor>
#include <QGlyphRun>
#include <QImage>
#include <QPainterPath>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QTransform>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>
class QPainter;
class QPaintDevice;
namespace sketchy {
// Drawing-sheet compositor (ADR 0164 §§1–2, R091.d). Paints one sheet's printable
// content at paper scale with Qt Gui only (no Widgets), so the desktop canvas, the
// CLI and R092's PDF writer share it. It never paints editor-only decorations: the
// "Changed" chip, selection, handles, the grid and the broken hatch belong to the
// canvas, which paints them after this.
//
// Evaluation is split so that typing never waits on it:
//   captureDrawingSheet  - owner thread: reads the Document into immutable values;
//   prepareDrawingSheet  - any thread: view fingerprints and measured drawings, item
//                          evaluation, measured paths, text shaping and the display
//                          list (needs a QGuiApplication for fonts, no painter);
//   paintDrawingSheet    - any QPaintDevice: replays the display list.
// prepareDrawingSheet(doc, sheet) is the single-thread composition of the first two
// and gives identical results.

// Owner-thread capture of one sheet.
struct DrawingSheetCapture {
    std::string identity;
    std::uint64_t revision{};
    Id sheet{};
    // A readSnapshot of the document at capture time: the sheet, view and item
    // records and the model state that item evaluation reads.
    Document document;
    std::vector<DrawingViewEvaluationInput> views; // The sheet's views, in ID order.
};
// Throws only when the sheet does not exist.
DrawingSheetCapture captureDrawingSheet(const Document &doc, Id sheet);

enum class DrawingSheetLayer { MarginFrame, View, Caption, Item };
// One display-list entry in paper millimetres (origin top-left, y down).
struct DrawingSheetPrimitive {
    DrawingSheetLayer layer{DrawingSheetLayer::Item};
    Id source{}; // The view or item it draws; zero for the margin frame.
    QPainterPath path;
    QColor color{Qt::black};
    double strokeMm{.25};
    bool fill{}, dashed{};
    QRectF clipMm;    // The view frame for view content, otherwise the paper.
    QString text;     // The text a shaped glyph path draws.
    QRectF textBoxMm; // The laid-out text box of a glyph path.
    // The same text as shaped glyph runs (R092): glyphTransform maps their layout units
    // to paper millimetres. DrawingTextMode::Text paints these as real text, so a PDF
    // embeds font subsets and the text stays selectable; path is their outline.
    QList<QGlyphRun> glyphs;
    QTransform glyphTransform;
    // A raster slot: the image supplied for rasterView is drawn into rasterRectMm.
    Id rasterView{};
    QRectF rasterRectMm;
    // Painted only when no raster image is supplied for this view.
    Id placeholderFor{};
};
// A font family that is not installed is replaced deterministically: by "DejaVu
// Sans" (the R078 export face), else the platform's general font. R092 turns these
// into the preflight list.
struct DrawingFontSubstitution {
    std::string requested, used;
    // The items, or the views whose captions and frame labels, asked for it.
    std::vector<Id> items;
    bool operator==(const DrawingFontSubstitution &) const = default;
};
struct PreparedDrawingView {
    DrawingViewRecord record;
    DrawingViewEvaluation evaluation;
    int number{};          // 1-based caption number: the view's place on the sheet in ID order.
    std::string caption;   // "1 · Ground floor plan 1:50", or "… Not to scale".
    bool raster{};         // Raster appearance or a perspective (not to scale) view.
    std::string pathError; // Measured paths exceeded the R078 budgets.
};
struct PreparedDrawingItem {
    DrawingItemRecord record;
    DrawingItemEvaluation evaluation;
};
struct PreparedDrawingSheet {
    std::string identity;
    std::uint64_t revision{};
    DrawingSheetRecord sheet;
    std::vector<PreparedDrawingView> views;        // ID order (the views' paint order).
    std::vector<PreparedDrawingItem> items;        // z order, bottom first.
    std::vector<DrawingSheetPrimitive> primitives; // Paint order.
    std::vector<DrawingFontSubstitution> fontSubstitutions;
    qsizetype missingGlyphs{};
};
// Pure; safe on a worker thread while a QGuiApplication exists.
PreparedDrawingSheet prepareDrawingSheet(const DrawingSheetCapture &capture);
// Single-thread path: prepareDrawingSheet(captureDrawingSheet(doc, sheet)).
PreparedDrawingSheet prepareDrawingSheet(const Document &doc, Id sheet);

// How text primitives reach the device. Outlines (the default) fill the shaped glyph
// paths, identical on every device. Text draws the glyph runs, so a QPdfWriter writes
// real text with embedded font subsets (R092); a run whose font cannot be embedded
// (drawingFontEmbeddable) is filled as outlines and reported.
enum class DrawingTextMode { Outlines, Text };
// Whether a PDF may embed this font: an sfnt font with TrueType outlines ("glyf") whose
// OS/2 fsType allows embedding (not restricted-license, not bitmap-only).
bool drawingFontEmbeddable(const QRawFont &font);
struct DrawingSheetPaintOptions {
    // Maps paper millimetres to the painter's current coordinates. Default: the
    // device's logical DPI / 25.4, so one paper millimetre is one device millimetre.
    std::optional<QTransform> paperToDevice;
    // Raster content for raster-appearance and perspective views, rendered by the
    // desktop viewport's scene-scoped capture (R091.e) or R092 at export DPI. A view
    // without an image paints a labelled placeholder and is reported.
    std::map<Id, QImage> rasters;
    // The page-margin frame (sheet marginMm); skipped when the margin is zero.
    bool marginFrame{true};
    DrawingTextMode text{DrawingTextMode::Outlines};
};
struct DrawingSheetRasterPlaceholder {
    Id view{};
    std::string reason; // Stable code: rasterNotSupplied or rasterImageInvalid.
    bool operator==(const DrawingSheetRasterPlaceholder &) const = default;
};
struct DrawingSheetPaintResult {
    std::vector<DrawingFontSubstitution> fontSubstitutions;
    std::vector<DrawingSheetRasterPlaceholder> rasterPlaceholders;
    std::vector<Id> rasterViews; // Painted from a supplied image.
    std::vector<Id> brokenViews; // Broken or failed: painted as a labelled empty frame.
    std::vector<Id> brokenItems;
    qsizetype missingGlyphs{};
    size_t primitives{}; // Display-list entries painted.
    // DrawingTextMode::Text: primitives drawn as real text, and the font families whose
    // runs could not be embedded and were filled as outlines instead.
    size_t textPrimitives{};
    std::vector<std::string> outlinedFonts;
};
// The paper-millimetre to device transform for a paint device.
QTransform drawingSheetDeviceTransform(const QPaintDevice &device);
// Paints the prepared sheet. The painter's state is restored afterwards.
DrawingSheetPaintResult paintDrawingSheet(QPainter &painter, const PreparedDrawingSheet &sheet,
                                          const DrawingSheetPaintOptions &options = {});
// The cells of a title block in paper millimetres (R091.g, the R091 design): a four-column
// grid with the project across the top row; the drawing title (three columns) and sheet
// number; then scale, date, revision and page. Stored cells are the editable fields
// (project, drawingTitle, date, revision); the sheet number, scale and page are derived.
struct DrawingTitleBlockCell {
    std::string field;   // "project", "drawingTitle", "sheet", "scale", "date", "revision", "page"
    std::string caption; // Empty for the project row.
    bool stored{};
    QRectF rectMm;
};
std::vector<DrawingTitleBlockCell> drawingTitleBlockCells(const DrawingItemFrame &frame);
// The laid-out size of item text in paper millimetres, as the compositor shapes it.
QSizeF drawingTextSizeMm(const std::string &text, const DrawingFont &font,
                         std::optional<double> wrapMm = std::nullopt);
// The display list of one item alone (paper millimetres), as prepareDrawingSheet builds
// it: the desktop canvas uses it for previews and for items edited since the last
// evaluation, so a change never waits on a worker.
std::vector<DrawingSheetPrimitive> drawingItemPrimitives(const DrawingSheetRecord &sheet,
                                                         const PreparedDrawingItem &item);
// Caption text of a view: "<number> · <name> 1:<scale>" or "... Not to scale".
std::string drawingViewCaption(int number, const DrawingViewRecord &view, bool notToScale);
} // namespace sketchy
