#include "core/assets.hpp"
#include "core/components.hpp"
#include "core/face_orientation.hpp"
#include "core/groups.hpp"
#include "core/materials.hpp"
#include "core/source_credits.hpp"
#include "core/tags.hpp"
#include "dem_export_fixture.hpp"
#include "integrations/glb_export.hpp"
#include "io/document_io.hpp"
#include "report_fixtures.hpp"
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
// The message of the exception operation throws, or empty when it does not throw.
template <class F> std::string refusal(F operation) {
    try {
        operation();
    } catch (const std::exception &error) {
        return error.what();
    }
    return {};
}
// R099.f: the limits a large capture meets. A terrain's triangles count against the terrain class
// (ADR 0168), not the 100,000-face modelling limit. The manifest bound is 128 MiB: exactly the
// bound is written, one byte more is refused by name. The render snapshot keeps its 20,000-body
// limit: 6,666 placements of the 12-face part are 19,998 bodies, and 6,667 are 20,001.
// R107.c1 (ADR 0181 §3.3, §3.4, V6): a scene that holds DEM terrain carries the credit in asset.copyright
// and in the manifest, once per notice, in document order. A scene without visible terrain has neither.
void checkDemCredits() {
    const std::string fixtures = std::string(SOURCE_DIR) + "/tests/fixtures/geotiff/";
    const auto usgs = QString::fromUtf8(demCreditUsgs), glo30 = QString::fromUtf8(demCreditGlo30);
    Document terrain;
    importDemFixture(terrain, fixtures, "plane-f32-strip-none-le.tif", DemProduct::Dep3, "NAVD88");
    const auto scene = exportGlb(RenderSnapshot::capture(terrain));
    const Parsed parsed(scene);
    check(parsed.root["asset"].toObject()["copyright"].toString() == usgs,
          "GLB asset.copyright holds the 3DEP credit exactly");
    check(scene.manifest["credits"] == QJsonArray{usgs}, "GLB manifest lists the 3DEP credit");
    check(parsed.root["asset"].toObject()["generator"] == "SketchyUp GLB snapshot v1",
          "The asset keeps its version and generator");

    Document joined;
    importDemFixture(joined, fixtures, "plane-f32-strip-none-le.tif", DemProduct::Dep3, "NAVD88");
    importDemFixture(joined, fixtures, "plane-f32-strip-none-le.tif", DemProduct::Dep3, "NAVD88");
    importDemFixture(joined, fixtures, "glo30-like-pixel-point-f32.tif", DemProduct::Glo30, "EGM2008",
                     0.5, "Test fixture: an EGM2008 terrain on a NAVD88 site, offset by hand");
    const auto both = exportGlb(RenderSnapshot::capture(joined));
    check(Parsed(both).root["asset"].toObject()["copyright"].toString() == usgs + "; " + glo30,
          "Two sources are joined in document order, and the 3DEP notice appears once");
    check(both.manifest["credits"] == QJsonArray{usgs, glo30}, "The manifest lists each notice once");

    // Hidden terrain is not in the scene. The box keeps the scene non-empty.
    auto mixed = box();
    importDemFixture(mixed, fixtures, "plane-f32-strip-none-le.tif", DemProduct::Dep3, "NAVD88");
    SelectionSet hidden;
    for (const auto &[id, body] : mixed.bodies())
        if (body->kind == BodyKind::Terrain)
            hidden.insert({id, SelectionKind::Body, 0});
    const auto shown = exportGlb(RenderSnapshot::capture(mixed));
    check(Parsed(shown).root["asset"].toObject().contains("copyright"),
          "A visible DEM terrain puts its credit in the scene");
    const auto without = exportGlb(RenderSnapshot::capture(mixed, {}, hidden));
    check(!Parsed(without).root["asset"].toObject().contains("copyright") &&
              !without.manifest.contains("credits"),
          "A hidden DEM terrain leaves no credit");
    check(!Parsed(exportGlb(RenderSnapshot::capture(box()))).root["asset"].toObject().contains("copyright"),
          "A scene without DEM terrain has no asset.copyright");

    // The notice is one line and joins as the record writes it (ADR 0181 §3.1).
    SourceCredits credits;
    TerrainDemSource dem;
    dem.credit = "Credit: U.S. Geological Survey";
    credits.add(dem);
    credits.add(dem);
    check(credits.notices().size() == 1 && credits.joined() == "Credit: U.S. Geological Survey",
          "A repeated notice is written once");
}

void checkLargeCaptureLimits(const QString &directory) {
    const auto mesh = report_fixtures::flatGrid(230, 1.); // 105,800 triangles, over 100,000 faces.
    Document terrain;
    createTerrain(terrain, mesh, report_fixtures::terrainRecord(mesh));
    check(exportGlb(RenderSnapshot::capture(terrain)).manifest["visibleTriangles"] == 105800,
          "A terrain over 100,000 faces exports under the terrain class");
    const auto scene = [](qsizetype padding) {
        GlbExport result;
        result.glb = QByteArray("glTF");
        const auto digest = QString::fromLatin1(
            QCryptographicHash::hash(result.glb, QCryptographicHash::Sha256).toHex());
        result.manifest = QJsonObject{{"scene", QJsonObject{{"file", "scene.glb"},
                                                            {"bytes", qint64(result.glb.size())},
                                                            {"sha256", digest}}},
                                      {"padding", QString(padding, QChar('x'))}};
        return result;
    };
    // The indented manifest is the published size; the padding adds one byte per character.
    const auto room = glbManifestByteLimit - QJsonDocument(scene(0).manifest).toJson().size();
    check(refusal([&] { writeGlbExport(scene(room + 1), directory + "/over"); }) ==
              "GLB manifest exceeds 128 MiB",
          "A manifest one byte over 128 MiB is refused by name");
    writeGlbExport(scene(room), directory + "/exact");
    check(QFileInfo(directory + "/exact/manifest.json").size() == glbManifestByteLimit,
          "A manifest of exactly 128 MiB is written");
    Document parts, over;
    report_fixtures::buildPartsModel(parts, 6666, false);
    report_fixtures::buildPartsModel(over, 6667, false);
    check(parts.bodies().size() == 19998 && over.bodies().size() == 20001,
          "The placement counts give 19,998 and 20,001 bodies");
    check(refusal([&] { RenderSnapshot::capture(parts); }).empty(),
          "A 19,998-body capture is accepted");
    check(refusal([&] { RenderSnapshot::capture(over); }) ==
              "Render snapshot contains too many records",
          "A 20,001-body capture is refused by name");
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
        check(!losses.contains("differentBackAppearancesUseFront") &&
                  colored.manifest["facesWithDistinctSides"] == 6 &&
                  colored.manifest["visibleTriangles"] == 24 &&
                  losses["textureAssetsPreservedWithoutUVMapping"] == 2 &&
                  losses["missingAssets"] == 1,
              "Appearance losses explicit");
        const auto mat = materialParsed.root["materials"].toArray()[0].toObject();
        check(mat["doubleSided"] == false && mat["alphaMode"] == "BLEND" &&
                  materialParsed.root["materials"].toArray().size() == 2,
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
        // The same material may be front in one pair and back in another.
        // Repeated reflected components must still share their paired mesh.
        auto mixed = box();
        const auto red = createMaterial(mixed, "Red", {1, 0, 0});
        const auto blue = createMaterial(mixed, "Blue", {0, 0, 1});
        assignMaterial(mixed, 1, {}, red, true, false);
        assignMaterial(mixed, 1, {}, blue, false, true);
        const auto reversedFace = mixed.bodies().at(1)->surface.faces.begin()->first;
        reverseSelectedFaces(mixed, {{1, SelectionKind::Face, reversedFace}}, 0);
        const auto mixedGroup = createGroup(mixed, {1}, "Sided assembly");
        const auto mixedComponent = createComponent(mixed, mixedGroup, "Sided component");
        placeComponent(mixed, mixedComponent.definition,
                       Transform::translation({6, 0, 0}) * Transform::scaling({-1, 1, 1}));
        const auto mixedScene = exportGlb(RenderSnapshot::capture(mixed));
        Parsed mixedParsed(mixedScene);
        check(mixedScene.manifest["uniqueMeshes"] == 1 &&
                  mixedScene.manifest["visibleTriangles"] == 48 &&
                  mixedParsed.root["materials"].toArray().size() == 4,
              "Opposite appearance roles are distinct and repeated sided meshes share storage");
        writeGlbExport(mixedScene, fixtures + "/mixed-sides");
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
        // Decode both sides independently. Native reversal must retain the red
        // physical +Z side even through a reflected, rotated, nonuniform placement.
        for (int variant = 0; variant < 6; ++variant) {
            Document plane;
            std::vector<std::vector<Vec3>> loops{{{-2, -2, 0}, {2, -2, 0}, {2, 2, 0}, {-2, 2, 0}}};
            if (variant == 5)
                loops.push_back({{-.5, -.5, 0}, {-.5, .5, 0}, {.5, .5, 0}, {.5, -.5, 0}});
            const auto owner = plane.addFace(loops);
            const auto face = plane.bodies().at(owner)->surface.faces.begin()->first;
            const auto red =
                           createMaterial(plane, "Physical front", {1, 0, 0}, variant == 4 ? 0 : 1),
                       blue = createMaterial(plane, "Physical back", {0, 0, 1});
            assignMaterial(plane, owner, face, red, true, false);
            assignMaterial(plane, owner, face, blue, false, true);
            const auto placement = Transform::translation({3, -2, 1}) *
                                   Transform::rotation({1, 2, 3}, .4) *
                                   Transform::scaling({variant & 2 ? -1.5 : 1.5, .8, 1.2});
            plane.transform(owner, placement);
            if (variant & 1)
                reverseSelectedFaces(plane, {{owner, SelectionKind::Face, face}}, 0);
            const auto original = encodeDocument(plane);
            const auto result = exportGlb(RenderSnapshot::capture(plane));
            Parsed sides(result);
            const auto materials = sides.root["materials"].toArray();
            check(materials.size() == 2 && result.manifest["facesWithDistinctSides"] == 1 &&
                      encodeDocument(plane) == original,
                  "Two independent appearances, immutable export");
            const auto map =
                result.manifest["bodies"].toArray()[0].toObject()["faces"].toArray()[0].toObject();
            const auto primitives =
                sides.root["meshes"].toArray()[0].toObject()["primitives"].toArray();
            auto values = [&](int primitive, const char *name) {
                const auto attributes = primitives[primitive].toObject()["attributes"].toObject();
                const auto accessor =
                    sides.root["accessors"].toArray()[attributes[name].toInt()].toObject();
                const auto view =
                    sides.root["bufferViews"].toArray()[accessor["bufferView"].toInt()].toObject();
                std::vector<Vec3> values;
                for (int i = 0; i < accessor["count"].toInt(); ++i) {
                    auto value = [&](int axis) {
                        return double(std::bit_cast<float>(
                            word(sides.binary, view["byteOffset"].toInt() + i * 12 + axis * 4)));
                    };
                    values.push_back({value(0), value(1), value(2)});
                }
                return values;
            };
            const auto fp = values(map["primitive"].toInt(), "POSITION"),
                       bp = values(map["backPrimitive"].toInt(), "POSITION"),
                       fn = values(map["primitive"].toInt(), "NORMAL"),
                       bn = values(map["backPrimitive"].toInt(), "NORMAL");
            check(fp.size() == bp.size() && fp.size() == size_t(map["vertexCount"].toInt()) &&
                      map["vertexCount"] == map["backVertexCount"] && map["firstVertex"] == 0 &&
                      map["backFirstVertex"] == 0,
                  "Both native-face ranges cover their triangles");
            for (size_t i = 0; i < fp.size(); i += 3) {
                check(fp[i] == bp[i] && fp[i + 1] == bp[i + 2] && fp[i + 2] == bp[i + 1],
                      "Back triangles have exactly opposite winding");
                near(dot(normalized(cross(fp[i + 1] - fp[i], fp[i + 2] - fp[i])), fn[i]), 1,
                     "Exported normal agrees with winding");
                near(dot(fn[i], bn[i]), -1, "Back normal is opposite");
            }
            const auto inverse = placement.inverse();
            auto worldNormal = [&](Vec3 n) {
                return normalized(Vec3{dot(inverse.vector({1, 0, 0}), n),
                                       dot(inverse.vector({0, 1, 0}), n),
                                       dot(inverse.vector({0, 0, 1}), n)});
            };
            const bool frontIsRed = dot(worldNormal(fn[0]), worldNormal({0, 0, 1})) > 0;
            const auto actual =
                materials[frontIsRed ? map["material"].toInt() : map["backAppearance"].toInt()]
                    .toObject();
            check(actual["extras"].toObject()["sketchyupMaterial"] == QString::number(red),
                  "Red remains on original physical front after reversal/reflection");
            writeGlbExport(result, fixtures + "/sides-" + QString::number(variant));
        }
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
        // R107.c1: the CLI prints the GLB manifest, which lists the notice of the DEM terrain it writes.
        {
            Document terrainDocument;
            importDemFixture(terrainDocument, std::string(SOURCE_DIR) + "/tests/fixtures/geotiff/",
                             "plane-f32-strip-none-le.tif", DemProduct::Dep3, "NAVD88");
            const auto demNative = files.path() + "/dem.sketchyup";
            saveDocument(terrainDocument, demNative);
            const auto printed = cli({"--input", demNative, "--export-glb", files.path() + "/dem-cli"});
            const auto credits = QJsonDocument::fromJson(printed.second).object()["credits"].toArray();
            check(printed.first == 0 && credits == QJsonArray{QString::fromUtf8(demCreditUsgs)},
                  "The CLI's GLB report lists the DEM notice");
        }
#endif
        checkDemCredits();
        checkLargeCaptureLimits(files.path());
        std::cout
            << "GLB snapshot, geometry, hierarchy, assets, cameras and publication checks passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
