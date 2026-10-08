#pragma once
#include "io/stl_import.hpp"
namespace sketchy {
enum class StlEncoding { Binary, Ascii };
struct StlExport {
    QByteArray bytes;
    QJsonObject report;
};
// All world-space surface triangles, including hidden geometry; no section clipping.
StlExport exportStl(const Document &document, StlCoordinateOptions coordinates,
                    StlEncoding encoding);
// New file only; does not replace an existing path or symlink.
void writeStlExport(const StlExport &result, const QString &path);
} // namespace sketchy
