#pragma once
#include "core/entity_measure.hpp"
#include <QJsonObject>
namespace sketchy {
QJsonObject entityDescription(const Document &doc, SelectedEntity entity);
} // namespace sketchy
