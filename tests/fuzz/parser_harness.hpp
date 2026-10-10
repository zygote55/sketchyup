#pragma once
#include "automation/extension.hpp"
#include "io/dxf_source.hpp"
#include "io/gltf_import.hpp"
#include "io/library_bundle.hpp"
#include "io/obj_source.hpp"
#include "io/stl_source.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>

namespace sketchy::fuzz {
inline constexpr size_t inputLimit = 1024 * 1024;
inline constexpr const char *formats[] = {
    "native-json", "native-container", "glb",       "stl-binary", "stl-ascii", "obj",
    "dxf",         "template",         "component", "extension"};
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
