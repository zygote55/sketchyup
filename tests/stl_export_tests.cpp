#include "io/stl_export.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QProcess>
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
        if (app.arguments().contains("--blender-real")) {
            const auto blender = qEnvironmentVariable("SKETCHYUP_BLENDER_TEST");
            if (blender.isEmpty())
                return 77;
            writeStlExport(exportStl(doc, {.001, StlUpAxis::Z}, StlEncoding::Binary),
                           dir.filePath("binary.stl"));
            writeStlExport(exportStl(doc, {.001, StlUpAxis::Z}, StlEncoding::Ascii),
                           dir.filePath("ascii.stl"));
            QProcess consumer;
            consumer.start(blender, {"--background", "--factory-startup", "--disable-autoexec",
                                     "--python-exit-code", "1", "--python",
                                     QStringLiteral(SOURCE_DIR "/tests/stl_blender_consumer.py"),
                                     "--", dir.path()});
            check(consumer.waitForStarted(10000) && consumer.waitForFinished(90000) &&
                      consumer.exitStatus() == QProcess::NormalExit && consumer.exitCode() == 0,
                  "Actual Blender STL consumer");
            check(consumer.readAllStandardOutput().contains("SKETCHYUP_STL_CONSUMER_VERIFIED"),
                  "Independent Blender assertions completed");
            std::cout << "Blender verified both STL encodings, dimensions, winding and volume\n";
            return 0;
        }
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
        auto distant = doc;
        auto parent = std::make_shared<Body>(*doc.bodies().at(1));
        parent->transform = Transform::translation({1000.123456, 20, 30});
        Edit move{"Precision fixture", {{1, doc.bodies().at(1), parent}}};
        distant.apply(std::move(move), distant.revision());
        rejects([&] { exportStl(distant, {1, StlUpAxis::Z}, StlEncoding::Binary); });
        check(exportStl(distant, {1, StlUpAxis::Z}, StlEncoding::Ascii).report["facets"].toInt() ==
                  12,
              "ASCII retains coordinates that exceed binary precision budget");
        Document holes;
        auto body = std::make_shared<Body>();
        body->id = 1;
        body->surface.addFace({{{0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {0, 4, 0}},
                               {{1, 1, 0}, {1, 3, 0}, {3, 3, 0}, {3, 1, 0}}});
        body->topology = Topology::rebuild(body->surface, {});
        Edit h{"Holed face", {{1, nullptr, body}}};
        h.nextIdFloor = 2;
        holes.apply(std::move(h), holes.revision());
        const auto source = parseStl(exportStl(holes, {1, StlUpAxis::Z}, StlEncoding::Ascii).bytes,
                                     {1, StlUpAxis::Z});
        double area{};
        for (const auto &f : source.facets) {
            area += length(cross(f.vertices[1] - f.vertices[0], f.vertices[2] - f.vertices[0])) / 2;
            near(f.normal.z, 1);
        }
        near(area, 12);
        check(QFile::link(dir.filePath("absent"), dir.filePath("dangling.stl")),
              "Dangling output link");
        rejects([&] {
            writeStlExport(exportStl(doc, {1, StlUpAxis::Z}, StlEncoding::Ascii),
                           dir.filePath("dangling.stl"));
        });
        std::cout << "STL binary/ASCII geometry, explicit units/axis, reflected normals and safe "
                     "publication passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
