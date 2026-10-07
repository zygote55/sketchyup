#include "io/obj_import.hpp"
#include "io/obj_material.hpp"
#include "io/texture_image.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <set>
namespace sketchy {
namespace {
struct Importer {
    QString root;
    size_t captured{}, mtlBytes{}, assetSize{}, decodedBytes{}, vertexCount{}, faceCount{},
        wireCount{};
    ObjSource source;
    ObjImport result;
    Edit edit{"Import OBJ", {}};
    QJsonObject losses;
    QJsonArray libraryReports;
    struct NamedMaterial {
        ObjMaterial material;
        QString directory;
    };
    std::map<QString, NamedMaterial> definitions;
    std::map<std::pair<QString, bool>, Id> materials;
    std::map<QString, Id> images;
    std::set<std::string> materialNames;
    Id nextBody{1}, nextMaterial{1}, nextAsset{1};
    void loss(const char *key, int count = 1) { losses[key] = losses[key].toInt() + count; }
    bool contained(const QString &path) const {
        return path == root || path.startsWith(root + '/');
    }
    QString sidecar(const QString &directory, const QString &name) const {
        if (name.isEmpty() || QDir::isAbsolutePath(name) || name.contains(':') ||
            name.contains('\\') || name.contains(QChar(0)))
            throw std::runtime_error("OBJ sidecars require contained relative paths");
        const auto path = QDir::cleanPath(directory + '/' + name);
        if (!contained(path))
            throw std::runtime_error("OBJ sidecar escapes source directory");
        auto ancestor = path;
        while (!QFileInfo::exists(ancestor) && !QFileInfo(ancestor).isSymLink()) {
            const auto parent = QFileInfo(ancestor).absolutePath();
            if (parent == ancestor)
                throw std::runtime_error("Cannot resolve OBJ sidecar parent");
            ancestor = parent;
        }
        const auto canonical = QFileInfo(ancestor).canonicalFilePath();
        if (canonical.isEmpty() || !contained(canonical))
            throw std::runtime_error("OBJ sidecar symlink escapes source directory");
        return QFileInfo::exists(path) ? QFileInfo(path).canonicalFilePath() : path;
    }
    QByteArray read(const QString &path, qsizetype limit) {
        const QFileInfo info(path);
        if (!info.isFile() || info.size() > limit)
            throw std::runtime_error("OBJ input is not a bounded regular file");
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            throw std::runtime_error("Cannot read OBJ input");
        auto bytes = file.read(limit + 1);
        if (bytes.size() > limit || file.error() != QFileDevice::NoError)
            throw std::runtime_error("Cannot capture bounded OBJ input");
        captured += size_t(bytes.size());
        if (captured > 128 * 1024 * 1024)
            throw std::runtime_error("OBJ package exceeds 128 MiB");
        return bytes;
    }
    Id image(const NamedMaterial &m) {
        const auto path = sidecar(m.directory, m.material.texture.path);
        if (images.contains(path))
            return images.at(path);
        if (nextAsset > 1024)
            throw std::runtime_error("OBJ exceeds 1024 images");
        auto asset = std::make_shared<AssetRecord>();
        asset->id = nextAsset++;
        asset->name = QFileInfo(path).fileName().toStdString();
        asset->mediaType = "application/octet-stream";
        if (!QFileInfo::exists(path))
            loss("missingTextures");
        else {
            const auto bytes = read(path, AssetPayload::limit);
            assetSize += size_t(bytes.size());
            if (assetSize > assetTotalLimit)
                throw std::runtime_error("OBJ images exceed 64 MiB");
            if (bytes.startsWith(QByteArray::fromHex("89504e470d0a1a0a")))
                asset->mediaType = "image/png";
            else if (bytes.startsWith(QByteArray::fromHex("ffd8")))
                asset->mediaType = "image/jpeg";
            if (bytes.isEmpty())
                loss("unsupportedTextures");
            else {
                asset->payload = std::make_shared<AssetPayload>(
                    std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
                const auto decoded = decodeTextureImage(*asset);
                if (decoded.status == TextureImageStatus::TooLarge)
                    throw std::runtime_error("OBJ texture exceeds decoded image limits");
                if (decoded.status != TextureImageStatus::Ready)
                    loss("unsupportedTextures");
                else {
                    decodedBytes += decoded.image->rgba().size();
                    if (decodedBytes > 256 * 1024 * 1024)
                        throw std::runtime_error("OBJ decoded textures exceed 256 MiB");
                }
            }
        }
        images[path] = asset->id;
        edit.assets.push_back({asset->id, nullptr, asset});
        return asset->id;
    }
    Id material(const QString &name, bool withoutTexture) {
        const auto key = std::make_pair(name, withoutTexture);
        if (materials.contains(key))
            return materials.at(key);
        if (nextMaterial > 1024)
            throw std::runtime_error("OBJ exceeds 1024 native materials");
        auto material = std::make_shared<MaterialRecord>();
        material->id = nextMaterial++;
        material->name = name.isEmpty() ? "OBJ default" : name.toStdString();
        if (withoutTexture)
            material->name += " (without UV)";
        const auto original = material->name;
        for (size_t suffix = 1; !materialNames.insert(material->name).second; ++suffix)
            material->name = original + " (OBJ " + std::to_string(suffix) + ")";
        ObjMaterial value;
        if (definitions.contains(name)) {
            const auto &definition = definitions.at(name);
            value = definition.material;
            if (!withoutTexture && !value.texture.path.isEmpty())
                material->asset = image(definition);
        } else if (!name.isEmpty())
            loss("missingMaterials");
        for (size_t i = 0; i < 3; ++i) {
            const auto v = value.diffuse[i];
            material->color[i] = v <= .0031308 ? 12.92 * v : 1.055 * std::pow(v, 1. / 2.4) - .055;
        }
        material->opacity = value.opacity;
        materials[key] = material->id;
        edit.materials.push_back({material->id, nullptr, material});
        return material->id;
    }
    struct FaceSource {
        size_t original{};
        std::uint64_t smoothing{};
        std::map<Id, size_t> normals;
    };
    struct Bucket {
        std::shared_ptr<Body> body;
        std::map<size_t, Id> vertices;
        std::map<Id, FaceSource> faces;
    };
    std::map<QString, Id> objects;
    std::map<std::pair<QString, QStringList>, Bucket> buckets;
    std::set<size_t> usedVertices;
    std::shared_ptr<Body> body(std::string name, Id parent, BodyKind kind) {
        if (nextBody > 10000)
            throw std::runtime_error("OBJ exceeds 10000 body records");
        auto b = std::make_shared<Body>();
        b->id = nextBody++;
        b->parent = parent;
        b->kind = kind;
        b->name = std::move(name);
        edit.changes.push_back({b->id, nullptr, b});
        return b;
    }
    Bucket &bucket(const ObjState &state) {
        if (!objects.contains(state.object))
            objects[state.object] =
                body(state.object.isEmpty() ? "OBJ object" : state.object.toStdString(), 1,
                     BodyKind::Group)
                    ->id;
        const auto key = std::make_pair(state.object, state.groups);
        if (!buckets.contains(key)) {
            auto b = body(state.groups.isEmpty() ? "Geometry"
                                                 : state.groups.join(" + ").left(256).toStdString(),
                          objects.at(state.object), BodyKind::Geometry);
            for (qsizetype i = 0; i < state.groups.size(); ++i)
                b->properties["obj.group." + std::to_string(i)] = state.groups[i].toStdString();
            if (state.groups.size() > 1)
                loss("overlappingGroupsCombined");
            buckets.emplace(key, Bucket{b, {}, {}});
        }
        return buckets.at(key);
    }
    Id vertex(Bucket &bucket, size_t index) {
        usedVertices.insert(index);
        if (bucket.vertices.contains(index))
            return bucket.vertices.at(index);
        if (++vertexCount > 100000)
            throw std::runtime_error("Expanded OBJ exceeds 100000 vertices");
        const auto id = bucket.body->surface.nextId++;
        bucket.body->surface.vertices[id] = source.vertices.at(index);
        bucket.vertices[index] = id;
        return id;
    }
    void face(const ObjFace &sourceFace, size_t original) {
        const auto &state = source.states.at(sourceFace.state);
        auto &b = bucket(state);
        auto &surface = b.body->surface;
        std::vector<Id> loop;
        FaceSource record{original, state.smoothing, {}};
        for (const auto &corner : sourceFace.corners) {
            const auto id = vertex(b, corner.vertex);
            loop.push_back(id);
            if (corner.normal)
                record.normals[id] = *corner.normal;
        }
        const auto id = surface.addFaceIds({loop});
        const bool uv = sourceFace.corners.front().texture.has_value();
        const bool textured = definitions.contains(state.material) &&
                              !definitions.at(state.material).material.texture.path.isEmpty();
        if (textured && !uv)
            loss("texturedFacesWithoutUV");
        const auto m = material(state.material, textured && !uv);
        std::vector<std::pair<Id, std::optional<TextureMapping>>> faces{{id, {}}};
        if (uv) {
            const auto transform = definitions.contains(state.material)
                                       ? definitions.at(state.material).material.texture
                                       : ObjTextureReference{};
            std::map<Id, TextureCoordinate> coordinates;
            for (size_t i = 0; i < loop.size(); ++i) {
                auto v = source.textures.at(*sourceFace.corners[i].texture);
                v.u = v.u * transform.scale[0] + transform.offset[0];
                v.v = v.v * transform.scale[1] + 1 - transform.scale[1] - transform.offset[1];
                coordinates[loop[i]] = v;
            }
            const auto triangles = surface.triangulate(id);
            auto triangleIds = [&](const Triangle &t) {
                std::array<Id, 3> ids{};
                const std::array<Vec3, 3> points{t.a, t.b, t.c};
                for (size_t i = 0; i < 3; ++i) {
                    for (const auto v : loop)
                        if (length(surface.vertices.at(v) - points[i]) < tolerance) {
                            ids[i] = v;
                            break;
                        }
                    if (!ids[i])
                        throw std::runtime_error(
                            "OBJ triangulation introduced an unbound UV vertex");
                }
                return ids;
            };
            auto mapping = [&](const std::array<Id, 3> &ids) {
                return pinnedTextureMapping(
                    {surface.vertices.at(ids[0]), surface.vertices.at(ids[1]),
                     surface.vertices.at(ids[2])},
                    {coordinates.at(ids[0]), coordinates.at(ids[1]), coordinates.at(ids[2])});
            };
            const auto first = mapping(triangleIds(triangles.front()));
            bool affine = true;
            for (const auto v : loop) {
                const auto a = first.coordinates(surface.vertices.at(v)), c = coordinates.at(v);
                if (std::abs(a.u - c.u) > 1e-7 || std::abs(a.v - c.v) > 1e-7)
                    affine = false;
            }
            if (affine)
                faces.front().second = first;
            else {
                surface.faces.erase(id);
                faces.clear();
                loss("facesTriangulatedForUV");
                for (const auto &triangle : triangles) {
                    const auto ids = triangleIds(triangle);
                    const auto f = surface.addFaceIds({{ids[0], ids[1], ids[2]}});
                    faces.emplace_back(f, mapping(ids));
                }
            }
        }
        faceCount += faces.size();
        if (faceCount > 100000)
            throw std::runtime_error("Expanded OBJ exceeds 100000 faces");
        for (const auto &[f, mapping] : faces) {
            b.body->faceMaterials[f] = {m, m};
            if (mapping)
                b.body->faceTextureMappings[f] = {mapping, mapping};
            b.faces[f] = record;
        }
    }
    ObjImport run(const QString &path, ObjImportOptions options) {
        options.validate();
        const QFileInfo input(path);
        if (!input.isFile())
            throw std::runtime_error("OBJ requires a regular source file");
        root = QFileInfo(input.canonicalFilePath()).absolutePath();
        source = parseObj(read(input.canonicalFilePath(), 64 * 1024 * 1024), options);
        for (const auto &library : source.materialLibraries) {
            const auto file = sidecar(root, library);
            if (!QFileInfo::exists(file)) {
                loss("missingMaterialLibraries");
                continue;
            }
            const auto bytes = read(file, 4 * 1024 * 1024);
            mtlBytes += size_t(bytes.size());
            if (mtlBytes > 16 * 1024 * 1024)
                throw std::runtime_error("OBJ material libraries exceed 16 MiB");
            const auto parsed = parseObjMaterials(bytes);
            libraryReports.append(parsed.report);
            for (const auto &m : parsed.materials) {
                if (!m.texture.path.isEmpty())
                    (void)sidecar(QFileInfo(file).absolutePath(), m.texture.path);
                if (definitions.contains(m.name))
                    loss("duplicateMaterialDefinitions");
                else {
                    if (definitions.size() >= 1024)
                        throw std::runtime_error("OBJ exceeds 1024 material definitions");
                    definitions.emplace(m.name, NamedMaterial{m, QFileInfo(file).absolutePath()});
                }
            }
        }
        body("OBJ import", 0, BodyKind::Group);
        for (size_t i = 0; i < source.faces.size(); ++i)
            face(source.faces[i], i);
        for (const auto &line : source.lines) {
            auto &b = bucket(source.states.at(line.state));
            for (size_t i = 1; i < line.corners.size(); ++i) {
                if (++wireCount > 100000)
                    throw std::runtime_error("OBJ exceeds 100000 wire segments");
                const auto a = vertex(b, line.corners[i - 1].vertex),
                           c = vertex(b, line.corners[i].vertex);
                b.body->surface.wires.push_back({a, c});
            }
            if (line.corners.front().texture)
                loss("lineTextureCoordinatesOmitted");
        }
        if (!faceCount && !wireCount)
            throw std::runtime_error("OBJ contains no supported faces or lines");
        for (auto &[key, b] : buckets) {
            (void)key;
            b.body->surface.validate();
            b.body->topology = Topology::rebuild(b.body->surface, {});
            const auto adjacency = b.body->topology.adjacency(b.body->surface);
            for (const auto &[edge, incidence] : adjacency.edgeFaces) {
                if (incidence.size() != 2)
                    continue;
                const auto &a = b.faces.at(incidence[0].face), &c = b.faces.at(incidence[1].face);
                bool smooth =
                    a.original == c.original || (a.smoothing && a.smoothing == c.smoothing);
                const auto &e = b.body->topology.edges.at(edge);
                for (auto v : {e.a, e.b})
                    if (a.normals.contains(v) && c.normals.contains(v) &&
                        a.normals.at(v) != c.normals.at(v))
                        smooth = false;
                if (smooth)
                    b.body->edgeAppearances[edge].smooth = true;
            }
        }
        if (!source.normals.empty())
            loss("customNormalsRecomputed", int(source.normals.size()));
        if (usedVertices.size() < source.vertices.size())
            loss("unusedVerticesOmitted", int(source.vertices.size() - usedVertices.size()));
        edit.nextIdFloor = nextBody;
        edit.nextMaterialFloor = nextMaterial;
        edit.nextAssetFloor = nextAsset;
        result.document.apply(std::move(edit), result.document.revision());
        (void)decodeDocument(encodeDocument(result.document));
        const QJsonObject descriptions{
            {"missingTextures",
             "Missing textures remain explicit managed placeholders with color fallback."},
            {"unsupportedTextures",
             "Unsupported or invalid texture bytes use the material color fallback."},
            {"missingMaterials", "Undefined materials use a neutral diffuse color."},
            {"missingMaterialLibraries", "Missing MTL files use material fallbacks."},
            {"duplicateMaterialDefinitions",
             "The first material definition wins across MTL files."},
            {"overlappingGroupsCombined", "Overlapping group membership becomes one editable group "
                                          "combination, with original names retained."},
            {"texturedFacesWithoutUV",
             "Faces without UV coordinates receive an untextured material copy."},
            {"facesTriangulatedForUV",
             "Non-affine polygon UV coordinates require editable triangle faces."},
            {"lineTextureCoordinatesOmitted", "Wire texture coordinates are omitted."},
            {"customNormalsRecomputed", "Normals are recomputed; smoothing groups and indexed "
                                        "normal boundaries control hard edges."},
            {"unusedVerticesOmitted", "Vertices unused by faces or lines are omitted."}};
        QJsonArray notices;
        for (auto it = losses.begin(); it != losses.end(); ++it)
            notices.append(QJsonObject{
                {"code", it.key()}, {"count", it.value()}, {"message", descriptions[it.key()]}});
        result.report = {{"apiVersion", 1},
                         {"format", "OBJ"},
                         {"source", source.report},
                         {"materialLibraries", libraryReports},
                         {"faces", qint64(faceCount)},
                         {"vertices", qint64(vertexCount)},
                         {"wireSegments", qint64(wireCount)},
                         {"groups", qint64(buckets.size())},
                         {"materials", qint64(materials.size())},
                         {"images", qint64(images.size())},
                         {"capturedBytes", qint64(captured)},
                         {"decodedTextureBytes", qint64(decodedBytes)},
                         {"losses", losses},
                         {"notices", notices}};
        return std::move(result);
    }
};
} // namespace
ObjImport loadObj(const QString &path, ObjImportOptions options) {
    return Importer().run(path, options);
}
} // namespace sketchy
