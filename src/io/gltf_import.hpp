#pragma once
#include "io/document_io.hpp"
#include "io/gltf_package.hpp"
namespace sketchy {
struct GltfImport {
    Document document;
    QJsonObject report;
};
// Creates a new unsaved document from immutable captured package bytes. No source writes.
GltfImport importGltf(const GltfPackage &package);
GltfImport loadGltf(const QString &path);
} // namespace sketchy
