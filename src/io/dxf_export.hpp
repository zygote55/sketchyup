#pragma once
#include "io/dxf_import.hpp"
namespace sketchy {
struct DxfExport {
    QByteArray bytes;
    QJsonObject report;
};
// All world-XY model edges, including hidden geometry. Nonplanar geometry rejects.
// siteCoordinates adds the document site origin back to X and Y (ADR 0168); it throws when the
// document has no site origin. Z stays unwritten, as in every planar export.
DxfExport exportDxf(const Document &document, double metresPerUnit, bool siteCoordinates = false);
void writeDxfExport(const DxfExport &result, const QString &path);
} // namespace sketchy
