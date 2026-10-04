#pragma once
#include "io/document_io.hpp"
namespace sketchy {
struct FormlineImport {
    Document document;
    QJsonObject report;
};
// Read-only source conversion. The result is a new unsaved document with one import edit.
FormlineImport importFormline(const QByteArray &bytes);
FormlineImport loadFormline(const QString &path);
} // namespace sketchy
