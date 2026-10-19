#pragma once
#include "automation/extension.hpp"
#include "io/dxf_source.hpp"
#include "io/gltf_import.hpp"
#include "io/library_bundle.hpp"
#include "io/obj_source.hpp"
#include "io/stl_source.hpp"
#include "io/terrain_io.hpp"
#include <QtEndian>
#include "parameter_expression_harness.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace sketchy::fuzz {
inline constexpr size_t inputLimit = 1024 * 1024;
inline constexpr const char *formats[] = {
    "native-json", "native-container", "glb",       "stl-binary", "stl-ascii",           "obj",
    "dxf",         "template",         "component", "extension",  "parameter-expression",
    "terrain-mesh"};
[[noreturn]] inline void fail(const char *message) {
    std::fprintf(stderr, "Parser fuzz invariant failed: %s\n", message);
    std::abort();
}
inline void require(bool value, const char *message) {
    if (!value)
        fail(message);
}
inline void coordinate(Vec3 point) {
    require(std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z) &&
                std::abs(point.x) <= 1e6 && std::abs(point.y) <= 1e6 && std::abs(point.z) <= 1e6,
            "accepted coordinates satisfy native bounds");
}
// R095.a: an accepted terrain-mesh chunk is within every limit and re-encodes to
// exactly its input (the layout is canonical).
inline void terrainMeshInvariants(const std::vector<TerrainMeshTile> &tiles,
                                  const QByteArray &bytes) {
    require(!tiles.empty() && tiles.size() <= 10000, "terrain tile count within limits");
    QByteArray encoded("SKTMESH1", 8), payload;
    char word[8];
    qToLittleEndian<quint32>(quint32(tiles.size()), word);
    encoded.append(word, 4);
    std::size_t triangles = 0;
    Id previous = 0;
    for (const auto &tile : tiles) {
        require(tile.body > previous, "terrain tiles ascend");
        previous = tile.body;
        require(!tile.triangles.empty() && tile.triangles.size() <= 2048 &&
                    tile.vertices.size() >= 3 && tile.vertices.size() <= 3 * tile.triangles.size(),
                "terrain tile counts within limits");
        triangles += tile.triangles.size();
        qToLittleEndian<quint64>(tile.body, word);
        encoded.append(word, 8);
        qToLittleEndian<quint32>(quint32(tile.vertices.size()), word);
        encoded.append(word, 4);
        qToLittleEndian<quint32>(quint32(tile.triangles.size()), word);
        encoded.append(word, 4);
        for (const auto point : tile.vertices) {
            coordinate(point);
            for (const double value : {point.x, point.y, point.z}) {
                quint64 bits;
                std::memcpy(&bits, &value, 8);
                qToLittleEndian<quint64>(bits, word);
                payload.append(word, 8);
            }
        }
        for (const auto &triangle : tile.triangles)
            for (const auto index : triangle) {
                require(index < tile.vertices.size(), "terrain index in range");
                qToLittleEndian<quint32>(index, word);
                payload.append(word, 4);
            }
    }
    require(triangles <= 250000, "terrain triangles within the document limit");
    require(encoded + payload == bytes, "accepted terrain mesh re-encodes exactly");
}
inline void roundtrip(const Document &document) {
    const auto encoded = encodeContainer(document);
    require(encodeContainer(decodeContainer(encoded)) == encoded,
            "accepted document survives canonical container validation");
}
// The sole path-based parser uses one file in private, disk-backed scratch.
// Production glTF sidecar validation still owns relative-path confinement.
inline bool parseInput(const QString &format, const QByteArray &bytes, const QString &glbPath) {
    require(bytes.size() <= qsizetype(inputLimit), "bounded harness input");
    bool accepted = false;
    try {
        if (format == "native-json" || format == "native-container") {
            const auto document =
                format == "native-json" ? decodeDocument(bytes) : decodeContainer(bytes);
            accepted = true;
            roundtrip(document);
        } else if (format == "glb") {
            QFile file(glbPath);
            require(file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
                        file.write(bytes) == bytes.size() && file.flush(),
                    "write private GLB input");
            file.close();
            const auto result = loadGltf(glbPath);
            accepted = true;
            roundtrip(result.document);
        } else if (format == "stl-binary" || format == "stl-ascii") {
            const auto result = parseStl(bytes, {1, StlUpAxis::Z});
            accepted = true;
            for (const auto &facet : result.facets)
                for (const auto point : facet.vertices)
                    coordinate(point);
        } else if (format == "obj") {
            const auto result = parseObj(bytes, {1, ObjUpAxis::Z});
            accepted = true;
            for (const auto point : result.vertices)
                coordinate(point);
            const auto corners = [&](const auto &records) {
                for (const auto &record : records) {
                    require(record.state < result.states.size(), "OBJ state exists");
                    for (const auto corner : record.corners) {
                        require(corner.vertex < result.vertices.size(), "OBJ vertex exists");
                        require(!corner.texture || *corner.texture < result.textures.size(),
                                "OBJ texture exists");
                        require(!corner.normal || *corner.normal < result.normals.size(),
                                "OBJ normal exists");
                    }
                }
            };
            corners(result.faces);
            corners(result.lines);
        } else if (format == "dxf") {
            const auto result = parseDxf(bytes);
            accepted = true;
            for (const auto &entity : result.entities)
                for (const auto &segment : entity.segments) {
                    coordinate(segment.start);
                    coordinate(segment.end);
                    coordinate(segment.center);
                    require(std::isfinite(segment.sweep), "finite DXF sweep");
                }
        } else if (format == "template") {
            const auto result = decodeTemplateBundle(bytes);
            accepted = true;
            roundtrip(result.document);
        } else if (format == "component") {
            const auto result = decodeComponentBundle(bytes);
            accepted = true;
            roundtrip(result.document);
            require(result.document.definitions().contains(result.definition),
                    "component definition exists");
        } else if (format == "terrain-mesh") {
            const auto tiles = decodeTerrainMesh(bytes);
            accepted = true;
            terrainMeshInvariants(tiles, bytes);
        } else if (format == "parameter-expression") {
            // R093.a: one expression, checked and evaluated against the fixed table.
            bool expression = false;
            try {
                expression = parameterExpressionAccepted(
                    std::string_view(bytes.constData(), std::size_t(bytes.size())));
            } catch (const ParameterHarnessFailure &failure) {
                fail(failure.what());
            }
            if (!expression)
                return false;
            accepted = true;
        } else if (format == "extension") {
            const auto result = parseExtensionManifest(bytes);
            accepted = true;
            require(!result.actions.isEmpty() && result.source == bytes,
                    "extension retains validated source");
        } else {
            fail("unknown format");
        }
    } catch (const std::bad_alloc &) {
        fail("resource exhaustion is not ordinary parser rejection");
    } catch (const std::exception &) {
        if (accepted)
            fail("accepted parser result failed its invariant");
        return false;
    }
    return true;
}
} // namespace sketchy::fuzz
