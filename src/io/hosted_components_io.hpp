#pragma once
#include "core/hosted_components.hpp"
#include <QJsonObject>
namespace sketchy {
QJsonObject encodeHostedComponents(const HostedComponents &records);
HostedPtr decodeHostedComponents(const QJsonValue &value);
} // namespace sketchy
