#include "app/reference_images_panel.hpp"
#include "app/unit_display.hpp"
#include "automation/measurements.hpp"
#include "io/texture_image.hpp"
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
namespace sketchy {
namespace {
QLineEdit *field(QFormLayout &form, const char *id, const char *label, const QString &value) {
    auto *result = new QLineEdit(value);
    result->setObjectName(id);
    form.addRow(label, result);
    return result;
}
double number(QLineEdit *field) {
    bool okay{};
    const auto value = QLocale().toDouble(field->text(), &okay);
    if (!okay || !std::isfinite(value))
        throw std::runtime_error("Enter a finite number");
    return value;
}
void buttons(QDialog &dialog, QFormLayout &form, const std::function<void()> &save) {
    auto *error = new QLabel;
    error->setObjectName("referenceDialogError");
    error->setTextFormat(Qt::PlainText);
    error->setWordWrap(true);
    form.addRow(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    form.addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&, error, save] {
        try {
            save();
            dialog.accept();
        } catch (const std::exception &e) {
            error->setText(QString::fromUtf8(e.what()));
        }
    });
}
} // namespace
ReferenceImagesPanel::ReferenceImagesPanel(Document &doc, Viewport &view, QWidget *parent)
    : QWidget(parent), doc_(doc), view_(view) {
    setObjectName("referenceImagesPanel");
    auto *layout = new QVBoxLayout(this);
    auto *note =
        new QLabel("Place a PNG or JPEG as a reference plane. Pixels are stored in the model. "
                   "Move, rotate and scale the whole image with the normal tools.");
    note->setWordWrap(true);
    layout->addWidget(note);
    details_ = new QLabel;
    details_->setObjectName("referenceImageDetails");
    details_->setWordWrap(true);
    details_->setTextFormat(Qt::PlainText);
    layout->addWidget(details_);
    auto *import = new QPushButton("Import reference image…");
    import->setObjectName("referenceImageImport");
    layout->addWidget(import);
    edit_ = new QPushButton("Edit selected image…");
    edit_->setObjectName("referenceImageEdit");
    layout->addWidget(edit_);
    calibrate_ = new QPushButton("Calibrate selected image…");
    calibrate_->setObjectName("referenceImageCalibrate");
    layout->addWidget(calibrate_);
    layout->addStretch();
    setFocusProxy(import);
    connect(import, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, "Import reference image", {},
                                                       "Images (*.png *.jpg *.jpeg)");
        if (path.isEmpty())
            return;
        try {
            importFile(path);
        } catch (const std::exception &e) {
            details_->setText(QString::fromUtf8(e.what()));
        }
    });
    connect(edit_, &QPushButton::clicked, this, [this] { edit(false); });
    connect(calibrate_, &QPushButton::clicked, this, [this] { edit(true); });
    connect(&view_, &Viewport::changed, this, [this] { refresh(); });
    connect(&view_, &Viewport::selected, this, [this] { refresh(); });
    connect(&view_, &QOpenGLWidget::frameSwapped, this, [this] {
        if (isVisible())
            refresh(); // Image availability changes without a document edit.
    });
    refresh();
}
Id ReferenceImagesPanel::selectedImage() const {
    const auto &selected = view_.selectionState().entities();
    if (selected.size() != 1)
        return 0;
    const auto entity = *selected.begin();
    return entity.kind == SelectionKind::Body && doc_.bodies().contains(entity.body) &&
                   doc_.bodies().at(entity.body)->referenceImage
               ? entity.body
               : 0;
}
void ReferenceImagesPanel::refresh() {
    const auto id = selectedImage();
    const bool editable = id && !view_.selectionState().locked(doc_, id);
    edit_->setEnabled(editable);
    calibrate_->setEnabled(editable);
    if (!id) {
        details_->setText("Select one reference image to edit its size, opacity or calibration.");
        return;
    }
    const auto &body = *doc_.bodies().at(id);
    const auto &image = *body.referenceImage;
    const auto &asset = *doc_.assets().at(image.asset);
    details_->setText(
        QString("%1\n%2 × %3 · opacity %4%\n%5\n%6")
            .arg(QString::fromStdString(body.name))
            .arg(displayLength(image.width, doc_.displayUnits(), doc_.displayPrecision()))
            .arg(displayLength(image.height, doc_.displayUnits(), doc_.displayPrecision()))
            .arg(image.opacity * 100, 0, 'g', 5)
            .arg(asset.payload ? "Embedded pixels" : "Missing pixels — purple placeholder")
            .arg(view_.textureSummary()));
}
void ReferenceImagesPanel::importFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > qint64(AssetPayload::limit))
        throw std::runtime_error("Choose a readable image no larger than 16 MiB");
    const auto data = file.read(AssetPayload::limit + 1);
    if (data.size() > qsizetype(AssetPayload::limit) || file.error() != QFileDevice::NoError)
        throw std::runtime_error("Could not read the image within the 16 MiB limit");
    const auto type = data.startsWith("\x89PNG\r\n\x1a\n") ? "image/png" : "image/jpeg";
    const auto payload =
        std::make_shared<AssetPayload>(std::vector<std::uint8_t>(data.begin(), data.end()));
    const auto decoded = decodeTextureImage({1, "Reference", type, payload});
    if (!decoded.image)
        throw std::runtime_error("Choose a complete PNG or JPEG up to 4096 × 4096 pixels");
    const auto stamp = doc_.saveStamp();
    const auto revision = doc_.revision();
    QDialog dialog(this);
    dialog.setObjectName("referenceImportDialog");
    dialog.setWindowTitle("Place reference image");
    QFormLayout form(&dialog);
    auto *name = field(form, "referenceName", "Name", QFileInfo(path).completeBaseName());
    auto *width = field(form, "referenceWidth", "Width", displayLength(1, doc_.displayUnits(), fullDisplayPrecision));
    auto *hint =
        new QLabel("The image is placed on the local XY plane at the context origin. Height "
                   "follows the pixel aspect ratio. Calibrate it afterward using a known length.");
    hint->setWordWrap(true);
    form.addRow(hint);
    buttons(dialog, form, [&, stamp, revision] {
        if (doc_.revision() != revision || !doc_.isCurrentSnapshot(stamp))
            throw std::runtime_error("The model changed. Reopen the image importer.");
        const auto meters = parseLength(width->text(), inputUnit(doc_.displayUnits()), QLocale());
        view_.importReferenceImage(data, type, name->text(), meters,
                                   meters * decoded.image->height() / decoded.image->width());
    });
    dialog.exec();
}
void ReferenceImagesPanel::edit(bool calibrate) {
    const auto id = selectedImage();
    if (!id)
        return;
    const auto original = doc_.bodies().at(id);
    const auto image = *original->referenceImage;
    const auto stamp = doc_.saveStamp();
    const auto revision = doc_.revision();
    QDialog dialog(this);
    dialog.setObjectName(calibrate ? "referenceCalibrationDialog" : "referenceEditDialog");
    dialog.setWindowTitle(calibrate ? "Calibrate reference image" : "Edit reference image");
    QFormLayout form(&dialog);
    std::function<QJsonObject()> command;
    if (calibrate) {
        auto *hint = new QLabel(
            "Enter two image positions from 0 to 1, measured from the top-left corner, and their "
            "known distance. Calibration preserves the first point and the image aspect ratio.");
        hint->setWordWrap(true);
        form.addRow(hint);
        auto *u1 = field(form, "referenceFirstU", "First horizontal", "0");
        auto *v1 = field(form, "referenceFirstV", "First vertical", "1");
        auto *u2 = field(form, "referenceSecondU", "Second horizontal", "1");
        auto *v2 = field(form, "referenceSecondV", "Second vertical", "1");
        const auto measured = sketchy::length(doc_.worldTransform(id).vector({image.width, 0, 0}));
        const auto parent = original->parent ? doc_.worldTransform(original->parent) : Transform{};
        auto *length = field(form, "referenceLength", "Known length",
                             displayLength(measured, doc_.displayUnits(), fullDisplayPrecision));
        const auto originalLength = length->text();
        command = [=, this] {
            const ImagePoint first{number(u1), number(v1)}, second{number(u2), number(v2)};
            const auto known =
                length->text() == originalLength
                    ? measured
                    : parseLength(length->text(), inputUnit(doc_.displayUnits()), QLocale());
            const auto calibrated =
                calibrateReferenceImage(image, original->transform, parent, first, second, known);
            if (calibrated.image == image && calibrated.local == original->transform)
                return QJsonObject{};
            return QJsonObject{{"command", "reference_image.calibrate"},
                               {"body", QString::number(id)},
                               {"first", QJsonArray{first[0], first[1]}},
                               {"second", QJsonArray{second[0], second[1]}},
                               {"knownLength", known}};
        };
    } else {
        auto *name = field(form, "referenceName", "Name", QString::fromStdString(original->name));
        auto *width =
            field(form, "referenceWidth", "Width",
                  displayLength(image.width, doc_.displayUnits(), fullDisplayPrecision));
        auto *height = field(form, "referenceHeight", "Height",
                             displayLength(image.height, doc_.displayUnits(), fullDisplayPrecision));
        auto *opacity = field(form, "referenceOpacity", "Opacity (0 to 1)",
                              QLocale().toString(image.opacity, 'g', 15));
        const auto oldWidth = width->text(), oldHeight = height->text(),
                   oldOpacity = opacity->text();
        command = [=, this] {
            QJsonObject result{{"command", "reference_image.update"},
                               {"body", QString::number(id)}};
            if (name->text() != QString::fromStdString(original->name))
                result["name"] = name->text();
            if (width->text() != oldWidth)
                result["width"] =
                    parseLength(width->text(), inputUnit(doc_.displayUnits()), QLocale());
            if (height->text() != oldHeight)
                result["height"] =
                    parseLength(height->text(), inputUnit(doc_.displayUnits()), QLocale());
            if (opacity->text() != oldOpacity)
                result["opacity"] = number(opacity);
            return result.size() > 2 ? result : QJsonObject{};
        };
    }
    buttons(dialog, form, [&, stamp, revision] {
        if (doc_.revision() != revision || !doc_.isCurrentSnapshot(stamp))
            throw std::runtime_error("The model changed. Reopen the image editor.");
        const auto value = command();
        if (!value.isEmpty())
            view_.editReferenceImages({value});
    });
    dialog.exec();
}
} // namespace sketchy
