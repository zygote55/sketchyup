#include "io/stl_import.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>
using namespace sketchy;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void near(double a, double b) {
    check(std::isfinite(a) && std::abs(a - b) < 1e-6, "Independent STL geometry oracle");
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Unselected STL repair must reject");
}
QJsonObject geometry(const StlImport &result) {
    return result.report["geometry"].toArray().first().toObject();
}
bool finding(const StlImport &result, const QString &code) {
    for (const auto &v : geometry(result)["findings"].toArray())
        if (v.toObject()["code"] == code)
            return true;
    return false;
}
StlSource cube() {
    Surface s;
    const auto face = s.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}});
    s.extrude(face, 4);
    StlSource out;
    out.solids = {"Known 24 cubic metre box"};
    for (const auto &t : s.triangles())
        out.facets.push_back({{t.a, t.b, t.c}, s.normal(t.face), 0, 0});
    return out;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        if (app.arguments().contains("--blender-real")) {
            const auto blender = qEnvironmentVariable("SKETCHYUP_BLENDER_TEST");
            if (blender.isEmpty())
                return 77;
            QTemporaryDir folder;
            check(folder.isValid(), "Blender STL scratch");
            QProcess producer;
            producer.start(blender, {"--background", "--factory-startup", "--disable-autoexec",
                                     "--python-exit-code", "1", "--python",
                                     QStringLiteral(SOURCE_DIR "/tests/stl_blender_fixture.py"),
                                     "--", folder.path()});
            check(producer.waitForStarted(10000) && producer.waitForFinished(90000) &&
                      producer.exitStatus() == QProcess::NormalExit && producer.exitCode() == 0,
                  "Actual Blender STL producer");
            for (const auto name : {"binary.stl", "ascii.stl"}) {
                const auto imported =
                    loadStl(folder.filePath(name), {.001, StlUpAxis::Z}, {StlWeld::Exact});
                check(imported.report["facets"].toInt() == 12 &&
                          imported.report["vertices"].toInt() == 8 &&
                          geometry(imported)["solidStatus"] == "solid",
                      "Both Blender encodings preserve closed reflected box");
                near(geometry(imported)["materialVolume"].toDouble(), 24);
                Vec3 low{1e6, 1e6, 1e6}, high{-1e6, -1e6, -1e6};
                for (const auto &[id, body] : imported.document.bodies())
                    for (const auto &[vertex, p] : body->surface.vertices) {
                        (void)id;
                        (void)vertex;
                        low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
                        high = {std::max(high.x, p.x), std::max(high.y, p.y),
                                std::max(high.z, p.z)};
                    }
                near(low.x, 9);
                near(high.x, 11);
                near(low.y, 18.5);
                near(high.y, 21.5);
                near(low.z, 28);
                near(high.z, 32);
            }
            std::cout << "Blender binary/ASCII STL units, placement, reflection, topology and "
                         "volume passed\n";
            return 0;
        }
        const auto source = cube();
        auto result = importStl(source, {StlWeld::Exact});
        check(result.report["vertices"].toInt() == 8 && result.report["facets"].toInt() == 12,
              "Exact welding reconstructs closed box topology");
        check(geometry(result)["solidStatus"] == "solid" &&
                  geometry(result)["analysisComplete"].toBool(),
              "Solid status requires complete independent diagnostics");
        near(geometry(result)["materialVolume"].toDouble(), 24);
        const auto before = encodeDocument(result.document);
        check(encodeDocument(decodeDocument(before)) == before, "Native STL round trip");
        result.document.undo();
        check(result.document.bodies().empty(), "Welding/import undo atomically");
        result.document.redo();
        check(result.document.bodies().size() == 1 &&
                  result.document.bodies().begin()->second->surface.faces.size() == 12,
              "Redo restores imported facets");
        auto soup = importStl(source, {StlWeld::None});
        check(soup.report["vertices"].toInt() == 36 && finding(soup, "open_boundary"),
              "No weld preserves independent corner identities and reports open boundaries");
        auto open = source;
        open.facets.pop_back();
        const auto opened = importStl(open, {StlWeld::Exact});
        check(finding(opened, "open_boundary") && geometry(opened)["materialVolume"].isNull(),
              "Open meshes import with explicit non-solid report");
        auto reversed = source;
        std::swap(reversed.facets[0].vertices[0], reversed.facets[0].vertices[1]);
        check(finding(importStl(reversed, {StlWeld::Exact}), "inconsistent_winding"),
              "Importer never silently changes facet winding");
        StlSource seam;
        seam.solids = {"Seam"};
        seam.facets = {
            {{Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{0, 1, 0}}, {0, 0, 1}, 0, 0},
            {{Vec3{.000005, 0, 0}, Vec3{0, 1.000005, 0}, Vec3{-1, 0, 0}}, {0, 0, 1}, 0, 0}};
        check(importStl(seam, {StlWeld::Exact}).report["vertices"].toInt() == 6,
              "Exact welding cannot move nearby vertices");
        const auto welded = importStl(seam, {StlWeld::Tolerance, 1e-5});
        check(welded.report["vertices"].toInt() == 4 &&
                  welded.report["movedCornerReferences"].toInt() == 2,
              "Explicit tolerance welding joins nearby representatives");
        auto collapsed = seam;
        collapsed.facets.push_back({{Vec3{5, 0, 0}, Vec3{6, 0, 0}, Vec3{7, 0, 0}}, {}, 0, 0});
        rejects([&] { importStl(collapsed, {StlWeld::Exact}); });
        const auto repaired = importStl(collapsed, {StlWeld::Exact, 1e-7, true});
        check(repaired.report["discardedFacets"].toInt() == 1 &&
                  repaired.report["facets"].toInt() == 2,
              "Explicit removal reports source degenerate triangle");
        auto close = seam;
        close.facets.push_back(
            {{Vec3{5, 0, 0}, Vec3{5.000005, 0, 0}, Vec3{5, 1, 0}}, {0, 0, 1}, 0, 0});
        rejects([&] { importStl(close, {StlWeld::Tolerance, 1e-5, false}); });
        check(
            importStl(close, {StlWeld::Tolerance, 1e-5, true}).report["discardedFacets"].toInt() ==
                1,
            "Weld-induced degeneration needs explicit removal");
        auto multiple = seam;
        multiple.solids.append("Second solid");
        multiple.facets[1].solid = 1;
        check(importStl(multiple, {StlWeld::Tolerance, 1e-3}).report["vertices"].toInt() == 6,
              "Welding cannot cross source solid boundaries");
        auto impossible = source;
        impossible.facets[0].solid = 99;
        rejects([&] { importStl(impossible, {}); });
        rejects([&] { importStl(source, {StlWeld::Tolerance, 0}); });
        QTemporaryDir dir;
        check(dir.isValid(), "Scratch folder");
        const auto path = dir.filePath("millimetres.stl");
        const QByteArray bytes = "solid test\nfacet normal 0 0 0\nouter loop\nvertex 0 0 0\nvertex "
                                 "2000 0 0\nvertex 0 3000 0\nendloop\nendfacet\nendsolid test\n";
        QFile file(path);
        check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "Write source");
        file.close();
        const auto loaded = loadStl(path, {.001, StlUpAxis::Y}, {StlWeld::Exact});
        const auto &body = *loaded.document.bodies().begin()->second;
        near(body.surface.area(body.surface.faces.begin()->first), 3);
        check(loaded.report["source"].toObject()["zeroNormals"].toInt() == 1,
              "Source normal findings survive conversion");
        check(file.open(QIODevice::ReadOnly) && file.readAll() == bytes, "Source bytes unchanged");
#ifdef CLI_PATH
        const auto cli = QString::fromUtf8(CLI_PATH);
        auto invoke = [&](QStringList args, bool success) {
            QProcess p;
            p.start(cli, args);
            check(p.waitForStarted(10000) && p.waitForFinished(30000), "STL CLI completed");
            check((p.exitCode() == 0) == success, "STL CLI expected status");
            const auto output = p.readAllStandardOutput() + p.readAllStandardError();
            check(QJsonDocument::fromJson(output).isObject(), "STL CLI structured report");
            return QJsonDocument::fromJson(output).object();
        };
        const auto native = dir.filePath("imported.sketchyup");
        const QStringList base{"--import-stl", path, "--stl-unit", "mm",
                               "--stl-up",     "z",  "--stl-weld", "exact"};
        invoke(base, true);
        invoke(base + QStringList{"--output", native}, true);
        invoke(base + QStringList{"--output", native}, false);
        invoke({"--import-stl", path, "--stl-unit", "mm", "--stl-up", "z"}, false);
        invoke(base + QStringList{"--stl-tolerance", ".0001"}, false);
        invoke(base + QStringList{"--script", "unused"}, false);
        invoke({"--stl-weld", "none"}, false);
        invoke({"--import-stl", path, "--stl-unit", "mm", "--stl-up", "z", "--stl-weld",
                "tolerance", "--stl-tolerance", "nan"},
               false);
        for (const auto encoding : {"binary", "ascii"}) {
            const auto output = dir.filePath(QString(encoding) + "-export.stl");
            const QStringList args{"--export-stl",   output,  "--input",  native,
                                   "--stl-unit",     "mm",    "--stl-up", "z",
                                   "--stl-encoding", encoding};
            invoke(args, true);
            invoke(args, false);
            check(
                loadStl(output, {.001, StlUpAxis::Z}, {StlWeld::Exact}).report["facets"].toInt() ==
                    1,
                "CLI export retains source triangle");
        }
        invoke({"--export-stl", dir.filePath("bad.stl"), "--input", native, "--stl-unit", "mm",
                "--stl-up", "z"},
               false);
        check(file.seek(0) && file.readAll() == bytes, "CLI leaves original STL unchanged");
#endif
        std::cout << "STL exact/none/tolerance welding, explicit repair, topology diagnostics, "
                     "units and undo passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
