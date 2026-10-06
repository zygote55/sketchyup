#include "integrations/glb_export.hpp"
#include "core/shading_normals.hpp"
#include "io/assets.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QtEndian>
#include <algorithm>
#include <bit>
#include <limits>
namespace sketchy {
namespace {
constexpr qsizetype binaryLimit = 256 * 1024 * 1024, jsonLimit = 16 * 1024 * 1024;
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
QString id(Id value) { return QString::number(value); }
QString digest(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
QJsonArray point(Vec3 p) { return {p.x, p.y, p.z}; }
QJsonArray matrix(Transform transform) {
    QJsonArray result;
    for (auto value : transform.m)
        result.append(value);
    return result;
}
void word(QByteArray &bytes, quint32 value) {
    const auto little = qToLittleEndian(value);
    bytes.append(reinterpret_cast<const char *>(&little), 4);
}
void scalar(QByteArray &bytes, double value) {
    const float converted = float(value);
    require(std::isfinite(converted), "GLB attribute cannot be represented as a finite float");
    word(bytes, std::bit_cast<quint32>(converted));
}
void vector(QByteArray &bytes, Vec3 p) {
    scalar(bytes, p.x);
    scalar(bytes, p.y);
    scalar(bytes, p.z);
}
void aligned(QByteArray &bytes, char padding) {
    while (bytes.size() % 4)
        bytes.append(padding);
}
void trs(const Transform &transform) {
    transform.validate();
    const auto x = normalized(transform.vector({1, 0, 0})),
               y = normalized(transform.vector({0, 1, 0})),
               z = normalized(transform.vector({0, 0, 1}));
    require(std::abs(dot(x, y)) < 1e-7 && std::abs(dot(x, z)) < 1e-7 && std::abs(dot(y, z)) < 1e-7,
            "GLB export does not support a sheared local transform");
}
double linear(double srgb) {
    return srgb <= .04045 ? srgb / 12.92 : std::pow((srgb + .055) / 1.055, 2.4);
}
struct Bounds {
    Vec3 low{}, high{};
    bool valid{};
    void add(Vec3 p) {
        if (!valid) {
            low = high = p;
            valid = true;
            return;
        }
        low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
        high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
    }
    QJsonObject json() const {
        return valid ? QJsonObject{{"min", point(low)}, {"max", point(high)}} : QJsonObject{};
    }
};
struct Primitive {
    QByteArray positions, normals;
    Bounds bounds;
    int count{};
};
struct Writer {
    const RenderSnapshot &snapshot;
    const Document &doc;
    QByteArray binary;
    QJsonArray nodes, meshes, materials, views, accessors, bodyMap, assetMap, materialMap,
        sidePairs;
    std::map<QByteArray, int> meshCache, materialCache;
    std::map<Id, int> nodeMap;
    std::set<Id> usedAssets, usedMaterials;
    size_t faces{}, triangles{}, references{}, hiddenBodies{}, hiddenFaces{}, wires{}, guides{},
        curves{}, backDifferences{};
    Bounds bounds;
    explicit Writer(const RenderSnapshot &snapshot)
        : snapshot(snapshot), doc(snapshot.document()) {}
    int buffer(const QByteArray &data, bool attributes) {
        aligned(binary, 0);
        require(data.size() <= binaryLimit - binary.size(), "GLB binary exceeds 256 MiB");
        QJsonObject view{
            {"buffer", 0}, {"byteOffset", int(binary.size())}, {"byteLength", int(data.size())}};
        if (attributes)
            view["target"] = 34962;
        const int index = views.size();
        views.append(view);
        binary.append(data);
        return index;
    }
    int attribute(const QByteArray &data, const Bounds *bounds = nullptr) {
        QJsonObject accessor{{"bufferView", buffer(data, true)},
                             {"componentType", 5126},
                             {"count", int(data.size() / 12)},
                             {"type", "VEC3"}};
        if (bounds) {
            accessor["min"] = point(bounds->low);
            accessor["max"] = point(bounds->high);
        }
        const int index = accessors.size();
        accessors.append(accessor);
        return index;
    }
    void useMaterial(Id material) {
        if (!material || !usedMaterials.insert(material).second)
            return;
        const auto &record = *doc.materials().at(material);
        if (record.asset)
            usedAssets.insert(record.asset);
        materialMap.append(QJsonObject{
            {"id", id(material)},
            {"name", QString::fromStdString(record.name)},
            {"srgbColor", QJsonArray{record.color[0], record.color[1], record.color[2]}},
            {"opacity", record.opacity},
            {"asset", id(record.asset)}});
    }
    QJsonObject appearanceKey(const SurfaceAppearance &appearance) {
        return {{"color", QJsonArray{linear(appearance.color[0]), linear(appearance.color[1]),
                                     linear(appearance.color[2]), appearance.opacity}},
                {"id", id(appearance.material)}};
    }
    int appendMaterial(const SurfaceAppearance &appearance, bool doubleSided) {
        useMaterial(appearance.material);
        QJsonArray rgba{linear(appearance.color[0]), linear(appearance.color[1]),
                        linear(appearance.color[2]), appearance.opacity};
        require(materials.size() < 4096, "GLB has too many distinct face appearances");
        const int index = materials.size();
        materials.append(QJsonObject{
            {"name", appearance.material
                         ? QString::fromStdString(doc.materials().at(appearance.material)->name)
                         : "Face color"},
            {"pbrMetallicRoughness", QJsonObject{{"baseColorFactor", rgba},
                                                 {"metallicFactor", 0},
                                                 {"roughnessFactor", .8}}},
            {"doubleSided", doubleSided},
            {"alphaMode", appearance.opacity < 1 ? "BLEND" : "OPAQUE"},
            {"extras", QJsonObject{{"sketchyupMaterial", id(appearance.material)},
                                   {"sketchyupAppearance", index}}}});
        return index;
    }
    int material(const SurfaceAppearance &front, const SurfaceAppearance &back) {
        const bool paired = front != back;
        const auto key = QJsonDocument(QJsonObject{{"front", appearanceKey(front)},
                                                   {"back", appearanceKey(back)}})
                             .toJson(QJsonDocument::Compact);
        if (materialCache.contains(key))
            return materialCache.at(key);
        const int index = appendMaterial(front, !paired);
        if (paired) {
            const int reverse = appendMaterial(back, false);
            sidePairs.append(QJsonObject{{"front", index}, {"back", reverse}});
        }
        materialCache[key] = index;
        return index;
    }
    std::pair<int, QJsonArray> mesh(Id owner, const Body &body) {
        std::map<int, Primitive> groups;
        QJsonArray faceMap;
        const auto world = doc.worldTransform(owner);
        const ShadingNormals shading(body);
        for (const auto &[face, record] : body.surface.faces) {
            if (!snapshot.visible(owner, face)) {
                ++hiddenFaces;
                continue;
            }
            require(++faces <= 100000, "GLB has too many visible faces");
            for (const auto &loop : record.loops) {
                references += loop.size();
                require(references <= 2000000, "GLB has too many face vertices");
            }
            const auto front = surfaceAppearance(doc.materials(), body, face),
                       back = surfaceAppearance(doc.materials(), body, face, true);
            useMaterial(back.material);
            if (front != back)
                ++backDifferences;
            const int appearance = material(front, back);
            const auto tessellated = body.surface.triangulate(face);
            const bool paired = front != back;
            triangles += tessellated.size() * (paired ? 2 : 1);
            require(triangles <= 1000000, "GLB has too many visible triangles");
            auto appendSide = [&](int index, bool reverse) {
                auto &group = groups[index];
                const int start = group.count;
                for (const auto &triangle : tessellated) {
                    const auto normals = shading.triangle(triangle);
                    const std::array<Vec3, 3> points{triangle.a, triangle.b, triangle.c};
                    for (const auto corner : {0, reverse ? 2 : 1, reverse ? 1 : 2}) {
                        const auto p = points[corner];
                        vector(group.positions, p);
                        vector(group.normals, normals[corner] * (reverse ? -1 : 1));
                        ++group.count;
                        group.bounds.add(
                            {double(float(p.x)), double(float(p.y)), double(float(p.z))});
                        bounds.add(world.point(p));
                    }
                }
                return start;
            };
            QJsonObject entry{{"id", id(face)},
                              {"material", appearance},
                              {"firstVertex", appendSide(appearance, false)},
                              {"vertexCount", int(tessellated.size() * 3)},
                              {"frontMaterial", id(front.material)},
                              {"backMaterial", id(back.material)}};
            if (paired) {
                entry["backAppearance"] = appearance + 1;
                entry["backFirstVertex"] = appendSide(appearance + 1, true);
                entry["backVertexCount"] = int(tessellated.size() * 3);
            }
            faceMap.append(entry);
        }
        if (groups.empty())
            return {-1, faceMap};
        QCryptographicHash hash(QCryptographicHash::Sha256);
        for (const auto &[appearance, group] : groups) {
            QByteArray header;
            word(header, quint32(appearance));
            word(header, quint32(group.count));
            hash.addData(header);
            hash.addData(group.positions);
            hash.addData(group.normals);
        }
        const auto key = hash.result();
        std::map<int, int> primitiveIndices;
        int primitive{};
        for (const auto &[appearance, _] : groups)
            primitiveIndices[appearance] = primitive++;
        for (qsizetype i = 0; i < faceMap.size(); ++i) {
            auto record = faceMap[i].toObject();
            record["primitive"] = primitiveIndices.at(record["material"].toInt());
            if (record.contains("backAppearance"))
                record["backPrimitive"] = primitiveIndices.at(record["backAppearance"].toInt());
            faceMap[i] = record;
        }
        if (meshCache.contains(key))
            return {meshCache.at(key), faceMap};
        QJsonArray primitives;
        for (const auto &[appearance, group] : groups) {
            require(group.count > 0, "Visible face produced no GLB triangles");
            primitives.append(QJsonObject{
                {"attributes", QJsonObject{{"POSITION", attribute(group.positions, &group.bounds)},
                                           {"NORMAL", attribute(group.normals)}}},
                {"material", appearance},
                {"mode", 4}});
        }
        const int index = meshes.size();
        meshes.append(QJsonObject{{"primitives", primitives}});
        meshCache[key] = index;
        return {index, faceMap};
    }
    GlbExport run() {
        // A proper rotation converts native Z-up metres into glTF Y-up metres.
        Transform basis;
        basis.m = {1, 0, 0, 0, 0, 0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1};
        nodes.append(QJsonObject{{"name", "SketchyUp basis"}, {"matrix", matrix(basis)}});
        for (const auto &[owner, record] : doc.bodies()) {
            if (!snapshot.visible(owner)) {
                ++hiddenBodies;
                continue;
            }
            trs(record->transform);
            nodeMap[owner] = nodes.size();
            const auto [geometry, faceMap] = mesh(owner, *record);
            QJsonObject node{{"name", QString::fromStdString(record->name)},
                             {"matrix", matrix(record->transform)},
                             {"extras", QJsonObject{{"sketchyupBody", id(owner)}}}};
            if (geometry >= 0)
                node["mesh"] = geometry;
            nodes.append(node);
            QJsonObject entry{{"body", id(owner)},
                              {"parent", id(record->parent)},
                              {"node", nodeMap.at(owner)},
                              {"kind", record->kind == BodyKind::Group ? "group" : "geometry"},
                              {"faces", faceMap}};
            if (geometry >= 0)
                entry["mesh"] = geometry;
            if (doc.instances().contains(owner)) {
                const auto &instance = *doc.instances().at(owner);
                QJsonObject members;
                for (auto [canonical, scene] : instance.members)
                    members[id(canonical)] = id(scene);
                entry["instance"] =
                    QJsonObject{{"definition", id(instance.definition)}, {"members", members}};
            }
            bodyMap.append(entry);
            wires += record->surface.wires.size();
            guides += record->guides.size();
            curves += record->curves.size();
        }
        require(triangles > 0, "No visible surface geometry to export");
        std::map<int, QJsonArray> children;
        for (const auto &[owner, index] : nodeMap) {
            const Id parent = doc.bodies().at(owner)->parent;
            children[parent ? nodeMap.at(parent) : 0].append(index);
        }
        const auto &camera = snapshot.camera();
        const auto &settings = snapshot.settings();
        const double aspect = double(settings.width) / settings.height;
        QJsonObject projection{{"znear", camera.nearClip}, {"zfar", camera.farClip}};
        if (camera.orthographic) {
            projection["xmag"] = camera.yMag * aspect;
            projection["ymag"] = camera.yMag;
        } else {
            projection["yfov"] = camera.verticalFov;
            projection["aspectRatio"] = aspect;
        }
        const int cameraNode = nodes.size();
        nodes.append(QJsonObject{{"name", "Render camera"},
                                 {"camera", 0},
                                 {"matrix", matrix(renderCameraTransform(camera))}});
        children[0].append(cameraNode);
        for (const auto &[index, list] : children) {
            auto node = nodes[index].toObject();
            node["children"] = list;
            nodes[index] = node;
        }
        size_t missingAssets{};
        for (auto asset : usedAssets) {
            const auto &record = *doc.assets().at(asset);
            QJsonObject entry{{"id", id(asset)},
                              {"name", QString::fromStdString(record.name)},
                              {"mediaType", QString::fromStdString(record.mediaType)},
                              {"missing", !record.payload}};
            if (record.payload) {
                const auto bytes = assetByteArray(record.payload);
                const int view = buffer(bytes, false);
                entry["bufferView"] = view;
                entry["byteOffset"] = views[view].toObject()["byteOffset"];
                entry["byteLength"] = bytes.size();
                entry["sha256"] = digest(bytes);
            } else
                ++missingAssets;
            assetMap.append(entry);
        }
        QJsonObject losses{{"wiresOmitted", double(wires)},
                           {"guidesOmitted", double(guides)},
                           {"analyticCurvesTessellatedOrOmitted", double(curves)},
                           {"textureAssetsPreservedWithoutUVMapping", int(usedAssets.size())},
                           {"missingAssets", double(missingAssets)}};
        QJsonObject gltf{
            {"asset", QJsonObject{{"version", "2.0"}, {"generator", "SketchyUp GLB snapshot v1"}}},
            {"scene", 0},
            {"scenes", QJsonArray{QJsonObject{{"nodes", QJsonArray{0}}}}},
            {"nodes", nodes},
            {"meshes", meshes},
            {"materials", materials},
            {"bufferViews", views},
            {"accessors", accessors},
            {"buffers", QJsonArray{QJsonObject{{"byteLength", binary.size()}}}},
            {"cameras", QJsonArray{QJsonObject{
                            {"type", camera.orthographic ? "orthographic" : "perspective"},
                            {camera.orthographic ? "orthographic" : "perspective", projection}}}},
            {"extras", QJsonObject{{"sketchyupDocument", QString::fromStdString(doc.identity())},
                                   {"sketchyupRevision", id(doc.revision())},
                                   {"managedAssets", assetMap},
                                   {"sketchyupSidedMaterials",
                                    QJsonObject{{"version", 1}, {"pairs", sidePairs}}},
                                   {"losses", losses}}}};
        auto json = QJsonDocument(gltf).toJson(QJsonDocument::Compact);
        require(json.size() <= jsonLimit, "GLB JSON exceeds 16 MiB");
        aligned(json, ' ');
        aligned(binary, 0);
        require(json.size() + binary.size() + 28 <= binaryLimit, "GLB export exceeds 256 MiB");
        QByteArray glb;
        glb.reserve(json.size() + binary.size() + 28);
        word(glb, 0x46546c67);
        word(glb, 2);
        word(glb, quint32(json.size() + binary.size() + 28));
        word(glb, quint32(json.size()));
        word(glb, 0x4e4f534a);
        glb.append(json);
        word(glb, quint32(binary.size()));
        word(glb, 0x004e4942);
        glb.append(binary);
        QJsonObject manifest{
            {"apiVersion", 1},
            {"adapter", "sketchyup-glb-v1"},
            {"documentId", QString::fromStdString(doc.identity())},
            {"revision", id(doc.revision())},
            {"scene",
             QJsonObject{{"file", "scene.glb"}, {"bytes", glb.size()}, {"sha256", digest(glb)}}},
            {"nativeUp", "Z"},
            {"exportUp", "Y"},
            {"units", "m"},
            {"nativeToGltf", matrix(basis)},
            {"settings", describeRenderSettings(settings)},
            {"camera", describeRenderCamera(camera)},
            {"cameraNode", cameraNode},
            {"nativeBounds", bounds.json()},
            {"bodies", bodyMap},
            {"materials", materialMap},
            {"assets", assetMap},
            {"losses", losses},
            {"hiddenBodiesOmitted", double(hiddenBodies)},
            {"hiddenFacesOmitted", double(hiddenFaces)},
            {"visibleTriangles", double(triangles)},
            {"facesWithDistinctSides", double(backDifferences)},
            {"uniqueMeshes", meshes.size()},
            {"colorPolicy", "sRGB face colors converted to linear PBR baseColorFactor"}};
        require(QJsonDocument(manifest).toJson(QJsonDocument::Compact).size() <= jsonLimit,
                "GLB manifest exceeds 16 MiB");
        return {std::move(glb), std::move(manifest)};
    }
};
} // namespace
GlbExport exportGlb(const RenderSnapshot &snapshot) { return Writer(snapshot).run(); }
void writeGlbExport(const GlbExport &scene, const QString &directory) {
    require(!directory.isEmpty(), "Choose an export directory");
    const auto manifest = QJsonDocument(scene.manifest).toJson();
    require(scene.glb.size() <= binaryLimit && manifest.size() <= jsonLimit,
            "Export artifacts exceed bounds");
    const auto identity = scene.manifest.value("scene").toObject();
    require(identity.value("file") == "scene.glb" &&
                identity.value("bytes").toInteger() == scene.glb.size() &&
                identity.value("sha256") == digest(scene.glb),
            "Export manifest does not match GLB bytes");
    const QString path = QFileInfo(directory).absoluteFilePath();
    require(!QFileInfo::exists(path) && !QFileInfo(path).isSymLink() && QDir().mkdir(path),
            "Export requires a new directory with an existing parent");
    QDir output(path);
    try {
        require(QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                                QFileDevice::ExeOwner),
                "Cannot protect export directory");
        auto write = [&](const char *name, const QByteArray &bytes) {
            QSaveFile file(output.filePath(name));
            require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() &&
                        file.commit(),
                    "Cannot write complete export artifact");
        };
        write("scene.glb", scene.glb);
        write("manifest.json", manifest);
    } catch (...) {
        QFile::remove(output.filePath("scene.glb"));
        QFile::remove(output.filePath("manifest.json"));
        QDir().rmdir(path);
        throw;
    }
}
} // namespace sketchy
