#include "io/obj_export.hpp"
#include "core/face_textures.hpp"
#include "core/shading_normals.hpp"
#include "io/texture_image.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <algorithm>
#include <set>
#include <tuple>
namespace sketchy {
namespace {
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
QString hash(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
QByteArray number(double value) {
    require(std::isfinite(value), "OBJ export requires finite coordinates");
    return QByteArray::number(value == 0 ? 0 : value, 'g', 17);
}
QByteArray vector(Vec3 value) {
    return number(value.x) + ' ' + number(value.y) + ' ' + number(value.z);
}
QJsonObject artifact(const QByteArray &bytes) {
    return {{"bytes", qint64(bytes.size())}, {"sha256", hash(bytes)}};
}
struct Writer {
    const Document &doc;
    ObjImportOptions options;
    ObjExport result;
    QByteArray positions, coordinates, normals, elements;
    size_t vertexCount{}, faceCount{}, cornerCount{}, wireCount{}, textureBytes{}, decodedBytes{};
    QJsonObject losses;
    QJsonArray objects;
    using Triple = std::tuple<double, double, double>;
    std::map<Triple, size_t> normalIndices;
    std::map<std::pair<double, double>, size_t> uvIndices;
    using MaterialKey = std::tuple<Id, std::array<float, 3>, float, Id>;
    std::map<MaterialKey, size_t> materials;
    std::map<Id, QString> assets;
    explicit Writer(const Document &document, ObjImportOptions units)
        : doc(document), options(units) {}
    void loss(const char *key, int count = 1) { losses[key] = losses[key].toInt() + count; }
    Vec3 axis(Vec3 p) const { return options.up == ObjUpAxis::Y ? Vec3{p.x, p.z, -p.y} : p; }
    void textBudget() const {
        require(positions.size() + coordinates.size() + normals.size() + elements.size() <=
                    64 * 1024 * 1024 - 256,
                "OBJ export exceeds 64 MiB geometry text");
    }
    QString texture(Id id) {
        if (!id)
            return {};
        if (assets.contains(id))
            return assets.at(id);
        const auto &asset = *doc.assets().at(id);
        QString filename;
        if (!asset.payload)
            loss("missingTextures");
        else {
            const auto decoded = decodeTextureImage(asset);
            if (decoded.status != TextureImageStatus::Ready)
                loss("unsupportedTextures");
            else {
                decodedBytes += decoded.image->rgba().size();
                require(decodedBytes <= 256 * 1024 * 1024,
                        "OBJ export exceeds decoded texture budget");
                const auto &bytes = asset.payload->bytes();
                textureBytes += bytes.size();
                require(textureBytes <= assetTotalLimit, "OBJ export exceeds image byte budget");
                filename = "texture-" + QString::number(id) +
                           (asset.mediaType == "image/png" ? ".png" : ".jpg");
                result.textures[filename] = QByteArray(reinterpret_cast<const char *>(bytes.data()),
                                                       qsizetype(bytes.size()));
            }
        }
        assets[id] = filename;
        return filename;
    }
    std::pair<size_t, bool> material(const Body &body, Id face) {
        const auto appearance = surfaceAppearance(doc.materials(), body, face);
        const auto back = surfaceAppearance(doc.materials(), body, face, true);
        const auto asset = appearance.material ? doc.materials().at(appearance.material)->asset : 0;
        const auto file = texture(asset);
        const MaterialKey key{appearance.material, appearance.color, appearance.opacity, asset};
        if (appearance != back ||
            faceTextureMappings(body, face).front != faceTextureMappings(body, face).back)
            loss("backFaceAppearanceOmitted");
        if (!materials.contains(key)) {
            require(materials.size() < 1024, "OBJ export exceeds 1024 materials");
            const auto id = materials.size() + 1;
            materials[key] = id;
            result.mtl += "newmtl material-" + QByteArray::number(id) + "\nKd";
            for (const auto c : appearance.color) {
                const double v = c <= .04045 ? c / 12.92 : std::pow((c + .055) / 1.055, 2.4);
                result.mtl += ' ' + number(v);
            }
            result.mtl += "\nd " + number(appearance.opacity) + "\n";
            if (!file.isEmpty())
                result.mtl += "map_Kd " + file.toUtf8() + "\n";
            result.mtl += '\n';
        }
        return {materials.at(key), !file.isEmpty()};
    }
    size_t normal(Vec3 value) {
        value = normalized(axis(value));
        const Triple key{value.x, value.y, value.z};
        if (!normalIndices.contains(key)) {
            require(normalIndices.size() < 300000, "OBJ export exceeds normal count");
            const auto id = normalIndices.size() + 1;
            normalIndices[key] = id;
            normals += "vn " + vector(value) + "\n";
        }
        return normalIndices.at(key);
    }
    size_t uv(TextureCoordinate value) {
        require(std::isfinite(value.u) && std::isfinite(value.v) && std::abs(value.u) <= 1e9 &&
                    std::abs(value.v) <= 1e9,
                "OBJ export UV exceeds supported bounds");
        const auto key = std::make_pair(value.u, 1 - value.v);
        if (!uvIndices.contains(key)) {
            require(uvIndices.size() < 300000, "OBJ export exceeds UV count");
            const auto id = uvIndices.size() + 1;
            uvIndices[key] = id;
            coordinates += "vt " + number(key.first) + ' ' + number(key.second) + "\n";
        }
        return uvIndices.at(key);
    }
    void geometry(Id id, const Body &body) {
        const auto world = doc.worldTransform(id), inverse = world.inverse();
        const bool reflected = world.determinant() < 0;
        std::map<std::pair<Id, Triple>, size_t> vertexIndices;
        auto vertex = [&](Vec3 local, Id identity) {
            const auto key = std::make_pair(identity, Triple{local.x, local.y, local.z});
            if (!vertexIndices.contains(key)) {
                require(vertexCount < 100000, "OBJ export exceeds 100000 positions");
                const auto p = axis(world.point(local)) * (1 / options.metresPerUnit);
                vertexIndices[key] = ++vertexCount;
                positions += "v " + vector(p) + "\n";
            }
            return vertexIndices.at(key);
        };
        auto transformedNormal = [&](Vec3 n) {
            return Vec3{inverse.m[0] * n.x + inverse.m[1] * n.y + inverse.m[2] * n.z,
                        inverse.m[4] * n.x + inverse.m[5] * n.y + inverse.m[6] * n.z,
                        inverse.m[8] * n.x + inverse.m[9] * n.y + inverse.m[10] * n.z};
        };
        const auto label = "body-" + QByteArray::number(id);
        elements += "o " + label + "\ng " + label + "\ns 1\n";
        objects.append(QJsonObject{{"object", QString::fromLatin1(label)},
                                   {"sourceBody", QString::number(id)},
                                   {"sourceName", QString::fromStdString(body.name)}});
        ShadingNormals shading(body);
        size_t currentMaterial = 0;
        for (const auto &[faceId, face] : body.surface.faces) {
            const auto [materialId, textured] = material(body, faceId);
            const bool hasUV = textured || faceTextureMappings(body, faceId).front.has_value();
            const auto mapping = effectiveFaceTextureMapping(body, faceId);
            if (materialId != currentMaterial) {
                elements += "usemtl material-" + QByteArray::number(materialId) + "\n";
                currentMaterial = materialId;
            }
            auto polygon = [&](std::vector<Vec3> points, std::vector<Vec3> smooth,
                               std::vector<Id> identities) {
                require(++faceCount <= 100000, "OBJ export exceeds 100000 faces");
                cornerCount += points.size();
                require(cornerCount <= 500000, "OBJ export exceeds 500000 corners");
                if (reflected) {
                    std::reverse(points.begin(), points.end());
                    std::reverse(smooth.begin(), smooth.end());
                    std::reverse(identities.begin(), identities.end());
                }
                elements += 'f';
                for (size_t i = 0; i < points.size(); ++i) {
                    const auto v = vertex(points[i], identities[i]),
                               n = normal(transformedNormal(smooth[i]));
                    elements += ' ' + QByteArray::number(v) + '/';
                    if (hasUV)
                        elements += QByteArray::number(uv(mapping.coordinates(points[i])));
                    elements += '/' + QByteArray::number(n);
                }
                elements += '\n';
                textBudget();
            };
            if (face.loops.size() == 1 && face.loops[0].size() <= 256) {
                std::vector<Vec3> points, smooth;
                for (auto v : face.loops[0]) {
                    points.push_back(body.surface.vertices.at(v));
                    smooth.push_back(shading.corner(faceId, v));
                }
                polygon(std::move(points), std::move(smooth), face.loops[0]);
            } else {
                loss("facesTriangulated");
                using Grid = std::array<std::int64_t, 3>;
                auto grid = [](Vec3 p) {
                    return Grid{std::int64_t(std::floor(p.x * 1e6)),
                                std::int64_t(std::floor(p.y * 1e6)),
                                std::int64_t(std::floor(p.z * 1e6))};
                };
                std::map<Grid, std::vector<Id>> vertices;
                for (const auto &loop : face.loops)
                    for (const auto v : loop)
                        vertices[grid(body.surface.vertices.at(v))].push_back(v);
                auto original = [&](Vec3 point) {
                    const auto key = grid(point);
                    Id identity = 0;
                    double closest = 2e-7;
                    for (int x = -1; x <= 1; ++x)
                        for (int y = -1; y <= 1; ++y)
                            for (int z = -1; z <= 1; ++z) {
                                const auto found =
                                    vertices.find({key[0] + x, key[1] + y, key[2] + z});
                                if (found == vertices.end())
                                    continue;
                                for (const auto v : found->second) {
                                    const auto distance =
                                        length(body.surface.vertices.at(v) - point);
                                    if (distance < closest) {
                                        closest = distance;
                                        identity = v;
                                    }
                                }
                            }
                    require(identity != 0, "OBJ triangulation cannot resolve an original vertex");
                    return identity;
                };
                for (const auto &triangle : body.surface.triangulate(faceId)) {
                    const auto smooth = shading.triangle(triangle);
                    const std::vector<Id> ids{original(triangle.a), original(triangle.b),
                                              original(triangle.c)};
                    polygon({body.surface.vertices.at(ids[0]), body.surface.vertices.at(ids[1]),
                             body.surface.vertices.at(ids[2])},
                            {smooth[0], smooth[1], smooth[2]}, ids);
                }
            }
        }
        for (const auto &wire : body.surface.wires) {
            require(++wireCount <= 100000, "OBJ export exceeds 100000 wire segments");
            cornerCount += 2;
            require(cornerCount <= 500000, "OBJ export exceeds 500000 corners");
            const auto a = vertex(body.surface.vertices.at(wire[0]), wire[0]),
                       b = vertex(body.surface.vertices.at(wire[1]), wire[1]);
            elements += "l " + QByteArray::number(a) + ' ' + QByteArray::number(b) + "\n";
            textBudget();
        }
        if (body.textSource)
            loss("editableTextFlattened");
        if (!body.guides.empty())
            loss("guidesOmitted", int(body.guides.size()));
        if (!body.curves.empty())
            loss("curveMetadataOmitted", int(body.curves.size()));
        if (!body.edgeAppearances.empty())
            loss("edgeFlagsOmitted", int(body.edgeAppearances.size()));
    }
    ObjExport run() {
        options.validate();
        require(doc.readSnapshotBytes() <= 256 * 1024 * 1024,
                "OBJ export exceeds document snapshot budget");
        for (const auto &[id, b] : doc.bodies()) {
            if (b->kind == BodyKind::ReferenceImage) {
                loss("referenceImagesOmitted");
                continue;
            }
            if (b->kind == BodyKind::Group) {
                loss("hierarchyFlattened");
                continue;
            }
            if (!b->surface.faces.empty() || !b->surface.wires.empty())
                geometry(id, *b);
        }
        require(faceCount + wireCount > 0, "OBJ export contains no supported faces or wires");
        require(faceCount + wireCount <= 100000, "OBJ export exceeds 100000 combined elements");
        if (!doc.instances().empty())
            loss("componentInstancesExpanded", int(doc.instances().size()));
        if (!doc.annotations().empty())
            loss("annotationsOmitted", int(doc.annotations().size()));
        if (!doc.scenes().empty())
            loss("savedScenesOmitted", int(doc.scenes().size()));
        if (!doc.sections().empty())
            loss("sectionPlanesOmitted", int(doc.sections().size()));
        loss("visibilityAndMetadataOmitted");
        result.obj = "# SketchyUp OBJ; units and axis are recorded in manifest.json\nmtllib "
                     "materials.mtl\n" +
                     positions + coordinates + normals + elements;
        QJsonObject files{{"model.obj", artifact(result.obj)},
                          {"materials.mtl", artifact(result.mtl)}};
        for (const auto &[name, bytes] : result.textures)
            files[name] = artifact(bytes);
        const QJsonObject descriptions{
            {"missingTextures", "Missing images export as diffuse material colors."},
            {"unsupportedTextures", "Unsupported images export as diffuse material colors."},
            {"backFaceAppearanceOmitted",
             "Only front-face material and texture placement is exported."},
            {"facesTriangulated", "Faces with holes or more than 256 corners are triangulated."},
            {"editableTextFlattened", "Editable text becomes its current geometry."},
            {"guidesOmitted", "Construction guides are omitted."},
            {"curveMetadataOmitted", "Curves retain tessellation only."},
            {"edgeFlagsOmitted", "Native edge flags are omitted; shading normals are exported."},
            {"hierarchyFlattened",
             "World geometry is grouped by body; nested ownership is flattened."},
            {"componentInstancesExpanded",
             "Component instances become independent object geometry."},
            {"annotationsOmitted", "Native annotation overlays are omitted."},
            {"savedScenesOmitted", "Saved cameras and scenes are omitted."},
            {"sectionPlanesOmitted",
             "Section planes are omitted; full geometry is exported without clipping."},
            {"referenceImagesOmitted", "Reference-image planes are omitted."},
            {"visibilityAndMetadataOmitted",
             "All model geometry is exported; visibility, tags, history, parametric associations, "
             "native style and lighting are not represented."}};
        QJsonArray notices;
        for (auto it = losses.begin(); it != losses.end(); ++it)
            notices.append(QJsonObject{
                {"code", it.key()}, {"count", it.value()}, {"message", descriptions[it.key()]}});
        result.manifest = {{"apiVersion", 1},
                           {"format", "OBJ"},
                           {"documentId", QString::fromStdString(doc.identity())},
                           {"revision", QString::number(doc.revision())},
                           {"metresPerUnit", options.metresPerUnit},
                           {"up", options.up == ObjUpAxis::Y ? "Y" : "Z"},
                           {"geometry", "all-world-geometry"},
                           {"vertices", qint64(vertexCount)},
                           {"faces", qint64(faceCount)},
                           {"wireSegments", qint64(wireCount)},
                           {"objects", objects},
                           {"materials", qint64(materials.size())},
                           {"files", files},
                           {"losses", losses},
                           {"notices", notices}};
        require(QJsonDocument(result.manifest).toJson().size() <= 16 * 1024 * 1024,
                "OBJ manifest exceeds 16 MiB");
        return std::move(result);
    }
};
} // namespace
ObjExport exportObj(const Document &document, ObjImportOptions options) {
    return Writer(document, options).run();
}
void writeObjExport(const ObjExport &package, const QString &directory) {
    std::map<QString, QByteArray> files = package.textures;
    static const QRegularExpression textureName("^texture-[1-9][0-9]{0,19}\\.(png|jpg)$");
    require(files.size() <= 1024, "OBJ package exceeds texture count");
    size_t textureBytes{};
    for (const auto &[name, bytes] : files) {
        require(textureName.match(name).hasMatch() && !bytes.isEmpty() &&
                    bytes.size() <= qsizetype(AssetPayload::limit),
                "Invalid OBJ texture artifact");
        textureBytes += size_t(bytes.size());
    }
    require(textureBytes <= assetTotalLimit && !package.obj.isEmpty() &&
                package.obj.size() <= 64 * 1024 * 1024 && package.mtl.size() <= 4 * 1024 * 1024,
            "OBJ package exceeds byte limits");
    files["model.obj"] = package.obj;
    files["materials.mtl"] = package.mtl;
    const auto metadata = package.manifest["files"].toObject();
    require(metadata.size() == qsizetype(files.size()), "OBJ manifest file count mismatch");
    for (const auto &[name, bytes] : files)
        require(metadata[name].toObject() == artifact(bytes), "OBJ manifest artifact mismatch");
    const auto manifest = QJsonDocument(package.manifest).toJson();
    require(manifest.size() <= 16 * 1024 * 1024, "OBJ manifest exceeds limit");
    require(!directory.isEmpty(), "Choose a new OBJ package directory");
    const auto path = QFileInfo(directory).absoluteFilePath();
    require(!QFileInfo::exists(path) && !QFileInfo(path).isSymLink() && QDir().mkdir(path),
            "OBJ export requires a new directory with an existing parent");
    QDir output(path);
    try {
        require(QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                                QFileDevice::ExeOwner),
                "Cannot protect OBJ package directory");
        files["manifest.json"] = manifest;
        auto write = [&](const QString &name, const QByteArray &bytes) {
            QSaveFile file(output.filePath(name));
            require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() &&
                        file.commit(),
                    "Cannot publish OBJ package file");
        };
        for (const auto &[name, bytes] : files)
            if (name != "manifest.json")
                write(name, bytes);
        write("manifest.json", manifest);
    } catch (...) {
        for (const auto &[name, bytes] : files) {
            (void)bytes;
            QFile::remove(output.filePath(name));
        }
        QDir().rmdir(path);
        throw;
    }
}
} // namespace sketchy
