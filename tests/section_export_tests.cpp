#include "core/assets.hpp"
#include "core/face_textures.hpp"
#include "core/groups.hpp"
#include "core/materials.hpp"
#include "core/sections.hpp"
#include "integrations/glb_export.hpp"
#include "io/assets.hpp"
#include "io/document_io.hpp"
#include "io/texture_image.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QtEndian>
#include <bit>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void near(double a, double b, const char *message) {
    check(std::isfinite(a) && std::abs(a - b) < 1e-5, message);
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
Id box(Document &doc, double x) {
    auto body = doc.addFace({{{x, 0, 0}, {x + 2, 0, 0}, {x + 2, 2, 0}, {x, 2, 0}}});
    doc.extrude(body, doc.bodies().at(body)->surface.faces.begin()->first, 2);
    return body;
}
QJsonObject bodyEntry(const GlbExport &scene, Id body) {
    for (const auto value : scene.manifest["bodies"].toArray()) {
        const auto entry = value.toObject();
        if (entry["body"].toString() == QString::number(body))
            return entry;
    }
    throw std::runtime_error("Expected body export entry");
}
Vec3 vertex(const std::vector<double> &values, size_t index) {
    return {values.at(index * 3), values.at(index * 3 + 1), values.at(index * 3 + 2)};
}
void nested() {
    Document doc;
    const auto body = box(doc, 0), sibling = box(doc, 3.5);
    const auto group = createGroup(doc, {body}, "Mirrored context");
    doc.transform(group, Transform::translation({2, 0, 0}) * Transform::scaling({-1, 1, 1}));
    const auto root = createSection(doc, "Model", 0, {{0, 0, -1}, 1});
    auto colored = *doc.sections().at(root);
    colored.color = {.1f, .8f, .2f};
    updateSection(doc, root, colored);
    const auto parent = createSection(doc, "Parent", group, {{0, 1, 0}, -1});
    const auto child = createSection(doc, "Child", body, {{1, 0, 0}, -1});
    setActiveSection(doc, 0, root);
    setActiveSection(doc, group, parent);
    setActiveSection(doc, body, child);
    const auto before = encodeContainer(doc);
    const auto snapshot = RenderSnapshot::capture(doc);
    const auto scene = exportGlb(snapshot);
    const Parsed parsed(scene);
    check(encodeContainer(doc) == before, "Section export preserves geometry and all saved state");
    check(scene.manifest["sectionCapTriangles"].toInt() > 0, "Section caps are exported");
    check(scene.manifest["losses"].toObject()["sectionCutEdgesOmitted"].toInt() > 0,
          "Surface-only export explicitly reports omitted cut edges");
    std::map<Id, double> areas;
    for (auto owner : {body, sibling}) {
        const auto entry = bodyEntry(scene, owner), sections = entry["sections"].toObject();
        check(sections["active"].toArray().size() == (owner == body ? 3 : 1),
              "Export scopes ancestor cuts");
        const auto primitives = parsed.root["meshes"]
                                    .toArray()[entry["mesh"].toInt()]
                                    .toObject()["primitives"]
                                    .toArray();
        const auto world = doc.worldTransform(owner);
        const auto cuts = effectiveSectionCuts(doc, owner);
        for (const auto item : primitives) {
            const auto positions =
                parsed.attribute(item.toObject()["attributes"].toObject()["POSITION"].toInt(), 3);
            for (size_t i = 0; i < positions.size() / 3; ++i)
                check(sectionContains(world.point(vertex(positions, i)), cuts),
                      "All exported points satisfy every ancestor plane");
        }
        for (const auto item : sections["caps"].toArray()) {
            const auto cap = item.toObject();
            const Id section = cap["section"].toString().toULongLong();
            check(!cap.contains("id"),
                  "Generated cap has plane identity, never native face identity");
            const auto attributes =
                primitives[cap["primitive"].toInt()].toObject()["attributes"].toObject();
            const auto positions = parsed.attribute(attributes["POSITION"].toInt(), 3);
            const auto normals = parsed.attribute(attributes["NORMAL"].toInt(), 3);
            const auto cut = *std::find_if(cuts.begin(), cuts.end(),
                                           [&](auto cut) { return cut.id == section; });
            const auto normal = cut.plane.transformed(world.inverse()).normal * -1;
            for (int i = cap["firstVertex"].toInt();
                 i < cap["firstVertex"].toInt() + cap["vertexCount"].toInt(); i += 3) {
                const auto a = vertex(positions, i), b = vertex(positions, i + 1),
                           c = vertex(positions, i + 2);
                check(dot(cross(b - a, c - a), normal) > 0,
                      "Mirrored cap local winding matches outward normal");
                near(length(vertex(normals, i) - normal), 0,
                     "Cap normals point away from retained half-space");
                for (const auto p : {a, b, c})
                    near(cut.plane.distance(world.point(p)), 0, "Cap stays on its world plane");
                areas[section] += length(cross(world.vector(b - a), world.vector(c - a))) * .5;
            }
        }
    }
    near(areas[root], 5, "Independent model cap area across quarter and sibling");
    near(areas[parent], 1, "Independent parent cap area");
    near(areas[child], 1, "Independent child cap area");
    const auto bounds = scene.manifest["nativeBounds"].toObject();
    near(bounds["max"].toArray()[2].toDouble(), 1, "Export bounds use retained geometry");
    Id sideFace{};
    const auto &solid = *doc.bodies().at(body);
    for (const auto &[face, record] : solid.surface.faces)
        if (std::all_of(record.loops.front().begin(), record.loops.front().end(),
                        [&](Id vertex) { return solid.surface.vertices.at(vertex).x == 2; }))
            sideFace = face;
    check(sideFace != 0, "Find retained side for hidden-boundary fixture");
    const auto opened =
        exportGlb(RenderSnapshot::capture(doc, {}, {{body, SelectionKind::Face, sideFace}}));
    const auto openSections = bodyEntry(opened, body)["sections"].toObject();
    check(openSections["unfilled"].toArray().contains(QString::number(root)),
          "Hidden face opens boundary before cap construction");
    for (const auto cap : openSections["caps"].toArray())
        check(cap.toObject()["section"].toString() != QString::number(root),
              "Hidden boundary does not receive an invented cap");
    check(!bodyEntry(opened, sibling)["sections"].toObject()["caps"].toArray().empty(),
          "Hidden face leaves sibling cap intact");
    auto record = *doc.sections().at(root);
    record.fill = false;
    updateSection(doc, root, record);
    const auto unfilled = exportGlb(RenderSnapshot::capture(doc));
    check(bodyEntry(unfilled, sibling)["sections"].toObject()["caps"].toArray().empty(),
          "Fill off exports retained surfaces without root cap");
    check(exportGlb(snapshot).glb == scene.glb,
          "Worker snapshot retains captured activation and fill");
    auto hidden = exportGlb(RenderSnapshot::capture(doc, {}, {{group, SelectionKind::Body, 0}}));
    check(hidden.manifest["bodies"].toArray().size() == 1,
          "Hidden ancestor omits its section geometry");
    const auto evidence = qEnvironmentVariable("SKETCHYUP_SECTION_EXPORT_EVIDENCE");
    if (!evidence.isEmpty())
        writeGlbExport(scene, evidence + "/nested");
    record.plane = {{0, 0, 1}, -100};
    updateSection(doc, root, record);
    bool rejected = false;
    try {
        exportGlb(RenderSnapshot::capture(doc));
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected, "Entirely clipped export rejects instead of producing uncut geometry");
}
void texturesAndOpenContours() {
    Document doc;
    const auto body = doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}}});
    const auto face = doc.bodies().at(body)->surface.faces.begin()->first;
    const auto image = encodeTexturePng(TextureImage(2, 2, std::vector<std::uint8_t>(16, 255)));
    const auto asset = createAsset(doc, "Image", "image/png", assetPayload(image));
    const auto material = createMaterial(doc, "Texture", {1, 1, 1}, 1, asset);
    assignMaterial(doc, body, {}, material, true, true);
    const TextureMapping front{{}, {.5, .1, 0}, {.2, .5, 0}, {.125, .25}},
        back{{}, {-.5, 0, 0}, {0, .25, 0}, {.5, .125}};
    assignTextureMapping(doc, body, face, front, true, false);
    assignTextureMapping(doc, body, face, back, false, true);
    doc.transform(body, Transform::scaling({-2, .75, 1}));
    const auto cut = createSection(doc, "Local", body, {{1, 0, 0}, -1});
    setActiveSection(doc, body, cut);
    const auto before = encodeContainer(doc);
    const auto scene = exportGlb(RenderSnapshot::capture(doc));
    const Parsed parsed(scene);
    const auto entry = bodyEntry(scene, body), section = entry["sections"].toObject();
    check(section["unfilled"].toArray() == QJsonArray{QString::number(cut)} &&
              section["caps"].toArray().empty(),
          "Open face clips and reports unavailable cap fill");
    const auto native = entry["faces"].toArray()[0].toObject();
    check(native["id"].toString() == QString::number(face), "Clipped faces retain native identity");
    const auto primitives =
        parsed.root["meshes"].toArray()[entry["mesh"].toInt()].toObject()["primitives"].toArray();
    for (bool reverse : {false, true}) {
        const auto primitive = native[reverse ? "backPrimitive" : "primitive"].toInt();
        const auto attributes = primitives[primitive].toObject()["attributes"].toObject();
        const auto positions = parsed.attribute(attributes["POSITION"].toInt(), 3),
                   uv = parsed.attribute(attributes["TEXCOORD_0"].toInt(), 2);
        const auto normals = parsed.attribute(attributes["NORMAL"].toInt(), 3);
        for (size_t i = 0; i < positions.size() / 3; ++i) {
            const auto p = vertex(positions, i);
            check(p.x >= 1 - 1e-6, "Texture triangle remains in local retained half");
            const auto expected = (reverse ? back : front).coordinates(p);
            for (size_t k = 0; k < 2; ++k) {
                const auto difference = uv[i * 2 + k] - (k ? expected.v : expected.u);
                near(difference, std::round(difference),
                     "Barycentric UV agrees with independent side mapping modulo wrapping");
            }
            near(vertex(normals, i).z, reverse ? -1 : 1,
                 "Clipped front/back normals remain independent");
        }
    }
    check(encodeContainer(doc) == before,
          "Textured section export leaves mappings and geometry unchanged");
    const auto evidence = qEnvironmentVariable("SKETCHYUP_SECTION_EXPORT_EVIDENCE");
    if (!evidence.isEmpty())
        writeGlbExport(scene, evidence + "/texture");
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        const auto evidence = qEnvironmentVariable("SKETCHYUP_SECTION_EXPORT_EVIDENCE");
        if (!evidence.isEmpty())
            check(QDir().mkpath(evidence), "Create export fixture root");
        nested();
        texturesAndOpenContours();
        std::cout << "Section export scopes, caps, winding, UVs, snapshots and bounds passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
