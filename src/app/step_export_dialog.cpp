#include "app/step_export_dialog.hpp"
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QVBoxLayout>
namespace sketchy {
namespace {
// The Written lines, in the order the dialog shows them (ADR 0172 sections 3 and 4, the R098.c notes).
struct WrittenLine {
    const char *key;
    const char *heading;
};
constexpr WrittenLine kWrittenLines[] = {
    {"Structure", "Structure"}, {"Bodies", "Bodies"},       {"Components", "Components"}, {"Colours", "Colours"},
    {"Geometry", "Geometry"},   {"Hidden", "Hidden"},       {"Layers", "Layers and names"},
};
} // namespace

const std::vector<StepNotWrittenKind> &stepNotWrittenKinds() {
    static const std::vector<StepNotWrittenKind> kinds{
        {"terrain", "Terrain and reference images", "notWritten"},
        {"untessellated", "Faces that could not be tessellated", "notTriangulated"},
        {"degenerate", "Zero-area triangles, left out", "degenerateTriangles"},
        {"layers", "A layer's own switch; its bodies keep their hidden state", nullptr},
        {"materials", "Material names and opacity; the colour is kept", nullptr},
        {"textures", "Textures", nullptr},
        {"annotations", "Guides, text, dimensions and annotations", nullptr},
        {"sections", "Sections and scenes", nullptr},
        {"sheets", "Sheets", nullptr},
        {"curves", "Edges and curves that are not part of a face", nullptr},
        {"site", "The site origin and its coordinate system", nullptr},
    };
    return kinds;
}

int stepNotWrittenCount(const QJsonObject &plan, const StepNotWrittenKind &kind) {
    return kind.countKey ? plan.value(kind.countKey).toInt(0) : 0;
}

StepExportDialog::StepExportDialog(QWidget *parent) : QDialog(parent) {
    setObjectName("stepExportDialog");
    setWindowTitle("Export STEP");
    build();
}

void StepExportDialog::build() {
    auto *layout = new QVBoxLayout(this);
    auto *title = new QLabel("Export STEP");
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 4);
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);
    auto *subline = new QLabel("Writes a new STEP file from this model. The model is not changed.");
    subline->setStyleSheet("color: #888;");
    layout->addWidget(subline);

    // The schema choice sits above the form, so Tab reaches it before the form's scroll region.
    // AP214 is the default; AP242 is not offered (ADR 0172 section 2: it is refused).
    auto *schemaBox = new QGroupBox("Schema");
    schemaBox->setObjectName("stepExportSchemaBox");
    auto *schemaLayout = new QVBoxLayout(schemaBox);
    schemaAp214 = new QRadioButton("AP214 (the default)");
    schemaAp214->setObjectName("stepExportSchemaAP214");
    schemaAp214->setAccessibleName("STEP schema: AP214, the default");
    schemaAp214->setChecked(true);
    schemaAp203 = new QRadioButton("AP203");
    schemaAp203->setObjectName("stepExportSchemaAP203");
    schemaAp203->setAccessibleName("STEP schema: AP203");
    auto *schemaNote = new QLabel("AP203 is the older schema, for a program that reads only it. AP242 is not written.");
    schemaNote->setStyleSheet("color: #888;");
    schemaNote->setWordWrap(true);
    schemaLayout->addWidget(schemaAp214);
    schemaLayout->addWidget(schemaAp203);
    schemaLayout->addWidget(schemaNote);
    layout->addWidget(schemaBox);

    // The form is one Tab stop (its scroll region); an arrow key scrolls it.
    formScroll = new QScrollArea;
    formScroll->setObjectName("stepExportFormScroll");
    formScroll->setWidgetResizable(true);
    formScroll->setFrameShape(QFrame::NoFrame);
    formScroll->setFocusPolicy(Qt::StrongFocus);
    form = new QWidget;
    auto *formLayout = new QVBoxLayout(form);
    formLayout->setContentsMargins(0, 0, 0, 0);

    QFont headingFont = subline->font();
    headingFont.setBold(true);
    auto *columns = new QHBoxLayout;
    // Left, Written: the groups, bodies, parts, colours, geometry, hidden bodies and layer names.
    auto *writtenColumn = new QVBoxLayout;
    auto *writtenHeading = new QLabel("Written");
    writtenHeading->setFont(headingFont);
    writtenColumn->addWidget(writtenHeading);
    for (const auto &line : kWrittenLines) {
        auto *nameLabel = new QLabel(QString::fromUtf8(line.heading));
        QFont nameFont = nameLabel->font();
        nameFont.setBold(true);
        nameLabel->setFont(nameFont);
        nameLabel->setObjectName(QString("stepExport%1Name").arg(line.key));
        auto *text = new QLabel;
        text->setObjectName(QString("stepExport%1Text").arg(line.key));
        text->setWordWrap(true);
        writtenColumn->addWidget(nameLabel);
        writtenColumn->addWidget(text);
    }
    writtenColumn->addStretch(1);
    // Right, Not written: one line per kind; a counted kind shows its count when the model has one.
    auto *notWrittenColumn = new QVBoxLayout;
    auto *notWrittenHeading = new QLabel("Not written");
    notWrittenHeading->setFont(headingFont);
    notWrittenColumn->addWidget(notWrittenHeading);
    auto *rows = new QGridLayout;
    int at = 0;
    for (const auto &kind : stepNotWrittenKinds()) {
        auto *name = new QLabel(QString::fromUtf8(kind.label));
        name->setWordWrap(true);
        name->setObjectName(QString("stepExportNotWritten_%1").arg(kind.key));
        auto *count = new QLabel;
        count->setObjectName(QString("stepExportCount_%1").arg(kind.key));
        rows->addWidget(name, at, 0);
        rows->addWidget(count, at, 1, Qt::AlignRight | Qt::AlignTop);
        ++at;
    }
    rows->setColumnStretch(0, 1);
    notWrittenColumn->addLayout(rows);
    notWrittenColumn->addStretch(1);
    columns->addLayout(writtenColumn, 3);
    columns->addLayout(notWrittenColumn, 2);
    formLayout->addLayout(columns);

    auto *whatHeading = new QLabel("What this is");
    whatHeading->setFont(headingFont);
    formLayout->addWidget(whatHeading);
    auto *what = new QLabel("STEP from SketchyUp is a mesh. Each face of the model is written as one flat face, and curves "
                            "are already facets: parametric and NURBS surfaces are not created, and the file keeps no curve "
                            "or surface definition. Importing it again in SketchyUp reproduces the shapes, names, colours, "
                            "layers and hidden bodies.");
    what->setObjectName("stepExportWhatThisIs");
    what->setWordWrap(true);
    formLayout->addWidget(what);

    formScroll->setWidget(form);
    layout->addWidget(formScroll, 1);

    banner = new QLabel;
    banner->setObjectName("stepExportBanner");
    banner->setWordWrap(true);
    banner->hide();
    layout->addWidget(banner);
    // Amber, and the words say what it means (the colour is not the only signal).
    sizeWarning = new QLabel;
    sizeWarning->setObjectName("stepExportSizeWarning");
    sizeWarning->setWordWrap(true);
    sizeWarning->setStyleSheet("color: #b26a00;");
    sizeWarning->hide();
    layout->addWidget(sizeWarning);
    helperLine = new QLabel;
    helperLine->setObjectName("stepExportHelperLine");
    helperLine->hide();
    layout->addWidget(helperLine);

    // A new file is created. An existing file is never replaced.  [Cancel] [Export to file...]
    auto *footer = new QHBoxLayout;
    auto *note = new QLabel("A new file is created. An existing file is never replaced.");
    note->setStyleSheet("color: #888;");
    footer->addWidget(note);
    footer->addStretch(1);
    cancel = new QPushButton("Cancel");
    cancel->setObjectName("stepExportCancel");
    footer->addWidget(cancel);
    go = new QPushButton("Export to file...");
    go->setObjectName("stepExportGo");
    go->setDefault(true); // Enter exports
    footer->addWidget(go);
    layout->addLayout(footer);
    setTabOrder(schemaAp214, schemaAp203);
    setTabOrder(schemaAp203, formScroll);
    setTabOrder(formScroll, cancel);
    setTabOrder(cancel, go);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(go, &QPushButton::clicked, this, &QDialog::accept);
}

void StepExportDialog::setProbe(const IfcProbe &probe) {
    if (probe.state == IfcState::Available) {
        helperLine->setText("✓ The STEP export helper is available (sketchyup-ifc-worker)");
        helperLine->setStyleSheet("color: #2e7d32;");
    } else if (probe.state == IfcState::Limited) {
        // The STEP export needs the helper, not the kernel (as the STEP import does): a missing
        // kernel never affects it.
        helperLine->setText("✓ The STEP export helper is available (sketchyup-ifc-worker); the export does not use the kernel");
        helperLine->setStyleSheet("color: #2e7d32;");
    } else {
        helperLine->clear();
    }
    helperLine->setVisible(!helperLine->text().isEmpty());
}

void StepExportDialog::setTriangleWarningLimit(qint64 triangles) {
    triangleLimit_ = triangles;
    updateSizeWarning();
}

StepExportOptions StepExportDialog::options() const {
    StepExportOptions options;
    options.schema = schemaAp203->isChecked() ? QStringLiteral("ap203") : QStringLiteral("ap214");
    return options;
}

void StepExportDialog::setDocument(const Document &document) {
    refused_ = false;
    banner->hide();
    go->setEnabled(true);
    try {
        // The plan, computed from the open model before anything is written: the capture the export
        // itself makes, with no file and no helper.
        plan_ = captureStepExport(document, QStringLiteral("model.step")).plan;
        fillPlan(plan_);
    } catch (const StepExportError &error) {
        // A cheap refusal: a red banner here with Export disabled; no file is chosen yet.
        showRefusal(error.code(), QString::fromUtf8(error.what()));
    }
}

void StepExportDialog::showRefusal(const std::string &code, const QString &message) {
    refused_ = true;
    plan_ = {};
    const QString text = code == "emptyModel" ? QStringLiteral("Nothing to export: the model has no body with faces.") : message;
    banner->setText("Refused — " + text);
    banner->setStyleSheet("color: #b3261e;");
    banner->show();
    sizeWarning->hide();
    go->setEnabled(false);
}

void StepExportDialog::updateSizeWarning() {
    // The plan's triangles are the file's triangles (the degenerate ones are already left out).
    const qint64 triangles = plan_.value("triangles").toInteger(0);
    const bool over = !refused_ && triangles > triangleLimit_;
    if (over)
        sizeWarning->setText(QString("⚠ This model has %1 triangles. A file this large may exceed the 256 MiB limit and be "
                                     "refused; a refused export writes nothing.")
                                 .arg(triangles));
    sizeWarning->setVisible(over);
}

void StepExportDialog::fillPlan(const QJsonObject &plan) {
    const auto countOf = [&](const char *key) { return plan.value(key).toInt(0); };
    const auto setWritten = [&](const char *key, const QString &text) {
        if (auto *label = form->findChild<QLabel *>(QString("stepExport%1Text").arg(key)))
            label->setText(text);
    };
    const int groups = countOf("groups");
    const int elements = countOf("elements");
    const int meshes = countOf("meshes");
    const int faces = countOf("faces");
    const int triangles = countOf("triangles");
    const int hidden = countOf("hidden");
    setWritten("Structure", groups > 0
                                ? QString("%1 groups, as assemblies; each body stays under its group. Names are kept.").arg(groups)
                                : QString("No groups: the bodies are written at the top level. Names are kept."));
    setWritten("Bodies", QString("%1 bodies. A closed body is a solid; an open one is a shell.").arg(elements));
    setWritten("Components", QString("%1 parts for %2 bodies. A shape that repeats is one part with several instances.")
                                 .arg(meshes)
                                 .arg(elements));
    setWritten("Colours", QString("Per-face colours on %1 faces").arg(faces));
    setWritten("Geometry", QString("%1 triangles on %2 faces, the mesh the file carries").arg(triangles).arg(faces));
    setWritten("Hidden", hidden > 0 ? QString("%1 hidden, written invisible; a body on a hidden layer counts too").arg(hidden)
                                    : QString("None hidden"));
    setWritten("Layers", QString("Each body keeps its layer name"));

    for (const auto &kind : stepNotWrittenKinds()) {
        auto *name = form->findChild<QLabel *>(QString("stepExportNotWritten_%1").arg(kind.key));
        auto *count = form->findChild<QLabel *>(QString("stepExportCount_%1").arg(kind.key));
        if (!name || !count)
            continue;
        if (!kind.countKey) {
            // A kind the plan does not count: always listed by name, with no count.
            count->clear();
            count->setVisible(false);
            name->setVisible(true);
            continue;
        }
        // A counted kind shows only when the model has some (as the IFC export's lines do).
        const int n = stepNotWrittenCount(plan, kind);
        count->setText(n > 0 ? QString::number(n) : QString{});
        count->setVisible(n > 0);
        name->setVisible(n > 0);
    }
    updateSizeWarning();
}
} // namespace sketchy
