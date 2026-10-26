// R102.a: the SKSKPP01 package writer (skp/package.cpp) and the checked decoder
// (io/skp_package.cpp) against each other, plus the decoder's hand-built bad packages:
// the wrong hash (a chunk the digest no longer covers), the truncated file, the bad magic,
// the bad reserved word, the trailing bytes, the oversize manifest, the bad index, the
// short loop and the oversize count. The writer and the decoder live in different targets
// on purpose: the application links the decoder and never the OpenSKP parser (ADR 0176
// decision 2); this test is the meeting point.
#include <algorithm>
#include "io/skp_package.hpp"
#include "skp/package.hpp"
#include "skp/sha256.hpp"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace wp = sketchyup::skp;  // the worker side (the writer)
namespace {
void check(bool ok, const char* message) {
    if (!ok) {
        std::fprintf(stderr, "skp package test failed: %s\n", message);
        std::exit(1);
    }
}
std::vector<std::uint8_t> bytesOf(const std::string& s) { return {s.begin(), s.end()}; }
void checkDecode(const std::vector<std::uint8_t>& bytes, const char* expected,
                 bool digest = false) {
    try {
        sketchy::decodeSkpPackage(bytes, sketchy::SkpPackageDecodeOptions{digest, true});
        std::fprintf(stderr, "expected a refusal: %s\n", expected);
        std::exit(1);
    } catch (const std::exception& problem) {
        const auto message = std::string(problem.what());
        if (message.find(expected) == std::string::npos) {
            std::fprintf(stderr, "wrong refusal\n  want: %s\n  got:  %s\n", expected,
                         message.c_str());
            std::exit(1);
        }
    }
}
void putU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(value & 0xFF);
    out.push_back((value >> 8) & 0xFF);
    out.push_back((value >> 16) & 0xFF);
    out.push_back((value >> 24) & 0xFF);
}
void putU64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (int i = 0; i <= 7; ++i)
        out.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xFF));
}
void putF64(std::vector<std::uint8_t>& out, double value) {
    std::uint64_t bits;
    std::memcpy(&bits, &value, 8);
    putU64(out, bits);
}
std::vector<std::uint8_t> skpRaw(const std::string& manifestJson,
                                 const std::vector<std::uint8_t>& geometry,
                                 const std::vector<std::uint8_t>& texture,
                                 std::uint32_t reserved = 0) {
    std::vector<std::uint8_t> out(8, 0);
    std::memcpy(out.data(), "SKSKPP01", 8);
    putU32(out, static_cast<std::uint32_t>(manifestJson.size()));
    putU32(out, reserved);
    putU64(out, geometry.size());
    putU64(out, texture.size());
    out.insert(out.end(), manifestJson.begin(), manifestJson.end());
    out.insert(out.end(), geometry.begin(), geometry.end());
    out.insert(out.end(), texture.begin(), texture.end());
    return out;
}
// The package's one definition: three vertices (0,0,0), (1,0,0), (1,1,0), one edge 0-1 and
// one triangle 0-1-2. The byte offsets of its fields, from the chunk's start.
constexpr int kVertexCount = 0;
constexpr int kVertices = 4;
constexpr int kEdgeCount = 4 + 72;         // 76
constexpr int kEdgeV1 = 76 + 4;           // 80
constexpr int kEdgeV2 = 80 + 4;           // 84
constexpr int kEdgeFlags = 84 + 1;        // 88
constexpr int kFaceCount = 88 + 1;        // 89
constexpr int kFaceFlags = 89 + 4;        // 93
constexpr int kFaceMatFront = 93 + 1;     // 94
constexpr int kFaceMatBack = 94 + 4;      // 98
constexpr int kFaceTag = 98 + 4;          // 102
constexpr int kFaceLoopCount = 102 + 4;   // 106
constexpr int kLoopSize = 106 + 4;        // 110
constexpr int kLoopIndex = 110 + 4;       // 114
constexpr int kChunkBytes = 130;
std::vector<std::uint8_t> geometryOf() {
    std::vector<std::uint8_t> g(kChunkBytes, 0);
    std::uint32_t vertexCount = 3;
    std::memcpy(g.data() + kVertexCount, &vertexCount, 4);
    std::uint64_t bits;
    auto writeVertex = [&](int offset, double x, double y, double z) {
        std::memcpy(&bits, &x, 8);
        std::memcpy(g.data() + offset, &bits, 8);
        std::memcpy(&bits, &y, 8);
        std::memcpy(g.data() + offset + 8, &bits, 8);
        std::memcpy(&bits, &z, 8);
        std::memcpy(g.data() + offset + 16, &bits, 8);
    };
    writeVertex(kVertices, 0.0, 0.0, 0.0);
    writeVertex(kVertices + 24, 1.0, 0.0, 0.0);
    writeVertex(kVertices + 48, 1.0, 1.0, 0.0);
    std::uint32_t edgeCount = 1;
    std::memcpy(g.data() + kEdgeCount, &edgeCount, 4);
    std::uint32_t one = 1, zero = 0;
    std::memcpy(g.data() + kEdgeV1, &zero, 4);
    std::memcpy(g.data() + kEdgeV2, &one, 4);
    std::memcpy(g.data() + kFaceCount, &one, 4);
    std::uint32_t none = 0xFFFFFFFFu;
    std::memcpy(g.data() + kFaceMatFront, &none, 4);
    std::memcpy(g.data() + kFaceMatBack, &none, 4);
    std::memcpy(g.data() + kFaceTag, &none, 4);
    std::memcpy(g.data() + kFaceLoopCount, &one, 4);
    std::uint32_t loopSize = 3;
    std::memcpy(g.data() + kLoopSize, &loopSize, 4);
    std::uint32_t loop[3] = {0, 1, 2};
    std::memcpy(g.data() + kLoopIndex, loop, 12);
    return g;
}
std::vector<std::uint8_t> textureOf() {
    std::vector<std::uint8_t> t(8, 0);
    t[0] = 0x89;
    t[1] = 'P';
    t[2] = 'N';
    t[3] = 'G';
    return t;
}
// A manifest the decoder accepts in full: the counts of the package above, one material, one
// tag, one texture. `edit` replaces one same-region slice of the manifest text.
std::vector<std::uint8_t> validPackage(const std::vector<std::uint8_t>& geometry,
                                      const std::vector<std::uint8_t>& texture,
                                      std::optional<std::pair<std::string, std::string>> edit =
                                          std::nullopt,
                                      std::uint32_t reserved = 0) {
    const auto digestOf = [](const std::vector<std::uint8_t>& data) {
        return sketchyup::skp::sha256_hex(data.data(), data.size());
    };
    std::string json =
        "{\"format\":\"sketchyup-skp-package\",\"version\":1,"
        "\"geometrySha256\":\"" +
        digestOf(geometry) +
        "\",\"textureSha256\":\"" +
        digestOf(texture) +
        "\",\"source\":{\"name\":\"box.skp\",\"size\":1000,\"sha256\":\"" +
        std::string(64, 'a') +
        "\"},\"formatVersion\":\"24.0.553\",\"container\":\"vff\",\"units\":\"Inches\","
        "\"unitScale\":0.0254,"
        "\"report\":{\"apiVersion\":1,"
        "\"counts\":{\"definitions\":1,\"vertices\":3,\"edges\":1,\"faces\":1,\"holes\":0,"
        "\"instances\":0,\"materials\":1,\"tags\":1,\"uvTransforms\":0,\"textures\":1},"
        "\"notices\":[{\"code\":\"note\",\"count\":1}],\"locations\":[],"
        "\"locationTotals\":{},\"omitted\":{},\"source\":{}},"
        "\"definitions\":[{\"name\":\"ROOT\",\"guid\":\"g1\",\"vertices\":3,\"edges\":1,"
        "\"faces\":1,\"instances\":0}],"
        "\"tags\":[{\"name\":\"hidden\",\"color\":[200,200,200],\"hidden\":true}],"
        "\"materials\":[{\"name\":\"red\",\"color\":[255,0,0,255],\"opacity\":1,"
        "\"texture\":0}],"
        "\"textures\":[{\"filename\":\"tex.png\",\"width\":100,\"height\":100,"
        "\"bytes\":" +
        std::to_string(texture.size()) + "}],";
    json += "\"counts\":{\"definitions\":1,\"vertices\":3,\"edges\":1,\"faces\":1,\"holes\":0,"
            "\"instances\":0,\"materials\":1,\"tags\":1,\"uvTransforms\":0,\"textures\":1}}";
    if (edit) {
        const auto from = json.find(edit->first);
        check(from != std::string::npos, "the edit anchor is missing");
        json.replace(from, edit->first.size(), edit->second);
    }
    return skpRaw(json, geometry, texture, reserved);
}
template <class A, class B>
bool sameCounts(const A& a, const B& b) {
    return a.definitions == b.definitions && a.vertices == b.vertices && a.edges == b.edges &&
           a.faces == b.faces && a.holes == b.holes && a.instances == b.instances &&
           a.materials == b.materials && a.tags == b.tags && a.uvTransforms == b.uvTransforms &&
           a.textures == b.textures;
}
}  // namespace

int main() {
    // The helper's own SHA-256 (no Qt in the worker): the FIPS 180-4 vectors.
    static const std::uint8_t abcd[4] = {'a', 'b', 'c', 'd'};
    check(wp::sha256_hex(abcd, 4) ==
              "88d4266fd4e6338d13b845fcf289579d209c897823b9217da3e161936f031589",
          "sha256 of abcd");
    static const std::uint8_t empty[1] = {};
    check(wp::sha256_hex(empty, 0) ==
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "sha256 of the empty message");

    // The package: one box with a holed face, an instance of it, two materials, a tag and a
    // texture.
    wp::SkpPackage sample;
    sample.formatVersion = "24.0.553";
    sample.container = "vff";
    sample.units = "Inches";
    sample.unitScale = 0.0254;
    sample.sourceName = "box.skp";
    sample.sourceSha256 = std::string(64, 'a');
    sample.sourceSize = 1000;
    auto& root = sample.definitions.emplace_back();
    root.name = "ROOT";
    root.vertices = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
    for (int v = 0; v < 4; ++v) {
        wp::SkpPackageEdge edge;
        edge.v1 = static_cast<std::uint32_t>(v);
        edge.v2 = static_cast<std::uint32_t>((v + 1) % 4);
        root.edges.push_back(edge);
    }
    wp::SkpPackageFace face;
    face.loops = {{0, 1, 2, 3}, {1, 2, 3}};  // a hole
    face.materialFront = 0;
    face.hasUvFront = true;
    face.uvFront = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    root.faces.push_back(face);
    wp::SkpPackageInstance instance;
    instance.definition = 1;
    instance.transform[12] = 0.254;
    root.instances.push_back(instance);
    auto& box = sample.definitions.emplace_back();
    box.name = "Box";
    box.vertices = {{0, 0, 0}, {0.254, 0, 0}, {0.254, 0.254, 0}};
    wp::SkpPackageMaterial red;
    red.name = "red";
    red.color = {255, 0, 0, 255};
    red.texture = 0;
    wp::SkpPackageMaterial blue;
    blue.name = "blue";
    sample.materials = {red, blue};
    wp::SkpPackageTag tag;
    tag.name = "hidden";
    tag.hidden = true;
    sample.tags = {tag};
    wp::SkpPackageTexture image;
    image.filename = "tex.png";
    image.width = 100;
    image.height = 100;
    static const std::uint8_t png[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
    image.bytes.assign(png, png + 8);
    sample.textures = {image};
    sample.counts.definitions = 2;
    sample.counts.vertices = 7;
    sample.counts.edges = 4;
    sample.counts.faces = 1;
    sample.counts.holes = 1;
    sample.counts.instances = 1;
    sample.counts.materials = 2;
    sample.counts.tags = 1;
    sample.counts.textures = 1;
    sample.counts.uvTransforms = 1;
    sample.notices["skippedVertex"] = 1;
    sample.sourceTotals = {{"edges", 4}, {"faces", 1}, {"instances", 1}, {"materials", 2}};

    // Round trip: the writer and the checked decoder agree field for field.
    std::string problem;
    const auto encoded = wp::serializeSkpPackage(sample, problem);
    check(!encoded.empty(), problem.c_str());
    const auto decoded = sketchy::decodeSkpPackage(bytesOf(encoded));
    check(decoded.formatVersion == sample.formatVersion, "formatVersion did not survive");
    check(decoded.container == sample.container, "container did not survive");
    check(decoded.unitScale == sample.unitScale, "unitScale did not survive");
    check(decoded.sourceName == sample.sourceName, "sourceName did not survive");
    check(decoded.sourceSha256 == sample.sourceSha256, "sourceSha256 did not survive");
    check(decoded.definitions.size() == sample.definitions.size(), "definitions lost");
    check(decoded.definitions[0].vertices.size() == 4, "root vertices lost");
    check(decoded.definitions[0].vertices[1] == std::array<double, 3>{1, 0, 0},
          "a vertex did not survive");
    check(decoded.definitions[0].faces[0].loops.size() == 2, "the hole did not survive");
    check(decoded.definitions[0].faces[0].materialFront == 0, "the material lost its index");
    check(decoded.definitions[0].instances[0].transform[12] == 0.254,
          "the instance transform did not survive");
    check(decoded.materials.size() == 2 && decoded.materials[1].name == "blue",
          "a material did not survive");
    check(decoded.tags[0].hidden, "the tag lost its hidden flag");
    check(decoded.textures[0].bytes.size() == 8, "the texture did not survive");
    check(sameCounts(decoded.counts, sample.counts), "the counts did not survive");
    auto notice = decoded.notices.find("skippedVertex");
    check(notice != decoded.notices.end() && notice->second == 1, "a notice did not survive");
    auto total = decoded.sourceTotals.find("edges");
    check(total != decoded.sourceTotals.end() && total->second == 4, "a source total did not survive");
    // ... and the digest-checked decode agrees with the fast one.
    const auto checked =
        sketchy::decodeSkpPackage(bytesOf(encoded), sketchy::SkpPackageDecodeOptions{true, true});
    check(sameCounts(checked.counts, decoded.counts), "the digest-checked decode disagrees");
    check(checked.definitions.size() == decoded.definitions.size(),
          "the digest-checked decode disagrees");

    // The hand-built packages: a valid one first, then each corruption in turn.
    const auto geometry = geometryOf();
    const auto texture = textureOf();
    const auto good = validPackage(geometry, texture);
    const auto goodChecked = sketchy::decodeSkpPackage(good);
    check(goodChecked.counts.faces == 1, "the hand-built package did not decode");

    // The wrong hash: a geometry byte the digest no longer covers. The chunk follows the
    // manifest, so locate the manifest length first.
    const auto manifestLen = std::uint32_t(good[8]) | (std::uint32_t(good[9]) << 8) |
                            (std::uint32_t(good[10]) << 16) | (std::uint32_t(good[11]) << 24);
    auto wrongHash = good;
    wrongHash[32 + manifestLen + 40] = 0xFF;
    checkDecode(wrongHash, "SHA-256 does not match", true);
    // ... and the texture's digest, the same way (the chunk follows the geometry).
    auto wrongTextureHash = good;
    wrongTextureHash[32 + manifestLen + kChunkBytes + 5] = 0x01;
    checkDecode(wrongTextureHash, "SHA-256 does not match", true);
    // A truncated file.
    checkDecode(std::vector<std::uint8_t>(good.begin(), good.begin() + 40),
               "length disagrees");
    // A bad magic.
    auto badMagic = good;
    badMagic[0] = 'X';
    checkDecode(badMagic, "bad header");
    // A reserved word that is not zero.
    checkDecode(validPackage(geometry, texture, std::nullopt, 1), "reserved");
    // Trailing bytes: the file is longer than the header announces.
    auto trailing = good;
    trailing.insert(trailing.end(), 5, 0);
    checkDecode(trailing, "length disagrees");
    // An oversize manifest: the header announces 32 MiB + 1.
    auto bigManifest = good;
    std::uint32_t big = 32 * 1024 * 1024 + 1;
    std::memcpy(bigManifest.data() + 8, &big, 4);
    checkDecode(bigManifest, "manifest exceeds");
    // A material whose texture index is one past the end of the texture table (the table has
    // one texture, so 1 is out of range): found by the decoder fuzz smoke on its first full run.
    {
        auto pastEnd = good;
        const std::string from = "\"texture\":0}", to = "\"texture\":1}";
        const auto at = std::search(pastEnd.begin(), pastEnd.end(), from.begin(), from.end());
        check(at != pastEnd.end(), "the hand-built manifest names the material's texture");
        std::copy(to.begin(), to.end(), at);
        checkDecode(pastEnd, "material texture index is out of range");
    }
    // A bad index: the edge points at a vertex the definition does not have (in the
    // geometry chunk, after the manifest).
    auto badIndex = good;
    std::uint32_t nine = 9;
    std::memcpy(badIndex.data() + 32 + manifestLen + kEdgeV2, &nine, 4);
    checkDecode(badIndex, "edge index is out of range");
    // The face's loop index, the same way.
    auto badLoopIndex = good;
    std::memcpy(badLoopIndex.data() + 32 + manifestLen + kLoopIndex + 4, &nine, 4);
    checkDecode(badLoopIndex, "face loop index is out of range");
    // A short loop: two vertices are not a loop.
    auto shortLoop = good;
    std::uint32_t shortSize = 2;
    std::memcpy(shortLoop.data() + 32 + manifestLen + kLoopSize, &shortSize, 4);
    checkDecode(shortLoop, "face loop is out of range");
    // An oversize count: the manifest's per-definition vertices exceed the 5 000 000 limit.
    const auto oversize = validPackage(
        geometry, texture,
        std::make_pair("\"guid\":\"g1\",\"vertices\":3,",
                       "\"guid\":\"g1\",\"vertices\":5000001,"));
    checkDecode(oversize, "out of range");

    std::puts("skp package tests ok");
    return 0;
}
