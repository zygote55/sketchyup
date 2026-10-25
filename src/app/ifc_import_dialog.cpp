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
} // the STEP kind (R098.a3, ADR 0172 section 8): the same screen's STEP wording.
const char *const stepDoesText =
    "Reads the STEP file (AP203, AP214, AP242) and its units; a unit is never guessed.\n"
    "Tessellates the transferred solids (BRepMesh, at the deviation and the crease angle) into meshes.\n"
    "Groups the solids under one root named for the file.\nNever modifies the source file.";
const char *const stepNextText =
    "The imported model is a new unsaved model. The source file is never modified.\n"
    "Solids arrive as tessellated meshes; the rest of the STEP content is reported, not converted.\n"
    "Save it as a .sketchyup file to keep it.";
} // namespace
IfcImportDialog::IfcImportDialog(QWidget *parent) : Settled<QDialog>(parent) {
    setObjectName("ifcImportDialog");
    setWindowTitle("IFC import");
    build();
    setKind(Kind::Ifc); // the default's own wording, through the one code path
}
void IfcImportDialog::build() {
    auto *root = new QVBoxLayout(this);
    title = new QLabel("IFC import");
    title->setObjectName("ifcImportTitle");
    title->setStyleSheet("font-size: 20px; font-weight: 600;");
    subline = new QLabel("IFC (STEP) → SketchyUp · unsaved new model · source is never modified");
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
    // The IFC-only sections, in their own widgets so the STEP kind can hide them (R098.a3).
    spacesSection = new QWidget;
    spacesSection->setObjectName("ifcImportSpacesSection");
    auto *spacesSectionLayout = new QVBoxLayout(spacesSection);
    spacesSectionLayout->setContentsMargins(0, 0, 0, 0);
    spacesHeading = new QLabel("Spaces (IFC Space tag)");
    spacesHeading->setStyleSheet("font-weight: 600;");
    auto *spaceGroup = new QButtonGroup(this);
    spaceHidden = new QRadioButton("Hidden");
    spaceShown = new QRadioButton("Shown");
    spaceSkip = new QRadioButton("Skip");
    for (auto *radio : {spaceHidden, spaceShown, spaceSkip}) {
        spacesSectionLayout->addWidget(radio);
        spaceGroup->addButton(radio);
    }
    spaceHidden->setChecked(true);
    left->addWidget(spacesSection);
    left->addSpacing(8);
    geoSection = new QWidget;
    geoSection->setObjectName("ifcImportGeoSection");
    auto *geoSectionLayout = new QVBoxLayout(geoSection);
    geoSectionLayout->setContentsMargins(0, 0, 0, 0);
    geoHeading = new QLabel("Georeference");
    geoHeading->setStyleSheet("font-weight: 600;");
    auto *geoGroup = new QButtonGroup(this);
    geoKeep = new QRadioButton("Keep the project axes");
    geoGrid = new QRadioButton("Align to the grid");
    for (auto *radio : {geoKeep, geoGrid}) {
        geoSectionLayout->addWidget(radio);
        geoGroup->addButton(radio);
    }
    geoKeep->setChecked(true);
    left->addWidget(geoSection);
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
    // The STEP kind's tessellation angle (ADR 0172 section 3), hidden for IFC (its crease angle is fixed).
    auto *angleRow = new QHBoxLayout;
    angleLabel = new QLabel("Crease angle:");
    angle = new QDoubleSpinBox;
    angle->setObjectName("ifcImportAngle");
    angle->setRange(1.0, 180.0);
    angle->setSingleStep(1.0);
    angle->setDecimals(1);
    angle->setValue(30.0);
    angle->setSuffix("°");
    angleRow->addWidget(angleLabel);
    angleRow->addWidget(angle);
    angleRow->addStretch();
    left->addLayout(angleRow);
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
    doesText = new QLabel(
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
    nextText = new QLabel(
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
    // R098.a3: the helper line is the two directions' own. The STEP import runs in the IFC helper
    // but needs none of the kernel, so its line says the worker's own fact (the step import's own
    // reader and BRepMesh), not the kernel's.
    if (isStep()) {
        helperLine->setText(QStringLiteral("✓ sketchyup-ifc-worker %1 · the STEP import's own reader").arg(probe.workerVersion));
        helperLine->setStyleSheet("color: #2e7d32;");
    } else if (probe.state == IfcState::Available) {
        helperLine->setText(QStringLiteral("✓ IfcOpenShell %1 · OpenCASCADE kernel").arg(probe.ifcOpenShell));
        helperLine->setStyleSheet("color: #2e7d32;");
    } else if (probe.state == IfcState::Limited) {
        helperLine->setText(QStringLiteral("⚠ IfcOpenShell %1 · kernel: none").arg(probe.ifcOpenShell));
        helperLine->setStyleSheet("color: #b26a00;");
    }
    applyProbeBanner();
}
void IfcImportDialog::applyProbeBanner() {
    // The Limited warning is the probe's own: it outlives the file choice, which owns only the refusal.
    // R098.a3: it is IFC's (the IFC kernel for B-spline and curve geometry); the STEP import needs
    // none (the helper's own STEP reader and BRepMesh), so it is not shown for the STEP kind.
    if (probe_.state == IfcState::Limited && !isStep()) {
        banner->setText("⚠ Limited: the OpenCASCADE kernel isn't installed. Only tessellated geometry will be converted.");
        banner->setStyleSheet("color: #b26a00");
        banner->show();
    } else {
        banner->hide();
    }
}
void IfcImportDialog::setFile(const QString &path) {
    const QFileInfo info(path);
    refused_ = false;
    bytes_ = info.size();
    schema_.clear();
    fileRow->setText(info.fileName().isEmpty() ? path : info.fileName());
    // The cheap checks of the application's bounded code (ADR 0172 section 10: a file that does not pass
    // them gets no conversion attempt, and its reason is said); for STEP (R098.a3) the application's
    // own bounded copy of the helper's cheap scan: the ISO-10303-21 header, one accepted AP schema,
    // and the declared unit (display; the helper's scan is the authority).
    try {
        if (isStep()) {
            const StepScan step = checkStepSource(path);
            schema_ = step.schema;
            fileFacts->setText(QString("%1 · %2 · %3")
                                  .arg(step.schema)
                                  .arg(step.unitDeclared ? step.unit : QStringLiteral("no unit declared"))
                                  .arg(humanSize(bytes_)));
        } else {
            schema_ = checkIfcSource(path);
            fileFacts->setText(QString("%1 · %2").arg(schema_).arg(humanSize(bytes_)));
        }
        applyProbeBanner(); // a valid file clears any refusal; the probe warning comes back
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
void IfcImportDialog::setKind(Kind kind) {
    kind_ = kind;
    const bool step = kind == Kind::Step;
    setWindowTitle(step ? "STEP import" : "IFC import");
    title->setText(step ? "STEP import" : "IFC import");
    subline->setText(step ? "STEP (AP203, AP214, AP242) → SketchyUp · unsaved new model · source is never modified"
                          : "IFC (STEP) → SketchyUp · unsaved new model · source is never modified");
    spacesSection->setVisible(!step); // the IFC-only choices (spaces, georeference) have no STEP meaning
    geoSection->setVisible(!step);
    angleLabel->setVisible(step); // the tessellation's crease angle (ADR 0172 section 3); the IFC crease angle is fixed
    angle->setVisible(step);
    deviation->setValue(step ? 1.0 : 0.5); // the two directions' own defaults (ADR 0172 section 3)
    // The unit is the two directions' own default for a file that declares none: IFC in metres, STEP in
    // millimetres (the STEP export writes millimetres, ADR 0172 section 2).
    unit->setCurrentText(step ? "mm" : "m");
    doesText->setText(step ? stepDoesText :
                                       "Reads the IFC structure (projects, sites, buildings, storeys) into the tree.\n"
                                       "Converts solid geometry (walls, slabs, objects with a solid representation) to "
                                       "meshes.\nKeeps property sets as properties and material assignments as materials.\n"
                                       "Never modifies the source file.");
    nextText->setText(step ? stepNextText :
                                       "The imported model is a new unsaved model. The source file is never modified.\n"
                                       "Tessellated geometry arrives as-is; other geometry needs the OpenCASCADE kernel.\n"
                                       "Save it as a .sketchyup file to keep it.");
    applyProbeBanner(); // a kind change re-asks whether the kernel banner applies to this kind
}
StepWorkerOptions IfcImportDialog::stepWorkerOptions() const {
    StepWorkerOptions worker;
    worker.deflectionMm = deviation->value();
    worker.angleDeg = angle->value();
    worker.lengthUnitOverride = unit->currentText();
    return worker;
}
bool IfcImportDialog::refused() const { return refused_; }
QString IfcImportDialog::fileName() const { return fileRow->text(); }
void IfcImportDialog::chooseFile() {
    const QString filter = isStep()
                               ? "STEP models (*.step *.STEP *.stp);;All files (*.*);;IFC models (*.ifc *.IFC)"
                               : "IFC models (*.ifc *.IFC);;All files (*.*);;STEP models (*.step *.STEP *.stp)";
    const auto path =
        QFileDialog::getOpenFileName(this, isStep() ? "Choose a STEP file" : "Choose an IFC file", {}, filter);
    if (!path.isEmpty())
        setFile(path);
}
} // namespace sketchy
#include "app/ifc_import_dialog.moc"
