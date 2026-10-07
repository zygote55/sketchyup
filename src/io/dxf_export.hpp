#pragma once
#include "io/dxf_import.hpp"
namespace sketchy {
struct DxfExport {
    QByteArray bytes;
    QJsonObject report;
};
// All world-XY model edges, including hidden geometry. Nonplanar geometry rejects.
DxfExport exportDxf(const Document &document, double metresPerUnit);
void writeDxfExport(const DxfExport &result, const QString &path);
} // namespace sketchy
