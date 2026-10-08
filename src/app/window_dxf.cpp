#include "app/report_sheet.hpp"
#include "app/window.hpp"
#include "io/dxf_export.hpp"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QJsonArray>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSpinBox>
namespace sketchy {
namespace {
struct Options {
    DxfOptions units;
    unsigned segments;
};
std::optional<Options> options(QWidget *parent, bool exporting) {
    QDialog dialog(parent);
    dialog.setObjectName("dxfOptionsDialog");
    dialog.setWindowTitle(exporting ? "DXF export options" : "DXF import options");
    QFormLayout form(&dialog);
    auto *units = new QComboBox;
    units->setObjectName("dxfUnits");
    units->setAccessibleName("DXF coordinate units");
    if (!exporting)
        units->addItem("Use declared drawing units", 0);
    for (const auto &[label, value] : std::vector<std::pair<QString, double>>{{"Millimeters", .001},
                                                                              {"Centimeters", .01},
                                                                              {"Meters", 1},
                                                                              {"Inches", .0254},
                                                                              {"Feet", .3048}})
        units->addItem(label, value);
    if (exporting)
        units->setCurrentIndex(2);
    form.addRow("Coordinate units", units);
    auto *segments = new QSpinBox(&dialog);
    segments->setObjectName("dxfSegments");
    segments->setAccessibleName("Chord segments per full circle");
    segments->setRange(12, 256);
    segments->setValue(96);
    if (!exporting)
        form.addRow("Segments per circle", segments);
    else
        segments->hide();
    auto *note = new QLabel(
        exporting
            ? "Exports all model edges in world XY, including hidden geometry, without section "
              "clipping. Faces become unfilled contours. Non-planar models cannot use this export."
            : "Imports supported model-space XY lines, polylines, arcs and circles as editable "
              "edges. Closed outlines do not create faces. Missing or unitless drawing units "
              "require an explicit choice. Review omitted entity counts after import.");
    note->setWordWrap(true);
    form.addRow(note);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form.addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return {};
    DxfOptions unit;
    if (units->currentData().toDouble() != 0)
        unit.metresPerUnit = units->currentData().toDouble();
    return Options{unit, unsigned(segments->value())};
}
QString describe(QString key) {
    key.replace(QRegularExpression("([a-z])([A-Z])"), "\\1 \\2");
    key.replace(':', " — ");
    return key;
}
void report(QWidget *parent, const QJsonObject &data, bool exported) {
    ReportSheet sheet("dxfReport", exported ? "DXF export report" : "DXF import report", parent);
    sheet.resize(700, 520);
    sheet.summary->setObjectName("dxfSummary");
    const auto source = data["source"].toObject();
    const auto units =
        exported ? data["metresPerUnit"].toDouble() : source["metresPerUnit"].toDouble();
    sheet.summary->setText(
        QString("%1\n%2 meters per drawing unit · World XY\n%3")
            .arg(exported ? "Exported a new DXF drawing" : "Imported an unsaved native model")
            .arg(units, 0, 'g', 8)
            .arg(exported ? QString("Entities: %1").arg(data["entities"].toInteger())
                          : QString("Bodies: %1 · Wire segments: %2 · Analytic curves: %3")
                                .arg(data["bodies"].toInteger())
                                .arg(data["wireSegments"].toInteger())
                                .arg(data["curves"].toInteger())));
    QString text = exported ? "Source model and history remain unchanged."
                            : "Save to a new .sketchyup file. Source bytes remain unchanged.";
    for (const auto &value : data["notices"].toArray())
        text += "\n• " + value.toString();
    if (!exported) {
        text += QString("\nMaximum chord deviation: %1 meters.")
                    .arg(data["maximumChordDeviationMetres"].toDouble(), 0, 'g', 8);
        const auto omissions = source["omittedEntities"].toObject();
        if (!omissions.isEmpty())
            text += "\n\nOmitted source entities (type:reason):";
        for (auto i = omissions.begin(); i != omissions.end(); ++i)
            text += QString("\n• %1 — %2").arg(describe(i.key())).arg(i.value().toInt());
    }
    const auto losses = (exported ? data : source)["losses"].toObject();
    if (!losses.isEmpty())
        text += "\n\nConversion counts:";
    for (auto i = losses.begin(); i != losses.end(); ++i)
        text += QString("\n• %1: %2").arg(describe(i.key())).arg(i.value().toInt());
    auto *details = new QPlainTextEdit;
    details->setObjectName("dxfDetails");
    details->setReadOnly(true);
    details->setPlainText(text);
    sheet.body->addWidget(details, 1);
    sheet.exec();
}
} // namespace
void Window::importDxfPath(const QString &path, DxfOptions options, unsigned segments) {
    auto imported = loadDxf(path, options, segments);
    if (!canReplace())
        return;
    doc_ = std::move(imported.document);
    resetRecoveryContext();
    path_.clear();
    viewport_->cancel();
    viewport_->setSelection(0);
    viewport_->fit();
    sync();
    status_->setText("Imported an unsaved DXF drawing · Source unchanged");
    report(this, imported.report, false);
}
void Window::exportDxfPath(const QString &path, double metresPerUnit) {
    const auto exported = exportDxf(doc_, metresPerUnit);
    writeDxfExport(exported, path);
    status_->setText("Exported a new DXF drawing · Model unchanged");
    report(this, exported.report, true);
}
void Window::importDxfDialog() {
    QFileDialog file(this, "Import 2D DXF");
    file.setObjectName("dxfImportFileDialog");
    file.setNameFilter("DXF drawing (*.dxf)");
    file.setFileMode(QFileDialog::ExistingFile);
    if (file.exec() != QDialog::Accepted || file.selectedFiles().isEmpty())
        return;
    const auto choice = options(this, false);
    if (choice)
        importDxfPath(file.selectedFiles().front(), choice->units, choice->segments);
}
void Window::exportDxfDialog() {
    const auto choice = options(this, true);
    if (!choice)
        return;
    QFileDialog file(this, "New 2D DXF drawing");
    file.setObjectName("dxfExportFileDialog");
    file.setAcceptMode(QFileDialog::AcceptSave);
    file.setFileMode(QFileDialog::AnyFile);
    file.setOption(QFileDialog::DontConfirmOverwrite);
    file.setNameFilter("DXF drawing (*.dxf)");
    file.setDefaultSuffix("dxf");
    if (file.exec() == QDialog::Accepted && !file.selectedFiles().isEmpty())
        exportDxfPath(file.selectedFiles().front(), *choice->units.metresPerUnit);
}
} // namespace sketchy
