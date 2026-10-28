#include "io/procedural_textures.hpp"
#include "io/texture_presets.hpp"
#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QImage>
#include <QString>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace sketchy;

namespace {

void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}

std::string sha256(const QByteArray &bytes) {
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toStdString();
}

QImage decode(const QByteArray &bytes) {
    QImage image;
    check(image.loadFromData(bytes, "JPEG"), "The texture decodes as JPEG");
    return image.convertToFormat(QImage::Format_RGB888);
}

// Mean of the red channel over the pixels where selected(x, y) holds; the tile is neutral, so
// red is the luminance.
template <class Select> double mean(const QImage &image, Select selected) {
    double sum = 0;
    size_t count = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (selected(x, y)) {
                sum += qRed(image.pixel(x, y)) / 255.0;
                ++count;
            }
    check(count > 0, "The sample selects pixels");
    return sum / double(count);
}

// The ADR 0174 "CC0 presets: files" table: the embedded file, its SHA-256 and its size in bytes.
struct EmbeddedFile {
    const char *resource;
    const char *sha256;
    size_t bytes;
    int width, height;
};

constexpr std::array<EmbeddedFile, 9> embedded{{
    {"wood-light-ash.jpg", "91a0846b4578f6d333a7fa950ac72c87b661eb0d23b45e7a015e89310e4a6b5e",
     30728, 512, 512},
    {"wood-dark-oak.jpg", "ff787eabf5b8b2638bae2ad4336dcc9f59fa03775a1e5320be8870009cda917d", 32898,
     512, 512},
    {"stone-rubble-wall.jpg", "6874d83108366863e409de5d23147a4f0602cfd36c4c42bf0232331e302ffe24",
     63050, 512, 512},
    {"metal-corrugated.jpg", "1e1a5803ac390860b34830eb64c2191b0684c758e0bbf0e6852dc18dd6c64d55",
     53376, 512, 512},
    {"grass.jpg", "b46a499a537948bf457533d6e522e545536e83d3a71f7e3cd4276191ca85c3a0", 89503, 512,
     512},
    {"gravel.jpg", "489e8fb30eb98b8f67158f9a677ebe3a239c8f88b0d6695e706d1b211a47dba8", 120454, 512,
     512},
    {"asphalt.jpg", "d0b08e7ac43f3a6ff5e118a0261b3d496f607c37be8731e1bccc95a6790aef8d", 25760, 512,
     512},
    {"roof-tiles-clay.jpg", "e6a214840c55dd99e83daf8331931322985bc8c37ea074ebf14fad3945caf013",
     71553, 512, 512},
    {"fabric-wool-herringbone.jpg",
     "6d18bc27c52f70dc73e5e53144f80c62b2cf281ea6be7ae49c562d71d051e04d", 56842, 502, 512},
}};

// Fixture SHA-256 of each generated preset's JPEG, with its size in bytes. The bytes are identical
// on the CI image (sketchyup-ci-pinned, Qt 6.11.2, libjpeg-turbo 3.2.0) and on Qt 6.12.0, so the
// file itself is pinned rather than the decoded pixels.
struct Generated {
    ProceduralTexture texture;
    const char *name;
    const char *sha256;
    size_t bytes;
};

constexpr std::array<Generated, 6> generated{{
    {ProceduralTexture::BrickBond, "Brick, running bond",
     "7a74c8734f3b19ed10f9146971760a1e61ef86ce3157cee8db05ff3cbb7307c4", 25817},
    {ProceduralTexture::ConcreteCast, "Concrete, cast",
     "a158a79002ea5e499cb94da14895cf647bf640d9e646c04be5e360998f6448e5", 38242},
    {ProceduralTexture::FloorTile600, "Floor tile, 600 mm",
     "579a283aec1fc608d801538a9ee73bb43fe8336933f9e5d7f8f75d6f8dabf8a1", 5593},
    {ProceduralTexture::PlasterFine, "Plaster, fine",
     "56a756f5085eb3de7b3f8b00e404201ac234c3152dedf7fa41ef487482d43856", 7054},
    {ProceduralTexture::MetalBrushed, "Metal, brushed",
     "323caca3925dc2f39db85df983d9b8698438e39ddd9520a684f6544c47a73b50", 15006},
    {ProceduralTexture::GlassReeded, "Glass, reeded",
     "126fe581d88f479d1a5ea54ca1a029eb2fc7794dcb92373fcf1a01c82a65deb9", 17574},
}};

constexpr size_t kibibyte = 1024;
constexpr size_t mebibyte = 1024 * 1024;

void checkPresetTable() {
    const auto presets = texturePresets();
    check(presets.size() == 15, "The Q1 list has fifteen textured presets");
    constexpr std::array<std::string_view, 15> order{"Wood, light",
                                                     "Wood, dark",
                                                     "Brick, running bond",
                                                     "Concrete, cast",
                                                     "Stone, rubble wall",
                                                     "Floor tile, 600 mm",
                                                     "Plaster, fine",
                                                     "Metal, brushed",
                                                     "Metal, corrugated",
                                                     "Glass, reeded",
                                                     "Grass",
                                                     "Gravel",
                                                     "Asphalt",
                                                     "Roof tiles, clay",
                                                     "Fabric, wool"};
    size_t cc0 = 0, procedural = 0;
    for (size_t i = 0; i < presets.size(); ++i) {
        check(presets[i].name == order[i], "Presets appear in the Q1 order");
        check(presets[i].tint.size() == 7 && presets[i].tint.front() == '#', "A tint is #RRGGBB");
        check(presets[i].opacity == 100 ||
                  (presets[i].name == "Glass, reeded" && presets[i].opacity == 35),
              "Opacity follows the Q1 table");
        check(presets[i].tileWidth > 0 && presets[i].tileHeight > 0, "A tile size is set");
        check(!presets[i].licence.empty(), "Each preset names its source and licence");
        if (presets[i].resource.empty())
            ++procedural;
        else
            ++cc0;
    }
    check(cc0 == 9 && procedural == 6, "Nine CC0 presets and six generated presets");
    // The tile sizes of the Q1 table, in metres, in the table's order. Each becomes the size of
    // the material a preset creates (R100.b2, ADR 0174 section 3).
    const std::array<std::array<double, 2>, 15> tiles{{{1.00, 1.00},
                                                       {1.00, 1.00},
                                                       {0.90, 0.90},
                                                       {2.00, 2.00},
                                                       {1.50, 1.50},
                                                       {0.60, 0.60},
                                                       {2.00, 2.00},
                                                       {1.00, 1.00},
                                                       {1.12, 1.12},
                                                       {0.30, 0.30},
                                                       {2.00, 2.00},
                                                       {1.90, 1.90},
                                                       {2.10, 2.10},
                                                       {2.50, 2.50},
                                                       {0.27, 0.28}}};
    for (size_t i = 0; i < presets.size(); ++i)
        check(presets[i].tileWidth == tiles[i][0] && presets[i].tileHeight == tiles[i][1],
              "Tile sizes match the Q1 table");
    check(presets[9].tint == "#b7d9e5" && presets[9].opacity == 35,
          "Glass, reeded keeps the flat Glass tint and opacity");
}

void checkEmbedded(const std::string &sourceDir) {
    size_t total = 0;
    for (const auto &file : embedded) {
        const auto bytes = [&] {
            QFile source(QString::fromUtf8(sourceDir.c_str()) + "/resources/textures/" +
                         QString::fromLatin1(file.resource));
            check(source.open(QIODevice::ReadOnly), "The embedded texture is in the source tree");
            return source.readAll();
        }();
        check(sha256(bytes) == file.sha256, "The embedded texture matches the ADR SHA-256");
        check(size_t(bytes.size()) == file.bytes, "The embedded size matches the ADR table");
        total += bytes.size();
        const auto image = decode(bytes);
        check(image.width() == file.width && image.height() == file.height,
              "The embedded texture has its recorded dimensions");
    }
    check(total == 544164, "The nine CC0 files total 544,164 bytes (ADR 0174)");
}

// The compiled-in copy of each CC0 texture is the verified file, byte for byte.
void checkResources() {
    size_t cc0 = 0;
    for (const auto &preset : texturePresets()) {
        if (preset.resource.empty())
            continue;
        ++cc0;
        const auto bytes = texturePresetBytes(preset);
        bool matched = false;
        for (const auto &file : embedded)
            if (preset.resource == file.resource) {
                check(sha256(bytes) == file.sha256, "The compiled-in texture matches the ADR");
                matched = true;
            }
        check(matched, "Each CC0 preset names an embedded file");
    }
    check(cc0 == embedded.size(), "Every embedded file is used by a preset");
}

QString readText(const std::string &sourceDir, const char *relative) {
    QFile file(QString::fromUtf8(sourceDir.c_str()) + "/" + relative);
    check(file.open(QIODevice::ReadOnly), "A source file the notices check reads exists");
    return QString::fromUtf8(file.readAll());
}

// The notice file credits every CC0 preset's Poly Haven asset, CMake collects it into
// THIRD_PARTY_NOTICES, and dependencies.json records it.
void checkNotices(const std::string &sourceDir) {
    const auto notice = readText(sourceDir, "third_party/textures/PolyHaven-CC0.txt");
    check(notice.contains("CC0 1.0 Universal") && notice.contains("https://polyhaven.com"),
          "The notice names the CC0 licence and Poly Haven");
    const auto cmake = readText(sourceDir, "CMakeLists.txt");
    check(cmake.contains("\ninstall(FILES third_party/textures/PolyHaven-CC0.txt DESTINATION "
                         "share/licenses/sketchyup RENAME Textures-PolyHaven-CC0)\n"),
          "CMake installs the notice so release-artifacts.sh collects it");
    const auto dependencies = readText(sourceDir, "dependencies.json");
    check(dependencies.contains("\"Poly Haven CC0 textures\"") &&
              dependencies.contains("\"license\": \"CC0-1.0\""),
          "dependencies.json records the Poly Haven CC0 textures");
    size_t credited = 0;
    for (const auto &preset : texturePresets()) {
        if (preset.resource.empty())
            continue;
        // The licence line names the asset: "CC0 1.0 · Poly Haven <id> · <creator>".
        const auto licence =
            QString::fromUtf8(preset.licence.data(), qsizetype(preset.licence.size()));
        const auto id = licence.split(QStringLiteral("Poly Haven "))
                            .last()
                            .split(QStringLiteral(" · "))
                            .first();
        check(notice.contains("https://polyhaven.com/a/" + id),
              "The notice credits every CC0 asset");
        ++credited;
    }
    check(credited == 9, "The notice credits nine assets");
}

void checkBudgets() {
    size_t total = 0;
    for (const auto &preset : texturePresets()) {
        const auto bytes = texturePresetBytes(preset);
        const auto image = decode(bytes);
        check(bytes.size() <= 200 * kibibyte, "Each texture is at most 200 KiB");
        check(image.width() <= 512 && image.height() <= 512, "Each texture is at most 512 px");
        total += size_t(bytes.size());
    }
    std::cout << "texture set: " << total << " bytes\n";
    check(total <= mebibyte, "The fifteen presets total at most 1 MiB");
}

void checkGenerated() {
    size_t total = 0;
    for (const auto &entry : generated) {
        const auto first = proceduralTextureJpeg(entry.texture);
        const auto second = proceduralTextureJpeg(entry.texture);
        check(first == second, "A generated texture is deterministic within a run");
        const auto image = decode(first);
        check(image.width() == 512 && image.height() == 512,
              "A generated texture is 512 px square");
        check(first.size() <= 200 * kibibyte, "A generated texture is at most 200 KiB");
        std::cout << "generated " << entry.name << ": " << first.size() << " bytes sha256 "
                  << sha256(first) << '\n';
        total += size_t(first.size());
        check(sha256(first) == entry.sha256, "A generated texture matches its pinned SHA-256");
        check(first.size() == entry.bytes, "A generated texture matches its pinned size");
    }
    std::cout << "generated set: " << total << " bytes\n";
}

// The floor tile's joint is darker than its field by a clearly visible margin (8 px at 0.62,
// against 0.95; the ADR draft was 3 px at 0.70, which the owner found nearly invisible).
void checkFloorGrout() {
    constexpr int joint = 8;
    const auto image = decode(proceduralTextureJpeg(ProceduralTexture::FloorTile600));
    const auto grout = mean(image, [](int x, int y) { return x < joint || y < joint; });
    const auto field = mean(image, [](int x, int y) { return x >= joint && y >= joint; });
    std::cout << "floor grout " << grout << " field " << field << '\n';
    check(grout > .55 && grout < .65, "Grout luminance is about 0.62");
    check(field > .93 && field < .97, "Field luminance is about 0.95");
    check(field - grout >= .30, "Grout is clearly visible against the field");
}

// Brick mortar is darker than the brick faces, as the ADR's luminance values give.
void checkBrickMortar() {
    const auto image = decode(proceduralTextureJpeg(ProceduralTexture::BrickBond));
    const auto bricks = mean(image, [&](int x, int y) { return qRed(image.pixel(x, y)) > 225; });
    const auto mortar = mean(image, [&](int x, int y) { return qRed(image.pixel(x, y)) < 215; });
    std::cout << "brick faces " << bricks << " mortar " << mortar << '\n';
    check(bricks - mortar >= .15, "Brick mortar is clearly darker than the brick faces");
}

} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const std::string sourceDir = SOURCE_DIR;
    try {
        checkPresetTable();
        checkEmbedded(sourceDir);
        checkResources();
        checkBudgets();
        checkNotices(sourceDir);
        checkGenerated();
        checkFloorGrout();
        checkBrickMortar();
        std::cout << "Textured presets: table, embedded files, budgets, generated determinism, "
                     "grout and mortar contrast passed.\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
