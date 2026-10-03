#pragma once
#include "core/model.hpp"
#include <QByteArray>
#include <QString>
namespace sketchy {
QByteArray encodeDocument(const Document &doc);
Document decodeDocument(const QByteArray &bytes);
void saveDocument(Document &doc, const QString &path);
Document loadDocument(const QString &path);
} // namespace sketchy
