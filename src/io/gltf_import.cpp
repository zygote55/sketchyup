#include "io/gltf_import.hpp"
#include "io/texture_image.hpp"
#include <QJsonArray>
#include <algorithm>
#include <cgltf.h>
#include <numbers>
#include <set>
namespace sketchy {
namespace {
const Transform basis{{1, 0, 0, 0, 0, 0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1}};
std::string name(const char *source, const std::string &fallback) {
    auto text = QString::fromUtf8(source ? source : "").trimmed();
    for (auto &c : text)
        if (c.unicode() < 32 || c.unicode() == 127) c = QChar(' ');
    if (text.isEmpty()) return fallback;
    if (text.toUtf8().size() > 512) throw std::runtime_error("glTF name exceeds 512 UTF-8 bytes");
    return text.toStdString();
}
const cgltf_accessor *attribute(const cgltf_primitive &p, cgltf_attribute_type type, int index = 0) {
    const cgltf_accessor *found = nullptr;
    for (size_t i = 0; i < p.attributes_count; ++i)
        if (p.attributes[i].type == type && p.attributes[i].index == index) {
            if (found) throw std::runtime_error("Duplicated glTF vertex attribute");
            found = p.attributes[i].data;
        }
    return found;
}
std::array<float, 4> values(const cgltf_accessor &a, size_t i, cgltf_type type) {
    std::array<float, 4> result{};
    if (a.type != type || !cgltf_accessor_read_float(&a, i, result.data(), result.size()))
        throw std::runtime_error("Invalid glTF vertex attribute shape");
    for (auto v : result)
        if (!std::isfinite(v)) throw std::runtime_error("Non-finite glTF vertex attribute");
    return result;
}
struct Importer {
    const GltfPackage &package;
    const cgltf_data &data;
    GltfImport result;
    Edit edit{"Import glTF 2.0", {}};
    QJsonObject losses;
    std::map<const cgltf_mesh *, DefinitionPtr> meshes;
    std::map<const cgltf_material *, Id> materials;
    std::map<std::pair<const cgltf_image *, int>, Id> images;
    Id nextBody{1}, nextDefinition{1}, nextMaterial{1}, nextAsset{1}, nextScene{1};
    size_t vertices{}, faces{}, records{}, assetSize{};
    std::set<const cgltf_node *> visited;
    explicit Importer(const GltfPackage &p) : package(p), data(p.data()) {}
    void loss(const char *key, int count = 1) { losses[key] = losses[key].toInt() + count; }
    void budget(size_t v, size_t f, size_t r) {
        vertices += v; faces += f; records += r;
        if (vertices > 100000 || faces > 100000 || records > 10000)
            throw std::runtime_error("Expanded glTF exceeds native geometry limits (100000 vertices/faces, 10000 records)");
    }
    Id material(const cgltf_material *source) {
        if (materials.contains(source)) return materials.at(source);
        auto m = std::make_shared<MaterialRecord>();
        m->id = nextMaterial++;
        m->name = name(source ? source->name : nullptr, "glTF material " + std::to_string(m->id));
        m->color = {1, 1, 1};
        if (source) {
            const auto &pbr = source->pbr_metallic_roughness;
            for (int i = 0; i < 4; ++i)
                if (!std::isfinite(pbr.base_color_factor[i]) || pbr.base_color_factor[i] < 0 || pbr.base_color_factor[i] > 1)
                    throw std::runtime_error("Invalid glTF base color factor");
            for (int i = 0; i < 3; ++i) {
                const double v = pbr.base_color_factor[i];
                m->color[i] = v <= .0031308 ? 12.92 * v : 1.055 * std::pow(v, 1.0 / 2.4) - .055;
            }
            if (source->alpha_mode == cgltf_alpha_mode_blend) m->opacity = pbr.base_color_factor[3];
            if (source->alpha_mode == cgltf_alpha_mode_mask)
                throw std::runtime_error("glTF alpha MASK materials are outside the supported import subset");
            if (pbr.base_color_texture.texture) m->asset = image(pbr.base_color_texture, source->alpha_mode);
            if (!source->double_sided) loss("backFaceCullingOmitted");
            if (pbr.metallic_factor != 0 || pbr.roughness_factor != 1 || pbr.metallic_roughness_texture.texture)
                loss("metallicRoughnessOmitted");
            if (source->normal_texture.texture) loss("normalMapsOmitted");
            if (source->occlusion_texture.texture) loss("occlusionMapsOmitted");
            if (source->emissive_texture.texture || source->emissive_factor[0] || source->emissive_factor[1] || source->emissive_factor[2])
                loss("emissionOmitted");
            if (source->unlit) loss("unlitShadingOmitted");
        } else loss("metallicRoughnessOmitted"); // glTF default is metallic white.
        materials[source] = m->id;
        edit.materials.push_back({m->id, nullptr, m});
        return m->id;
    }
    Id image(const cgltf_texture_view &view, cgltf_alpha_mode alpha) {
        const auto &texture = *view.texture;
        if (!texture.image) throw std::runtime_error("glTF base color texture has no supported image");
        if (texture.sampler) {
            const auto &s = *texture.sampler;
            if (s.wrap_s != cgltf_wrap_mode_repeat || s.wrap_t != cgltf_wrap_mode_repeat)
                throw std::runtime_error("glTF textures require repeat addressing for native import");
            if (s.mag_filter == cgltf_filter_type_nearest || s.min_filter == cgltf_filter_type_nearest ||
                s.min_filter == cgltf_filter_type_nearest_mipmap_nearest || s.min_filter == cgltf_filter_type_nearest_mipmap_linear)
                loss("nearestTextureFilteringApproximated");
        }
        const auto key = std::make_pair(texture.image, int(alpha));
        if (images.contains(key)) return images.at(key);
        const auto &bytes = package.image(size_t(texture.image - data.images));
        auto a = std::make_shared<AssetRecord>();
        a->id = nextAsset++;
        a->name = name(texture.image->name, "glTF image " + std::to_string(a->id));
        a->mediaType = bytes.startsWith("\x89PNG") ? "image/png" : "image/jpeg";
        a->payload = std::make_shared<AssetPayload>(std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
        const auto decoded = decodeTextureImage(*a);
        if (decoded.status != TextureImageStatus::Ready) throw std::runtime_error("glTF texture is not a supported bounded PNG/JPEG image");
        if (alpha == cgltf_alpha_mode_opaque && decoded.image->hasTransparency()) {
            auto rgba = decoded.image->rgba();
            for (size_t i = 3; i < rgba.size(); i += 4) rgba[i] = 255;
            const auto png = encodeTexturePng(TextureImage(decoded.image->width(), decoded.image->height(), std::move(rgba)));
            a->mediaType = "image/png";
            a->payload = std::make_shared<AssetPayload>(std::vector<std::uint8_t>(png.begin(), png.end()));
        }
        assetSize += a->payload->bytes().size();
        if (assetSize > assetTotalLimit) throw std::runtime_error("Imported textures exceed native asset budget");
        images[key] = a->id;
        edit.assets.push_back({a->id, nullptr, a});
        return a->id;
    }
    DefinitionPtr mesh(const cgltf_mesh &source) {
        if (meshes.contains(&source)) return meshes.at(&source);
        auto definition = std::make_shared<ComponentDefinition>();
        definition->id = nextDefinition++; definition->root = 1;
        definition->name = name(source.name, "glTF mesh " + std::to_string(definition->id));
        auto root = std::make_shared<Body>(); root->id = 1; root->kind = BodyKind::Group; root->name = definition->name;
        definition->members[1] = root; budget(0, 0, 1);
        Id member = 2;
        for (size_t pi = 0; pi < source.primitives_count; ++pi) {
            const auto &p = source.primitives[pi];
            if (p.targets_count) throw std::runtime_error("glTF morph targets are outside the supported import subset");
            if (p.has_draco_mesh_compression) throw std::runtime_error("Compressed glTF meshes are unsupported");
            if (p.type != cgltf_primitive_type_triangles && p.type != cgltf_primitive_type_triangle_strip && p.type != cgltf_primitive_type_triangle_fan) {
                loss("nonTrianglePrimitivesOmitted"); continue;
            }
            const auto *position = attribute(p, cgltf_attribute_type_position);
            if (!position || position->type != cgltf_type_vec3 || position->component_type != cgltf_component_type_r_32f || position->normalized)
                throw std::runtime_error("glTF POSITION must be floating-point VEC3");
            const auto count = p.indices ? p.indices->count : position->count;
            if (count < 3 || (p.type == cgltf_primitive_type_triangles && count % 3))
                throw std::runtime_error("Invalid glTF triangle index count");
            if (p.indices && (p.indices->type != cgltf_type_scalar || p.indices->normalized ||
                (p.indices->component_type != cgltf_component_type_r_8u && p.indices->component_type != cgltf_component_type_r_16u && p.indices->component_type != cgltf_component_type_r_32u)))
                throw std::runtime_error("Invalid glTF index accessor");
            budget(position->count, p.type == cgltf_primitive_type_triangles ? count / 3 : count - 2, 1);
            auto body = std::make_shared<Body>(); body->id = member++; body->parent = 1;
            body->name = "Primitive " + std::to_string(pi + 1);
            const auto materialId = material(p.material); body->materials = {materialId, materialId};
            for (size_t i = 0; i < position->count; ++i) {
                const auto v = values(*position, i, cgltf_type_vec3);
                const Vec3 point{v[0], -v[2], v[1]}; checkPoint(point);
                body->surface.vertices[i + 1] = point;
            }
            body->surface.nextId = position->count + 1;
            const auto *normal = attribute(p, cgltf_attribute_type_normal);
            if (normal) {
                if (normal->count != position->count) throw std::runtime_error("glTF normal count mismatch");
                for (size_t i = 0; i < normal->count; ++i) {
                    const auto v = values(*normal, i, cgltf_type_vec3);
                    if (length({v[0], v[1], v[2]}) < 1e-6) throw std::runtime_error("Invalid glTF zero normal");
                }
                loss("customNormalsRecomputed");
            }
            const cgltf_texture_view *view = p.material && p.material->pbr_metallic_roughness.base_color_texture.texture ? &p.material->pbr_metallic_roughness.base_color_texture : nullptr;
            const cgltf_accessor *uv = nullptr;
            if (view) {
                const auto index = view->has_transform && view->transform.has_texcoord ? view->transform.texcoord : view->texcoord;
                uv = attribute(p, cgltf_attribute_type_texcoord, index);
                if (!uv || uv->count != position->count || uv->type != cgltf_type_vec2) throw std::runtime_error("Missing or invalid glTF texture coordinates");
            }
            for (size_t ai = 0; ai < p.attributes_count; ++ai) {
                const auto type = p.attributes[ai].type;
                if (type == cgltf_attribute_type_color) loss("vertexColorsOmitted");
                if (type == cgltf_attribute_type_tangent) loss("tangentsRecomputed");
                if (type == cgltf_attribute_type_joints || type == cgltf_attribute_type_weights)
                    throw std::runtime_error("glTF skin attributes are outside the supported import subset");
            }
            const auto index = [&](size_t i) -> Id {
                const auto vertex = p.indices ? cgltf_accessor_read_index(p.indices, i) : i;
                if (vertex >= position->count) throw std::runtime_error("glTF vertex index out of range");
                return vertex + 1;
            };
            for (size_t i = 0; i < count - 2; i += p.type == cgltf_primitive_type_triangles ? 3 : 1) {
                std::array<Id, 3> ids{index(i), index(i + 1), index(i + 2)};
                if (p.type == cgltf_primitive_type_triangle_fan) ids = {index(0), index(i + 1), index(i + 2)};
                if (p.type == cgltf_primitive_type_triangle_strip && i % 2) std::swap(ids[0], ids[1]);
                std::array<Vec3, 3> points{body->surface.vertices.at(ids[0]), body->surface.vertices.at(ids[1]), body->surface.vertices.at(ids[2])};
                if (length(cross(points[1] - points[0], points[2] - points[0])) <= tolerance * tolerance) {
                    loss("degenerateTrianglesOmitted"); continue;
                }
                const Id face = body->surface.nextId++;
                body->surface.faces[face] = Face{face, {{ids[0], ids[1], ids[2]}}};
                if (uv) {
                    std::array<TextureCoordinate, 3> coordinates{};
                    for (size_t k = 0; k < 3; ++k) {
                        const auto v = values(*uv, ids[k] - 1, cgltf_type_vec2);
                        double u = v[0], w = v[1];
                        if (view->has_transform) {
                            const auto &t = view->transform;
                            const double x = u * t.scale[0], y = w * t.scale[1];
                            u = t.offset[0] + std::cos(t.rotation) * x - std::sin(t.rotation) * y;
                            w = t.offset[1] + std::sin(t.rotation) * x + std::cos(t.rotation) * y;
                        }
                        coordinates[k] = {u, w};
                    }
                    const auto mapping = pinnedTextureMapping(points, coordinates);
                    body->faceTextureMappings[face] = {mapping, mapping};
                }
            }
            body->surface.validate(); body->topology = Topology::rebuild(body->surface, {});
            if (normal) {
                const auto adjacency = body->topology.adjacency(body->surface);
                for (const auto &[edge, incident] : adjacency.edgeFaces)
                    if (incident.size() == 2) body->edgeAppearances[edge] = {false, true, true};
            }
            definition->members[body->id] = body;
        }
        definition->nextMemberId = member;
        meshes[&source] = definition; edit.definitions.push_back({definition->id, nullptr, definition});
        return definition;
    }
    void camera(const cgltf_camera &source, const Transform &world, const std::string &nodeName) {
        if (edit.scenes.size() == sceneCountLimit) throw std::runtime_error("Too many glTF cameras");
        const auto eye = world.point({});
        const auto forward = normalized(world.vector({0, 0, -1}));
        const auto up = normalized(world.vector({0, 1, 0}));
        SceneCamera pose;
        const auto back = forward * -1;
        pose.yaw = std::atan2(back.y, back.x) * 180 / std::numbers::pi;
        pose.pitch = std::asin(std::clamp(back.z, -1.0, 1.0)) * 180 / std::numbers::pi;
        pose.distance = 10;
        if (source.type == cgltf_camera_type_perspective) pose.fieldOfView = source.data.perspective.yfov * 180 / std::numbers::pi;
        else if (source.type == cgltf_camera_type_orthographic) {
            pose.orthographic = true; pose.distance = source.data.orthographic.ymag / .45;
        } else throw std::runtime_error("Invalid glTF camera type");
        pose.target = eye + forward * pose.distance; pose.validate();
        if (std::abs(dot(forward, {0, 0, 1})) > .999999 || dot(up, normalized(cross(normalized(cross(forward, {0, 0, 1})), forward))) < .99999)
            loss("cameraRollApproximated");
        loss("cameraAspectAndClipPlanesOmitted");
        auto scene = std::make_shared<SceneRecord>(); scene->id = nextScene++;
        scene->position = edit.scenes.size(); scene->name = nodeName + " (camera " + std::to_string(scene->id) + ")";
        scene->snapshot.camera = pose; edit.scenes.push_back({scene->id, nullptr, scene});
    }
    void node(const cgltf_node &source, Id parent, const Transform &parentWorld, size_t depth) {
        if (depth > 64 || !visited.insert(&source).second) throw std::runtime_error("glTF selected scene has repeated nodes or excessive depth");
        if (source.skin || source.weights_count) throw std::runtime_error("glTF skinning/morph weights are outside the supported import subset");
        budget(0, 0, 1);
        auto body = std::make_shared<Body>(); body->id = nextBody++; body->parent = parent; body->kind = BodyKind::Group;
        body->name = name(source.name, "glTF node " + std::to_string(body->id));
        cgltf_float raw[16]; cgltf_node_transform_local(&source, raw);
        Transform local; std::copy(raw, raw + 16, local.m.begin()); local.validate();
        body->transform = basis * local * basis.inverse();
        const auto nativeWorld = parentWorld * body->transform;
        edit.changes.push_back({body->id, nullptr, body});
        if (source.mesh) {
            const auto definition = mesh(*source.mesh);
            auto instance = std::make_shared<ComponentInstance>(); instance->definition = definition->id;
            for (const auto &[member, canonical] : definition->members) {
                budget(canonical->surface.vertices.size(), canonical->surface.faces.size(), 1);
                instance->members[member] = nextBody++;
            }
            for (const auto &[member, canonical] : definition->members) {
                auto placed = std::make_shared<Body>(*canonical); placed->id = instance->members.at(member);
                placed->parent = member == definition->root ? body->id : instance->members.at(canonical->parent);
                edit.changes.push_back({placed->id, nullptr, placed});
            }
            edit.instances.push_back({instance->members.at(definition->root), nullptr, instance});
        }
        if (source.camera) camera(*source.camera, nativeWorld * basis, body->name);
        if (source.light) loss("punctualLightsOmitted");
        for (size_t i = 0; i < source.children_count; ++i) node(*source.children[i], body->id, nativeWorld, depth + 1);
    }
    GltfImport run() {
        if (data.animations_count) loss("animationsOmitted", int(data.animations_count));
        for (size_t i = 0; i < data.extensions_used_count; ++i)
            if (std::string(data.extensions_used[i]) != "KHR_texture_transform") loss("optionalExtensionsOmitted");
        const auto *scene = data.scene ? data.scene : (data.scenes_count ? &data.scenes[0] : nullptr);
        if (scene) {
            for (size_t i = 0; i < scene->nodes_count; ++i) node(*scene->nodes[i], 0, {}, 0);
            if (data.scenes_count > 1) loss("additionalScenesOmitted", int(data.scenes_count - 1));
        } else {
            for (size_t i = 0; i < data.nodes_count; ++i) if (!data.nodes[i].parent) node(data.nodes[i], 0, {}, 0);
        }
        size_t triangles = 0;
        for (const auto &c : edit.changes) triangles += c.after->surface.faces.size();
        if (!triangles) throw std::runtime_error("glTF selected scene contains no supported nondegenerate triangles");
        edit.nextIdFloor = nextBody; edit.nextDefinitionFloor = nextDefinition;
        edit.nextMaterialFloor = nextMaterial; edit.nextAssetFloor = nextAsset; edit.nextSceneFloor = nextScene;
        result.document.apply(std::move(edit), result.document.revision());
        // Full native serialization validates world bounds and all linked records before publication.
        (void)decodeDocument(encodeDocument(result.document));
        result.report = {{"format", "glTF 2.0"}, {"units", "m"}, {"sourceUp", "Y"}, {"nativeUp", "Z"},
                         {"nodes", qint64(visited.size())}, {"triangles", qint64(triangles)},
                         {"meshDefinitions", qint64(meshes.size())}, {"instances", qint64(result.document.instances().size())},
                         {"materials", qint64(materials.size())}, {"images", qint64(images.size())},
                         {"cameras", qint64(result.document.scenes().size())}, {"losses", losses}, {"package", package.report()}};
        return std::move(result);
    }
};
} // namespace
GltfImport importGltf(const GltfPackage &package) { return Importer(package).run(); }
GltfImport loadGltf(const QString &path) { return importGltf(GltfPackage::read(path)); }
} // namespace sketchy
