#pragma once
#include "io/obj_import.hpp"
namespace sketchy {
struct ObjExport {
    QByteArray obj, mtl;
    std::map<QString, QByteArray> textures;
    QJsonObject manifest;
};
// Exports all model geometry in world coordinates; editor visibility/sections do not clip it.
ObjExport exportObj(const Document &document, ObjImportOptions options);
// New directory only. Manifest is published last after all hash-verified package files.
void writeObjExport(const ObjExport &package, const QString &directory);
} // namespace sketchy
