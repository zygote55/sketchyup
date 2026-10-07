#include "app/window.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSpinBox>
#include <QVBoxLayout>
namespace sketchy {
void Window::exportMeasuredDialog() {
    QDialog dialog(this);
    dialog.setObjectName("measuredExportDialog");
    dialog.setWindowTitle("Export measured PDF / SVG");
    QFormLayout form(&dialog);
    auto *note = new QLabel("Exports an orthographic projection in the current viewing direction, "
                            "centred on the view target. Enter the physical paper size and scale. "
                            "Print at actual size to preserve measurements.");
    note->setWordWrap(true);
    form.addRow(note);
    auto *format = new QComboBox;
    format->setObjectName("measuredFormat");
    format->addItems({"PDF", "SVG"});
    form.addRow("Format", format);
    auto *mode = new QComboBox;
    mode->setObjectName("measuredMode");
    mode->addItems({"Technical lines (vector)", "Current appearance (raster)"});
    form.addRow("Content", mode);
    auto *appearance = new QLabel("Technical lines preserve visible edges, section cuts and "
                                  "dimensions. Faces are unfilled; colors, textures, images and "
                                  "lighting are omitted. Choose raster appearance to retain them.");
    appearance->setWordWrap(true);
    form.addRow(appearance);
    auto field = [&](const char *name, const QString &label, double low, double high,
                     double initial, int decimals) {
        auto *value = new QDoubleSpinBox;
        value->setObjectName(name);
        value->setDecimals(decimals);
        value->setRange(low, high);
        value->setValue(initial);
        form.addRow(label, value);
        return value;
    };
    auto *width = field("measuredWidth", "Paper width (mm)", 10, 2000, 297, 2);
    auto *height = field("measuredHeight", "Paper height (mm)", 10, 2000, 210, 2);
    auto *margin = field("measuredMargin", "Margin (mm)", 0, 999, 10, 2);
    auto *scale = field("measuredScale", "Scale 1 :", .001, 1e9, 100, 3);
    auto *hidden = new QCheckBox("Show hidden lines as dashed");
    hidden->setObjectName("measuredHidden");
    form.addRow(hidden);
    auto *dpi = new QSpinBox;
    dpi->setObjectName("measuredDpi");
    dpi->setRange(72, 300);
    dpi->setValue(150);
    dpi->setEnabled(false);
    form.addRow("Raster resolution (DPI)", dpi);
    connect(mode, &QComboBox::currentIndexChanged, &dialog, [=](int index) {
        dpi->setEnabled(index == 1);
        hidden->setEnabled(index == 0);
    });
    auto *error = new QLabel;
    error->setObjectName("measuredExportError");
    error->setTextFormat(Qt::PlainText);
    error->setWordWrap(true);
    form.addRow(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    form.addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            MeasuredPage page;
            page.widthMm = width->value();
            page.heightMm = height->value();
            page.marginMm = margin->value();
            page.scaleDenominator = scale->value();
            page.includeHidden = hidden->isChecked() && mode->currentIndex() == 0;
            page.validate();
            const auto kind =
                format->currentIndex() == 0 ? MeasuredFormat::Pdf : MeasuredFormat::Svg;
            const auto output =
                viewport_->renderMeasuredView(page, kind, mode->currentIndex() == 1, dpi->value());
            QFileDialog file(&dialog, "New measured drawing");
            file.setObjectName("measuredExportFileDialog");
            file.setAcceptMode(QFileDialog::AcceptSave);
            file.setFileMode(QFileDialog::AnyFile);
            file.setOption(QFileDialog::DontConfirmOverwrite);
            file.setNameFilter(kind == MeasuredFormat::Pdf ? "PDF document (*.pdf)"
                                                           : "SVG drawing (*.svg)");
            file.setDefaultSuffix(kind == MeasuredFormat::Pdf ? "pdf" : "svg");
            if (file.exec() != QDialog::Accepted || file.selectedFiles().isEmpty())
                return;
            writeMeasuredExport(output, file.selectedFiles().front());
            QDialog report(&dialog);
            report.setObjectName("measuredExportReport");
            report.setWindowTitle("Measured drawing exported");
            QVBoxLayout layout(&report);
            auto *summary =
                new QLabel(QString("%1 × %2 mm · 1:%3 · %4. Print at actual size.")
                               .arg(page.widthMm)
                               .arg(page.heightMm)
                               .arg(page.scaleDenominator)
                               .arg(mode->currentIndex() == 1 ? "raster appearance"
                                                              : "vector technical lines"));
            summary->setWordWrap(true);
            layout.addWidget(summary);
            auto *details = new QPlainTextEdit;
            details->setObjectName("measuredExportDetails");
            details->setReadOnly(true);
            QString explanation;
            if (mode->currentIndex() == 1) {
                explanation =
                    QString("Raster appearance: %1 × %2 pixels at %3 DPI.\nImages, surface "
                            "appearance, sections and annotations are embedded in the drawing.\n")
                        .arg(output.report["rasterWidth"].toInt())
                        .arg(output.report["rasterHeight"].toInt())
                        .arg(output.report["rasterDpi"].toDouble(), 0, 'f', 1);
            } else {
                explanation = QString("Vector technical lines: %1 segments and %2 "
                                      "annotations.\nSurface colors, textures, images and lighting "
                                      "are omitted. Annotation text uses portable outlines.\n")
                                  .arg(output.report["lineSegments"].toInt())
                                  .arg(output.report["annotations"].toInt());
                const auto losses = output.report["losses"].toObject();
                for (auto it = losses.begin(); it != losses.end(); ++it) {
                    auto label = it.key();
                    label.replace(QRegularExpression("([a-z])([A-Z])"), "\\1 \\2");
                    explanation += QString("%1: %2\n").arg(label).arg(it.value().toInt());
                }
                if (output.report["missingFontGlyphs"].toInt())
                    explanation += QString("Unavailable font glyphs: %1\n")
                                       .arg(output.report["missingFontGlyphs"].toInt());
            }
            details->setPlainText(explanation);
            layout.addWidget(details);
            auto *close = new QDialogButtonBox(QDialogButtonBox::Close);
            connect(close, &QDialogButtonBox::rejected, &report, &QDialog::reject);
            layout.addWidget(close);
            report.resize(640, 480);
            report.exec();
            status_->setText("Exported measured drawing");
            dialog.accept();
        } catch (const std::exception &failure) {
            error->setText(QString::fromUtf8(failure.what()));
        }
    });
    dialog.exec();
}
} // namespace sketchy
