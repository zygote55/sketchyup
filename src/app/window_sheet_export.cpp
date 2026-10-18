#include "app/file_operation.hpp"
#include "app/report_sheet.hpp"
#include "app/sheet_canvas.hpp"
#include "app/sheet_export_dialog.hpp"
#include "app/sheet_item_info.hpp"
#include "app/sheet_workspace.hpp"
#include "app/sheets_tray.hpp"
#include "app/window.hpp"
#include "core/drawing_sheets.hpp"
#include "io/drawing_sheet_paint.hpp"
#include "io/sheet_export.hpp"
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QMessageBox>
#include <QPageLayout>
#include <QPlainTextEdit>
#include <QPrinter>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QStandardPaths>
#include <QTabWidget>
namespace sketchy {
namespace {
// The proposed output: "<model>-sheets.pdf" beside the model (else in Documents), never an
// existing file, because an export never replaces one.
QString proposedSheetPdf(const QString &modelPath, const QString &stem) {
    auto folder = modelPath.isEmpty()
                      ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                      : QFileInfo(modelPath).absolutePath();
    if (folder.isEmpty() || !QDir(folder).exists())
        folder = QDir::homePath();
    const QDir dir(folder);
    auto candidate = dir.filePath(stem + "-sheets.pdf");
    for (int n = 2; QFileInfo::exists(candidate) && n < 1000; ++n)
        candidate = dir.filePath(QString("%1-sheets-%2.pdf").arg(stem).arg(n));
    return candidate;
}
// Raster content for the export, drawn by the viewport on the UI thread before the worker
// starts: the GL renderer belongs to this thread, and the worker only looks the images up.
// The preflight's rasterViews finding names each raster view and its pixel size.
SheetRasterRenderer capturedRasters(const Document &doc, Viewport &viewport,
                                    const SheetExportPreflight &preflight) {
    if (preflight.revision != doc.revision() || preflight.documentId != doc.identity())
        throw std::runtime_error("The model changed after the check; export again");
    auto images = std::make_shared<std::map<Id, QImage>>();
    std::map<Id, DrawingSheetCapture> captures;
    for (const auto &finding : preflight.findings) {
        if (finding.code != "rasterViews")
            continue;
        for (const auto &value : finding.detail["views"].toArray()) {
            const auto entry = value.toObject();
            const Id view = entry["view"].toString().toULongLong();
            const QSize pixels(entry["widthPx"].toInt(), entry["heightPx"].toInt());
            const auto record = doc.drawingViews().find(view);
            if (record == doc.drawingViews().end() || pixels.isEmpty())
                continue;
            const auto sheet = record->second->sheet;
            auto capture = captures.find(sheet);
            if (capture == captures.end())
                capture = captures.emplace(sheet, captureDrawingSheet(doc, sheet)).first;
            for (const auto &input : capture->second.views)
                if (input.view == view) {
                    try {
                        images->emplace(view, viewport.renderDrawingViewRaster(input, pixels));
                    } catch (const std::exception &) {
                        // The export reports it: a failure, or an empty frame when opted in.
                    }
                }
        }
    }
    return [images](const DrawingViewEvaluationInput &input, QSize) {
        const auto found = images->find(input.view);
        if (found == images->end())
            throw std::runtime_error("the viewport could not draw this view");
        return found->second;
    };
}
QString viewName(const QJsonObject &page, const QJsonObject &view) {
    const auto number = page["number"].toString();
    return (number.isEmpty() ? "page " + QString::number(page["page"].toInt()) : number) +
           " view " + QString::number(view["number"].toInt()) + " “" + view["name"].toString() +
           "”";
}
} // namespace

QPrinter *Window::sheetPrinter() {
    if (!sheetPrinter_) {
        sheetPrinter_ = std::make_unique<QPrinter>(QPrinter::HighResolution);
        // Actual size: the paper's corner is the sheet's, with no margins and no fitting.
        sheetPrinter_->setFullPage(true);
        sheetPrinter_->setPageMargins(QMarginsF(), QPageLayout::Millimeter);
    }
    return sheetPrinter_.get();
}
void Window::exportSheetsDialog() { sheetExportFlow(false); }
void Window::printSheetsDialog() { sheetExportFlow(true); }

void Window::sheetExportFlow(bool print) {
    if (fileOperation_) {
        status_->setText("Wait for the current file operation to finish.");
        return;
    }
    if (doc_.drawingSheets().empty()) {
        status_->setText("This model has no sheets. Add a page in the Sheets workspace first.");
        return;
    }
    const auto name = path_.isEmpty()
                          ? (recoveredName_.isEmpty() ? QString("Untitled") : recoveredName_)
                          : QFileInfo(path_).fileName();
    SheetExportDialog::Setup setup;
    setup.mode = print ? SheetExportDialog::Mode::Print : SheetExportDialog::Mode::Export;
    setup.documentName = QFileInfo(name).completeBaseName();
    setup.currentSheet = sheets_->currentSheet();
    setup.colors = sheetColors_;
    if (print)
        setup.printer = sheetPrinter();
    else
        setup.path = proposedSheetPdf(path_, setup.documentName);
    SheetExportDialog dialog(doc_, setup, this);
    sheetExport_ = &dialog;
    dialog.exec();
    sheetExport_.clear();
    // A closed dialog returns focus to the page or the model (UX_DESIGN §4.1). Reactivate the
    // window first: without a window manager (X11 under Xvfb) nothing hands activation back.
    activateWindow();
    focusWorkspace();
    switch (dialog.outcome()) {
    case SheetExportDialog::Outcome::Cancel:
        return;
    case SheetExportDialog::Outcome::GoToView:
    case SheetExportDialog::Outcome::ReviewChanged:
        // Nothing is accepted without the user: the view is selected, Accept view (U) is theirs.
        goToSheetView(dialog.target());
        return;
    case SheetExportDialog::Outcome::ChooseFont:
        chooseSheetItemFont(dialog.target());
        return;
    case SheetExportDialog::Outcome::Confirm:
        break;
    }
    const auto &preflight = dialog.preflight();
    if (!preflight)
        return;
    try {
        if (print)
            printSheetsTo(*sheetPrinter(), dialog.options(), *preflight);
        else
            exportSheetsTo(dialog.path(), dialog.options(), *preflight);
    } catch (const std::exception &error) {
        const auto what = QString::fromUtf8(error.what());
        status_->setText(
            (print ? "Could not print the sheets: " : "Could not export the sheets: ") + what);
        QMessageBox box(
            QMessageBox::Warning, print ? "Could not print sheets" : "Could not export sheets",
            (print ? "The sheets were not printed.\n\n" : "No file was written.\n\n") + what,
            QMessageBox::Ok, this);
        box.setObjectName("sheetExportFailure");
        box.exec();
        activateWindow();
        focusWorkspace();
    }
}

void Window::exportSheetsTo(const QString &path, SheetExportOptions options,
                            const SheetExportPreflight &preflight) {
    options.rasterRenderer = capturedRasters(doc_, *viewport_, preflight);
    SheetExport exported;
    {
        QScopedValueRollback busy(fileOperation_, true);
        status_->setText("Exporting sheets…");
        // The worker owns a snapshot; nothing reads the live document or a widget there.
        exported = runFileOperation(this, "Exporting sheets…",
                                    [snapshot = doc_.readSnapshot(), options, path]() mutable {
                                        return exportSheetsPdf(snapshot, options, path);
                                    });
    }
    const auto pages = exported.report["pageCount"].toInt();
    status_->setText(QString("Exported %1 %2 to %3 · Model unchanged")
                         .arg(pages)
                         .arg(pages == 1 ? "page" : "pages")
                         .arg(QFileInfo(path).fileName()));
    sheetExportReport(path, exported);
}

void Window::printSheetsTo(QPrinter &printer, SheetExportOptions options,
                           const SheetExportPreflight &preflight) {
    options.rasterRenderer = capturedRasters(doc_, *viewport_, preflight);
    printer.setFullPage(true);
    printer.setDocName(QFileInfo(path_.isEmpty() ? QString("Untitled") : path_).completeBaseName() +
                       " sheets");
    SheetPrint printed;
    {
        QScopedValueRollback busy(fileOperation_, true);
        status_->setText("Printing sheets…");
        printed = runFileOperation(this, "Printing sheets…",
                                   [snapshot = doc_.readSnapshot(), options, &printer]() mutable {
                                       return printSheets(snapshot, options, printer);
                                   });
    }
    // The progress surface may have shown and hidden: return focus to the page or the model.
    activateWindow();
    focusWorkspace();
    status_->setText(QString("Printed %1 %2 at actual size on %3 · Model unchanged")
                         .arg(printed.pages)
                         .arg(printed.pages == 1 ? "page" : "pages")
                         .arg(printer.pageLayout().pageSize().name()));
}

void Window::sheetExportReport(const QString &path, const SheetExport &exported) {
    const auto &report = exported.report;
    ReportSheet sheet("sheetExportReport", "Sheet export report", this);
    sheet.summary->setObjectName("sheetExportSummary");
    const auto pages = report["pageCount"].toInt();
    sheet.summary->setText(
        QString("Exported a new PDF: %1\n%2 %3 · printed at actual size · %4")
            .arg(QFileInfo(path).fileName())
            .arg(pages)
            .arg(pages == 1 ? "page" : "pages")
            .arg(QLocale().formattedDataSize(qint64(report["bytes"].toDouble()))));
    QStringList pageNames, asIs, empty, rasters, fonts;
    std::map<QString, QString> names;
    for (const auto &value : report["pages"].toArray()) {
        const auto page = value.toObject();
        pageNames << (page["number"].toString().isEmpty()
                          ? page["name"].toString()
                          : page["number"].toString() + " " + page["name"].toString());
        for (const auto &entry : page["views"].toArray()) {
            const auto view = entry.toObject();
            names[view["view"].toString()] = viewName(page, view);
            if (view["content"] == "raster")
                rasters << viewName(page, view) + " at " +
                               QString::number(std::lround(view["rasterDpi"].toDouble())) + " dpi";
        }
    }
    for (const auto &id : report["exportedAsIs"].toArray())
        asIs << names[id.toString()];
    for (const auto &id : report["exportedEmpty"].toArray())
        empty << names[id.toString()];
    for (const auto &value : report["fontSubstitutions"].toArray()) {
        const auto font = value.toObject();
        fonts << "“" + font["requested"].toString() + "” → " + font["used"].toString();
    }
    QStringList text{"File: " + QDir::toNativeSeparators(path), "Pages: " + pageNames.join(", "),
                     asIs.isEmpty()
                         ? QString("Exported as they are now: none.")
                         : "Exported as they are now (changed since accepted): " + asIs.join(", "),
                     empty.isEmpty() ? QString("Exported as empty, labelled frames: none.")
                                     : "Exported as empty, labelled frames: " + empty.join(", ")};
    if (!rasters.isEmpty())
        text << "Raster views: " + rasters.join(", ");
    if (!fonts.isEmpty())
        text << "Font substitutions: " + fonts.join(", ");
    text << "The model and its sheets are unchanged.";
    auto *details = new QPlainTextEdit;
    details->setObjectName("sheetExportDetails");
    details->setAccessibleName("Sheet export details");
    details->setReadOnly(true);
    details->setPlainText(text.join('\n'));
    sheet.body->addWidget(details, 1);
    // The reconciliation report, attached to the PDF and shown here on request.
    auto *link = new QPushButton("Show the reconciliation report (JSON)");
    link->setObjectName("sheetExportReportLink");
    link->setAccessibleDescription("Also attached to the PDF as sheet-export-report.json");
    link->setToolTip("Also attached to the PDF as sheet-export-report.json");
    link->setAutoDefault(false);
    link->setCursor(Qt::PointingHandCursor);
    link->setStyleSheet(
        QString("#sheetExportReportLink {background:transparent;border:1px solid transparent;"
                "border-radius:4px;padding:0 2px;color:%1;text-decoration:underline;}"
                "#sheetExportReportLink:focus {border:1px solid %2;}")
            .arg(sheetColors_.dark ? sheetColors_.accent.name() : QString("#3f6043"),
                 sheetColors_.accent.name()));
    sheet.body->addWidget(link, 0, Qt::AlignLeft);
    auto *json = new QPlainTextEdit;
    json->setObjectName("sheetExportReportJson");
    json->setAccessibleName("Reconciliation report (JSON)");
    json->setReadOnly(true);
    json->setPlainText(QString::fromUtf8(QJsonDocument(report).toJson(QJsonDocument::Indented)));
    json->hide();
    sheet.body->addWidget(json, 2);
    connect(link, &QPushButton::clicked, &sheet, [link, json] {
        json->setVisible(!json->isVisible());
        link->setText(json->isVisible() ? "Hide the reconciliation report (JSON)"
                                        : "Show the reconciliation report (JSON)");
        if (json->isVisible())
            json->setFocus(Qt::OtherFocusReason);
    });
    sheet.exec();
    activateWindow();
    focusWorkspace();
}

void Window::goToSheetView(Id view) {
    const auto found = doc_.drawingViews().find(view);
    if (found == doc_.drawingViews().end())
        return;
    setWorkspace(Workspace::Sheets);
    sheets_->setCurrentSheet(found->second->sheet);
    auto *canvas = sheets_->canvas();
    canvas->setSelectedView(view);
    canvas->setFocus(Qt::OtherFocusReason);
}
void Window::chooseSheetItemFont(Id item) {
    const auto found = doc_.drawingItems().find(item);
    if (found == doc_.drawingItems().end())
        return;
    setWorkspace(Workspace::Sheets);
    sheets_->setCurrentSheet(found->second->sheet);
    sheets_->canvas()->setSelectedItem(item);
    if (const auto index = sideTabs_->indexOf(tray_); index >= 0)
        sideTabs_->setCurrentIndex(index);
    if (narrowLayout())
        openOverlay(PanelKey::More);
    if (auto *font = sheetsTray_->itemInfo()->findChild<QComboBox *>("sheetItemFont");
        font && font->isVisibleTo(this))
        font->setFocus(Qt::OtherFocusReason);
    else
        sheets_->canvas()->setFocus(Qt::OtherFocusReason);
}
} // namespace sketchy
