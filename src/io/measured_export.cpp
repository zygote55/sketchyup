#include "io/measured_export.hpp"
#include "io/new_file.hpp"
#include "presentation/annotation_display.hpp"
#include <QBuffer>
#include <QCryptographicHash>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QPainter>
#include <QPainterPath>
#include <QPdfWriter>
#include <QTextLayout>
#include <QXmlStreamWriter>
namespace sketchy {
namespace {
constexpr qsizetype byteLimit = 64 * 1024 * 1024;
constexpr double mmPerPixel = 25.4 / 96;
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
QString number(double value) {
    require(std::isfinite(value), "Nonfinite drawing coordinate");
    return QString::number(value == 0 ? 0 : value, 'g', 16);
}
QString hash(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
class BoundedOutput final : public QIODevice {
  public:
    QByteArray bytes;
    bool exceeded{};
    BoundedOutput() { open(QIODevice::WriteOnly); }
    bool isSequential() const override { return true; }

  protected:
    qint64 readData(char *, qint64) override { return -1; }
    qint64 writeData(const char *data, qint64 size) override {
        if (size < 0 || size > byteLimit - bytes.size()) {
            exceeded = true;
            return -1;
        }
        bytes.append(data, size);
        return size;
    }
};
struct Primitive {
    QPainterPath path;
    QColor color{Qt::black};
    double width{.25};
    bool fill{}, dashed{};
    QString label;
};
struct DrawingPaths {
    std::vector<Primitive> values;
    qsizetype elements{}, textBytes{}, missingGlyphs{};
    void append(Primitive p) {
        require(p.path.elementCount() <= 1000000 - elements,
                "Drawing exceeds one million path elements");
        elements += p.path.elementCount();
        values.push_back(std::move(p));
    }
};
QPointF point(Vec3 p) {
    require(std::isfinite(p.x) && std::isfinite(p.y) && std::abs(p.x) <= 1e12 &&
                std::abs(p.y) <= 1e12,
            "Drawing point exceeds finite page bounds");
    return {p.x, p.y};
}
QRectF content(const MeasuredPage &p) {
    return {p.marginMm, p.marginMm, p.widthMm - 2 * p.marginMm, p.heightMm - 2 * p.marginMm};
}
DrawingPaths paths(const MeasuredDrawing &drawing) {
    DrawingPaths result;
    require(drawing.geometry.lines.size() <= 100000 &&
                drawing.annotations.size() <= annotationRecordLimit,
            "Drawing record budget exceeded");
    for (bool hidden : {true, false}) {
        Primitive geometry;
        geometry.dashed = hidden;
        geometry.color = hidden ? QColor(130, 130, 130) : QColor(Qt::black);
        for (const auto &line : drawing.geometry.lines)
            if (line.hidden == hidden) {
                geometry.path.moveTo(point(line.a));
                geometry.path.lineTo(point(line.b));
            }
        result.append(std::move(geometry));
    }
    for (const auto &annotation : drawing.annotations) {
        const auto &record = annotation.record;
        const auto &measurement = annotation.measurement;
        const auto text = annotationText(record, measurement, drawing.units);
        result.textBytes += text.toUtf8().size();
        require(result.textBytes <= 1024 * 1024, "Drawing annotation text exceeds 1 MiB");
        const bool broken = measurement.state != AnchorState::Resolved;
        const auto color =
            broken ? QColor(180, 45, 30)
                   : QColor::fromRgbF(record.color[0], record.color[1], record.color[2]);
        Primitive lines;
        lines.color = color;
        lines.width = 1.5 * mmPerPixel;
        lines.dashed = broken;
        auto segment = [&](QPointF a, QPointF b) {
            lines.path.moveTo(a);
            lines.path.lineTo(b);
        };
        auto tick = [&](QPointF p, QPointF direction) {
            const auto size = std::hypot(direction.x(), direction.y());
            if (size < 1e-9)
                return;
            direction /= size;
            const QPointF perpendicular{-direction.y(), direction.x()};
            segment(p, p + direction * (7 * mmPerPixel) + perpendicular * (3 * mmPerPixel));
            segment(p, p + direction * (7 * mmPerPixel) - perpendicular * (3 * mmPerPixel));
        };
        const auto textPoint = point(annotation.textPoint);
        if (record.kind == AnnotationKind::Distance) {
            require(measurement.anchors.size() == 2 && annotation.anchors.size() == 2,
                    "Invalid dimension capture");
            const auto a = point(annotation.anchors[0]), b = point(annotation.anchors[1]);
            const auto da =
                point(drawing.page.project(measurement.anchors[0].point + record.offset));
            const auto db =
                point(drawing.page.project(measurement.anchors[1].point + record.offset));
            segment(da, db);
            if (record.leader) {
                segment(a, da);
                segment(b, db);
            }
            tick(da, db - da);
            tick(db, da - db);
        } else if (record.leader) {
            require(annotation.anchors.size() == 1, "Invalid label capture");
            const auto a = point(annotation.anchors[0]);
            segment(a, textPoint);
            tick(a, textPoint - a);
        }
        if (broken)
            for (size_t i = 0; i < measurement.anchors.size(); ++i)
                if (measurement.anchors[i].state != AnchorState::Resolved) {
                    const auto p = point(annotation.anchors.at(i));
                    const auto d = 4 * mmPerPixel;
                    segment(p + QPointF(-d, -d), p + QPointF(d, d));
                    segment(p + QPointF(-d, d), p + QPointF(d, -d));
                }
        result.append(std::move(lines));
        QFont font("DejaVu Sans");
        font.setPixelSize(qRound(record.textSize));
        auto layoutText = text;
        layoutText.replace('\n', QChar(0x2028));
        QTextLayout layout(layoutText, font);
        QTextOption option;
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        layout.setTextOption(option);
        layout.beginLayout();
        double height{};
        int count{};
        while (true) {
            const auto line = layout.createLine();
            if (!line.isValid())
                break;
            require(++count <= 1024, "Annotation exceeds 1024 text lines");
            auto positioned = line;
            positioned.setLineWidth(360);
            positioned.setPosition({0, height});
            height += positioned.height();
        }
        layout.endLayout();
        QPainterPath glyphs;
        for (const auto &run : layout.glyphRuns()) {
            const auto indexes = run.glyphIndexes();
            const auto positions = run.positions();
            for (qsizetype i = 0; i < indexes.size(); ++i) {
                if (!indexes[i])
                    ++result.missingGlyphs;
                const auto path = run.rawFont().pathForGlyph(indexes[i]).translated(positions[i]);
                require(path.elementCount() <= 1000000 - result.elements - glyphs.elementCount(),
                        "Annotation glyph budget exceeded");
                glyphs.addPath(path);
            }
        }
        QTransform transform;
        transform.translate(textPoint.x(), textPoint.y());
        transform.scale(mmPerPixel, mmPerPixel);
        const auto bounds = glyphs.boundingRect();
        transform.translate(-bounds.center().x(), -bounds.center().y());
        Primitive background;
        background.fill = true;
        background.color = Qt::white;
        background.path.addRoundedRect(transform.mapRect(bounds).adjusted(-1, -.7, 1, .7), .6, .6);
        result.append(std::move(background));
        Primitive label;
        label.fill = true;
        label.color = color;
        label.label = text;
        label.path = transform.map(glyphs);
        result.append(std::move(label));
    }
    return result;
}
QString pathData(const QPainterPath &path) {
    QString out;
    for (int i = 0; i < path.elementCount(); ++i) {
        const auto p = path.elementAt(i);
        if (p.isMoveTo())
            out += "M" + number(p.x) + " " + number(p.y);
        else if (p.isLineTo())
            out += "L" + number(p.x) + " " + number(p.y);
        else if (p.type == QPainterPath::CurveToElement) {
            require(i + 2 < path.elementCount(), "Incomplete drawing curve");
            const auto b = path.elementAt(++i), c = path.elementAt(++i);
            out += "C" + number(p.x) + " " + number(p.y) + " " + number(b.x) + " " + number(b.y) +
                   " " + number(c.x) + " " + number(c.y);
        } else
            throw std::runtime_error("Invalid drawing path element");
        require(out.size() <= byteLimit / 2, "SVG path exceeds output budget");
    }
    return out;
}
} // namespace
MeasuredExport exportMeasuredDrawing(const MeasuredDrawing &drawing, MeasuredFormat format,
                                     const std::optional<QImage> &pageRaster) {
    drawing.page.validate();
    require(qobject_cast<QGuiApplication *>(QCoreApplication::instance()),
            "Measured export requires the graphical font worker");
    require(format == MeasuredFormat::Svg || format == MeasuredFormat::Pdf,
            "Unknown measured export format");
    if (pageRaster) {
        const auto &image = *pageRaster;
        require(!image.isNull() && image.width() <= 8192 && image.height() <= 8192 &&
                    qint64(image.width()) * image.height() <= 16000000,
                "Page raster must be nonempty and at most 8192 pixels per side / 16 megapixels");
        require(std::abs(image.width() -
                         image.height() * drawing.page.widthMm / drawing.page.heightMm) <= 1,
                "Page raster aspect ratio differs from measured paper");
    }
    const auto geometry = pageRaster ? DrawingPaths{} : paths(drawing);
    BoundedOutput output;
    const auto &page = drawing.page;
    MeasuredExport result;
    result.report = drawing.report;
    result.report["format"] = format == MeasuredFormat::Svg ? "svg" : "pdf";
    result.report["rasterized"] = bool(pageRaster);
    result.report["annotationText"] = pageRaster ? "rasterized" : "shaped vector outlines";
    result.report["missingFontGlyphs"] = double(geometry.missingGlyphs);
    if (pageRaster) {
        result.report["mode"] = "orthographicRasterAppearance";
        result.report["technicalLineOmissions"] = result.report.take("losses");
        result.report["surfaceColorAndLighting"] = "appearance captured in page raster";
        result.report["rasterWidth"] = pageRaster->width();
        result.report["rasterHeight"] = pageRaster->height();
        result.report["rasterDpi"] = pageRaster->width() * 25.4 / page.widthMm;
    }
    if (format == MeasuredFormat::Svg) {
        QXmlStreamWriter xml(&output);
        xml.writeStartDocument();
        xml.writeStartElement("svg");
        xml.writeDefaultNamespace("http://www.w3.org/2000/svg");
        xml.writeAttribute("version", "1.1");
        xml.writeAttribute("width", number(page.widthMm) + "mm");
        xml.writeAttribute("height", number(page.heightMm) + "mm");
        xml.writeAttribute("viewBox", "0 0 " + number(page.widthMm) + " " + number(page.heightMm));
        xml.writeTextElement("title", "SketchyUp measured orthographic drawing");
        xml.writeTextElement(
            "desc", QString::fromUtf8(QJsonDocument(result.report).toJson(QJsonDocument::Compact)));
        const auto clip = content(page);
        xml.writeStartElement("defs");
        xml.writeStartElement("clipPath");
        xml.writeAttribute("id", "paper-content");
        xml.writeStartElement("rect");
        xml.writeAttribute("x", number(clip.x()));
        xml.writeAttribute("y", number(clip.y()));
        xml.writeAttribute("width", number(clip.width()));
        xml.writeAttribute("height", number(clip.height()));
        xml.writeEndElement();
        xml.writeEndElement();
        xml.writeEndElement();
        xml.writeStartElement("g");
        xml.writeAttribute("clip-path", "url(#paper-content)");
        if (pageRaster) {
            QByteArray png;
            QBuffer buffer(&png);
            buffer.open(QIODevice::WriteOnly);
            require(pageRaster->save(&buffer, "PNG"), "Could not encode drawing raster");
            xml.writeStartElement("image");
            xml.writeAttribute("width", number(page.widthMm));
            xml.writeAttribute("height", number(page.heightMm));
            xml.writeAttribute("http://www.w3.org/1999/xlink", "href",
                               "data:image/png;base64," + QString::fromLatin1(png.toBase64()));
            xml.writeEndElement();
        } else
            for (const auto &p : geometry.values) {
                xml.writeStartElement("path");
                xml.writeAttribute("d", pathData(p.path));
                xml.writeAttribute("fill", p.fill ? p.color.name() : "none");
                xml.writeAttribute("fill-rule", "evenodd");
                xml.writeAttribute("stroke", p.fill ? "none" : p.color.name());
                if (!p.fill) {
                    xml.writeAttribute("stroke-width", number(p.width));
                    xml.writeAttribute("stroke-linecap", "round");
                    xml.writeAttribute("stroke-linejoin", "round");
                }
                if (p.dashed)
                    xml.writeAttribute("stroke-dasharray",
                                       number(p.width * 4) + " " + number(p.width * 2));
                if (!p.label.isEmpty())
                    xml.writeTextElement("title", p.label);
                xml.writeEndElement();
            }
        xml.writeEndElement();
        xml.writeEndElement();
        xml.writeEndDocument();
        require(!xml.hasError(), "SVG serialization failed or exceeded output budget");
    } else {
        QPdfWriter pdf(&output);
        pdf.setResolution(2540);
        pdf.setTitle(pageRaster ? "SketchyUp rasterized measured orthographic drawing"
                                : "SketchyUp vector measured orthographic drawing");
        pdf.addFileAttachment("drawing-report.json", QJsonDocument(result.report).toJson(),
                              "application/json");
        pdf.setCreator("SketchyUp");
        const QPageSize size(QSizeF(page.widthMm, page.heightMm), QPageSize::Millimeter, "Measured",
                             QPageSize::ExactMatch);
        require(pdf.setPageLayout(
                    QPageLayout(size, QPageLayout::Portrait, QMarginsF{}, QPageLayout::Millimeter)),
                "PDF page layout rejected");
        QPainter painter;
        require(painter.begin(&pdf), "PDF painter could not start");
        painter.setRenderHint(QPainter::LosslessImageRendering);
        painter.scale(100, 100);
        painter.setClipRect(content(page));
        if (pageRaster)
            painter.drawImage(QRectF(0, 0, page.widthMm, page.heightMm), *pageRaster);
        else
            for (const auto &p : geometry.values) {
                if (!p.path.boundingRect()
                         .adjusted(-p.width, -p.width, p.width, p.width)
                         .intersects(content(page)))
                    continue;
                painter.setPen(p.fill
                                   ? QPen(Qt::NoPen)
                                   : QPen(p.color, p.width, p.dashed ? Qt::DashLine : Qt::SolidLine,
                                          Qt::RoundCap, Qt::RoundJoin));
                painter.setBrush(p.fill ? QBrush(p.color) : QBrush(Qt::NoBrush));
                painter.drawPath(p.path);
            }
        require(painter.end(), "PDF painter could not finish");
    }
    require(!output.exceeded && !output.bytes.isEmpty(),
            "Measured output exceeds 64 MiB or is empty");
    result.bytes = std::move(output.bytes);
    result.report["sha256"] = hash(result.bytes);
    result.report["bytes"] = result.bytes.size();
    return result;
}
void writeMeasuredExport(const MeasuredExport &output, const QString &path) {
    require(!output.bytes.isEmpty() && output.bytes.size() <= byteLimit &&
                output.report["sha256"].toString() == hash(output.bytes),
            "Measured output hash or size is invalid");
    publishNewFile(path, output.bytes);
}
} // namespace sketchy
