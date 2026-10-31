#pragma once
#include "core/reference_image.hpp"

#include <QJsonObject>

namespace sketchy {

QJsonObject encodeReferenceImage(const ReferenceImage &image);
// A body's reference image: four keys, or five with the schema-35 georeference. `georeferenced` is
// false for a document older than schema 35, which may not carry the record.
ReferenceImage decodeReferenceImage(const QJsonObject &object, bool georeferenced = true);

} // namespace sketchy
