#include "io/stl_source.hpp"
#include <QCoreApplication>
#include <QtEndian>
#include <bit>
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
    throw std::runtime_error("Malformed STL must reject");
}
QByteArray binary() {
    QByteArray bytes(134, 0);
    bytes.replace(0, 5, "solid");
    qToLittleEndian<quint32>(1, bytes.data() + 80);
    const std::array<float, 12> values{0, 0, 1, 0, 0, 0, 2000, 0, 0, 0, 3000, 0};
    for (size_t i = 0; i < values.size(); ++i)
        qToLittleEndian<quint32>(std::bit_cast<quint32>(values[i]), bytes.data() + 84 + i * 4);
    return bytes;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        const QByteArray ascii =
            "solid Triangle\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 2000 0 0\nvertex "
            "0 3000 0\nendloop\nendfacet\nendsolid Triangle\n";
        for (const auto bytes : {ascii, binary()}) {
            const auto parsed = parseStl(bytes, {.001, StlUpAxis::Y});
            check(parsed.facets.size() == 1, "One source facet");
            check(parsed.facets[0].vertices[1] == Vec3{2, 0, 0} &&
                      parsed.facets[0].vertices[2] == Vec3{0, 0, 3},
                  "Independent millimetres/Y-up oracle");
            check(parsed.facets[0].normal == Vec3{0, -1, 0},
                  "Normal rotates without length scaling");
        }
        check(parseStl(binary(), {1, StlUpAxis::Z}).report["format"] == "binary STL",
              "Exact count/length identifies binary even with solid header");
        auto attr = binary();
        qToLittleEndian<quint16>(0x8001, attr.data() + 132);
        check(parseStl(attr, {1, StlUpAxis::Z}).report["nonzeroAttributeWords"].toInt() == 1,
              "Vendor attribute words counted");
        auto zero = ascii;
        zero.replace("normal 0 0 1", "normal 0 0 0");
        check(parseStl(zero, {1, StlUpAxis::Z}).report["zeroNormals"].toInt() == 1,
              "Zero normals retained for explicit conversion report");
        auto tiny = ascii;
        tiny.replace("normal 0 0 1", "normal 0 0 1e-320");
        check(parseStl(tiny, {1, StlUpAxis::Z}).facets[0].normal == Vec3{0, 0, 1},
              "Subnormal ASCII normal normalizes without overflow");
        QByteArray tooMany(84 + 50 * 100001, 0);
        qToLittleEndian<quint32>(100001, tooMany.data() + 80);
        rejects([&] { parseStl(tooMany, {1, StlUpAxis::Z}); });
        auto small = ascii;
        small.replace("2000", "0.0001");
        small.replace("3000", "0.0002");
        check(parseStl(small, {1, StlUpAxis::Z}).report["degenerateFacets"].toInt() == 0,
              "Small valid triangle uses area-dimensional tolerance");
        auto wrong = ascii;
        wrong.replace("normal 0 0 1", "normal 0 0 -1");
        check(parseStl(wrong, {1, StlUpAxis::Z}).report["normalsDisagreeWithWinding"].toInt() == 1,
              "Winding/normal mismatch counted");
        auto collapsed = ascii;
        collapsed.replace("vertex 0 3000 0", "vertex 4000 0 0");
        check(parseStl(collapsed, {1, StlUpAxis::Z}).report["degenerateFacets"].toInt() == 1,
              "Degeneracy retained for explicit repair decision");
        auto second = ascii;
        second.replace("Triangle", "Second");
        check(parseStl(ascii + second, {1, StlUpAxis::Z}).solids.size() == 2,
              "Multiple named ASCII solids");
        auto crlf = ascii;
        crlf.replace("\n", "\r\n");
        check(parseStl(QByteArray::fromHex("efbbbf") + crlf, {1, StlUpAxis::Z}).facets.size() == 1,
              "UTF-8 BOM and CRLF");
        for (auto text :
             {QByteArray{}, QByteArray("solid empty\nendsolid empty\n"),
              ascii.left(ascii.size() - 19), ascii + "junk\n", ascii + QByteArray(1, '\0')})
            rejects([&] { parseStl(text, {1, StlUpAxis::Z}); });
        for (const auto pair : std::vector<std::pair<QByteArray, QByteArray>>{
                 {"outer loop", "outer wrong"},
                 {"endloop", "endfacet"},
                 {"endsolid Triangle", "endsolid Other"},
                 {"normal 0 0 1", "normal nan 0 1"},
                 {"vertex 2000 0 0", "vertex inf 0 0"},
                 {"vertex 2000 0 0", "vertex 2000 0"},
                 {"vertex 2000 0 0", "vertex 2000 0 0 7"},
                 {"endloop", "vertex 1 2 3\nendloop"}}) {
            auto bad = ascii;
            bad.replace(pair.first, pair.second);
            rejects([&] { parseStl(bad, {1, StlUpAxis::Z}); });
        }
        auto bad = binary();
        bad.chop(1);
        rejects([&] { parseStl(bad, {1, StlUpAxis::Z}); });
        bad = binary();
        bad += 'x';
        rejects([&] { parseStl(bad, {1, StlUpAxis::Z}); });
        bad = binary();
        qToLittleEndian<quint32>(0xffffffff, bad.data() + 80);
        rejects([&] { parseStl(bad, {1, StlUpAxis::Z}); });
        bad = binary();
        qToLittleEndian<quint32>(0x7fc00000, bad.data() + 84);
        rejects([&] { parseStl(bad, {1, StlUpAxis::Z}); });
        rejects([&] { parseStl(ascii, {0, StlUpAxis::Z}); });
        rejects([&] { parseStl(ascii, {1, StlUpAxis(99)}); });
        rejects(
            [&] { parseStl("solid " + QByteArray(513, 'a') + "\n" + ascii, {1, StlUpAxis::Z}); });
        rejects([&] { parseStl(QByteArray(64 * 1024 * 1024 + 1, 'x'), {1, StlUpAxis::Z}); });
        std::cout << "STL bounded binary/ASCII grammar, count, units, axis, normals and source "
                     "reports passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
