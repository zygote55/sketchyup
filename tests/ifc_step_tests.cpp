// R098.a: the STEP probe and import, driven through the worker against its own writer (ADR
// 0172). The corpus is the three self-generated files of ADR 0172 section 7 (a box 10 x 20
// x 30, a cylinder of radius 5 and height 20, an assembly of the two, all in millimetres);
// the unit policy is checked against raw text, the round trip through the writer and reader
// is checked against the hand-computed geometry, and the refusals against the policy. Needs
// sketchyup-ifc-worker (IFC_WORKER_PATH); exits 77 (skipped) when the helper is not built.
#include <QCoreApplication>
#include <QFileInfo>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QtEndian>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <optional>
#include <iostream>
#include <vector>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string &message) {
    if (!ok) {
        ++failures;
        std::cerr << "FAIL: " << message << "\n";
    }
}
bool near(double a, double b, double tolerance = 1e-6) {
    return std::abs(a - b) <= tolerance * std::max(1.0, std::abs(b));
}
struct WorkerResult {
    int code = -1;
    QJsonObject json;
    QString error;
};
WorkerResult runWorker(const QString &worker, const QStringList &args) {
    QProcess process;
    // QProcess::start prepends the program as argv[0]; the list carries it again, so drop the copy.
    const auto arguments = args.first() == worker ? args.mid(1) : args;
    process.start(worker, arguments);
    if (!process.waitForFinished(300000)) {
        process.kill();
        return {124, {}, "timeout"};
    }
    WorkerResult result;
    result.code = process.exitCode();
    result.error = process.readAllStandardError().trimmed();
    // The OCCT transfer statistics share stdout. The report is the worker's last result
    // line: "R {json}" (the lifecycle runs) or a bare {json} (the metadata report).
    const auto lines = process.readAllStandardOutput().split('\n');
    for (int i = static_cast<int>(lines.size()) - 1; i >= 0; --i) {
        const auto trimmed = lines.at(i).trimmed();
        if (trimmed.startsWith("R {")) {
            result.json = QJsonDocument::fromJson(trimmed.mid(2)).object();
            break;
        }
        if (trimmed.startsWith("{"))
            result.json = QJsonDocument::fromJson(trimmed).object();
    }
    return result;
}
void boxGeometryCheck(const QJsonObject &shape, const char *where) {
    const auto box = shape.value("boundingBox").toArray();
    const auto boxMm = shape.value("boundingBoxMm").toArray();
    const auto &source = box.isEmpty() ? boxMm : box;
    check(source.size() == 6, std::string(where) + ": the shape carries a six-number bounding box");
    if (source.size() != 6)
        return;
    const double x0 = source[0].toDouble(), y0 = source[1].toDouble(), z0 = source[2].toDouble();
    const double x1 = source[3].toDouble(), y1 = source[4].toDouble(), z1 = source[5].toDouble();
    check(near(x0, 0.0) && near(y0, 0.0) && near(z0, 0.0), std::string(where) + ": the origin");
    check(near(x1, 10.0) && near(y1, 20.0) && near(z1, 30.0), std::string(where) + ": the corner (10, 20, 30)");
}
// The worker's package, checked the way the application's decoder checks it (the same
// invariants as src/io/ifc_package.cpp): the magic and the exact length, the reserved word,
// the manifest's keys, the per-mesh layout (f64 x3 per vertex, u32 x3 per triangle, u32 per
// triangle, u32 x2 per edge, f32 x3 per colour when present), the coordinate bound, the
// indices, the material indices and the node classes.
void decodePackageCheck(const QString &path, bool &ok, std::string &problem,
                       int &nodesCount, int &rootClassIsAssembly) {
    ok = false;
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly)) {
        problem = "could not open the package";
        return;
    }
    const QByteArray bytes = in.readAll();
    if (bytes.size() < 24 || bytes.left(8) != "SKIFCP01") {
        problem = "bad magic or length";
        return;
    }
    const uint32_t manifestBytes = qFromLittleEndian<uint32_t>(bytes.constData() + 8);
    const uint32_t reserved = qFromLittleEndian<uint32_t>(bytes.constData() + 12);
    const uint64_t meshBytes = qFromLittleEndian<uint64_t>(bytes.constData() + 16);
    if (reserved != 0) {
        problem = "the reserved word";
        return;
    }
    if (manifestBytes > 32ull * 1024 * 1024 || meshBytes > 128ull * 1024 * 1024) {
        problem = "a section over its bound";
        return;
    }
    if (bytes.size() != static_cast<int>(24 + static_cast<int>(manifestBytes) + meshBytes)) {
        problem = "the file is not exactly 24 + manifest + mesh bytes long";
        return;
    }
    const auto manifest = QJsonDocument::fromJson(bytes.mid(24, manifestBytes)).object();
    const QStringList keys{"format", "version", "meshSha256", "engine", "source", "units", "report",
                           "meshes", "materials", "nodes"};
    for (const auto &key : manifest.keys())
        if (!keys.contains(key)) {
            problem = "a manifest key the decoder does not know: " + key.toStdString();
            return;
        }
    for (const auto &key : keys)
        if (!manifest.contains(key)) {
            problem = "the manifest lacks the key " + key.toStdString();
            return;
        }
    if (manifest.value("format").toString() != "sketchyup-ifc-package" || manifest.value("version").toInt() != 1) {
        problem = "the format or version is wrong";
        return;
    }
    const auto meshes = manifest.value("meshes").toArray();
    if (meshes.isEmpty()) {
        problem = "no meshes";
        return;
    }
    int64_t total = 0;
    int meshOffset = 0;
    for (const auto &value : meshes) {
        const auto d = value.toObject();
        const int64_t n = d.value("vertices").toInt(), t = d.value("triangles").toInt();
        const int64_t e = d.value("edges").toInt();
        const bool colors = d.value("colors").toBool();
        if (n < 0 || t < 0 || e < 0 || n > 300000 || t > 100000) {
            problem = "a mesh over the package's bounds";
            return;
        }
        total += n * 24 + t * 12 + t * 4 + e * 8 + (colors ? t * 12 : 0);
        const uint8_t *data = reinterpret_cast<const uint8_t *>(bytes.constData()) + 24 + manifestBytes + meshOffset;
        for (int64_t i = 0; i < n * 3; ++i) {
            const uint64_t bits = qFromLittleEndian<uint64_t>(data + i * 8);
            double v;
            std::memcpy(&v, &bits, sizeof v);
            if (!std::isfinite(v) || std::abs(v) > 1e6) {
                problem = "a vertex outside the coordinate bound";
                return;
            }
        }
        meshOffset += n * 24;
        for (int64_t i = 0; i < t * 3; ++i) {
            const uint32_t index = qFromLittleEndian<uint32_t>(data + n * 24 + i * 4);
            if (index >= static_cast<uint32_t>(n)) {
                problem = "a triangle index out of range";
                return;
            }
        }
        meshOffset += t * 12;
        for (int64_t i = 0; i < t; ++i) {
            const uint32_t material = qFromLittleEndian<uint32_t>(data + n * 24 + t * 12 + i * 4);
            if (material != 0xFFFFFFFFu) {
                problem = "a material index where the step import ships none (R098.b)";
                return;
            }
        }
        meshOffset += t * 4 + e * 8 + (colors ? t * 12 : 0);
    }
    if (total != static_cast<int64_t>(meshBytes)) {
        problem = "the mesh chunk length disagrees with its meshes";
        return;
    }
    const auto nodes = manifest.value("nodes").toArray();
    nodesCount = static_cast<int>(nodes.size());
    for (int i = 0; i < static_cast<int>(nodes.size()); ++i) {
        const auto node = nodes.at(i).toObject();
        const auto className = node.value("class").toString().toStdString();
        if (className.size() < 4 || className.rfind("Ifc", 0) != 0) {
            problem = "a node class without the Ifc prefix";
            return;
        }
        for (const char c : className)
            if (!std::isalnum(static_cast<unsigned char>(c))) {
                problem = "a node class with a non-alphanumeric character";
                return;
            }
        const auto transform = node.value("transform").toArray();
        if (transform.size() != 16) {
            problem = "a node transform that is not 16 numbers";
            return;
        }
        for (int k = 0; k < 16; ++k) {
            const double v = transform.at(k).toDouble();
            if (!std::isfinite(v)) {
                problem = "a non-finite transform entry";
                return;
            }
        }
        if (std::abs(transform.at(12).toDouble()) > 1e-12 || std::abs(transform.at(13).toDouble()) > 1e-12
            || std::abs(transform.at(14).toDouble()) > 1e-12 || std::abs(transform.at(15).toDouble() - 1.0) > 1e-12) {
            problem = "a non-affine node transform";
            return;
        }
        const int parent = node.value("parent").toInt();
        if (parent < -1 || parent >= static_cast<int>(i)) {
            problem = "a node parent that is not an earlier node";
            return;
        }
    }
    if (nodes.isEmpty()) {
        problem = "no nodes";
        return;
    }
    rootClassIsAssembly = nodes.first().toObject().value("class").toString() == "IfcStepAssembly";
    ok = true;
}

// The package's combined mesh volume, area, vertex bounds, and the count of the nodes whose
// transform is not the identity, from the manifest's counts and the chunk's vertices and
// triangle indices. The bounds are the mesh's coordinates as written: the node transforms
// are not applied, so the placement a file carries rides on the transforms while the bounds
// are the geometry the reader delivered.
struct PackageGeometry {
    double volume = 0.0;
    double area = 0.0;
    double min[3] = {0.0, 0.0, 0.0};
    double max[3] = {0.0, 0.0, 0.0};
    int nonIdentityTransforms = 0;
    int placed = 0;  // the report's non-identity placement count
    double firstNonIdentity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};
std::optional<PackageGeometry> packageGeometry(const QString &path) {
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QByteArray bytes = in.readAll();
    if (bytes.size() < 24)
        return std::nullopt;
    const uint32_t manifestBytes = qFromLittleEndian<uint32_t>(bytes.constData() + 8);
    const auto manifest = QJsonDocument::fromJson(bytes.mid(24, manifestBytes)).object();
    PackageGeometry out;
    bool any = false;
    int64_t offset = 0;
    for (const auto &value : manifest.value("meshes").toArray()) {
        const auto d = value.toObject();
        const int64_t n = d.value("vertices").toInt(), t = d.value("triangles").toInt();
        const uint8_t *data = reinterpret_cast<const uint8_t *>(bytes.constData()) + 24 + manifestBytes + offset;
        std::vector<std::array<double, 3>> vertices;
        vertices.reserve(n);
        for (int i = 0; i < n; ++i) {
            std::array<double, 3> coordinate{};
            for (int k = 0; k < 3; ++k) {
                const uint64_t bits = qFromLittleEndian<uint64_t>(data + (int64_t(i) * 3 + k) * 8);
                std::memcpy(&coordinate[k], &bits, sizeof coordinate[k]);
            }
            vertices.push_back(coordinate);
        }
        for (int i = 0; i < t; ++i) {
            const int a = qFromLittleEndian<uint32_t>(data + n * 24 + i * 12);
            const int b = qFromLittleEndian<uint32_t>(data + n * 24 + i * 12 + 4);
            const int c = qFromLittleEndian<uint32_t>(data + n * 24 + i * 12 + 8);
            const auto &p = vertices[static_cast<size_t>(a)], &q = vertices[static_cast<size_t>(b)], &r = vertices[static_cast<size_t>(c)];
            out.volume += (p[0] * (q[1] * r[2] - r[1] * q[2]) + p[1] * (r[0] * q[2] - q[0] * r[2])
                          + p[2] * (q[0] * r[1] - r[0] * q[1])) / 6.0;
            const double e1x = q[0] - p[0], e1y = q[1] - p[1], e1z = q[2] - p[2];
            const double e2x = r[0] - p[0], e2y = r[1] - p[1], e2z = r[2] - p[2];
            const double cx = e1y * e2z - e1z * e2y, cy = e1z * e2x - e1x * e2z, cz = e1x * e2y - e1y * e2x;
            out.area += 0.5 * std::sqrt(cx * cx + cy * cy + cz * cz);
            any = true;
        }
        if (n > 0) {
            for (int axis = 0; axis < 3; ++axis) {
                double lo = vertices[0][static_cast<size_t>(axis)], hi = lo;
                for (const auto &v : vertices) {
                    lo = std::min(lo, v[static_cast<size_t>(axis)]);
                    hi = std::max(hi, v[static_cast<size_t>(axis)]);
                }
                out.min[axis] = lo;
                out.max[axis] = hi;
            }
        }
        offset += n * 24 + t * 12 + t * 4 + d.value("edges").toInt() * 8 + (d.value("colors").toBool() ? t * 12 : 0);
    }
    for (const auto &value : manifest.value("nodes").toArray()) {
        const auto transform = value.toObject().value("transform").toArray();
        if (transform.size() != 16)
            continue;
        const double i16[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        bool identity = true;
        for (int k = 0; k < 16 && identity; ++k)
            identity = std::abs(transform.at(k).toDouble() - i16[k]) <= 1e-12;
        if (!identity) {
            ++out.nonIdentityTransforms;
            if (out.nonIdentityTransforms == 1)
                for (int k = 0; k < 16; ++k)
                    out.firstNonIdentity[k] = transform.at(k).toDouble();
        }
    }
    out.placed = manifest.value("report").toObject().value("counts").toObject().value("placed").toInt();
    if (!any)
        return std::nullopt;
    return out;
}

// The original two-number form the existing checks use (the bounds and the transform count
// are the R098.b additions; the volume and the area are the ADR 0172 section 7 values).
std::optional<std::pair<double, double>> packageVolumeAndArea(const QString &path) {
    auto geometry = packageGeometry(path);
    if (!geometry)
        return std::nullopt;
    return std::pair{geometry->volume, geometry->area};
}

// A relative comparison (the analytic values are small in metres; the default near() is
// absolute for them).
bool nearRel(double a, double b, double relative) {
    return std::abs(a - b) <= relative * std::abs(b);
}
}  // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
#ifdef IFC_WORKER_PATH
    const QString worker = QStringLiteral(IFC_WORKER_PATH);
#else
    return 77;
#endif
    if (!QFile::exists(worker))
        return 77;

    QTemporaryDir temp;
    check(temp.isValid(), "a temporary directory for the corpus");
    const auto run = [&](const QStringList &args) { return runWorker(worker, args); };

    // 1. The self-generated corpus (ADR 0172 section 7): three files, all AP214.
    {
        const auto result = run({worker, "--write-step", temp.path()});
        check(result.code == 0, "write-step exits 0");
        const auto files = result.json.value("files").toArray();
        check(files.size() == 6, "write-step writes six files (the box, the cylinder, the assembly, the placed box, the mirrored box, the instanced assembly)");
        for (const auto &value : files) {
            const auto file = value.toObject();
            check(file.value("written").toBool(), std::string("write-step: ") + file.value("file").toString().toStdString());
            check(file.value("bytes").toInt() > 0, std::string("write-step: bytes > 0 for ") + file.value("file").toString().toStdString());
            check(file.value("schema").toString() == "ap214", std::string("write-step: AP214 for ") + file.value("file").toString().toStdString());
        }
        // Determinism: the corpus is a regression guard on the writer, so the second write
        // must be byte identical.
        QJsonObject first = result.json;
        const auto again = run({worker, "--write-step", temp.path()});
        check(again.code == 0, "write-step (second) exits 0");
        const auto files2 = again.json.value("files").toArray();
        for (int i = 0; i < files.size(); ++i) {
            const auto a = files[i].toObject(), b = files2[i].toObject();
            check(a.value("bytes").toInt() == b.value("bytes").toInt(), std::string("write-step: deterministic bytes for ") + a.value("file").toString().toStdString());
        }
    }

    // 1b. The committed fixtures (tests/fixtures/step/): the corpus pinned in the repository.
    // The FILE_NAME carries a wall-clock timestamp, so the fixture is a regression input, not a
    // byte comparison of the fresh write: the probe and the import must see the same geometry.
    {
        const auto fixture = [&](const char *name) {
            return run({worker, "--probe-step", QStringLiteral(SOURCE_DIR "/tests/fixtures/step/") + name}).json;
        };
        const auto boxFixture = fixture("sketchyup-step-box.step");
        const auto boxShapes = boxFixture.value("shapes").toArray();
        check(boxShapes.size() == 1 && boxShapes[0].toObject().value("solids").toInt() == 1
              && boxShapes[0].toObject().value("faces").toInt() == 6, "fixture box: one solid, six faces");
        if (boxShapes.size() == 1)
            boxGeometryCheck(boxShapes[0].toObject(), "fixture box");
        const auto boxImport = run({worker, "--import-step", QStringLiteral(SOURCE_DIR "/tests/fixtures/step/sketchyup-step-box.step")});
        check(boxImport.code == 0 && boxImport.json.value("status").toString() == "ok", "fixture import: the box reads with the declared unit");
        const auto cylinderFixture = fixture("sketchyup-step-cylinder.step");
        const auto cylShapes = cylinderFixture.value("shapes").toArray();
        check(cylShapes.size() == 1 && cylShapes[0].toObject().value("faces").toInt() == 3, "fixture cylinder: one solid, three faces");
        const auto assemblyFixture = fixture("sketchyup-step-assembly.step");
        const auto asmShapes = assemblyFixture.value("shapes").toArray();
        check(asmShapes.size() == 1 && asmShapes[0].toObject().value("solids").toInt() == 2, "fixture assembly: one shape, two solids");
        // The placed and the mirrored fixtures (the R098.b corpus additions): the writer bakes
        // the placement into the coordinates (a det -1 placement is not expressible in the
        // step subset - the mirror is the face's orientation, ADR 0172 section 7), so both
        // read as one solid with the box's six faces at the placed or mirrored coordinates.
        const auto placedFixture = fixture("sketchyup-step-placed.step");
        const auto placedShapes = placedFixture.value("shapes").toArray();
        check(placedShapes.size() == 1 && placedShapes[0].toObject().value("solids").toInt() == 1
              && placedShapes[0].toObject().value("faces").toInt() == 6, "fixture placed: one solid, six faces");
        if (placedShapes.size() == 1) {
            const auto box = placedShapes[0].toObject().value("boundingBox").toArray();
            check(box.size() == 6, "fixture placed: a six-number bounding box in mm");
            if (box.size() == 6) {
                // The (5, 10, 15) mm translation, in millimetres.
                check(near(box[0].toDouble(), 5.0) && near(box[1].toDouble(), 10.0) && near(box[2].toDouble(), 15.0),
                      "fixture placed: the origin is the translation");
                check(near(box[3].toDouble(), 15.0) && near(box[4].toDouble(), 30.0) && near(box[5].toDouble(), 45.0),
                      "fixture placed: the box is 10 x 20 x 30 mm at the translation");
            }
        }
        const auto mirrorFixture = fixture("sketchyup-step-mirror.step");
        const auto mirrorShapes = mirrorFixture.value("shapes").toArray();
        check(mirrorShapes.size() == 1 && mirrorShapes[0].toObject().value("solids").toInt() == 1
              && mirrorShapes[0].toObject().value("faces").toInt() == 6, "fixture mirror: one solid, six faces");
        if (mirrorShapes.size() == 1) {
            const auto box = mirrorShapes[0].toObject().value("boundingBox").toArray();
            check(box.size() == 6, "fixture mirror: a six-number bounding box in mm");
            if (box.size() == 6) {
                // The writer's mirror is the point symmetry about the OX line at the origin
                // (gp_Ax1's mirror): y and z flip, x is kept - the box at [0, 10] x [-20, 0] x [-30, 0].
                check(near(box[0].toDouble(), 0.0) && near(box[1].toDouble(), -20.0) && near(box[2].toDouble(), -30.0),
                  "fixture mirror: the mirrored origin");
                check(near(box[3].toDouble(), 10.0) && near(box[4].toDouble(), 0.0) && near(box[5].toDouble(), 0.0),
                  "fixture mirror: the box is 10 x 20 x 30 mm mirrored about the OX line");
            }
        }
        const auto placedImport = run({worker, "--import-step", QStringLiteral(SOURCE_DIR "/tests/fixtures/step/sketchyup-step-placed.step")});
        check(placedImport.code == 0 && placedImport.json.value("status").toString() == "ok",
              "fixture placed import: the placed box reads with the declared unit");
        const auto mirrorImport = run({worker, "--import-step", QStringLiteral(SOURCE_DIR "/tests/fixtures/step/sketchyup-step-mirror.step")});
        check(mirrorImport.code == 0 && mirrorImport.json.value("status").toString() == "ok",
              "fixture mirror import: the mirrored box reads with the declared unit");
        // The hand-crafted placed-assembly fixture (ADR 0172 section 4: the file's local
        // transformation as the instance placement): the assembly fixture with the box's
        // placement offset to (50, 0, 0) mm. The 7.9.3 reader preserves the component's
        // AXIS2_PLACEMENT_3D as the label's location, so the probe's bounding box is the
        // offset box's, and the import's package node carries the placement.
        const auto placedAsmFixture = fixture("sketchyup-step-placed-assembly.step");
        const auto placedAsmShapes = placedAsmFixture.value("shapes").toArray();
        check(placedAsmShapes.size() == 1 && placedAsmShapes[0].toObject().value("solids").toInt() == 2
              && placedAsmShapes[0].toObject().value("faces").toInt() == 9,
              "fixture placed-assembly: one shape, two solids, nine faces");
        if (placedAsmShapes.size() == 1) {
            const auto box = placedAsmShapes[0].toObject().value("boundingBox").toArray();
            check(box.size() == 6, "fixture placed-assembly: a six-number bounding box");
            if (box.size() == 6) {
                // The box at (50, 0, 0) mm, the cylinder at the origin.
                check(near(box[0].toDouble(), -5.0) && near(box[3].toDouble(), 60.0)
                      && near(box[1].toDouble(), -5.0) && near(box[4].toDouble(), 20.0)
                      && near(box[2].toDouble(), 0.0) && near(box[5].toDouble(), 30.0),
                      "fixture placed-assembly: the bounding box is the offset box's, got [" + std::to_string(box[0].toDouble()) + ", " + std::to_string(box[3].toDouble()) + "]");
            }
        }
        const auto placedAsmImport = run({worker, "--import-step", QStringLiteral(SOURCE_DIR "/tests/fixtures/step/sketchyup-step-placed-assembly.step")});
        check(placedAsmImport.code == 0 && placedAsmImport.json.value("status").toString() == "ok",
              "fixture placed-assembly import: the offset component placement reads");
        // R098.b fixture (a): the two-level assembly with one part instanced three times (the
        // writer's NEXT_ASSEMBLY_USAGE_OCCURRENCE structure, the ADR 0172 section 4 corpus):
        // the probe sees the assembly's three placed instances (the basic reader normalises the
        // placements into the coordinates), and the import reads the XDE structure (the part
        // shared by the three instances, each placement a transform, the shared-mesh design).
        const auto instFixture = fixture("sketchyup-step-instanced.step");
        const auto instShapes = instFixture.value("shapes").toArray();
        check(instShapes.size() == 1 && instShapes[0].toObject().value("solids").toInt() == 3
              && instShapes[0].toObject().value("faces").toInt() == 18,
              "fixture instanced: one shape, three solids, eighteen faces");
        if (instShapes.size() == 1) {
            const auto box = instShapes[0].toObject().value("boundingBox").toArray();
            check(box.size() == 6, "fixture instanced: a six-number bounding box");
            if (box.size() == 6) {
                // The three instances of the 10 x 20 x 30 box at (50, 0, 0), (0, 50, 0) and
                // (0, 0, 50) mm. The writer keeps the placements in one shared shape
                // representation, and the basic reader applies a context-dependent shape's
                // placements as a chain, so the transferred shape's bounding box reaches
                // (60, 70, 80) (the import's XDE read keeps the placements as node
                // transforms, the package test below).
                check(near(box[0].toDouble(), 0.0) && near(box[3].toDouble(), 60.0)
                      && near(box[1].toDouble(), 0.0) && near(box[4].toDouble(), 70.0)
                      && near(box[2].toDouble(), 0.0) && near(box[5].toDouble(), 80.0),
                      "fixture instanced: the bounding box carries the placements, got [" + std::to_string(box[3].toDouble()) + ", " + std::to_string(box[4].toDouble()) + ", " + std::to_string(box[5].toDouble()) + "]");
            }
        }
        const auto instImport = run({worker, "--import-step", QStringLiteral(SOURCE_DIR "/tests/fixtures/step/sketchyup-step-instanced.step")});
        check(instImport.code == 0 && instImport.json.value("status").toString() == "ok",
              "fixture instanced import: the three instances read");
        // The hand-written inch fixture (tests/fixtures/step/inch-units.step): the writer's box
        // with the context's unit a CONVERSION_BASED_UNIT ('INCH'). The raw text's first
        // declaration is the conversion's millimetre base (the unit entity the writer wrote
        // stands before the conversion unit); the reader resolves the unit the context applies
        // and normalizes the geometry to millimetres at read time.
        const auto inchFixture = fixture("inch-units.step");
        const auto inchShapes = inchFixture.value("shapes").toArray();
        check(inchShapes.size() == 1 && inchShapes[0].toObject().value("solids").toInt() == 1
              && inchShapes[0].toObject().value("faces").toInt() == 6, "fixture inch: one solid, six faces");
        if (inchShapes.size() == 1) {
            const auto box = inchShapes[0].toObject().value("boundingBox").toArray();
            check(box.size() == 6, "fixture inch: a six-number bounding box");
            if (box.size() == 6) {
                // The reader's bounding box carries the file's 1e-7 uncertainty and float noise
                // (the measured box is -1e-7 to 254.0000001), so the assertion uses the 1e-6 default.
                check(near(box[0].toDouble(), 0.0) && near(box[1].toDouble(), 0.0)
                      && near(box[2].toDouble(), 0.0), "fixture inch: the origin");
                check(near(box[3].toDouble(), 254.0) && near(box[4].toDouble(), 508.0)
                      && near(box[5].toDouble(), 762.0), "fixture inch: the reader delivered the geometry in millimetres");
            }
        }
        check(inchFixture.value("modelLengthUnit").toString() == "INCH", "fixture inch: the model resolves the file's unit as the inch");
        check(near(inchFixture.value("modelLengthMetres").toDouble(), 0.0254, 1e-9), "fixture inch: the inch is 0.0254 m");
        const auto inchImport = run({worker, "--import-step", QStringLiteral(SOURCE_DIR "/tests/fixtures/step/inch-units.step")});
        check(inchImport.code == 0 && inchImport.json.value("status").toString() == "ok", "fixture inch import: the box reads with the conversion unit");
        check(inchImport.json.value("modelLengthUnit").toString() == "INCH", "fixture inch: the report carries the model's unit");
        {
            const auto u = inchImport.json.value("units").toObject();
            check(u.value("applied").toString() == "declared", "fixture inch: the declared unit is applied");
            check(near(u.value("mmPerUnit").toDouble(), 1.0, 1e-9), "fixture inch: the reader's conversion is applied (1.0)");
            check(near(u.value("rawLengthMetres").toDouble(), 0.001, 1e-9), "fixture inch: the raw text's first declaration is the millimetre base");
            check(u.value("length").toString() == "millimetre", "fixture inch: the raw text reports the base it declares");
        }
        {
            const auto shapes = inchImport.json.value("shapes").toArray();
            check(shapes.size() == 1, "fixture inch: one shape");
            if (shapes.size() == 1) {
                const auto boxMm = shapes[0].toObject().value("boundingBoxMm").toArray();
                check(boxMm.size() == 6, "fixture inch: a six-number bounding box in mm");
                if (boxMm.size() == 6) {
                    check(near(boxMm[0].toDouble(), 0.0) && near(boxMm[1].toDouble(), 0.0)
                          && near(boxMm[2].toDouble(), 0.0), "fixture inch: the origin in mm");
                    check(near(boxMm[3].toDouble(), 254.0) && near(boxMm[4].toDouble(), 508.0)
                          && near(boxMm[5].toDouble(), 762.0), "fixture inch: the box is 254 x 508 x 762 mm");
                }
            }
        }
    }

    // 2. The box, probed with the round trip (ADR 0172 section 2).
    const QString box = temp.filePath("sketchyup-step-box.step");
    const QJsonObject probe = run({worker, "--probe-step", box, "--round-trip-directory", temp.filePath("rt")}).json;
    check(probe.value("code").toInt() == 0, "probe of the box exits 0");
    {
        const auto file = probe.value("file").toObject();
        check(file.value("isoHeader").toBool(), "probe: the ISO header is there");
        check(file.value("schema").toString() == "ap214", "probe: the schema is AP214");
        const auto units = probe.value("units").toObject();
        check(units.value("declared").toBool(), "probe: the units are declared in the raw text");
        check(units.value("length").toString() == "millimetre", "probe: the declared length unit is the millimetre");
        check(near(units.value("lengthMetres").toDouble(), 0.001, 1e-9), "probe: the millimetre is 0.001 m");
        check(units.value("angle").toString() == "RADIAN", "probe: the plane angle is the radian");
        check(!probe.value("occt").toObject().value("version").toString().isEmpty(), "probe: the OCCT version is reported");
        check(probe.value("entities").toInt() > 0, "probe: the entity count is positive");
        check(probe.value("transferred").toInt() >= 1, "probe: a shape was transferred");
        const auto shapes = probe.value("shapes").toArray();
        check(shapes.size() == 1, "probe: one shape");
        if (shapes.size() == 1) {
            const auto shape = shapes[0].toObject();
            check(shape.value("solids").toInt() == 1, "probe: the box is one solid");
            check(shape.value("faces").toInt() == 6, "probe: the box has six faces");
            boxGeometryCheck(shape, "probe");
        }
        const auto roundTrip = probe.value("writeRoundTrip").toObject();
        const auto ap214 = roundTrip.value("ap214").toObject();
        check(ap214.value("written").toBool(), "round trip: the AP214 file was written");
        check(ap214.value("schema").toString() == "ap214", "round trip: the AP214 file reads back AP214");
        check(ap214.value("readStatus").toString() == "done", "round trip: the AP214 file reads");
        {
            QJsonObject probeShape;
            probeShape["boundingBox"] = ap214.value("boundingBox");
            boxGeometryCheck(probeShape, "round trip AP214");
        }
        const auto ap242 = roundTrip.value("ap242").toObject();
        check(ap242.value("written").toBool(), "round trip: the AP242 file was written");
        check(ap242.value("schema").toString() == "ap242", "round trip: the AP242 file reads back AP242");
        check(ap242.value("fileSchemaRaw").toString().contains("10303 442"), "round trip: the AP242 identifier carries part 442");
        check(ap242.value("readStatus").toString() == "done", "round trip: the AP242 file reads");
        {
            QJsonObject probeShape;
            probeShape["boundingBox"] = ap242.value("boundingBox");
            boxGeometryCheck(probeShape, "round trip AP242");
        }
    }

    // 3. The cylinder and the assembly.
    {
        const auto cylinder = run({worker, "--probe-step", temp.filePath("sketchyup-step-cylinder.step")}).json;
        const auto shapes = cylinder.value("shapes").toArray();
        check(shapes.size() == 1 && shapes[0].toObject().value("faces").toInt() == 3, "cylinder: one solid, three faces");
        const auto assembly = run({worker, "--probe-step", temp.filePath("sketchyup-step-assembly.step")}).json;
        const auto assemblyShapes = assembly.value("shapes").toArray();
        check(assemblyShapes.size() == 1 && assemblyShapes[0].toObject().value("solids").toInt() == 2, "assembly: two solids");
    }

    // 4. The unit policy (ADR 0172 decision 4): the declared unit is authoritative.
    {
        const auto noOption = run({worker, "--import-step", box});
        check(noOption.code == 0, "import (no option): exits 0");
        check(noOption.json.value("status").toString() == "ok", "import (no option): ok");
        const auto units = noOption.json.value("units").toObject();
        check(units.value("applied").toString() == "declared", "import (no option): the declared unit is applied");
        check(near(units.value("mmPerUnit").toDouble(), 1.0, 1e-9), "import (no option): the millimetre is 1 mm");
        check(units.value("solidAngle").toString() == "STERADIAN", "import (no option): the box's solid angle is the steradian");
        // The raw-text unit scan reads the unit's own entity (the STEP complex entity writes its
        // parts in alphabetical order, so the SI_UNIT can stand before the marker), with the
        // whitespace tolerated:
        const auto spaced = run({worker, "--import-step", QStringLiteral(SOURCE_DIR "/tests/fixtures/step/spaced-units.step")});
        check(spaced.code == 0, "import (spaced units): exits 0");
        {
            const auto u = spaced.json.value("units").toObject();
            check(u.value("length").toString() == "millimetre", "spaced units: the millimetre is read");
            check(u.value("angle").toString() == "RADIAN", "spaced units: the radian is read");
            check(u.value("solidAngle").toString() == "STERADIAN", "spaced units: the steradian is read");
        }
        const QString noSolid = temp.filePath("no-solid.step");
        QFile noSolidOut(noSolid);
        check(noSolidOut.open(QIODevice::WriteOnly), "the no-solid-angle file");
        noSolidOut.write("ISO-10303-21;\nHEADER;\nFILE_DESCRIPTION(('x'),'2;1');\n"
                        "FILE_NAME('a','',(''),(''),'','','');\n"
                        "FILE_SCHEMA(('AUTOMOTIVE_DESIGN { 1 2 10303 214 1 1 1 1 }'));\nENDSEC;\nDATA;\n"
                        "#1=( LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.) );\n"
                        "#2=( NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.) );\nENDSEC;\n"
                        "END-ISO-10303-21;\n");
        noSolidOut.close();
        const auto noSolidResult = run({worker, "--import-step", noSolid});
        check(noSolidResult.code == 0, "import (no solid angle): exits 0");
        check(noSolidResult.json.value("units").toObject().value("solidAngle").toString().isEmpty(),
              "import (no solid angle): the solid angle is empty");
        const auto withOption = run({worker, "--import-step", box, "--step-length-unit", "in"});
        check(withOption.code == 0, "import (with an option): exits 0");
        check(withOption.json.value("units").toObject().value("applied").toString() == "declared",
              "import (with an option): the declared unit still wins");
    }

    // 5. The refusals. A file without the ISO-10303-21 header, and a file with the header
    // but no length unit and no --step-length-unit.
    {
        const QString noHeader = temp.filePath("no-header.step");
        QFile out(noHeader);
        check(out.open(QIODevice::WriteOnly), "the no-header file");
        out.write("this is not a STEP file\n");
        out.close();
        const auto refused = run({worker, "--import-step", noHeader});
        check(refused.code == 2, "refusal: exit 2 without the ISO header");
        check(refused.json.value("status").toString().startsWith("refused"), "refusal: the status is refused-*");
        const QString noUnits = temp.filePath("no-units.step");
        QFile unitsOut(noUnits);
        check(unitsOut.open(QIODevice::WriteOnly), "the no-units file");
        unitsOut.write("ISO-10303-21;\nHEADER;\nFILE_DESCRIPTION(('test'),'2;1');\n"
                     "FILE_NAME('test','2026-01-01',('A'),('O'),('T'),('P'),'');\n"
                     "FILE_SCHEMA(('AUTOMOTIVE_DESIGN { 1 2 10303 214 0 1 1 1 }'));\nENDSEC;\nDATA;\nENDSEC;\n");
        unitsOut.close();
        const auto noUnitsRefused = run({worker, "--import-step", noUnits});
        check(noUnitsRefused.code == 2, "refusal: exit 2 with no units and no option");
        check(noUnitsRefused.json.value("status").toString().startsWith("refused"), "refusal: refused-* without units and no option");
        const auto noUnitsOption = run({worker, "--import-step", noUnits, "--step-length-unit", "mm"});
        check(noUnitsOption.json.value("units").toObject().value("applied").toString() == "option",
              "import (no units, with the option): the option is applied");
    }

    // 6. R098.a2: the tessellated package import (--import-step --output PKG), against the
    // hand-computed geometry (ADR 0172 section 7 and E1): the box is exact, the cylinder and
    // the assembly within the tessellation error of the analytic values, the unit policy and
    // the triangle caps of section 6 held, and the package layout the application's decoder
    // reads (SKIFCP01, the counts and the node tree).
    {
        const QString boxFile = temp.filePath("sketchyup-step-box.step");
        const auto importPackage = [&](const QString &file, const QStringList &extra) {
            QStringList args{worker, "--import-step", file, "--output", temp.filePath(QFileInfo(file).fileName() + ".pkg")};
            args += extra;
            return run(args);
        };
        const auto checkPackage = [&](const QString &file) -> QString {
            bool ok = false;
            std::string problem;
            int nodes = 0, rootIsAssembly = 0;
            decodePackageCheck(file, ok, problem, nodes, rootIsAssembly);
            return ok ? QString() : QString::fromStdString(problem);
        };

        // The box: exact (planar faces carry no tessellation error).
        const auto box = importPackage(boxFile, {});
        check(box.code == 0, "package import (box): exits 0");
        check(box.json.value("status").toString() == "ok", "package import (box): ok");
        check(box.json.value("meshes").toInt() == 1 && box.json.value("converted").toInt() == 1
              && box.json.value("triangles").toInt() == 12, "package import (box): one mesh, twelve triangles");
        check(box.json.value("omitted").toInt() == 0 && box.json.value("unsupported").toInt() == 0,
              "package import (box): nothing omitted or unsupported");
        const QString boxPackage = temp.filePath("sketchyup-step-box.step.pkg");
        check(checkPackage(boxPackage).isEmpty(), "package decode (box): " + checkPackage(boxPackage).toStdString());
        if (auto geometry = packageVolumeAndArea(boxPackage)) {
            check(near(geometry->first, 0.01 * 0.02 * 0.03, 1e-9),
                  "package box: the volume is the analytic value, got " + std::to_string(geometry->first));
            check(near(geometry->second, 2.0 * (0.01 * 0.02 + 0.01 * 0.03 + 0.02 * 0.03), 1e-9),
                  "package box: the area is the analytic value, got " + std::to_string(geometry->second));
        } else {
            check(false, "package box: the geometry could not be read");
        }

        // The cylinder and the assembly: within the tessellation error (E1: 1e-3 relative for
        // analytic primitives; the 1 mm deflection is far below the 10 mm radius, so a 5%
        // envelope is a generous bound on the mesh's chord error).
        const auto cylinder = importPackage(temp.filePath("sketchyup-step-cylinder.step"), {});
        check(cylinder.code == 0 && cylinder.json.value("status").toString() == "ok",
              "package import (cylinder): ok");
        if (auto geometry = packageVolumeAndArea(temp.filePath("sketchyup-step-cylinder.step.pkg"))) {
            check(nearRel(geometry->first, 1.5708e-06, 0.05),
                  "package cylinder: the volume is within the tessellation error, got " + std::to_string(geometry->first));
            check(nearRel(geometry->second, 0.000785398, 0.05),
                  "package cylinder: the area is within the tessellation error, got " + std::to_string(geometry->second));
        } else {
            check(false, "package cylinder: the geometry could not be read");
        }
        const auto assembly = importPackage(temp.filePath("sketchyup-step-assembly.step"), {});
        check(assembly.code == 0 && assembly.json.value("status").toString() == "ok",
              "package import (assembly): ok");
        check(assembly.json.value("meshes").toInt() == 2 && assembly.json.value("converted").toInt() == 3
              && assembly.json.value("triangles").toInt() == 104,
              "package import (assembly): two meshes, 104 triangles (the group converts as a slot, no mesh of its own)");
        const QString assemblyPackage = temp.filePath("sketchyup-step-assembly.step.pkg");
        check(checkPackage(assemblyPackage).isEmpty(), "package decode (assembly): " + checkPackage(assemblyPackage).toStdString());
        if (auto geometry = packageVolumeAndArea(assemblyPackage)) {
            check(nearRel(geometry->first, 0.01 * 0.02 * 0.03 + 1.5708e-06, 0.05),
                  "package assembly: the combined volume is within the tessellation error");
            check(nearRel(geometry->second, 0.0022 + 0.000785398, 0.05),
                  "package assembly: the combined area is within the tessellation error");
        } else {
            check(false, "package assembly: the geometry could not be read");
        }

        // The placed and the mirrored fixtures (the R098.b corpus additions): the writer
        // bakes the placement into the coordinates (a det -1 placement is not expressible in
        // the step subset - the mirror is the face's orientation, ADR 0172 section 7), and
        // the 7.9.3 reader normalises the location to identity - so the package's mesh is
        // the geometry at the placed position, the node transforms are the identity (the
        // placement is not double applied), and the tessellated winding is the box's own
        // (the positive signed volume, the ADR 0172 section 7 mirrored-solid measurement).
        const auto placedImport = importPackage(QStringLiteral(SOURCE_DIR "/tests/fixtures/step/sketchyup-step-placed.step"), {});
        check(placedImport.code == 0 && placedImport.json.value("status").toString() == "ok",
              "package import (placed): ok");
        check(placedImport.json.value("meshes").toInt() == 1 && placedImport.json.value("converted").toInt() == 1
              && placedImport.json.value("triangles").toInt() == 12,
              "package import (placed): one mesh, twelve triangles");
        const QString placedPackage = temp.filePath("sketchyup-step-placed.step.pkg");
        check(checkPackage(placedPackage).isEmpty(), "package decode (placed): " + checkPackage(placedPackage).toStdString());
        if (auto geometry = packageGeometry(placedPackage)) {
            // The (5, 10, 15) mm translation, in the package's metres.
            check(near(geometry->min[0], 0.005) && near(geometry->max[0], 0.015)
                  && near(geometry->min[1], 0.01) && near(geometry->max[1], 0.03)
                  && near(geometry->min[2], 0.015) && near(geometry->max[2], 0.045),
                  "package placed: the mesh is at the translated coordinates, got [" + std::to_string(geometry->min[0]) + ", " + std::to_string(geometry->max[0]) + "]");
            check(near(geometry->volume, 0.01 * 0.02 * 0.03, 1e-9),
                  "package placed: the signed volume is the analytic value (the winding is the box's own), got " + std::to_string(geometry->volume));
            check(geometry->nonIdentityTransforms == 0,
                  "package placed: the node transforms are the identity (the placement is not double applied)");
        } else {
            check(false, "package placed: the geometry could not be read");
        }
        const auto mirror = importPackage(QStringLiteral(SOURCE_DIR "/tests/fixtures/step/sketchyup-step-mirror.step"), {});
        check(mirror.code == 0 && mirror.json.value("status").toString() == "ok",
              "package import (mirror): ok");
        check(mirror.json.value("meshes").toInt() == 1 && mirror.json.value("converted").toInt() == 1
              && mirror.json.value("triangles").toInt() == 12,
              "package import (mirror): one mesh, twelve triangles");
        const QString mirrorPackage = temp.filePath("sketchyup-step-mirror.step.pkg");
        check(checkPackage(mirrorPackage).isEmpty(), "package decode (mirror): " + checkPackage(mirrorPackage).toStdString());
        if (auto geometry = packageGeometry(mirrorPackage)) {
            // The mirror is the point symmetry about the OX line at the origin (the writer's
            // gp_Ax1 mirror): y and z flip, x is kept - the box at [0, 0.01] x [-0.02, 0] x [-0.03, 0] m.
            check(near(geometry->min[0], 0.0) && near(geometry->max[0], 0.01)
                  && near(geometry->min[1], -0.02) && near(geometry->max[1], 0.0)
                  && near(geometry->min[2], -0.03) && near(geometry->max[2], 0.0),
                  "package mirror: the mesh is at the mirrored coordinates, got [" + std::to_string(geometry->min[0]) + ", " + std::to_string(geometry->max[0]) + "]");
            check(near(geometry->volume, 0.01 * 0.02 * 0.03, 1e-9),
                  "package mirror: the signed volume is positive (the winding survives the mirror - the ADR 0172 section 7 mirrored-solid measurement), got " + std::to_string(geometry->volume));
            check(geometry->nonIdentityTransforms == 0,
                  "package mirror: the node transforms are the identity (a det -1 placement is not expressible in the step subset - the mirror is the face's orientation, not a transform)");
        } else {
            check(false, "package mirror: the geometry could not be read");
        }
        // The hand-crafted placed-assembly fixture (ADR 0172 section 4): the 7.9.3 reader
        // preserves the component's placement as a non-identity XDE location (unlike the
        // writer-generated cases, which normalise it), so the import applies it as the node
        // transform (in metres, the 50 mm offset) and the report counts it; the meshes stay
        // in the bases' own frames (the shared-mesh design).
        const auto placedAsm = importPackage(QStringLiteral(SOURCE_DIR "/tests/fixtures/step/sketchyup-step-placed-assembly.step"), {});
        check(placedAsm.code == 0 && placedAsm.json.value("status").toString() == "ok",
              "package import (placed assembly): ok");
        check(placedAsm.json.value("meshes").toInt() == 2 && placedAsm.json.value("converted").toInt() == 3
              && placedAsm.json.value("triangles").toInt() == 104,
              "package import (placed assembly): two meshes, three slots, 104 triangles (as the unplaced assembly)");
        const QString placedAsmPackage = temp.filePath("sketchyup-step-placed-assembly.step.pkg");
        check(checkPackage(placedAsmPackage).isEmpty(), "package decode (placed assembly): " + checkPackage(placedAsmPackage).toStdString());
        if (auto geometry = packageGeometry(placedAsmPackage)) {
            check(geometry->nonIdentityTransforms == 1,
                  "package placed assembly: exactly one node carries the placement (the offset component)");
            const double expected[16] = {1, 0, 0, 0.05, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
            bool transformOk = true;
            for (int k = 0; k < 16 && transformOk; ++k)
                transformOk = std::abs(geometry->firstNonIdentity[k] - expected[k]) <= 1e-12;
            check(transformOk,
                  "package placed assembly: the node transform is the (0.05, 0, 0) m translation (the 50 mm offset, in metres)");
            check(geometry->placed == 1,
                  "package placed assembly: the report counts the placement (placed = 1)");
            check(nearRel(geometry->volume, 0.01 * 0.02 * 0.03 + 1.5708e-06, 0.05),
                  "package placed assembly: the combined volume is within the tessellation error (the meshes are in the bases' own frames)");
        } else {
            check(false, "package placed assembly: the geometry could not be read");
        }

        // R098.b fixture (a) (ADR 0172 section 4): the two-level assembly with one part
        // instanced three times at different placements. The import shares the part's mesh
        // (tessellated once, in the base's own frame) across the three instances; each node's
        // transform carries the placement (the shared-mesh design).
        const auto instanced = importPackage(QStringLiteral(SOURCE_DIR "/tests/fixtures/step/sketchyup-step-instanced.step"), {});
        check(instanced.code == 0 && instanced.json.value("status").toString() == "ok",
              "package import (instanced): ok");
        check(instanced.json.value("meshes").toInt() == 1 && instanced.json.value("converted").toInt() == 2
              && instanced.json.value("triangles").toInt() == 12,
              "package import (instanced): one shared mesh, the group and the part converted, twelve triangles");
        check(instanced.json.value("nodes").toInt() == 5,
              "package import (instanced): five nodes (the root, the assembly, three instances)");
        const QString instancedPackage = temp.filePath("sketchyup-step-instanced.step.pkg");
        check(checkPackage(instancedPackage).isEmpty(), "package decode (instanced): " + checkPackage(instancedPackage).toStdString());
        {
            QFile in(instancedPackage);
            check(in.open(QIODevice::ReadOnly), "the instanced package opens");
            const QByteArray bytes = in.readAll();
            const uint32_t manifestBytes = qFromLittleEndian<uint32_t>(bytes.constData() + 8);
            const auto manifest = QJsonDocument::fromJson(bytes.mid(24, manifestBytes)).object();
            const auto nodes = manifest.value("nodes").toArray();
            check(nodes.size() == 5, "package instanced: five manifest nodes");
            if (nodes.size() == 5) {
                const auto root = nodes.at(0).toObject();
                const auto group = nodes.at(1).toObject();
                check(root.value("class").toString() == "IfcStepAssembly" && root.value("parent").toInt() == -1,
                      "package instanced: the root is an assembly");
                check(group.value("class").toString() == "IfcStepAssembly" && group.value("parent").toInt() == 0
                      && group.value("name").toString() == "Assembly",
                      "package instanced: the assembly node is the file's product, under the root");
                // The three instance nodes: one shared mesh index, three distinct placements (in
                // metres, the file's millimetres), all under the assembly, in the write order.
                bool sameMesh = true, distinct = true, parentsOk = true;
                const std::array<double, 3> expected[3] = {{0.05, 0.0, 0.0}, {0.0, 0.05, 0.0}, {0.0, 0.0, 0.05}};
                for (int i = 2; i < 5; ++i) {
                    const auto node = nodes.at(i).toObject();
                    if (node.value("class").toString() != "IfcStepSolid" || node.value("parent").toInt() != 1)
                        parentsOk = false;
                    if (i > 2 && node.value("mesh").toInt() != nodes.at(2).toObject().value("mesh").toInt())
                        sameMesh = false;
                    const auto transform = node.value("transform").toArray();
                    if (transform.size() != 16) {
                        distinct = false;
                        continue;
                    }
                    const double tx = transform.at(3).toDouble(), ty = transform.at(7).toDouble(), tz = transform.at(11).toDouble();
                    const auto &e = expected[static_cast<size_t>(i) - 2];
                    if (!near(tx, e[0]) || !near(ty, e[1]) || !near(tz, e[2])) distinct = false;
                    // The placement is a pure translation: the 3x3 is the identity.
                    if (std::abs(transform.at(0).toDouble() - 1.0) > 1e-12 || std::abs(transform.at(1).toDouble()) > 1e-12
                        || std::abs(transform.at(2).toDouble()) > 1e-12 || std::abs(transform.at(4).toDouble()) > 1e-12
                        || std::abs(transform.at(5).toDouble() - 1.0) > 1e-12 || std::abs(transform.at(6).toDouble()) > 1e-12
                        || std::abs(transform.at(8).toDouble()) > 1e-12 || std::abs(transform.at(9).toDouble()) > 1e-12
                        || std::abs(transform.at(10).toDouble() - 1.0) > 1e-12 || std::abs(transform.at(12).toDouble()) > 1e-12
                        || std::abs(transform.at(13).toDouble()) > 1e-12 || std::abs(transform.at(14).toDouble()) > 1e-12
                        || std::abs(transform.at(15).toDouble() - 1.0) > 1e-12)
                        distinct = false;
                }
                check(sameMesh, "package instanced: the three instances share one mesh index");
                check(distinct, "package instanced: the three instances carry the three distinct placements (in metres)");
                check(parentsOk, "package instanced: the three instances sit under the assembly, in the write order");
            }
            const auto counts = manifest.value("report").toObject().value("counts").toObject();
            check(counts.value("references").toInt() == 3 && counts.value("placed").toInt() == 3
                  && counts.value("assemblies").toInt() == 1 && counts.value("shapes").toInt() == 4
                  && counts.value("slots").toInt() == 2 && counts.value("solids").toInt() == 1,
                  "package instanced: the report counts the three references, the three placements and the two slots");
        }
        if (auto geometry = packageGeometry(instancedPackage)) {
            // The shared mesh is the box in its own frame: [0, 0.01] x [0, 0.02] x [0, 0.03] m, one box's volume.
            check(near(geometry->min[0], 0.0) && near(geometry->max[0], 0.01)
                  && near(geometry->min[1], 0.0) && near(geometry->max[1], 0.02)
                  && near(geometry->min[2], 0.0) && near(geometry->max[2], 0.03),
                  "package instanced: the shared mesh is in the base's own frame");
            check(near(geometry->volume, 0.01 * 0.02 * 0.03, 1e-9),
                  "package instanced: the shared mesh's volume is one box's (the tessellation is exact for the box)");
            check(geometry->nonIdentityTransforms == 3,
                  "package instanced: the three instance nodes carry the three placements (the group is the identity)");
        } else {
            check(false, "package instanced: the geometry could not be read");
        }

        // The unit policy (decision 4) held by the package import: the declared unit is
        // authoritative (the option does not rescale), a file with no unit and no option
        // refuses, and the option stands for a file that declares nothing.
        const auto withOption = importPackage(boxFile, {"--step-length-unit", "in"});
        check(withOption.code == 0 && withOption.json.value("status").toString() == "ok",
              "package import (declared units, with a mismatching option): the declared unit still wins");
        if (auto geometry = packageVolumeAndArea(boxPackage))
            check(near(geometry->first, 0.01 * 0.02 * 0.03, 1e-9),
                  "package import (with an option): the declared unit keeps the geometry unchanged");
        const QString noUnitsShape = temp.filePath("no-units-shape.step");
        {
            QFile in(boxFile);
            check(in.open(QIODevice::ReadOnly), "the box file for the no-units copy");
            const QString text = QString::fromUtf8(in.readAll());
            const QStringList snippets{QStringLiteral("GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT((#349)) GLOBAL_UNIT_ASSIGNED_CONTEXT\n((#346,#347,#348)) "),
                                      QStringLiteral("#346 = ( LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.) );\n"),
                                      QStringLiteral("#347 = ( NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.) );\n"),
                                      QStringLiteral("#348 = ( NAMED_UNIT(*) SI_UNIT($,.STERADIAN.) SOLID_ANGLE_UNIT() );\n"),
                                      QStringLiteral("#349 = UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(1.E-07),#346,\n  'distance_accuracy_value','confusion accuracy');\n")};
            QString stripped = text;
            for (const auto &snippet : snippets)
                stripped.replace(snippet, "");
            check(!stripped.contains("LENGTH_UNIT"), "the no-units copy: the length unit is stripped");
            QFile out(noUnitsShape);
            check(out.open(QIODevice::WriteOnly), "the no-units-with-a-shape file");
            out.write(stripped.toUtf8());
            out.close();
        }
        const auto noUnitsRefused = run({worker, "--import-step", noUnitsShape, "--output", temp.filePath("no-units.pkg")});
        check(noUnitsRefused.code == 3, "package refusal (no units, no option): exit 3");
        check(noUnitsRefused.json.value("status").toString() == "refused",
              "package refusal (no units, no option): the status is refused");
        const auto noUnitsOption = importPackage(noUnitsShape, {"--step-length-unit", "mm"});
        check(noUnitsOption.code == 0 && noUnitsOption.json.value("status").toString() == "ok",
              "package import (no units, with the option): ok");

        // The caps (ADR 0172 section 6): a solid over its per-element cap is reported as not
        // imported (and a file nothing converts writes no package), and the total budget the
        // remaining solids report; the report carries the notice.
        const auto capped = run({worker, "--import-step", temp.filePath("sketchyup-step-cylinder.step"),
                                 "--output", temp.filePath("capped.pkg"), "--step-max-triangles", "50"});
        check(capped.code == 3, "package import (the 92-triangle cylinder, per-element cap 50): refused");
        check(capped.json.value("status").toString() == "refused",
              "package import (capped): the status is refused (nothing left to convert)");
        const auto budget = run({worker, "--import-step", temp.filePath("sketchyup-step-assembly.step"),
                                "--output", temp.filePath("budget.pkg"), "--step-max-total-triangles", "50"});
        check(budget.code == 0, "package import (the 104-triangle assembly, total cap 50): the first fits");
        check(budget.json.value("converted").toInt() == 2 && budget.json.value("omitted").toInt() == 1,
              "package import (total cap 50): the group and the box convert, the cylinder is reported omitted");
        const auto deflection = run({worker, "--import-step", boxFile, "--output", temp.filePath("bad.pkg"),
                                     "--step-deflection-mm", "0"});
        check(deflection.code == 3 && deflection.json.value("status").toString() == "refused",
              "package import (deflection 0): refused");

        // The committed inch fixture (the corpus box with a conversion-based inch unit the
        // context applies): the reader's conversion (25.4 mm per file unit) stands, and the
        // box comes out 0.254 x 0.508 x 0.762 m.
        const QString inchFixture = QStringLiteral(SOURCE_DIR "/tests/fixtures/step/inch-units.step");
        if (QFile::exists(inchFixture)) {
            const auto inch = importPackage(inchFixture, {});
            check(inch.code == 0 && inch.json.value("status").toString() == "ok",
                  "package import (the inch fixture): ok");
            const QString inchPackage = temp.filePath("inch-units.step.pkg");
            check(checkPackage(inchPackage).isEmpty(), "package decode (inch): " + checkPackage(inchPackage).toStdString());
            if (auto geometry = packageVolumeAndArea(inchPackage)) {
                const double expected = 0.254 * 0.508 * 0.762;
                check(nearRel(geometry->first, expected, 1e-3),
                      "package inch: the volume is the analytic value (25.4 mm per file unit), got " + std::to_string(geometry->first));
                check(nearRel(geometry->second, 2.0 * (0.254 * 0.508 + 0.254 * 0.762 + 0.508 * 0.762), 1e-3),
                      "package inch: the area is the analytic value");
            } else {
                check(false, "package inch: the geometry could not be read");
            }
        }

        // The mirrored and offset solids (the review's winding check): a placement in the
        // step subset is a proper isometry, so a mirror-image solid reaches the kernel only
        // as a valid B-rep (a det -1 placement is not expressible and a degenerate one is
        // right-handeded by the reader). Two independent fixtures put the box's coordinates
        // at a negative x: one hand-crafted (the box's 3D points negated in x) and one the
        // OpenCASCADE writer itself writes (a proper translation of the box to x in
        // [-10, 0]). Both must come out a consistent outward-facing solid: the signed
        // volume is positive and exact, the area exact.
        const QString mirrorFixture = QStringLiteral(SOURCE_DIR "/tests/fixtures/step/mirror-box.step");
        if (QFile::exists(mirrorFixture)) {
            const auto mirror = importPackage(mirrorFixture, {});
            check(mirror.code == 0 && mirror.json.value("status").toString() == "ok",
                  "package import (the mirrored box): ok");
            check(mirror.json.value("meshes").toInt() == 1 && mirror.json.value("converted").toInt() == 1
                  && mirror.json.value("triangles").toInt() == 12,
                  "package import (the mirrored box): one mesh, twelve triangles");
            const QString mirrorPackage = temp.filePath("mirror-box.step.pkg");
            check(checkPackage(mirrorPackage).isEmpty(), "package decode (the mirrored box): " + checkPackage(mirrorPackage).toStdString());
            if (auto geometry = packageVolumeAndArea(mirrorPackage)) {
                check(near(geometry->first, 0.01 * 0.02 * 0.03, 1e-9),
                      "package mirror: the signed volume is positive and the analytic value, got " + std::to_string(geometry->first));
                check(near(geometry->second, 2.0 * (0.01 * 0.02 + 0.01 * 0.03 + 0.02 * 0.03), 1e-9),
                      "package mirror: the area is the analytic value, got " + std::to_string(geometry->second));
            } else {
                check(false, "package mirror: the geometry could not be read");
            }
        }
        const QString negativeFixture = QStringLiteral(SOURCE_DIR "/tests/fixtures/step/box-negative.step");
        if (QFile::exists(negativeFixture)) {
            const auto negative = importPackage(negativeFixture, {});
            check(negative.code == 0 && negative.json.value("status").toString() == "ok",
                  "package import (the OCCT-written negative-x box): ok");
            const QString negativePackage = temp.filePath("box-negative.step.pkg");
            check(checkPackage(negativePackage).isEmpty(), "package decode (the negative-x box): " + checkPackage(negativePackage).toStdString());
            if (auto geometry = packageVolumeAndArea(negativePackage)) {
                check(near(geometry->first, 0.01 * 0.02 * 0.03, 1e-9),
                      "package negative-x box: the signed volume is positive and the analytic value, got " + std::to_string(geometry->first));
                check(near(geometry->second, 2.0 * (0.01 * 0.02 + 0.01 * 0.03 + 0.02 * 0.03), 1e-9),
                      "package negative-x box: the area is the analytic value");
            } else {
                check(false, "package negative-x box: the geometry could not be read");
            }
        }

        // The metadata import is unchanged (R098.a): without --output the report stands, with
        // the units the import applies.
        const auto metadata = run({worker, "--import-step", boxFile});
        check(metadata.code == 0 && metadata.json.value("status").toString() == "ok",
              "metadata import (no --output): the report stands");
    }

    if (failures) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "ok\n";
    return 0;
}
