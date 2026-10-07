#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <array>
#include <vector>
namespace sketchy {
struct ObjTextureReference {
    QString path;
    std::array<double, 2> scale{1, 1}, offset{0, 0};
};
struct ObjMaterial {
    QString name;
    std::array<double, 3> diffuse{.8, .8,
                                  .8}; // Linear reflectance; native swatches need sRGB conversion.
    double opacity{1};
    ObjTextureReference texture;
};
struct ObjMaterialLibrary {
    std::vector<ObjMaterial> materials;
    QJsonObject report;
};
// Pure bounded MTL subset: names, Kd, d/Tr and diffuse texture scale/offset.
ObjMaterialLibrary parseObjMaterials(const QByteArray &bytes);
} // namespace sketchy
