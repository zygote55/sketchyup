#pragma once
#include "solar/solar.hpp"
#include <QJsonObject>
namespace sketchy {
QJsonObject encodeSolarSettings(const SolarSettings &settings);
SolarSettings decodeSolarSettings(const QJsonValue &value);
QJsonObject describeSolarPosition(const SolarSettings &settings);
} // namespace sketchy
