#pragma once
#include <QColor>
namespace sketchy {
struct ThemeColors {
    QColor surface, ink, border, selected, hover, input, accent, muted, canvas;
    bool dark;
    // ADR 0164 §7 (R091.e). Selection blue-violet, warning amber (stale, "Changed") and failure
    // red (broken), each with the surface and border its banners and chips use. Paper is white
    // in both themes because it represents print; the pasteboard is the area around it.
    QColor selectionStroke{}, warning{}, warningSurface{}, warningBorder{}, failure{},
        failureSurface{}, paper{"#ffffff"}, pasteboard{};
    // ADR 0166 §1.2 (R094.c). Information blue and its fill: an overridden component
    // parameter's caption, border and field.
    QColor information{}, informationSurface{};
    // D-023: the 3D canvas's own focus ring. The accent, except that the light palette's is one
    // step lighter in green so it has 3:1 against the dark chrome beside the canvas (the accent is
    // 2.997:1 there) while keeping 4.3:1 against the light canvas and chrome.
    QColor canvasRing{};
};
inline ThemeColors themeColors(bool dark) {
    if (dark) {
        ThemeColors colors{"#202923", "#edf2ea", "#48584b", "#405842", "#344337",
                           "#172019", "#9ec99d", "#b0beaf", "#252e28", true};
        colors.selectionStroke = QColor("#a29bf5");
        colors.warning = QColor("#ecc983");
        colors.warningSurface = QColor("#3b3322");
        colors.warningBorder = QColor("#8a6a2c");
        colors.failure = QColor("#f9c0b8");
        colors.failureSurface = QColor("#3d2624");
        colors.pasteboard = QColor("#252e28");
        colors.information = QColor("#b3cdf8");
        colors.informationSurface = QColor("#1f2b3d");
        colors.canvasRing = colors.accent;
        return colors;
    }
    ThemeColors colors{"#f7f7f2", "#263a32", "#c9d4c5", "#dce7dc", "#e6ebdf",
                       "#ffffff", "#547858", "#617266", "#f0f1ec", false};
    colors.selectionStroke = QColor("#5b5bd6");
    colors.warning = QColor("#7a4f00");
    colors.warningSurface = QColor("#fdf3dc");
    colors.warningBorder = QColor("#e3b75c");
    colors.failure = QColor("#a8322b");
    colors.failureSurface = QColor("#fbf1f0");
    colors.pasteboard = QColor("#e9ebe4");
    colors.information = QColor("#2c5aa0");
    colors.informationSurface = QColor("#eef3fb");
    colors.canvasRing = QColor("#547958");
    return colors;
}
} // namespace sketchy
