#include "automation/inspection.hpp"
#include "core/annotation_anchors.hpp"
#include "core/drawing_item_records.hpp"
#include "core/drawing_sheets.hpp"
#include "core/scenes.hpp"
#include "io/document_io.hpp"
#include "io/drawing_items.hpp"
#include "io/drawing_sheet_paint.hpp"
#include "io/pdf_info.hpp"
#include "io/sheet_export.hpp"
#include "pdf_info_check.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTimeZone>
#include <cmath>
#include <iostream>
#include <string>
#include <tuple>
using namespace sketchy;
// R092.a: drawing-sheet PDF export, its preflight and the CLI. The PDF itself is
// checked: page count and media boxes from its bytes, embedded fonts and text from
// its dictionaries, and, with Poppler (SKETCHYUP_PDF_TEST=pdftoppm, as
// measured_export_real), measured scale, clipping and extractable text.
namespace {
void check(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> std::string rejects(F f) {
    try {
        f();
    } catch (const SheetExportError &error) {
        return error.code();
    } catch (const std::exception &error) {
        return "runtime";
    }
    throw std::runtime_error("Expected a rejection");
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
DrawingViewRecord viewRecord(Id sheet, Id scene, std::string name, DrawingFrame frame,
                             double scale = 50) {
    DrawingViewRecord record;
    record.name = std::move(name);
    record.sheet = sheet;
    record.source = {DrawingViewSourceKind::Scene, scene};
    record.frame = frame;
    record.scaleDenominator = scale;
    return record;
}
Id vertexAt(const Document &doc, Id body, Vec3 point) {
    for (const auto &[id, p] : doc.bodies().at(body)->surface.vertices)
        if (length(p - point) < 1e-9)
            return id;
    throw std::runtime_error("No vertex at the point");
}
void acceptAll(Document &doc) {
    std::map<Id, DrawingViewAcceptance> stamps;
    for (const auto &[view, record] : doc.drawingViews()) {
        const auto status = evaluateDrawingView(doc, view);
        if (!status.fingerprint.empty())
            stamps[view] = {doc.revision(), status.fingerprint};
    }
    acceptDrawingViews(doc, stamps);
}

// A 2 m x 1 m slab. Sheet A-101 (A3 landscape): a plan view at 1:50 whose scene target
// (1, 0.5) sits at the frame centre (120, 95), so the slab is drawn at x 100–140 and
// y 85–105 mm; a dimension "2000"; a text item; a title block. Sheet A-102 (A4
// portrait): a 1:20 detail cropped by a 60 x 60 mm frame at (20, 20): the slab (100 x 50 mm
// at 1:20, centred at (50, 50)) overflows it left and right. Every view is accepted, so a clean
// document exports without opt-ins.
struct Fixture {
    Document doc;
    Id face{}, plan{}, a101{}, a102{}, planView{}, detail{}, dimension{}, note{};
    Fixture() {
        face = doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 1, 0}, {0, 1, 0}}}, "Slab");
        plan = createScene(doc, "Plan", planScene());
        a101 = createDrawingSheet(doc, "Ground floor", {420, 297}, "A-101", 10);
        a102 = createDrawingSheet(doc, "Details", {210, 297}, "A-102", 10);
        planView =
            createDrawingView(doc, viewRecord(a101, plan, "Ground floor plan", {20, 20, 200, 150}));
        detail = createDrawingView(doc, viewRecord(a102, plan, "Slab edge", {20, 20, 60, 60}, 20));
        DrawingDimensionItem d;
        d.view = planView;
        d.anchors = {vertexAnchor(doc, face, vertexAt(doc, face, {0, 0, 0})),
                     vertexAnchor(doc, face, vertexAt(doc, face, {2, 0, 0}))};
        d.orientation = DrawingDimensionOrientation::Horizontal;
        d.offsetMm = -10;
        d.lastMm = {DrawingPointMm{100, 105}, DrawingPointMm{140, 105}};
        dimension = createDrawingItem(doc, a101, d);
        DrawingTextItem t;
        t.frame = {30, 200, 80, 20};
        t.text = "General notes";
        note = createDrawingItem(doc, a101, t);
        DrawingTitleBlockItem b;
        b.frame = {300, 220, 110, 70};
        b.fields = {"House", "Ground floor plan", "2026-10-14", "B"};
        createDrawingItem(doc, a101, b);
        acceptAll(doc);
    }
};

const SheetExportFinding *find(const SheetExportPreflight &p, const std::string &code) {
    for (const auto &f : p.findings)
        if (f.code == code)
            return &f;
    return nullptr;
}
std::vector<QSizeF> mediaBoxes(const QByteArray &pdf) {
    std::vector<QSizeF> result;
    auto matches = QRegularExpression(R"(/MediaBox\s*\[\s*0\s+0\s+([0-9.]+)\s+([0-9.]+)\s*\])")
                       .globalMatch(QString::fromLatin1(pdf));
    while (matches.hasNext()) {
        const auto m = matches.next();
        result.push_back({m.captured(1).toDouble(), m.captured(2).toDouble()});
    }
    return result;
}
// Qt's PDF engine writes page boxes in whole points (QPageLayout::fullRectPoints), so
// a page is within half a point (0.18 mm) of its paper size; drawing scale is exact.
bool nearPt(double points, double mm) { return std::abs(points - mm * 72 / 25.4) <= .5; }
QImage fakeRaster(const DrawingViewEvaluationInput &, QSize pixels) {
    QImage image(pixels, QImage::Format_RGB32);
    image.fill(Qt::red);
    return image;
}

void pagesAndPaperSizes() {
    Fixture f;
    const auto exported = renderSheetsPdf(f.doc, {});
    const auto &pdf = exported.bytes;
    check(pdf.startsWith("%PDF-"), "PDF header");
    check(pdf.count("/Type /Page\n") + pdf.count("/Type /Page ") + pdf.count("/Type /Page/") == 2,
          "Two pages");
    const auto boxes = mediaBoxes(pdf);
    std::string listed;
    for (const auto &box : boxes)
        listed += " " + std::to_string(box.width()) + "x" + std::to_string(box.height());
    check(boxes.size() == 2, "One media box per page:" + listed);
    check(nearPt(boxes[0].width(), 420) && nearPt(boxes[0].height(), 297),
          "Page 1 is A3 landscape at actual size:" + listed);
    check(nearPt(boxes[1].width(), 210) && nearPt(boxes[1].height(), 297),
          "Page 2 is A4 portrait at actual size");
    const auto &report = exported.report;
    check(report["pageCount"] == 2 && report["pages"].toArray().size() == 2,
          "Report lists both pages");
    const auto page1 = report["pages"].toArray()[0].toObject();
    check(page1["number"] == "A-101" && page1["paperMm"].toObject()["width"] == 420,
          "Report page paper");
    const auto view = page1["views"].toArray()[0].toObject();
    check(view["state"] == "current" && view["scale"] == "1:50" && view["content"] == "vector" &&
              view["exported"] == "current" && view["fingerprint"].toString().size() == 64,
          "Report view state, scale and fingerprint");
    check(report["sha256"].toString().size() == 64 && report["bytes"].toDouble() == pdf.size(),
          "Report hashes the PDF");
    check(pdf.contains("/EmbeddedFiles"), "Report attached to the PDF");
    // Page ranges.
    check(resolveSheetPageRange(f.doc, "all") == std::vector<Id>{f.a101, f.a102}, "all");
    check(resolveSheetPageRange(f.doc, "A-102") == std::vector<Id>{f.a102}, "By number");
    check(resolveSheetPageRange(f.doc, "2") == std::vector<Id>{f.a102}, "By position");
    check(resolveSheetPageRange(f.doc, "A-101..A-102") == std::vector<Id>{f.a101, f.a102}, "Range");
    check(resolveSheetPageRange(f.doc, "A-101–A-102") == std::vector<Id>{f.a101, f.a102},
          "En-dash range");
    check(resolveSheetPageRange(f.doc, "A-102, 1") == std::vector<Id>{f.a101, f.a102},
          "List in page order");
    check(rejects([&] { resolveSheetPageRange(f.doc, "A-999"); }) == "INVALID_REQUEST",
          "Unknown page");
    check(rejects([&] { resolveSheetPageRange(f.doc, "A-102..A-101"); }) == "INVALID_REQUEST",
          "Reversed range");
    SheetExportOptions one;
    one.sheets = resolveSheetPageRange(f.doc, "A-102");
    const auto single = renderSheetsPdf(f.doc, one);
    check(mediaBoxes(single.bytes).size() == 1 && nearPt(mediaBoxes(single.bytes)[0].width(), 210),
          "Range exports only its pages");
    std::cout << "pages and paper sizes: ok\n";
}

// D-036: the document information dictionary is well formed on every Qt. Qt 6.8.0 and
// 6.8.1 left it unclosed ("2" for ">>"), and Qt 6.8.0 wrote "D:00000000-1-1-1Z" dates
// without a known system time zone; repairQtPdfInfo fixes both in place.
void documentInformation() {
    Fixture f;
    const auto exported = renderSheetsPdf(f.doc, {});
    const auto problem =
        pdfcheck::infoProblem(exported.bytes, "SketchyUp drawing sheets", "SketchyUp");
    check(problem.empty(), "Well-formed PDF document information: " + problem);
    auto unchanged = exported.bytes;
    check(!repairQtPdfInfo(unchanged, QDateTime::currentDateTimeUtc()) &&
              unchanged == exported.bytes,
          "A well-formed PDF is left byte for byte unchanged");
    const auto broken = pdfcheck::qt680Info(exported.bytes);
    check(broken.size() == exported.bytes.size(), "Qt 6.8.0 shaped document information");
    check(pdfcheck::infoProblem(broken, "SketchyUp drawing sheets", "SketchyUp")
              .starts_with("Dictionary key must be a name object"),
          "The checker rejects the unclosed Qt 6.8.0 dictionary");
    auto repaired = broken;
    const QDateTime created(QDate(2026, 10, 15), QTime(22, 9, 18), QTimeZone(QTimeZone::UTC));
    check(repairQtPdfInfo(repaired, created) && repaired.size() == broken.size(),
          "The Qt 6.8.0 dictionary is repaired at the same length");
    const auto after = pdfcheck::infoProblem(repaired, "SketchyUp drawing sheets", "SketchyUp");
    check(after.empty(), "Repaired document information: " + after);
    check(repaired.count("(D:20261015220918Z)") == 2 && !repaired.contains("-1-1-1Z"),
          "Invalid dates become the export time in UTC");
    check(QByteArrayView(repaired).sliced(repaired.indexOf("/Pages")) ==
              QByteArrayView(exported.bytes).sliced(exported.bytes.indexOf("/Pages")),
          "Nothing after the document information changes");
    std::cout << "document information: ok\n";
}

void realTextAndClipping() {
    Fixture f;
    const auto exported = renderSheetsPdf(f.doc, {});
    // Real text: an embedded TrueType subset with a Unicode map, not glyph outlines.
    check(exported.bytes.contains("/FontFile2") && exported.bytes.contains("/ToUnicode"),
          "Text is real text with embedded TrueType font subsets");
    check(exported.report["outlinedFonts"].toArray().isEmpty(), "No font fell back to outlines");
    // Subsets: the faces themselves are hundreds of KiB each.
    check(exported.bytes.size() < 200 * 1024, "Fonts are embedded as subsets");
    // Clipping: the 1:20 detail's content is clipped to its frame in the display list.
    const auto sheet = prepareDrawingSheet(f.doc, f.a102);
    bool content = false;
    for (const auto &p : sheet.primitives)
        if (p.source == f.detail && p.layer == DrawingSheetLayer::View) {
            content = true;
            check(p.clipMm == QRectF(20, 20, 60, 60), "Detail clipped to its frame");
        }
    check(content, "Detail paints");
    std::cout << "real text and clipping: ok\n";
}

void preflightFindings() {
    Fixture f;
    // Clean: no blocking findings; the scale summary names both scales.
    auto clean = preflightSheetExport(f.doc, {});
    check(!clean.blocked(), "Accepted views do not block");
    const auto *scales = find(clean, "scaleSummary");
    check(scales && scales->severity == SheetExportSeverity::Info &&
              scales->message == "2 views at measured scale: 1 at 1:50 and 1 at 1:20. Print at "
                                 "100% to measure from paper.",
          "Scale summary: " + (scales ? scales->message : std::string()));
    // A third view at 1:50: "3 views at measured scale: 2 at 1:50 and 1 at 1:20".
    createDrawingView(f.doc, viewRecord(f.a101, f.plan, "Copy", {240, 20, 150, 100}));
    acceptAll(f.doc);
    scales = find(clean = preflightSheetExport(f.doc, {}), "scaleSummary");
    check(scales &&
              scales->message.starts_with("3 views at measured scale: 2 at 1:50 and 1 at 1:20"),
          "Scale summary orders by count");

    // Changed since accepted: blocks; allowChanged accepts it as a warning.
    f.doc.addWire(0, {.8, .3, .1}, {1.2, .3, .1}); // Inside all three views.
    auto changed = preflightSheetExport(f.doc, {});
    const auto *stale = find(changed, "viewChanged");
    check(changed.blocked() && stale && stale->severity == SheetExportSeverity::Blocking &&
              stale->views.size() == 3 &&
              stale->message.starts_with("3 views changed since accepted: A-101 view 1"),
          "Changed views block: " + (stale ? stale->message : std::string()));
    check(rejects([&] { renderSheetsPdf(f.doc, {}); }) == "EXPORT_BLOCKED",
          "Export refuses while blocked");
    SheetExportOptions asIs;
    asIs.allowChanged = true;
    auto accepted = preflightSheetExport(f.doc, asIs);
    stale = find(accepted, "viewChanged");
    check(!accepted.blocked() && stale && stale->severity == SheetExportSeverity::Warning &&
              stale->optedIn,
          "allowChanged turns the finding into a warning");
    const auto exported = renderSheetsPdf(f.doc, asIs);
    check(exported.report["exportedAsIs"].toArray().size() == 3,
          "Report names views exported as-is");
    acceptAll(f.doc);

    // Broken: a deleted scene blocks; allowBroken exports an empty, labelled frame.
    const auto gone = createScene(f.doc, "South elevation", planScene());
    const auto broken =
        createDrawingView(f.doc, viewRecord(f.a102, gone, "Section B-B", {20, 100, 150, 80}));
    eraseScene(f.doc, gone);
    auto blocked = preflightSheetExport(f.doc, {});
    const auto *bad = find(blocked, "viewBroken");
    check(blocked.blocked() && bad && bad->views == std::vector<Id>{broken} &&
              bad->sheets == std::vector<Id>{f.a102} &&
              bad->message == "A-102, view 2 “Section B-B”: its scene was deleted.",
          "Broken view blocks: " + (bad ? bad->message : std::string()));
    try {
        renderSheetsPdf(f.doc, {});
        check(false, "Blocked export must throw");
    } catch (const SheetExportError &error) {
        check(error.code() == "EXPORT_BLOCKED" && error.preflight() &&
                  find(*error.preflight(), "viewBroken"),
              "Blocked export carries its findings");
    }
    SheetExportOptions empty;
    empty.allowBroken = true;
    auto allowed = preflightSheetExport(f.doc, empty);
    check(!allowed.blocked() && find(allowed, "viewBroken")->optedIn, "allowBroken unblocks");
    const auto withEmpty = renderSheetsPdf(f.doc, empty);
    check(withEmpty.report["exportedEmpty"].toArray() == QJsonArray{QString::number(broken)},
          "Report names the empty frame");
    eraseDrawingView(f.doc, broken);

    // A missing font names its substitute.
    DrawingTextItem t;
    t.frame = {30, 240, 80, 20};
    t.text = "Revision notes";
    t.font = {"No Such Font R092", 10};
    const auto text = createDrawingItem(f.doc, f.a101, t);
    auto fonts = preflightSheetExport(f.doc, {});
    // (Where "DejaVu Sans" itself is not installed, it is reported too.)
    const SheetExportFinding *substitution = nullptr;
    for (const auto &finding : fonts.findings)
        if (finding.code == "fontSubstituted" && finding.detail["requested"] == "No Such Font R092")
            substitution = &finding;
    check(!fonts.blocked() && substitution &&
              substitution->severity == SheetExportSeverity::Warning &&
              substitution->items == std::vector<Id>{text} &&
              substitution->detail["requested"] == "No Such Font R092" &&
              !substitution->detail["used"].toString().isEmpty() &&
              substitution->message.find("will be replaced by " +
                                         substitution->detail["used"].toString().toStdString()) !=
                  std::string::npos,
          "Missing font reported with its substitute: " +
              (substitution ? substitution->message : std::string()));

    {
        // View and item IDs come from separate allocators: an item whose ID equals a view's is
        // still reported as an item (R092.b's Choose another font selects it).
        Document doc;
        doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 1, 0}, {0, 1, 0}}}, "Slab");
        const auto scene = createScene(doc, "Plan", planScene());
        const auto sheet = createDrawingSheet(doc, "Only", {420, 297}, "A-1", 10);
        const auto view =
            createDrawingView(doc, viewRecord(sheet, scene, "Plan", {20, 20, 200, 150}));
        DrawingTextItem note;
        note.frame = {250, 20, 80, 20};
        note.text = "Same ID";
        note.font = {"No Such Font R092", 10};
        const auto item = createDrawingItem(doc, sheet, note);
        check(item == view, "The fixture gives the item its view's ID");
        acceptAll(doc);
        const auto preflight = preflightSheetExport(doc, {});
        const SheetExportFinding *same = nullptr;
        for (const auto &finding : preflight.findings)
            if (finding.code == "fontSubstituted" &&
                finding.detail["requested"] == "No Such Font R092")
                same = &finding;
        check(same && same->items == std::vector<Id>{item} && same->views.empty(),
              "A substituted item is an item even when a view has its ID");
    }

    // Raster: a perspective view is counted; without a renderer it blocks.
    const auto eye = createScene(f.doc, "Eye", planScene(false));
    const auto garden =
        createDrawingView(f.doc, viewRecord(f.a101, eye, "Garden view", {240, 140, 150, 60}));
    acceptAll(f.doc);
    auto raster = preflightSheetExport(f.doc, {});
    const auto *rasters = find(raster, "rasterViews");
    check(rasters && rasters->views == std::vector<Id>{garden} &&
              rasters->message == "1 view will be raster: “Garden view” (perspective) at "
                                  "300 dpi. Everything else is vector.",
          "Raster count: " + (rasters ? rasters->message : std::string()));
    check(raster.blocked() && find(raster, "rasterUnavailable"),
          "A raster view without a renderer blocks");
    SheetExportOptions rendered;
    rendered.rasterRenderer = fakeRaster;
    const auto withRaster = renderSheetsPdf(f.doc, rendered);
    const auto list = withRaster.report["rasters"].toArray();
    check(list.size() == 1 && list[0].toObject()["dpi"] == 300 &&
              list[0].toObject()["widthPx"] == int(std::lround(150 / 25.4 * 300)) &&
              withRaster.bytes.contains("/Subtype /Image"),
          "Raster view painted at 300 dpi");

    // The inspection query gives the same findings.
    QJsonObject request{{"apiVersion", 1},
                        {"documentId", QString::fromStdString(f.doc.identity())},
                        {"expectedRevision", QString::number(f.doc.revision())},
                        {"query", "drawing_sheets.export_preflight"},
                        {"pages", "A-101"},
                        {"rasterCapture", true}};
    const auto data = inspectDocument(f.doc, request)["data"].toObject();
    check(data["pageCount"] == 1 && data["blocked"] == false &&
              data["findings"].toArray().size() > 0,
          "Inspection query runs the shared preflight");
    std::cout << "preflight findings: ok\n";
}

void atomicAndReadOnly() {
    QTemporaryDir scratch;
    check(scratch.isValid(), "Scratch folder");
    Fixture f;
    const auto before = encodeContainer(f.doc);
    const auto revision = f.doc.revision();
    // Exporting never edits the document.
    const auto path = scratch.filePath("set.pdf");
    const auto exported = exportSheetsPdf(f.doc, {}, path);
    check(QFile(path).size() == exported.bytes.size(), "Published");
    check(f.doc.revision() == revision && encodeContainer(f.doc) == before,
          "Export leaves the document unchanged");
    // An existing file is never replaced.
    check(rejects([&] { exportSheetsPdf(f.doc, {}, path); }) == "INVALID_PATH",
          "Existing destination refused");
    QFile published(path);
    check(published.open(QIODevice::ReadOnly) && published.readAll() == exported.bytes,
          "Existing file untouched");
    // A missing parent and a blocked export leave nothing behind.
    check(rejects([&] { exportSheetsPdf(f.doc, {}, scratch.filePath("no/set.pdf")); }) ==
              "INVALID_PATH",
          "Missing parent refused");
    const auto gone = createScene(f.doc, "Gone", planScene());
    createDrawingView(f.doc, viewRecord(f.a102, gone, "Lost", {20, 100, 150, 80}));
    eraseScene(f.doc, gone);
    const auto blockedPath = scratch.filePath("blocked.pdf");
    check(rejects([&] { exportSheetsPdf(f.doc, {}, blockedPath); }) == "EXPORT_BLOCKED",
          "Blocked export refused");
    check(!QFileInfo::exists(blockedPath), "Blocked export leaves no file");
    check(QDir(scratch.path()).entryList(QDir::Files | QDir::Hidden | QDir::System) ==
              QStringList{"set.pdf"},
          "No partial or temporary files");
    std::cout << "atomic and read-only: ok\n";
}

// Poppler: measured scale and clipping from the rasterized PDF, and extractable text.
int popplerChecks() {
    const auto executable = qEnvironmentVariable("SKETCHYUP_PDF_TEST");
    if (executable.isEmpty())
        return 77;
    QTemporaryDir scratch;
    check(scratch.isValid(), "Scratch folder");
    Fixture f;
    const auto file = scratch.filePath("set.pdf");
    exportSheetsPdf(f.doc, {}, file);
    auto run = [](const QString &program, const QStringList &arguments) {
        QProcess process;
        process.start(program, arguments);
        check(process.waitForStarted(10000) && process.waitForFinished(60000) &&
                  process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0,
              "Poppler tool " + program.toStdString());
        return process.readAllStandardOutput();
    };
    // Page 1 at 254 dpi: 10 pixels per millimetre.
    const auto prefix = scratch.filePath("page1");
    run(executable, {"-f", "1", "-l", "1", "-singlefile", "-r", "254", "-png", file, prefix});
    const QImage page(prefix + ".png");
    // Whole-point page boxes: within 0.5 pt (1.8 px at 254 dpi) of the paper.
    auto paper = [](const QImage &image, int w, int h) {
        return std::abs(image.width() - w) <= 3 && std::abs(image.height() - h) <= 3;
    };
    check(paper(page, 4200, 2970),
          "Poppler renders A3 landscape at actual size: " + std::to_string(page.width()) + "x" +
              std::to_string(page.height()));
    auto dark = [](const QImage &image, int x, int y) {
        const auto c = image.pixelColor(x, y);
        return c.red() < 128 && c.green() < 128 && c.blue() < 128;
    };
    // The slab's vertical edges cross y = 95 mm at x = 100 and 140 mm. Their stroke
    // centres are 40 mm apart: 2 m at 1:50, to within 0.1 mm.
    auto centre = [&](const QImage &image, int y, int from, int to) {
        double sum = 0;
        int count = 0;
        for (int x = from; x < to; ++x)
            if (dark(image, x, y)) {
                sum += x + .5;
                ++count;
            }
        check(count > 0, "Edge found on the measured row");
        return sum / count;
    };
    const auto left = centre(page, 950, 950, 1050), right = centre(page, 950, 1350, 1450);
    const auto measured = (right - left) / 10;
    check(std::abs(measured - 40) <= .1,
          "2 m at 1:50 measures 40 mm on paper, got " + std::to_string(measured));
    // Text is real and extractable.
    const auto text =
        QString::fromUtf8(run(QFileInfo(executable).dir().filePath("pdftotext"), {file, "-"}));
    for (const auto *expected :
         {"2000", "General notes", "House", "Ground floor plan", "1:50", "A-101", "1 of 2"})
        check(text.contains(expected), std::string("Extractable text ") + expected +
                                           " in: " + text.simplified().toStdString());
    const auto fonts =
        QString::fromUtf8(run(QFileInfo(executable).dir().filePath("pdffonts"), {file}));
    // Qt embeds subsets without the "ABCDEF+" name tag, so pdffonts' "sub" column says
    // no; realTextAndClipping checks the subset size.
    check(QRegularExpression(R"(TrueType\s+Identity-H\s+yes\s+(yes|no)\s+yes)")
                  .match(fonts)
                  .hasMatch() &&
              !fonts.contains("Type 3"),
          "Fonts are embedded with Unicode maps: " + fonts.toStdString());
    // The reconciliation report is recoverable from the PDF itself.
    run(QFileInfo(executable).dir().filePath("pdfdetach"),
        {"-saveall", "-o", scratch.path(), file});
    QFile attached(scratch.filePath("sheet-export-report.json"));
    check(attached.open(QIODevice::ReadOnly), "Report attached as sheet-export-report.json");
    const auto report = QJsonDocument::fromJson(attached.readAll()).object();
    check(report["pageCount"] == 2 &&
              report["pages"].toArray()[0].toObject()["views"].toArray()[0].toObject()["scale"] ==
                  "1:50",
          "Attached report lists pages and view scales");
    // Clipping: page 2's 1:20 detail frame spans x and y 20–80 mm. The slab's 100 mm
    // edges overflow it, but nothing is drawn outside the frame apart from the caption
    // below it and the margin frame.
    const auto prefix2 = scratch.filePath("page2");
    run(executable, {"-f", "2", "-l", "2", "-singlefile", "-r", "254", "-png", file, prefix2});
    const QImage detail(prefix2 + ".png");
    check(paper(detail, 2100, 2970), "Poppler renders A4 portrait");
    int inside = 0, outside = 0;
    QRect stray;
    for (int y = 110; y < 1000; ++y)
        for (int x = 110; x < 1950; ++x) { // Inside the margin frame.
            if (!dark(detail, x, y))
                continue;
            const bool inFrame = x >= 198 && x <= 802 && y >= 198 && y <= 802;
            const bool caption = y > 802 && y < 950;
            (inFrame ? inside : caption ? inside : outside) += 1;
            if (!inFrame && !caption)
                stray |= QRect(x, y, 1, 1);
        }
    check(inside > 0 && outside == 0,
          "Detail content clipped to its frame: " + std::to_string(outside) +
              " pixels outside, in " + std::to_string(stray.x()) + "," + std::to_string(stray.y()) +
              " " + std::to_string(stray.width()) + "x" + std::to_string(stray.height()));
    std::cout << "poppler scale, text and clipping: ok\n";
    return 0;
}

#ifdef CLI_PATH
struct Cli {
    int code;
    QJsonObject out, err;
};
Cli cli(const QStringList &arguments) {
    QProcess process;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.remove("DISPLAY");
    environment.remove("WAYLAND_DISPLAY");
    environment.insert("QT_QPA_PLATFORMTHEME", "unusable-theme");
    process.setProcessEnvironment(environment);
    process.start(CLI_PATH, arguments);
    check(process.waitForStarted(10000) && process.waitForFinished(60000), "CLI runs");
    auto parse = [](const QByteArray &bytes) {
        return QJsonDocument::fromJson(bytes.trimmed().split('\n').last()).object();
    };
    return {process.exitCode(), parse(process.readAllStandardOutput()),
            parse(process.readAllStandardError())};
}
void cliRoundTrip() {
    QTemporaryDir scratch;
    check(scratch.isValid(), "Scratch folder");
    Fixture f;
    const auto model = scratch.filePath("house.sketchyup");
    auto copy = f.doc;
    saveDocument(copy, model);
    const auto pdf = scratch.filePath("set.pdf");
    auto preflight = cli({"--export-sheets", pdf, "--input", model, "--preflight-only"});
    check(preflight.code == 0 && preflight.out["status"] == "preflight" &&
              preflight.out["preflight"].toObject()["blocked"] == false &&
              preflight.out["preflight"].toObject()["pageCount"] == 2 && !QFileInfo::exists(pdf),
          "CLI preflight only");
    auto exported = cli({"--export-sheets", pdf, "--input", model, "--pages", "A-102"});
    check(exported.code == 0 && exported.out["status"] == "exported" &&
              exported.out["exportReport"].toObject()["pageCount"] == 1 && QFileInfo::exists(pdf),
          "CLI exports the selected page");
    QFile file(pdf);
    check(file.open(QIODevice::ReadOnly) &&
              QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex() ==
                  exported.out["exportReport"].toObject()["sha256"].toString().toLatin1(),
          "CLI report hash matches the file");
    QFile written(pdf);
    check(written.open(QIODevice::ReadOnly), "CLI PDF readable");
    const auto info =
        pdfcheck::infoProblem(written.readAll(), "SketchyUp drawing sheets", "SketchyUp");
    check(info.empty(), "CLI PDF document information is well formed: " + info);
    auto again = cli({"--export-sheets", pdf, "--input", model});
    check(again.code == 1 && again.err["status"] == "failed", "CLI never replaces a file");
    // A broken view blocks until --allow-broken.
    auto broken = f.doc;
    const auto gone = createScene(broken, "Gone", planScene());
    createDrawingView(broken, viewRecord(f.a102, gone, "Lost", {20, 100, 150, 80}));
    eraseScene(broken, gone);
    const auto brokenModel = scratch.filePath("broken.sketchyup");
    saveDocument(broken, brokenModel);
    const auto blockedPdf = scratch.filePath("blocked.pdf");
    auto blocked = cli({"--export-sheets", blockedPdf, "--input", brokenModel});
    check(blocked.code == 1 && blocked.err["code"] == "EXPORT_BLOCKED" &&
              blocked.err["preflight"].toObject()["findings"].toArray()[0].toObject()["code"] ==
                  "viewBroken" &&
              !QFileInfo::exists(blockedPdf),
          "CLI refuses a blocked export and prints its findings");
    auto opted = cli({"--export-sheets", blockedPdf, "--input", brokenModel, "--allow-broken"});
    check(opted.code == 0 &&
              opted.out["exportReport"].toObject()["exportedEmpty"].toArray().size() == 1,
          "CLI --allow-broken exports an empty, labelled frame");
    auto stray = cli({"--input", model, "--allow-changed"});
    check(stray.code == 1 && stray.err["code"] == "INVALID_REQUEST",
          "Sheet export options require --export-sheets");
    auto badRange =
        cli({"--export-sheets", scratch.filePath("x.pdf"), "--input", model, "--pages", "Z-9"});
    check(badRange.code == 1 && badRange.err["code"] == "INVALID_REQUEST", "CLI page range error");
    std::cout << "CLI round trip: ok\n";
}
#endif
} // namespace

// ADR 0164 §3 addendum: paper anchors follow an edge split inside the model edit, so the
// exported page is unchanged except that its annotations stay correct: the dimension and
// the label draw exactly as before instead of breaking, and preflight finds nothing.
void anchorsSurviveEdgeSplit() {
    Fixture f;
    auto &doc = f.doc;
    const auto &body = *doc.bodies().at(f.face);
    Id bottom{}, top{};
    for (const auto &[id, edge] : body.topology.edges) {
        const auto a = body.surface.vertices.at(edge.a), b = body.surface.vertices.at(edge.b);
        if (std::abs(a.y) < 1e-9 && std::abs(b.y) < 1e-9)
            bottom = id;
        if (std::abs(a.y - 1) < 1e-9 && std::abs(b.y - 1) < 1e-9)
            top = id;
    }
    check(bottom && top, "The slab's bottom and top edges");
    DrawingDimensionItem depth;
    depth.view = f.planView;
    depth.anchors = {edgeAnchor(doc, f.face, bottom, .5), edgeAnchor(doc, f.face, top, .5)};
    depth.orientation = DrawingDimensionOrientation::Vertical;
    depth.offsetMm = 12;
    depth.lastMm = {DrawingPointMm{120, 105}, DrawingPointMm{120, 85}};
    const auto dimension = createDrawingItem(doc, f.a101, depth);
    DrawingLabelItem leader;
    leader.text = "Slab edge";
    leader.textAt = {60, 160};
    leader.target.kind = DrawingLabelTargetKind::View;
    leader.target.view = f.planView;
    leader.target.anchor = edgeAnchor(doc, f.face, bottom, .25);
    leader.target.lastMm = {105, 105};
    const auto label = createDrawingItem(doc, f.a101, leader);
    acceptAll(doc);
    auto items = [&] {
        std::vector<std::tuple<Id, QString, QString, QRectF, QRgb>> result;
        for (const auto &p : prepareDrawingSheet(doc, f.a101).primitives)
            if (p.layer == DrawingSheetLayer::Item && (p.source == dimension || p.source == label))
                result.emplace_back(
                    p.source, p.text,
                    QString::fromLatin1(QByteArray::number(p.path.length(), 'f', 6)),
                    p.path.boundingRect(), p.color.rgba());
        return result;
    };
    const auto before = items();
    check(!before.empty(), "The dimension and the label paint");
    check(!find(preflightSheetExport(doc, {}), "itemBroken"), "Nothing is broken before");
    // Split the bottom edge under the dimension's endpoint, away from the leader's tip.
    doc.splitEdge(f.face, bottom, .5);
    acceptAll(doc);
    const auto preflight = preflightSheetExport(doc, {});
    check(!find(preflight, "itemBroken") && !preflight.blocked(),
          "Preflight finds no broken item after the split");
    check(items() == before, "The annotations draw exactly as before the split");
    const auto exported = renderSheetsPdf(doc, {});
    check(exported.bytes.startsWith("%PDF-") && evaluateDrawingItem(doc, dimension).text == "1000",
          "The page exports with the dimension's value, not a missing reference");
    std::cout << "paper anchors survive an edge split in the export: ok\n";
}

int main(int argc, char **argv) {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    try {
        if (app.arguments().contains("--pdf-real"))
            return popplerChecks();
        pagesAndPaperSizes();
        documentInformation();
        realTextAndClipping();
        preflightFindings();
        atomicAndReadOnly();
        anchorsSurviveEdgeSplit();
#ifdef CLI_PATH
        cliRoundTrip();
#endif
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
