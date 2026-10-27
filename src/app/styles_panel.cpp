#include "app/styles_panel.hpp"
#include "app/accessible_names.hpp"
#include "app/keypad_decimal.hpp"
#include "app/style_dialogs.hpp"
#include "app/unit_display.hpp"
#include "automation/measurements.hpp"
#include "io/assets.hpp"
#include "io/model_style_io.hpp"
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLocale>
#include <QMenu>
#include <QPixmap>
#include <QPushButton>
#include <QShowEvent>
#include <QStandardItemModel>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <cmath>
namespace sketchy {
namespace {
constexpr std::array modes{ModelStyleMode::Textured, ModelStyleMode::Shaded,
                           ModelStyleMode::Monochrome, ModelStyleMode::Wireframe,
                           ModelStyleMode::XRay};
QComboBox *modeBox() {
    auto *box = new QComboBox;
    box->addItems({"Textured", "Shaded", "Monochrome", "Wireframe", "X-ray"});
    return box;
}
int modeIndex(ModelStyleMode mode) {
    return int(std::find(modes.begin(), modes.end(), mode) - modes.begin());
}
QString decimal(double value) {
    QLocale locale;
    locale.setNumberOptions(locale.numberOptions() | QLocale::OmitGroupSeparator);
    return locale.toString(value, 'g', 12);
}
double scalar(QLineEdit *field, double original) {
    if (!field->isModified())
        return original;
    bool valid = false;
    const auto value = QLocale().toDouble(field->text(), &valid);
    if (!valid || !std::isfinite(value))
        throw std::runtime_error("Enter a finite number");
    return value;
}
// A refusal under a field begins "Invalid:" and is a sentence (UX_DESIGN §18.7).
void setRefusal(QLabel *label, const QString &text) {
    label->setText(text);
    label->setVisible(!text.isEmpty());
}
QLabel *sectionTitle(const QString &text, QLabel **state) {
    auto *title = new QLabel(text);
    QFont font = title->font();
    font.setBold(true);
    title->setFont(font);
    *state = new QLabel;
    (*state)->setAlignment(Qt::AlignRight);
    return title;
}
QLabel *caption(const QString &text) {
    auto *label = new QLabel(text);
    label->setWordWrap(true);
    return label;
}
} // namespace

StylesPanel::StylesPanel(Document &doc, Viewport &view, QWidget *parent)
    : QWidget(parent), doc_(doc), view_(view) {
    setObjectName("stylesPanel");
    auto *layout = new QVBoxLayout(this);
    auto *note = new QLabel(
        "Display settings are saved with this model. Application theme is set in View → Theme.");
    note->setWordWrap(true);
    layout->addWidget(note);
    mode_ = modeBox();
    mode_->setObjectName("styleMode");
    mode_->setAccessibleName("Model display mode");
    layout->addWidget(mode_);
    connect(mode_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (!syncing_ && index >= 0 && index < int(modes.size()))
            change([&](ModelStyle &style) { style.mode = modes[size_t(index)]; });
    });
    auto flag = [&](const QString &text, const QString &name, bool ModelStyle::*field) {
        auto *box = new QCheckBox(text);
        box->setObjectName(name);
        layout->addWidget(box);
        flags_.push_back({box, field});
        connect(box, &QCheckBox::toggled, this, [this, field](bool checked) {
            if (!syncing_)
                change([&](ModelStyle &style) { style.*field = checked; });
        });
    };
    flag("Edges", "styleEdges", &ModelStyle::edgesVisible);
    flag("Profiles", "styleProfiles", &ModelStyle::profiles);
    flag("Grid", "styleGrid", &ModelStyle::gridVisible);
    flag("Axes", "styleAxes", &ModelStyle::axesVisible);
    flag("Ground", "styleGround", &ModelStyle::groundVisible);
    auto *edit = new QPushButton("Colors and details…");
    edit->setObjectName("styleCustomize");
    layout->addWidget(edit);
    connect(edit, &QPushButton::clicked, this, [this] { customize(); });
    auto *reset = new QPushButton("Reset model style");
    reset->setObjectName("styleReset");
    layout->addWidget(reset);
    connect(reset, &QPushButton::clicked, this,
            [this] { labelled(StyleEdit{ModelStyle{}, "Reset model style"}); });
    auto *hint =
        new QLabel("Wireframe keeps edges visible. Shaded and monochrome retain transparent "
                   "cutouts. Ground is a visual reference and does not block selection.");
    hint->setWordWrap(true);
    layout->addWidget(hint);

    // Edge stroke (UX_DESIGN §18.3). Each control is one labelled document.style edit, and the
    // fields are off while the stroke is off.
    auto *strokeHeader = new QHBoxLayout;
    strokeHeader->addWidget(sectionTitle("Edge stroke", &strokeState_));
    strokeHeader->addWidget(strokeState_);
    layout->addLayout(strokeHeader);
    stroke_ = new QCheckBox("Use an edge stroke");
    stroke_->setObjectName("styleEdgeStroke");
    stroke_->setAccessibleName("Use an edge stroke");
    layout->addWidget(stroke_);
    connect(stroke_, &QCheckBox::toggled, this, [this](bool on) {
        if (syncing_)
            return;
        editStroke([on](const ModelStyle &style) {
            return on ? turnOnEdgeStroke(style) : turnOffEdgeStroke(style);
        });
    });
    auto *fields = new QGridLayout;
    layout->addLayout(fields);
    width_ = new QLineEdit;
    width_->setObjectName("styleEdgeWidth");
    width_->setAccessibleName("Edge width, 1 to 8 pixels");
    acceptKeypadDecimal(width_);
    width_->installEventFilter(this);
    connect(width_, &QLineEdit::editingFinished, this, [this] {
        if (!syncing_ && width_->isEnabled() && width_->isModified())
            setWidth(width_->text());
    });
    fields->addWidget(new QLabel("Width"), 0, 0);
    fields->addWidget(width_, 0, 1);
    fields->addWidget(caption("1 to 8 px"), 0, 2);
    opacity_ = new QLineEdit;
    opacity_->setObjectName("styleEdgeOpacity");
    opacity_->setAccessibleName("Edge opacity, 1 to 100 percent");
    acceptKeypadDecimal(opacity_);
    opacity_->installEventFilter(this);
    connect(opacity_, &QLineEdit::editingFinished, this, [this] {
        if (!syncing_ && opacity_->isEnabled() && opacity_->isModified())
            setOpacity(opacity_->text());
    });
    fields->addWidget(new QLabel("Opacity"), 1, 0);
    fields->addWidget(opacity_, 1, 1);
    fields->addWidget(caption("1 to 100 %"), 1, 2);
    dash_ = new QComboBox;
    dash_->setObjectName("styleEdgeDash");
    dash_->setAccessibleName("Edge dash");
    dash_->addItems({"Solid", "Dashed", "Dotted", "Dash-dot"});
    fields->addWidget(new QLabel("Dash"), 2, 0);
    fields->addWidget(dash_, 2, 1, 1, 2);
    connect(dash_, &QComboBox::activated, this, [this](int index) {
        editStroke([index](const ModelStyle &style) {
            return setEdgeStrokeDash(style, EdgeDash(index));
        });
    });
    texture_ = new QComboBox;
    texture_->setObjectName("styleEdgeTexture");
    texture_->setAccessibleName("Edge texture");
    fields->addWidget(new QLabel("Texture"), 3, 0);
    fields->addWidget(texture_, 3, 1, 1, 2);
    connect(texture_, &QComboBox::activated, this, [this](int index) {
        const auto id = Id(texture_->itemData(index).toULongLong());
        const auto name = id && doc_.assets().count(id)
                              ? QString::fromStdString(doc_.assets().at(id)->name).toStdString()
                              : std::string();
        editStroke([id, name](const ModelStyle &style) {
            return setEdgeStrokeTexture(style, id, name);
        });
    });
    textureNote_ = caption(QString());
    textureNote_->setObjectName("styleEdgeTextureNote");
    textureNote_->hide();
    layout->addWidget(textureNote_);
    strokeSchema_ = caption("Saved in schema 31. SketchyUp 1.3.0 cannot open it.");
    strokeSchema_->setObjectName("styleSchemaCaption");
    layout->addWidget(strokeSchema_);
    strokeError_ = new QLabel;
    strokeError_->setObjectName("styleEdgeError");
    strokeError_->setWordWrap(true);
    strokeError_->setTextFormat(Qt::PlainText);
    strokeError_->setStyleSheet("color: #a8322b; font-weight: 600;");
    strokeError_->hide();
    layout->addWidget(strokeError_);

    // Style library (UX_DESIGN §18.1, §18.2): the folder, search, the rows and the commands.
    auto *libraryHeader = new QHBoxLayout;
    libraryHeader->addWidget(sectionTitle("Style library", &libraryCount_));
    libraryHeader->addWidget(libraryCount_);
    layout->addLayout(libraryHeader);
    auto *folderRow = new QHBoxLayout;
    folderRow->addWidget(new QLabel("Folder"));
    folder_ = new QLineEdit;
    folder_->setObjectName("styleLibraryFolder");
    folder_->setAccessibleName("Style library folder");
    folder_->setReadOnly(true);
    folder_->setFocusPolicy(Qt::NoFocus);
    folderRow->addWidget(folder_, 1);
    layout->addLayout(folderRow);
    search_ = new QLineEdit;
    search_->setObjectName("styleSearch");
    search_->setPlaceholderText("Search names and descriptions");
    search_->setAccessibleName("Search style names and descriptions");
    search_->setClearButtonEnabled(true);
    connect(search_, &QLineEdit::textChanged, this, [this] { fillLibrary(); });
    layout->addWidget(search_);
    library_ = new QListWidget;
    library_->setObjectName("styleLibrary");
    library_->setAccessibleName("Style library");
    library_->setIconSize(QSize(64, 20));
    library_->setContextMenuPolicy(Qt::CustomContextMenu);
    library_->setMinimumHeight(120);
    layout->addWidget(library_, 1);
    connect(library_, &QListWidget::currentItemChanged, this,
            [this] { updateLibraryButtons(); });
    connect(library_, &QListWidget::itemActivated, this, [this] { applySelected(); });
    connect(library_, &QListWidget::customContextMenuRequested, this,
            [this](const QPoint &point) { showLibraryMenu(point); });
    libraryNotice_ = caption(QString());
    libraryNotice_->setObjectName("styleLibraryNotice");
    libraryNotice_->hide();
    layout->addWidget(libraryNotice_);
    libraryError_ = new QLabel;
    libraryError_->setObjectName("styleLibraryError");
    libraryError_->setWordWrap(true);
    libraryError_->setTextFormat(Qt::PlainText);
    libraryError_->setStyleSheet("color: #a8322b; font-weight: 600;");
    libraryError_->hide();
    layout->addWidget(libraryError_);
    auto *folderButtons = new QHBoxLayout;
    auto *choose = new QPushButton("Choose folder…");
    choose->setObjectName("styleChooseFolder");
    connect(choose, &QPushButton::clicked, this, [this] { chooseFolder(); });
    folderButtons->addWidget(choose);
    auto *refreshButton = new QPushButton("Refresh");
    refreshButton->setObjectName("styleRefresh");
    connect(refreshButton, &QPushButton::clicked, this, [this] { scanLibrary(); });
    folderButtons->addWidget(refreshButton);
    folderButtons->addStretch();
    layout->addLayout(folderButtons);
    auto *libraryButtons = new QHBoxLayout;
    apply_ = new QPushButton("Apply to model");
    apply_->setObjectName("styleApplyLibrary");
    apply_->setEnabled(false);
    connect(apply_, &QPushButton::clicked, this, [this] { applySelected(); });
    libraryButtons->addWidget(apply_);
    saveLibrary_ = new QPushButton("Save to library…");
    saveLibrary_->setObjectName("styleSaveLibrary");
    connect(saveLibrary_, &QPushButton::clicked, this, [this] { saveToLibrary(); });
    libraryButtons->addWidget(saveLibrary_);
    libraryButtons->addStretch();
    layout->addLayout(libraryButtons);

    error_ = new QLabel;
    error_->setObjectName("styleError");
    error_->setWordWrap(true);
    layout->addWidget(error_);
    layout->addStretch();
    connect(&view_, &Viewport::changed, this, [this] { refresh(); });
    refresh();
}

void StylesPanel::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    // The folder is scanned when the tab is shown, and again when the folder or its preference
    // changed. A folder's modification time moves when a file is added or removed.
    const auto folder = styleLibraryPreference();
    if (!scanned_ || folder != scannedPath_ || QFileInfo(folder).lastModified() != scannedModified_)
        scanLibrary();
}

bool StylesPanel::eventFilter(QObject *watched, QEvent *event) {
    // Enter commits a field (editingFinished) and Esc restores its value (UX_DESIGN §18.7).
    if (event->type() == QEvent::KeyPress && (watched == width_ || watched == opacity_) &&
        static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        static_cast<QLineEdit *>(watched)->setModified(false);
        setRefusal(strokeError_, QString());
        refreshStroke();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void StylesPanel::refresh() {
    syncing_ = true;
    mode_->setCurrentIndex(modeIndex(doc_.style().mode));
    for (auto [box, field] : flags_)
        box->setChecked(doc_.style().*field);
    syncing_ = false;
    refreshStroke();
    updateLibraryButtons();
}

void StylesPanel::change(const std::function<void(ModelStyle &)> &operation) {
    try {
        auto style = doc_.style();
        operation(style);
        view_.applyModelStyle(style);
        error_->clear();
        refresh();
    } catch (const std::exception &error) {
        error_->setText(error.what());
        refresh();
    }
}

void StylesPanel::labelled(const StyleEdit &edit) {
    try {
        view_.applyStyleEdit(edit);
        error_->clear();
    } catch (const std::exception &error) {
        error_->setText(error.what());
    }
    refresh();
}

void StylesPanel::editStroke(const std::function<StyleEdit(const ModelStyle &)> &operation) {
    try {
        view_.applyStyleEdit(operation(doc_.style()));
        setRefusal(strokeError_, QString());
    } catch (const std::exception &error) {
        setRefusal(strokeError_, QString("Invalid: ") + QString::fromUtf8(error.what()) + ".");
    }
    refresh();
}

void StylesPanel::setWidth(const QString &text) {
    bool valid = false;
    const auto value = QLocale().toDouble(text, &valid);
    if (!valid || !std::isfinite(value)) {
        setRefusal(strokeError_, "Invalid: enter a number from 1 to 8 for the edge width.");
        return;
    }
    editStroke([value](const ModelStyle &style) { return setEdgeStrokeWidth(style, value); });
}

void StylesPanel::setOpacity(const QString &text) {
    bool valid = false;
    const auto percent = QLocale().toDouble(text, &valid);
    if (!valid || !std::isfinite(percent)) {
        setRefusal(strokeError_, "Invalid: enter a number from 1 to 100 for the edge opacity.");
        return;
    }
    editStroke([percent](const ModelStyle &style) {
        return setEdgeStrokeOpacity(style, percent / 100);
    });
}

void StylesPanel::refreshStroke() {
    const auto &style = doc_.style();
    const auto &stroke = style.edgeStroke;
    syncing_ = true;
    stroke_->setChecked(bool(stroke));
    syncing_ = false;
    strokeState_->setText(stroke ? "On" : "Off");
    strokeSchema_->setVisible(bool(stroke));
    width_->setEnabled(bool(stroke));
    opacity_->setEnabled(bool(stroke));
    dash_->setEnabled(bool(stroke));
    texture_->setEnabled(bool(stroke));
    // While the stroke is off, the fields show what turning it on would take: the profile width
    // and full opacity (UX_DESIGN §18.3 copies the profile width on).
    if (!width_->isModified())
        width_->setText(decimal(stroke ? stroke->width : style.profileWidth));
    if (!opacity_->isModified())
        opacity_->setText(decimal((stroke ? stroke->opacity : 1) * 100));
    dash_->setCurrentIndex(stroke ? int(stroke->dash) : 0);
    setTextureChoices();
}

void StylesPanel::setTextureChoices() {
    const auto &style = doc_.style();
    const auto current = style.edgeStroke ? style.edgeStroke->texture : Id{};
    texture_->clear();
    texture_->addItem("None", QVariant::fromValue<qulonglong>(0));
    auto *model = qobject_cast<QStandardItemModel *>(texture_->model());
    auto add = [&](Id id, const AssetRecord &asset) {
        const auto name = QString::fromStdString(asset.name);
        const bool present = bool(asset.payload);
        texture_->addItem(present ? name + " ✓ Stored" : "⚠ " + name + " · Missing",
                          QVariant::fromValue<qulonglong>(id));
        // A missing texture is listed with its warning word and cannot be chosen (UX_DESIGN §18.3).
        if (!present && model)
            model->item(texture_->count() - 1)->setEnabled(false);
    };
    bool listed = false;
    for (const auto &[id, asset] : doc_.assets()) {
        if (asset->mediaType != "image/png" && asset->mediaType != "image/jpeg" && id != current)
            continue;
        add(id, *asset);
        listed = listed || id == current;
    }
    if (current && !listed && doc_.assets().count(current))
        add(current, *doc_.assets().at(current));
    texture_->setCurrentIndex(std::max(0, texture_->findData(QVariant::fromValue<qulonglong>(current))));

    // The missing texture's state is in the field, the caption and the tooltip, which is also the
    // accessible description (UX_DESIGN §18.3). Colour is never the only carrier.
    const auto asset = current && doc_.assets().count(current) ? doc_.assets().at(current) : nullptr;
    const bool missing = asset && !asset->payload;
    texture_->setStyleSheet(missing ? "QComboBox { background: #fdf3dc; color: #7a4f00; }"
                                    : QString());
    if (missing) {
        const auto name = QString::fromStdString(asset->name);
        const auto tip = name + " is missing from this model (asset " +
                         QString::number(asset->id) +
                         "). Edges that use it draw hatched until it is restored. No other texture "
                         "is used.";
        textureNote_->setText("Missing: edges that use it draw hatched. No other texture is used.");
        textureNote_->setToolTip(tip);
        textureNote_->show();
        texture_->setToolTip(tip);
        texture_->setAccessibleDescription(tip);
    } else {
        textureNote_->hide();
        texture_->setToolTip(QString());
        texture_->setAccessibleDescription(QString());
    }
}

void StylesPanel::customize() {
    const auto stamp = doc_.saveStamp();
    const auto revision = doc_.revision();
    const auto original = doc_.style();
    auto candidate = original;
    QDialog dialog(this);
    dialog.setObjectName("styleDialog");
    dialog.setWindowTitle("Model style");
    auto *form = new QFormLayout(&dialog);
    auto *mode = modeBox();
    mode->setObjectName("styleDialogMode");
    mode->setCurrentIndex(modeIndex(original.mode));
    form->addRow("Display mode", mode);
    auto color = [&](const QString &label, const QString &name,
                     std::array<float, 3> ModelStyle::*field) {
        auto *button = new QPushButton;
        button->setObjectName(name);
        button->setAccessibleName(label + " color");
        auto update = [&, button, field] {
            const auto &rgb = candidate.*field;
            const auto value = QColor::fromRgbF(rgb[0], rgb[1], rgb[2]);
            button->setText(value.name(QColor::HexRgb));
            button->setAccessibleDescription(button->text());
            QPixmap swatch(24, 16);
            swatch.fill(value);
            button->setIcon(QIcon(swatch));
        };
        update();
        form->addRow(label, button);
        connect(button, &QPushButton::clicked, &dialog, [&, field, update, label] {
            const auto &rgb = candidate.*field;
            QColorDialog picker(QColor::fromRgbF(rgb[0], rgb[1], rgb[2]), &dialog);
            picker.setWindowTitle(label);
            picker.setOption(QColorDialog::DontUseNativeDialog);
            nameColorDialog(picker);
            // Qt initializes this chooser at 8-bit channel precision. An unchanged
            // acceptance must preserve the original document's float channels.
            const auto baseline = picker.currentColor();
            if (picker.exec() == QDialog::Accepted && picker.currentColor() != baseline) {
                const auto selected = picker.currentColor();
                candidate.*field = {float(selected.redF()), float(selected.greenF()),
                                    float(selected.blueF())};
                update();
            }
        });
    };
    color("Background", "styleBackgroundColor", &ModelStyle::background);
    color("Ground", "styleGroundColor", &ModelStyle::ground);
    color("Monochrome front", "styleFrontColor", &ModelStyle::front);
    color("Monochrome back", "styleBackColor", &ModelStyle::back);
    color("Edges and profiles", "styleEdgeColor", &ModelStyle::edge);
    auto *ground = new QLineEdit(displayLength(original.groundHeight, doc_.displayUnits(), fullDisplayPrecision));
    acceptKeypadDecimal(ground);
    ground->setObjectName("styleGroundHeight");
    form->addRow("Ground height", ground);
    auto *width = new QLineEdit(decimal(original.profileWidth));
    acceptKeypadDecimal(width);
    width->setObjectName("styleProfileWidth");
    form->addRow("Profile width (px)", width);
    // While the edge stroke is on, its width is the one drawn, so Profile width is dimmed (UX_DESIGN
    // §18.3, gap 2 of §18.8).
    if (original.edgeStroke) {
        width->setEnabled(false);
        auto *note = caption("Set by the edge stroke");
        note->setObjectName("styleProfileWidthNote");
        form->addRow(note);
    }
    auto *opacity = new QLineEdit(decimal(original.xrayOpacity * 100));
    acceptKeypadDecimal(opacity);
    opacity->setObjectName("styleXrayOpacity");
    form->addRow("X-ray opacity (%)", opacity);
    auto *error = new QLabel;
    error->setObjectName("styleDialogError");
    error->setWordWrap(true);
    form->addRow(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            if (doc_.revision() != revision || !doc_.isCurrentSnapshot(stamp))
                throw std::runtime_error(
                    "The model changed while editing. Cancel and reopen the style editor.");
            candidate.mode = modes.at(size_t(mode->currentIndex()));
            candidate.groundHeight =
                ground->isModified()
                    ? parseLength(ground->text(), inputUnit(doc_.displayUnits()), QLocale())
                    : original.groundHeight;
            candidate.profileWidth = scalar(width, original.profileWidth);
            candidate.xrayOpacity = scalar(opacity, original.xrayOpacity * 100) / 100;
            // Preserve untouched doubles exactly, including values not representable in percent.
            if (!opacity->isModified())
                candidate.xrayOpacity = original.xrayOpacity;
            candidate.validate();
            view_.applyModelStyle(candidate);
            dialog.accept();
            refresh();
        } catch (const std::exception &exception) {
            error->setText(exception.what());
        }
    });
    dialog.exec();
}

void StylesPanel::scanLibrary() {
    const auto folder = styleLibraryPreference();
    folder_->setText(QDir::toNativeSeparators(folder));
    scan_ = {};
    libraryError_->clear();
    libraryError_->hide();
    scannedPath_ = folder;
    scannedModified_ = QFileInfo(folder).lastModified();
    scanned_ = true;
    try {
        if (QFileInfo::exists(folder))
            scan_ = scanStyleDirectory(folder);
    } catch (const std::exception &error) {
        libraryError_->setText("✕ " + QString::fromUtf8(error.what()));
        libraryError_->show();
    }
    QString notice = scan_.notices.join("\n");
    if (scan_.entries.empty() && libraryError_->isHidden() && notice.isEmpty())
        notice = "No styles in this folder yet. Save to library… or Import style… adds one.";
    libraryNotice_->setText(notice);
    libraryNotice_->setVisible(!notice.isEmpty());
    fillLibrary();
}

void StylesPanel::fillLibrary() {
    const auto filter = search_->text().trimmed();
    library_->clear();
    for (size_t index = 0; index < scan_.entries.size(); ++index) {
        const auto &entry = scan_.entries[index];
        if (!filter.isEmpty() &&
            !entry.metadata.name.contains(filter, Qt::CaseInsensitive) &&
            !entry.metadata.description.contains(filter, Qt::CaseInsensitive))
            continue;
        auto *item = new QListWidgetItem(library_);
        item->setData(Qt::UserRole, qulonglong(index));
        const bool readable = entry.error.isEmpty() &&
                              entry.textureStatus != StyleTextureStatus::HashMismatch;
        if (!readable) {
            // A file that cannot be read is listed with its reason and cannot be selected.
            const auto reason = entry.error.isEmpty()
                                    ? QStringLiteral("the bytes do not match the recorded checksum")
                                    : entry.error;
            item->setText(entry.metadata.name + "\n✕ Cannot read: " + reason);
            item->setFlags(Qt::ItemIsEnabled);
            continue;
        }
        item->setIcon(QIcon(styleStrokeSample(entry.style, QSize(64, 20))));
        const auto texture = entry.textureStatus == StyleTextureStatus::Present ? "texture stored"
                             : entry.textureStatus == StyleTextureStatus::Missing
                                 ? "texture missing from file"
                                 : "no texture";
        const bool missing = entry.textureStatus == StyleTextureStatus::Missing;
        item->setText(entry.metadata.name + "\n" + styleSettingsWords(entry.style) + " · " +
                      texture + (missing ? " · ⚠ Texture missing from file · cannot apply"
                                         : " · ✓ Stored"));
        if (missing)
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    }
    const auto count = qulonglong(scan_.entries.size());
    libraryCount_->setText(QString::number(count) + (count == 1 ? " style" : " styles"));
    if (library_->count())
        library_->setCurrentRow(0);
    updateLibraryButtons();
}

const StyleLibraryEntry *StylesPanel::selectedEntry() const {
    auto *item = library_->currentItem();
    if (!item)
        return nullptr;
    const auto index = item->data(Qt::UserRole).toULongLong();
    return index < scan_.entries.size() ? &scan_.entries[size_t(index)] : nullptr;
}

bool StylesPanel::applicable(const StyleLibraryEntry *entry) const {
    return entry && entry->error.isEmpty() &&
           (entry->textureStatus == StyleTextureStatus::None ||
            entry->textureStatus == StyleTextureStatus::Present);
}

void StylesPanel::updateLibraryButtons() {
    if (apply_)
        apply_->setEnabled(applicable(selectedEntry()));
}

void StylesPanel::chooseFolder() {
    const auto path =
        QFileDialog::getExistingDirectory(this, "Choose style folder", styleLibraryPreference());
    if (path.isEmpty())
        return;
    setStyleLibraryPreference(path);
    scanLibrary();
}

void StylesPanel::applySelected() {
    const auto *entry = selectedEntry();
    if (!applicable(entry))
        return;
    try {
        QFile file(entry->path);
        if (!file.open(QIODevice::ReadOnly) || file.size() > styleFileLimit)
            throw std::runtime_error("Could not read the style file");
        const auto bytes = file.readAll();
        const auto name = entry->metadata.name;
        const auto before = doc_.style();
        const auto status = view_.applyStyleFile(bytes, name);
        libraryError_->clear();
        libraryError_->hide();
        emit view_.message(styleApplyOutcome(name, before, doc_.style(), status));
    } catch (const std::exception &error) {
        libraryError_->setText("✕ Nothing was applied. " + QString::fromUtf8(error.what()));
        libraryError_->show();
    }
    refresh();
}

void StylesPanel::saveToLibrary() {
    StyleSaveDialog dialog(doc_, styleLibraryPreference(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    emit view_.message("Saved “" + styleNameFromPath(dialog.savedPath()) +
                       "” to the style library · Model unchanged");
    scanLibrary();
}

void StylesPanel::showLibraryMenu(const QPoint &point) {
    // The row menu offers the buttons' commands, so the keyboard and the mouse reach the same ones
    // (UX_DESIGN §18.7). It opens without a nested event loop.
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    auto *apply = menu->addAction("Apply to model", this, [this] { applySelected(); });
    apply->setEnabled(applicable(selectedEntry()));
    menu->addAction("Save to library…", this, [this] { saveToLibrary(); });
    menu->addAction("Choose folder…", this, [this] { chooseFolder(); });
    menu->addAction("Refresh", this, [this] { scanLibrary(); });
    menu->popup(library_->viewport()->mapToGlobal(point));
}

} // namespace sketchy
