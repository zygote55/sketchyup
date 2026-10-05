#pragma once
#include <QJsonObject>
namespace sketchy::inspection_detail {
// Shared validator for the bounded schema vocabulary emitted by inspection registries.
void validateParameters(const QJsonObject &request, const QJsonObject &schema);
} // namespace sketchy::inspection_detail
