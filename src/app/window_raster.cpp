#include "app/window.hpp"
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QSpinBox>
namespace sketchy {
void Window::exportRasterDialog() {
    QDialog dialog(this);
    dialog.setObjectName("rasterExportDialog");
    dialog.setWindowTitle("Export view as PNG");
    QFormLayout form(&dialog);
    auto *width = new QSpinBox;
    auto *height = new QSpinBox;
    width->setObjectName("rasterWidth");
    height->setObjectName("rasterHeight");
    for (auto *field : {width, height})
        field->setRange(1, 8192);
    width->setValue(1920);
    height->setValue(1080);
    form.addRow("Width (pixels)", width);
    form.addRow("Height (pixels)", height);
    auto *note = new QLabel(
        "Render the current camera at these exact dimensions, up to 16 megapixels. "
        "Vertical framing stays fixed; changing the aspect ratio changes the horizontal framing. "
        "Model style, images, sections, sun and annotations are included. Selection, tools, guides "
        "and interface overlays are omitted. Text sizes are measured in output pixels.");
    note->setWordWrap(true);
    form.addRow(note);
    auto *error = new QLabel;
    error->setObjectName("rasterExportError");
    error->setWordWrap(true);
    error->setTextFormat(Qt::PlainText);
    form.addRow(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    form.addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            const QSize size(width->value(), height->value());
            if (qint64(size.width()) * size.height() > 16 * 1024 * 1024)
                throw std::runtime_error("Choose dimensions totaling at most 16 megapixels");
            const auto path =
                QFileDialog::getSaveFileName(&dialog, "Save PNG", "View.png", "PNG images (*.png)");
            if (path.isEmpty())
                return;
            viewport_->exportRaster(path, size);
            status_->setText(
                QString("Exported %1 × %2 pixel PNG").arg(size.width()).arg(size.height()));
            dialog.accept();
        } catch (const std::exception &failure) {
            error->setText(QString::fromUtf8(failure.what()));
        }
    });
    dialog.exec();
}
} // namespace sketchy
