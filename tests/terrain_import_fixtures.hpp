#pragma once
// R095.c terrain import fixtures (ADR 0168 section 6), generated in code: no third-party
// survey data. One synthetic site in local metres, written at 4 decimals:
//
//   - a hill: 16 rotated, concentric ellipses at 0.5 m intervals (100.5 .. 108.0 m), the
//     summit as a 2D POLYLINE with its elevation on the polyline;
//   - a valley: 8 hairpin (U-shaped) contours at 0.5 m intervals (99.5 .. 96.0 m) whose arms
//     run past the boundary, so they are clipped (the hairpin is the flat-triangle case);
//   - a 3D POLYLINE breakline and a LINE contour between them;
//   - a boundary (closed LWPOLYLINE on BOUNDARY) and a pond hole (closed LWPOLYLINE on POND).
//
// terrainContoursMetres() is that site; terrainContoursUsft() the same values in US survey
// feet at 6 decimals with a unitless header; terrainUtm() the site translated by
// (512345.678 E, 5234567.891 N, 1850 m datum), written exactly in decimal; terrainXyz() its
// contour and breakline vertices as an "E N Z" point file; terrainInvalid() the problem cases.
// Shuffling permutes entities, reverses open polylines and rotates and reverses rings.
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <numeric>
#include <optional>
#include <vector>

namespace sketchy::terrain_fixture {

enum class Kind { LwPolyline, Polyline2d, Polyline3d, Line };
struct Item {
    Kind kind{Kind::LwPolyline};
    QString layer;
    bool closed{};
    std::vector<std::array<std::int64_t, 3>> q; // coordinates in 1e-4 m units
    std::vector<double> bulges;                 // per vertex, LWPOLYLINE only
    std::uint64_t handle{};
};
inline std::int64_t quantum(double metres) { return std::llround(metres * 1e4); }
inline std::array<std::int64_t, 3> at(double x, double y, double z) {
    return {quantum(x), quantum(y), quantum(z)};
}

// The site.
inline std::vector<Item> site() {
    std::vector<Item> items;
    std::uint64_t handle = 0x100;
    auto add = [&](Item item) {
        item.handle = handle++;
        items.push_back(std::move(item));
    };
    const double pi = std::numbers::pi;
    // Hill.
    for (int k = 0; k < 16; ++k) {
        const double a = 38 - 2.3 * k, b = 24 - 1.45 * k, z = 100.5 + 0.5 * k;
        const double rotation = 0.3, cx = -45.37, cy = 3.13;
        int n = std::max(13, int(2 * pi * std::sqrt((a * a + b * b) / 2) / 2.5));
        if (n % 2 == 0)
            ++n;
        Item item{k == 15 ? Kind::Polyline2d : Kind::LwPolyline, "CONTOURS", true, {}, {}, 0};
        for (int i = 0; i < n; ++i) {
            const double t = 2 * pi * i / n + 0.1234 + 0.05 * k;
            const double ex = a * std::cos(t), ey = b * std::sin(t);
            item.q.push_back(at(cx + ex * std::cos(rotation) - ey * std::sin(rotation),
                                cy + ex * std::sin(rotation) + ey * std::cos(rotation), z));
        }
        add(std::move(item));
    }
    // Valley: hairpins open to the north, arms past the boundary.
    for (int j = 0; j < 8; ++j) {
        const double x0 = 52.17, w = 30 - 3.4 * j, yb = -42 + 4.1 * j, z = 99.5 - 0.5 * j;
        Item item{Kind::LwPolyline, "CONTOURS", false, {}, {}, 0};
        auto arm = [&](double y) { return 0.4 * std::sin(y / 7 + j); };
        std::vector<double> left;
        for (double y = 70 - 0.37 * j; y > yb + 0.5; y -= 2.3)
            left.push_back(y);
        for (const double y : left)
            item.q.push_back(at(x0 - w + arm(y), y, z));
        const int n = std::max(7, int(pi * w / 2.1));
        for (int i = 0; i <= n; ++i) {
            const double t = pi + pi * (i + 0.5) / (n + 1);
            item.q.push_back(at(x0 + w * std::cos(t), yb + w * std::sin(t), z));
        }
        for (double y = yb + 0.91 + 0.53 * j; y < 70; y += 2.45)
            item.q.push_back(at(x0 + w + arm(y + 3), y, z));
        add(std::move(item));
    }
    // Breakline (3D POLYLINE) and a LINE contour between hill and valley.
    {
        Item item{Kind::Polyline3d, "BREAKLINES", false, {}, {}, 0};
        for (int i = 0; i <= 20; ++i)
            item.q.push_back(at(7 + 0.3 * std::sin(i), -30 + 3 * i, 99.8 + 0.02 * i));
        add(std::move(item));
    }
    add({Kind::Line, "CONTOURS", false, {at(0.25, 40.5, 100), at(12.75, 44.25, 100)}, {}, 0});
    // Boundary and pond.
    add({Kind::LwPolyline,
         "BOUNDARY",
         true,
         {at(-95.3, -55.2, 100), at(95.1, -54.8, 100), at(94.7, 55.4, 100), at(-94.9, 54.9, 100)},
         {},
         0});
    {
        Item item{Kind::LwPolyline, "POND", true, {}, {}, 0};
        for (int i = 0; i < 6; ++i) {
            const double t = 2 * pi * i / 6 + 0.2;
            item.q.push_back(at(52.48 + 3.2 * std::cos(t), -5.17 + 3.2 * std::sin(t), 96));
        }
        add(std::move(item));
    }
    return items;
}

// Value writers: exact 4-decimal metres from quanta, or another unit at 6 decimals.
inline QString metres(std::int64_t q) {
    const bool negative = q < 0;
    const auto magnitude = negative ? -q : q;
    return QString("%1%2.%3")
        .arg(negative ? "-" : "")
        .arg(magnitude / 10000)
        .arg(magnitude % 10000, 4, 10, QChar('0'));
}
struct Writer {
    std::array<std::int64_t, 3> offset{}; // quanta added before writing
    double unitMetres{1};                 // write value / unitMetres at 6 decimals when != 1
    QString value(std::int64_t q, int axis) const {
        const auto shifted = q + offset[axis];
        if (unitMetres == 1)
            return metres(shifted);
        return QString::number(double((long double)shifted / 10000.0L / (long double)unitMetres),
                               'f', 6);
    }
};

inline void shuffle(std::vector<Item> &items) {
    // A fixed permutation (multiplicative stride) plus reversed and rotated polylines.
    const std::size_t n = items.size();
    std::vector<Item> out;
    std::size_t stride = 7;
    while (std::gcd(stride, n) != 1)
        stride += 2;
    for (std::size_t i = 0; i < n; ++i)
        out.push_back(items[(i * stride + 3) % n]);
    for (std::size_t i = 0; i < out.size(); ++i) {
        auto &item = out[i];
        if (item.closed) {
            std::rotate(item.q.begin(), item.q.begin() + std::ptrdiff_t(i % item.q.size()),
                        item.q.end());
            if (i % 2)
                std::reverse(item.q.begin(), item.q.end());
        } else if (i % 2 == 0)
            std::reverse(item.q.begin(), item.q.end());
    }
    items = std::move(out);
}

inline QByteArray dxf(const std::vector<Item> &items, const Writer &writer,
                      std::optional<int> insunits) {
    QStringList out;
    auto pair = [&](int code, const QString &value) { out << QString::number(code) << value; };
    pair(0, "SECTION");
    pair(2, "HEADER");
    pair(9, "$ACADVER");
    pair(1, "AC1015");
    if (insunits) {
        pair(9, "$INSUNITS");
        pair(70, QString::number(*insunits));
    }
    pair(0, "ENDSEC");
    pair(0, "SECTION");
    pair(2, "ENTITIES");
    std::uint64_t extra = 0x8000;
    auto hex = [](std::uint64_t value) { return QString::number(value, 16).toUpper(); };
    for (const auto &item : items) {
        const auto &q = item.q;
        switch (item.kind) {
        case Kind::LwPolyline:
            pair(0, "LWPOLYLINE");
            pair(5, hex(item.handle));
            pair(8, item.layer);
            pair(90, QString::number(q.size()));
            pair(70, item.closed ? "1" : "0");
            pair(38, writer.value(q.front()[2], 2));
            for (std::size_t i = 0; i < q.size(); ++i) {
                pair(10, writer.value(q[i][0], 0));
                pair(20, writer.value(q[i][1], 1));
                if (i < item.bulges.size() && item.bulges[i] != 0)
                    pair(42, QString::number(item.bulges[i], 'g', 17));
            }
            break;
        case Kind::Polyline2d:
        case Kind::Polyline3d: {
            const bool threeD = item.kind == Kind::Polyline3d;
            pair(0, "POLYLINE");
            pair(5, hex(item.handle));
            pair(8, item.layer);
            pair(66, "1");
            pair(10, "0.0");
            pair(20, "0.0");
            pair(30, threeD ? QString("0.0") : writer.value(q.front()[2], 2));
            pair(70, QString::number((item.closed ? 1 : 0) + (threeD ? 8 : 0)));
            for (const auto &v : q) {
                pair(0, "VERTEX");
                pair(5, hex(extra++));
                pair(8, item.layer);
                pair(10, writer.value(v[0], 0));
                pair(20, writer.value(v[1], 1));
                pair(30, threeD ? writer.value(v[2], 2) : QString("0.0"));
                pair(70, threeD ? "32" : "0");
            }
            pair(0, "SEQEND");
            pair(5, hex(extra++));
            pair(8, item.layer);
            break;
        }
        case Kind::Line:
            pair(0, "LINE");
            pair(5, hex(item.handle));
            pair(8, item.layer);
            pair(10, writer.value(q[0][0], 0));
            pair(20, writer.value(q[0][1], 1));
            pair(30, writer.value(q[0][2], 2));
            pair(11, writer.value(q[1][0], 0));
            pair(21, writer.value(q[1][1], 1));
            pair(31, writer.value(q[1][2], 2));
            break;
        }
    }
    pair(0, "ENDSEC");
    pair(0, "EOF");
    return (out.join('\n') + '\n').toUtf8();
}

inline constexpr std::array<std::int64_t, 3> utmOffset{5123456780, 52345678910, 18500000};

inline QByteArray terrainContoursMetres(bool shuffled = false) {
    auto items = site();
    if (shuffled)
        shuffle(items);
    return dxf(items, {}, 6);
}
inline QByteArray terrainContoursUsft() {
    Writer writer;
    writer.unitMetres = 1200.0 / 3937.0;
    return dxf(site(), writer, std::nullopt); // unitless: needs the explicit unit
}
inline QByteArray terrainUtm() { return dxf(site(), {utmOffset, 1}, 6); }
inline QByteArray terrainXyz(bool utm, bool shuffled = false) {
    Writer writer;
    if (utm)
        writer.offset = utmOffset;
    QStringList lines;
    for (const auto &item : site())
        if (item.layer == "CONTOURS" || item.layer == "BREAKLINES")
            for (const auto &v : item.q)
                lines << writer.value(v[0], 0) + " " + writer.value(v[1], 1) + " " +
                             writer.value(v[2], 2);
    if (shuffled)
        for (qsizetype i = 0; i < lines.size(); ++i)
            lines.swapItemsAt(i, (i * 7919 + 13) % lines.size());
    return (lines.join('\n') + '\n').toUtf8();
}

// The problem cases (imported with roles BOUNDARY=boundary, CONTOURS=contour):
//   - 40 vertical LINE contours at 10 m crossed by one LINE at 11 m: 40 crossing pairs;
//   - a self-crossing (figure-eight) contour at 12 m;
//   - two LINEs sharing an end at 13 m and 14 m: a conflicting duplicate;
//   - a varying-Z 3D POLYLINE on CONTOURS: a "contour" imported as a breakline;
//   - a contour with a repeated vertex (zero-length segment) and one with a spike;
//   - a contour with an arc segment (bulge);
//   - a contour wholly outside the boundary; POINT and TEXT entities (ignored).
inline QByteArray terrainInvalid() {
    std::vector<Item> items;
    std::uint64_t handle = 0x200;
    auto add = [&](Item item) {
        item.handle = handle++;
        items.push_back(std::move(item));
    };
    add({Kind::LwPolyline,
         "BOUNDARY",
         true,
         {at(-10, -10, 0), at(80, -10, 0), at(80, 30, 0), at(-10, 30, 0)},
         {},
         0});
    for (int i = 0; i < 40; ++i)
        add({Kind::Line, "CONTOURS", false, {at(i + 0.5, 0, 10), at(i + 0.5, 10, 10)}, {}, 0});
    add({Kind::Line, "CONTOURS", false, {at(-1, 5, 11), at(45, 5, 11)}, {}, 0});
    add({Kind::LwPolyline,
         "CONTOURS",
         false,
         {at(50, 0, 12), at(54, 4, 12), at(54, 0, 12), at(50, 4, 12)},
         {},
         0});
    add({Kind::Line, "CONTOURS", false, {at(60, 0, 13), at(62, 0, 13)}, {}, 0});
    add({Kind::Line, "CONTOURS", false, {at(60, 0, 14), at(60, 2, 14)}, {}, 0});
    add({Kind::Polyline3d,
         "CONTOURS",
         false,
         {at(65, 10, 1), at(67, 12, 2), at(69, 10, 3)},
         {},
         0});
    add({Kind::LwPolyline,
         "CONTOURS",
         false,
         {at(70, 20, 15), at(72, 20, 15), at(72, 20, 15), at(74, 21, 15)},
         {},
         0});
    add({Kind::LwPolyline,
         "CONTOURS",
         false,
         {at(40, 20, 18), at(44, 20, 18), at(42, 20, 18), at(42, 24, 18)},
         {},
         0});
    add({Kind::LwPolyline, "CONTOURS", false, {at(60, 20, 16), at(64, 20, 16)}, {0.5, 0}, 0});
    add({Kind::Line, "CONTOURS", false, {at(100, 100, 17), at(105, 100, 17)}, {}, 0});
    auto bytes = dxf(items, {}, 6);
    // Two entities that are not terrain sources.
    bytes.replace("0\nENDSEC\n0\nEOF",
                  "0\nPOINT\n5\n2FF\n8\nCONTOURS\n10\n1.0\n20\n1.0\n30\n5.0\n"
                  "0\nTEXT\n5\n2FE\n8\nCONTOURS\n10\n1.0\n20\n1.0\n30\n0.0\n40\n1.0\n1\nNote\n"
                  "0\nENDSEC\n0\nEOF");
    return bytes;
}

} // namespace sketchy::terrain_fixture
