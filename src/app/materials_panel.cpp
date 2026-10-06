#include "app/materials_panel.hpp"
#include "app/texture_editor.hpp"
#include "core/face_textures.hpp"
#include "io/assets.hpp"
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>
namespace sketchy {
namespace {
QString sid(Id id) { return QString::number(id); }
QString describe(const Document &doc, Id id) {
    if (!id)
        return "Original face color";
    const auto &m = *doc.materials().at(id);
    return QString::fromStdString(m.name) + QString(" · %1% opacity").arg(qRound(m.opacity * 100));
}
} // namespace
MaterialsPanel::MaterialsPanel(Document &doc, Viewport &view, QWidget *parent)
    : QWidget(parent), doc_(doc), view_(view) {
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea;
    scroll->setObjectName("materialScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *content = new QWidget;
    scroll->setWidget(content);
    outer->addWidget(scroll);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *heading = new QLabel("In-model materials");
    layout->addWidget(heading);
    swatches_ = new QListWidget;
    swatches_->setObjectName("materialSwatches");
    swatches_->setAccessibleName("In-model material swatches");
    swatches_->setMinimumHeight(100);
    swatches_->setMaximumHeight(180);
    layout->addWidget(swatches_, 1);
    side_ = new QComboBox;
    side_->setObjectName("materialSide");
    side_->addItems({"Front", "Back", "Both sides"});
    side_->setCurrentIndex(2);
    side_->setAccessibleName("Material assignment side");
    layout->addWidget(side_);
    selection_ = new QLabel;
    selection_->setObjectName("materialSelection");
    selection_->setWordWrap(true);
    selection_->setTextFormat(Qt::PlainText);
    layout->addWidget(selection_);
    details_ = new QLabel;
    details_->setObjectName("materialDetails");
    details_->setWordWrap(true);
    details_->setTextFormat(Qt::PlainText);
    layout->addWidget(details_);
    auto *buttons = new QGridLayout;
    auto add = [&](int n, const char *id, const char *label, std::function<void()> operation) {
        auto *button = new QPushButton(label);
        button->setObjectName(id);
        button->setMinimumHeight(button->fontMetrics().height() + 20);
        buttons->addWidget(button, n / 2, n % 2);
        connect(button, &QPushButton::clicked, this, [this, operation] { attempt(operation); });
    };
    add(0, "materialNew", "New…", [this] { edit(true); });
    add(1, "materialEdit", "Edit…", [this] { edit(false); });
    add(2, "materialPaint", "Paint (B)", [this] {
        view_.setTool(Viewport::Tool::Paint);
        view_.setFocus();
    });
    add(3, "materialApply", "Apply", [this] { view_.applyMaterialToSelection(); });
    add(4, "materialAttach", "Attach file…", [this] { attach(false); });
    add(5, "materialResolve", "Replace file…", [this] { attach(true); });
    add(6, "materialDetach", "Detach file", [this] { detach(); });
    add(7, "materialDelete", "Delete", [this] { remove(); });
    add(8, "materialPurgeAssets", "Clean files", [this] {
        std::set<Id> used;
        for (const auto &[id, material] : doc_.materials())
            if (material->asset)
                used.insert(material->asset);
        QJsonArray commands;
        for (const auto &[id, asset] : doc_.assets())
            if (!used.contains(id))
                commands.append(QJsonObject{{"command", "asset.delete"}, {"asset", sid(id)}});
        if (!commands.empty())
            view_.editMaterials(commands);
    });
    add(9, "materialTexture", "Texture mapping…", [this] {
        editTextureMapping(doc_, view_, this, side_->currentIndex());
    });
    add(10, "materialResetTexture", "Reset mapping", [this] {
        view_.mapSelectedTextures(QJsonValue::Null, "local", side_->currentIndex());
    });
    layout->addLayout(buttons);
    findChild<QPushButton *>("materialAttach")
        ->setToolTip("Store a file in the model; PNG/JPEG images appear on painted faces");
    findChild<QPushButton *>("materialPurgeAssets")
        ->setToolTip("Remove files that no material uses; undo restores them");
    auto *hint = new QLabel("Alt-click with Paint samples the visible side. PNG/JPEG images repeat "
                            "across painted faces and stay packaged with the model.");
    hint->setWordWrap(true);
    layout->addWidget(hint);
    error_ = new QLabel;
    error_->setObjectName("materialError");
    error_->setWordWrap(true);
    error_->setTextFormat(Qt::PlainText);
    layout->addWidget(error_);
    setFocusProxy(swatches_);
    auto choose = [this] {
        if (syncing_ || !swatches_->currentItem())
            return;
        attempt([&] {
            view_.setPaintMaterial(swatches_->currentItem()->data(Qt::UserRole).toULongLong(),
                                   side_->currentIndex());
        });
    };
    connect(swatches_, &QListWidget::currentRowChanged, this, choose);
    connect(side_, &QComboBox::currentIndexChanged, this, choose);
    connect(&view_, &Viewport::materialChanged, this, [this] { refresh(); });
    refresh();
}
void MaterialsPanel::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    refresh();
}
void MaterialsPanel::attempt(const std::function<void()> &operation) {
    try {
        operation();
        error_->clear();
        refresh();
    } catch (const std::exception &error) {
        error_->setText(error.what());
        QTimer::singleShot(0, this, [this] {
            findChild<QScrollArea *>("materialScroll")->ensureWidgetVisible(error_);
        });
    }
}
void MaterialsPanel::refresh() {
    if (syncing_)
        return;
    syncing_ = true;
    const QSignalBlocker listBlock(swatches_), sideBlock(side_);
    const auto current = view_.paintMaterial();
    if (!doc_.owns(stamp_) || materials_ != doc_.materials() || assets_ != doc_.assets() ||
        swatches_->count() == 0) {
        swatches_->clear();
        auto add = [&](Id id, const QString &name, QColor color) {
            auto *item = new QListWidgetItem(name, swatches_);
            item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(id));
            QPixmap icon(20, 20);
            icon.fill(color);
            item->setIcon(QIcon(icon));
        };
        add(0, "Original face color", palette().color(QPalette::Base));
        for (const auto &[id, material] : doc_.materials()) {
            auto name = QString::fromStdString(material->name);
            if (material->asset && !doc_.assets().at(material->asset)->payload)
                name += " · Missing file";
            add(id, name,
                QColor::fromRgbF(material->color[0], material->color[1], material->color[2]));
        }
        materials_ = doc_.materials();
        assets_ = doc_.assets();
    }
    for (int row = 0; row < swatches_->count(); ++row)
        if (swatches_->item(row)->data(Qt::UserRole).toULongLong() == current)
            swatches_->setCurrentRow(row);
    side_->setCurrentIndex(view_.paintSide());
    findChild<QPushButton *>("materialResolve")->setText("Replace file…");
    QString detail = describe(doc_, current);
    if (current) {
        const auto asset = doc_.materials().at(current)->asset;
        if (asset) {
            const auto &record = *doc_.assets().at(asset);
            if (!record.payload)
                findChild<QPushButton *>("materialResolve")->setText("Resolve file…");
            detail += "\n" + QString::fromStdString(record.name) +
                      (record.payload ? " · Stored" : " · Missing");
            detail += "\nReplacing this file updates every material using it.";
        } else
            detail += "\nNo attached file";
    }
    details_->setText(detail);
    selection_->setText("Select a face to inspect both sides.");
    const auto &selected = view_.selectionState().entities();
    if (selected.size() == 1 && selected.begin()->kind == SelectionKind::Face) {
        const auto entity = *selected.begin();
        if (doc_.bodies().contains(entity.body) &&
            doc_.bodies().at(entity.body)->surface.faces.contains(entity.entity)) {
            const auto sides = faceMaterials(*doc_.bodies().at(entity.body), entity.entity);
            const auto mapping = faceTextureMappings(*doc_.bodies().at(entity.body), entity.entity);
            selection_->setText("Front: " + describe(doc_, sides.front) +
                                (mapping.front ? " · Custom mapping" : " · Default mapping") +
                                "\nBack: " + describe(doc_, sides.back) +
                                (mapping.back ? " · Custom mapping" : " · Default mapping"));
        }
    }
    stamp_ = doc_.saveStamp();
    syncing_ = false;
}
void MaterialsPanel::edit(bool create) {
    const auto id = create ? 0 : view_.paintMaterial();
    if (!create && !id)
        throw std::runtime_error("Choose a named swatch to edit");
    const auto stamp = doc_.saveStamp();
    const auto revision = doc_.revision();
    QDialog dialog(this);
    dialog.setObjectName("materialDialog");
    dialog.setWindowTitle(create ? "New material" : "Edit material");
    auto *form = new QFormLayout(&dialog);
    auto *name = new QLineEdit(create ? "New material"
                                      : QString::fromStdString(doc_.materials().at(id)->name));
    name->setObjectName("materialName");
    name->selectAll();
    const auto rgb =
        create ? std::array<float, 3>{.73f, .79f, .73f} : doc_.materials().at(id)->color;
    auto *color = new QLineEdit(QColor::fromRgbF(rgb[0], rgb[1], rgb[2]).name());
    color->setObjectName("materialColor");
    auto *opacity = new QDoubleSpinBox;
    opacity->setObjectName("materialOpacity");
    opacity->setRange(0, 100);
    opacity->setDecimals(2);
    opacity->setSuffix(" %");
    opacity->setValue(create ? 100 : doc_.materials().at(id)->opacity * 100);
    const auto initialOpacity = opacity->value();
    auto *asset = new QComboBox;
    asset->setObjectName("materialAsset");
    asset->addItem("No attached file", "0");
    for (const auto &[assetId, record] : doc_.assets())
        asset->addItem(QString::fromStdString(record->name) +
                           (record->payload ? " · Stored" : " · Missing"),
                       sid(assetId));
    if (id)
        asset->setCurrentIndex(asset->findData(sid(doc_.materials().at(id)->asset)));
    if (create) {
        struct Preset {
            const char *name;
            const char *color;
            double opacity;
        };
        static constexpr std::array<Preset, 5> presets{{{"White", "#e6e6e6", 100},
                                                        {"Concrete", "#b0ada6", 100},
                                                        {"Terracotta", "#c45f42", 100},
                                                        {"Steel", "#7c8a96", 100},
                                                        {"Glass", "#b7d9e5", 35}}};
        auto *library = new QComboBox;
        library->setObjectName("materialLibrary");
        library->addItem("Custom");
        for (const auto &preset : presets) {
            QPixmap swatch(16, 16);
            swatch.fill(QColor(preset.color));
            library->addItem(QIcon(swatch), preset.name);
        }
        form->addRow("Local library", library);
        connect(library, &QComboBox::currentIndexChanged, &dialog, [=](int index) {
            if (index <= 0)
                return;
            const auto &preset = presets[size_t(index - 1)];
            name->setText(preset.name);
            color->setText(preset.color);
            color->setModified(true);
            opacity->setValue(preset.opacity);
        });
    }
    form->addRow("Name", name);
    auto *colorRow = new QHBoxLayout;
    colorRow->addWidget(color);
    auto *choose = new QPushButton("Choose…");
    choose->setObjectName("materialChooseColor");
    colorRow->addWidget(choose);
    connect(choose, &QPushButton::clicked, &dialog, [&] {
        const auto chosen =
            QColorDialog::getColor(QColor(color->text()), &dialog, "Material color");
        if (chosen.isValid()) {
            color->setText(chosen.name());
            color->setModified(true);
        }
    });
    form->addRow("Color", colorRow);
    form->addRow("Opacity", opacity);
    form->addRow("Stored file", asset);
    auto *error = new QLabel;
    error->setObjectName("materialDialogError");
    error->setWordWrap(true);
    error->setTextFormat(Qt::PlainText);
    form->addRow(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            if (doc_.revision() != revision || !doc_.isCurrentSnapshot(stamp))
                throw std::runtime_error("The model changed; cancel and reopen this editor");
            const QColor chosen(color->text());
            if (!chosen.isValid() || color->text().size() != 7 || !color->text().startsWith('#'))
                throw std::runtime_error("Enter a color as #RRGGBB");
            QJsonObject command{{"command", create ? "material.create" : "material.edit"},
                                {"name", name->text()},
                                {"asset", asset->currentData().toString()}};
            // Preserve untouched floating-point channels and opacity exactly.
            if (create || color->isModified())
                command["color"] = QJsonArray{chosen.redF(), chosen.greenF(), chosen.blueF()};
            if (create || opacity->value() != initialOpacity)
                command["opacity"] = opacity->value() / 100;
            const auto next = doc_.nextMaterialId();
            if (!create)
                command["material"] = sid(id);
            view_.editMaterials({command});
            view_.setPaintMaterial(create ? next : id, view_.paintSide());
            dialog.accept();
        } catch (const std::exception &failure) {
            error->setText(failure.what());
        }
    });
    dialog.exec();
}
void MaterialsPanel::attach(bool replace) {
    const auto id = view_.paintMaterial();
    if (!id)
        throw std::runtime_error("Choose a named swatch first");
    const auto asset = doc_.materials().at(id)->asset;
    if (replace && !asset)
        throw std::runtime_error("This material has no file to replace");
    const auto stamp = doc_.saveStamp();
    const auto revision = doc_.revision();
    const auto path =
        QFileDialog::getOpenFileName(this, replace ? "Replace stored file" : "Attach file");
    if (path.isEmpty())
        return;
    if (!doc_.isCurrentSnapshot(stamp) || doc_.revision() != revision)
        throw std::runtime_error("The model changed while choosing a file; try again");
    const auto file = readAssetFile(path);
    const auto data = QString::fromLatin1(assetByteArray(file.payload).toBase64());
    if (replace)
        view_.editMaterials({QJsonObject{{"command", "asset.replace"},
                                         {"asset", sid(asset)},
                                         {"data", data},
                                         {"mediaType", QString::fromStdString(file.mediaType)}}});
    else
        view_.editMaterials({QJsonObject{{"command", "asset.import"},
                                         {"name", QString::fromStdString(file.name)},
                                         {"mediaType", QString::fromStdString(file.mediaType)},
                                         {"data", data}},
                             QJsonObject{{"command", "material.edit"},
                                         {"material", sid(id)},
                                         {"asset", sid(doc_.nextAssetId())}}});
}
void MaterialsPanel::detach() {
    const auto id = view_.paintMaterial();
    if (!id)
        throw std::runtime_error("Choose a named swatch first");
    view_.editMaterials(
        {QJsonObject{{"command", "material.edit"}, {"material", sid(id)}, {"asset", "0"}}});
}
void MaterialsPanel::remove() {
    const auto id = view_.paintMaterial();
    if (!id)
        throw std::runtime_error("Choose an unused named swatch to delete");
    view_.editMaterials({QJsonObject{{"command", "material.delete"}, {"material", sid(id)}}});
}
} // namespace sketchy
