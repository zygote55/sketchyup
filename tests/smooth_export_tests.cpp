#include "core/components.hpp"
#include "core/edge_appearance.hpp"
#include "core/groups.hpp"
#include "core/materials.hpp"
#include "integrations/glb_export.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtEndian>
#include <bit>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
quint32 word(const QByteArray &bytes, qsizetype offset) {
    check(offset >= 0 && offset + 4 <= bytes.size(), "GLB word bounds");
    return qFromLittleEndian<quint32>(bytes.constData() + offset);
}
void fixture(QString directory, int variant) {
    Document doc;
    const auto body = doc.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
    doc.extrude(body, 5, 1);
    const auto front = createMaterial(doc, "Front", {.8f, .8f, .8f});
    const auto back = createMaterial(doc, "Back", {.1f, .2f, .8f});
    assignMaterial(doc, body, {}, front, true, false);
    assignMaterial(doc, body, {}, back, false, true);
    SelectionSet edges;
    for (const auto &[edge, record] : doc.bodies().at(body)->topology.edges)
        edges.insert({body, SelectionKind::Edge, edge});
    if (variant != 0)
        setEdgeAppearance(doc, edges, 0, {}, {}, true);
    if (variant == 3) {
        // Clear all edges of the horizontal top face; its four corners form a
        // separate hard fan while the other five faces still smooth together.
        SelectionSet top;
        for (const auto &[edge, record] : doc.bodies().at(body)->topology.edges) {
            const auto &s = doc.bodies().at(body)->surface;
            if (s.vertices.at(record.a).z == 1 && s.vertices.at(record.b).z == 1)
                top.insert({body, SelectionKind::Edge, edge});
        }
        setEdgeAppearance(doc, top, 0, {}, {}, false);
    }
    if (variant == 2) {
        const auto group = createGroup(doc, {body});
        const auto component = createComponent(doc, group);
        placeComponent(doc, component.definition,
                       Transform::translation({3, 0, 0}) * Transform::rotation({0, 0, 1}, .41) *
                           Transform::scaling({-1.5, .75, 2}));
    }
    const auto before = encodeContainer(doc);
    const auto scene = exportGlb(RenderSnapshot::capture(doc));
    check(before == encodeContainer(doc), "Smoothing export preserves model bytes");
    const auto jsonSize = word(scene.glb, 12);
    const auto root = QJsonDocument::fromJson(scene.glb.mid(20, jsonSize)).object();
    const auto binary = scene.glb.mid(28 + jsonSize);
    const auto accessors = root["accessors"].toArray(), views = root["bufferViews"].toArray();
    auto read = [&](int accessor, int index) {
        const auto a = accessors[accessor].toObject();
        const auto v = views[a["bufferView"].toInt()].toObject();
        const auto offset = v["byteOffset"].toInt() + a["byteOffset"].toInt() + index * 12;
        return Vec3{std::bit_cast<float>(word(binary, offset)),
                    std::bit_cast<float>(word(binary, offset + 4)),
                    std::bit_cast<float>(word(binary, offset + 8))};
    };
    const auto meshes = root["meshes"].toArray();
    check(meshes.size() == 1, "Reflected instances share the same shaded mesh");
    const auto primitives = meshes[0].toObject()["primitives"].toArray();
    check(primitives.size() == 2, "Distinct physical sides have two paired primitives");
    size_t checked = 0;
    for (auto value : primitives) {
        const auto primitive = value.toObject(), attrs = primitive["attributes"].toObject();
        const auto position = attrs["POSITION"].toInt(), normal = attrs["NORMAL"].toInt();
        const auto count = accessors[position].toObject()["count"].toInt();
        check(count == 36, "Smoothing retains twelve cube triangles per side");
        const int sign = primitive["material"].toInt() == 0 ? 1 : -1;
        for (int i = 0; i < count; i += 3) {
            const auto a = read(position, i), b = read(position, i + 1), c = read(position, i + 2);
            const auto center = (a + b + c) * (1. / 3.);
            const bool topFace = a.z == 1 && b.z == 1 && c.z == 1;
            for (int j = 0; j < 3; ++j) {
                const auto p = read(position, i + j), n = read(normal, i + j);
                Vec3 expected = p - Vec3{.5, .5, .5};
                if (variant == 0) {
                    // Analytic axis-aligned face normal, independently of exported winding.
                    expected = {center.x == 0   ? -1.
                                : center.x == 1 ? 1.
                                                : 0.,
                                center.y == 0   ? -1.
                                : center.y == 1 ? 1.
                                                : 0.,
                                center.z == 0   ? -1.
                                : center.z == 1 ? 1.
                                                : 0.};
                } else if (variant == 3 && p.z == 1) {
                    expected = topFace ? Vec3{0, 0, 1} : Vec3{expected.x, expected.y, 0};
                }
                expected = normalized(expected) * sign;
                check(length(n - expected) < 1e-6,
                      "Decoded normal agrees with analytic corner fan");
                ++checked;
            }
        }
    }
    check(checked == 72 && scene.manifest["visibleTriangles"].toInt() == (variant == 2 ? 48 : 24),
          "All front/back corners checked without changing triangle counts");
    writeGlbExport(scene, directory);
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir files;
        const auto root = argc > 1 ? QString::fromLocal8Bit(argv[1]) : files.path() + "/fixtures";
        check(QDir().mkdir(root), "Fresh smooth export fixture directory");
        for (int variant = 0; variant < 4; ++variant)
            fixture(root + "/smooth-" + QString::number(variant), variant);
        std::cout
            << "Decoded smooth GLB normals, hard seams, paired sides and shared mirrors passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
