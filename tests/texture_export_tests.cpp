#include "core/assets.hpp"
#include "core/components.hpp"
#include "core/face_orientation.hpp"
#include "core/face_textures.hpp"
#include "core/groups.hpp"
#include "core/materials.hpp"
#include "integrations/glb_export.hpp"
#include "io/assets.hpp"
#include "io/document_io.hpp"
#include "io/texture_image.hpp"
#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtEndian>
#include <bit>
#include <iostream>
#include <limits>
using namespace sketchy;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F operation) {
    try {
        operation();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Invalid texture export must reject");
}
void near(double a, double b, double epsilon = 1e-6) {
    check(std::isfinite(a) && std::abs(a - b) <= epsilon, "Independent exported UV oracle");
}
quint32 word(const QByteArray &bytes, qsizetype offset) {
    check(offset >= 0 && offset + 4 <= bytes.size(), "GLB word within buffer");
    return qFromLittleEndian<quint32>(bytes.constData() + offset);
}
struct Parsed {
    QJsonObject root;
    QByteArray binary;
    explicit Parsed(const GlbExport &scene) {
        check(word(scene.glb, 0) == 0x46546c67 && word(scene.glb, 4) == 2 &&
                  word(scene.glb, 8) == scene.glb.size(),
              "GLB header");
        const auto count = word(scene.glb, 12);
        root = QJsonDocument::fromJson(scene.glb.mid(20, count)).object();
        binary = scene.glb.mid(28 + count);
        check(!root.isEmpty(), "GLB JSON parses");
    }
    QByteArray view(int index) const {
        const auto v = root["bufferViews"].toArray()[index].toObject();
        const int offset = v["byteOffset"].toInt(), size = v["byteLength"].toInt();
        check(offset >= 0 && offset % 4 == 0 && size > 0 && offset + size <= binary.size(),
              "Aligned bounded buffer view");
        return binary.mid(offset, size);
    }
    std::vector<double> attribute(int index, int dimensions) const {
        const auto a = root["accessors"].toArray()[index].toObject();
        check(a["componentType"] == 5126 && a["type"] == (dimensions == 2 ? "VEC2" : "VEC3"),
              "Float coordinate accessor type");
        const auto bytes = view(a["bufferView"].toInt());
        check(bytes.size() == a["count"].toInt() * dimensions * 4, "Coordinate count and stride");
        std::vector<double> result;
        for (qsizetype i = 0; i < bytes.size(); i += 4)
            result.push_back(std::bit_cast<float>(word(bytes, i)));
        return result;
    }
};
QByteArray checker(int variant) {
    const std::array<std::array<std::uint8_t, 4>, 4> a{
        {{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}, {255, 255, 255, 255}}};
    const std::array<std::array<std::uint8_t, 4>, 4> b{
        {{0, 255, 255, 255}, {255, 0, 255, 128}, {255, 255, 0, 0}, {0, 0, 0, 255}}};
    std::vector<std::uint8_t> pixels;
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            const int quadrant = (y >= 8 ? 2 : 0) + (x >= 8 ? 1 : 0);
            auto rgba = variant == 1 ? b[quadrant] : a[quadrant];
            if (variant == 2 && quadrant < 2)
                rgba[3] = quadrant == 0 ? 0 : 128;
            pixels.insert(pixels.end(), rgba.begin(), rgba.end());
        }
    return encodeTexturePng(TextureImage(16, 16, std::move(pixels)));
}
void implicitAndPacking() {
    Document doc;
    const auto horizontal = doc.addFace({{{2, 3, 4}, {4, 3, 4}, {4, 5, 4}, {2, 5, 4}}});
    const auto face = doc.bodies().at(horizontal)->surface.faces.begin()->first;
    const auto implicit = effectiveFaceTextureMapping(*doc.bodies().at(horizontal), face);
    check(implicit == TextureMapping{}, "Horizontal default is body-local XY at one metre/repeat");
    reverseSelectedFaces(doc, {{horizontal, SelectionKind::Face, face}}, 0);
    check(effectiveFaceTextureMapping(*doc.bodies().at(horizontal), face) == implicit &&
              effectiveFaceTextureMapping(*doc.bodies().at(horizontal), face, true) == implicit,
          "Implicit image axes remain unchanged through winding reversal");
    const auto wall = doc.addFace({{{2, 3, 4}, {4, 3, 4}, {4, 3, 6}, {2, 3, 6}}});
    const auto wf = doc.bodies().at(wall)->surface.faces.begin()->first;
    const auto uv =
        effectiveFaceTextureMapping(*doc.bodies().at(wall), wf).coordinates({2.5, 3, 5.5});
    near(uv.u, 2.5);
    near(uv.v, -5.5);
    for (const auto loop : {std::vector<Vec3>{{2, 3, 4}, {4, 3, 4}, {4, 5, 4}},
                            std::vector<Vec3>{{2, 3, 4}, {4, 5, 4}, {2, 5, 4}}}) {
        Body part;
        const auto id = part.surface.addFace({loop});
        check(effectiveFaceTextureMapping(part, id) == implicit,
              "Coplanar subdivisions do not change default phase or basis");
    }
    const auto packed = floatTextureCoordinates(
        {TextureCoordinate{1e9 + .9, -1e9 + .2}, {1e9 + 1.1, -1e9 + .2}, {1e9 + .9, -1e9 + .8}},
        {1e-6, 1e-6});
    near(packed[1][0] - packed[0][0], .2, 2e-7);
    near((packed[0][0] + packed[1][0]) / 2, 1, 1e-7);
    check(packed[1][0] > 1, "Triangle packing preserves interpolation across a repeat boundary");
    rejects([] {
        floatTextureCoordinates({TextureCoordinate{}, {1e9 + .1, 0}, {0, 1}}, {1e-6, 1e-6});
    });
    rejects([] { floatTextureCoordinates({TextureCoordinate{}, {1, 0}, {0, 1}}, {0, 1e-6}); });
    rejects([] {
        // Exact endpoints still need fractional resolution between them.
        floatTextureCoordinates({TextureCoordinate{}, {1e9, 0}, {0, 1}}, {1e-6, 1e-6});
    });
    rejects([] {
        floatTextureCoordinates(
            {TextureCoordinate{}, {0, std::numeric_limits<double>::infinity()}, {0, 1}},
            {1e-6, 1e-6});
    });
}
void fixture(const QString &path, int variant) {
    Document doc;
    std::vector<std::vector<Vec3>> loops{{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}};
    if (variant == 9)
        loops.push_back({{.18, .18, 0}, {.18, .32, 0}, {.32, .32, 0}, {.32, .18, 0}});
    const auto body = doc.addFace(loops);
    const auto face = doc.bodies().at(body)->surface.faces.begin()->first;
    auto source = checker(variant == 6 ? 2 : 0);
    if (variant == 11) {
        const auto image = QImage::fromData(source, "PNG");
        source.clear();
        QBuffer output(&source);
        check(output.open(QIODevice::WriteOnly) && image.save(&output, "JPEG", 100),
              "Synthetic JPEG source");
    }
    const auto first = createAsset(doc, "Front checker", variant == 11 ? "image/jpeg" : "image/png",
                                   assetPayload(source));
    const auto second = createAsset(doc, "Back checker", "image/png", assetPayload(checker(1)));
    const auto a = createMaterial(doc, "Checker A",
                                  variant == 10 ? std::array<float, 3>{.25f, 1, .5f}
                                                : std::array<float, 3>{1, 1, 1},
                                  1, first);
    const auto b = createMaterial(doc, "Checker B",
                                  variant == 10 ? std::array<float, 3>{.5f, .25f, 1}
                                                : std::array<float, 3>{1, 1, 1},
                                  .5, variant == 10 ? first : second);
    const auto plain = createMaterial(doc, "Plain", {.8f, .2f, .1f});
    assignMaterial(doc, body, {}, variant == 3 ? plain : a, true, false);
    assignMaterial(doc, body, {},
                   variant == 4                                                    ? plain
                   : variant == 2 || variant == 3 || variant == 8 || variant == 10 ? b
                                                                                   : a,
                   false, true);
    if (variant == 1 || variant == 5 || variant == 8)
        assignTextureMapping(doc, body, face, TextureMapping{{}, {-1, 0, 0}, {0, 1, 0}, {1, 0}},
                             false, true);
    if (variant == 7)
        assignTextureMapping(
            doc, body, face,
            TextureMapping{{}, {1.5, .25, 0}, {-.125, 2, 0}, {999999999. + .125, -1e9 + .25}});
    if (variant == 8)
        reverseSelectedFaces(doc, {{body, SelectionKind::Face, face}}, 0);
    if (variant == 5) {
        const auto group = createGroup(doc, {body});
        const auto component = createComponent(doc, group);
        placeComponent(doc, component.definition,
                       Transform::translation({3, 0, 0}) * Transform::rotation({1, 2, 3}, .41) *
                           Transform::scaling({-1.5, .75, 2}));
    }
    const auto before = encodeContainer(doc);
    const auto snapshot = RenderSnapshot::capture(doc);
    const auto scene = exportGlb(snapshot);
    const Parsed parsed(scene);
    check(encodeContainer(doc) == before, "Texture export preserves document and source bytes");
    check(scene.manifest["losses"].toObject()["textureAssetsPreservedWithoutUVMapping"] == 0,
          "Every referenced valid image is mapped");
    const bool paired = variant == 1 || variant == 2 || variant == 3 || variant == 4 ||
                        variant == 5 || variant == 8 || variant == 10;
    check(parsed.root["meshes"].toArray().size() == 1 && scene.manifest["uniqueMeshes"] == 1,
          "Reflected component instances share the textured mesh");
    const auto primitives = parsed.root["meshes"].toArray()[0].toObject()["primitives"].toArray();
    check(primitives.size() == (paired ? 2 : 1),
          "Independent side projections require paired geometry");
    check(parsed.root["extras"].toObject()["sketchyupSidedMaterials"].toObject()["version"] == 2,
          "Textured snapshots advertise the supported sided adapter version");
    for (auto value : primitives) {
        const auto primitive = value.toObject(), attrs = primitive["attributes"].toObject();
        const int material = primitive["material"].toInt();
        const auto m = parsed.root["materials"].toArray()[material].toObject();
        const auto pbr = m["pbrMetallicRoughness"].toObject();
        check(m["doubleSided"] == !paired, "Paired surfaces are single-sided");
        check(attrs.contains("TEXCOORD_0") == pbr.contains("baseColorTexture"),
              "Every textured primitive has UVs and plain primitives do not invent them");
        if (!attrs.contains("TEXCOORD_0"))
            continue;
        const auto positions = parsed.attribute(attrs["POSITION"].toInt(), 3);
        const auto uv = parsed.attribute(attrs["TEXCOORD_0"].toInt(), 2);
        check(positions.size() * 2 == uv.size() * 3, "Per-vertex UV count");
        const bool mirror =
            ((variant == 1 || variant == 5) && material == 1) ||
            (variant == 8 && m["extras"].toObject()["sketchyupMaterial"] == QString::number(b));
        auto oracle = [&](size_t vertex) {
            const double x = positions[vertex * 3], y = positions[vertex * 3 + 1];
            return variant == 7 ? TextureCoordinate{999999999. + .125 + 1.5 * x + .25 * y,
                                                    -1e9 + .25 - .125 * x + 2 * y}
                                : TextureCoordinate{mirror ? 1 - x : x, y};
        };
        for (size_t start = 0; start < positions.size() / 3; start += 3) {
            const auto anchor = oracle(start);
            for (size_t i = start; i < start + 3; ++i) {
                const auto expected = oracle(i);
                near(uv[i * 2], expected.u - std::floor(anchor.u));
                near(uv[i * 2 + 1], expected.v - std::floor(anchor.v));
            }
        }
    }
    for (auto value : scene.manifest["assets"].toArray()) {
        const auto entry = value.toObject();
        const auto id = entry["id"].toString().toULongLong();
        check(parsed.view(entry["bufferView"].toInt()) ==
                  assetByteArray(doc.assets().at(id)->payload),
              "Original managed image bytes are retained exactly beside normalized PNGs");
    }
    for (auto value : parsed.root["images"].toArray()) {
        const auto entry = value.toObject();
        check(!entry.contains("uri") && entry["mimeType"] == "image/png",
              "No external image paths");
        const auto id = entry["extras"].toObject()["sketchyupAsset"].toString().toULongLong();
        const auto pixels =
            decodeTextureImage({id, "Normalized", "image/png",
                                assetPayload(parsed.view(entry["bufferView"].toInt()))});
        check(pixels.image &&
                  pixels.image->rgba() == decodeTextureImage(*doc.assets().at(id)).image->rgba(),
              "Normalized embedded pixels retain color and coverage");
    }
    check(exportGlb(RenderSnapshot::capture(decodeContainer(before))).glb == scene.glb,
          "Relocation retains the exact textured GLB");
    replaceAsset(doc, first, {});
    check(exportGlb(snapshot).glb == scene.glb,
          "Snapshot texture survives later missing-asset edit");
    writeGlbExport(scene, path);
    QFile native(path + "/model.sketchyup");
    check(native.open(QIODevice::WriteOnly) && native.write(before) == before.size(),
          "Retain original packaged source for installed and native rendering checks");
}
void failures() {
    Document doc;
    const auto body = doc.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
    const auto face = doc.bodies().at(body)->surface.faces.begin()->first;
    auto huge = checker(0);
    qToBigEndian(quint32(4097), huge.data() + 16);
    const std::vector<std::pair<std::string, AssetPayloadPtr>> cases{
        {"image/png", {}},
        {"application/octet-stream", assetPayload("opaque")},
        {"image/png", assetPayload("corrupt")},
        {"image/png", assetPayload(huge)}};
    const std::array<const char *, 4> expected{"missing", "unsupported", "invalid", "too_large"};
    for (size_t i = 0; i < cases.size(); ++i) {
        const auto asset = createAsset(doc, "Fallback", cases[i].first, cases[i].second);
        const auto material =
            createMaterial(doc, "Color retained " + std::to_string(i), {1, 0, 0}, .75, asset);
        assignMaterial(doc, body, {}, material);
        const auto scene = exportGlb(RenderSnapshot::capture(doc));
        const Parsed parsed(scene);
        check(
            !parsed.root.contains("textures") &&
                scene.manifest["textureImages"].toArray()[0].toObject()["status"] == expected[i] &&
                scene.manifest["losses"].toObject()["textureAssetsPreservedWithoutUVMapping"] == 1,
            "Fallback retains color/opacity and reports its actual reason");
    }
    const auto asset = createAsset(doc, "Valid", "image/png", assetPayload(checker(0)));
    const auto material = createMaterial(doc, "Mapped", {1, 1, 1}, 1, asset);
    assignMaterial(doc, body, {}, material);
    assignTextureMapping(doc, body, face, TextureMapping{{}, {999999999.1, 0, 0}, {0, 1, 0}});
    const auto before = encodeContainer(doc);
    rejects([&] { exportGlb(RenderSnapshot::capture(doc)); });
    check(encodeContainer(doc) == before, "Precision rejection leaves source unchanged");
}
void independentMeshes() {
    Document doc;
    const auto image = createAsset(doc, "Mesh checker", "image/png", assetPayload(checker(0)));
    const auto material = createMaterial(doc, "Shared image", {1, 1, 1}, 1, image);
    const std::vector<std::vector<Vec3>> loop{{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}};
    const auto a = doc.addFace(loop), b = doc.addFace(loop);
    assignMaterial(doc, a, {}, material);
    assignMaterial(doc, b, {}, material);
    doc.transform(b, Transform::translation({2, 0, 0}));
    const auto face = doc.bodies().at(b)->surface.faces.begin()->first;
    check(exportGlb(RenderSnapshot::capture(doc)).manifest["uniqueMeshes"] == 1,
          "Identical textured geometry shares storage");
    assignTextureMapping(doc, b, face, TextureMapping{{}, {1, 0, 0}, {0, 1, 0}, {.25, 0}});
    check(exportGlb(RenderSnapshot::capture(doc)).manifest["uniqueMeshes"] == 2,
          "Different UVs must never alias the same exported mesh");
    doc.undo();
    check(exportGlb(RenderSnapshot::capture(doc)).manifest["uniqueMeshes"] == 1,
          "Undo restores textured mesh sharing");
}

void aggregateBudget() {
    const auto payload = assetPayload(encodeTexturePng(
        TextureImage(4096, 4096, std::vector<std::uint8_t>(TextureImage::byteLimit, 255))));
    Document doc;
    for (int i = 0; i < 5; ++i) {
        const double x = i * 2;
        const auto body = doc.addFace({{{x, 0, 0}, {x + 1, 0, 0}, {x + 1, 1, 0}, {x, 1, 0}}});
        const auto bytes =
            i == 4 ? assetPayload(encodeTexturePng(TextureImage(1, 1, {255, 255, 255, 255})))
                   : payload;
        const auto asset = createAsset(doc, "Budget image", "image/png", bytes);
        assignMaterial(
            doc, body, {},
            createMaterial(doc, "Budget swatch " + std::to_string(i), {1, 1, 1}, 1, asset));
        if (i == 3) {
            const auto scene = exportGlb(RenderSnapshot::capture(doc));
            check(scene.manifest["decodedTextureBytes"].toInteger() == 256 * 1024 * 1024 &&
                      Parsed(scene).root["images"].toArray().size() == 4,
                  "Exact aggregate decoded-image budget is accepted");
        }
    }
    const auto before = encodeContainer(doc);
    bool bounded = false;
    try {
        exportGlb(RenderSnapshot::capture(doc));
    } catch (const std::exception &error) {
        bounded = std::string(error.what()) == "GLB decoded texture images exceed 256 MiB";
    }
    check(bounded && encodeContainer(doc) == before,
          "One pixel beyond aggregate image budget rejects atomically");
}

} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir temporary;
        const auto root =
            argc > 1 ? QString::fromLocal8Bit(argv[1]) : temporary.path() + "/fixtures";
        check(QDir().mkdir(root), "Fresh texture fixture directory");
        implicitAndPacking();
        for (int variant = 0; variant < 12; ++variant)
            fixture(root + "/texture-" + QString::number(variant), variant);
        failures();
        independentMeshes();
        aggregateBudget();
        std::cout << "Textured GLB coordinates, independent sides, images, relocation and failures "
                     "passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
