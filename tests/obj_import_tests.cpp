#include "io/obj_import.hpp"
#include "io/texture_image.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>
using namespace sketchy;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void near(double a, double b) { check(std::abs(a - b) < 1e-6, "Independent numeric oracle"); }
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Unsafe or unsupported OBJ must reject");
}
void write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "Write fixture");
}
QByteArray read(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Read fixture");
    return file.readAll();
}
const Body &mesh(const Document &doc) {
    for (const auto &[id, b] : doc.bodies()) {
        (void)id;
        if (b->kind == BodyKind::Geometry)
            return *b;
    }
    throw std::runtime_error("No geometry");
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir directory;
        check(directory.isValid(), "Scratch directory");
        const auto root = directory.path(), path = root + "/study.obj";
        const QByteArray concave =
            "o Building\ng Walls Exterior\nv 0 0 0\nv 2000 0 0\nv 2000 1000 0\nv 1000 1000 0\nv "
            "1000 2000 0\nv 0 2000 0\nf -6 -5 -4 -3 -2 -1\nl 1 3\n";
        write(path, concave);
        auto imported = loadObj(path, {.001, ObjUpAxis::Y});
        const auto &b = mesh(imported.document);
        check(b.surface.faces.size() == 1 && b.surface.wires.size() == 1,
              "Concave polygon and wire remain editable");
        near(b.surface.area(b.surface.faces.begin()->first), 3);
        check(b.surface.triangles().size() == 4 && b.properties.size() == 2,
              "Concavity and both group names retained");
        check(imported.report["losses"].toObject()["overlappingGroupsCombined"].toInt() == 1,
              "Overlapping membership report");
        double maximum = 0;
        for (const auto &[id, p] : b.surface.vertices) {
            (void)id;
            near(p.y, 0);
            maximum = std::max(maximum, p.z);
        }
        near(maximum, 2);
        const auto encoded = encodeDocument(imported.document);
        check(encodeDocument(decodeDocument(encoded)) == encoded, "Native round trip");
        imported.document.undo();
        check(imported.document.bodies().empty(), "One atomic import undo");
        imported.document.redo();
        const auto restored = decodeDocument(encoded);
        check(imported.document.bodies().size() == restored.bodies().size(),
              "Redo restores body count");
        for (const auto &[id, body] : restored.bodies())
            check(*imported.document.bodies().at(id) == *body,
                  "Redo restores geometry and records");
        check(read(path) == concave, "Source unchanged");
        const QByteArray quad = "mtllib paint.mtl\nusemtl Paint\nv 0 0 0\nv 2 0 0\nv 2 3 0\nv 0 3 "
                                "0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nf 1/1 2/2 3/3 4/4\n";
        write(root + "/paint.mtl",
              "newmtl Paint\nKd .25 .5 1\nd .4\nmap_Kd -s 2 3 -o .2 .3 tile.png\n");
        write(root + "/tile.png", encodeTexturePng(TextureImage(1, 1, {255, 0, 0, 255})));
        write(path, quad);
        imported = loadObj(path, {1, ObjUpAxis::Z});
        const auto &textured = mesh(imported.document);
        check(textured.surface.faces.size() == 1 && imported.document.assets().size() == 1,
              "Affine quad with managed image");
        const auto face = textured.surface.faces.begin()->first;
        const auto mapping = *textured.faceTextureMappings.at(face).front;
        auto uv = mapping.coordinates({0, 0, 0});
        near(uv.u, .2);
        near(uv.v, .7);
        uv = mapping.coordinates({2, 3, 0});
        near(uv.u, 2.2);
        near(uv.v, -2.3);
        const auto material = imported.document.materials().begin()->second;
        near(material->color[0], 1.055 * std::pow(.25, 1. / 2.4) - .055);
        near(material->opacity, .4);
        check(decodeTextureImage(*imported.document.assets().begin()->second).status ==
                  TextureImageStatus::Ready,
              "Managed texture decodes");
        auto warped = quad;
        warped.replace("vt 1 1", "vt 1.5 1");
        write(path, warped);
        imported = loadObj(path, {1, ObjUpAxis::Z});
        check(mesh(imported.document).surface.faces.size() == 2 &&
                  imported.report["losses"].toObject()["facesTriangulatedForUV"].toInt() == 1,
              "Non-affine UV quad triangulates");
        QFile::remove(root + "/tile.png");
        imported = loadObj(path, {1, ObjUpAxis::Z});
        check(!imported.document.assets().begin()->second->payload &&
                  imported.report["losses"].toObject()["missingTextures"].toInt() == 1,
              "Missing texture placeholder and report");
        auto bare = quad;
        bare.replace("f 1/1 2/2 3/3 4/4", "f 1 2 3 4");
        write(path, bare);
        imported = loadObj(path, {1, ObjUpAxis::Z});
        check(!imported.document.materials().begin()->second->asset &&
                  imported.report["losses"].toObject()["texturedFacesWithoutUV"].toInt() == 1,
              "No accidental default texture projection");
        QFile::remove(root + "/paint.mtl");
        imported = loadObj(path, {1, ObjUpAxis::Z});
        check(imported.report["losses"].toObject()["missingMaterialLibraries"].toInt() == 1 &&
                  imported.report["losses"].toObject()["missingMaterials"].toInt() == 1,
              "Missing MTL and material fallback");
        for (const auto &unsafe :
             {"../outside.mtl", "/etc/passwd", "https://host/file", "..\\outside.mtl"}) {
            write(path, "mtllib " + QByteArray(unsafe) + "\n" + concave);
            rejects([&] { loadObj(path, {1, ObjUpAxis::Z}); });
        }
        QTemporaryDir outside;
        write(outside.path() + "/external.mtl", "newmtl Paint\n");
        check(QFile::link(outside.path(), root + "/escape"), "Sidecar symlink fixture");
        write(path, "mtllib escape/external.mtl\n" + concave);
        rejects([&] { loadObj(path, {1, ObjUpAxis::Z}); });
        write(path, "mtllib escape/missing.mtl\n" + concave);
        rejects([&] { loadObj(path, {1, ObjUpAxis::Z}); });
        write(path, quad);
        write(root + "/paint.mtl", "newmtl Paint\nmap_Kd ../outside.png\n");
        rejects([&] { loadObj(path, {1, ObjUpAxis::Z}); });
        QDir(root).mkdir("materials");
        write(root + "/materials/paint.mtl", "newmtl Paint\nmap_Kd ../tile.png\n");
        write(root + "/tile.png", encodeTexturePng(TextureImage(1, 1, {0, 255, 0, 255})));
        auto nested = quad;
        nested.replace("paint.mtl", "materials/paint.mtl");
        write(path, nested);
        check(loadObj(path, {1, ObjUpAxis::Z}).document.assets().size() == 1,
              "Contained parent reference from nested MTL");
        write(path, "v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nf 1/1 2/1 3/1\n");
        rejects([&] { loadObj(path, {1, ObjUpAxis::Z}); });
        const QByteArray smooth = "v 0 0 0\nv 1 0 0\nv 0 1 0\nv 0 0 1\ns 7\nf 1 2 3\nf 2 1 4\n";
        write(path, smooth);
        check(mesh(loadObj(path, {1, ObjUpAxis::Z}).document).edgeAppearances.size() == 1,
              "Shared smoothing group smooths shared edge");
        auto hard = smooth;
        hard.replace("f 2 1 4", "s off\nf 2 1 4");
        write(path, hard);
        check(mesh(loadObj(path, {1, ObjUpAxis::Z}).document).edgeAppearances.empty(),
              "Smoothing boundary stays hard");
        write(path, "v 0 0 0\nv 1 0 0\nl 1 2\n");
        check(loadObj(path, {1, ObjUpAxis::Z}).report["wireSegments"].toInt() == 1,
              "Wire-only model imports");
        write(path, "v 0 0 0\n");
        rejects([&] { loadObj(path, {1, ObjUpAxis::Z}); });
        std::cout << "OBJ native geometry, units, concavity, UV, material, sidecar, undo and "
                     "bounds checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
