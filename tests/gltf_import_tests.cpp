#include "integrations/glb_export.hpp"
#include "io/gltf_import.hpp"
#include "io/texture_image.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
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
        if (app.arguments().contains("--blender-real")) {
            const auto blender = qEnvironmentVariable("SKETCHYUP_BLENDER_TEST");
            if (blender.isEmpty())
                return 77;
            QProcess producer;
            producer.start(blender, {"--background", "--factory-startup", "--disable-autoexec",
                                     "--python-exit-code", "1", "--python",
                                     QStringLiteral(SOURCE_DIR "/tests/gltf_blender_fixture.py"),
                                     "--", dir.path()});
            check(producer.waitForStarted(10000) && producer.waitForFinished(90000) &&
                      producer.exitStatus() == QProcess::NormalExit && producer.exitCode() == 0,
                  "Actual Blender fixture export");
            const auto result = loadGltf(dir.filePath("blender.gltf"));
            check(result.report["triangles"] == 2 && result.report["instances"] == 2 &&
                      result.report["cameras"] == 1 && result.report["images"].toInt() >= 1,
                  "Blender hierarchy, cameras, shared textures and triangles import");
            int mirrored = 0, ordinary = 0;
            for (const auto &[id, body] : result.document.bodies())
                if (body->kind == BodyKind::Geometry) {
                    const auto world = result.document.worldTransform(id);
                    const auto origin = world.point({});
                    near(origin.x, 10);
                    near(origin.y, 20);
                    near(origin.z, 30);
                    bool expectedTip = false, expectedHeight = false;
                    for (const auto &[vertex, point] : body->surface.vertices) {
                        (void)vertex;
                        const auto p = world.point(point);
                        expectedTip |=
                            length(p - Vec3{world.determinant() < 0 ? 8. : 12., 20, 30}) < 1e-5;
                        expectedHeight |= length(p - Vec3{10, 23, 30}) < 1e-5;
                    }
                    check(expectedTip && expectedHeight && !body->faceTextureMappings.empty(),
                          "Blender metre dimensions, reflection and UVs match independent source");
                    if (world.determinant() < 0)
                        ++mirrored;
                    else
                        ++ordinary;
                }
            check(mirrored == 1 && ordinary == 1, "Blender mirrored instance retained");
            std::cout << QJsonDocument(result.report).toJson().constData();
            return 0;
        }
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
#ifdef CLI_PATH
        const auto cli = QString::fromUtf8(CLI_PATH);
        auto invoke = [&](QStringList arguments, bool success = true) {
            QProcess process;
            process.start(cli, arguments);
            check(process.waitForStarted(10000) && process.waitForFinished(30000) &&
                      process.exitStatus() == QProcess::NormalExit &&
                      (process.exitCode() == 0) == success,
                  "glTF CLI operation result");
            const auto output =
                success ? process.readAllStandardOutput() : process.readAllStandardError();
            const auto report = QJsonDocument::fromJson(output);
            check(report.isObject(), "glTF CLI returns structured JSON");
            return report.object();
        };
        const auto destination = dir.filePath("converted.sketchyup");
        check(invoke({"--import-gltf", path})["importReport"].toObject()["triangles"] == 2,
              "CLI conversion report without output");
        const auto saved = invoke({"--import-gltf", path, "--output", destination});
        check(saved["nativeFile"].toObject()["status"] == "created" &&
                  loadDocument(destination).instances().size() == 2,
              "CLI creates a complete native copy");
        const auto copy = encodeDocument(loadDocument(destination));
        invoke({"--import-gltf", path, "--output", destination}, false);
        invoke({"--import-gltf", path, "--output", path}, false);
        const auto link = dir.filePath("existing-link");
        check(QFile::link(path, link), "CLI output symlink fixture");
        invoke({"--import-gltf", path, "--output", link}, false);
        invoke({"--import-gltf", path, "--input", destination}, false);
        invoke({"--import-gltf", path, "--format-capabilities"}, false);
        invoke({"--import-gltf", path, "extra"}, false);
        check(encodeDocument(loadDocument(destination)) == copy,
              "Rejected output replacement preserves native copy");
        check(source.open(QIODevice::ReadOnly) && source.readAll() == bytes,
              "CLI source remains byte exact");
        source.close();
#endif
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
        // Both strip and fan expansion preserve winding; budget checks precede allocation.
        for (int mode : {5, 6}) {
            Fixture strip;
            const int positions = strip.floats({0, 0, 0, 2, 0, 0, 0, 3, 0, 2, 3, 0}, "VEC3", 3);
            auto p = strip.primitive();
            p["mode"] = mode;
            p["attributes"] = QJsonObject{{"POSITION", positions}};
            strip.primitive(p);
            strip.save(path);
            const auto result = loadGltf(path);
            check(result.report["triangles"] == 4,
                  "Strip/fan expands two triangles in each instance");
            if (mode == 5) {
                const auto &surface =
                    result.document.definitions().begin()->second->members.at(2)->surface;
                for (const auto &[id, face] : surface.faces) {
                    (void)face;
                    near(surface.normal(id).y, -1);
                }
            }
        }
        Fixture huge;
        auto accessor = huge.accessors[0].toObject();
        accessor["count"] = 100001;
        huge.accessors[0] = accessor;
        auto view = huge.views[0].toObject();
        view["byteLength"] = 100001 * 12;
        huge.views[0] = view;
        huge.buffer.resize(100001 * 12);
        auto p = huge.primitive();
        p["attributes"] = QJsonObject{{"POSITION", 0}};
        p["mode"] = 5;
        huge.primitive(p);
        huge.save(path);
        rejects([&] { loadGltf(path); });
        Fixture duplicates;
        auto duplicateMaterials = duplicates.tree["materials"].toArray();
        duplicateMaterials.append(duplicateMaterials[0]);
        duplicates.tree["materials"] = duplicateMaterials;
        auto duplicateMeshes = duplicates.tree["meshes"].toArray();
        auto anotherPrimitive = duplicates.primitive();
        anotherPrimitive["material"] = 1;
        duplicateMeshes.append(QJsonObject{{"primitives", QJsonArray{anotherPrimitive}}});
        duplicates.tree["meshes"] = duplicateMeshes;
        auto duplicateNodes = duplicates.tree["nodes"].toArray();
        auto anotherNode = duplicateNodes[2].toObject();
        anotherNode["mesh"] = 1;
        duplicateNodes[2] = anotherNode;
        duplicates.tree["nodes"] = duplicateNodes;
        duplicates.save(path);
        const auto renamed = loadGltf(path);
        check(renamed.document.materials().size() == 2 &&
                  renamed.report["losses"].toObject()["duplicateMaterialNamesRenamed"] == 1,
              "Valid repeated glTF names become distinct native materials with a notice");
        Fixture compressed;
        const auto largePng = encodeTexturePng(
            TextureImage(4096, 4096, std::vector<std::uint8_t>(64 * 1024 * 1024, 255)));
        QJsonArray images, textures, materials, meshes, roots;
        for (int i = 0; i < 5; ++i) {
            images.append(QJsonObject{
                {"uri", "data:image/png;base64," + QString::fromLatin1(largePng.toBase64())}});
            textures.append(QJsonObject{{"source", i}});
            materials.append(
                QJsonObject{{"pbrMetallicRoughness",
                             QJsonObject{{"baseColorTexture", QJsonObject{{"index", i}}}}}});
            auto primitive = compressed.primitive();
            primitive["material"] = i;
            meshes.append(QJsonObject{{"primitives", QJsonArray{primitive}}});
            roots.append(QJsonObject{{"mesh", i}});
        }
        compressed.tree["images"] = images;
        compressed.tree["textures"] = textures;
        compressed.tree["materials"] = materials;
        compressed.tree["meshes"] = meshes;
        compressed.tree["nodes"] = roots;
        compressed.tree["scenes"] = QJsonArray{QJsonObject{{"nodes", QJsonArray{0, 1, 2, 3, 4}}}};
        compressed.save(path);
        bool decodedBudget = false;
        try {
            loadGltf(path);
        } catch (const std::exception &error) {
            decodedBudget =
                QString::fromUtf8(error.what()).contains("aggregate decoded-image budget");
        }
        check(decodedBudget, "Small compressed images cannot bypass aggregate decode budget");
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
