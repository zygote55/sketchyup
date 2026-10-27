#include "io/procedural_textures.hpp"
#include <QBuffer>
#include <QImage>
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace sketchy {
namespace {

constexpr int side = 512;

// SplitMix64 (Steele, Lea and Flood, 2014): unsigned 64-bit integer arithmetic only, so every
// draw is the same on every platform. Each generator takes its seed from this file.
class SplitMix64 {
  public:
    explicit SplitMix64(std::uint64_t seed) : state_(seed) {}
    std::uint64_t next() {
        std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
    // In [0, 1): the top 53 bits of the next draw.
    double uniform() { return double(next() >> 11) * 0x1.0p-53; }

  private:
    std::uint64_t state_;
};

int wrap(int index, int count) { return ((index % count) + count) % count; }

// Periodic value noise on a lattice of random values that wraps at its edges.
class Lattice {
  public:
    Lattice(SplitMix64 &random, int columns, int rows)
        : columns_(columns), rows_(rows), values_(size_t(columns) * size_t(rows)) {
        for (auto &value : values_)
            value = random.uniform();
    }

    // (u, v) in tile units; the result is in [0, 1).
    double at(double u, double v) const {
        const double x = u * columns_, y = v * rows_;
        const double fx = std::floor(x), fy = std::floor(y);
        const double tx = fade(x - fx), ty = fade(y - fy);
        const int x0 = wrap(int(fx), columns_), y0 = wrap(int(fy), rows_);
        const int x1 = wrap(x0 + 1, columns_), y1 = wrap(y0 + 1, rows_);
        const double top = value(x0, y0) + tx * (value(x1, y0) - value(x0, y0));
        const double bottom = value(x0, y1) + tx * (value(x1, y1) - value(x0, y1));
        return top + ty * (bottom - top);
    }

  private:
    static double fade(double t) { return t * t * (3 - 2 * t); }
    double value(int column, int row) const {
        return values_[size_t(row) * size_t(columns_) + size_t(column)];
    }

    int columns_, rows_;
    std::vector<double> values_;
};

// Octaves of value noise, each with twice the lattice density and half the amplitude of the one
// before. The sum is divided by its total amplitude, so the result is in [0, 1].
class Fbm {
  public:
    Fbm(SplitMix64 &random, int columns, int rows, int octaves) {
        for (int octave = 0; octave < octaves; ++octave)
            lattices_.emplace_back(random, columns << octave, rows << octave);
    }

    double at(double u, double v) const {
        double sum = 0, amplitude = 1, total = 0;
        for (const auto &lattice : lattices_) {
            sum += amplitude * lattice.at(u, v);
            total += amplitude;
            amplitude *= .5;
        }
        return sum / total;
    }

  private:
    std::vector<Lattice> lattices_;
};

// Box-Muller from two uniform draws, so the draw count per sample is fixed.
double gaussian(SplitMix64 &random) {
    const double u1 = std::max(random.uniform(), 0x1.0p-53);
    const double u2 = random.uniform();
    return std::sqrt(-2 * std::log(u1)) * std::cos(2 * std::numbers::pi * u2);
}

// The centre of pixel index i, as a fraction of the tile.
double tileFraction(int index) { return (index + .5) / side; }

// Pixel index to a point in millimetres across a tile of the given size.
double millimetres(int index, double tileMillimetres) {
    return (index + .5) * tileMillimetres / side;
}

using Luminance = std::vector<double>;

// 215 by 65 mm bricks, 10 mm joints, 75 mm courses, 12 courses in a 900 mm tile. Odd courses are
// offset by half a pitch (225 mm) and the joints and bricks take their own luminance.
Luminance brickBond() {
    SplitMix64 random(11);
    std::array<std::array<double, 4>, 12> offset{};
    for (auto &course : offset)
        for (auto &brick : course)
            brick = .04 * (2 * random.uniform() - 1);
    const Lattice fine(random, 64, 64);
    constexpr double mortar = .76;
    Luminance out(size_t(side) * side);
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x) {
            const double xm = millimetres(x, 900), ym = millimetres(y, 900);
            const int course = int(ym / 75);
            const double rise = ym - 75.0 * course;
            double value = mortar;
            if (rise < 65) {
                const double shift = course % 2 ? 112.5 : 0;
                const double run = std::fmod(xm + shift, 900.0);
                const int brick = int(run / 225);
                if (run - 225.0 * brick < 215)
                    value = .96 + offset[size_t(course)][size_t(brick)] +
                            .02 * (2 * fine.at(tileFraction(x), tileFraction(y)) - 1);
            }
            out[size_t(y) * side + x] = value;
        }
    return out;
}

// Cast concrete: a 2 m tile of fBm with 1,200 seeded pits and a speckle of sigma 0.01.
Luminance concreteCast() {
    SplitMix64 random(14);
    const Fbm base(random, 8, 8, 4);
    struct Pit {
        int x, y, radius;
    };
    std::vector<Pit> pits;
    pits.reserve(1200);
    for (int i = 0; i < 1200; ++i) {
        const int x = int(random.uniform() * side), y = int(random.uniform() * side);
        pits.push_back({x, y, 1 + int(random.uniform() * 3)});
    }
    Luminance out(size_t(side) * side);
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x)
            out[size_t(y) * side + x] = .85 + .15 * base.at(tileFraction(x), tileFraction(y));
    for (const auto &pit : pits) {
        for (int dy = -pit.radius; dy <= pit.radius; ++dy)
            for (int dx = -pit.radius; dx <= pit.radius; ++dx) {
                const double distance = std::hypot(double(dx), double(dy));
                if (distance >= pit.radius)
                    continue;
                auto &value =
                    out[size_t(wrap(pit.y + dy, side)) * side + size_t(wrap(pit.x + dx, side))];
                value = std::min(value, .65 + .35 * distance / pit.radius);
            }
    }
    for (auto &value : out)
        value += .01 * gaussian(random);
    return out;
}

// A 600 mm tile: an 8 px joint on the left and top edges at luminance 0.62, and a field of 0.95
// with fBm variation of plus or minus 0.015. The ADR draft had a 3 px joint at 0.70. The owner
// asked on 2026-10-27 for the grout to be clearly visible; at 3 px it vanishes when the 512 px tile
// repeats at normal distance, since there are no mipmaps (ADR 0065).
Luminance floorTile600() {
    SplitMix64 random(12);
    const Fbm field(random, 4, 4, 4);
    constexpr int groutWidth = 8;
    constexpr double grout = .62;
    Luminance out(size_t(side) * side);
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x) {
            const bool joint = x < groutWidth || y < groutWidth;
            out[size_t(y) * side + x] =
                joint ? grout : .95 + .015 * (2 * field.at(tileFraction(x), tileFraction(y)) - 1);
        }
    return out;
}

// Fine plaster: a 2 m tile of fBm, 4 octaves on 4 base cells, luminance 0.88 to 1.00.
Luminance plasterFine() {
    SplitMix64 random(13);
    const Fbm base(random, 4, 4, 4);
    Luminance out(size_t(side) * side);
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x)
            out[size_t(y) * side + x] = .88 + .12 * base.at(tileFraction(x), tileFraction(y));
    return out;
}

// Brushed metal: a row value smoothed by a 5-tap periodic kernel (1 2 3 2 1 over 9) gives streaks
// of plus or minus 0.03, and an anisotropic fBm on 8 by 64 base cells adds plus or minus 0.02.
Luminance metalBrushed() {
    SplitMix64 random(15);
    Luminance raw(side);
    for (auto &value : raw)
        value = 2 * random.uniform() - 1;
    const Fbm grain(random, 8, 64, 3);
    constexpr std::array<double, 5> kernel{1, 2, 3, 2, 1};
    Luminance out(size_t(side) * side);
    for (int y = 0; y < side; ++y) {
        double streak = 0;
        for (int k = 0; k < 5; ++k)
            streak += kernel[size_t(k)] * raw[size_t(wrap(y + k - 2, side))];
        streak /= 9;
        for (int x = 0; x < side; ++x)
            out[size_t(y) * side + x] =
                .85 + .03 * streak + .02 * (2 * grain.at(tileFraction(x), tileFraction(y)) - 1);
    }
    return out;
}

// Reeded glass: 12 vertical ribs at a 25 mm pitch in a 300 mm tile. Each rib's profile is the
// square root of (0.5 + 0.5 cos phi), which lies between 0.82 and 1.00 with a seeded offset of plus
// or minus 0.02 per rib.
Luminance glassReeded() {
    SplitMix64 random(16);
    std::array<double, 12> offset{};
    for (auto &rib : offset)
        rib = .02 * (2 * random.uniform() - 1);
    Luminance out(size_t(side) * side);
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x) {
            const double xm = millimetres(x, 300);
            const int rib = int(xm / 25);
            const double phase = std::fmod(xm, 25.0) / 25.0 - .5;
            const double profile = std::sqrt(.5 + .5 * std::cos(2 * std::numbers::pi * phase));
            out[size_t(y) * side + x] = .82 + .18 * profile + offset[size_t(rib)];
        }
    return out;
}

// One byte per channel: floor(value * 255 + 0.5), clamped to the byte range.
std::uint8_t quantise(double value) {
    return std::uint8_t(std::clamp(std::floor(value * 255 + .5), 0., 255.));
}

QByteArray encodeJpeg(const Luminance &luminance) {
    QImage image(side, side, QImage::Format_RGB888);
    for (int y = 0; y < side; ++y) {
        auto *row = image.scanLine(y);
        for (int x = 0; x < side; ++x) {
            const auto byte = quantise(luminance[size_t(y) * side + size_t(x)]);
            row[3 * x] = row[3 * x + 1] = row[3 * x + 2] = byte;
        }
    }
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "JPEG", 85))
        throw std::runtime_error("The procedural texture could not be encoded as JPEG");
    return bytes;
}

} // namespace

QByteArray proceduralTextureJpeg(ProceduralTexture texture) {
    switch (texture) {
    case ProceduralTexture::BrickBond:
        return encodeJpeg(brickBond());
    case ProceduralTexture::ConcreteCast:
        return encodeJpeg(concreteCast());
    case ProceduralTexture::FloorTile600:
        return encodeJpeg(floorTile600());
    case ProceduralTexture::PlasterFine:
        return encodeJpeg(plasterFine());
    case ProceduralTexture::MetalBrushed:
        return encodeJpeg(metalBrushed());
    case ProceduralTexture::GlassReeded:
        return encodeJpeg(glassReeded());
    }
    throw std::logic_error("Unknown procedural texture");
}

} // namespace sketchy
