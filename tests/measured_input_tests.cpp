#include "app/surface_format.hpp"
#include "app/window.hpp"
#include "core/annotations.hpp"
#include "core/assets.hpp"
#include "core/reference_images.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QXmlStreamReader>
#include <iostream>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Expected measured native rejection");
}
QImage embeddedImage(const QByteArray &svg) {
    QXmlStreamReader xml(svg);
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == u"image")
            for (const auto &attribute : xml.attributes())
                if (attribute.name() == u"href") {
                    const auto value = attribute.value().toLatin1();
                    const auto comma = value.indexOf(',');
                    return QImage::fromData(QByteArray::fromBase64(value.mid(comma + 1)), "PNG");
                }
    }
    throw std::runtime_error("Missing measured raster image");
}
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(0);
    sketchy::setDefaultViewportFormat(format);
    QTemporaryDir files;
    qputenv("XDG_CONFIG_HOME", files.path().toUtf8());
    qputenv("XDG_DATA_HOME", files.path().toUtf8());
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    try {
        Window window;
        window.resize(1100, 850);
        auto &doc = window.document();
        auto &view = *window.viewport();
        const auto png = encodeTexturePng(TextureImage(1, 1, {255, 0, 0, 255}));
        const auto asset = createAsset(
            doc, "Red reference", "image/png",
            std::make_shared<AssetPayload>(std::vector<std::uint8_t>(png.begin(), png.end())));
        const auto imageBody = createReferenceImage(doc, {asset, 2, 1, 1});
        auto style = doc.style();
        style.axesVisible = style.gridVisible = style.groundVisible = false;
        style.background = {1, 1, 1};
        doc.setStyle(style);
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Measured window exposed");
        view.standardView(1);
        view.frameBounds({0, 0, 0}, {2, 1, 0});
        view.refresh();
        QCoreApplication::processEvents();
        view.grabFramebuffer();
        check(QTest::qWaitFor([&] { return !view.texturesPending(); }, 10000),
              "Measured images loaded");
        view.grabFramebuffer();
        view.setSelection(imageBody);
        doc.markSaved();
        const auto before = encodeContainer(doc);
        const auto camera = describeRenderCamera(view.renderCamera());
        const auto selection = view.selectionState().entities();
        const auto ordinary = view.renderRaster({800, 600});
        MeasuredPage page;
        page.widthMm = 254;
        page.heightMm = 203.2;
        page.scaleDenominator = 50;
        const auto raster = view.renderMeasuredView(page, MeasuredFormat::Svg, true, 254);
        check(raster.report["rasterized"].toBool(), "Native raster output disclosed");
        const auto pixels = embeddedImage(raster.bytes);
        check(pixels.size() == QSize(2540, 2032), "Exact physical raster dimensions");
        int left = pixels.width(), right = -1, top = pixels.height(), bottom = -1;
        for (int y = 0; y < pixels.height(); ++y)
            for (int x = 0; x < pixels.width(); ++x) {
                const auto c = pixels.pixelColor(x, y);
                if (c.red() > 220 && c.green() < 30 && c.blue() < 30) {
                    left = std::min(left, x);
                    right = std::max(right, x);
                    top = std::min(top, y);
                    bottom = std::max(bottom, y);
                }
            }
        check(std::abs(left - 1070) <= 2 && std::abs(right - 1470) <= 2 &&
                  std::abs(top - 916) <= 2 && std::abs(bottom - 1116) <= 2,
              "Native reference image measures 40 by 20 mm at 1:50, independent of viewport size");
        const auto vector = view.renderMeasuredView(page, MeasuredFormat::Svg, false);
        check(!vector.report["rasterized"].toBool() &&
                  vector.report["losses"].toObject()["referenceImagesOmitted"] == 1 &&
                  !vector.bytes.contains("data:image"),
              "Technical-line image omission explicit");
        check(encodeContainer(doc) == before &&
                  describeRenderCamera(view.renderCamera()) == camera &&
                  view.selectionState().entities() == selection &&
                  view.renderRaster({800, 600}) == ordinary,
              "Measured export restores camera, normal renderer, document and selection");
        rejects([&] { view.renderMeasuredView(page, MeasuredFormat::Pdf, true, 301); });
        view.setClipPlane(std::array<double, 4>{0, 0, 1, 0});
        rejects([&] { view.renderMeasuredView(page, MeasuredFormat::Pdf, false); });
        view.setClipPlane({});
        page.marginMm = 150;
        rejects([&] { view.renderMeasuredView(page, MeasuredFormat::Svg, false); });
        page.marginMm = 10;
        AnnotationRecord dimension;
        dimension.name = "Two metres";
        dimension.anchors = {pointAnchor({0, 0, 0}), pointAnchor({2, 0, 0})};
        dimension.offset = {0, -.4, 0};
        createAnnotation(doc, dimension);
        view.refresh();
        auto *action = window.findChild<QAction *>("file.exportMeasured");
        check(action, "Measured export File action");
        bool cancel = true, configured = false, fileChosen = false, reportSeen = false,
             errorSeen = false, handling = false;
        QString asynchronousFailure;
        const auto destination = files.filePath("measured.svg");
        QTimer timer;
        timer.setInterval(10);
        QObject::connect(&timer, &QTimer::timeout, &window, [&] {
            if (handling)
                return;
            handling = true;
            try {
                if (auto *report = window.findChild<QDialog *>("measuredExportReport");
                    report && report->isVisible()) {
                    reportSeen = report->findChild<QPlainTextEdit *>("measuredExportDetails")
                                     ->toPlainText()
                                     .contains("Vector technical lines");
                    if (const auto evidence = qEnvironmentVariable("SKETCHYUP_MEASURED_EVIDENCE");
                        !evidence.isEmpty())
                        check(report->grab().save(evidence), "Measured report screenshot");
                    report->reject();
                } else if (auto *file = window.findChild<QFileDialog *>("measuredExportFileDialog");
                           file && file->isVisible() && !fileChosen) {
                    fileChosen = true;
                    file->findChild<QLineEdit *>("fileNameEdit")->setText(destination);
                    QMetaObject::invokeMethod(file, "accept", Qt::QueuedConnection);
                } else if (auto *dialog = window.findChild<QDialog *>("measuredExportDialog");
                           dialog && dialog->isVisible()) {
                    if (cancel)
                        dialog->reject();
                    else if (!dialog->findChild<QLabel *>("measuredExportError")
                                  ->text()
                                  .isEmpty()) {
                        errorSeen = true;
                        dialog->reject();
                    } else if (!configured) {
                        configured = true;
                        dialog->findChild<QComboBox *>("measuredFormat")->setCurrentIndex(1);
                        dialog->findChild<QDoubleSpinBox *>("measuredWidth")->setValue(254);
                        dialog->findChild<QDoubleSpinBox *>("measuredHeight")->setValue(203.2);
                        dialog->findChild<QDoubleSpinBox *>("measuredScale")->setValue(50);
                        auto *save =
                            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save);
                        QMetaObject::invokeMethod(save, "click", Qt::QueuedConnection);
                    }
                }
            } catch (const std::exception &e) {
                asynchronousFailure = QString::fromUtf8(e.what());
                for (auto *d : window.findChildren<QDialog *>())
                    d->reject();
            }
            handling = false;
        });
        timer.start();
        action->trigger();
        check(!QFile::exists(destination), "Cancel creates no file");
        cancel = false;
        action->trigger();
        check(asynchronousFailure.isEmpty() && reportSeen && !errorSeen &&
                  QFile::exists(destination),
              "Native dialog exports measured SVG and reports content");
        QFile saved(destination);
        check(saved.open(QIODevice::ReadOnly), "Open exported SVG");
        const auto original = saved.readAll();
        saved.close();
        configured = fileChosen = reportSeen = false;
        action->trigger();
        check(errorSeen && !reportSeen, "Native existing destination rejected");
        check(saved.open(QIODevice::ReadOnly) && saved.readAll() == original,
              "Existing export remains byte-exact");
        timer.stop();
        check(describeRenderCamera(view.renderCamera()) == camera,
              "Dialog export preserves camera");
        std::cout << "Measured native workflow: physical raster scale, source/view restoration, "
                     "vectors, annotations, cancel and nonreplacement passed\n";
        window.hide();
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
