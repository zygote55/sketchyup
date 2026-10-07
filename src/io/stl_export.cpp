#include "io/stl_export.hpp"
#include "io/new_file.hpp"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QtEndian>
#include <bit>
namespace sketchy {
namespace {
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
QString hash(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
QByteArray vector(Vec3 p) {
    return QByteArray::number(p.x == 0 ? 0 : p.x, 'g', 17) + ' ' +
           QByteArray::number(p.y == 0 ? 0 : p.y, 'g', 17) + ' ' +
           QByteArray::number(p.z == 0 ? 0 : p.z, 'g', 17);
}
Vec3 normal(const std::array<Vec3, 3> &p) {
    const auto crossProduct = cross(p[1] - p[0], p[2] - p[0]);
    const auto magnitude = length(crossProduct);
    const auto longest = std::max({length(p[1] - p[0]), length(p[2] - p[1]), length(p[0] - p[2])});
    require(std::isfinite(magnitude) &&
                magnitude >= std::max(2 * tolerance * tolerance, longest * tolerance),
            "STL export contains a collapsed or sub-tolerance world triangle");
    return {crossProduct.x / magnitude, crossProduct.y / magnitude, crossProduct.z / magnitude};
}
void binaryFloat(QByteArray &bytes, double value) {
    const float f = static_cast<float>(value);
    require(std::isfinite(f), "STL binary32 value exceeds finite range");
    char buffer[4];
    qToLittleEndian(std::bit_cast<quint32>(f), buffer);
    bytes.append(buffer, 4);
}
void binaryVector(QByteArray &bytes, Vec3 p) {
    binaryFloat(bytes, p.x);
    binaryFloat(bytes, p.y);
    binaryFloat(bytes, p.z);
}
} // namespace
StlExport exportStl(const Document &doc, StlCoordinateOptions coordinates, StlEncoding encoding) {
    coordinates.validate();
    require(encoding == StlEncoding::Binary || encoding == StlEncoding::Ascii,
            "Invalid STL encoding");
    require(doc.readSnapshotBytes() <= 256 * 1024 * 1024,
            "STL export exceeds document snapshot budget");
    StlExport result;
    const bool binary = encoding == StlEncoding::Binary;
    if (binary) {
        result.bytes = QByteArray(84, '\0');
        result.bytes.replace(0, 24, "SketchyUp triangle mesh ");
    } else
        result.bytes = "solid sketchyup\n";
    auto axis = [&](Vec3 p) { return coordinates.up == StlUpAxis::Y ? Vec3{p.x, p.z, -p.y} : p; };
    size_t count{}, bodies{}, wires{}, groups{}, images{};
    double maximumError{};
    for (const auto &[id, body] : doc.bodies()) {
        if (body->kind == BodyKind::Group) {
            ++groups;
            continue;
        }
        if (body->kind == BodyKind::ReferenceImage) {
            ++images;
            continue;
        }
        wires += body->surface.wires.size();
        if (body->surface.faces.empty())
            continue;
        ++bodies;
        const auto transform = doc.worldTransform(id);
        for (const auto &[faceId, face] : body->surface.faces) {
            (void)face;
            for (const auto &t : body->surface.triangulate(faceId)) {
                require(++count <= 100000, "STL export exceeds 100000 facets");
                std::array<Vec3, 3> points{axis(transform.point(t.a)), axis(transform.point(t.b)),
                                           axis(transform.point(t.c))};
                if (transform.determinant() < 0)
                    std::swap(points[1], points[2]);
                for (auto p : points)
                    checkPoint(p);
                const auto originalNormal = normal(points);
                auto encoded = points;
                for (auto &p : encoded) {
                    p = p * (1 / coordinates.metresPerUnit);
                    if (binary)
                        p = {double(float(p.x)), double(float(p.y)), double(float(p.z))};
                    require(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z),
                            "STL coordinates exceed representation bounds");
                }
                auto restored = encoded;
                for (size_t i = 0; i < 3; ++i) {
                    restored[i] = restored[i] * coordinates.metresPerUnit;
                    checkPoint(restored[i]);
                    const auto error = length(restored[i] - points[i]);
                    maximumError = std::max(maximumError, error);
                    require(error <= 1e-6, "STL binary precision exceeds one micrometre; choose "
                                           "ASCII or relocate geometry");
                }
                const auto n = normal(restored);
                require(dot(n, originalNormal) > 0, "STL quantization reverses a triangle");
                if (binary) {
                    binaryVector(result.bytes, n);
                    for (auto p : encoded)
                        binaryVector(result.bytes, p);
                    result.bytes.append(2, '\0');
                } else {
                    result.bytes += "facet normal " + vector(n) + "\nouter loop\n";
                    for (auto p : encoded)
                        result.bytes += "vertex " + vector(p) + "\n";
                    result.bytes += "endloop\nendfacet\n";
                }
                require(result.bytes.size() <= 64 * 1024 * 1024 - 32, "STL export exceeds 64 MiB");
            }
        }
    }
    require(count > 0, "STL export contains no surface triangles");
    if (binary)
        qToLittleEndian(quint32(count), result.bytes.data() + 80);
    else
        result.bytes += "endsolid sketchyup\n";
    result.report = {
        {"apiVersion", 1},
        {"encoding", binary ? "binary" : "ascii"},
        {"metresPerUnit", coordinates.metresPerUnit},
        {"sourceUp", coordinates.up == StlUpAxis::Y ? "Y" : "Z"},
        {"facets", qint64(count)},
        {"geometryBodies", qint64(bodies)},
        {"maximumCoordinateErrorMetres", maximumError},
        {"bytes", qint64(result.bytes.size())},
        {"sha256", hash(result.bytes)},
        {"losses", QJsonObject{{"hierarchyFlattened", qint64(groups)},
                               {"componentInstancesExpanded", qint64(doc.instances().size())},
                               {"wireSegmentsOmitted", qint64(wires)},
                               {"referenceImagesOmitted", qint64(images)},
                               {"annotationsOmitted", qint64(doc.annotations().size())}}},
        {"notices",
         QJsonArray{"STL carries triangles only; materials, textures, hierarchy and parametric "
                    "metadata are omitted.",
                    "All world geometry is exported, including hidden surfaces; sections do not "
                    "clip this export.",
                    "Units and up axis must be supplied when importing; STL does not encode them.",
                    "Export does not repair topology or certify a closed, printable solid."}}};
    return result;
}
void writeStlExport(const StlExport &result, const QString &path) {
    require(!result.bytes.isEmpty() && result.bytes.size() <= 64 * 1024 * 1024 &&
                result.report["bytes"].toInteger() == result.bytes.size() &&
                result.report["sha256"] == hash(result.bytes),
            "STL export bytes do not match the report");
    publishNewFile(path, result.bytes);
}
} // namespace sketchy
