#include "app/report_sheet.hpp"
#include "app/window.hpp"
#include "io/stl_export.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QJsonArray>
#include <QPlainTextEdit>
namespace sketchy {
namespace {
struct StlOptions {
    StlCoordinateOptions coordinates;
    StlRepairOptions repairs;
    StlEncoding encoding;
};
std::optional<StlOptions> options(QWidget *parent, bool exporting) {
    QDialog dialog(parent);
    dialog.setObjectName("stlOptionsDialog");
    dialog.setWindowTitle(exporting ? "STL export options" : "STL import options");
    QFormLayout form(&dialog);
    auto *units = new QComboBox;
    units->setObjectName("stlUnits");
    units->setAccessibleName("STL coordinate units");
    for (const auto &[name, scale] : std::vector<std::pair<QString, double>>{{"Millimeters", .001},
                                                                             {"Centimeters", .01},
                                                                             {"Meters", 1},
                                                                             {"Inches", .0254},
                                                                             {"Feet", .3048}})
        units->addItem(name, scale);
    units->setCurrentIndex(2);
    form.addRow("Coordinate units", units);
    auto *up = new QComboBox;
    up->setObjectName("stlUpAxis");
    up->setAccessibleName("STL up axis");
    up->addItems({"Y up", "Z up"});
    up->setCurrentIndex(1);
    form.addRow("Up axis", up);
    auto *weld = new QComboBox;
    weld->setObjectName("stlWeld");
    weld->setAccessibleName("STL vertex welding");
    weld->addItems({"None — keep separate triangle corners", "Exact — join identical positions",
                    "Tolerance — join nearby positions"});
    auto *tolerance = new QDoubleSpinBox;
    tolerance->setObjectName("stlTolerance");
    tolerance->setAccessibleName("Weld tolerance in meters");
    tolerance->setDecimals(9);
    tolerance->setRange(1e-9, 1e-3);
    tolerance->setValue(1e-7);
    tolerance->setSingleStep(1e-7);
    tolerance->setEnabled(false);
    auto *discard = new QCheckBox("Remove collapsed or sub-tolerance facets");
    discard->setObjectName("stlDiscardDegenerate");
    auto *encoding = new QComboBox;
    encoding->setObjectName("stlEncoding");
    encoding->setAccessibleName("STL encoding");
    encoding->addItems({"Binary", "ASCII (higher coordinate precision)"});
    if (exporting) {
        form.addRow("Encoding", encoding);
        delete weld;
        delete tolerance;
        delete discard;
        weld = nullptr;
        tolerance = nullptr;
        discard = nullptr;
    } else {
        form.addRow("Vertex welding", weld);
        form.addRow("Tolerance (meters)", tolerance);
        form.addRow(discard);
        delete encoding;
        encoding = nullptr;
        QObject::connect(weld, &QComboBox::currentIndexChanged, &dialog,
                         [=](int index) { tolerance->setEnabled(index == 2); });
    }

    auto *note = new QLabel(
        exporting ? "Exports all surface triangles, including hidden objects, without section "
                    "clipping. Materials and hierarchy are omitted. Creates a new STL file."
                  : "Choose the author's units and up axis. Repairs apply only to this imported "
                    "copy and can be undone. Welding never crosses source solids. Materials and "
                    "hierarchy are unavailable.");
    note->setWordWrap(true);
    form.addRow(note);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form.addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return {};
    return StlOptions{
        {units->currentData().toDouble(), up->currentIndex() == 0 ? StlUpAxis::Y : StlUpAxis::Z},
        {weld ? static_cast<StlWeld>(weld->currentIndex()) : StlWeld::None,
         tolerance ? tolerance->value() : 1e-7, discard && discard->isChecked()},
        encoding && encoding->currentIndex() == 1 ? StlEncoding::Ascii : StlEncoding::Binary};
}

void report(QWidget *parent, const QJsonObject &data, StlCoordinateOptions options, bool exported) {
    ReportSheet sheet("stlReport", exported ? "STL export report" : "STL import report", parent);
    sheet.resize(680, 520);
    sheet.summary->setObjectName("stlSummary");
    sheet.summary->setText(
        QString("%1\nTriangles: %2 · %3 meters per STL unit · %4 up")
            .arg(exported ? "Exported a new STL file" : "Imported an unsaved native model")
            .arg(data["facets"].toInteger())
            .arg(options.metresPerUnit, 0, 'g', 8)
            .arg(options.up == StlUpAxis::Y ? "Y" : "Z"));
    QString text;
    if (!exported)
        text =
            QString("Welding: %1 · Joined corner references: %2 · Moved references: %3\nRemoved "
                    "facets: %4\nSave to a new .sketchyup file. Source bytes remain unchanged.\n")
                .arg(data["weld"].toString())
                .arg(data["weldedCornerReferences"].toInteger())
                .arg(data["movedCornerReferences"].toInteger())
                .arg(data["discardedFacets"].toInteger());
    for (const auto &value : data["notices"].toArray())
        text +=
            "\n• " + (value.isString() ? value.toString() : value.toObject()["message"].toString());
    if (exported)
        text += QString("\nMaximum coordinate error: %1 meters.")
                    .arg(data["maximumCoordinateErrorMetres"].toDouble(), 0, 'g', 8);
    for (const auto &value : data["geometry"].toArray()) {
        const auto geometry = value.toObject();
        text += QString("\n\nBody %1: %2 · Analysis %3")
                    .arg(geometry["body"].toString(), geometry["solidStatus"].toString(),
                         geometry["analysisComplete"].toBool() ? "complete" : "incomplete");
        if (!geometry["materialVolume"].isNull())
            text +=
                QString(" · Volume %1 m³").arg(geometry["materialVolume"].toDouble(), 0, 'g', 8);
        for (const auto &finding : geometry["findings"].toArray()) {
            const auto f = finding.toObject();
            text += QString("\n• %1 Count: %2%3.")
                        .arg(f["message"].toString())
                        .arg(f["count"].toInteger())
                        .arg(f["countExact"].toBool() ? "" : " (bounded estimate)");
        }
    }
    auto *details = new QPlainTextEdit;
    details->setObjectName("stlDetails");
    details->setReadOnly(true);
    details->setPlainText(text);
    sheet.body->addWidget(details, 1);
    sheet.exec();
}
} // namespace
void Window::importStlPath(const QString &path, StlCoordinateOptions options,
                           StlRepairOptions repairs) {
    auto imported = loadStl(path, options, repairs);
    if (!canReplace())
        return;
    doc_ = std::move(imported.document);
    resetRecoveryContext();
    path_.clear();
    viewport_->cancel();
    viewport_->setSelection(0);
    viewport_->fit();
    sync();
    status_->setText("Imported an unsaved native model · Source files unchanged");
    report(this, imported.report, options, false);
}
void Window::exportStlPath(const QString &path, StlCoordinateOptions options,
                           StlEncoding encoding) {
    const auto package = exportStl(doc_, options, encoding);
    writeStlExport(package, path);
    status_->setText("Exported STL file · Model unchanged");
    report(this, package.report, options, true);
}
void Window::importStlDialog() {
    QFileDialog file(this, "Import STL");
    file.setObjectName("stlImportFileDialog");
    file.setNameFilter("STL triangle mesh (*.stl)");
    file.setFileMode(QFileDialog::ExistingFile);
    if (file.exec() != QDialog::Accepted || file.selectedFiles().isEmpty())
        return;
    const auto choice = options(this, false);
    if (choice)
        importStlPath(file.selectedFiles().front(), choice->coordinates, choice->repairs);
}
void Window::exportStlDialog() {
    const auto choice = options(this, true);
    if (!choice)
        return;
    QFileDialog folder(this, "New STL file");
    folder.setObjectName("stlExportFileDialog");
    folder.setAcceptMode(QFileDialog::AcceptSave);
    folder.setFileMode(QFileDialog::AnyFile);
    folder.setOption(QFileDialog::DontConfirmOverwrite);
    folder.setNameFilter("STL triangle mesh (*.stl)");
    folder.setDefaultSuffix("stl");
    if (folder.exec() != QDialog::Accepted || folder.selectedFiles().isEmpty())
        return;
    exportStlPath(folder.selectedFiles().front(), choice->coordinates, choice->encoding);
}
} // namespace sketchy
