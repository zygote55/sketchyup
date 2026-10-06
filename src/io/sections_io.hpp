#pragma once
#include "core/section_records.hpp"
#include <QJsonArray>
namespace sketchy {
QJsonArray encodeSections(const SectionRecords &sections);
SectionRecords decodeSections(const QJsonValue &value, Id nextSectionId);
QJsonArray encodeActiveSections(const ActiveSections &active);
ActiveSections decodeActiveSections(const QJsonValue &value);
} // namespace sketchy
