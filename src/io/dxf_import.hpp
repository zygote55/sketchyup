#pragma once
#include "io/document_io.hpp"
#include "io/dxf_source.hpp"
namespace sketchy {
struct DxfImport {
    Document document;
    QJsonObject report;
};
// Separate unsaved document; one atomic edit; exact analytic curve records are retained.
DxfImport importDxf(const DxfSource &source, unsigned segmentsPerCircle = 96);
DxfImport loadDxf(const QString &path, DxfOptions options = {}, unsigned segmentsPerCircle = 96);
} // namespace sketchy
