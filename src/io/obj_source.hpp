#pragma once
#include "core/texture_mapping.hpp"
#include <QByteArray>
#include <QJsonObject>
#include <QStringList>
#include <optional>
namespace sketchy {
enum class ObjUpAxis { Y, Z };
struct ObjImportOptions {
    double metresPerUnit{1};
    ObjUpAxis up{ObjUpAxis::Y};
    void validate() const;
};
struct ObjCorner {
    size_t vertex{};
    std::optional<size_t> texture, normal;
};
struct ObjState {
    QString object;
    QStringList groups;
    QString material;
    std::uint64_t smoothing{};
};
struct ObjFace {
    std::vector<ObjCorner> corners;
    size_t state{}, line{};
};
struct ObjLine {
    std::vector<ObjCorner> corners;
    size_t state{}, line{};
};
struct ObjSource {
    std::vector<Vec3> vertices, normals;
    // Converted to native top-left texture coordinates; no image is loaded here.
    std::vector<TextureCoordinate> textures;
    std::vector<ObjState> states;
    std::vector<ObjFace> faces;
    std::vector<ObjLine> lines;
    QStringList materialLibraries;
    QJsonObject report;
};
// Pure bounded text parsing. File directives are retained or rejected; never executed.
ObjSource parseObj(const QByteArray &bytes, ObjImportOptions options);
} // namespace sketchy
