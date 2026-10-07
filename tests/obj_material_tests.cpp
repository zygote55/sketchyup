#include "io/obj_material.hpp"
#include <QCoreApplication>
#include <cmath>
#include <iostream>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Invalid MTL must reject");
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        const auto library = parseObjMaterials(
            "# bounded material\nnewmtl Plaster\nKd .25 .5 1\nd .7\nTr .3\nmap_Kd -s 2 3 1 -o .25 "
            "-.5 0 tiles/checker.png\nNs 100\nnewmtl White\nKd .8\n");
        check(library.materials.size() == 2, "Two named materials");
        const auto &m = library.materials[0];
        check(m.diffuse == std::array<double, 3>{.25, .5, 1} && std::abs(m.opacity - .7) < 1e-12,
              "Linear diffuse and consistent d/Tr retained");
        check(m.texture.path == "tiles/checker.png" &&
                  m.texture.scale == std::array<double, 2>{2, 3} &&
                  m.texture.offset == std::array<double, 2>{.25, -.5},
              "Diffuse texture path and transform captured without loading");
        check(library.report["omittedStatements"].toObject()["Ns"] == 1,
              "Unsupported shading explicitly counted");
        check(library.materials[1].diffuse == std::array<double, 3>{.8, .8, .8},
              "Single-channel Kd expands consistently");
        for (const QByteArray &option :
             {QByteArray("-clamp on"), QByteArray("-s 1 1 2"), QByteArray("-unknown 1")}) {
            const auto fallback =
                parseObjMaterials("newmtl Test\nmap_Kd " + option + " texture.png\n");
            check(fallback.materials[0].texture.path.isEmpty() &&
                      !fallback.report["omittedStatements"].toObject().isEmpty(),
                  "Unsupported image sampling retains explicit material fallback");
        }
        check(parseObjMaterials("newmtl Test\nmap_Kd -s 2 -o .5 image with spaces.png\n")
                      .materials[0]
                      .texture.path == "image with spaces.png",
              "Texture filenames can contain spaces");
        for (const QByteArray &bad :
             {QByteArray("newmtl A\nnewmtl A\n"), QByteArray("Kd 1 1 1\n"),
              QByteArray("newmtl A\nKd nan\n"), QByteArray("newmtl A\nKd -1\n"),
              QByteArray("newmtl A\nd .2\nTr .2\n"), QByteArray("newmtl A\nmap_Kd -s nope.png\n"),
              QByteArray("csh command\n"), QByteArray("call outside.mtl\n"),
              QByteArray("newmtl A\nmap_Kd \\\n")})
            rejects([&] { parseObjMaterials(bad); });
        rejects([] { parseObjMaterials(QByteArray(4 * 1024 * 1024 + 1, ' ')); });
        std::cout
            << "Bounded MTL diffuse, opacity, texture transforms and explicit fallbacks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
