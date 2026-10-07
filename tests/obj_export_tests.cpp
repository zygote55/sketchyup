#include "io/obj_export.hpp"
#include "io/texture_image.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void near(double a, double b) {
    check(std::abs(a - b) < 1e-6, "Independent export coordinate oracle");
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Unsafe OBJ publication must reject");
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
Document fixture() {
    Document doc;
    Edit edit{"Export fixture", {}};
    auto parent = std::make_shared<Body>();
    parent->id = 1;
    parent->name = "Parent with spaces";
    parent->kind = BodyKind::Group;
    parent->transform = Transform::translation({10, 20, 30});
    edit.changes.push_back({1, nullptr, parent});
    auto mesh = std::make_shared<Body>();
    mesh->id = 2;
    mesh->parent = 1;
    mesh->name = "../../unsafe name\nmtllib /etc/passwd";
    mesh->transform = Transform::scaling({-2, 3, 1});
    const auto face =
        mesh->surface.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 1, 0}, {1, 1, 0}, {1, 2, 0}, {0, 2, 0}}});
    mesh->materials = {1, 2};
    mesh->topology = Topology::rebuild(mesh->surface, {});
    mesh->hidden = true;
    mesh->faceTextureMappings[face].front = planarTextureMapping({}, {0, 0, 1}, {1, 0, 0}, 1, 1);
    edit.changes.push_back({2, nullptr, mesh});
    edit.nextIdFloor = 3;
    auto front = std::make_shared<MaterialRecord>();
    front->id = 1;
    front->name = "Front";
    front->color = {.5, .75, 1};
    front->opacity = .8;
    front->asset = 1;
    auto back = std::make_shared<MaterialRecord>();
    back->id = 2;
    back->name = "Back";
    back->color = {1, 0, 0};
    edit.materials = {{1, nullptr, front}, {2, nullptr, back}};
    edit.nextMaterialFloor = 3;
    auto image = std::make_shared<AssetRecord>();
    image->id = 1;
    image->name = "../../../texture.png";
    image->mediaType = "image/png";
    const auto png = encodeTexturePng(TextureImage(1, 1, {128, 64, 32, 255}));
    image->payload =
        std::make_shared<AssetPayload>(std::vector<std::uint8_t>(png.begin(), png.end()));
    edit.assets = {{1, nullptr, image}};
    edit.nextAssetFloor = 2;
    doc.apply(std::move(edit), doc.revision());
    return doc;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir dir;
        check(dir.isValid(), "Scratch directory");
        auto doc = fixture();
        const auto original = encodeDocument(doc);
        const auto history = doc.history().total;
        const auto package = exportObj(doc, {.001, ObjUpAxis::Y});
        check(package.obj.contains("o body-2\n") && !package.obj.contains("/etc/passwd") &&
                  !package.mtl.contains("../"),
              "Untrusted names never become directives or filesystem paths");
        check(package.manifest["faces"].toInt() == 1 &&
                  package.manifest["losses"].toObject()["backFaceAppearanceOmitted"].toInt() == 1,
              "Concave face and explicit back-face loss");
        check(package.manifest["losses"].toObject()["hierarchyFlattened"].toInt() == 1,
              "Nested hierarchy loss");
        const auto output = dir.path() + "/package";
        writeObjExport(package, output);
        if (app.arguments().contains("--blender-real")) {
            const auto blender = qEnvironmentVariable("SKETCHYUP_BLENDER_TEST");
            if (blender.isEmpty())
                return 77;
            QProcess consumer;
            consumer.start(blender, {"--background", "--factory-startup", "--disable-autoexec",
                                     "--python-exit-code", "1", "--python",
                                     QStringLiteral(SOURCE_DIR "/tests/obj_blender_consumer.py"),
                                     "--", output + "/model.obj"});
            check(consumer.waitForStarted(10000) && consumer.waitForFinished(90000) &&
                      consumer.exitStatus() == QProcess::NormalExit && consumer.exitCode() == 0,
                  "Actual Blender OBJ consumer");
            check(consumer.readAllStandardOutput().contains("SKETCHYUP_OBJ_CONSUMER_VERIFIED"),
                  "Independent Blender assertions completed");
            std::cout
                << "Blender verified OBJ geometry, UV, texture, units, reflection and opacity\n";
            return 0;
        }
        check(QJsonDocument::fromJson(read(output + "/manifest.json")).object() == package.manifest,
              "Manifest published intact");
        const auto imported = loadObj(output + "/model.obj", {.001, ObjUpAxis::Y});
        size_t faces = 0;
        bool tip = false;
        for (const auto &[id, body] : imported.document.bodies()) {
            (void)id;
            if (body->kind != BodyKind::Geometry)
                continue;
            faces += body->surface.faces.size();
            near(body->surface.area(body->surface.faces.begin()->first), 18);
            near(body->surface.normal(body->surface.faces.begin()->first).z, 1);
            for (const auto &[v, p] : body->surface.vertices) {
                (void)v;
                near(p.z, 30);
                tip |= length(p - Vec3{6, 20, 30}) < 1e-6;
            }
            const auto mapping = *body->faceTextureMappings.begin()->second.front;
            const auto uv = mapping.coordinates({6, 20, 30});
            near(uv.u, 2);
            near(uv.v, 0);
        }
        check(faces == 1 && tip,
              "World placement, reflection, winding and nonuniform scale round trip");
        const auto material = imported.document.materials().begin()->second;
        near(material->color[0], .5);
        near(material->opacity, .8);
        check(imported.document.assets().size() == 1 &&
                  decodeTextureImage(*imported.document.assets().begin()->second).status ==
                      TextureImageStatus::Ready,
              "Packaged image round trip");
        const auto moved = dir.path() + "/relocated";
        check(QDir().rename(output, moved), "Relocate package");
        check(loadObj(moved + "/model.obj", {.001, ObjUpAxis::Y}).document.assets().size() == 1,
              "Relative sidecars survive relocation");
        rejects([&] { writeObjExport(package, moved); });
        check(read(moved + "/model.obj") == package.obj, "Existing package remains intact");
        check(QFile::link(moved, dir.path() + "/link"), "Output symlink fixture");
        rejects([&] { writeObjExport(package, dir.path() + "/link"); });
        auto corrupt = package;
        corrupt.obj += ' ';
        rejects([&] { writeObjExport(corrupt, dir.path() + "/corrupt"); });
        check(!QFileInfo::exists(dir.path() + "/corrupt"), "Manifest mismatch leaves no directory");
        corrupt = package;
        corrupt.textures["../outside.png"] = "bad";
        rejects([&] { writeObjExport(corrupt, dir.path() + "/escape"); });
        check(encodeDocument(doc) == original && doc.history().total == history,
              "Export leaves model and history unchanged");
        auto missing = doc;
        auto image = std::make_shared<AssetRecord>(*doc.assets().at(1));
        image->payload.reset();
        Edit e{"Missing image", {}};
        e.assets = {{1, doc.assets().at(1), image}};
        missing.apply(std::move(e), missing.revision());
        const auto fallback = exportObj(missing, {1, ObjUpAxis::Z});
        check(fallback.textures.empty() &&
                  fallback.manifest["losses"].toObject()["missingTextures"].toInt() == 1,
              "Missing image has swatch fallback and report");
        Document holes;
        auto b = std::make_shared<Body>();
        b->id = 1;
        b->surface.addFace({{{0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {0, 4, 0}},
                            {{1, 1, 0}, {1, 3, 0}, {3, 3, 0}, {3, 1, 0}}});
        b->topology = Topology::rebuild(b->surface, {});
        Edit h{"Hole", {{1, nullptr, b}}};
        h.nextIdFloor = 2;
        holes.apply(std::move(h), holes.revision());
        const auto hp = exportObj(holes, {1, ObjUpAxis::Z});
        check(hp.manifest["losses"].toObject()["facesTriangulated"].toInt() == 1,
              "Holed faces triangulate explicitly");
        writeObjExport(hp, dir.path() + "/holes");
        const auto hi = loadObj(dir.path() + "/holes/model.obj", {1, ObjUpAxis::Z});
        double area = 0;
        for (const auto &[id, body] : hi.document.bodies()) {
            (void)id;
            for (const auto &[f, face] : body->surface.faces) {
                (void)face;
                area += body->surface.area(f);
            }
        }
        near(area, 12);
        Document separate;
        auto seams = std::make_shared<Body>();
        seams->id = 1;
        seams->surface.vertices = {{1, {0, 0, 0}}, {2, {1, 0, 0}},  {3, {0, 1, 0}},
                                   {4, {0, 0, 0}}, {5, {-1, 0, 0}}, {6, {0, 0, 1}}};
        seams->surface.nextId = 7;
        seams->surface.addFaceIds({{1, 2, 3}});
        seams->surface.addFaceIds({{4, 5, 6}});
        seams->topology = Topology::rebuild(seams->surface, {});
        Edit se{"Separate coincident vertices", {{1, nullptr, seams}}};
        se.nextIdFloor = 2;
        separate.apply(std::move(se), separate.revision());
        const auto sp = exportObj(separate, {1, ObjUpAxis::Z});
        check(parseObj(sp.obj, {1, ObjUpAxis::Z}).vertices.size() == 6,
              "Distinct coincident native vertices retain separate OBJ indices");
        Document wires;
        auto w = std::make_shared<Body>();
        w->id = 1;
        w->surface.vertices = {{1, {0, 0, 0}}, {2, {1, 2, 3}}};
        w->surface.nextId = 3;
        w->surface.wires = {{1, 2}};
        w->topology = Topology::rebuild(w->surface, {});
        Edit we{"Wire", {{1, nullptr, w}}};
        we.nextIdFloor = 2;
        wires.apply(std::move(we), wires.revision());
        writeObjExport(exportObj(wires, {1, ObjUpAxis::Z}), dir.path() + "/wires");
        check(loadObj(dir.path() + "/wires/model.obj", {1, ObjUpAxis::Z})
                      .report["wireSegments"]
                      .toInt() == 1,
              "Wire-only package round trip");
        std::cout << "OBJ package, native round-trip, transforms, holes, materials and publication "
                     "checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
