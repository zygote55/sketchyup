#include "geometry/solid.hpp"
#include "text/text_geometry.hpp"
#include <QFontDatabase>
#include <QGuiApplication>
#include <iomanip>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F operation) {
    try {
        operation();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Invalid text geometry accepted");
}
double area(const TextGeometry &g) {
    double result{};
    for (const auto &region : g.regions)
        for (const auto &[id, face] : region.faces)
            result += region.area(id);
    return result;
}
} // namespace
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    try {
        TextGeometrySettings s;
        const auto families = QFontDatabase::families();
        for (const auto *family : {"DejaVu Sans", "Noto Sans", "Liberation Sans"})
            if (families.contains(family)) {
                s.family = family;
                break;
            }
        const auto chosenFamily = s.family;
        s.text = "O";
        s.height = 1;
        const auto flat = shapeTextGeometry(s);
        check(flat.regions.size() == 1 && flat.holes == 1 && area(flat) > .1,
              "O retains its hole and filled area");
        check(flat.actualFamily == chosenFamily && !flat.substituted && !flat.fonts.empty() &&
                  flat.fonts[0].fingerprint.size() == 64,
              "Actual font provenance reported");
        s.depth = .2;
        const auto solid = shapeTextGeometry(s);
        check(solid.regions.size() == 1, "Extrusion retains connected region");
        for (const auto &region : solid.regions) {
            region.validate();
            for (const auto &edge : region.edges())
                check(edge.faces.size() == 2, "Extruded glyph is closed");
        }
        const auto volume = inspectSolid(solid.regions[0], Topology::rebuild(solid.regions[0], {}));
        const auto volumeTolerance = tolerance * area(solid);
        if (!volume.volume || std::abs(*volume.volume - area(flat) * s.depth) >= volumeTolerance)
            std::cerr << std::setprecision(17) << "Text volume status=" << volume.status
                      << " volume=" << volume.volume.value_or(-1)
                      << " expected=" << area(flat) * s.depth
                      << " vertices=" << solid.regions[0].vertices.size() << '\n';
        check(volume.volume && std::abs(*volume.volume - area(flat) * s.depth) < volumeTolerance,
              "Holed glyph extrusion has the expected material volume");
        s.depth = 0;
        s.text = "B";
        check(shapeTextGeometry(s).holes == 2, "B retains two holes");
        s.text = "i";
        check(shapeTextGeometry(s).regions.size() == 2, "Disconnected glyph dot stays separate");
        s.text = QString::fromUtf8("Ω café\nلا");
        const auto unicode = shapeTextGeometry(s);
        check(unicode.lines == 2 && !unicode.regions.empty(),
              "Unicode and multiline shape into valid regions");
        double lowY = 1e9, highY = -1e9;
        for (const auto &r : unicode.regions)
            for (const auto &[id, p] : r.vertices) {
                lowY = std::min(lowY, p.y);
                highY = std::max(highY, p.y);
            }
        check(lowY < -.5 && highY > 0, "Multiline baselines progress down the text plane");
        s.text = QString::fromUtf8("لا");
        const auto joined = shapeTextGeometry(s);
        s.text = QString::fromUtf8("ل ا");
        check(joined.advanceWidth < shapeTextGeometry(s).advanceWidth,
              "Arabic joining preserves shaped advances");
        s.text = "A A";
        const auto spaced = shapeTextGeometry(s);
        s.text = "AA";
        check(spaced.advanceWidth > shapeTextGeometry(s).advanceWidth,
              "Spaces retain advance without fabricated faces");
        s.text = "O\nO";
        s.lineSpacing = .5;
        const auto overlap = shapeTextGeometry(s);
        check(overlap.regions.size() == 1,
              "Overlapping multiline glyphs union into one native region");
        s.lineSpacing = 1.2;
        s.text = QString(1024, 'O');
        rejects([&] { shapeTextGeometry(s); });
        s.text = "O";
        const auto first = shapeTextGeometry(s), second = shapeTextGeometry(s);
        check(first.regions == second.regions &&
                  first.fonts[0].fingerprint == second.fonts[0].fingerprint,
              "Same local font reproduces exact geometry");
        s.family = "SketchyUp nonexistent font 6fd3c1";
        rejects([&] { shapeTextGeometry(s); });
        s.allowSubstitution = true;
        check(shapeTextGeometry(s).substituted,
              "Missing font substitution is explicit and reported");
        s.family = chosenFamily;
        s.style = "Absent style";
        s.allowSubstitution = false;
        rejects([&] { shapeTextGeometry(s); });
        s.style.clear();
        for (const auto text :
             {QString("   \n\t"), QString(4097, 'A'), QString::fromUcs4(U"\U0010ffff")}) {
            s.text = text;
            rejects([&] { shapeTextGeometry(s); });
        }
        s.text = "O";
        s.height = 0;
        rejects([&] { shapeTextGeometry(s); });
        s.height = 1;
        s.depth = -1;
        rejects([&] { shapeTextGeometry(s); });
        s.depth = 0;
        s.lineSpacing = 0;
        rejects([&] { shapeTextGeometry(s); });
        std::cout << "Local font outlines, holes, Unicode shaping, extrusion, provenance and "
                     "bounds passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
