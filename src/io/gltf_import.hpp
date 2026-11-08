#pragma once
#include "core/component_records.hpp"
#include "core/document_limits.hpp"
#include "io/document_io.hpp"
#include "io/gltf_package.hpp"
#include <cstddef>
namespace sketchy {
struct GltfImport {
    Document document;
    QJsonObject report;
};
// Creates a new unsaved document from immutable captured package bytes. No source writes.
// The aggregate decoded-image budget (R115: eight times the R113 value of 256 MiB). The budget is a parameter
// so that a test reaches the refusal with a small image.
inline constexpr std::size_t gltfDecodedImageLimit = 2048ull * 1024 * 1024;
// `uniqueBounds` is the unique geometry bound the import checks (the product bound by default; a test lowers one).
// `componentDefinitionBound` is the most component definitions the import makes (R119): a mesh used by
// several nodes is one definition, and a mesh used by one node is a group. The product count by default.
GltfImport importGltf(const GltfPackage &package, std::size_t decodedImageBudget = gltfDecodedImageLimit,
                      const LimitCounts &uniqueBounds = DocumentLimits::unique,
                      std::size_t componentDefinitionBound = componentDefinitionCountLimit);
GltfImport loadGltf(const QString &path);
} // namespace sketchy
