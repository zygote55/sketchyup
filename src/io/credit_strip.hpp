#pragma once
// R107.c2 (ADR 0181 §3.3, owner decision 8): the visible credit of a drawing that shows DEM
// terrain. A PDF sheet's "Sources" line and a measured drawing's credit strip share this layout and
// painter, so the text is the helper's notice wherever it appears. The strip is real text
// (selectable in a PDF).
#include "core/source_credits.hpp"
#include <QJsonArray>
#include <QPointF>
#include <QString>
#include <QTransform>
#include <optional>
#include <string>
#include <vector>

class QPainter;

namespace sketchy {

struct CreditStrip {
    std::vector<QString> lines; // The notice, word-wrapped; the words keep their order and spacing.
    double emMm{};              // Font size: the em, in millimetres.
    double lineMm{};            // Baseline-to-baseline distance.
    double ascentMm{};          // Baseline offset from the top of the first line.
    double heightMm{};          // All the lines.
};

// The notice laid out to widthMm, at the largest size from 2.5 mm (about 7 pt) down to 1.4 mm
// (about 4 pt) whose height is at most maxHeightMm. Empty when no size fits, for example a
// zero-width band.
std::optional<CreditStrip> layoutCreditStrip(const std::string &text, double widthMm,
                                             double maxHeightMm);

// Paints the strip with its first line's top-left corner at topLeftMm, which paperToDevice maps to
// device pixels. The painter's transform and clip are restored afterwards, so the strip may sit
// outside the clip the drawing uses.
void paintCreditStrip(QPainter &painter, const QTransform &paperToDevice, const CreditStrip &strip,
                      QPointF topLeftMm);

// The font family the strip uses (the family the measured annotation text uses).
QString creditStripFamily();

// The notices as a JSON array of strings, for an export report's "credits" key.
QJsonArray creditNoticesJson(const SourceCredits &credits);

} // namespace sketchy
