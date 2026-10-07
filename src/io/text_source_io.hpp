#pragma once
#include "core/text_source.hpp"
#include <QJsonObject>
namespace sketchy {
QJsonObject encodeTextSource(const TextSource &source);
TextSource decodeTextSource(const QJsonObject &object);
} // namespace sketchy
