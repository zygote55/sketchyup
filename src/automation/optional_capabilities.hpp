#pragma once
#include <QJsonObject>
namespace sketchy {
// Read-only dependency presence; never execute helpers or inspect credentials.
QJsonObject optionalCapabilities();
} // namespace sketchy
