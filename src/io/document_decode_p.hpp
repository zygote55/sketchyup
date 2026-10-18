#pragma once
#include "io/document_io.hpp"
namespace sketchy::document_io_detail {
// Internal reuse of a parsed native payload. Every record and byte limit is still validated.
// `terrainMesh` is the packaged terrain-mesh chunk (schema 29), or null.
Document decodeParsedDocument(const QJsonObject &root, qsizetype inputBytes,
                              const AssetPayloads &assets,
                              const QByteArray *terrainMesh = nullptr);
} // namespace sketchy::document_io_detail
