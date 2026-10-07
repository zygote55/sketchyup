#pragma once
#include "io/document_io.hpp"
#include "io/obj_source.hpp"
namespace sketchy {
struct ObjImport {
    Document document;
    QJsonObject report;
};
// Captures contained sidecars and builds a new unsaved document. Never writes source files.
ObjImport loadObj(const QString &path, ObjImportOptions options);
} // namespace sketchy
