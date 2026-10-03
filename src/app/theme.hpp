#pragma once
#include <QColor>
namespace sketchy {
struct ThemeColors {
    QColor surface, ink, border, selected, hover, input, accent, muted, canvas;
    bool dark;
};
inline ThemeColors themeColors(bool dark) {
    if (dark)
        return {"#202923", "#edf2ea", "#48584b", "#405842", "#344337", "#172019",
                "#9ec99d", "#b0beaf", "#252e28", true};
    return {"#f7f7f2", "#263a32", "#c9d4c5", "#dce7dc", "#e6ebdf", "#ffffff",
            "#547858", "#617266", "#f0f1ec", false};
}
} // namespace sketchy
