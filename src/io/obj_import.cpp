#include "io/obj_import.hpp"
#include "core/document_limits.hpp"
#include "io/body_budget.hpp"
#include "io/format_limits.hpp"
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
// The OBJ import byte bounds (R113, D-102), eight times their R113 values in R115: a 1 GiB input
// file and 2 GiB of decoded textures. The input bound is the one the OBJ reader checks.
constexpr std::size_t objInputLimit = 1024ull * 1024 * 1024;
constexpr std::size_t objDecodedTextureLimit = 2048ull * 1024 * 1024;
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
        auto bytes = file.read(qMin(file.size(), qint64(limit)) + 1);
        if (bytes.size() > limit || file.error() != QFileDevice::NoError)
            throw std::runtime_error("Cannot capture bounded OBJ input");
        captured += size_t(bytes.size());
        if (captured > objInputLimit)
            throw std::runtime_error("OBJ package exceeds " + std::to_string(objInputLimit / (1024 * 1024)) +
                                     " MiB");
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
                throw std::runtime_error("OBJ images exceed " + std::to_string(assetTotalLimit / (1024 * 1024)) + " MiB");
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
                    if (decodedBytes > objDecodedTextureLimit)
                        throw std::runtime_error("OBJ decoded textures exceed " +
                                                 std::to_string(objDecodedTextureLimit / (1024 * 1024)) +
                                                 " MiB");
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
    // One body of a bucket. A mesh over the one-body limits (R113, D-102) is several pieces: each
    // continues the bucket in the next body, named "Name (2)" and so on.
    struct Piece {
        std::shared_ptr<Body> body;
        std::map<size_t, Id> vertices;
        std::map<Id, FaceSource> faces;
        BodyBudget budget;
    };
    struct Bucket {
        std::string name;
        Id parent{};
        QStringList groups;
        std::vector<Piece> pieces;
    };
    std::map<QString, Id> objects;
    std::map<std::pair<QString, QStringList>, Bucket> buckets;
    std::set<size_t> usedVertices;
    std::shared_ptr<Body> body(std::string name, Id parent, BodyKind kind) {
        checkFormatCount("OBJ import", "bodies", nextBody, DocumentLimits::unique.bodies,
                         "unique geometry");
        auto b = std::make_shared<Body>();
        b->id = nextBody++;
        b->parent = parent;
        b->kind = kind;
        b->name = std::move(name);
        edit.changes.push_back({b->id, nullptr, b});
        return b;
    }
    // A new piece of a bucket, continuing it in the next body.
    Piece &startPiece(Bucket &bucket) {
        auto b = body(continuedBodyName(bucket.name, bucket.pieces.size() + 1), bucket.parent,
                      BodyKind::Geometry);
        for (qsizetype i = 0; i < bucket.groups.size(); ++i)
            b->properties["obj.group." + std::to_string(i)] = bucket.groups[i].toStdString();
        bucket.pieces.push_back(Piece{b, {}, {}, {}});
        return bucket.pieces.back();
    }
    Bucket &bucket(const ObjState &state) {
        if (!objects.contains(state.object))
            objects[state.object] =
                body(state.object.isEmpty() ? "OBJ object" : state.object.toStdString(), 1,
                     BodyKind::Group)
                    ->id;
        const auto key = std::make_pair(state.object, state.groups);
        if (!buckets.contains(key)) {
            auto &created = buckets[key];
            created.name = state.groups.isEmpty() ? "Geometry"
                                                  : state.groups.join(" + ").left(256).toStdString();
            created.parent = objects.at(state.object);
            created.groups = state.groups;
            startPiece(created);
            if (state.groups.size() > 1)
                loss("overlappingGroupsCombined");
        }
        return buckets.at(key);
    }
    // The piece that takes `moreFaces` faces, the source vertices `vertices` (those it does not
    // hold yet are new), and `moreEdges` edges: the bucket's last piece when it fits under the
    // one-body limits, or a new piece.
    Piece &place(Bucket &bucket, size_t moreFaces, const std::vector<size_t> &vertices,
                 size_t moreEdges) {
        auto &last = bucket.pieces.back();
        std::set<size_t> fresh;
        for (const auto v : vertices)
            if (!last.vertices.contains(v))
                fresh.insert(v);
        if (last.budget.admits(moreFaces, fresh.size(), moreEdges))
            return last;
        return startPiece(bucket);
    }
    Id vertex(Piece &piece, size_t index) {
        usedVertices.insert(index);
        if (piece.vertices.contains(index))
            return piece.vertices.at(index);
        checkFormatCount("OBJ import", "vertices", ++vertexCount,
                         DocumentLimits::unique.vertices, "unique geometry");
        const auto id = piece.body->surface.nextId++;
        piece.body->surface.vertices[id] = source.vertices.at(index);
        piece.vertices[index] = id;
        return id;
    }
    void face(const ObjFace &sourceFace, size_t original) {
        const auto &state = source.states.at(sourceFace.state);
        std::vector<size_t> indices;
        for (const auto &corner : sourceFace.corners)
            indices.push_back(corner.vertex);
        // A face of k corners is one face, or k - 2 triangles when UV coordinates are not affine.
        auto &piece = place(bucket(state), std::max<size_t>(1, indices.size() - 2), indices,
                            indices.size());
        auto &surface = piece.body->surface;
        std::vector<Id> loop;
        FaceSource record{original, state.smoothing, {}};
        for (const auto &corner : sourceFace.corners) {
            const auto id = vertex(piece, corner.vertex);
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
        checkFormatCount("OBJ import", "faces", faceCount, DocumentLimits::unique.faces,
                         "unique geometry");
        for (const auto &[f, mapping] : faces) {
            piece.body->faceMaterials[f] = {m, m};
            if (mapping)
                piece.body->faceTextureMappings[f] = {mapping, mapping};
            piece.faces[f] = record;
        }
        piece.budget.faces = surface.faces.size();
        piece.budget.vertices = surface.vertices.size();
        piece.budget.edges += indices.size();
    }
    ObjImport run(const QString &path, ObjImportOptions options) {
        options.validate();
        const QFileInfo input(path);
        if (!input.isFile())
            throw std::runtime_error("OBJ requires a regular source file");
        root = QFileInfo(input.canonicalFilePath()).absolutePath();
        source = parseObj(read(input.canonicalFilePath(), qsizetype(objInputLimit)), options);
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
                checkFormatCount("OBJ import", "wires", ++wireCount,
                                 DocumentLimits::unique.wires, "unique geometry");
                const auto first = line.corners[i - 1].vertex, second = line.corners[i].vertex;
                auto &piece = place(b, 0, {first, second}, 1);
                const auto a = vertex(piece, first), c = vertex(piece, second);
                piece.body->surface.wires.push_back({a, c});
                piece.budget.vertices = piece.body->surface.vertices.size();
                piece.budget.edges += 1;
            }
            if (line.corners.front().texture)
                loss("lineTextureCoordinatesOmitted");
        }
        if (!faceCount && !wireCount)
            throw std::runtime_error("OBJ contains no supported faces or lines");
        size_t edgeCount{};
        for (auto &[key, b] : buckets) {
            (void)key;
            for (auto &piece : b.pieces) {
                auto &body = *piece.body;
                body.surface.validate();
                body.topology = Topology::rebuild(body.surface, {});
                edgeCount += body.topology.edges.size();
                const auto adjacency = body.topology.adjacency(body.surface);
                for (const auto &[edge, incidence] : adjacency.edgeFaces) {
                    if (incidence.size() != 2)
                        continue;
                    const auto &a = piece.faces.at(incidence[0].face),
                               &c = piece.faces.at(incidence[1].face);
                    bool smooth =
                        a.original == c.original || (a.smoothing && a.smoothing == c.smoothing);
                    const auto &e = body.topology.edges.at(edge);
                    for (auto v : {e.a, e.b})
                        if (a.normals.contains(v) && c.normals.contains(v) &&
                            a.normals.at(v) != c.normals.at(v))
                            smooth = false;
                    if (smooth)
                        body.edgeAppearances[edge].smooth = true;
                }
            }
        }
        checkFormatCount("OBJ import", "edges", edgeCount, DocumentLimits::unique.edges,
                         "unique geometry");
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
        // A mesh over the one-body limit is in more than one body. Shading does not cross the
        // split, since the bodies share no vertices (R113, D-102).
        QJsonArray bodySplits;
        for (const auto &[key, b] : buckets) {
            (void)key;
            if (b.pieces.size() > 1)
                bodySplits.append(QJsonObject{{"name", QString::fromStdString(b.name)},
                                              {"bodies", qint64(b.pieces.size())}});
        }
        result.report = {{"apiVersion", 1},
                         {"format", "OBJ"},
                         {"source", source.report},
                         {"materialLibraries", libraryReports},
                         {"faces", qint64(faceCount)},
                         {"vertices", qint64(vertexCount)},
                         {"wireSegments", qint64(wireCount)},
                         {"groups", qint64(buckets.size())},
                         {"bodySplits", bodySplits},
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
