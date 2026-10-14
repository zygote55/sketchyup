#pragma once
#include "core/drawing_sheet_records.hpp"
#include "io/drawing_items.hpp"
#include "io/drawing_view.hpp"
#include <QString>
#include <cmath>
#include <optional>
namespace sketchy {
// Shared wording of the Sheets workspace (R091.e), so the canvas, Tray, breadcrumb and
// accessible descriptions name pages, papers and view states the same way.
inline QString sheetTitle(const DrawingSheetRecord &sheet) {
    const auto name = QString::fromStdString(sheet.name);
    return sheet.number.empty() ? name : QString::fromStdString(sheet.number) + " " + name;
}
inline QString paperMillimetres(double value) {
    return std::abs(value - std::round(value)) < 1e-6
               ? QString::number(qlonglong(std::round(value)))
               : QString::number(value, 'f', 1);
}
// "A3 landscape · 420 × 297 mm · page 1 of 4".
inline QString sheetPaperLine(const DrawingSheetRecord &sheet, size_t count) {
    const auto &paper = sheet.paper;
    const auto named = QString::fromStdString(drawingPaperName(paper));
    const auto orientation = paper.widthMm > paper.heightMm   ? "landscape"
                             : paper.widthMm < paper.heightMm ? "portrait"
                                                              : "square";
    return (named.isEmpty() ? QString("Custom paper") : named + " " + orientation) + " · " +
           paperMillimetres(paper.widthMm) + " × " + paperMillimetres(paper.heightMm) +
           " mm · page " + QString::number(sheet.position + 1) + " of " + QString::number(count);
}
inline QString viewScaleText(const DrawingViewRecord &view, bool notToScale) {
    return notToScale ? QString("Not to scale")
                      : "1:" + QString::number(view.scaleDenominator, 'g', 10);
}
inline QString brokenReasonText(const std::string &reason) {
    if (reason == "sceneMissing")
        return "scene missing";
    if (reason == "sceneHasNoCamera")
        return "scene has no camera";
    if (reason == "temporaryClipPlane")
        return "temporary section";
    if (reason == "showHidden")
        return "scene shows hidden geometry";
    if (reason == "notToScale")
        return "not to scale";
    if (reason == "referenceMissing")
        return "reference missing";
    return QString::fromStdString(reason);
}
// Short state: "Current", "Changed", "Broken: scene missing", "Could not draw".
inline QString viewStateText(const DrawingViewStatus &status) {
    switch (status.state) {
    case DrawingViewState::Current:
        return "Current";
    case DrawingViewState::Stale:
        return "Changed";
    case DrawingViewState::Broken:
        return "Broken: " + (status.reasons.empty() ? QString("cannot draw")
                                                    : brokenReasonText(status.reasons.front()));
    case DrawingViewState::Failed:
        return "Could not draw";
    }
    return {};
}
// Long state for descriptions: "Changed since accepted".
inline QString viewStateSentence(const DrawingViewStatus &status) {
    return status.state == DrawingViewState::Stale ? QString("Changed since accepted")
                                                   : viewStateText(status);
}
} // namespace sketchy
