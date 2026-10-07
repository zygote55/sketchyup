#pragma once
#include <QSettings>
namespace sketchy {
inline int interfaceTextPercent() {
    bool valid = false;
    const auto value =
        QSettings("SketchyUp", "SketchyUp").value("interfaceTextPercent", 100).toInt(&valid);
    return valid && value >= 75 && value <= 200 && value % 25 == 0 ? value : 100;
}
inline int interfaceExtent(int logicalPixels, int percent) {
    return (logicalPixels * percent + 50) / 100;
}
} // namespace sketchy
