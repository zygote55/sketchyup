#include "app/ifc_export_dialog.hpp"
#include "io/ifc_export.hpp"
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QPushButton>
#include <QVBoxLayout>

namespace sketchy {
namespace {
// The "Not written" kinds the plan counts, in the order the dialog shows them (ADR 0171 section 16.8).
struct NotWrittenLine {
    const char *key;
    const char *label;
};
constexpr NotWrittenLine kNotWrittenLines[] = {
    {"terrain", "Terrain"},
    {"terrainTiles", "Terrain tiles"},
    {"referenceImages", "Reference images"},
    {"guides", "Guides"},
    {"annotations", "Text, dimensions, annotations"},
    {"looseEdges", "Edges and curves not part of a face"},
    {"sections", "Sections"},
    {"scenes", "Scenes"},
    {"sheets", "Sheets"},
    {"componentParameters", "Component parameters"},
    {"textures", "Textures"},
    {"emptyGroups", "Empty groups"},
    {"bodiesWithoutFaces", "Bodies without faces"},
};
// "Edges and curves not part of a face" is one line for the plan's two kinds.
int notWrittenCount(const QJsonObject &notWritten, const NotWrittenLine &line) {
    int count = notWritten.value(line.key).toInt(0);
    if (QString(line.key) == "looseEdges")
        count += notWritten.value("curves").toInt(0);
    return count;
}
} // namespace

IfcExportDialog::IfcExportDialog(QWidget *parent) : QDialog(parent) {
    setObjectName("ifcExportDialog");
    setWindowTitle("Export IFC");
    build();
}

void IfcExportDialog::build() {
    auto *layout = new QVBoxLayout(this);
    auto *title = new QLabel("Export IFC");
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 4);
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);
    auto *subline = new QLabel("Writes a new IFC4 file from this model. The model is not changed.");
    subline->setStyleSheet("color: #888;");
    layout->addWidget(subline);

    // The form is one Tab stop (its scroll region); an arrow key scrolls it, Enter goes to the
    // default (Export to file...), Esc cancels (ADR 0171 sections 16.8, 16.9).
    formScroll = new QScrollArea;
    formScroll->setObjectName("ifcExportFormScroll");
    formScroll->setWidgetResizable(true);
    formScroll->setFrameShape(QFrame::NoFrame);
    formScroll->setFocusPolicy(Qt::StrongFocus);
    form = new QWidget;
    auto *formLayout = new QVBoxLayout(form);
    formLayout->setContentsMargins(0, 0, 0, 0);

    auto *columns = new QHBoxLayout;
    // Left, Written, in counts: the structure, groups, bodies, component placements, colours,
    // properties, hidden bodies (written, on a layer that is switched off) and identifiers.
    auto *writtenColumn = new QVBoxLayout;
    auto *writtenHeading = new QLabel("Written");
    QFont headingFont = writtenHeading->font();
    headingFont.setBold(true);
    writtenHeading->setFont(headingFont);
    writtenColumn->addWidget(writtenHeading);
    for (const char *name : {"Structure", "Groups", "Bodies", "Components", "Colours", "Properties", "Hidden", "Identifiers"}) {
        auto *row = new QVBoxLayout;
        auto *nameLabel = new QLabel(QString::fromUtf8(name));
        QFont nameFont = nameLabel->font();
        nameFont.setBold(true);
        nameLabel->setFont(nameFont);
        nameLabel->setObjectName(QString("ifcExport%1Name").arg(name));
        auto *text = new QLabel;
        text->setObjectName(QString("ifcExport%1Text").arg(name));
        text->setWordWrap(true);
        row->addWidget(nameLabel);
        row->addWidget(text);
        writtenColumn->addLayout(row);
    }
    writtenColumn->addStretch(1);
    // Right, Not written: one line per kind with its count.
    auto *notWrittenColumn = new QVBoxLayout;
    auto *notWrittenHeading = new QLabel("Not written (counted in the report)");
    notWrittenHeading->setFont(headingFont);
    notWrittenHeading->setWordWrap(true);
    notWrittenColumn->addWidget(notWrittenHeading);
    auto *rows = new QGridLayout;
    int at = 0;
    for (const auto &line : kNotWrittenLines) {
        auto *name = new QLabel(QString::fromUtf8(line.label));
        name->setObjectName(QString("ifcExportNotWritten_%1").arg(line.key));
        auto *count = new QLabel;
        count->setObjectName(QString("ifcExportCount_%1").arg(line.key));
        rows->addWidget(name, at, 0);
        rows->addWidget(count, at, 1, Qt::AlignRight);
        ++at;
    }
    rows->setColumnStretch(0, 1);
    notWrittenColumn->addLayout(rows);
    notWrittenColumn->addStretch(1);
    columns->addLayout(writtenColumn, 3);
    columns->addLayout(notWrittenColumn, 2);
    formLayout->addLayout(columns);

    // Georeferencing: a green line when the site origin and CRS are written, an amber
    // "Not written" that says so in advance (its reason and how to fix it) otherwise.
    auto *geoHeading = new QLabel("Georeferencing");
    geoHeading->setFont(headingFont);
    formLayout->addWidget(geoHeading);
    auto *geoBox = new QFrame;
    geoBox->setObjectName("ifcExportGeoreferenceBox");
    auto *geo = new QLabel;
    geo->setObjectName("ifcExportGeoreference");
    geo->setWordWrap(true);
    auto *geoLayout = new QHBoxLayout(geoBox);
    geoLayout->setContentsMargins(8, 6, 8, 6);
    geoLayout->addWidget(geo);
    formLayout->addWidget(geoBox);

    auto *whatHeading = new QLabel("What this is");
    whatHeading->setFont(headingFont);
    formLayout->addWidget(whatHeading);
    auto *what = new QLabel("IFC4 with tessellated geometry. It is not a certified Reference View. "
                            "Importing it again reproduces geometry, names and properties; other programs may differ.");
    what->setObjectName("ifcExportWhatThisIs");
    what->setWordWrap(true);
    formLayout->addWidget(what);

    formScroll->setWidget(form);
    layout->addWidget(formScroll, 1);

    banner = new QLabel;
    banner->setObjectName("ifcExportBanner");
    banner->setWordWrap(true);
    banner->hide();
    layout->addWidget(banner);
    helperLine = new QLabel;
    helperLine->setObjectName("ifcExportHelperLine");
    helperLine->hide();
    layout->addWidget(helperLine);

    // A new file is created. An existing file is never replaced.  [Cancel] [Export to file...]
    auto *footer = new QHBoxLayout;
    auto *note = new QLabel("A new file is created. An existing file is never replaced.");
    note->setStyleSheet("color: #888;");
    footer->addWidget(note);
    footer->addStretch(1);
    cancel = new QPushButton("Cancel");
    cancel->setObjectName("ifcExportCancel");
    footer->addWidget(cancel);
    go = new QPushButton("Export to file...");
    go->setObjectName("ifcExportGo");
    go->setDefault(true); // Enter exports
    footer->addWidget(go);
    layout->addLayout(footer);
    setTabOrder(formScroll, cancel);
    setTabOrder(cancel, go);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(go, &QPushButton::clicked, this, &QDialog::accept);
}

void IfcExportDialog::setProbe(const IfcProbe &probe) {
    if (probe.state == IfcState::Available) {
        helperLine->setText(QString("✓ IfcOpenShell %1").arg(probe.ifcOpenShell));
        helperLine->setStyleSheet("color: #2e7d32;");
    } else if (probe.state == IfcState::Limited) {
        // Export needs the helper, not the kernel (ADR 0171 section 16.7): a missing kernel never
        // affects it.
        helperLine->setText(QString("✓ IfcOpenShell %1 (the export does not use the kernel)").arg(probe.ifcOpenShell));
        helperLine->setStyleSheet("color: #2e7d32;");
    } else {
        helperLine->clear();
    }
    helperLine->setVisible(!helperLine->text().isEmpty());
}

void IfcExportDialog::setDocument(const Document &document) {
    try {
        // The plan, computed from the open model before anything is written (ADR 0171 section 16.8).
        plan_ = planIfcExport(document);
        fillPlan(plan_);
    } catch (const IfcExportError &error) {
        // A cheap refusal: a red banner here with Export disabled; no file is chosen yet.
        showRefusal(error.code(), QString::fromUtf8(error.what()));
    }
}

void IfcExportDialog::showRefusal(const std::string &code, const QString &message) {
    (void)code;
    refused_ = true;
    banner->setText("Refused — " + message);
    banner->setStyleSheet("color: #b3261e;");
    banner->show();
    go->setEnabled(false);
}

void IfcExportDialog::fillPlan(const QJsonObject &plan) {
    const auto spatial = plan["spatial"].toObject();
    const auto written = plan["written"].toObject();
    const auto notWritten = plan["notWritten"].toObject();
    const auto geo = plan["georeference"].toObject();
    const auto countOf = [&] (const QJsonObject &object, const char *key) { return object.value(key).toInt(0); };
    const auto setWritten = [&] (const char *name, const QString &text) {
        auto *label = form->findChild<QLabel *>(QString("ifcExport%1Text").arg(name));
        if (label)
            label->setText(text);
    };
    // Structure: the IFC structure when the model carries one, or one site and one building;
    // storeys are kept, never invented.
    const int sites = countOf(spatial, "sites");
    const int buildings = countOf(spatial, "buildings");
    const int storeys = countOf(spatial, "storeys");
    if (sites + buildings + storeys > 0)
        setWritten("Structure", QString("The IFC structure is kept: %1 sites, %2 buildings, %3 storeys").arg(sites).arg(buildings).arg(storeys));
    else
        setWritten("Structure", "1 site and 1 building are created; no storeys are invented");
    setWritten("Groups", QString("%1, as assemblies").arg(countOf(written, "groups")));
    setWritten("Bodies", QString("%1, as triangulated shapes. Class from ifc.Class, otherwise IfcBuildingElementProxy")
                     .arg(countOf(written, "bodies")));
    setWritten("Components", QString("%1 placements, written as separate geometry (no instancing yet)").arg(countOf(written, "placements")));
    setWritten("Colours", QString("%1 materials, with per-face colours").arg(countOf(written, "materials")));
    setWritten("Properties",
               QString("%1, as property sets; keys without a dot go in a set named SketchyUp").arg(countOf(written, "properties")));
    const int hidden = countOf(written, "hidden");
    setWritten("Hidden", hidden > 0 ? QString("%1, written on a layer that is switched off").arg(hidden) : "None hidden");
    setWritten("Identifiers", "Globalids are kept from an import where valid and unique; the others are derived from the model, "
                              "so exporting twice gives the same ids");

    for (const auto &line : kNotWrittenLines) {
        auto *label = form->findChild<QLabel *>(QString("ifcExportCount_%1").arg(line.key));
        if (!label)
            continue;
        const int count = notWrittenCount(notWritten, line);
        label->setText(count > 0 ? QString::number(count) : QString{});
        label->setVisible(count > 0);
        form->findChild<QLabel *>(QString("ifcExportNotWritten_%1").arg(line.key))->setVisible(count > 0);
    }

    auto *geoLabel = form->findChild<QLabel *>("ifcExportGeoreference");
    auto *geoBox = form->findChild<QFrame *>("ifcExportGeoreferenceBox");
    const QString result = geo["result"].toString();
    if (result == "written") {
        geoLabel->setText(QString("✓ Written — the site origin and the coordinate system %1 will be written.")
                             .arg(geo["crs"].toString()));
        geoLabel->setStyleSheet("color: #2e7d32;");
        geoBox->setStyleSheet("border: 1px solid #2e7d32; border-radius: 4px;");
    } else if (result == "notWritten") {
        // Its reason and how to fix it, in advance (the same text the report's notice carries).
        geoLabel->setText(geo["reason"].toString() == "noCrsLabel"
                              ? "⚠ Not written — the site origin is set but has no coordinate system label; set it to an authority "
                                "code such as EPSG:32760 in File, Site settings..."
                              : QString("⚠ Not written — the site origin's label \"%1\" is not an authority code such as "
                                        "EPSG:32760; change it in File, Site settings...")
                                    .arg(geo["crs"].toString()));
        geoLabel->setStyleSheet("color: #b26a00;");
        geoBox->setStyleSheet("border: 1px solid #b26a00; border-radius: 4px;");
    } else {
        geoLabel->setText("No site origin; nothing to georeference.");
        geoBox->setStyleSheet(QString{});
    }
}
} // namespace sketchy
