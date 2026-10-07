#include "integrations/glb_export.hpp"
#include "io/gltf_package.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtEndian>
#include <bit>
#include <cgltf.h>
#include <iostream>
#include <vector>
using namespace sketchy;
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
void write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(),
          "Write glTF fixture");
}
template <class F> void rejects(F f) {
    bool failed = false;
    try {
        f();
    } catch (const std::exception &) {
        failed = true;
    }
    check(failed, "Unsafe glTF package must be rejected");
}
QJsonObject fixture() {
    return {{"asset", QJsonObject{{"version", "2.0"}}},
            {"buffers", QJsonArray{QJsonObject{{"uri", "triangle.bin"}, {"byteLength", 36}}}},
            {"bufferViews",
             QJsonArray{QJsonObject{{"buffer", 0}, {"byteOffset", 0}, {"byteLength", 36}}}},
            {"accessors", QJsonArray{QJsonObject{{"bufferView", 0},
                                                 {"componentType", 5126},
                                                 {"type", "VEC3"},
                                                 {"count", 3},
                                                 {"min", QJsonArray{0, 0, 0}},
                                                 {"max", QJsonArray{1, 1, 0}}}}},
            {"meshes", QJsonArray{QJsonObject{
                           {"primitives", QJsonArray{QJsonObject{
                                              {"attributes", QJsonObject{{"POSITION", 0}}}}}}}}},
            {"nodes", QJsonArray{QJsonObject{{"mesh", 0}}}},
            {"scenes", QJsonArray{QJsonObject{{"nodes", QJsonArray{0}}}}},
            {"scene", 0}};
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir files;
        check(files.isValid(), "glTF fixture directory");
        QDir().mkdir(files.filePath("model"));
        const auto folder = files.filePath("model"), source = folder + "/triangle.gltf",
                   sidecar = folder + "/triangle.bin";
        QByteArray vertices;
        for (float value : {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f}) {
            const auto offset = vertices.size();
            vertices.resize(offset + 4);
            qToLittleEndian<quint32>(std::bit_cast<quint32>(value), vertices.data() + offset);
        }
        write(sidecar, vertices);
        auto tree = fixture();
        write(source, QJsonDocument(tree).toJson());
        auto package = GltfPackage::read(source);
        check(package.data().meshes_count == 1 && package.data().nodes_count == 1 &&
                  package.report()["sidecars"] == 1,
              "Relative glTF buffer loads");
        float position[3]{};
        check(cgltf_accessor_read_float(&package.data().accessors[0], 1, position, 3) &&
                  position[0] == 1,
              "Captured accessor decodes expected geometry");
        QFile::remove(sidecar);
        check(cgltf_accessor_read_float(&package.data().accessors[0], 2, position, 3) &&
                  position[1] == 1,
              "Captured buffers survive original file removal");
        auto inlineTree = fixture();
        auto buffers = inlineTree["buffers"].toArray();
        auto buffer = buffers[0].toObject();
        buffer["uri"] =
            "data:application/octet-stream;base64," + QString::fromLatin1(vertices.toBase64());
        buffers[0] = buffer;
        inlineTree["buffers"] = buffers;
        write(source, QJsonDocument(inlineTree).toJson());
        check(GltfPackage::read(source).report()["sidecars"] == 0,
              "Embedded base64 buffers need no sidecar");
        for (const auto &uri : QStringList{"../outside.bin", "%2e%2e/outside.bin", "/etc/passwd",
                                           "https://example.invalid/model.bin",
                                           "file:///etc/passwd", "triangle.bin?query", "bad%ZZ.bin",
                                           "data:application/octet-stream;base64,AAAA="}) {
            auto bad = fixture();
            auto list = bad["buffers"].toArray();
            auto record = list[0].toObject();
            record["uri"] = uri;
            list[0] = record;
            bad["buffers"] = list;
            write(source, QJsonDocument(bad).toJson());
            rejects([&] { GltfPackage::read(source); });
        }
        write(files.filePath("outside.bin"), vertices);
        check(QFile::link(files.filePath("outside.bin"), sidecar), "Create escaping sidecar link");
        write(source, QJsonDocument(fixture()).toJson());
        rejects([&] { GltfPackage::read(source); });
        QFile::remove(sidecar);
        write(sidecar, vertices);
        for (const auto &patch : std::vector<QJsonObject>{{{"count", 1000001}},
                                                          {{"byteOffset", 1}},
                                                          {{"count", 100000}},
                                                          {{"componentType", 0}},
                                                          {{"byteOffset", 1e20}}}) {
            auto bad = fixture();
            auto list = bad["accessors"].toArray();
            auto record = list[0].toObject();
            for (auto it = patch.begin(); it != patch.end(); ++it)
                record[it.key()] = it.value();
            list[0] = record;
            bad["accessors"] = list;
            write(source, QJsonDocument(bad).toJson());
            rejects([&] { GltfPackage::read(source); });
        }
        auto bad = fixture();
        bad["extensionsRequired"] = QJsonArray{"UNKNOWN_required"};
        write(source, QJsonDocument(bad).toJson());
        rejects([&] { GltfPackage::read(source); });
        bad = fixture();
        bad["nodes"] = QJsonArray{QJsonObject{{"children", QJsonArray{0}}}};
        write(source, QJsonDocument(bad).toJson());
        rejects([&] { GltfPackage::read(source); });
        auto sparseTree = fixture();
        auto sparseBytes = vertices;
        sparseBytes.append(char(1));
        sparseBytes.resize(40, char(0));
        for (float value : {4.f, 5.f, 6.f}) {
            const auto offset = sparseBytes.size();
            sparseBytes.resize(offset + 4);
            qToLittleEndian<quint32>(std::bit_cast<quint32>(value), sparseBytes.data() + offset);
        }
        write(sidecar, sparseBytes);
        auto sparseBuffers = sparseTree["buffers"].toArray();
        auto sparseBuffer = sparseBuffers[0].toObject();
        sparseBuffer["byteLength"] = sparseBytes.size();
        sparseBuffers[0] = sparseBuffer;
        sparseTree["buffers"] = sparseBuffers;
        auto views = sparseTree["bufferViews"].toArray();
        views.append(QJsonObject{{"buffer", 0}, {"byteOffset", 36}, {"byteLength", 1}});
        views.append(QJsonObject{{"buffer", 0}, {"byteOffset", 40}, {"byteLength", 12}});
        sparseTree["bufferViews"] = views;
        auto accessors = sparseTree["accessors"].toArray();
        auto accessor = accessors[0].toObject();
        accessor["max"] = QJsonArray{4, 5, 6};
        accessor["sparse"] =
            QJsonObject{{"count", 1},
                        {"indices", QJsonObject{{"bufferView", 1}, {"componentType", 5121}}},
                        {"values", QJsonObject{{"bufferView", 2}}}};
        accessors[0] = accessor;
        sparseTree["accessors"] = accessors;
        write(source, QJsonDocument(sparseTree).toJson());
        auto sparse = GltfPackage::read(source);
        check(cgltf_accessor_read_float(&sparse.data().accessors[0], 1, position, 3) &&
                  position[0] == 4 && position[2] == 6,
              "Sparse accessor overlays captured base geometry");
        auto interleaved = sparseTree;
        auto paddedViews = views;
        auto baseView = paddedViews[0].toObject();
        baseView["byteStride"] = 16;
        baseView["byteLength"] = 48;
        paddedViews[0] = baseView;
        // All views remain in the buffer; only the sparse/base stride combination is unsupported.
        interleaved["bufferViews"] = paddedViews;
        write(source, QJsonDocument(interleaved).toJson());
        bool strideRejected = false;
        try {
            GltfPackage::read(source);
        } catch (const std::exception &error) {
            strideRejected = QString::fromUtf8(error.what()).contains("tightly packed");
        }
        check(strideRejected,
              "Sparse readers never advance tight values by an interleaved base stride");
        write(source, QJsonDocument(sparseTree).toJson());
        sparseBytes[36] = char(3);
        write(sidecar, sparseBytes);
        rejects([&] { GltfPackage::read(source); });
        Document doc;
        doc.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
        const auto exported = exportGlb(RenderSnapshot::capture(doc));
        const auto glb = folder + "/scene.glb";
        write(glb, exported.glb);
        check(GltfPackage::read(glb).data().meshes_count == 1,
              "Native GLB exporter round-trips through strict package parser");
        auto broken = exported.glb;
        broken.append('x');
        write(glb, broken);
        rejects([&] { GltfPackage::read(glb); });
        broken = exported.glb;
        qToLittleEndian<quint32>(3, broken.data() + 4);
        write(glb, broken);
        rejects([&] { GltfPackage::read(glb); });
        std::cout << "Bounded GLB/glTF parsing, immutable buffers and contained sidecars passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
