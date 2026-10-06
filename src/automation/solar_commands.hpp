#pragma once
#include "core/model.hpp"
#include <QJsonObject>
namespace sketchy {
QJsonObject solarSettingsSchema();
QJsonObject solarDescription(const Document &document);
} // namespace sketchy
