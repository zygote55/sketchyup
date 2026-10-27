#include "app/style_dialogs.hpp"
#include "io/assets.hpp"
#include "io/catalog_io.hpp"
#include "io/model_style_io.hpp"
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QVBoxLayout>
#include <QtEndian>
#include <optional>
#include <stdexcept>

namespace sketchy {
namespace {

const QByteArray styleMagic("SKYLIB\0\1", 8);
constexpr qsizetype styleHeaderSize = 24;
constexpr qsizetype bytesPerMebibyte = 1024 * 1024;

// A refusal of §18.6: what did not happen is in the title, the reason has the measured number, and
// the next steps follow. Thrown inside the Import dialog's constructor and shown in its body.
struct StyleImportRefusal {
    QString title, reason;
    QStringList next;
};

void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}

QString fileSize(qsizetype bytes) {
    if (bytes < bytesPerMebibyte)
        return QString::number(double(bytes) / 1024.0, 'f', 0) + " KiB";
    return QString::number(double(bytes) / double(bytesPerMebibyte), 'f', 1) + " MiB";
}

QString modeWord(ModelStyleMode mode) {
    switch (mode) {
    case ModelStyleMode::Textured:
        return "Textured";
    case ModelStyleMode::Shaded:
        return "Shaded";
    case ModelStyleMode::Monochrome:
        return "Monochrome";
    case ModelStyleMode::Wireframe:
        return "Wireframe";
    case ModelStyleMode::XRay:
        return "X-ray";
    }
    return "Textured";
}

QString textureFormat(const std::string &mediaType) {
    return mediaType == "image/jpeg" ? QStringLiteral("JPEG") : QStringLiteral("PNG");
}

QLabel *muted(const QString &text) {
    auto *label = new QLabel(text);
    label->setWordWrap(true);
    return label;
}

QLabel *heading(const QString &text) {
    auto *label = new QLabel(text);
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
    return label;
}

// The refusal of a missing edge texture, which names it and writes nothing (UX_DESIGN §18.6). Empty
// when the stroke names no texture or the texture has its bytes.
QString missingTextureRefusal(const Document &doc, const char *verb, const char *outcome) {
    const auto &style = doc.style();
    if (!style.edgeStroke || !style.edgeStroke->texture)
        return {};
    const auto &asset = doc.assets().at(style.edgeStroke->texture);
    if (asset->payload)
        return {};
    return QString(verb) + " refused: “" + QString::fromStdString(asset->name) + "” (asset " +
           QString::number(asset->id) + ") is missing from this model. Nothing was " + outcome + ".";
}

QLabel *errorLabel(const char *name) {
    auto *label = new QLabel;
    label->setObjectName(name);
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    label->setStyleSheet("color: #a8322b; font-weight: 600;");
    label->hide();
    return label;
}

// A refusal is a sentence that begins with a cross, never colour alone (UX_DESIGN §18.7).
void showError(QLabel *label, const QString &text) {
    label->setText(text.isEmpty() ? QString() : "✕ " + text);
    label->setVisible(!text.isEmpty());
}

// The §18.6 refusal shape, with its title, its reason and the next steps.
QWidget *refusalBody(const StyleImportRefusal &refusal, QWidget *parent) {
    auto *body = new QWidget(parent);
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *title = heading(refusal.title);
    title->setObjectName("styleRefusalTitle");
    layout->addWidget(title);
    auto *reason = new QLabel(refusal.reason);
    reason->setObjectName("styleRefusalReason");
    reason->setWordWrap(true);
    layout->addWidget(reason);
    if (!refusal.next.isEmpty()) {
        layout->addSpacing(8);
        layout->addWidget(heading("What to do next"));
        for (int i = 0; i < refusal.next.size(); ++i) {
            auto *step = new QLabel(QString::number(i + 1) + ". " + refusal.next.at(i));
            step->setWordWrap(true);
            layout->addWidget(step);
        }
    }
    layout->addStretch();
    return body;
}

} // namespace

QString styleSettingsWords(const ModelStyle &style) {
    if (!style.edgeStroke)
        return "Edge stroke off";
    const auto &stroke = *style.edgeStroke;
    return QString::number(stroke.width, 'f', 1) + " px · " +
           QString::number(stroke.opacity * 100, 'g', 3) + " % · " + edgeDashWord(stroke.dash);
}

QString edgeDashWord(EdgeDash dash) {
    switch (dash) {
    case EdgeDash::Solid:
        return "Solid";
    case EdgeDash::Dashed:
        return "Dashed";
    case EdgeDash::Dotted:
        return "Dotted";
    case EdgeDash::DashDot:
        return "Dash-dot";
    }
    return "Solid";
}

QString styleApplyOutcome(const QString &name, const ModelStyle &before, const ModelStyle &after,
                          const QString &status) {
    if (status == "unchanged")
        return "This model already has this style. Nothing changed.";
    const auto changed = changedStyleSettings(before, after);
    QString stroke;
    if (!before.edgeStroke && after.edgeStroke)
        stroke = "edge stroke on, ";
    else if (before.edgeStroke && !after.edgeStroke)
        stroke = "edge stroke off, ";
    return "Applied “" + name + "”: " + stroke + QString::number(changed) +
           (changed == 1 ? " setting" : " settings") + " changed. Undo: Apply style “" + name + "”.";
}

QPixmap styleStrokeSample(const ModelStyle &style, const QSize &size) {
    QPixmap pixmap(size);
    pixmap.fill(QColor(240, 241, 236));
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    const double width = style.edgeStroke ? style.edgeStroke->width : style.profileWidth;
    QColor colour = QColor::fromRgbF(style.edge[0], style.edge[1], style.edge[2]);
    colour.setAlphaF(style.edgeStroke ? style.edgeStroke->opacity : 1);
    QPen pen(colour, width, Qt::SolidLine, Qt::FlatCap);
    if (style.edgeStroke && style.edgeStroke->dash != EdgeDash::Solid) {
        QList<qreal> pattern;
        for (auto length : edgeDashLengths(style.edgeStroke->dash))
            pattern.append(length / width); // Qt measures a dash in pen widths
        pen.setDashPattern(pattern);
    }
    painter.setPen(pen);
    const double y = size.height() / 2.0;
    painter.drawLine(QPointF(size.width() * .08, y), QPointF(size.width() * .92, y));
    return pixmap;
}

int changedStyleSettings(const ModelStyle &before, const ModelStyle &after) {
    int changed = 0;
    changed += before.mode != after.mode;
    changed += before.background != after.background;
    changed += before.ground != after.ground;
    changed += before.front != after.front;
    changed += before.back != after.back;
    changed += before.edge != after.edge;
    changed += before.groundVisible != after.groundVisible;
    changed += before.groundHeight != after.groundHeight;
    changed += before.gridVisible != after.gridVisible;
    changed += before.axesVisible != after.axesVisible;
    changed += before.edgesVisible != after.edgesVisible;
    changed += before.profiles != after.profiles;
    changed += before.profileWidth != after.profileWidth;
    changed += before.xrayOpacity != after.xrayOpacity;
    if (before.edgeStroke.has_value() != after.edgeStroke.has_value())
        ++changed;
    else if (before.edgeStroke && after.edgeStroke) {
        changed += before.edgeStroke->width != after.edgeStroke->width;
        changed += before.edgeStroke->opacity != after.edgeStroke->opacity;
        changed += before.edgeStroke->dash != after.edgeStroke->dash;
        changed += before.edgeStroke->texture != after.edgeStroke->texture;
    }
    return changed;
}

QString styleLibraryNameError(const QString &name) {
    return isCatalogLibraryName(name)
               ? QString()
               : QStringLiteral("Name rule: 1 to 64 characters; letters, digits, spaces, dots, "
                                "underscores and hyphens; starting and ending with a letter, digit, "
                                "underscore or hyphen.");
}

QString styleLibraryPath(const QString &folder, const QString &name) {
    return QDir(folder).filePath(name + QStringLiteral(".sketchystyle"));
}

QString styleNameFromPath(const QString &path) {
    static const QString suffix(".sketchystyle");
    auto name = QFileInfo(path).fileName();
    if (name.endsWith(suffix, Qt::CaseInsensitive))
        name.chop(suffix.size());
    return name;
}

QString suggestFreeStyleName(const QString &folder, const QString &name) {
    if (!QFileInfo::exists(styleLibraryPath(folder, name)))
        return name;
    for (int number = 2; number < 1000; ++number) {
        const auto suffix = " " + QString::number(number);
        const auto candidate = name.left(64 - suffix.size()) + suffix;
        if (!QFileInfo::exists(styleLibraryPath(folder, candidate)))
            return candidate;
    }
    return name;
}

QString styleLibraryPreference() {
    return QSettings("SketchyUp", "SketchyUp")
        .value("styleFolder", styleLibraryFolder())
        .toString();
}

void setStyleLibraryPreference(const QString &folder) {
    QSettings("SketchyUp", "SketchyUp").setValue("styleFolder", QDir(folder).absolutePath());
}

StyleImportDialog::StyleImportDialog(Document &doc, Viewport &view, const QString &path,
                                     QWidget *parent)
    : QDialog(parent), doc_(doc), view_(view), path_(path) {
    setObjectName("importStyleDialog");
    setWindowTitle("Import style");
    setMinimumWidth(640);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(heading("Import style"));
    layout->addWidget(muted("Reads a .sketchystyle file. The file is never changed."));

    // The header is read first, so a texture over the limit is named instead of failing the read
    // (UX_DESIGN §18.6). Nothing is applied or written on any refusal.
    std::optional<StyleImportRefusal> refusal;
    try {
        QFile file(path_);
        require(file.open(QIODevice::ReadOnly), "Could not open the file");
        const auto header = file.read(styleHeaderSize);
        if (header.size() == styleHeaderSize && header.startsWith(styleMagic)) {
            const auto payload = qFromLittleEndian<quint64>(header.constData() + 12);
            if (payload > quint64(AssetPayload::limit))
                throw StyleImportRefusal{
                    "Import refused: the texture is too large",
                    "The texture is " + fileSize(qsizetype(payload)) +
                        ", and the limit is 16 MiB. Nothing was applied.",
                    {"Reduce the texture to 16 MiB or less, export the style again, then import "
                     "it."}};
        }
        require(file.size() <= styleFileLimit, "The file is larger than a style file can be");
        file.seek(0);
        bytes_ = file.readAll();
        const auto kind = sketchyLibraryKind(bytes_);
        if (!kind.isEmpty() && kind != "style")
            throw StyleImportRefusal{
                "This file is a " + kind + ", not a style.",
                "This file is a " + kind + ", and it is not imported as a style. Nothing was "
                                           "applied.",
                kind == "catalog" ? QStringList{"Use File → Asset catalogs…"} : QStringList{}};
        bundle_ = decodeStyleBundle(bytes_);
        if (bundle_.textureStatus == StyleTextureStatus::Missing)
            throw StyleImportRefusal{
                "The edge texture is not in the file.",
                "The style names an edge texture, " + QString::fromStdString(bundle_.texture.name) +
                    " (asset " + QString::number(bundle_.texture.id) +
                    "). The file lists it, but the entry has no bytes. SketchyUp does not use "
                    "another texture, and it does not draw the edge in a plain colour instead.",
                {"Export the style again from the model that has the texture, with Export style….",
                 "Or choose another style file."}};
        if (bundle_.textureStatus == StyleTextureStatus::HashMismatch)
            throw StyleImportRefusal{
                "The edge texture does not match its checksum.",
                "The bytes do not match the recorded checksum, so the file may be damaged. "
                "Nothing was applied.",
                {"Copy the file again from its source.", "Or choose another style file."}};
        usable_ = true;
    } catch (const StyleImportRefusal &failure) {
        refusal = failure;
    } catch (const std::exception &error) {
        const auto message = QString::fromUtf8(error.what());
        refusal = StyleImportRefusal{
            "Import style refused",
            message.contains("PNG or JPEG")
                ? QStringLiteral("The texture is not an image SketchyUp can read. Nothing was "
                                 "applied.")
                : "This file is not a style SketchyUp can read: " + message +
                      ". Nothing was applied.",
            {"Choose another style file."}};
    }

    if (!usable_) {
        setObjectName("importStyleRefused");
        layout->addWidget(refusalBody(*refusal, this), 1);
        auto *footer = new QHBoxLayout;
        footer->addStretch();
        auto *choose = new QPushButton("Choose another file…");
        choose->setObjectName("importChooseAnother");
        connect(choose, &QPushButton::clicked, this,
                [this] { done(StyleImportDialog::kChooseAnother); });
        footer->addWidget(choose);
        auto *close = new QPushButton("Close");
        close->setObjectName("importClose");
        close->setDefault(true);
        connect(close, &QPushButton::clicked, this, &QDialog::reject);
        footer->addWidget(close);
        layout->addLayout(footer);
        return;
    }

    const auto metadata = bundle_.metadata;
    const auto style = bundle_.style;
    auto *columns = new QHBoxLayout;
    layout->addLayout(columns);

    auto *left = new QVBoxLayout;
    columns->addLayout(left, 1);
    left->addWidget(heading("The file"));
    auto *facts = new QGridLayout;
    left->addLayout(facts);
    int row = 0;
    auto fact = [&](const QString &label, const QString &value, const char *objectName) {
        facts->addWidget(new QLabel(label), row, 0, Qt::AlignTop);
        auto *text = new QLabel(value);
        text->setWordWrap(true);
        text->setObjectName(objectName);
        facts->addWidget(text, row++, 1);
    };
    fact("File", QFileInfo(path_).fileName() + " · " + fileSize(bytes_.size()), "importFile");
    fact("Name", metadata.name, "importFileName");
    if (!metadata.description.isEmpty())
        fact("Description", metadata.description, "importDescription");
    fact("Display", modeWord(style.mode) + " · 14 settings, all read ✓", "importDisplay");
    fact("Edge stroke", styleSettingsWords(style), "importStroke");
    QString texture = "None";
    if (bundle_.textureStatus == StyleTextureStatus::Present)
        texture = QString::fromStdString(bundle_.texture.name) + " · asset " +
                  QString::number(bundle_.texture.id) + " · " +
                  textureFormat(bundle_.texture.mediaType) + " · stored in the file ✓";
    fact("Texture", texture, "importTexture");
    left->addSpacing(8);
    left->addWidget(heading("Preview"));
    auto *preview = new QLabel;
    preview->setObjectName("importPreview");
    preview->setPixmap(styleStrokeSample(style, QSize(300, 64)));
    left->addWidget(preview);
    left->addWidget(muted("The sample draws the stroke's own settings. It is not a render."));
    left->addStretch();

    auto *right = new QVBoxLayout;
    columns->addLayout(right, 1);
    right->addWidget(heading("What happens"));
    apply_ = new QRadioButton("Apply to this model");
    apply_->setObjectName("importApply");
    apply_->setChecked(true);
    right->addWidget(apply_);
    right->addWidget(muted("Replaces this model's display settings in one undoable step, “Apply "
                           "style “" + metadata.name + "””. Geometry, materials, tags and "
                           "components are not changed. A style equal to the model's current "
                           "style changes nothing."));
    save_ = new QRadioButton("Save to the style library");
    save_->setObjectName("importSave");
    right->addWidget(save_);
    right->addWidget(muted("Copies the file into the library folder under a new name. This model "
                           "is not changed."));
    auto *nameRow = new QHBoxLayout;
    nameLabel_ = new QLabel("Name in library");
    nameRow->addWidget(nameLabel_);
    name_ = new QLineEdit(suggestFreeStyleName(styleLibraryPreference(), metadata.name));
    name_->setObjectName("importName");
    name_->setAccessibleName("Name in library");
    name_->setMaxLength(64);
    nameRow->addWidget(name_, 1);
    right->addLayout(nameRow);
    nameLabel_->setEnabled(false);
    name_->setEnabled(false);
    if (style.edgeStroke) {
        right->addSpacing(8);
        right->addWidget(heading("Models with an edge stroke"));
        right->addWidget(muted("A model that uses an edge stroke is saved in schema 31. SketchyUp "
                               "1.3.0 cannot open it."));
    }
    right->addStretch();
    error_ = errorLabel("importError");
    layout->addWidget(error_);

    auto *footer = new QHBoxLayout;
    status_ = muted("Ready. Nothing changes until you press the button.");
    status_->setObjectName("importStatus");
    footer->addWidget(status_, 1);
    auto *cancel = new QPushButton("Cancel");
    cancel->setObjectName("importCancel");
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    footer->addWidget(cancel);
    primary_ = new QPushButton("Apply to model");
    primary_->setObjectName("importPrimary");
    primary_->setDefault(true);
    footer->addWidget(primary_);
    layout->addLayout(footer);

    connect(save_, &QRadioButton::toggled, this, [this](bool saving) {
        nameLabel_->setEnabled(saving);
        name_->setEnabled(saving);
        primary_->setText(saving ? "Save to library" : "Apply to model");
        showError(error_, QString());
    });
    connect(primary_, &QPushButton::clicked, this, [this] {
        if (save_->isChecked())
            save();
        else
            apply();
    });
}

void StyleImportDialog::apply() {
    try {
        const auto before = doc_.style();
        const auto name = bundle_.metadata.name;
        const auto status = view_.applyStyleFile(bytes_, name);
        outcome_ = styleApplyOutcome(name, before, doc_.style(), status);
        accept();
    } catch (const std::exception &error) {
        showError(error_, QString::fromUtf8(error.what()));
    }
}

void StyleImportDialog::save() {
    try {
        const auto folder = styleLibraryPreference();
        const auto name = name_->text().trimmed();
        const auto rule = styleLibraryNameError(name);
        require(rule.isEmpty(), rule.toUtf8().constData());
        const auto target = styleLibraryPath(folder, name);
        if (QFileInfo::exists(target))
            throw std::runtime_error(("A style named “" + name +
                                      "” is already in the library. An existing file is never "
                                      "replaced; choose a new name.")
                                         .toUtf8()
                                         .constData());
        QDir().mkpath(folder);
        writeStyleBundle(bytes_, target);
        outcome_ = "Saved “" + name + "” to the style library · Model unchanged";
        accept();
    } catch (const std::exception &error) {
        showError(error_, QString::fromUtf8(error.what()));
    }
}

StyleExportDialog::StyleExportDialog(Document &doc, QWidget *parent)
    : QDialog(parent), doc_(doc) {
    setObjectName("exportStyleDialog");
    setWindowTitle("Export style");
    setMinimumWidth(640);
    const auto style = doc.style();
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(heading("Export style"));
    layout->addWidget(muted("Writes a new .sketchystyle file from this model's display settings. "
                            "The model is not changed."));

    auto *columns = new QHBoxLayout;
    layout->addLayout(columns);
    auto *left = new QGridLayout;
    columns->addLayout(left, 1);
    left->addWidget(heading("Written"), 0, 0, 1, 2);
    name_ = new QLineEdit("Model style");
    name_->setObjectName("exportName");
    name_->setAccessibleName("Style name");
    name_->setMaxLength(256);
    left->addWidget(new QLabel("Name"), 1, 0);
    left->addWidget(name_, 1, 1);
    description_ = new QLineEdit;
    description_->setObjectName("exportDescription");
    description_->setAccessibleName("Style description");
    description_->setMaxLength(4096);
    left->addWidget(new QLabel("Description"), 2, 0);
    left->addWidget(description_, 2, 1);
    int row = 3;
    auto fact = [&](const QString &label, const QString &value, const char *objectName) {
        left->addWidget(new QLabel(label), row, 0, Qt::AlignTop);
        auto *text = new QLabel(value);
        text->setWordWrap(true);
        text->setObjectName(objectName);
        left->addWidget(text, row++, 1);
    };
    fact("Display", modeWord(style.mode) + " · 14 settings", "exportDisplay");
    fact("Edge stroke", styleSettingsWords(style), "exportStroke");
    QString texture = "None";
    if (style.edgeStroke && style.edgeStroke->texture) {
        const auto &asset = doc.assets().at(style.edgeStroke->texture);
        if (asset->payload) {
            texture = QString::fromStdString(asset->name) + " · asset " +
                      QString::number(asset->id) + " · " + textureFormat(asset->mediaType) +
                      " · stored in the file ✓";
        } else {
            texture = "⚠ " + QString::fromStdString(asset->name) + " · Missing";
        }
    }
    fact("Texture", texture, "exportTexture");
    fact("Dependencies",
         style.edgeStroke && style.edgeStroke->texture
             ? QString("None outside the file. The texture is inside it.")
             : QString("None"),
         "exportDependencies");

    auto *right = new QVBoxLayout;
    columns->addLayout(right, 1);
    right->addWidget(heading("Not written (counted)"));
    auto *counts = new QGridLayout;
    right->addLayout(counts);
    const std::pair<const char *, size_t> items[] = {
        {"Materials (swatches)", doc.materials().size()},
        {"Tags", doc.tags().size()},
        {"Components", doc.definitions().size()},
        {"Scenes", doc.scenes().size()}};
    int countRow = 0;
    for (const auto &[label, count] : items) {
        counts->addWidget(new QLabel(label), countRow, 0);
        auto *value = new QLabel(QString::number(qulonglong(count)));
        value->setAlignment(Qt::AlignRight);
        counts->addWidget(value, countRow++, 1);
    }
    right->addWidget(muted("These belong in an asset catalog: File → Export asset catalog…."));
    right->addSpacing(8);
    right->addWidget(heading("Edge texture"));
    right->addWidget(muted("If the texture is missing from this model, the export is refused and "
                           "names it. A partial style is never written."));
    right->addStretch();

    error_ = errorLabel("exportError");
    layout->addWidget(error_);
    showError(error_, missingTextureRefusal(doc, "Export", "written"));

    auto *footer = new QHBoxLayout;
    footer->addWidget(muted("A new file is created. An existing file is never replaced."), 1);
    auto *cancel = new QPushButton("Cancel");
    cancel->setObjectName("exportCancel");
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    footer->addWidget(cancel);
    auto *exportButton = new QPushButton("Export to file…");
    exportButton->setObjectName("exportPrimary");
    exportButton->setDefault(true);
    connect(exportButton, &QPushButton::clicked, this, [this] { exportFile(); });
    footer->addWidget(exportButton);
    layout->addLayout(footer);
}

void StyleExportDialog::exportFile() {
    try {
        StyleMetadata metadata;
        metadata.name = name_->text().trimmed();
        metadata.description = description_->text();
        // The bytes are built before the file dialog, so a refusal names the texture and writes
        // nothing (UX_DESIGN §18.6).
        if (const auto refusal = missingTextureRefusal(doc_, "Export", "written"); !refusal.isEmpty())
            throw std::runtime_error(refusal.toUtf8().constData());
        const auto bytes = exportStyleBundle(doc_, metadata);
        QFileDialog file(this, "Export style");
        file.setObjectName("exportStyleFileDialog");
        file.setAcceptMode(QFileDialog::AcceptSave);
        file.setFileMode(QFileDialog::AnyFile);
        file.setNameFilter("SketchyUp styles (*.sketchystyle)");
        file.setDefaultSuffix("sketchystyle");
        file.setOption(QFileDialog::DontConfirmOverwrite);
        file.selectFile(metadata.name + ".sketchystyle");
        if (file.exec() != QDialog::Accepted || file.selectedFiles().isEmpty())
            return;
        auto path = file.selectedFiles().front();
        if (!path.endsWith(".sketchystyle", Qt::CaseInsensitive))
            path += ".sketchystyle";
        if (QFileInfo::exists(path))
            throw std::runtime_error("That file already exists. An existing file is never "
                                     "replaced; choose a new name.");
        writeStyleBundle(bytes, path);
        path_ = path;
        accept();
    } catch (const std::exception &error) {
        showError(error_, QString::fromUtf8(error.what()));
    }
}

StyleSaveDialog::StyleSaveDialog(Document &doc, const QString &folder, QWidget *parent)
    : QDialog(parent), doc_(doc), folder_(folder) {
    setObjectName("saveStyleDialog");
    setWindowTitle("Save style to library");
    setMinimumWidth(560);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(heading("Save style to library"));
    layout->addWidget(muted("Copies the current style into the library folder. This model is not "
                            "changed."));
    auto *form = new QGridLayout;
    layout->addLayout(form);
    form->addWidget(new QLabel("Name"), 0, 0);
    name_ = new QLineEdit(suggestFreeStyleName(folder_, "Model style"));
    name_->setObjectName("libraryStyleName");
    name_->setAccessibleName("Style name");
    name_->setMaxLength(64);
    form->addWidget(name_, 0, 1);
    error_ = errorLabel("libraryStyleError");
    form->addWidget(error_, 1, 1);
    form->addWidget(new QLabel("File"), 2, 0);
    file_ = new QLabel;
    file_->setObjectName("libraryStyleFile");
    file_->setWordWrap(true);
    form->addWidget(file_, 2, 1);
    form->addWidget(new QLabel("Folder"), 3, 0);
    auto *folderLabel = new QLabel(QDir::toNativeSeparators(folder_));
    folderLabel->setWordWrap(true);
    form->addWidget(folderLabel, 3, 1);
    form->addWidget(new QLabel("Description"), 4, 0);
    description_ = new QLineEdit;
    description_->setObjectName("libraryStyleDescription");
    description_->setAccessibleName("Style description");
    description_->setMaxLength(4096);
    form->addWidget(description_, 4, 1);
    layout->addWidget(muted("Name rule: 1 to 64 characters; letters, digits, spaces, dots, "
                            "underscores and hyphens; starting and ending with a letter, digit, "
                            "underscore or hyphen."));
    layout->addStretch();

    auto *footer = new QHBoxLayout;
    footer->addStretch();
    auto *cancel = new QPushButton("Cancel");
    cancel->setObjectName("libraryStyleCancel");
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    footer->addWidget(cancel);
    save_ = new QPushButton("Save to library");
    save_->setObjectName("libraryStyleSave");
    save_->setDefault(true);
    footer->addWidget(save_);
    layout->addLayout(footer);

    // A taken name is refused here, before anything is written, and the free suggestion is shown.
    auto check = [this] {
        const auto name = name_->text().trimmed();
        const auto rule = styleLibraryNameError(name);
        const auto target = styleLibraryPath(folder_, name);
        const bool taken = rule.isEmpty() && QFileInfo::exists(target);
        if (!rule.isEmpty()) {
            showError(error_, rule);
            file_->setText(QString());
        } else if (taken) {
            showError(error_, "A style named “" + name +
                                  "” is already in the library. An existing file is never "
                                  "replaced; choose a new name.");
            file_->setText(QFileInfo(styleLibraryPath(folder_, suggestFreeStyleName(folder_, name)))
                               .fileName() +
                           " (suggested, free)");
        } else {
            showError(error_, QString());
            file_->setText(QFileInfo(target).fileName() + " (free)");
        }
        save_->setEnabled(rule.isEmpty() && !taken);
    };
    connect(name_, &QLineEdit::textChanged, this, check);
    connect(save_, &QPushButton::clicked, this, [this] {
        try {
            const auto name = name_->text().trimmed();
            const auto rule = styleLibraryNameError(name);
            require(rule.isEmpty(), rule.toUtf8().constData());
            const auto target = styleLibraryPath(folder_, name);
            if (QFileInfo::exists(target))
                throw std::runtime_error(("A style named “" + name +
                                          "” is already in the library. An existing file is never "
                                          "replaced; choose a new name.")
                                             .toUtf8()
                                             .constData());
            if (const auto refusal = missingTextureRefusal(doc_, "Save", "saved"); !refusal.isEmpty())
                throw std::runtime_error(refusal.toUtf8().constData());
            StyleMetadata metadata;
            metadata.name = name;
            metadata.description = description_->text();
            const auto bytes = exportStyleBundle(doc_, metadata);
            QDir().mkpath(folder_);
            writeStyleBundle(bytes, target);
            path_ = target;
            accept();
        } catch (const std::exception &error) {
            showError(error_, QString::fromUtf8(error.what()));
        }
    });
    check();
    name_->setFocus();
}

} // namespace sketchy
