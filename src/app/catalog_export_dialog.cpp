#include "app/catalog_export_dialog.hpp"

#include "app/ifc_sheets.hpp"
#include "io/catalog_insert.hpp"
#include "io/catalog_io.hpp"

#include <QCheckBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace sketchy {
namespace {

// The export's refusal (UX_DESIGN 18.6, 18.7): what did not happen is the first line, then the
// reason, and the model unchanged.
void showExportRefusal(QWidget *parent, const QString &reason) {
    RefusalSheet face;
    face.sheetName = "catalogExportRefused";
    face.title = "Export asset catalog refused";
    face.prefix = "catalogExport";
    face.leadingLine = QStringLiteral("✕ Nothing was written.");
    face.summary = QStringLiteral("Export refused: %1. Your model is unchanged.").arg(reason);
    face.closeButton = "OK";
    face.closeSuffix = "OK";
    face.closeIsDefault = true;
    face.width = 560;
    face.height = 260;
    showRefusalSheet(parent, face);
}

// A list of the model's records, each one a checkable row named by the record's own name.
void fillChoices(QListWidget *list, const QStringList &names, const QList<qulonglong> &ids) {
    for (qsizetype i = 0; i < names.size(); ++i) {
        auto *item = new QListWidgetItem(names.at(i), list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
        item->setData(Qt::UserRole, ids.at(i));
    }
}

} // namespace

CatalogExportDialog::CatalogExportDialog(Document &doc, QWidget *parent)
    : QDialog(parent), doc_(doc) {
    setObjectName("catalogExportDialog");
    setWindowTitle("Export asset catalog");
    resize(880, 620);
    auto *layout = new QVBoxLayout(this);

    auto *intro = new QLabel(
        "Writes a new .sketchycat file from this model: the chosen components and materials, their "
        "textures, and the document style. The model is not changed.");
    intro->setWordWrap(true);
    layout->addWidget(intro);

    // The catalog's own fields. The name is the catalog's name, not the file's.
    auto *form = new QVBoxLayout;
    auto *nameRow = new QHBoxLayout;
    name_ = new QLineEdit("Asset catalog");
    name_->setObjectName("catalogExportName");
    name_->setAccessibleName("Catalog name");
    name_->setMaxLength(256);
    nameRow->addWidget(new QLabel("Name"));
    nameRow->addWidget(name_, 1);
    form->addLayout(nameRow);
    description_ = new QPlainTextEdit;
    description_->setObjectName("catalogExportDescription");
    description_->setAccessibleName("Catalog description");
    description_->setTabChangesFocus(true);
    description_->setMaximumHeight(72);
    form->addWidget(new QLabel("Description"));
    form->addWidget(description_);
    auto *metaRow = new QHBoxLayout;
    labels_ = new QLineEdit;
    labels_->setObjectName("catalogExportLabels");
    labels_->setAccessibleName("Labels, comma separated");
    version_ = new QLineEdit("1.0.0");
    version_->setObjectName("catalogExportVersion");
    version_->setAccessibleName("Version");
    version_->setMaxLength(64);
    license_ = new QLineEdit("LicenseRef-Unspecified");
    license_->setObjectName("catalogExportLicense");
    license_->setAccessibleName("Licence identifier");
    license_->setMaxLength(64);
    metaRow->addWidget(new QLabel("Labels"));
    metaRow->addWidget(labels_, 1);
    metaRow->addWidget(new QLabel("Version"));
    metaRow->addWidget(version_);
    metaRow->addWidget(new QLabel("Licence"));
    metaRow->addWidget(license_);
    form->addLayout(metaRow);
    layout->addLayout(form);

    // What the file will hold: the model's components and materials, checked by default, and the
    // style. Each choice is named, so a list of two components reads as two components.
    auto *choices = new QHBoxLayout;
    auto *componentColumn = new QVBoxLayout;
    componentColumn->addWidget(new QLabel("Components"));
    components_ = new QListWidget;
    components_->setObjectName("catalogExportComponents");
    components_->setAccessibleName("Components to export");
    QStringList componentNames;
    QList<qulonglong> componentIds;
    for (const auto &[id, definition] : doc_.definitions()) {
        componentNames << QString::fromStdString(definition->name);
        componentIds << qulonglong(id);
    }
    fillChoices(components_, componentNames, componentIds);
    componentColumn->addWidget(components_);
    choices->addLayout(componentColumn, 1);

    auto *materialColumn = new QVBoxLayout;
    materialColumn->addWidget(new QLabel("Materials"));
    materials_ = new QListWidget;
    materials_->setObjectName("catalogExportMaterials");
    materials_->setAccessibleName("Materials to export");
    QStringList materialNames;
    QList<qulonglong> materialIds;
    for (const auto &[id, material] : doc_.materials()) {
        materialNames << QString::fromStdString(material->name);
        materialIds << qulonglong(id);
    }
    fillChoices(materials_, materialNames, materialIds);
    materialColumn->addWidget(materials_);
    choices->addLayout(materialColumn, 1);
    layout->addLayout(choices, 1);

    style_ = new QCheckBox("Include the document style, with its edge stroke and edge texture");
    style_->setObjectName("catalogExportStyle");
    style_->setChecked(true);
    layout->addWidget(style_);

    auto *holds = new QLabel(
        "Not written: tags, scenes and geometry outside the chosen components. A texture that a "
        "chosen record uses is stored once, in the file. If a chosen record's texture is missing "
        "from this model, the export is refused and names it.");
    holds->setWordWrap(true);
    layout->addWidget(holds);

    reason_ = new QLabel;
    reason_->setObjectName("catalogExportReason");
    reason_->setWordWrap(true);
    reason_->setTextFormat(Qt::PlainText);
    layout->addWidget(reason_);

    // The footer: a new file is created and an existing one is never replaced. Export to file… is
    // the default, and it is off until something is chosen.
    auto *footer = new QHBoxLayout;
    auto *note = new QLabel("A new file is created. An existing file is never replaced.");
    note->setWordWrap(true);
    footer->addWidget(note, 1);
    auto *cancel = new QPushButton("Cancel");
    cancel->setObjectName("catalogExportCancel");
    exportButton_ = new QPushButton("Export to file…");
    exportButton_->setObjectName("catalogExportButton");
    exportButton_->setDefault(true);
    footer->addWidget(cancel);
    footer->addWidget(exportButton_);
    layout->addLayout(footer);

    connect(components_, &QListWidget::itemChanged, this, [this] { refreshState(); });
    connect(materials_, &QListWidget::itemChanged, this, [this] { refreshState(); });
    connect(style_, &QCheckBox::toggled, this, [this] { refreshState(); });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(exportButton_, &QPushButton::clicked, this, &CatalogExportDialog::exportToFile);
    refreshState();
}

void CatalogExportDialog::refreshState() {
    qsizetype chosen = style_->isChecked() ? 1 : 0;
    for (auto *list : {components_, materials_})
        for (int row = 0; row < list->count(); ++row)
            if (list->item(row)->checkState() == Qt::Checked)
                ++chosen;
    exportButton_->setEnabled(chosen > 0);
    reason_->setText(chosen > 0 ? QString()
                                : QStringLiteral("Choose at least one component, material or the "
                                                 "style to export."));
    exportButton_->setAccessibleDescription(reason_->text());
}

void CatalogExportDialog::exportToFile() {
    CatalogExportRequest request;
    request.name = name_->text().trimmed();
    request.description = description_->toPlainText();
    request.version = version_->text().trimmed();
    request.licenseIdentifier = license_->text().trimmed();
    for (const auto &label : labels_->text().split(QLatin1Char(','), Qt::SkipEmptyParts))
        request.labels.append(label.trimmed());
    for (int row = 0; row < components_->count(); ++row)
        if (components_->item(row)->checkState() == Qt::Checked)
            request.components.push_back(
                Id(components_->item(row)->data(Qt::UserRole).toULongLong()));
    for (int row = 0; row < materials_->count(); ++row)
        if (materials_->item(row)->checkState() == Qt::Checked)
            request.materials.push_back(
                Id(materials_->item(row)->data(Qt::UserRole).toULongLong()));
    request.style = style_->isChecked();
    const auto entries =
        qsizetype(request.components.size() + request.materials.size()) + (request.style ? 1 : 0);

    // The bytes are built and checked before the chooser opens, so a refusal writes nothing.
    QByteArray bytes;
    try {
        bytes = exportCatalog(doc_, request);
    } catch (const std::exception &failure) {
        showExportRefusal(this, QString::fromUtf8(failure.what()));
        return;
    }

    // The system Save dialog, with no overwrite prompt (as the IFC export's): a name that exists is
    // refused below, with nothing written.
    QFileDialog save(this, "New asset catalog file");
    save.setObjectName("catalogExportFileDialog");
    save.setAcceptMode(QFileDialog::AcceptSave);
    save.setFileMode(QFileDialog::AnyFile);
    save.setOption(QFileDialog::DontConfirmOverwrite);
    save.setNameFilter("SketchyUp catalogs (*.sketchycat);;All files (*)");
    save.setDefaultSuffix("sketchycat");
    if (save.exec() != QDialog::Accepted || save.selectedFiles().isEmpty())
        return;
    const auto path = save.selectedFiles().constFirst();
    const QFileInfo info(path);
    if (info.exists() || info.isSymLink()) {
        showExportRefusal(this, QStringLiteral("That file already exists. An existing file is never "
                                               "replaced; choose a new name"));
        return;
    }
    try {
        writeCatalog(bytes, path);
    } catch (const std::exception &failure) {
        showExportRefusal(this, QString::fromUtf8(failure.what()));
        return;
    }
    exported_ = QStringLiteral("Exported asset catalog “%1”: %2 entries. Your model is unchanged.")
                    .arg(request.name)
                    .arg(entries);
    accept();
}

} // namespace sketchy
