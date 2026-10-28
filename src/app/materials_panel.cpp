#include "app/materials_panel.hpp"
#include "app/accessible_names.hpp"
#include "app/keypad_decimal.hpp"
#include "app/texture_editor.hpp"
#include "core/face_textures.hpp"
#include "io/assets.hpp"
#include "io/texture_presets.hpp"
#include <QCheckBox>
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
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStyle>
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

// The library's item data: -1 for Custom, 0 to 5 for a colour preset, and this plus the index in
// texturePresets() for a textured preset.
constexpr int texturedLibraryKey = 100;

QString text(std::string_view value) {
    return QString::fromUtf8(value.data(), qsizetype(value.size()));
}

// The asset already in the model with the same media type and equal bytes, or 0 (ADR 0131). A
// textured preset reuses it instead of adding a copy (UX_DESIGN 18.4).
Id identicalTexture(const Document &doc, const QByteArray &bytes) {
    for (const auto &[id, asset] : doc.assets())
        if (asset->payload && asset->mediaType == texturePresetMediaType &&
            assetByteArray(asset->payload) == bytes)
            return id;
    return 0;
}

// The preset texture tinted by the material colour, for the preview and the library icons. The
// renderer multiplies linear values (ADR 0065); this multiplies sRGB values, so the shade here is
// only an approximation.
QImage tintedTexture(const TexturePreset &preset, int size, const QColor &tint) {
    QImage image;
    if (!image.loadFromData(texturePresetBytes(preset), "JPEG"))
        throw std::runtime_error("The texture preset could not be decoded");
    image = image.convertToFormat(QImage::Format_RGB888)
                .scaled(size, size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const auto pixel = image.pixelColor(x, y);
            image.setPixelColor(x, y,
                                QColor::fromRgbF(pixel.redF() * tint.redF(),
                                                 pixel.greenF() * tint.greenF(),
                                                 pixel.blueF() * tint.blueF()));
        }
    return image;
}

} // namespace
MaterialsPanel::MaterialsPanel(Document &doc, Viewport &view, QWidget *parent)
    : QWidget(parent), doc_(doc), view_(view) {
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea;
    scroll->setObjectName("materialScroll");
    scroll->setAccessibleName("Material properties");
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
        buttons_.push_back(button);
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
    buttonGrid_ = buttons;
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
    arrangeButtons();
    refresh();
}
void MaterialsPanel::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    arrangeButtons();
}
// D-023 (d): the panel's scroll area does not scroll sideways, so two columns of buttons that need
// more than the panel is wide (the second column was cut off) become one column.
void MaterialsPanel::arrangeButtons() {
    if (!buttonGrid_)
        return;
    int widest[2]{};
    for (size_t i = 0; i < buttons_.size(); ++i)
        widest[i % 2] = std::max(widest[i % 2], buttons_[i]->sizeHint().width());
    const int bar = style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, this);
    const int spacing = std::max(0, buttonGrid_->spacing());
    const bool two = width() - bar >= widest[0] + widest[1] + spacing;
    if (two == twoColumns_)
        return;
    twoColumns_ = two;
    for (auto *button : buttons_)
        buttonGrid_->removeWidget(button);
    for (size_t i = 0; i < buttons_.size(); ++i)
        buttonGrid_->addWidget(buttons_[i], two ? int(i / 2) : int(i), two ? int(i % 2) : 0);
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
    color->setAccessibleName("Color");
    auto *opacity = new QDoubleSpinBox;
    acceptKeypadDecimal(opacity);
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
    // R100.b2: the real-world size of one texture repeat, in metres. Off means the implicit one
    // metre. A textured preset turns it on with its signed-off size.
    const auto initialSize =
        create ? std::optional<TextureSize>{} : doc_.materials().at(id)->textureSize;
    auto *sized = new QCheckBox("Set size");
    sized->setObjectName("materialTextureSized");
    sized->setAccessibleName("Set texture size");
    sized->setChecked(initialSize.has_value());
    auto sizeSpin = [&](const char *name, const char *label, double value) {
        auto *spin = new QDoubleSpinBox;
        acceptKeypadDecimal(spin);
        spin->setObjectName(name);
        spin->setAccessibleName(label);
        spin->setRange(minimumTextureSize, maximumTextureSize);
        spin->setDecimals(2);
        spin->setSuffix(" m");
        spin->setValue(value);
        spin->setEnabled(sized->isChecked());
        return spin;
    };
    auto *tileWidth =
        sizeSpin("materialTextureWidth", "Texture width", initialSize ? initialSize->width : 1);
    auto *tileHeight =
        sizeSpin("materialTextureHeight", "Texture height", initialSize ? initialSize->height : 1);
    const auto initialWidth = tileWidth->value(), initialHeight = tileHeight->value();
    connect(sized, &QCheckBox::toggled, &dialog, [=](bool on) {
        tileWidth->setEnabled(on);
        tileHeight->setEnabled(on);
    });
    auto *sizeRow = new QHBoxLayout;
    sizeRow->addWidget(sized);
    sizeRow->addWidget(tileWidth);
    sizeRow->addWidget(tileHeight);
    // The JSON value of the field: an object, or null for no size.
    auto sizeValue = [=]() -> QJsonValue {
        if (!sized->isChecked())
            return QJsonValue(QJsonValue::Null);
        return QJsonObject{{"width", tileWidth->value()}, {"height", tileHeight->value()}};
    };
    auto *error = new QLabel;
    error->setObjectName("materialDialogError");
    error->setWordWrap(true);
    error->setTextFormat(Qt::PlainText);
    QComboBox *library{};
    QLabel *texturePreview{}, *textureCaption{}, *textureLicence{};
    if (create) {
        struct Preset {
            const char *name;
            const char *color;
            double opacity;
        };
        // "Terrain" is the material new terrains get (core terrainMaterialName and
        // terrainMaterialColor, R095.e2).
        static constexpr std::array<Preset, 6> presets{{{"White", "#e6e6e6", 100},
                                                        {"Concrete", "#b0ada6", 100},
                                                        {"Terracotta", "#c45f42", 100},
                                                        {"Steel", "#7c8a96", 100},
                                                        {"Glass", "#b7d9e5", 35},
                                                        {"Terrain", "#8a9a6b", 100}}};
        // The Colour and Textured headings carry no data and are disabled, so the arrow keys skip
        // them (UX_DESIGN 18.7).
        auto *model = new QStandardItemModel(&dialog);
        const auto heading = [&](const char *label) {
            auto *item = new QStandardItem(label);
            item->setFlags(Qt::NoItemFlags);
            item->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
            model->appendRow(item);
        };
        const auto entry = [&](const QString &label, const QIcon &icon, int key) {
            auto *item = new QStandardItem(icon, label);
            item->setData(key, Qt::UserRole);
            model->appendRow(item);
            return item;
        };
        entry("Custom", {}, -1);
        heading("Colour");
        for (size_t i = 0; i < presets.size(); ++i) {
            QPixmap swatch(16, 16);
            swatch.fill(QColor(presets[i].color));
            entry(presets[i].name, QIcon(swatch), int(i));
        }
        heading("Textured");
        const auto textures = texturePresets();
        for (size_t i = 0; i < textures.size(); ++i) {
            const auto &preset = textures[i];
            const auto tint = QColor(text(preset.tint));
            entry(text(preset.name), QIcon(QPixmap::fromImage(tintedTexture(preset, 16, tint))),
                  texturedLibraryKey + int(i))
                ->setToolTip(text(preset.licence));
        }
        library = new QComboBox;
        library->setObjectName("materialLibrary");
        library->setModel(model);
        form->addRow("Local library", library);
        // A textured preset shows its texture as a 96 px tile, with the licence and whether the
        // texture is added or reused (UX_DESIGN 18.4). These rows stay hidden for other choices.
        texturePreview = new QLabel;
        texturePreview->setObjectName("materialTexturePreview");
        texturePreview->setAccessibleName("Texture preview, tinted with the colour");
        texturePreview->setFixedSize(96, 96);
        texturePreview->setVisible(false);
        textureCaption = new QLabel;
        textureCaption->setObjectName("materialTextureCaption");
        textureCaption->setWordWrap(true);
        textureCaption->setTextFormat(Qt::PlainText);
        textureCaption->setVisible(false);
        textureLicence = new QLabel;
        textureLicence->setObjectName("materialTextureLicence");
        textureLicence->setWordWrap(true);
        textureLicence->setTextFormat(Qt::PlainText);
        textureLicence->setVisible(false);
        form->addRow(texturePreview);
        form->addRow(textureCaption);
        form->addRow(textureLicence);
        // Shows a textured preset's tile, caption and licence, and holds the asset chooser to the
        // preset's own texture. Any other choice hides them and restores the chooser.
        const auto showTexture = [=, this](int key) {
            const bool textured = key >= texturedLibraryKey;
            if (textured) {
                const auto &preset = textures[size_t(key - texturedLibraryKey)];
                const auto chosen = QColor(color->text());
                const auto tint = chosen.isValid() ? chosen : QColor(text(preset.tint));
                texturePreview->setPixmap(QPixmap::fromImage(tintedTexture(preset, 96, tint)));
                const bool reused = identicalTexture(doc_, texturePresetBytes(preset)) != 0;
                textureCaption->setText(reused
                                            ? "Reuses the texture already in this model; no copy "
                                              "is added."
                                            : "Adds 1 texture, embedded in this model.");
                textureLicence->setText(text(preset.licence));
            }
            texturePreview->setVisible(textured);
            textureCaption->setVisible(textured);
            textureLicence->setVisible(textured);
            const int presetAsset = asset->findData("preset");
            if (textured) {
                if (presetAsset < 0)
                    asset->addItem("Preset texture (added on Create)", "preset");
                asset->setCurrentIndex(asset->findData("preset"));
            } else if (presetAsset >= 0) {
                asset->removeItem(presetAsset);
                asset->setCurrentIndex(0);
            }
            asset->setEnabled(!textured);
        };
        connect(library, &QComboBox::currentIndexChanged, &dialog, [=, this](int index) {
            const auto data = library->itemData(index, Qt::UserRole);
            if (!data.isValid())
                return; // A heading: the list does not select it.
            try {
                const auto key = data.toInt();
                if (key >= texturedLibraryKey) {
                    const auto &preset = textures[size_t(key - texturedLibraryKey)];
                    name->setText(text(preset.name));
                    color->setText(text(preset.tint));
                    color->setModified(true);
                    opacity->setValue(preset.opacity);
                    sized->setChecked(true);
                    tileWidth->setValue(preset.tileWidth);
                    tileHeight->setValue(preset.tileHeight);
                    showTexture(key);
                } else if (key >= 0) {
                    const auto &preset = presets[size_t(key)];
                    name->setText(preset.name);
                    color->setText(preset.color);
                    color->setModified(true);
                    opacity->setValue(preset.opacity);
                    showTexture(-1);
                } else
                    showTexture(-1); // Custom keeps the fields as they are.
            } catch (const std::exception &failure) {
                error->setText(failure.what());
            }
        });
        // The tile follows a colour the person types, because the tint is the colour.
        connect(color, &QLineEdit::textEdited, &dialog, [=, this](const QString &) {
            const auto key = library->currentData(Qt::UserRole).toInt();
            if (key < texturedLibraryKey)
                return;
            try {
                showTexture(key);
            } catch (const std::exception &failure) {
                error->setText(failure.what());
            }
        });
    }
    form->addRow("Name", name);
    auto *colorRow = new QHBoxLayout;
    colorRow->addWidget(color);
    auto *choose = new QPushButton("Choose…");
    choose->setObjectName("materialChooseColor");
    colorRow->addWidget(choose);
    connect(choose, &QPushButton::clicked, &dialog, [&] {
        QColorDialog picker(QColor(color->text()), &dialog);
        picker.setWindowTitle("Material color");
        nameColorDialog(picker);
        const auto chosen = picker.exec() == QDialog::Accepted ? picker.selectedColor() : QColor{};
        if (chosen.isValid()) {
            color->setText(chosen.name());
            color->setModified(true);
        }
    });
    form->addRow("Color", colorRow);
    form->addRow("Opacity", opacity);
    form->addRow("Stored file", asset);
    form->addRow("Texture size", sizeRow);
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
            const auto key = library ? library->currentData(Qt::UserRole).toInt() : -1;
            if (create && key >= texturedLibraryKey) {
                // A textured preset adds its texture, unless an identical one is in the model, and
                // the material that binds it: one edit, one undo step (UX_DESIGN 18.4).
                const auto &preset = texturePresets()[size_t(key - texturedLibraryKey)];
                const auto bytes = texturePresetBytes(preset);
                const auto existing = identicalTexture(doc_, bytes);
                const auto texture = existing ? existing : doc_.nextAssetId();
                const auto next = doc_.nextMaterialId();
                QJsonArray commands;
                if (!existing)
                    commands.append(QJsonObject{{"command", "asset.import"},
                                                {"name", text(preset.name)},
                                                {"mediaType", text(texturePresetMediaType)},
                                                {"data", QString::fromLatin1(bytes.toBase64())}});
                commands.append(QJsonObject{
                    {"command", "material.create"},
                    {"name", name->text()},
                    {"color", QJsonArray{chosen.redF(), chosen.greenF(), chosen.blueF()}},
                    {"opacity", opacity->value() / 100},
                    {"asset", sid(texture)},
                    {"textureSize", sizeValue()}});
                view_.editMaterials(commands);
                view_.setPaintMaterial(next, view_.paintSide());
                dialog.accept();
                return;
            }
            QJsonObject command{{"command", create ? "material.create" : "material.edit"},
                                {"name", name->text()},
                                {"asset", asset->currentData().toString()}};
            // Preserve untouched floating-point channels and opacity exactly.
            if (create || color->isModified())
                command["color"] = QJsonArray{chosen.redF(), chosen.greenF(), chosen.blueF()};
            if (create || opacity->value() != initialOpacity)
                command["opacity"] = opacity->value() / 100;
            // The size is sent when it changed; a material created without one sends none.
            const bool sizeChanged = sized->isChecked() != initialSize.has_value() ||
                                     (sized->isChecked() && (tileWidth->value() != initialWidth ||
                                                             tileHeight->value() != initialHeight));
            if (sizeChanged && (!create || sized->isChecked()))
                command["textureSize"] = sizeValue();
            const auto next = doc_.nextMaterialId();
            if (!create)
                command["material"] = sid(id);
            // OK without a change closes, as in the other editors, instead of reporting an
            // empty edit.
            if (!create && !command.contains("color") && !command.contains("opacity") &&
                !command.contains("textureSize") &&
                name->text().toStdString() == doc_.materials().at(id)->name &&
                asset->currentData().toString() == sid(doc_.materials().at(id)->asset)) {
                dialog.accept();
                return;
            }
            view_.editMaterials({command});
            view_.setPaintMaterial(create ? next : id, view_.paintSide());
            dialog.accept();
        } catch (const std::exception &failure) {
            error->setText(failure.what());
        }
    });
    nameInternalEditors(&dialog);
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
