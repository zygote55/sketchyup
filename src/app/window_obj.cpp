#include "app/report_sheet.hpp"
#include "app/window.hpp"
#include "io/obj_export.hpp"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QJsonArray>
#include <QPlainTextEdit>
namespace sketchy {
namespace {
std::optional<ObjImportOptions> options(QWidget *parent, bool exporting) {
    QDialog dialog(parent);
    dialog.setObjectName("objOptionsDialog");
    dialog.setWindowTitle(exporting ? "OBJ export options" : "OBJ import options");
    QFormLayout form(&dialog);
    auto *units = new QComboBox;
    units->setObjectName("objUnits");
    units->setAccessibleName("OBJ coordinate units");
    for (const auto &[name, scale] : std::vector<std::pair<QString, double>>{{"Millimeters", .001},
                                                                             {"Centimeters", .01},
                                                                             {"Meters", 1},
                                                                             {"Inches", .0254},
                                                                             {"Feet", .3048}})
        units->addItem(name, scale);
    units->setCurrentIndex(2);
    form.addRow("Coordinate units", units);
    auto *up = new QComboBox;
    up->setObjectName("objUpAxis");
    up->setAccessibleName("OBJ up axis");
    up->addItems({"Y up", "Z up"});
    form.addRow("Up axis", up);
    auto *note = new QLabel(
        exporting
            ? "Exports all model geometry, including hidden objects, without section clipping. "
              "Creates a new folder containing model.obj, materials.mtl, textures and a conversion "
              "report."
            : "OBJ does not declare units or an up axis. Choose the settings used by its author. "
              "Import creates a separate editable native model; the source stays unchanged.");
    note->setWordWrap(true);
    form.addRow(note);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form.addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return {};
    return ObjImportOptions{units->currentData().toDouble(),
                            up->currentIndex() == 0 ? ObjUpAxis::Y : ObjUpAxis::Z};
}
void report(QWidget *parent, const QJsonObject &data, ObjImportOptions options, bool exported) {
    ReportSheet sheet("objReport", exported ? "OBJ export report" : "OBJ import report", parent);
    sheet.resize(650, 480);
    sheet.summary->setObjectName("objSummary");
    sheet.summary->setText(
        QString("%1\nFaces: %2 · Wire segments: %3 · Materials: %4\n%5 meters per OBJ unit · %6 up")
            .arg(exported ? "Exported a new OBJ package" : "Imported an unsaved native model")
            .arg(data["faces"].toInteger())
            .arg(data["wireSegments"].toInteger())
            .arg(data["materials"].toInteger())
            .arg(options.metresPerUnit, 0, 'g', 8)
            .arg(options.up == ObjUpAxis::Y ? "Y" : "Z"));
    QString text =
        exported
            ? "The package contains all model geometry, including hidden objects, without section "
              "clipping. Keep model.obj, materials.mtl and textures together when moving it."
            : "Open the imported object groups to edit their faces and wires. Save to a new "
              ".sketchyup file. External edits do not update this model.";
    text += "\n\nConversion notices:";
    const auto notices = data["notices"].toArray();
    for (const auto &v : notices) {
        const auto n = v.toObject();
        text += QString("\n• %1 Count: %2.").arg(n["message"].toString()).arg(n["count"].toInt());
    }
    auto omitted = [&](const QJsonObject &values, const QString &kind) {
        for (auto it = values.begin(); it != values.end(); ++it)
            text += QString("\n• Unsupported %1 statement %2 omitted. Count: %3.")
                        .arg(kind, it.key())
                        .arg(it.value().toInt());
    };
    if (!exported) {
        omitted(data["source"].toObject()["omittedStatements"].toObject(), "OBJ");
        for (const auto &library : data["materialLibraries"].toArray())
            omitted(library.toObject()["omittedStatements"].toObject(), "MTL");
    }
    auto *details = new QPlainTextEdit;
    details->setObjectName("objDetails");
    details->setReadOnly(true);
    details->setPlainText(text);
    sheet.body->addWidget(details, 1);
    sheet.exec();
}
} // namespace
void Window::importObjPath(const QString &path, ObjImportOptions options) {
    auto imported = loadObj(path, options);
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
void Window::exportObjPath(const QString &path, ObjImportOptions options) {
    const auto package = exportObj(doc_, options);
    writeObjExport(package, path);
    status_->setText("Exported OBJ package · Model unchanged");
    report(this, package.manifest, options, true);
}
void Window::importObjDialog() {
    QFileDialog file(this, "Import OBJ/MTL");
    file.setObjectName("objImportFileDialog");
    file.setNameFilter("Wavefront OBJ (*.obj)");
    file.setFileMode(QFileDialog::ExistingFile);
    if (file.exec() != QDialog::Accepted || file.selectedFiles().isEmpty())
        return;
    const auto choice = options(this, false);
    if (choice)
        importObjPath(file.selectedFiles().front(), *choice);
}
void Window::exportObjDialog() {
    const auto choice = options(this, true);
    if (!choice)
        return;
    QFileDialog folder(this, "New OBJ package folder");
    folder.setObjectName("objExportFileDialog");
    folder.setAcceptMode(QFileDialog::AcceptSave);
    folder.setFileMode(QFileDialog::AnyFile);
    folder.setOption(QFileDialog::DontConfirmOverwrite);
    folder.setLabelText(QFileDialog::FileName, "New folder name:");
    if (folder.exec() != QDialog::Accepted || folder.selectedFiles().isEmpty())
        return;
    exportObjPath(folder.selectedFiles().front(), *choice);
}
} // namespace sketchy
