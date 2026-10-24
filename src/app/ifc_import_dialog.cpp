#include "app/ifc_import_dialog.hpp"
#include "io/ifc_worker.hpp"
#include <QButtonGroup>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>
namespace sketchy {
namespace {
QString humanSize(qint64 bytes) {
    if (bytes >= 1024 * 1024)
        return QString::asprintf("%.1f MB", double(bytes) / (1024.0 * 1024.0));
    if (bytes >= 1024)
        return QString::asprintf("%d KB", int(bytes / 1024));
    return QString::number(bytes) + " B";
}
} // namespace
IfcImportDialog::IfcImportDialog(QWidget *parent) : Settled<QDialog>(parent) {
    setObjectName("ifcImportDialog");
    setWindowTitle("IFC import");
    build();
}
void IfcImportDialog::build() {
    auto *root = new QVBoxLayout(this);
    auto *title = new QLabel("IFC import");
    title->setObjectName("ifcImportTitle");
    title->setStyleSheet("font-size: 20px; font-weight: 600;");
    auto *subline = new QLabel("IFC (STEP) → SketchyUp · unsaved new model · source is never modified");
    subline->setObjectName("ifcImportSubline");
    subline->setStyleSheet("color: #888;");
    root->addWidget(title);
    root->addWidget(subline);
    auto *columns = new QHBoxLayout;
    // ---- the left column -------------------------------------------------------------
    auto *left = new QVBoxLayout;
    auto *fileLayout = new QHBoxLayout;
    auto *choose = new QPushButton("Choose…");
    choose->setObjectName("ifcImportChoose");
    fileLayout->addWidget(choose);
    fileRow = new QLabel("No file chosen");
    fileRow->setObjectName("ifcImportFileName");
    fileLayout->addWidget(fileRow, 1);
    left->addLayout(fileLayout);
    fileFacts = new QLabel;
    fileFacts->setObjectName("ifcImportFileFacts");
    fileFacts->setStyleSheet("color: #888;");
    left->addWidget(fileFacts);
    auto *unitRow = new QHBoxLayout;
    auto *unitLabel = new QLabel("Unit (when the file doesn't declare one):");
    unit = new QComboBox(this);
    unit->setObjectName("ifcImportUnit");
    for (const char *u : {"m", "mm", "cm", "in", "ft"})
        unit->addItem(u);
    unit->setCurrentText("m");
    unitRow->addWidget(unitLabel);
    unitRow->addWidget(unit);
    unitRow->addStretch();
    left->addLayout(unitRow);
    left->addSpacing(8);
    auto *spacesHeading = new QLabel("Spaces (IFC Space tag)");
    spacesHeading->setStyleSheet("font-weight: 600;");
    left->addWidget(spacesHeading);
    auto *spaceGroup = new QButtonGroup(this);
    spaceHidden = new QRadioButton("Hidden");
    spaceShown = new QRadioButton("Shown");
    spaceSkip = new QRadioButton("Skip");
    for (auto *radio : {spaceHidden, spaceShown, spaceSkip}) {
        left->addWidget(radio);
        spaceGroup->addButton(radio);
    }
    spaceHidden->setChecked(true);
    left->addSpacing(8);
    auto *geoHeading = new QLabel("Georeference");
    geoHeading->setStyleSheet("font-weight: 600;");
    left->addWidget(geoHeading);
    auto *geoGroup = new QButtonGroup(this);
    geoKeep = new QRadioButton("Keep the project axes");
    geoGrid = new QRadioButton("Align to the grid");
    for (auto *radio : {geoKeep, geoGrid}) {
        left->addWidget(radio);
        geoGroup->addButton(radio);
    }
    geoKeep->setChecked(true);
    left->addSpacing(8);
    auto *deviationRow = new QHBoxLayout;
    auto *deviationLabel = new QLabel("Maximum deviation:");
    deviation = new QDoubleSpinBox;
    deviation->setObjectName("ifcImportDeviation");
    deviation->setRange(0.05, 10.0);
    deviation->setSingleStep(0.05);
    deviation->setDecimals(2);
    deviation->setValue(0.5);
    deviation->setSuffix(" mm");
    deviationRow->addWidget(deviationLabel);
    deviationRow->addWidget(deviation);
    deviationRow->addStretch();
    left->addLayout(deviationRow);
    helperLine = new QLabel;
    helperLine->setObjectName("ifcImportHelperLine");
    left->addWidget(helperLine);
    banner = new QLabel;
    banner->setObjectName("ifcImportBanner");
    banner->setWordWrap(true);
    banner->hide();
    left->addWidget(banner);
    left->addStretch();
    columns->addLayout(left, 3);
    // ---- the right column ----------------------------------------------------------
    auto *right = new QVBoxLayout;
    auto *doesHeading = new QLabel("What import does");
    doesHeading->setStyleSheet("font-weight: 600;");
    auto *doesText = new QLabel(
        "Reads the IFC structure (projects, sites, buildings, storeys) into the tree.\n"
        "Converts solid geometry (walls, slabs, objects with a solid representation) to "
        "meshes.\nKeeps property sets as properties and material assignments as materials.\n"
        "Never modifies the source file.");
    doesText->setWordWrap(true);
    right->addWidget(doesHeading);
    right->addWidget(doesText);
    right->addSpacing(12);
    auto *nextHeading = new QLabel("What to do next");
    nextHeading->setStyleSheet("font-weight: 600;");
    auto *nextText = new QLabel(
        "The imported model is a new unsaved model. The source file is never modified.\n"
        "Tessellated geometry arrives as-is; other geometry needs the OpenCASCADE kernel.\n"
        "Save it as a .sketchyup file to keep it.");
    nextText->setWordWrap(true);
    right->addWidget(nextHeading);
    right->addWidget(nextText);
    right->addStretch();
    columns->addLayout(right, 2);
    root->addLayout(columns);
    // ---- the footer -----------------------------------------------------------------
    auto *footer = new QHBoxLayout;
    importButton = new QPushButton("Import");
    importButton->setObjectName("ifcImportGo");
    importButton->setDefault(true);
    importButton->setEnabled(false);
    auto *chooseAnother = new QPushButton("Choose another file");
    chooseAnother->setObjectName("ifcImportChooseAnother");
    auto *cancel = new QPushButton("Cancel");
    cancel->setObjectName("ifcImportCancel");
    QObject::connect(choose, &QPushButton::clicked, this, &IfcImportDialog::chooseFile);
    QObject::connect(chooseAnother, &QPushButton::clicked, this, &IfcImportDialog::chooseFile);
    QObject::connect(importButton, &QPushButton::clicked, this, [this] { done(QDialog::Accepted); });
    QObject::connect(cancel, &QPushButton::clicked, this, [this] { done(QDialog::Rejected); });
    footer->addWidget(importButton);
    footer->addStretch();
    footer->addWidget(chooseAnother);
    footer->addWidget(cancel);
    root->addLayout(footer);
    setMinimumWidth(720);
}
void IfcImportDialog::setProbe(const IfcProbe &probe) {
    probe_ = probe;
    if (probe.state == IfcState::Available) {
        helperLine->setText(QString("✓ IfcOpenShell %1 · OpenCASCADE kernel").arg(probe.ifcOpenShell));
        helperLine->setStyleSheet("color: #2e7d32;");
        banner->hide();
    } else if (probe.state == IfcState::Limited) {
        helperLine->setText(QString("⚠ IfcOpenShell %1 · kernel: none").arg(probe.ifcOpenShell));
        helperLine->setStyleSheet("color: #b26a00;");
        banner->setText("⚠ Limited: the OpenCASCADE kernel isn't installed. Only tessellated geometry will be converted.");
        banner->setStyleSheet("color: #b26a00;");
        banner->show();
    }
}
void IfcImportDialog::setFile(const QString &path) {
    const QFileInfo info(path);
    refused_ = false;
    bytes_ = info.size();
    schema_.clear();
    fileRow->setText(info.fileName().isEmpty() ? path : info.fileName());
    try {
        schema_ = checkIfcSource(path);
        fileFacts->setText(QString("%1 · %2").arg(schema_).arg(humanSize(bytes_)));
        banner->hide();
        if (importButton)
            importButton->setEnabled(true);
    } catch (const IfcImportError &error) {
        // A refusal: name what is wrong and keep Import off.
        refused_ = true;
        fileFacts->setText(humanSize(bytes_));
        banner->setText(QString("✗ %1").arg(QString::fromStdString(error.what())));
        banner->setStyleSheet("color: #b3261e;");
        banner->show();
        if (importButton)
            importButton->setEnabled(false);
    }
}
IfcImportOptions IfcImportDialog::options() const {
    IfcImportOptions options;
    options.spaces = spaceSkip->isChecked() ? IfcSpaces::Skip
                  : spaceShown->isChecked()  ? IfcSpaces::Shown
                                           : IfcSpaces::Hidden;
    options.georeference = geoGrid->isChecked() ? IfcGeoreference::Grid : IfcGeoreference::KeepAxes;
    return options;
}
IfcWorkerOptions IfcImportDialog::workerOptions() const {
    IfcWorkerOptions worker;
    worker.chordDeviationMm = deviation->value();
    worker.lengthUnit = unit->currentText();
    return worker;
}
bool IfcImportDialog::refused() const { return refused_; }
QString IfcImportDialog::fileName() const { return fileRow->text(); }
void IfcImportDialog::chooseFile() {
    const auto path =
        QFileDialog::getOpenFileName(this, "Choose an IFC file", {}, "IFC models (*.ifc *.IFC);;All files (*.*)");
    if (!path.isEmpty())
        setFile(path);
}
} // namespace sketchy
#include "app/ifc_import_dialog.moc"
