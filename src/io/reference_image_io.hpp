#pragma once
#include "core/reference_image.hpp"
#include <QJsonObject>
namespace sketchy {
QJsonObject encodeReferenceImage(const ReferenceImage &image);
ReferenceImage decodeReferenceImage(const QJsonObject &object);
} // namespace sketchy
