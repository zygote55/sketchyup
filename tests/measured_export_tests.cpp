#include "core/annotations.hpp"
#include "dem_export_fixture.hpp"
#include "io/measured_export.hpp"
#include "pdf_info_check.hpp"
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
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
MeasuredDrawing drawing(const Document &doc, double scale = 50) {
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
    page.scaleDenominator = scale;
    return captureMeasuredDrawing(RenderSnapshot::capture(doc, options), page);
}
// The embedded PNG of an SVG's raster appearance.
QImage embeddedPng(const QByteArray &svg) {
    const QByteArray marker("data:image/png;base64,");
    const auto start = svg.indexOf(marker);
    check(start >= 0, "SVG embeds a PNG");
    const auto end = svg.indexOf('"', start);
    return QImage::fromData(
        QByteArray::fromBase64(svg.mid(start + marker.size(), end - start - marker.size())), "PNG");
}
// R107.c2 (ADR 0181 §3.3): a drawing of DEM terrain carries its notice in the bottom page margin,
// as text in vector output. A raster is composed with the notice in its own bottom margin, so it
// keeps its size.
void sourceCredit() {
    Document terrain;
    importDemFixture(terrain, std::string(SOURCE_DIR) + "/tests/fixtures/geotiff/",
                     "plane-f32-strip-none-le.tif", DemProduct::Dep3, "NAVD88");
    // 1:1000: the 300 m terrain window fills the page, so its lines reach it.
    const auto shown = drawing(terrain, 2000);
    const QString usgs = QString::fromStdString(demCreditUsgs);
    check(shown.credits.joined() == demCreditUsgs, "The drawing carries the 3DEP notice");
    // Vector SVG: the notice is the text of <text> elements, each in the bottom margin (below 203.2
    // mm).
    const auto svg = exportMeasuredDrawing(shown, MeasuredFormat::Svg);
    check(svg.report["creditPlacement"].toString() == "bottomMargin" &&
              svg.report["credits"].toArray().size() == 1,
          "The SVG report lists the notice and its placement");
    QXmlStreamReader xml(svg.bytes);
    QString text;
    bool inText = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == u"text") {
            inText = true;
            const auto baseline = xml.attributes().value("y").toString().toDouble();
            check(baseline > 193.2 && baseline <= 203.2, "The credit sits in the bottom margin");
        } else if (xml.isEndElement() && xml.name() == u"text") {
            inText = false;
        } else if (xml.isCharacters() && inText) {
            text += xml.text();
        }
    }
    check(!xml.hasError() && text == usgs, "The SVG text is the 3DEP notice");
    check(exportMeasuredDrawing(shown, MeasuredFormat::Pdf).report["credits"].toArray().size() == 1,
          "The PDF report lists the notice");
    // Raster: the strip is part of the image. The drawing keeps its size; white outside it, red
    // inside.
    QImage raster(1000, 800, QImage::Format_ARGB32_Premultiplied);
    raster.fill(Qt::red);
    const auto composed =
        embeddedPng(exportMeasuredDrawing(shown, MeasuredFormat::Svg, raster).bytes);
    check(composed.size() == QSize(1000, 800), "The composed raster keeps its size");
    check(composed.pixelColor(500, 400) == QColor(Qt::red) &&
              composed.pixelColor(500, 10) == QColor(Qt::white),
          "The drawing stays in the margins and the top margin stays white");
    int dark = 0;
    for (int y = 765; y < 800; ++y)
        for (int x = 0; x < 1000; ++x)
            dark += composed.pixelColor(x, y).lightness() < 128;
    check(dark > 50, "The strip's text is in the bottom margin of the image");
    // A document with no DEM terrain keeps its raster as it was: no composition, no strip.
    Document plainDocument;
    plainDocument.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 1, 0}, {0, 1, 0}}});
    const auto plain = exportMeasuredDrawing(drawing(plainDocument), MeasuredFormat::Svg, raster);
    check(!plain.report.contains("credits") && !plain.bytes.contains("<text"),
          "A drawing without DEM terrain names no credit and writes no text");
    check(embeddedPng(plain.bytes).pixelColor(500, 790) == QColor(Qt::red),
          "Without a credit the raster is not composed");
    // A page with no margin has no room for the notice: refused, not overlapped.
    auto zero = shown;
    zero.page.marginMm = 0;
    rejects([&] { exportMeasuredDrawing(zero, MeasuredFormat::Pdf); });
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
        // D-036: well-formed document information on every Qt (Qt 6.8.0 left it unclosed).
        const auto info = pdfcheck::infoProblem(
            pdf.bytes, "SketchyUp vector measured orthographic drawing", "SketchyUp");
        if (!info.empty())
            std::cerr << "PDF document information: " << info << '\n';
        check(info.empty(), "Well-formed PDF document information");
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
            QProcess attachments;
            attachments.start(QFileInfo(executable).dir().filePath("pdfdetach"),
                              {"-saveall", "-o", scratch.path(), file});
            check(attachments.waitForStarted(10000) && attachments.waitForFinished(10000) &&
                      attachments.exitCode() == 0,
                  "Independent PDF attachment extraction");
            QFile reportFile(scratch.filePath("drawing-report.json"));
            check(reportFile.open(QIODevice::ReadOnly),
                  "PDF drawing report is independently recoverable");
            const auto report = QJsonDocument::fromJson(reportFile.readAll()).object();
            check(report["scaleDenominator"] == 50 && report["rasterized"] == false &&
                      report["units"] == "mm",
                  "PDF embedded report preserves scale and vector disclosure");
            Document wire;
            wire.addWire(0, {0, 0, 0}, {2, 0, 0});
            const auto wireFile = scratch.filePath("wire.pdf");
            writeMeasuredExport(exportMeasuredDrawing(drawing(wire), MeasuredFormat::Pdf),
                                wireFile);
            const auto wirePrefix = scratch.filePath("wire");
            consumer.start(executable, {"-singlefile", "-r", "254", "-png", wireFile, wirePrefix});
            check(consumer.waitForStarted(10000) && consumer.waitForFinished(60000) &&
                      consumer.exitCode() == 0,
                  "Independent wire PDF consumer");
            const QImage wireImage(wirePrefix + ".png");
            check(!wireImage.isNull() && wireImage.pixelColor(1470, 1016).red() < 100,
                  "Horizontal standalone line survives zero-height path bounding box");
            QImage red(1000, 800, QImage::Format_RGB32);
            red.fill(Qt::red);
            const auto rasterFile = scratch.filePath("raster.pdf");
            writeMeasuredExport(exportMeasuredDrawing(page, MeasuredFormat::Pdf, red), rasterFile);
            const auto rasterPrefix = scratch.filePath("raster");
            consumer.start(executable,
                           {"-singlefile", "-r", "254", "-png", rasterFile, rasterPrefix});
            check(consumer.waitForStarted(10000) && consumer.waitForFinished(60000) &&
                      consumer.exitCode() == 0,
                  "Independent raster PDF consumer");
            const QImage rasterImage(rasterPrefix + ".png");
            if (!rasterImage.isNull())
                std::cerr << "Raster centre/margin: "
                          << rasterImage.pixelColor(1270, 1016).name().toStdString() << " / "
                          << rasterImage.pixelColor(50, 50).name().toStdString() << '\n';
            check(!rasterImage.isNull() && rasterImage.pixelColor(1270, 1016) == QColor(Qt::red) &&
                      rasterImage.pixelColor(50, 50) == QColor(Qt::white),
                  "Embedded appearance raster and paper margins render correctly");
            // R107.c2: the notice is text in the PDF's text layer, in a DEM drawing.
            Document terrain;
            importDemFixture(terrain, std::string(SOURCE_DIR) + "/tests/fixtures/geotiff/",
                             "plane-f32-strip-none-le.tif", DemProduct::Dep3, "NAVD88");
            const auto demPdf = exportMeasuredDrawing(drawing(terrain, 2000), MeasuredFormat::Pdf);
            const auto demFile = scratch.filePath("dem-drawing.pdf");
            QFile demOut(demFile);
            check(demOut.open(QIODevice::WriteOnly) &&
                      demOut.write(demPdf.bytes) == demPdf.bytes.size(),
                  "Write the DEM drawing PDF");
            demOut.close();
            QProcess reader;
            reader.start(QFileInfo(executable).dir().filePath("pdftotext"), {demFile, "-"});
            check(reader.waitForStarted(10000) && reader.waitForFinished(60000) &&
                      reader.exitCode() == 0,
                  "pdftotext reads the DEM drawing PDF");
            check(QString::fromUtf8(reader.readAllStandardOutput())
                      .simplified()
                      .contains(QString::fromStdString(demCreditUsgs)),
                  "The DEM drawing's PDF text layer carries the 3DEP notice");
            std::cout << "Poppler independently verified page size, print scale, placement, wire, "
                         "raster and report\n";
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
        const auto fixtureFolder = qEnvironmentVariable("SKETCHYUP_MEASURED_FIXTURES");
        if (!fixtureFolder.isEmpty()) {
            QDir directory(fixtureFolder);
            check(directory.mkpath("."), "Measured fixture folder");
            writeMeasuredExport(annotated, directory.filePath("dimension.svg"));
            writeMeasuredExport(annotatedPdf, directory.filePath("dimension.pdf"));
        }
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
        sourceCredit();
        std::cout << "Measured PDF/SVG serialization, dimensions, Unicode outlines, raster "
                     "disclosure and publication passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
