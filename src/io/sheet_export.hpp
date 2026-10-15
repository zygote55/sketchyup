#pragma once
#include "core/model.hpp"
#include "io/drawing_view.hpp"
#include <QByteArray>
#include <QImage>
#include <QJsonObject>
#include <QSize>
class QPagedPaintDevice;
#include <QString>
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
namespace sketchy {
// Drawing-sheet PDF export (R092.a, ADR 0164 "R092 export"). Every page is painted by
// the sheet compositor (paintDrawingSheet) at actual size: each page is its own paper
// size in millimetres, so a 2 m edge at 1:50 measures 40 mm on paper. Geometry stays
// vector, text is real text with embedded font subsets, and raster views are drawn at
// the export DPI. Nothing here edits the document.
//
// preflightSheetExport is the single check the desktop dialog (R092.b), the CLI and
// the inspection query share. exportSheetsPdf refuses to publish while a blocking
// finding is unresolved; the opt-in options turn specific findings into warnings.

// Raster content for a raster-appearance or perspective view at the requested pixel
// size, from the desktop viewport's scene-scoped capture. Headless callers have none.
using SheetRasterRenderer =
    std::function<QImage(const DrawingViewEvaluationInput &view, QSize pixels)>;

struct SheetExportOptions {
    // The pages to export, as sheet IDs; empty exports every sheet. Pages are always
    // written in page order. resolveSheetPageRange turns "A-101..A-301" into IDs.
    std::vector<Id> sheets;
    // Export broken or failed views (and raster views that cannot be rendered here) as
    // empty, labelled frames instead of refusing.
    bool allowBroken{};
    // Export views that changed since they were accepted as they are now; the
    // reconciliation report names them.
    bool allowChanged{};
    // Raster views: 72–1200 dots per inch (default 300), capped at 8192 pixels per side
    // and 16 megapixels per view; the report gives the DPI actually used.
    int rasterDpi{300};
    // Without a renderer, raster views can only print as labelled frames.
    SheetRasterRenderer rasterRenderer;
};

enum class SheetExportSeverity { Blocking, Warning, Info };
const char *sheetExportSeverityName(SheetExportSeverity severity);

// One preflight finding. Codes are stable:
//   blocking unless opted in: viewBroken (allowBroken), viewChanged (allowChanged),
//                             rasterUnavailable (allowBroken);
//   warning:                  fontSubstituted, fontOutlined, missingGlyphs, itemBroken,
//                             sceneReferencesMissing;
//   info:                     scaleSummary, rasterViews.
// An opted-in finding keeps its code and becomes a warning with optedIn set.
struct SheetExportFinding {
    SheetExportSeverity severity{SheetExportSeverity::Info};
    std::string code;
    std::vector<Id> sheets, views, items;
    std::string message;
    QJsonObject detail; // Code-specific structured data (fonts, scales, reasons, DPI).
    bool optedIn{};
};

// One page of the export: the sheet and its paper.
struct SheetExportPage {
    Id sheet{};
    std::string number, name;
    double widthMm{}, heightMm{};
};

struct SheetExportPreflight {
    std::string documentId;
    std::uint64_t revision{};
    std::vector<SheetExportPage> pages; // Page order.
    std::vector<SheetExportFinding> findings;
    bool blocked() const;
};

// Thrown for a page range or option the export cannot honour (code INVALID_REQUEST),
// and by exportSheetsPdf while preflight blocks (code EXPORT_BLOCKED, preflight set).
struct SheetExportError : std::runtime_error {
    SheetExportError(std::string code, const std::string &message,
                     std::optional<SheetExportPreflight> preflight = std::nullopt);
    const std::string &code() const { return code_; }
    const std::optional<SheetExportPreflight> &preflight() const { return preflight_; }

  private:
    std::string code_;
    std::optional<SheetExportPreflight> preflight_;
};

// Page-range text: "all" (or empty) for every sheet, else comma-separated parts. A part
// is one page or a range "FROM..TO" (or "FROM–TO" with an en dash; a hyphen belongs to
// sheet numbers such as "A-101"). A page is named by its sheet number ("A-101") or,
// when no sheet has that number, by its 1-based position ("3"). A range covers the
// pages between its ends in page order. Returns sheet IDs in page order, without
// repeats. Throws SheetExportError(INVALID_REQUEST) for an unknown page or a reversed
// range.
std::vector<Id> resolveSheetPageRange(const Document &doc, const std::string &range);

// Pure: reads the document, never edits it. Evaluates every selected view and item and
// shapes their text, so it requires a QGuiApplication (fonts); throws
// SheetExportError(UNAVAILABLE_CONTEXT) without one, and INVALID_REQUEST for a document
// without sheets, an unknown sheet or a DPI out of range.
SheetExportPreflight preflightSheetExport(const Document &doc, const SheetExportOptions &options);

struct SheetExport {
    QByteArray bytes;   // The PDF.
    QJsonObject report; // The reconciliation report, with sha256 and bytes.
    SheetExportPreflight preflight;
};
// Runs the preflight and paints every page into a PDF in memory. The reconciliation
// report is also attached to the PDF as "sheet-export-report.json". Throws
// SheetExportError(EXPORT_BLOCKED) while a blocking finding is unresolved.
SheetExport renderSheetsPdf(const Document &doc, const SheetExportOptions &options);
// renderSheetsPdf, then publishes the PDF atomically at a new path (publishNewFile:
// an existing file is never replaced, and a failure leaves no partial file).
SheetExport exportSheetsPdf(const Document &doc, const SheetExportOptions &options,
                            const QString &path);

// Printing (R092.b). Paints every selected page at actual size onto the device's current
// paper, one device page per sheet: one paper millimetre is one device millimetre, never
// scaled to fit. The caller chooses the paper and a full-page layout (the paper's top-left
// corner is the sheet's); a sheet larger than the paper is cut off at the paper's edge, so
// the desktop warns before printing. The preflight, opt-ins and raster rules are
// renderSheetsPdf's: it throws SheetExportError(EXPORT_BLOCKED) while a blocking finding is
// unresolved, and paints nothing.
struct SheetPrint {
    SheetExportPreflight preflight;
    size_t pages{};
    std::vector<Id> exportedAsIs, exportedEmpty, rasterViews;
};
SheetPrint printSheets(const Document &doc, const SheetExportOptions &options,
                       QPagedPaintDevice &device);

// JSON forms shared by the CLI, the inspection query and the report.
QJsonObject sheetExportFindingJson(const SheetExportFinding &finding);
QJsonObject sheetExportPreflightJson(const SheetExportPreflight &preflight);
} // namespace sketchy
