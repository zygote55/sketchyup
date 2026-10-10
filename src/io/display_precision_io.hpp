#pragma once
#include "core/units.hpp"
#include <QJsonValue>
#include <stdexcept>
namespace sketchy {
// Public automation wire form: "full" or a decimal-place count. Native model
// JSON stores the integer, with -1 for Full.
inline QJsonValue encodeDisplayPrecision(int precision) {
    return precision == fullDisplayPrecision ? QJsonValue("full") : QJsonValue(precision);
}
inline int decodeDisplayPrecision(const QJsonValue &value, DisplayUnit unit) {
    if (value == QJsonValue("full"))
        return fullDisplayPrecision;
    const auto number = value.toDouble(-1);
    if (!value.isDouble() || number < 0 || number != double(int(number)) ||
        !validDisplayPrecision(unit, int(number)))
        requireDisplayPrecision(unit, -2);
    return int(number);
}
} // namespace sketchy
