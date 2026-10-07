#include "integrations/glb_export.hpp"
#include "io/gltf_import.hpp"
#include "io/texture_image.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtEndian>
#include <bit>
#include <iostream>
#include <numbers>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void near(double a, double b) {
    check(std::isfinite(a) && std::abs(a - b) < 1e-5, "Independent import coordinate oracle");
}
template <class F> void rejects(F operation) {
    try {
        operation();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Unsupported glTF must reject");
}
void write(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "Write import fixture");
}
struct Fixture {
    QJsonObject tree{{"asset", QJsonObject{{"version", "2.0"}}}};
    QByteArray buffer;
    QJsonArray views, accessors;
    int floats(std::initializer_list<float> data, const char *type, int width) {
        const auto start = buffer.size();
        for (auto v : data) {
            const auto offset = buffer.size();
            buffer.resize(offset + 4);
            qToLittleEndian<quint32>(std::bit_cast<quint32>(v), buffer.data() + offset);
        }
        views.append(QJsonObject{
            {"buffer", 0}, {"byteOffset", start}, {"byteLength", buffer.size() - start}});
        accessors.append(QJsonObject{{"bufferView", views.size() - 1},
                                     {"componentType", 5126},
                                     {"type", type},
                                     {"count", int(data.size()) / width}});
        return accessors.size() - 1;
    }
    Fixture() {
        const auto p = floats({0, 0, 0, 2, 0, 0, 0, 3, 0}, "VEC3", 3);
        const auto n = floats({0, 0, 1, 0, 0, 1, 0, 0, 1}, "VEC3", 3);
        const auto uv = floats({0, 0, 1, 0, 0, 1}, "VEC2", 2);
        tree["meshes"] = QJsonArray{QJsonObject{
            {"name", "Triangle"},
            {"primitives",
             QJsonArray{QJsonObject{
                 {"attributes", QJsonObject{{"POSITION", p}, {"NORMAL", n}, {"TEXCOORD_0", uv}}},
                 {"material", 0}}}}}};
        tree["materials"] = QJsonArray{QJsonObject{
            {"name", "Red"},
            {"doubleSided", true},
            {"pbrMetallicRoughness", QJsonObject{{"baseColorFactor", QJsonArray{.25, .5, 1, .4}},
                                                 {"metallicFactor", 0},
                                                 {"roughnessFactor", 1}}},
            {"alphaMode", "BLEND"}}};
        tree["nodes"] = QJsonArray{
            QJsonObject{{"name", "Parent"},
                        {"translation", QJsonArray{10, 20, 30}},
                        {"children", QJsonArray{1, 2}}},
            QJsonObject{{"name", "Normal"}, {"mesh", 0}},
            QJsonObject{{"name", "Mirror"}, {"mesh", 0}, {"scale", QJsonArray{-1, 1, 1}}}};
        tree["scenes"] = QJsonArray{QJsonObject{{"nodes", QJsonArray{0}}}};
        tree["scene"] = 0;
    }
    void save(const QString &path) {
        tree["buffers"] =
            QJsonArray{QJsonObject{{"byteLength", buffer.size()},
                                   {"uri", "data:application/octet-stream;base64," +
                                               QString::fromLatin1(buffer.toBase64())}}};
        tree["bufferViews"] = views;
        tree["accessors"] = accessors;
        write(path, QJsonDocument(tree).toJson());
    }
    void primitive(const QJsonObject &p) {
        auto meshes = tree["meshes"].toArray();
        auto mesh = meshes[0].toObject();
        mesh["primitives"] = QJsonArray{p};
        meshes[0] = mesh;
        tree["meshes"] = meshes;
    }
    QJsonObject primitive() const {
        return tree["meshes"].toArray()[0].toObject()["primitives"].toArray()[0].toObject();
    }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir dir;
        check(dir.isValid(), "Import scratch");
        const auto path = dir.filePath("model.gltf");
        Fixture f;
        f.save(path);
        QFile original(path);
        check(original.open(QIODevice::ReadOnly), "Read original");
        const auto bytes = original.readAll();
        original.close();
        auto captured = GltfPackage::read(path);
        QFile::remove(path);
        auto imported = importGltf(captured);
        auto &doc = imported.document;
        check(doc.definitions().size() == 1 && doc.instances().size() == 2 &&
                  doc.bodies().size() == 7,
              "Shared mesh instances retain nested hierarchy");
        check(imported.report["triangles"] == 2 &&
                  imported.report["losses"].toObject()["customNormalsRecomputed"] == 1,
              "Per-feature normal loss explicit");
        const auto &canonical = *doc.definitions().begin()->second->members.at(2);
        check(canonical.surface.vertices.at(3) == Vec3{0, 0, 3},
              "Y-up metres become native Z-up metres");
        size_t positive = 0, negative = 0;
        for (const auto &[id, body] : doc.bodies())
            if (body->kind == BodyKind::Geometry) {
                const auto world = doc.worldTransform(id);
                const auto origin = world.point({});
                near(origin.x, 10);
                near(origin.y, -30);
                near(origin.z, 20);
                const auto point = world.point({2, 0, 0});
                if (world.determinant() < 0) {
                    ++negative;
                    near(point.x, 8);
                } else {
                    ++positive;
                    near(point.x, 12);
                }
            }
        check(positive == 1 && negative == 1,
              "Mirrored placement retained without rewriting canonical mesh");
        const auto &m = *doc.materials().begin()->second;
        near(m.color[0], 1.055 * std::pow(.25, 1. / 2.4) - .055);
        near(m.opacity, .4);
        const auto encoded = encodeDocument(doc);
        check(encodeDocument(decodeDocument(encoded)) == encoded, "Complete native round trip");
        check(doc.dirty() && doc.canUndo(), "Import creates one unsaved undoable edit");
        doc.undo();
        check(doc.bodies().empty() && doc.definitions().empty(), "Import undo is atomic");
        doc.redo();
        auto restored = QJsonDocument::fromJson(encodeDocument(doc)).object();
        auto expected = QJsonDocument::fromJson(encoded).object();
        restored.remove("revision");
        expected.remove("revision");
        check(restored == expected, "Import redo restores all records; revision remains monotonic");
        write(path, bytes);
        check(loadGltf(path).report["triangles"] == 2, "Filesystem import works");
        QFile source(path);
        check(source.open(QIODevice::ReadOnly) && source.readAll() == bytes,
              "Source bytes unchanged");
        source.close();
        // Embedded image and KHR_texture_transform use glTF top-left UVs directly.
        const auto png = encodeTexturePng(TextureImage(2, 1, {255, 0, 0, 128, 0, 255, 0, 255}));
        f.tree["images"] = QJsonArray{
            QJsonObject{{"uri", "data:image/png;base64," + QString::fromLatin1(png.toBase64())}}};
        f.tree["textures"] = QJsonArray{QJsonObject{{"source", 0}}};
        f.tree["extensionsRequired"] = QJsonArray{"KHR_texture_transform"};
        f.tree["extensionsUsed"] = QJsonArray{"KHR_texture_transform"};
        auto mats = f.tree["materials"].toArray();
        auto mat = mats[0].toObject();
        auto pbr = mat["pbrMetallicRoughness"].toObject();
        pbr["baseColorTexture"] = QJsonObject{
            {"index", 0},
            {"extensions", QJsonObject{{"KHR_texture_transform",
                                        QJsonObject{{"offset", QJsonArray{.25, .5}},
                                                    {"scale", QJsonArray{2, 3}},
                                                    {"rotation", std::numbers::pi / 2}}}}}};
        mat["pbrMetallicRoughness"] = pbr;
        mats[0] = mat;
        f.tree["materials"] = mats;
        f.save(path);
        const auto textured = loadGltf(path);
        const auto &b = *textured.document.definitions().begin()->second->members.at(2);
        const auto &mapping = *b.faceTextureMappings.begin()->second.front;
        const auto first = mapping.coordinates({2, 0, 0}), second = mapping.coordinates({0, 0, 3});
        near(first.u, .25);
        near(first.v, 2.5);
        near(second.u, -2.75);
        near(second.v, .5);
        const auto &asset = *textured.document.assets().begin()->second;
        check(decodeTextureImage(asset).image->hasTransparency(), "BLEND texture alpha preserved");
        mat["alphaMode"] = "OPAQUE";
        mats[0] = mat;
        f.tree["materials"] = mats;
        f.save(path);
        const auto opaque = loadGltf(path);
        check(
            !decodeTextureImage(*opaque.document.assets().begin()->second).image->hasTransparency(),
            "OPAQUE ignores image alpha");
        near(opaque.document.materials().begin()->second->opacity, 1);
        mat["alphaMode"] = "MASK";
        mats[0] = mat;
        f.tree["materials"] = mats;
        f.save(path);
        rejects([&] { loadGltf(path); });
        Fixture camera;
        auto nodes = camera.tree["nodes"].toArray();
        nodes.append(QJsonObject{{"camera", 0}, {"translation", QJsonArray{1, 2, 3}}});
        auto parent = nodes[0].toObject();
        parent["children"] = QJsonArray{1, 2, 3};
        nodes[0] = parent;
        camera.tree["nodes"] = nodes;
        camera.tree["cameras"] = QJsonArray{QJsonObject{
            {"type", "perspective"},
            {"perspective", QJsonObject{{"yfov", std::numbers::pi / 3}, {"znear", .1}}}}};
        camera.save(path);
        const auto cameraImport = loadGltf(path);
        const auto &pose = *cameraImport.document.scenes().begin()->second->snapshot.camera;
        near(pose.fieldOfView, 60);
        near(pose.target.x, 11);
        near(pose.target.y, -23);
        near(pose.target.z, 22);
        near(pose.yaw, -90);
        near(pose.pitch, 0);
        Fixture bad;
        auto primitive = bad.primitive();
        primitive["mode"] = 1;
        bad.primitive(primitive);
        bad.save(path);
        rejects([&] { loadGltf(path); });
        bad = Fixture{};
        primitive = bad.primitive();
        primitive["targets"] = QJsonArray{QJsonObject{{"POSITION", 0}}};
        bad.primitive(primitive);
        bad.save(path);
        rejects([&] { loadGltf(path); });
        bad = Fixture{};
        nodes = bad.tree["nodes"].toArray();
        auto node = nodes[1].toObject();
        node["scale"] = QJsonArray{0, 1, 1};
        nodes[1] = node;
        bad.tree["nodes"] = nodes;
        bad.save(path);
        rejects([&] { loadGltf(path); });
        // Native exporter is another producer; a GLB must return the same metre dimensions.
        Document native;
        native.addFace({{{0, 0, 0}, {2, 0, 0}, {0, 3, 0}}});
        const auto glb = exportGlb(RenderSnapshot::capture(native));
        const auto glbPath = dir.filePath("native.glb");
        write(glbPath, glb.glb);
        check(loadGltf(glbPath).report["triangles"].toInt() >= 1,
              "Native GLB imports through public parser");
        std::cout
            << "glTF mesh, instance, material, UV, camera, atomicity and rejection tests passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
