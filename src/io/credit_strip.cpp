#include "io/credit_strip.hpp"

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace sketchy {
namespace {
// Metrics are taken at 100 pixels per millimetre, so their values convert to millimetres by 1/100.
constexpr double metricPixelsPerMm = 100;
constexpr double stripSizesMm[] = {2.5, 2.2, 2.0, 1.8, 1.6, 1.4};

QFont fontOfPixelSize(double pixelSize) {
    QFont font(creditStripFamily());
    font.setPixelSize(std::max(1, static_cast<int>(std::lround(pixelSize))));
    return font;
}
} // namespace

QString creditStripFamily() { return QStringLiteral("DejaVu Sans"); }

QJsonArray creditNoticesJson(const SourceCredits &credits) {
    QJsonArray notices;
    for (const auto &notice : credits.notices())
        notices.append(QString::fromStdString(notice));
    return notices;
}

std::optional<CreditStrip> layoutCreditStrip(const std::string &text, double widthMm,
                                             double maxHeightMm) {
    const auto notice = QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
    const auto words = notice.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (words.isEmpty() || !(widthMm > 0) || !(maxHeightMm > 0))
        return std::nullopt;
    for (const double em : stripSizesMm) {
        const QFontMetricsF metrics(fontOfPixelSize(em * metricPixelsPerMm));
        const double maxWidth = widthMm * metricPixelsPerMm;
        std::vector<QString> lines;
        QString current;
        bool fits = true;
        for (const auto &word : words) {
            if (metrics.horizontalAdvance(word) > maxWidth) {
                fits = false;
                break;
            }
            const auto candidate = current.isEmpty() ? word : current + QLatin1Char(' ') + word;
            if (metrics.horizontalAdvance(candidate) <= maxWidth) {
                current = candidate;
                continue;
            }
            lines.push_back(current);
            current = word;
        }
        if (!fits)
            continue;
        if (!current.isEmpty())
            lines.push_back(current);
        CreditStrip strip;
        strip.lines = std::move(lines);
        strip.emMm = em;
        strip.lineMm = metrics.lineSpacing() / metricPixelsPerMm;
        strip.ascentMm = metrics.ascent() / metricPixelsPerMm;
        strip.heightMm = static_cast<double>(strip.lines.size()) * strip.lineMm;
        if (strip.heightMm <= maxHeightMm)
            return strip;
    }
    return std::nullopt;
}

void paintCreditStrip(QPainter &painter, const QTransform &paperToDevice, const CreditStrip &strip,
                      QPointF topLeftMm) {
    // The strip is set in device pixels: its font size is the em in device pixels, so the text
    // keeps its paper size whatever transform the drawing used.
    const double pixelsPerMm = paperToDevice.m11();
    const auto origin = paperToDevice.map(topLeftMm);
    const QFont font = fontOfPixelSize(strip.emMm * pixelsPerMm);
    painter.save();
    painter.setWorldTransform(QTransform(), false);
    painter.setClipping(false);
    painter.setFont(font);
    const double ascent = strip.ascentMm * pixelsPerMm;
    for (size_t i = 0; i < strip.lines.size(); ++i) {
        const QPointF baseline(origin.x(), origin.y() + ascent +
                                               static_cast<double>(i) * strip.lineMm * pixelsPerMm);
        painter.drawText(baseline, strip.lines[i]);
    }
    painter.restore();
}

} // namespace sketchy
