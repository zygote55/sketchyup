#include "io/stl_import.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
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
        std::cout << "STL exact/none/tolerance welding, explicit repair, topology diagnostics, "
                     "units and undo passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
