#include "core/assets.hpp"
#include "core/components.hpp"
#include "core/groups.hpp"
#include "core/materials.hpp"
#include "core/tags.hpp"
#include "integrations/glb_export.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QtEndian>
#include <bit>
#include <future>
#include <iostream>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F operation) {
    bool failed = false;
    try {
        operation();
    } catch (const std::exception &) {
        failed = true;
    }
    check(failed, "Invalid export must fail");
}
void near(double a, double b, const char *message) { check(std::abs(a - b) < 1e-5, message); }
quint32 word(const QByteArray &bytes, qsizetype offset) {
    check(offset >= 0 && offset + 4 <= bytes.size(), "GLB word bounds");
    return qFromLittleEndian<quint32>(bytes.constData() + offset);
}
struct Parsed {
    QJsonObject root;
    QByteArray binary;
    explicit Parsed(const GlbExport &scene) {
        const auto &bytes = scene.glb;
        check(word(bytes, 0) == 0x46546c67 && word(bytes, 4) == 2 && word(bytes, 8) == bytes.size(),
              "GLB header");
        const auto jsonLength = word(bytes, 12);
        check(jsonLength % 4 == 0 && word(bytes, 16) == 0x4e4f534a, "JSON chunk alignment/type");
        root = QJsonDocument::fromJson(bytes.mid(20, jsonLength)).object();
        check(!root.empty(), "GLB JSON parses");
        const auto offset = 20 + jsonLength;
        check(word(bytes, offset) % 4 == 0 && word(bytes, offset + 4) == 0x004e4942 &&
                  offset + 8 + word(bytes, offset) == bytes.size(),
              "Binary chunk bounds/alignment");
        binary = bytes.mid(offset + 8);
        check(root["buffers"].toArray().size() == 1 &&
                  root["buffers"].toArray()[0].toObject()["byteLength"].toInteger() <=
                      binary.size(),
              "One embedded buffer");
        for (auto v : root["bufferViews"].toArray()) {
            const auto view = v.toObject();
            check(view["byteOffset"].toInt() % 4 == 0 && view["byteLength"].toInt() > 0 &&
                      view["byteOffset"].toInteger() + view["byteLength"].toInteger() <=
                          binary.size(),
                  "Every buffer view is aligned and contained");
        }
        for (auto v : root["accessors"].toArray()) {
            const auto a = v.toObject();
            const auto view = root["bufferViews"].toArray()[a["bufferView"].toInt()].toObject();
            check(a["componentType"] == 5126 && a["type"] == "VEC3" && a["count"].toInt() > 0 &&
                      a["count"].toInt() * 12 == view["byteLength"].toInt(),
                  "Attribute type/count/stride");
            for (int i = 0; i < a["count"].toInt(); ++i)
                for (int axis = 0; axis < 3; ++axis) {
                    const auto number = std::bit_cast<float>(
                        word(binary, view["byteOffset"].toInt() + 12 * i + 4 * axis));
                    check(std::isfinite(number), "Finite float32 attribute");
                    if (a.contains("min"))
                        check(number >= a["min"].toArray()[axis].toDouble() &&
                                  number <= a["max"].toArray()[axis].toDouble(),
                              "Float32 positions fit accessor bounds");
                }
        }
    }
};
Document box() {
    Document doc;
    const Id body = doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}});
    doc.extrude(body, doc.bodies().at(body)->surface.faces.begin()->first, 4);
    return doc;
}
QByteArray read(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Read artifact");
    return file.readAll();
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir files;
        check(files.isValid(), "Export test directory");
        const QString fixtures =
            argc > 1 ? QString::fromLocal8Bit(argv[1]) : files.path() + "/fixtures";
        check(QDir().mkdir(fixtures), "Create fresh interop fixture directory");
        auto doc = box();
        const auto before = encodeDocument(doc);
        const auto stamp = doc.saveStamp();
        const auto captured = RenderSnapshot::capture(doc);
        const auto scene = exportGlb(captured);
        Parsed parsed(scene);
        check(encodeDocument(doc) == before && doc.isCurrentSnapshot(stamp),
              "Capture/export does not mutate source");
        check(scene.manifest["visibleTriangles"] == 12 &&
                  parsed.root["meshes"].toArray().size() == 1,
              "Box exports twelve triangles");
        const auto bounds = scene.manifest["nativeBounds"].toObject();
        check(bounds["min"] == QJsonArray({0, 0, 0}) && bounds["max"] == QJsonArray({2, 3, 4}),
              "Known-size native bounds");
        const auto basis = parsed.root["nodes"].toArray()[0].toObject()["matrix"].toArray();
        check(basis[6] == -1 && basis[9] == 1, "Proper root rotation maps Z-up to Y-up");
        doc.move(1, {10, 0, 0});
        const auto worker =
            std::async(std::launch::async, [captured] { return exportGlb(captured); }).get();
        check(worker.glb == scene.glb && worker.manifest == scene.manifest &&
                  captured.document().revision() != doc.revision(),
              "Worker snapshot remains at captured revision during later edits");
        writeGlbExport(scene, fixtures + "/box");
        check(read(fixtures + "/box/scene.glb") == scene.glb, "Published GLB bytes unchanged");
        rejects([&] { writeGlbExport(scene, fixtures + "/box"); });
        check(read(fixtures + "/box/scene.glb") == scene.glb, "Existing export never overwritten");
        auto corrupt = scene;
        corrupt.glb.append('x');
        rejects([&] { writeGlbExport(corrupt, fixtures + "/corrupt"); });
        check(!QFileInfo::exists(fixtures + "/corrupt"),
              "Mismatched manifest never creates output");
        rejects([&] { writeGlbExport(scene, fixtures + "/absent/child"); });
        auto instances = box();
        const auto root = createGroup(instances, {1}, "Box assembly");
        const auto component = createComponent(instances, root, "Shared box");
        const auto reflected =
            placeComponent(instances, component.definition,
                           Transform::translation({6, 0, 0}) * Transform::scaling({-1, 1, 1}));
        const auto shared = exportGlb(RenderSnapshot::capture(instances));
        Parsed sharedParsed(shared);
        check(shared.manifest["uniqueMeshes"] == 1 && shared.manifest["visibleTriangles"] == 24,
              "Reflected instances share one GLB mesh");
        int meshNodes = 0, negativeNodes = 0;
        for (auto value : sharedParsed.root["nodes"].toArray()) {
            const auto node = value.toObject();
            if (node.contains("mesh")) {
                ++meshNodes;
                check(node["mesh"] == 0, "Shared mesh node");
            }
            const auto transform = node.value("matrix").toArray();
            if (transform.size() == 16 && transform[0] == -1)
                ++negativeNodes;
        }
        check(meshNodes == 2 && negativeNodes == 1, "Mirrored placement and hierarchy retained");
        writeGlbExport(shared, fixtures + "/mirrored");
        setEntityState(instances, reflected.instance, true, {});
        auto hidden = exportGlb(RenderSnapshot::capture(instances));
        check(hidden.manifest["visibleTriangles"] == 12,
              "Ancestor visibility hides instance geometry");
        const auto tag = createTag(instances, "Hidden");
        assignTag(instances, reflected.instance, tag);
        setEntityState(instances, reflected.instance, false, {});
        editTag(instances, tag, {}, {}, false);
        check(exportGlb(RenderSnapshot::capture(instances)).manifest["visibleTriangles"] == 12,
              "Tag visibility hides instance geometry");
        const auto face = instances.bodies().at(1)->surface.faces.begin()->first;
        hidden =
            exportGlb(RenderSnapshot::capture(instances, {}, {{1, SelectionKind::Face, face}}));
        check(hidden.manifest["visibleTriangles"] == 10 &&
                  hidden.manifest["hiddenFacesOmitted"] == 1,
              "Transient face hiding captured");
        rejects([&] {
            exportGlb(RenderSnapshot::capture(instances, {}, {{root, SelectionKind::Body, 0}}));
        });
        auto appearance = box();
        const std::vector<std::uint8_t> bytes{1, 2, 3, 4, 5, 6, 7};
        const auto asset = createAsset(appearance, "Embedded swatch", "application/octet-stream",
                                       std::make_shared<AssetPayload>(bytes));
        const auto missing = createAsset(appearance, "Missing swatch", "image/png");
        const auto front = createMaterial(appearance, "Glass", {.5f, .25f, .75f}, .35f, asset),
                   back = createMaterial(appearance, "Back", {1, 0, 0}, 1, missing);
        assignMaterial(appearance, 1, {}, front, true, false);
        assignMaterial(appearance, 1, {}, back, false, true);
        const auto materialSnapshot = RenderSnapshot::capture(appearance);
        const auto colored = exportGlb(materialSnapshot);
        Parsed materialParsed(colored);
        const auto losses = colored.manifest["losses"].toObject();
        check(losses["differentBackAppearancesUseFront"] == 6 &&
                  losses["textureAssetsPreservedWithoutUVMapping"] == 2 &&
                  losses["missingAssets"] == 1,
              "Appearance losses explicit");
        const auto mat = materialParsed.root["materials"].toArray()[0].toObject();
        check(mat["doubleSided"] == true && mat["alphaMode"] == "BLEND",
              "Opacity and double-sided policy");
        near(mat["pbrMetallicRoughness"].toObject()["baseColorFactor"].toArray()[0].toDouble(),
             .21404114, "sRGB swatch converts to linear PBR");
        near(mat["pbrMetallicRoughness"].toObject()["baseColorFactor"].toArray()[3].toDouble(), .35,
             "Alpha remains linear");
        const auto embedded = colored.manifest["assets"].toArray()[0].toObject();
        check(materialParsed.binary.mid(embedded["byteOffset"].toInt(),
                                        embedded["byteLength"].toInt()) ==
                  QByteArray(reinterpret_cast<const char *>(bytes.data()), bytes.size()),
              "Packaged asset bytes embedded exactly");
        check(!embedded.contains("path") && !embedded.contains("uri"),
              "No external source path retained");
        const auto packaged = decodeContainer(encodeContainer(appearance));
        check(exportGlb(RenderSnapshot::capture(packaged)).glb == colored.glb,
              "Packaged native assets resolve without external source files");
        replaceAsset(appearance, asset,
                     std::make_shared<AssetPayload>(std::vector<std::uint8_t>{9}));
        check(exportGlb(materialSnapshot).glb == colored.glb,
              "Later asset replacement cannot change captured payload");
        writeGlbExport(colored, fixtures + "/materials");
        RenderOptions orthographic;
        RenderCamera camera;
        camera.orthographic = true;
        camera.position = {8, -9, 7};
        camera.target = {1, 1.5, 2};
        camera.yMag = 6;
        orthographic.camera = camera;
        orthographic.settings.width = 1200;
        orthographic.settings.height = 600;
        const auto ortho = exportGlb(RenderSnapshot::capture(box(), orthographic));
        Parsed orthoParsed(ortho);
        const auto projection =
            orthoParsed.root["cameras"].toArray()[0].toObject()["orthographic"].toObject();
        near(projection["xmag"].toDouble(), 12, "Orthographic aspect ratio");
        near(projection["ymag"].toDouble(), 6, "Orthographic vertical size");
        const auto cameraTransform = renderCameraTransform(camera);
        near(length(cameraTransform.point({}) - camera.position), 0, "Camera origin");
        near(length(cameraTransform.vector({0, 0, -1}) -
                    normalized(camera.target - camera.position)),
             0, "Camera points along negative local Z");
        writeGlbExport(ortho, fixtures + "/orthographic");
        auto distant = box();
        distant.move(1, {999997, 0, 0});
        check(exportGlb(RenderSnapshot::capture(distant)).manifest["visibleTriangles"] == 12,
              "Auto-fit camera may lie outside modeling coordinate bounds");
        auto shear = box();
        Transform skew;
        skew.m[4] = .5;
        shear.transform(1, skew);
        rejects([&] { exportGlb(RenderSnapshot::capture(shear)); });
        rejects([] { exportGlb(RenderSnapshot::capture(Document{})); });
        for (auto bad : QJsonArray{QJsonObject{}, QJsonObject{{"apiVersion", 2}},
                                   QJsonObject{{"apiVersion", 1}, {"width", 1}},
                                   QJsonObject{{"apiVersion", 1}, {"samples", 1.5}},
                                   QJsonObject{{"apiVersion", 1}, {"script", "evil"}},
                                   QJsonObject{{"apiVersion", 1}, {"camera", true}}})
            rejects([&] { parseRenderOptions(bad.toObject()); });
        auto options = parseRenderOptions({{"apiVersion", 1},
                                           {"width", 800},
                                           {"height", 600},
                                           {"samples", 16},
                                           {"seed", 42},
                                           {"camera", describeRenderCamera(camera)}});
        check(options.camera && options.camera->orthographic && options.settings.seed == 42,
              "Versioned settings round trip");
        camera.up = camera.position - camera.target;
        rejects([&] { renderCameraTransform(camera); });
        camera.up = {0, 0, 1};
        camera.position = camera.target;
        rejects([&] { renderCameraTransform(camera); });
#ifdef CLI_PATH
        const auto native = files.path() + "/input.sketchyup";
        saveDocument(doc, native);
        const auto nativeBytes = read(native);
        auto cli = [&](QStringList args) {
            QProcess process;
            process.start(QStringLiteral(CLI_PATH), args);
            check(process.waitForStarted(10000) && process.waitForFinished(30000),
                  "Export CLI finishes");
            return std::pair{process.exitCode(), process.readAllStandardOutput()};
        };
        const auto result = cli({"--input", native, "--export-glb", files.path() + "/cli"});
        check(result.first == 0 && read(native) == nativeBytes &&
                  QJsonDocument::fromJson(result.second).object()["documentId"] ==
                      QString::fromStdString(doc.identity()),
              "Headless CLI exports without changing source");
        check(cli({"--input", native, "--export-glb", files.path() + "/cli"}).first != 0,
              "CLI preserves existing output");
        check(cli({"--input", native, "--export-glb", files.path() + "/mixed", "--output", native})
                          .first != 0 &&
                  read(native) == nativeBytes && !QFileInfo::exists(files.path() + "/mixed"),
              "CLI rejects mixed modes before effects");
        check(cli({"--render-settings", "missing"}).first != 0, "Orphan render settings rejected");
#endif
        std::cout
            << "GLB snapshot, geometry, hierarchy, assets, cameras and publication checks passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
