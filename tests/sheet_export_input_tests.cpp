#include "app/sheet_canvas.hpp"
#include "app/sheet_evaluation.hpp"
#include "app/sheet_export_dialog.hpp"
#include "app/sheet_workspace.hpp"
#include "app/sheets_tray.hpp"
#include "app/surface_format.hpp"
#include "app/viewport.hpp"
#include "app/window.hpp"
#include "automation/commands.hpp"
#include "core/drawing_item_records.hpp"
#include "core/drawing_sheets.hpp"
#include "core/scenes.hpp"
#include "io/document_io.hpp"
#include "io/drawing_sheet_paint.hpp"
#include "io/drawing_view.hpp"
#include "qt_warning_guard.hpp"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFrame>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPageLayout>
#include <QPlainTextEdit>
#include <QPrinter>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <cmath>
#include <functional>
#include <iostream>
// R092.b: File › Export sheets… and Print… in the desktop (ADR 0164 "R092 export").
//
// A drawing set with every kind of finding (a broken view, changed views, a missing font, a
// perspective raster view and measured scales) is exported and printed through the real
// preflight dialog, driven by keys sent to the focused widget:
//   - the dialog opens with grouped findings, and Export PDF is disabled while anything blocks;
//   - each opt-in check box unblocks its finding; the list re-runs when the document changes;
//   - Go to view, Review and accept, and Choose another font close the dialog and select the
//     view or item, accepting nothing;
//   - Cancel writes nothing; Export writes a PDF (page count, raster image, and with Poppler the
//     page count and real text) and shows the report sheet;
//   - raster views come from the viewport, so rasterUnavailable never appears;
//   - Print paints each page at 100% on a PDF-backed QPrinter (1:1, measured with Poppler) and
//     warns, without scaling, when the paper is smaller than a sheet;
//   - focus returns to the canvas, or in Model to the viewport, after every close.
using namespace sketchy;
namespace {
void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
QWidget *focused() { return QApplication::focusWidget(); }
std::string identity(const QWidget *widget) {
    if (!widget)
        return "none";
    return std::string(widget->metaObject()->className()) + "/" +
           widget->objectName().toStdString();
}
SceneSnapshot camera(double yaw, double pitch, bool orthographic, Vec3 target) {
    SceneSnapshot snapshot;
    snapshot.camera = SceneCamera{};
    snapshot.camera->target = target;
    snapshot.camera->yaw = yaw;
    snapshot.camera->pitch = pitch;
    snapshot.camera->distance = 30;
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
void acceptAll(Document &doc) {
    std::map<Id, DrawingViewAcceptance> stamps;
    for (const auto &[view, record] : doc.drawingViews()) {
        const auto status = evaluateDrawingView(doc, view);
        if (!status.fingerprint.empty())
            stamps[view] = {doc.revision(), status.fingerprint};
    }
    acceptDrawingViews(doc, stamps);
}
struct Fixture {
    Id a101{}, a201{}, a301{}, plan{}, south{}, garden{}, section{}, note{};
};
// "house": a 10 × 8 m slab with two walls. A-101 has the plan at 1:50, a note in a font that is
// not installed ("ISOCPEUR") and a title block; A-201 the south elevation at 1:50 and a
// perspective "Garden view"; A-301 "Section B-B", whose scene is deleted. Every view is
// accepted, then a model edit changes the plan and the elevation.
Fixture writeFixture(const QString &path) {
    Document doc;
    auto box = [&](double x, double y, double w, double d, double h, const char *name) {
        x -= 5;
        y -= 4;
        const auto id =
            doc.addFace({{{x, y, 0}, {x + w, y, 0}, {x + w, y + d, 0}, {x, y + d, 0}}}, name);
        doc.extrude(id, doc.bodies().at(id)->surface.faces.begin()->first, h);
        return id;
    };
    box(0, 0, 10, 8, .2, "Slab");
    box(0, 7.7, 10, .3, 2.7, "North wall");
    box(0, 0, .3, 7.7, 2.7, "West wall");
    Fixture f;
    const auto planScene = createScene(doc, "Ground plan", camera(0, 90, true, {0, 0, 0}));
    const auto southScene = createScene(doc, "South elevation", camera(-90, 0, true, {0, 0, 1.4}));
    const auto gardenScene = createScene(doc, "Garden view", camera(-60, 25, false, {0, 0, 1}));
    const auto goneScene = createScene(doc, "Section B-B", camera(0, 0, true, {0, 0, 1.4}));
    f.a101 = createDrawingSheet(doc, "Ground floor", {420, 297}, "A-101", 10);
    f.a201 = createDrawingSheet(doc, "Elevations", {420, 297}, "A-201", 10);
    f.a301 = createDrawingSheet(doc, "Sections", {420, 297}, "A-301", 10);
    f.plan = createDrawingView(
        doc, viewRecord(f.a101, planScene, "Ground floor plan", {20, 20, 260, 200}));
    f.south = createDrawingView(
        doc, viewRecord(f.a201, southScene, "South elevation", {20, 20, 260, 120}));
    f.garden =
        createDrawingView(doc, viewRecord(f.a201, gardenScene, "Garden view", {300, 20, 90, 60}));
    f.section =
        createDrawingView(doc, viewRecord(f.a301, goneScene, "Section B-B", {20, 20, 200, 120}));
    DrawingTextItem note;
    note.frame = {300, 30, 100, 20};
    note.text = "General notes";
    note.font = {"ISOCPEUR", 10};
    f.note = createDrawingItem(doc, f.a101, note);
    DrawingTitleBlockItem block;
    block.frame = {300, 230, 110, 57};
    block.fields = {"House", "Ground floor plan", "2026-10-15", "A"};
    createDrawingItem(doc, f.a101, block);
    acceptAll(doc);
    eraseScene(doc, goneScene);
    doc.addWire(0, {-2, -1, .3}, {2, -1, .3}); // Seen in the plan and the elevation.
    saveDocument(doc, path);
    return f;
}

// Runs `body` on the named dialog once it shows (a modal dialog blocks its opener).
class DialogWatch {
  public:
    DialogWatch(QString name, std::function<void(QDialog &)> body)
        : name_(std::move(name)), body_(std::move(body)) {
        QObject::connect(&timer_, &QTimer::timeout, [this] {
            for (auto *widget : QApplication::topLevelWidgets())
                if (auto *dialog = qobject_cast<QDialog *>(widget);
                    dialog && dialog->isVisible() && dialog->objectName() == name_) {
                    timer_.stop();
                    seen_ = true;
                    try {
                        body_(*dialog);
                    } catch (...) {
                        failure_ = std::current_exception();
                        if (dialog->isVisible())
                            dialog->reject();
                    }
                    return;
                }
        });
        timer_.start(20);
    }
    void finish() {
        check(QTest::qWaitFor([&] { return seen_; }, 10000),
              "Dialog shown: " + name_.toStdString());
        if (failure_)
            std::rethrow_exception(failure_);
    }

  private:
    QString name_;
    std::function<void(QDialog &)> body_;
    QTimer timer_;
    bool seen_{};
    std::exception_ptr failure_;
};
void activate(QDialog &dialog) {
    check(QTest::qWaitForWindowExposed(&dialog), "Dialog exposed");
    dialog.activateWindow();
    check(QTest::qWaitForWindowFocused(&dialog), "Dialog has keyboard focus");
    check(QTest::qWaitFor([&] {
              auto *f = focused();
              return f && (f == &dialog || dialog.isAncestorOf(f));
          }),
          "Dialog owns keyboard focus");
}
// Tab until `name` has focus; fails when focus leaves the dialog.
QWidget *tabTo(QDialog &dialog, const QString &name) {
    for (int i = 0; i < 60; ++i) {
        auto *f = focused();
        check(f && (f == &dialog || dialog.isAncestorOf(f)),
              "Tab stays in the dialog looking for " + name.toStdString() + "; focus " +
                  identity(f) + ", active window " + identity(QApplication::activeWindow()));
        if (f->objectName() == name)
            return f;
        QTest::keyClick(f, Qt::Key_Tab);
    }
    std::string chain;
    for (int i = 0; i < 16; ++i) {
        chain += " " + identity(focused());
        QTest::keyClick(focused(), Qt::Key_Tab);
    }
    throw std::runtime_error("Tab reaches " + name.toStdString() + "; the chain:" + chain);
}
void space(QDialog &dialog, const QString &name) {
    auto *control = tabTo(dialog, name);
    QTest::keyClick(control, Qt::Key_Space);
}
SheetExportDialog &settled(QDialog &dialog) {
    auto *export_ = dynamic_cast<SheetExportDialog *>(&dialog);
    check(export_, "The dialog is the sheet export dialog");
    check(QTest::qWaitFor([&] { return !export_->checking() && export_->preflight(); }, 30000),
          "The preflight settles");
    QTest::qWait(30);
    return *export_;
}
std::set<QString> blockCodes(QDialog &dialog) {
    std::set<QString> codes;
    for (auto *block : dialog.findChildren<QFrame *>("sheetExportFinding"))
        if (block->isVisible())
            codes.insert(block->property("code").toString());
    return codes;
}
QFrame *block(QDialog &dialog, const QString &code) {
    for (auto *frame : dialog.findChildren<QFrame *>("sheetExportFinding"))
        if (frame->isVisible() && frame->property("code") == code)
            return frame;
    throw std::runtime_error("Finding block shown: " + code.toStdString());
}
QString blockText(QDialog &dialog, const QString &code) {
    return block(dialog, code)->findChild<QLabel *>("sheetExportFindingText")->accessibleName();
}
QPushButton *confirm(QDialog &dialog) {
    auto *button = dialog.findChild<QPushButton *>("sheetExportConfirm");
    check(button, "The confirming button exists");
    return button;
}
QString hint(QDialog &dialog) { return dialog.findChild<QLabel *>("sheetExportHint")->text(); }
QAction *action(Window &window, const char *id) {
    auto *result = window.findChild<QAction *>(id);
    check(result, std::string("Action exists: ") + id);
    return result;
}
size_t entries(const Document &doc) { return doc.history(0, 1).position; }
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
bool nearPt(double points, double mm) { return std::abs(points - mm * 72 / 25.4) <= .6; }
QByteArray read(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Read " + path.toStdString());
    return file.readAll();
}
QByteArray tool(const QString &name, const QStringList &arguments) {
    QProcess process;
    process.start(QStandardPaths::findExecutable(name), arguments);
    check(process.waitForStarted(10000) && process.waitForFinished(60000) &&
              process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0,
          "Poppler tool " + name.toStdString());
    return process.readAllStandardOutput();
}
bool poppler() {
    return !QStandardPaths::findExecutable("pdftotext").isEmpty() &&
           !QStandardPaths::findExecutable("pdfinfo").isEmpty() &&
           !QStandardPaths::findExecutable("pdftoppm").isEmpty();
}
QString screenshotDir() { return qEnvironmentVariable("SKETCHYUP_SCREENSHOT_DIR"); }
QString currentTheme = "light";
void shoot(QWidget &widget, const QString &name) {
    const auto dir = screenshotDir();
    if (dir.isEmpty())
        return;
    QDir().mkpath(dir);
    QTest::qWait(60);
    widget.grab().save(
        QString("%1/%2-%3-%4x.png").arg(dir, name, currentTheme).arg(widget.devicePixelRatioF()));
}

struct Results {
    QJsonObject findings, optIns, navigation, exported, printed;
};

Results run(Window &window, const QString &files, const Fixture &f) {
    Results results;
    auto &doc = window.document();
    auto *sheets = window.sheetWorkspace();
    auto *canvas = sheets->canvas();
    window.setWorkspace(Window::Workspace::Sheets);
    sheets->setCurrentSheet(f.a101);
    auto canvasFocused = [&](const char *after) {
        check(QTest::qWaitFor([&] { return window.isActiveWindow() && focused() == canvas; }),
              std::string("Focus returns to the canvas after ") + after + "; got " +
                  identity(focused()));
    };
    auto focusCanvas = [&] {
        canvas->setFocus(Qt::OtherFocusReason);
        check(QTest::qWaitFor([&] { return canvas->hasFocus(); }), "Canvas focus");
    };
    focusCanvas();
    const auto history = entries(doc);
    const auto acceptedBefore = [&] {
        std::map<Id, std::optional<DrawingViewAcceptance>> stamps;
        for (const auto &[id, record] : doc.drawingViews())
            stamps[id] = record->accepted;
        return stamps;
    }();

    // ---- The dialog opens with grouped findings; anything blocking disables Export --------
    std::cerr << "[export] findings" << std::endl;
    check(action(window, "sheet.export")->isVisible() &&
              action(window, "sheet.export")->isEnabled() &&
              !action(window, "file.exportSheets")->isVisible() &&
              action(window, "sheet.print")->isVisible(),
          "Sheets shows File › Export sheets… and Print…");
    {
        DialogWatch watch("sheetExportDialog", [&](QDialog &dialog) {
            activate(dialog);
            auto &d = settled(dialog);
            check(dialog.windowTitle() == "Export sheets as PDF", "Title");
            const auto subline = dialog.findChild<QLabel *>("sheetExportSubline")->text();
            check(subline == "house · 3 pages · printed at actual size",
                  "Subline: " + subline.toStdString());
            const auto codes = blockCodes(dialog);
            for (const auto *code :
                 {"viewBroken", "viewChanged", "fontSubstituted", "scaleSummary", "rasterViews"})
                check(codes.contains(code), std::string("Finding shown: ") + code);
            check(!codes.contains("rasterUnavailable"),
                  "The viewport renders raster views: no rasterUnavailable");
            check(block(dialog, "viewBroken")->property("tone") == "failure" &&
                      block(dialog, "viewChanged")->property("tone") == "warning" &&
                      block(dialog, "fontSubstituted")->property("tone") == "warning" &&
                      block(dialog, "scaleSummary")->property("tone") == "info",
                  "Blocks carry their tone");
            const auto broken = blockText(dialog, "viewBroken");
            check(broken == "1 broken view blocks export. A-301, view 1 “Section B-B”: its scene "
                            "was deleted.",
                  "Broken view text: " + broken.toStdString());
            const auto changed = blockText(dialog, "viewChanged");
            check(changed.startsWith("3 views changed since accepted. A-101 view 1 “Ground floor "
                                     "plan”"),
                  "Changed text: " + changed.toStdString());
            const auto font = blockText(dialog, "fontSubstituted");
            check(font.startsWith("1 font is not installed. “ISOCPEUR”"),
                  "Font text: " + font.toStdString());
            check(blockText(dialog, "rasterViews")
                      .startsWith("1 view will be raster: “Garden "
                                  "view” (perspective) at 300 dpi"),
                  "Raster note");
            check(!confirm(dialog)->isEnabled() && confirm(dialog)->isDefault(),
                  "Export PDF is the default and disabled while blocked");
            check(hint(dialog) == "Fix the broken view, or choose to export it empty.",
                  "Footer names what to fix: " + hint(dialog).toStdString());
            results.findings = {{"codes",
                                 [&] {
                                     QJsonArray list;
                                     for (const auto &code : codes)
                                         list.append(code);
                                     return list;
                                 }()},
                                {"blocked", d.preflight()->blocked()},
                                {"rasterUnavailable", codes.contains("rasterUnavailable")}};
            shoot(dialog, "export-dialog");
            QTest::keyClick(focused(), Qt::Key_Escape);
        });
        action(window, "sheet.export")->trigger();
        watch.finish();
    }
    canvasFocused("Escape");

    // ---- Opt-ins unblock; the list re-runs when the document changes ---------------------
    std::cerr << "[export] opt-ins and live preflight" << std::endl;
    {
        DialogWatch watch("sheetExportDialog", [&](QDialog &dialog) {
            activate(dialog);
            auto &d = settled(dialog);
            space(dialog, "sheetExportAllowBroken");
            settled(dialog);
            check(d.options().allowBroken && !d.options().allowChanged, "allowBroken set");
            check(focused() && focused()->objectName() == "sheetExportAllowBroken",
                  "Refreshing the list keeps keyboard focus on the check box; got " +
                      identity(focused()));
            check(blockText(dialog, "viewBroken")
                      .startsWith("1 broken view exports as an empty, "
                                  "labelled frame."),
                  "The broken view is opted in: " + blockText(dialog, "viewBroken").toStdString());
            check(!confirm(dialog)->isEnabled() &&
                      hint(dialog) ==
                          "Accept the changed views, or choose to export them as they are now.",
                  "Changed views still block: " + hint(dialog).toStdString());
            space(dialog, "sheetExportAllowChanged");
            settled(dialog);
            check(confirm(dialog)->isEnabled() && hint(dialog).isEmpty() &&
                      !d.preflight()->blocked(),
                  "Both opt-ins unblock Export PDF");
            // Live: accepting the views elsewhere (automation) removes their finding.
            executeBatch(doc,
                         {{"apiVersion", 1},
                          {"documentId", QString::fromStdString(doc.identity())},
                          {"expectedRevision", QString::number(doc.revision())},
                          {"commands", QJsonArray{QJsonObject{{"command", "drawing_view.accept"},
                                                              {"all", true}}}}});
            emit window.viewport()->changed();
            check(QTest::qWaitFor(
                      [&] { return !d.checking() && !blockCodes(dialog).contains("viewChanged"); },
                      30000),
                  "The preflight re-runs after a document change");
            check(focused() && dialog.isAncestorOf(focused()),
                  "A finding that goes away leaves focus in the dialog; got " +
                      identity(focused()));
            results.optIns = {
                {"allowBroken", true}, {"allowChanged", true}, {"liveRerunAfterAccept", true}};
            QTest::keyClick(focused(), Qt::Key_Escape);
        });
        action(window, "sheet.export")->trigger();
        watch.finish();
    }
    canvasFocused("the opt-in check");
    // Undo the automation's accept: the views are changed again.
    check(QTest::qWaitFor([&] { return action(window, "edit.undo")->isEnabled(); }), "Undo");
    action(window, "edit.undo")->trigger();
    check(doc.drawingViews().at(f.plan)->accepted == acceptedBefore.at(f.plan),
          "Undo restores the accepted stamps");
    check(entries(doc) == history, "Nothing else entered the history");

    // ---- Go to view, Review and accept, Choose another font --------------------------------
    std::cerr << "[export] navigation" << std::endl;
    {
        DialogWatch watch("sheetExportDialog", [&](QDialog &dialog) {
            activate(dialog);
            settled(dialog);
            space(dialog, "sheetExportGoToView");
        });
        action(window, "sheet.export")->trigger();
        watch.finish();
        check(sheets->currentSheet() == f.a301 && canvas->selectedView() == f.section,
              "Go to view selects the broken view on its page");
        canvasFocused("Go to view");
    }
    {
        DialogWatch watch("sheetExportDialog", [&](QDialog &dialog) {
            activate(dialog);
            settled(dialog);
            space(dialog, "sheetExportReview");
        });
        action(window, "sheet.export")->trigger();
        watch.finish();
        check(sheets->currentSheet() == f.a101 && canvas->selectedView() == f.plan,
              "Review and accept selects the first changed view");
        canvasFocused("Review and accept");
        for (const auto &[id, record] : doc.drawingViews())
            check(record->accepted == acceptedBefore.at(id),
                  "Review and accept accepts nothing by itself");
        check(entries(doc) == history, "Review and accept adds no history entry");
    }
    {
        DialogWatch watch("sheetExportDialog", [&](QDialog &dialog) {
            activate(dialog);
            settled(dialog);
            space(dialog, "sheetExportChooseFont");
        });
        action(window, "sheet.export")->trigger();
        watch.finish();
        auto *font = window.sheetsTray()->findChild<QComboBox *>("sheetItemFont");
        check(canvas->selectedItem() == f.note && font && QTest::qWaitFor([&] {
                  return focused() == font || (font->lineEdit() && focused() == font->lineEdit());
              }),
              "Choose another font selects the note and focuses its Font; got " +
                  identity(focused()));
    }
    results.navigation = {{"goToView", true},
                          {"reviewSelectsFirstChanged", true},
                          {"acceptsNothing", true},
                          {"chooseFontFocusesFont", true}};
    sheets->setCurrentSheet(f.a101);
    canvas->setSelectedItem(0);
    canvas->setSelectedView(0);
    focusCanvas();

    // ---- Cancel writes nothing -----------------------------------------------------------
    std::cerr << "[export] cancel" << std::endl;
    const QDir out(files + "/out");
    QDir().mkpath(out.path());
    const auto pdf = out.filePath("house-sheets.pdf");
    auto chooseFile = [&](QDialog &dialog, const QString &path) {
        DialogWatch file("sheetExportFileDialog", [&](QDialog &chooser) {
            auto *files = qobject_cast<QFileDialog *>(&chooser);
            check(files && files->acceptMode() == QFileDialog::AcceptSave, "A save dialog");
            files->selectFile(path);
            QMetaObject::invokeMethod(files, "accept");
        });
        space(dialog, "sheetExportChoose");
        file.finish();
        // The chooser hands activation back to the export dialog before keys go on.
        check(QTest::qWaitFor([&] {
                  return dialog.isActiveWindow() && focused() && dialog.isAncestorOf(focused());
              }),
              "Focus returns to the export dialog after Choose…; got " + identity(focused()));
        check(dynamic_cast<SheetExportDialog &>(dialog).path() == path,
              "Choose… sets the output file");
    };
    {
        DialogWatch watch("sheetExportDialog", [&](QDialog &dialog) {
            activate(dialog);
            settled(dialog);
            chooseFile(dialog, pdf);
            space(dialog, "sheetExportAllowBroken");
            settled(dialog);
            space(dialog, "sheetExportAllowChanged");
            settled(dialog);
            check(confirm(dialog)->isEnabled(), "Ready to export");
            space(dialog, "sheetExportCancel");
        });
        action(window, "sheet.export")->trigger();
        watch.finish();
    }
    canvasFocused("Cancel");
    check(out.entryList(QDir::Files | QDir::Hidden | QDir::System).isEmpty(),
          "Cancel writes nothing");

    // ---- Export writes the PDF and shows the report sheet ---------------------------------
    std::cerr << "[export] export" << std::endl;
    QString summary, details, json;
    {
        DialogWatch report("sheetExportReport", [&](QDialog &sheet) {
            activate(sheet);
            summary = sheet.findChild<QLabel *>("sheetExportSummary")->text();
            details = sheet.findChild<QPlainTextEdit *>("sheetExportDetails")->toPlainText();
            shoot(sheet, "export-report");
            auto *link = sheet.findChild<QPushButton *>("sheetExportReportLink");
            check(link, "The report links the reconciliation JSON");
            link->setFocus(Qt::TabFocusReason);
            QTest::keyClick(link, Qt::Key_Space);
            auto *text = sheet.findChild<QPlainTextEdit *>("sheetExportReportJson");
            check(text && text->isVisible(), "The link shows the reconciliation JSON");
            json = text->toPlainText();
            shoot(sheet, "export-report-json");
            QTest::keyClick(focused(), Qt::Key_Escape);
        });
        DialogWatch watch("sheetExportDialog", [&](QDialog &dialog) {
            activate(dialog);
            settled(dialog);
            chooseFile(dialog, pdf);
            space(dialog, "sheetExportAllowBroken");
            settled(dialog);
            space(dialog, "sheetExportAllowChanged");
            settled(dialog);
            // Enter reaches the default button, Export PDF.
            auto *all = tabTo(dialog, "sheetExportAll");
            QTest::keyClick(all, Qt::Key_Return);
        });
        action(window, "sheet.export")->trigger();
        watch.finish();
        report.finish();
    }
    canvasFocused("the export report");
    check(QFileInfo::exists(pdf), "Export writes the PDF");
    check(out.entryList(QDir::Files | QDir::Hidden | QDir::System) ==
              QStringList{"house-sheets.pdf"},
          "No partial or temporary files");
    const auto bytes = read(pdf);
    check(bytes.startsWith("%PDF-"), "A PDF");
    const auto boxes = mediaBoxes(bytes);
    check(boxes.size() == 3 && nearPt(boxes[0].width(), 420) && nearPt(boxes[0].height(), 297),
          "Three A3 landscape pages at actual size");
    check(bytes.contains("/Subtype /Image"), "The raster view is a real image");
    check(summary.startsWith("Exported a new PDF: house-sheets.pdf\n3 pages · printed at actual "
                             "size"),
          "Report summary: " + summary.toStdString());
    check(details.contains("Exported as empty, labelled frames: A-301 view 1 “Section B-B”") &&
              details.contains("Exported as they are now (changed since accepted): A-101 view 1 "
                               "“Ground floor plan”") &&
              details.contains("Raster views: A-201 view 2 “Garden view” at 300 dpi") &&
              details.contains("Font substitutions: “ISOCPEUR” → "),
          "Report details: " + details.toStdString());
    const auto report = QJsonDocument::fromJson(json.toUtf8()).object();
    check(report["pageCount"] == 3 && report["options"].toObject()["rasterCapture"] == true &&
              report["rasters"].toArray().size() == 1 &&
              report["exportedEmpty"].toArray().size() == 1,
          "The reconciliation report: raster captured, one empty frame");
    bool rasterUnavailable{};
    for (const auto &finding : report["findings"].toArray())
        rasterUnavailable |= finding.toObject()["code"] == "rasterUnavailable";
    check(!rasterUnavailable, "No rasterUnavailable in the export");
    results.exported = {{"pages", 3},
                        {"bytes", qint64(bytes.size())},
                        {"rasterViews", report["rasters"].toArray().size()},
                        {"exportedEmpty", report["exportedEmpty"].toArray().size()},
                        {"exportedAsIs", report["exportedAsIs"].toArray().size()},
                        {"poppler", poppler()}};
    if (poppler()) {
        const auto info = QString::fromUtf8(tool("pdfinfo", {pdf}));
        check(QRegularExpression(R"(Pages:\s+3\b)").match(info).hasMatch(),
              "pdfinfo counts three pages: " + info.toStdString());
        const auto text = QString::fromUtf8(tool("pdftotext", {pdf, "-"}));
        for (const auto *expected :
             {"General notes", "House", "Ground floor plan", "1:50", "A-101", "Section B-B"})
            check(text.contains(expected), std::string("Extractable text ") + expected);
        results.exported["pdftotext"] = true;
    }
    check(entries(doc) == history &&
              doc.drawingViews().at(f.plan)->accepted == acceptedBefore.at(f.plan),
          "Exporting changes nothing in the document");

    // ---- Print at 100% on a PDF-backed printer ---------------------------------------------
    std::cerr << "[export] print" << std::endl;
    auto *printer = window.sheetPrinter();
    const auto printed = out.filePath("printed.pdf");
    printer->setOutputFormat(QPrinter::PdfFormat);
    printer->setOutputFileName(printed);
    printer->setResolution(600);
    // A4 portrait: every sheet is larger, so the dialog warns and Cancel prints nothing.
    printer->setPageLayout(
        QPageLayout(QPageSize(QPageSize::A4), QPageLayout::Portrait, QMarginsF()));
    {
        DialogWatch watch("sheetPrintDialog", [&](QDialog &dialog) {
            activate(dialog);
            settled(dialog);
            check(dialog.windowTitle() == "Print sheets", "Print title");
            check(blockCodes(dialog).contains("paperSmaller") &&
                      blockText(dialog, "paperSmaller")
                          .startsWith("3 pages are larger than the "
                                      "paper."),
                  "A smaller paper is a warning in the list");
            shoot(dialog, "print-dialog-small-paper");
            QTest::keyClick(focused(), Qt::Key_Escape);
        });
        action(window, "sheet.print")->trigger();
        watch.finish();
    }
    canvasFocused("Print cancelled");
    check(!QFileInfo::exists(printed), "Cancelled printing writes nothing");
    printer->setPageLayout(
        QPageLayout(QPageSize(QPageSize::A3), QPageLayout::Landscape, QMarginsF()));
    {
        DialogWatch watch("sheetPrintDialog", [&](QDialog &dialog) {
            activate(dialog);
            settled(dialog);
            check(!blockCodes(dialog).contains("paperSmaller"), "A3 landscape fits");
            check(!confirm(dialog)->isEnabled() && confirm(dialog)->text() == "Print",
                  "Print is blocked like Export");
            space(dialog, "sheetExportAllowBroken");
            settled(dialog);
            space(dialog, "sheetExportAllowChanged");
            settled(dialog);
            check(confirm(dialog)->isEnabled(), "Print is ready");
            space(dialog, "sheetExportConfirm");
        });
        action(window, "sheet.print")->trigger();
        watch.finish();
    }
    canvasFocused("Print");
    check(QFileInfo::exists(printed), "Print wrote the PDF-backed printer's file");
    const auto printedBytes = read(printed);
    const auto printedBoxes = mediaBoxes(printedBytes);
    check(printedBoxes.size() == 3 && nearPt(printedBoxes[0].width(), 420) &&
              nearPt(printedBoxes[0].height(), 297),
          "Three pages on the printer's A3 paper");
    // 1:1: one paper millimetre is resolution / 25.4 device pixels on the full page.
    const auto transform = drawingSheetDeviceTransform(*printer);
    check(printer->fullPage() && std::abs(transform.m11() - printer->resolution() / 25.4) < 1e-9 &&
              std::abs(transform.m22() - printer->resolution() / 25.4) < 1e-9 &&
              transform.dx() == 0 && transform.dy() == 0,
          "Printing maps paper millimetres 1:1 from the paper's corner");
    results.printed = {{"pages", 3},
                       {"paper", "A3 landscape"},
                       {"resolution", printer->resolution()},
                       {"deviceUnitsPerMm", transform.m11()}};
    if (poppler()) {
        // The margin frame lies 10 mm inside each edge: 400 mm apart at 10 px per mm.
        const auto prefix = out.filePath("printed-page1");
        tool("pdftoppm",
             {"-f", "1", "-l", "1", "-singlefile", "-r", "254", "-png", printed, prefix});
        const QImage page(prefix + ".png");
        check(std::abs(page.width() - 4200) <= 3 && std::abs(page.height() - 2970) <= 3,
              "Printed page rasterizes at A3");
        auto centre = [&](int y, int from, int to) {
            double sum = 0;
            int count = 0;
            for (int x = from; x < to; ++x) {
                const auto c = page.pixelColor(x, y);
                if (c.red() < 160 && c.green() < 160 && c.blue() < 160) {
                    sum += x + .5;
                    ++count;
                }
            }
            check(count > 0, "Margin frame found on the measured row");
            return sum / count;
        };
        const auto left = centre(1485, 50, 150), right = centre(1485, 4050, 4150);
        const auto measured = (right - left) / 10;
        check(std::abs(left / 10 - 10) <= .2 && std::abs(measured - 400) <= .2,
              "Printed at 100%: the margin frame is 10 mm in and 400 mm wide, got " +
                  std::to_string(left / 10) + " and " + std::to_string(measured));
        results.printed["marginFrameMm"] = measured;
    }

    // ---- From Model: File › Export sheets… while the document has sheets -------------------
    std::cerr << "[export] model entry" << std::endl;
    window.setWorkspace(Window::Workspace::Model);
    check(action(window, "file.exportSheets")->isVisible() &&
              action(window, "file.exportSheets")->isEnabled() &&
              !action(window, "sheet.export")->isVisible() &&
              !action(window, "sheet.print")->isVisible(),
          "Model offers Export sheets… for a document with sheets");
    window.viewport()->setFocus(Qt::OtherFocusReason);
    {
        DialogWatch watch("sheetExportDialog", [&](QDialog &dialog) {
            activate(dialog);
            settled(dialog);
            QTest::keyClick(focused(), Qt::Key_Escape);
        });
        action(window, "file.exportSheets")->trigger();
        watch.finish();
    }
    check(
        QTest::qWaitFor([&] { return window.isActiveWindow() && focused() == window.viewport(); }),
        "Focus returns to the viewport in Model; got " + identity(focused()));
    return results;
}
} // namespace

int main(int argc, char **argv) {
    QTemporaryDir files;
    if (!files.isValid())
        return 2;
    qputenv("XDG_CONFIG_HOME", files.path().toUtf8());
    qputenv("XDG_DATA_HOME", files.path().toUtf8());
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    sketchy::setDefaultViewportFormat(format);
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    qt_warning_guard::install();
    QApplication app(argc, argv);
    app.setOrganizationName("SketchyUp");
    app.setApplicationName("SketchyUp");
    try {
        const auto path = files.filePath("house.sketchyup");
        const auto fixture = writeFixture(path);
        // An unexpected message box fails the run instead of blocking it.
        QTimer guard;
        std::string unexpected;
        QObject::connect(&guard, &QTimer::timeout, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                unexpected = box->text().toStdString();
                std::cerr << "Unexpected message box: " << unexpected << std::endl;
                box->reject();
            }
        });
        guard.start(500);
        Window window;
        window.resize(1440, 900);
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Window exposed");
        check(QTest::qWaitFor([&] { return window.viewport()->rendererReady(); }, 10000),
              "Viewport renderer initialized");
        window.openPath(path);
        window.activateWindow();
        check(QTest::qWaitForWindowActive(&window), "Window active");
        // The design review's light and dark captures: the findings dialog in both themes.
        Results results;
        const bool both = !screenshotDir().isEmpty();
        for (const int theme : both ? std::vector<int>{2, 1} : std::vector<int>{1}) {
            window.findChild<QAction *>(QString("view.theme.%1").arg(theme))->trigger();
            currentTheme = theme == 2 ? "dark" : "light";
            QTest::qWait(50);
            window.activateWindow();
            check(QTest::qWaitForWindowActive(&window), "Window active after theme");
            if (theme == 2) {
                // Dark: only the captures; the full run happens once, in light.
                window.setWorkspace(Window::Workspace::Sheets);
                window.sheetWorkspace()->setCurrentSheet(fixture.a101);
                window.sheetWorkspace()->canvas()->setFocus(Qt::OtherFocusReason);
                DialogWatch watch("sheetExportDialog", [&](QDialog &dialog) {
                    activate(dialog);
                    settled(dialog);
                    shoot(dialog, "export-dialog");
                    QTest::keyClick(focused(), Qt::Key_Escape);
                });
                window.findChild<QAction *>("sheet.export")->trigger();
                watch.finish();
                continue;
            }
            results = run(window, files.path(), fixture);
        }
        check(unexpected.empty(), "No unexpected message box: " + unexpected);
        check(qt_warning_guard::first().isEmpty(),
              "No Qt geometry warning: " + qt_warning_guard::first().toStdString());
        window.document().markSaved();
        const QJsonObject summary{{"suite", "sheet_export_input_tests"},
                                  {"passed", true},
                                  {"platform", QGuiApplication::platformName()},
                                  {"scale", window.devicePixelRatioF()},
                                  {"findings", results.findings},
                                  {"optIns", results.optIns},
                                  {"navigation", results.navigation},
                                  {"exported", results.exported},
                                  {"printed", results.printed}};
        std::cout << QJsonDocument(summary).toJson(QJsonDocument::Compact).constData() << '\n';
        std::cout << "Sheet export: the preflight dialog lists broken, changed, font, scale and "
                     "raster findings; opt-ins unblock and the list follows document changes; Go "
                     "to view, Review and accept and Choose another font select without "
                     "accepting; Cancel writes nothing; Export writes a 3-page PDF with a "
                     "viewport raster and the report sheet; Print paints at 100% on a PDF "
                     "printer and warns about small paper; focus returns after every close\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
