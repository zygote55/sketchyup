#pragma once
#include "io/document_io.hpp"
namespace sketchy::document_io_detail {
// Internal reuse of a parsed native payload. Every record and byte limit is still validated.
Document decodeParsedDocument(const QJsonObject &root, qsizetype inputBytes,
                              const AssetPayloads &assets);
} // namespace sketchy::document_io_detail
