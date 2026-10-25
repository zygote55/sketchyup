#pragma once
#include "automation/extension.hpp"
#include "io/dxf_source.hpp"
#include "io/gltf_import.hpp"
#include "io/ifc_package.hpp"
#include "io/library_bundle.hpp"
#include "io/obj_source.hpp"
#include "io/skp_package.hpp"
#include "io/stl_source.hpp"
#include "io/terrain_import.hpp"
#include "io/terrain_io.hpp"
#include "../terrain_edit_test_kinds.hpp"
#include <QJsonArray>
#include <QJsonDocument>
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
    "terrain-mesh", "terrain-dxf",     "terrain-points", "terrain-edits", "ifc-package",
    "skp-package"};
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
// R095.c: an accepted terrain source has finite coordinates within the site bound, valid
// layer indices and polylines of at least two vertices; its preparation and triangulation
// (a dry run of terrain.import) never throws for bad data, the report stays within 64 KiB with
// bounded kinds and locations, and an accepted mesh references existing vertices.
inline void terrainSourceInvariants(const TerrainSourceSet &set) {
    const double bound = TerrainSourceLimits::coordinate / set.unit.metresPerUnit;
    auto finite = [&](const TerrainVec3 &p) {
        require(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
                    std::abs(p.x) <= bound && std::abs(p.y) <= bound && std::abs(p.z) <= bound,
                "terrain source coordinates within the site bound");
    };
    for (const auto &polyline : set.polylines) {
        require(polyline.points.size() >= 2 && polyline.layer < set.layers.size(),
                "terrain polylines have two vertices and a layer");
        for (const auto &p : polyline.points)
            finite(p);
    }
    for (const auto &point : set.points) {
        require(point.layer < set.layers.size(), "terrain points have a code");
        finite(point.position);
    }
    const auto result = runTerrainImport(set, {}, std::nullopt);
    require(QJsonDocument(result.report).toJson(QJsonDocument::Compact).size() <= 64 * 1024,
            "terrain import report within 64 KiB");
    const auto terrain = result.report["terrain"].toObject();
    require(terrain["refusals"].toArray().size() + terrain["notices"].toArray().size() <= 64,
            "terrain report kinds bounded");
    for (const auto *list : {"refusals", "notices"})
        for (const auto &kind : terrain[list].toArray())
            require(kind.toObject()["locations"].toArray().size() <= 32 &&
                        kind.toObject()["message"].toString().toUtf8().size() <= 256,
                    "terrain report locations and messages bounded");
    require(result.ok == (terrain["outcome"] != "refused"), "terrain outcome matches");
    if (result.ok) {
        require(!result.mesh.triangles.empty() && result.mesh.triangles.size() <= 250000,
                "terrain mesh within the triangle limit");
        for (const auto &t : result.mesh.triangles)
            for (const auto index : t)
                require(index < result.mesh.vertices.size(), "terrain index in range");
    }
}
// R096.a: an accepted terrain edit list (with the test-only kinds, the only ones R096.a has)
// is within its bounds, has unique ids and re-encodes to a list that decodes to the same.
inline void terrainEditsInvariants(const std::vector<TerrainEditPtr> &edits) {
    require(!edits.empty() && edits.size() <= TerrainLimits::edits, "terrain edits bounded");
    require(terrainEditBytes(edits) <= TerrainLimits::editBytes + edits.size() * 256,
            "terrain edit data bounded");
    QJsonArray encoded;
    for (const auto &edit : edits)
        encoded.append(encodeTerrainEdit(*edit));
    const auto again = decodeTerrainEdits(encoded);
    require(again.size() == edits.size(), "terrain edits re-decode");
    for (std::size_t i = 0; i < edits.size(); ++i)
        require(*again[i] == *edits[i], "terrain edits round-trip");
}
// R097.b: an accepted IFC conversion package is within every bound, its references resolve, and it
// re-encodes to a package that decodes to an equal one (the manifest form is canonical).
inline void ifcPackageInvariants(const IfcPackage &package) {
    require(package.nodes.size() <= IfcPackageLimits::nodes &&
                package.meshes.size() <= IfcPackageLimits::meshes &&
                package.materials.size() <= IfcPackageLimits::materials,
            "IFC package counts within limits");
    std::size_t triangles = 0;
    for (const auto &mesh : package.meshes) {
        require(!mesh.triangles.empty() && mesh.triangles.size() <= IfcPackageLimits::meshTriangles &&
                    mesh.vertices.size() >= 3 && mesh.materials.size() == mesh.triangles.size() &&
                    (mesh.colors.empty() || mesh.colors.size() == mesh.triangles.size()),
                "IFC package mesh counts within limits");
        triangles += mesh.triangles.size();
        for (const auto point : mesh.vertices)
            coordinate(point);
        for (const auto &t : mesh.triangles)
            for (const auto index : t)
                require(index < mesh.vertices.size(), "IFC package triangle index in range");
        for (const auto material : mesh.materials)
            require(material == IfcPackageMesh::noMaterial || material < package.materials.size(),
                    "IFC package material index in range");
        for (const auto &edge : mesh.featureEdges)
            require(edge[0] < mesh.vertices.size() && edge[1] < mesh.vertices.size(),
                    "IFC package edge index in range");
    }
    require(triangles <= IfcPackageLimits::totalTriangles, "IFC package triangles within limit");
    for (std::size_t i = 0; i < package.nodes.size(); ++i) {
        const auto &node = package.nodes[i];
        require(node.parent >= -1 && node.parent < std::int64_t(i), "IFC package parent precedes its child");
        require(node.mesh >= -1 && node.mesh < std::int64_t(package.meshes.size()),
                "IFC package mesh index in range");
        require(node.properties.size() <= IfcPackageLimits::nodeProperties &&
                    node.provenance.size() <= IfcPackageLimits::nodeProvenance,
                "IFC package property counts within limits");
    }
    const auto encoded = encodeIfcPackage(package);
    require(decodeIfcPackage(encoded) == package, "accepted IFC package re-encodes to an equal package");
}
// R102.a (ADR 0176 section 1.6): the invariants the .skp decoder guarantees the conversion
// (R102.b) can rely on: every count within its limit, every index in range, every coordinate
// finite and bounded, every loop a valid polygon, and the package's totals agreeing.
inline void skpPackageInvariants(const SkpPackage &package) {
    require(!package.definitions.empty(), "SKP package has a root definition");
    require(package.definitions.size() <= SkpPackageLimits::definitions &&
                package.materials.size() <= SkpPackageLimits::materials &&
                package.tags.size() <= SkpPackageLimits::tags &&
                package.textures.size() <= SkpPackageLimits::textures,
            "SKP package counts within limits");
    long long v = 0, e = 0, f = 0, inst = 0, holes = 0, uv = 0;
    for (const auto &definition : package.definitions) {
        v += std::size_t(definition.vertices.size());
        e += std::size_t(definition.edges.size());
        f += std::size_t(definition.faces.size());
        inst += std::size_t(definition.instances.size());
        for (const auto &point : definition.vertices)
            for (const double x : point)
                require(std::isfinite(x) && std::abs(x) <= 1e6, "SKP vertex within 1e6 m");
        for (const auto &edge : definition.edges)
            require(edge.v1 < definition.vertices.size() && edge.v2 < definition.vertices.size(),
                    "SKP edge index in range");
        for (const auto &face : definition.faces) {
            require(!face.loops.empty() && face.loops.size() <= SkpPackageLimits::loopsPerFace,
                    "SKP face loops bounded");
            for (std::size_t l = 0; l < face.loops.size(); ++l) {
                const auto &loop = face.loops[l];
                require(loop.size() >= 3 && loop.size() <= SkpPackageLimits::loopVertices,
                        "SKP face loop a valid polygon");
                if (l > 0)
                    holes++;
                for (const auto index : loop)
                    require(index < definition.vertices.size(), "SKP face loop index in range");
            }
            require(face.materialFront == -1 || face.materialFront < package.materials.size(),
                    "SKP face material in range");
            require(face.materialBack == -1 || face.materialBack < package.materials.size(),
                    "SKP face material in range");
            require(face.tag == -1 || face.tag < package.tags.size(), "SKP face tag in range");
            if (face.hasUvFront)
                uv++;
            if (face.hasUvBack)
                uv++;
        }
        for (const auto &instance : definition.instances)
            require(instance.definition == -1 || instance.definition < package.definitions.size(),
                    "SKP instance reference in range");
    }
    require(v <= SkpPackageLimits::vertices && e <= SkpPackageLimits::edges &&
                f <= SkpPackageLimits::faces && inst <= SkpPackageLimits::instances,
            "SKP package totals within limits");
    require(package.counts.definitions == std::size_t(package.definitions.size()) &&
                package.counts.vertices == v && package.counts.edges == e &&
                package.counts.faces == f && package.counts.instances == inst &&
                package.counts.holes == holes && package.counts.uvTransforms == uv,
            "SKP package totals agree with its chunks");
    for (const auto &material : package.materials)
        require(material.texture == -1 || material.texture < package.textures.size(),
                "SKP material texture in range");
}
inline void registerTerrainTestKinds() {
    static const bool registered = [] {
        terrain_test_kinds::registerKinds();
        return true;
    }();
    (void)registered;
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
        } else if (format == "terrain-dxf") {
            const auto result = parseDxfTerrain(bytes, {terrainUnit("m"), 0.05});
            accepted = true;
            terrainSourceInvariants(result);
        } else if (format == "terrain-points") {
            TerrainPointOptions options;
            options.unit = *terrainUnit("m");
            const auto result = parseTerrainPoints(bytes, options);
            accepted = true;
            terrainSourceInvariants(result);
        } else if (format == "terrain-edits") {
            registerTerrainTestKinds();
            const auto json = QJsonDocument::fromJson(bytes);
            if (!json.isArray())
                return false;
            const auto edits = decodeTerrainEdits(json.array());
            accepted = true;
            terrainEditsInvariants(edits);
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
        } else if (format == "ifc-package") {
            // The digest check is off so mutated mesh chunks reach the structural checks.
            const auto package = decodeIfcPackage(bytes, {false});
            accepted = true;
            ifcPackageInvariants(package);
            bool digestMatches = true;
            IfcPackage checked;
            try {
                checked = decodeIfcPackage(bytes, {true});
            } catch (const std::exception &) {
                digestMatches = false;
            }
            require(!digestMatches || checked == package, "digest-checked decode agrees");
        } else if (format == "skp-package") {
            // The digest check is off so mutated chunks reach the structural checks.
            const auto package = decodeSkpPackage(std::vector<std::uint8_t>(
                bytes.constData(), bytes.constData() + bytes.size()),
                SkpPackageDecodeOptions{false, true});
            accepted = true;
            skpPackageInvariants(package);
            bool digestMatches = true;
            SkpPackage checked;
            try {
                checked = decodeSkpPackage(std::vector<std::uint8_t>(
                    bytes.constData(), bytes.constData() + bytes.size()),
                    SkpPackageDecodeOptions{true, true});
            } catch (const std::exception &) {
                digestMatches = false;
            }
            require(!digestMatches || sameSkpPackageSummary(package, checked),
                    "digest-checked decode agrees");
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
