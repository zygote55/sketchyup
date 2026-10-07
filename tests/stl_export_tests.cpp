#include "io/stl_export.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>
#include <iostream>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void near(double a, double b) {
    check(std::isfinite(a) && std::abs(a - b) < 1e-6, "STL independent geometry oracle");
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Unsafe export must reject");
}
Document fixture() {
    Document doc;
    Edit edit{"Reflected hidden box", {}};
    auto group = std::make_shared<Body>();
    group->id = 1;
    group->kind = BodyKind::Group;
    group->transform = Transform::translation({10, 20, 30});
    auto body = std::make_shared<Body>();
    body->id = 2;
    body->parent = 1;
    body->hidden = true;
    body->transform = Transform::scaling({-1, 1.5, 2});
    body->name = "../../name\nendsolid";
    const auto face = body->surface.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}}});
    body->surface.extrude(face, 2);
    body->topology = Topology::rebuild(body->surface, {});
    edit.changes = {{1, nullptr, group}, {2, nullptr, body}};
    edit.nextIdFloor = 3;
    doc.apply(std::move(edit), doc.revision());
    return doc;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir dir;
        check(dir.isValid(), "Scratch folder");
        auto doc = fixture();
        const auto before = encodeDocument(doc);
        const auto history = doc.history().total;
        for (auto encoding : {StlEncoding::Binary, StlEncoding::Ascii})
            for (auto up : {StlUpAxis::Y, StlUpAxis::Z}) {
                const auto result = exportStl(doc, {.001, up}, encoding);
                check(result.report["facets"].toInt() == 12 && !result.bytes.contains("../../"),
                      "Triangulated hidden box and safe name");
                const auto imported =
                    importStl(parseStl(result.bytes, {.001, up}), {StlWeld::Exact});
                check(imported.report["vertices"].toInt() == 8, "Closed shared box vertices");
                const auto geometry = imported.report["geometry"].toArray().first().toObject();
                check(geometry["solidStatus"] == "solid", "Mirroring preserves solid winding");
                near(geometry["materialVolume"].toDouble(), 24);
                Vec3 low{1e6, 1e6, 1e6}, high{-1e6, -1e6, -1e6};
                for (const auto &[id, b] : imported.document.bodies())
                    for (const auto &[v, p] : b->surface.vertices) {
                        (void)id;
                        (void)v;
                        low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
                        high = {std::max(high.x, p.x), std::max(high.y, p.y),
                                std::max(high.z, p.z)};
                    }
                near(low.x, 8);
                near(high.x, 10);
                near(low.y, 20);
                near(high.y, 23);
                near(low.z, 30);
                near(high.z, 34);
                const auto path = dir.filePath(QString::number(int(encoding)) +
                                               QString::number(int(up)) + ".stl");
                writeStlExport(result, path);
                rejects([&] { writeStlExport(result, path); });
                QFile file(path);
                check(file.open(QIODevice::ReadOnly) && file.readAll() == result.bytes,
                      "No replacement and exact publication");
                auto corrupt = result;
                corrupt.bytes[0] = '?';
                rejects([&] { writeStlExport(corrupt, dir.filePath("bad.stl")); });
                check(QFile::link(path, dir.filePath("link.stl")), "Create symlink");
                rejects([&] { writeStlExport(result, dir.filePath("link.stl")); });
                QFile::remove(dir.filePath("link.stl"));
            }
        check(encodeDocument(doc) == before && doc.history().total == history,
              "Export leaves source/history unchanged");
        rejects([&] { exportStl(Document{}, {1, StlUpAxis::Z}, StlEncoding::Binary); });
        rejects([&] { exportStl(doc, {0, StlUpAxis::Z}, StlEncoding::Binary); });
        std::cout << "STL binary/ASCII geometry, explicit units/axis, reflected normals and safe "
                     "publication passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
