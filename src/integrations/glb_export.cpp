#include "io/solar_io.hpp"
#include "integrations/glb_export.hpp"
#include "core/face_textures.hpp"
#include "core/shading_normals.hpp"
#include "core/section_records.hpp"
#include "io/assets.hpp"
#include "io/texture_image.hpp"
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
    QByteArray positions, normals, coordinates;
    Bounds bounds;
    int count{};
};
struct ImageExport {
    TextureImageStatus status{TextureImageStatus::Missing};
    int texture{-1}, width{}, height{};
    bool transparent{};
};
struct Writer {
    const RenderSnapshot &snapshot;
    const Document &doc;
    QByteArray binary;
    QJsonArray nodes, meshes, materials, views, accessors, bodyMap, assetMap, materialMap,
        sidePairs, images, textures, imageMap;
    std::map<Id, ImageExport> imageCache;
    size_t decodedImageBytes{};
    std::map<QByteArray, int> meshCache, materialCache;
    std::map<Id, int> nodeMap;
    std::set<Id> usedAssets, usedMaterials;
    size_t faces{}, triangles{}, references{}, hiddenBodies{}, hiddenFaces{}, wires{}, guides{},
        curves{}, backDifferences{}, cutEdges{}, capTriangles{}, textSources{}, referenceImages{};
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
    int attribute(const QByteArray &data, const Bounds *bounds = nullptr, int dimensions = 3) {
        QJsonObject accessor{{"bufferView", buffer(data, true)},
                             {"componentType", 5126},
                             {"count", int(data.size() / (4 * dimensions))},
                             {"type", dimensions == 2 ? "VEC2" : "VEC3"}};
        if (bounds) {
            accessor["min"] = point(bounds->low);
            accessor["max"] = point(bounds->high);
        }
        const int index = accessors.size();
        accessors.append(accessor);
        return index;
    }
    ImageExport imageFor(const SurfaceAppearance &appearance) {
        if (!appearance.material)
            return {};
        const auto asset = doc.materials().at(appearance.material)->asset;
        if (!asset)
            return {};
        if (imageCache.contains(asset))
            return imageCache.at(asset);
        const auto &record = *doc.assets().at(asset);
        const auto decoded = decodeTextureImage(record);
        ImageExport result{decoded.status};
        QJsonObject entry{{"asset", id(asset)},
                          {"status", QString::fromUtf8(textureImageStatusName(decoded.status))}};
        if (decoded.image) {
            const auto &image = *decoded.image;
            require(image.rgba().size() <= size_t(binaryLimit) - decodedImageBytes,
                    "GLB decoded texture images exceed 256 MiB");
            decodedImageBytes += image.rgba().size();
            const auto png = encodeTexturePng(image);
            result.texture = textures.size();
            result.width = image.width();
            result.height = image.height();
            result.transparent = image.hasTransparency();
            const int source = images.size();
            images.append(QJsonObject{{"name", QString::fromStdString(record.name)},
                                      {"mimeType", "image/png"},
                                      {"bufferView", buffer(png, false)},
                                      {"extras", QJsonObject{{"sketchyupAsset", id(asset)}}}});
            textures.append(QJsonObject{{"sampler", 0}, {"source", source}});
            entry["texture"] = result.texture;
            entry["width"] = result.width;
            entry["height"] = result.height;
            entry["hasTransparency"] = result.transparent;
            entry["normalizedSha256"] = digest(png);
        }
        imageMap.append(entry);
        imageCache[asset] = result;
        return result;
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
        const auto image = imageFor(appearance);
        QJsonObject pbr{{"baseColorFactor", rgba}, {"metallicFactor", 0}, {"roughnessFactor", .8}};
        if (image.texture >= 0)
            pbr["baseColorTexture"] = QJsonObject{{"index", image.texture}, {"texCoord", 0}};
        const int index = materials.size();
        materials.append(QJsonObject{
            {"name", appearance.material
                         ? QString::fromStdString(doc.materials().at(appearance.material)->name)
                         : "Face color"},
            {"pbrMetallicRoughness", pbr},
            {"doubleSided", doubleSided},
            {"alphaMode", appearance.opacity < 1 || image.transparent ? "BLEND" : "OPAQUE"},
            {"extras", QJsonObject{{"sketchyupMaterial", id(appearance.material)},
                                   {"sketchyupAppearance", index}}}});
        return index;
    }
    int material(const SurfaceAppearance &front, const SurfaceAppearance &back, bool paired) {
        const auto key = QJsonDocument(QJsonObject{{"paired", paired},
                                                   {"front", appearanceKey(front)},
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
    std::tuple<int, QJsonArray, QJsonObject> mesh(Id owner, const Body &body) {
        std::map<int, Primitive> groups;
        QJsonArray faceMap;
        const auto world = doc.worldTransform(owner);
        const ShadingNormals shading(body);
        const auto cuts = effectiveSectionCuts(doc, owner);
        std::vector<Triangle> source;
        std::map<Id, std::vector<SectionTriangle>> retained;
        SectionMesh section;
        QJsonObject sectionInfo;
        QJsonArray caps;
        if (!cuts.empty()) {
            std::vector<Triangle> worldSource;
            for (const auto &[face, record] : body.surface.faces)
                if (snapshot.visible(owner, face)) {
                    const auto triangles = body.surface.triangulate(face);
                    require(triangles.size() <= sectionInputTriangleLimit - source.size(),
                            "Section export exceeds input triangle budget");
                    for (const auto &triangle : triangles) {
                        source.push_back(triangle);
                        worldSource.push_back({world.point(triangle.a), world.point(triangle.b),
                                               world.point(triangle.c), face});
                    }
                }
            // Clip in the same world coordinates as the viewport, then return derived
            // positions to the existing local-node frame. Provenance stays unchanged.
            section = sectionMesh(worldSource, cuts);
            const auto inverse = world.inverse();
            for (auto &triangle : section.triangles) {
                for (auto &vertex : triangle.vertices)
                    vertex.point = inverse.point(vertex.point);
                if (triangle.source != noSectionSource)
                    retained[triangle.face].push_back(triangle);
            }
            QJsonArray planes, unfilled;
            for (const auto &cut : cuts)
                planes.append(id(cut.id));
            for (auto plane : section.unfilledSections)
                unfilled.append(id(plane));
            size_t displayedEdges{};
            for (const auto &edge : section.edges)
                if (doc.sections().at(edge.section)->edges)
                    ++displayedEdges;
            cutEdges += displayedEdges;
            sectionInfo = {{"active", planes}, {"unfilled", unfilled},
                           {"cutEdgesOmitted", double(displayedEdges)}};
        }
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
            if (!cuts.empty() && !retained.contains(face))
                continue;
            const auto front = surfaceAppearance(doc.materials(), body, face),
                       back = surfaceAppearance(doc.materials(), body, face, true);
            useMaterial(back.material);
            const auto frontImage = imageFor(front), backImage = imageFor(back);
            const bool textured = frontImage.texture >= 0 || backImage.texture >= 0;
            const auto frontMapping =
                           textured ? effectiveFaceTextureMapping(body, face) : TextureMapping{},
                       backMapping = textured ? effectiveFaceTextureMapping(body, face, true)
                                              : TextureMapping{};
            const bool paired = front != back || (textured && frontMapping != backMapping);
            if (paired)
                ++backDifferences;
            const int appearance = material(front, back, paired);
            const auto tessellated = cuts.empty() ? body.surface.triangulate(face)
                                                  : std::vector<Triangle>{};
            const auto count = cuts.empty() ? tessellated.size() : retained.at(face).size();
            triangles += count * (paired ? 2 : 1);
            require(triangles <= 1000000, "GLB has too many visible triangles");
            auto appendSide = [&](int index, bool reverse) {
                auto &group = groups[index];
                const int start = group.count;
                const auto &image = reverse ? backImage : frontImage;
                const auto &mapping = reverse ? backMapping : frontMapping;
                for (size_t t = 0; t < count; ++t) {
                    const auto *derived = cuts.empty() ? nullptr : &retained.at(face)[t];
                    const auto &triangle = derived ? source.at(derived->source) : tessellated[t];
                    const auto normals = shading.triangle(triangle);
                    const std::array<Vec3, 3> points{triangle.a, triangle.b, triangle.c};
                    std::array<std::array<float, 2>, 3> uv{};
                    if (image.texture >= 0)
                        uv = floatTextureCoordinates(
                            {mapping.coordinates(points[0]), mapping.coordinates(points[1]),
                             mapping.coordinates(points[2])},
                            {1. / (64 * image.width), 1. / (64 * image.height)});
                    for (const auto corner : {0, reverse ? 2 : 1, reverse ? 1 : 2}) {
                        const auto p = derived ? derived->vertices[corner].point : points[corner];
                        auto normal = normals[corner];
                        std::array<double, 2> coordinate{uv[corner][0], uv[corner][1]};
                        if (derived) {
                            normal = {};
                            coordinate = {};
                            for (size_t k = 0; k < 3; ++k) {
                                const auto weight = derived->vertices[corner].weights[k];
                                normal = normal + normals[k] * weight;
                                coordinate[0] += uv[k][0] * weight;
                                coordinate[1] += uv[k][1] * weight;
                            }
                            normal = normalized(normal);
                        }
                        vector(group.positions, p);
                        vector(group.normals, normal * (reverse ? -1 : 1));
                        if (image.texture >= 0) {
                            scalar(group.coordinates, coordinate[0]);
                            scalar(group.coordinates, coordinate[1]);
                        }
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
                              {"vertexCount", int(count * 3)},
                              {"frontMaterial", id(front.material)},
                              {"backMaterial", id(back.material)}};
            if (paired) {
                entry["backAppearance"] = appearance + 1;
                entry["backFirstVertex"] = appendSide(appearance + 1, true);
                entry["backVertexCount"] = int(count * 3);
            }
            faceMap.append(entry);
        }
        for (const auto &cut : cuts) {
            const auto &record = *doc.sections().at(cut.id);
            if (!record.fill)
                continue;
            const auto normal = cut.plane.transformed(world.inverse()).normal * -1;
            int appearance = -1, first{}, count{};
            for (const auto &triangle : section.triangles) {
                if (triangle.section != cut.id || triangle.source != noSectionSource)
                    continue;
                if (appearance < 0) {
                    const SurfaceAppearance color{record.color, 1, 0};
                    appearance = material(color, color, false);
                    first = groups[appearance].count;
                }
                require(++triangles <= 1000000, "GLB has too many visible triangles");
                ++capTriangles;
                auto &group = groups[appearance];
                std::array<Vec3, 3> points{triangle.vertices[0].point, triangle.vertices[1].point,
                                         triangle.vertices[2].point};
                if (dot(cross(points[1] - points[0], points[2] - points[0]), normal) < 0)
                    std::swap(points[1], points[2]);
                for (const auto p : points) {
                    vector(group.positions, p);
                    vector(group.normals, normal);
                    ++group.count;
                    ++count;
                    group.bounds.add({double(float(p.x)), double(float(p.y)), double(float(p.z))});
                    bounds.add(world.point(p));
                }
            }
            if (count)
                caps.append(QJsonObject{{"section", id(cut.id)}, {"material", appearance},
                                         {"firstVertex", first}, {"vertexCount", count}});
        }
        if (!cuts.empty())
            sectionInfo["caps"] = caps;
        if (groups.empty())
            return {-1, faceMap, sectionInfo};
        QCryptographicHash hash(QCryptographicHash::Sha256);
        for (const auto &[appearance, group] : groups) {
            QByteArray header;
            word(header, quint32(appearance));
            word(header, quint32(group.count));
            hash.addData(header);
            hash.addData(group.positions);
            hash.addData(group.normals);
            hash.addData(group.coordinates);
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
        for (qsizetype i = 0; i < caps.size(); ++i) {
            auto entry = caps[i].toObject();
            entry["primitive"] = primitiveIndices.at(entry["material"].toInt());
            caps[i] = entry;
        }
        if (!cuts.empty())
            sectionInfo["caps"] = caps;
        if (meshCache.contains(key))
            return {meshCache.at(key), faceMap, sectionInfo};
        QJsonArray primitives;
        for (const auto &[appearance, group] : groups) {
            require(group.count > 0, "Visible face produced no GLB triangles");
            QJsonObject attributes{{"POSITION", attribute(group.positions, &group.bounds)},
                                   {"NORMAL", attribute(group.normals)}};
            if (!group.coordinates.isEmpty()) {
                require(group.coordinates.size() == group.count * 8,
                        "GLB texture coordinate count mismatch");
                attributes["TEXCOORD_0"] = attribute(group.coordinates, nullptr, 2);
            }
            primitives.append(
                QJsonObject{{"attributes", attributes}, {"material", appearance}, {"mode", 4}});
        }
        const int index = meshes.size();
        meshes.append(QJsonObject{{"primitives", primitives}});
        meshCache[key] = index;
        return {index, faceMap, sectionInfo};
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
            const auto [geometry, faceMap, sectionInfo] = mesh(owner, *record);
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
            if (!sectionInfo.isEmpty())
                entry["sections"] = sectionInfo;
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
            textSources += record->textSource.has_value();
            referenceImages += record->referenceImage.has_value();
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
        QJsonObject losses{
            {"wiresOmitted", double(wires)},
            {"guidesOmitted", double(guides)},
            {"sectionCutEdgesOmitted", double(cutEdges)},
            {"annotationsOmitted", double(doc.annotations().size())},
            {"editableTextSourcesOmitted", double(textSources)},
            {"referenceImagesOmitted", double(referenceImages)},
            {"solarLightingOmitted", doc.solar().enabled ? 1 : 0},
            {"analyticCurvesTessellatedOrOmitted", double(curves)},
            {"textureAssetsPreservedWithoutUVMapping", int(usedAssets.size()) - textures.size()},
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
                                    QJsonObject{{"version", textures.isEmpty() ? 1 : 2},
                                                {"pairs", sidePairs}}},
                                   {"losses", losses}}}};
        if (!textures.isEmpty()) {
            gltf["images"] = images;
            gltf["textures"] = textures;
            gltf["samplers"] = QJsonArray{QJsonObject{
                {"magFilter", 9729}, {"minFilter", 9729}, {"wrapS", 10497}, {"wrapT", 10497}}};
        }
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
            {"solar", encodeSolarSettings(doc.solar())},
            {"solarPosition", describeSolarPosition(doc.solar())},
            {"camera", describeRenderCamera(camera)},
            {"cameraNode", cameraNode},
            {"nativeBounds", bounds.json()},
            {"bodies", bodyMap},
            {"materials", materialMap},
            {"assets", assetMap},
            {"textureImages", imageMap},
            {"decodedTextureBytes", double(decodedImageBytes)},
            {"losses", losses},
            {"hiddenBodiesOmitted", double(hiddenBodies)},
            {"hiddenFacesOmitted", double(hiddenFaces)},
            {"visibleTriangles", double(triangles)},
            {"sectionCapTriangles", double(capTriangles)},
            {"facesWithDistinctSides", double(backDifferences)},
            {"uniqueMeshes", meshes.size()},
            {"colorPolicy", "Linear PBR swatch factor times normalized sRGB straight-alpha image"}};
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
