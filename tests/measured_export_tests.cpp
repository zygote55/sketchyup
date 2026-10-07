#include "core/annotations.hpp"
#include "io/measured_export.hpp"
#include <QFile>
#include <QGuiApplication>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
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
    throw std::runtime_error("Expected measured export rejection");
}
MeasuredDrawing drawing(const Document &doc) {
    RenderCamera camera;
    camera.orthographic = true;
    camera.position = {0, 0, 10};
    camera.target = {};
    camera.up = {0, 1, 0};
    RenderOptions options;
    options.camera = camera;
    MeasuredPage page;
    page.widthMm = 254;
    page.heightMm = 203.2;
    page.scaleDenominator = 50;
    return captureMeasuredDrawing(RenderSnapshot::capture(doc, options), page);
}
int main(int argc, char **argv) {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    try {
        QTemporaryDir scratch;
        check(scratch.isValid(), "Scratch folder");
        Document doc;
        doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 1, 0}, {0, 1, 0}}});
        const auto page = drawing(doc);
        const auto svg = exportMeasuredDrawing(page, MeasuredFormat::Svg);
        check(!svg.report["rasterized"].toBool() && !svg.bytes.contains("data:image"),
              "Pure vector drawing");
        QXmlStreamReader xml(svg.bytes);
        bool root = false, physicalLine = false;
        while (!xml.atEnd()) {
            xml.readNext();
            if (!xml.isStartElement())
                continue;
            if (xml.name() == u"svg") {
                root = true;
                check(xml.attributes().value("width") == u"254mm" &&
                          xml.attributes().value("height") == u"203.2mm" &&
                          xml.attributes().value("viewBox") == u"0 0 254 203.2",
                      "Exact SVG physical page dimensions");
            }
            if (xml.name() == u"path") {
                const auto d = xml.attributes().value("d").toString();
                physicalLine |=
                    d.contains("M127 101.6L167 101.6") || d.contains("M167 101.6L127 101.6");
            }
        }
        check(!xml.hasError() && root && physicalLine,
              "Independent SVG parser finds 40 mm source edge at 1:50");
        const auto pdf = exportMeasuredDrawing(page, MeasuredFormat::Pdf);
        const auto media =
            QRegularExpression(R"(/MediaBox\s*\[\s*0\s+0\s+([0-9.]+)\s+([0-9.]+)\s*\])")
                .match(QString::fromLatin1(pdf.bytes));
        if (!media.hasMatch() || media.captured(1).toDouble() != 720 ||
            media.captured(2).toDouble() != 576)
            std::cerr << "MediaBox: " << media.captured().toStdString() << '\n';
        check(pdf.bytes.startsWith("%PDF-") && media.hasMatch() &&
                  media.captured(1).toDouble() == 720 && media.captured(2).toDouble() == 576,
              "PDF exact 10 by 8 inch media box");
        const auto file = scratch.filePath("measured.pdf");
        writeMeasuredExport(pdf, file);
        if (app.arguments().contains("--pdf-real")) {
            const auto executable = qEnvironmentVariable("SKETCHYUP_PDF_TEST");
            if (executable.isEmpty())
                return 77;
            QProcess consumer;
            const auto prefix = scratch.filePath("render");
            consumer.start(executable, {"-singlefile", "-r", "254", "-png", file, prefix});
            check(consumer.waitForStarted(10000) && consumer.waitForFinished(60000) &&
                      consumer.exitCode() == 0 && consumer.exitStatus() == QProcess::NormalExit,
                  "Independent Poppler PDF consumer");
            const QImage image(prefix + ".png");
            check(image.size() == QSize(2540, 2032),
                  "Independent PDF consumer verifies physical page size");
            int left = image.width(), right = -1, top = image.height(), bottom = -1;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x) {
                    const auto c = image.pixelColor(x, y);
                    if (c.red() < 100 && c.green() < 100 && c.blue() < 100) {
                        left = std::min(left, x);
                        right = std::max(right, x);
                        top = std::min(top, y);
                        bottom = std::max(bottom, y);
                    }
                }
            check(std::abs(left - 1270) <= 2 && std::abs(right - 1670) <= 2 &&
                      std::abs(top - 816) <= 2 && std::abs(bottom - 1016) <= 2,
                  "Independent PDF raster measures 2 by 1 metres as 40 by 20 millimetres at 1:50");
            std::cout << "Poppler independently verified PDF page size, print scale and rectangle "
                         "placement\n";
            return 0;
        }
        rejects([&] { writeMeasuredExport(pdf, file); });
        auto corrupt = pdf;
        corrupt.bytes += ' ';
        rejects([&] { writeMeasuredExport(corrupt, scratch.filePath("bad.pdf")); });
        QFile source(file);
        check(source.open(QIODevice::ReadOnly) && source.readAll() == pdf.bytes,
              "Non-replacing verified PDF publication");
        AnnotationRecord annotation;
        annotation.name = "Dimension";
        annotation.kind = AnnotationKind::Distance;
        annotation.anchors = {pointAnchor({0, 0, 0}), pointAnchor({2, 0, 0})};
        annotation.offset = {0, -.5, 0};
        annotation.text = "Width <2> & Δ قياس";
        createAnnotation(doc, annotation);
        const auto annotated = exportMeasuredDrawing(drawing(doc), MeasuredFormat::Svg);
        check(annotated.bytes.contains("Width &lt;2&gt; &amp;") && annotated.bytes.contains("2 m"),
              "Shaped annotation outlines retain escaped readable label");
        check(annotated.report["missingFontGlyphs"].toInt() == 0,
              "Unicode annotation shaping resolves font glyphs");
        QXmlStreamReader annotationXml(annotated.bytes);
        while (!annotationXml.atEnd())
            annotationXml.readNext();
        check(!annotationXml.hasError(), "Unicode SVG remains well-formed");
        const auto annotatedPdf = exportMeasuredDrawing(drawing(doc), MeasuredFormat::Pdf);
        check(annotatedPdf.bytes.contains("/EmbeddedFiles") &&
                  annotatedPdf.bytes.contains("/EmbeddedFile"),
              "PDF includes machine-readable drawing report");
        QImage raster(1000, 800, QImage::Format_ARGB32_Premultiplied);
        raster.fill(Qt::red);
        const auto rasterSvg = exportMeasuredDrawing(page, MeasuredFormat::Svg, raster);
        check(rasterSvg.report["rasterized"].toBool() &&
                  rasterSvg.report["rasterDpi"].toDouble() == 100 &&
                  rasterSvg.bytes.contains("data:image/png;base64,"),
              "Self-contained raster appearance is explicitly identified at physical resolution");
        check(
            exportMeasuredDrawing(page, MeasuredFormat::Pdf, raster).report["rasterized"].toBool(),
            "PDF raster report");
        rejects([&] { exportMeasuredDrawing(page, MeasuredFormat::Svg, QImage{}); });
        rejects([&] {
            exportMeasuredDrawing(page, MeasuredFormat::Svg,
                                  QImage(100, 100, QImage::Format_RGB32));
        });
        std::cout << "Measured PDF/SVG serialization, dimensions, Unicode outlines, raster "
                     "disclosure and publication passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
