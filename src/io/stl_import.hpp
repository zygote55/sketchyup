#pragma once
#include "io/document_io.hpp"
#include "io/stl_source.hpp"
namespace sketchy {
enum class StlWeld { None, Exact, Tolerance };
struct StlRepairOptions {
    StlWeld weld{StlWeld::Exact};
    double toleranceMetres{1e-7};
    bool discardDegenerate{};
    void validate() const;
};
struct StlImport {
    Document document;
    QJsonObject report;
};
// Repairs apply only to this new document and are part of its single undoable import edit.
StlImport importStl(const StlSource &source, StlRepairOptions repairs);
StlImport loadStl(const QString &path, StlCoordinateOptions coordinates, StlRepairOptions repairs);
} // namespace sketchy
