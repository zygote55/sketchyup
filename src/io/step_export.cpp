#include "io/step_export.hpp"

#include "core/material_records.hpp"
#include "core/tag_records.hpp"
#include "geometry/surface.hpp"
#include "io/ifc_worker_run.hpp"
#include "io/new_file.hpp"
#include "sketchyup/version.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <vector>

namespace sketchy {
namespace {

const char privatePrefix[] = "sketchyup-step-";
const qint64 kMaxStepBytes = qint64(256) * 1024 * 1024;  // the ADR's output cap (ADR 0172 section 6)
const std::size_t kMaxManifestBytes = 32u * 1024 * 1024;
const std::size_t kMaxMeshChunkBytes = 128u * 1024 * 1024;
const std::size_t kMaxNodes = 400'000;
const std::size_t kMaxMeshes = 400'000;
const std::size_t kMaxMeshTriangles = 100'000;  // per element (ADR 0172 section 6)
const std::size_t kMaxTotalTriangles = 1'000'000;
const std::size_t kMaxMeshVertices = 300'000;
const std::size_t kMaxNameBytes = 2048;
// The native model is in metres; the STEP file is in millimetres (ADR 0172 section 2: the export writes millimetres, so the
// package carries millimetres, scaled once here and never rescaled by the helper).
const double kMillimetresPerMetre = 1000.0;

[[noreturn]] void refuse(const std::string &code, const std::string &message, QJsonObject report = {}) {
    throw StepExportError(code, message, std::move(report));
}

// Control characters become spaces and the text is clipped to `bytes` at a character boundary.
std::string cleanText(const std::string &text, std::size_t bytes) {
    std::string out;
    for (const char c : text)
        out += static_cast<unsigned char>(c) < 0x20 ? ' ' : c;
    if (out.size() > bytes) {
        std::size_t cut = bytes;
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80)
            --cut;
        out.resize(cut);
    }
    return out;
}

// ---- Geometry helpers (as in the IFC export: a rigid part is what the placement can carry, the rest is a residual) ----
Vec3 column(const Transform &t, int c) {
    return {t.m[std::size_t(c * 4)], t.m[std::size_t(c * 4 + 1)], t.m[std::size_t(c * 4 + 2)]};
}

bool nearIdentity(const Transform &t, double tol = 1e-12) {
    const Transform identity;
    for (std::size_t i = 0; i < 16; ++i)
        if (std::abs(t.m[i] - identity.m[i]) > tol)
            return false;
    return true;
}

double snap(double v) {
    if (std::abs(v) < 1e-15)
        return 0;
    if (std::abs(v - 1) < 1e-15)
        return 1;
    if (std::abs(v + 1) < 1e-15)
        return -1;
    return v;
}

Transform rigidPart(const Transform &t) {
    auto x = normalized(column(t, 0));
    auto zRaw = column(t, 2);
    auto z = normalized(zRaw - x * dot(zRaw, x));
    auto y = cross(z, x);
    Transform r;
    r.m = {x.x, x.y, x.z, 0, y.x, y.y, y.z, 0, z.x, z.y, z.z, 0, t.m[12], t.m[13], t.m[14], 1};
    return r;
}

std::array<double, 9> placementOf(const Transform &r) {
    const Vec3 z = column(r, 2), x = column(r, 0);
    const double k = kMillimetresPerMetre;
    return {r.m[12] * k, r.m[13] * k, r.m[14] * k, snap(z.x), snap(z.y), snap(z.z), snap(x.x), snap(x.y), snap(x.z)};
}

// True when the three corners make no area (a zero-area triangle is left out, as the IFC export does).
bool degenerate(const Vec3 &a, const Vec3 &b, const Vec3 &c) {
    return length(cross(b - a, c - a)) < 1e-12;
}

// Every directed edge has its opposite in the mesh: the element is a closed solid (the helper makes a solid of it).
bool closedMesh(const std::vector<std::uint32_t> &triangles) {
    std::vector<std::uint64_t> directed;
    directed.reserve(triangles.size());
    for (std::size_t t = 0; t + 2 < triangles.size(); t += 3) {
        const std::uint64_t a = triangles[t], b = triangles[t + 1], c = triangles[t + 2];
        directed.push_back((a << 32) | b);
        directed.push_back((b << 32) | c);
        directed.push_back((c << 32) | a);
    }
    if (directed.empty())
        return false;
    std::sort(directed.begin(), directed.end());
    for (const std::uint64_t edge : directed) {
        const std::uint64_t reverse = ((edge & 0xFFFFFFFFu) << 32) | (edge >> 32);
        if (!std::binary_search(directed.begin(), directed.end(), reverse))
            return false;
    }
    return true;
}

// ---- Little-endian output of the mesh chunk ----
void putU32(QByteArray &out, std::uint32_t v) {
    for (int k = 0; k < 4; ++k)
        out.append(char((v >> (8 * k)) & 0xFF));
}

void putF64(QByteArray &out, double v) {
    std::uint64_t bits;
    std::memcpy(&bits, &v, sizeof bits);
    for (int k = 0; k < 8; ++k)
        out.append(char((bits >> (8 * k)) & 0xFF));
}

void putF32(QByteArray &out, float v) {
    std::uint32_t bits;
    std::memcpy(&bits, &v, sizeof bits);
    putU32(out, bits);
}

// One mesh of the package, in its element's frame.
struct StepMesh {
    std::vector<double> vertices;
    std::vector<std::uint32_t> triangles, faceOf, faceColor;
    std::vector<std::array<float, 3>> colors;
    std::size_t faces{};
    bool closed{};
    QByteArray chunk;  // the mesh's bytes in the mesh chunk
};

QByteArray meshChunk(const StepMesh &m) {
    QByteArray out;
    out.reserve(qsizetype(m.vertices.size() * 8 + m.triangles.size() * 4 + m.faceOf.size() * 4 + m.faceColor.size() * 4 + m.colors.size() * 12));
    for (const double v : m.vertices)
        putF64(out, v);
    for (const std::uint32_t index : m.triangles)
        putU32(out, index);
    for (const std::uint32_t face : m.faceOf)
        putU32(out, face);
    for (const std::uint32_t colour : m.faceColor)
        putU32(out, colour);
    for (const auto &rgb : m.colors)
        for (const float v : rgb)
            putF32(out, v);
    return out;
}

// One node of the package: a group (an assembly) or an element (a part with a mesh).
struct StepNode {
    bool assembly{};
    QString name, layer;
    long long parent{-1};
    long long mesh{-1};
    std::array<double, 9> placement{0, 0, 0, 0, 0, 1, 1, 0, 0};
    bool hidden{};
};

// The capture: the document's tree, each body in its place, into the nodes and the meshes.
class StepCapture {
public:
    explicit StepCapture(const Document &document) : doc_(document) {}

    StepExportCapture run(const QString &fileName) {
        index();
        for (const Id root : roots_)
            visit(root, -1, Transform{});
        if (!anyElement_)
            refuse("emptyModel", "The model has no body to export; nothing was written.");
        return assemble(fileName);
    }

private:
    void index() {
        for (const auto &[id, body] : doc_.bodies()) {
            if (body->parent)
                children_[body->parent].push_back(id);
            else
                roots_.push_back(id);
        }
        for (auto &[parent, list] : children_)
            std::sort(list.begin(), list.end());
        std::sort(roots_.begin(), roots_.end());
    }

    const Body &body(Id id) const { return *doc_.bodies().at(id); }

    // The body and its subtree. A node is pushed before its children; a group with nothing written under it is dropped
    // again (nothing is pushed after it in that case). `frame` is the parent's frame in the world.
    bool visit(Id id, long long parent, const Transform &parentFrame) {
        const Body &b = body(id);
        if (b.kind == BodyKind::Terrain || b.kind == BodyKind::ReferenceImage) {
            ++notWritten_;
            return false;
        }
        const Transform world = doc_.worldTransform(id);
        const Transform relative = parentFrame.inverse() * world;
        const Transform rigid = rigidPart(relative);
        const Transform frame = parentFrame * rigid;
        const bool hasChildren = children_.contains(id);
        const bool hidden = b.hidden || !tagVisible(doc_.tags(), b.tag);
        if (b.kind == BodyKind::Group || hasChildren) {
            const std::size_t at = nodes_.size();
            nodes_.push_back(StepNode{true, QString::fromStdString(cleanText(b.name, kMaxNameBytes)), {}, parent, -1, placementOf(rigid), hidden});
            bool any = false;
            // A geometry with children writes its own faces as an element under its group.
            if (!b.surface.faces.empty())
                any = element(id, static_cast<long long>(at), frame, world, {0, 0, 0, 0, 0, 1, 1, 0, 0}, hidden) || any;
            if (const auto found = children_.find(id); found != children_.end())
                for (const Id child : found->second)
                    any = visit(child, static_cast<long long>(at), frame) || any;
            if (!any) {
                nodes_.resize(at);
                return false;
            }
            ++groups_;
            return true;
        }
        return element(id, parent, frame, world, placementOf(rigid), hidden);
    }

    // An element: its mesh (shared with an identical one), its node under `parent`, in the frame `frame`.
    bool element(Id id, long long parent, const Transform &frame, const Transform &world, const std::array<double, 9> &placement, bool hidden) {
        const Body &b = body(id);
        if (b.surface.faces.empty()) {
            ++noFaces_;
            return false;
        }
        const Transform residual = frame.inverse() * world;
        StepMesh mesh = makeMesh(b, residual);
        if (mesh.triangles.empty()) {
            ++noTriangles_;
            return false;
        }
        if (mesh.triangles.size() / 3 > kMaxMeshTriangles)
            refuse("meshTooLarge", "The body \"" + cleanText(b.name, 80) + "\" has more triangles than an element takes (100,000).");
        if (mesh.vertices.size() / 3 > kMaxMeshVertices)
            refuse("meshTooLarge", "The body \"" + cleanText(b.name, 80) + "\" has more vertices than an element takes.");
        mesh.chunk = meshChunk(mesh);
        const std::string digest = QCryptographicHash::hash(mesh.chunk, QCryptographicHash::Sha256).toHex().toStdString();
        auto found = meshIndex_.find(digest);
        if (found == meshIndex_.end()) {
            totalTriangles_ += mesh.triangles.size() / 3;
            if (totalTriangles_ > kMaxTotalTriangles)
                refuse("tooManyTriangles", "The model has more than 1,000,000 triangles; nothing was written.");
            meshes_.push_back(std::move(mesh));
            found = meshIndex_.emplace(digest, meshes_.size() - 1).first;
        }
        // The node's name and layer are its own (a shared mesh is one part with several instances, when the name, the layer
        // and the visibility match).
        std::string layer = layerOf(b);
        nodes_.push_back(StepNode{false, QString::fromStdString(cleanText(b.name, kMaxNameBytes)), QString::fromStdString(cleanText(layer, kMaxNameBytes)),
                                  parent, static_cast<long long>(found->second), placement, hidden});
        anyElement_ = true;
        ++elements_;
        return true;
    }

    // The layer: the body's step.Layer (an imported body's own), else its tag's name (the tags are the layers).
    std::string layerOf(const Body &b) const {
        if (const auto found = b.properties.find("step.Layer"); found != b.properties.end())
            if (const auto *text = std::get_if<std::string>(&found->second))
                return *text;
        if (b.tag != 0)
            if (const auto tag = doc_.tags().find(b.tag); tag != doc_.tags().end() && tag->second->name.rfind("STEP ", 0) != 0)
                return tag->second->name;
        return {};
    }

    // The colour of a face: its front material's, else its own, else the body's.
    std::array<float, 3> colourOf(const Body &b, Id face) const {
        const MaterialSides sides = faceMaterials(b, face);
        if (sides.front)
            return doc_.materials().at(sides.front)->color;
        if (const auto found = b.faceColors.find(face); found != b.faceColors.end())
            return found->second;
        return b.color;
    }

    // The body's triangles in its own frame (the residual applied): one planar face per native face, its colour per face,
    // its vertices welded so that the faces share their edges.
    StepMesh makeMesh(const Body &b, const Transform &residual) {
        StepMesh out;
        std::map<std::array<double, 3>, std::uint32_t> welded;
        std::map<std::array<float, 3>, std::uint32_t> colourIndex;
        const bool identity = nearIdentity(residual);
        const bool flip = residual.determinant() < 0;
        for (const auto &[faceId, face] : b.surface.faces) {
            (void)face;
            std::vector<Triangle> triangles;
            try {
                triangles = b.surface.triangulateExact(faceId);
            } catch (const std::exception &) {
                ++notTriangulated_;
                continue;
            }
            std::uint32_t faceIndex = 0;
            bool started = false;
            for (const Triangle &t : triangles) {
                Vec3 corners[3] = {t.a, t.b, t.c};
                if (degenerate(corners[0], corners[1], corners[2])) {
                    ++degenerateTriangles_;
                    continue;
                }
                if (!started) {
                    const auto rgb = colourOf(b, faceId);
                    auto colour = colourIndex.find(rgb);
                    if (colour == colourIndex.end()) {
                        colour = colourIndex.emplace(rgb, std::uint32_t(out.colors.size())).first;
                        out.colors.push_back(rgb);
                    }
                    faceIndex = std::uint32_t(out.faces++);
                    out.faceColor.push_back(colour->second);
                    started = true;
                }
                std::uint32_t ids[3];
                for (int k = 0; k < 3; ++k) {
                    const Vec3 p = identity ? corners[k] : residual.point(corners[k]);
                    const double x = p.x * kMillimetresPerMetre, y = p.y * kMillimetresPerMetre, z = p.z * kMillimetresPerMetre;
                    const std::array<double, 3> key{x == 0 ? 0.0 : x, y == 0 ? 0.0 : y, z == 0 ? 0.0 : z};
                    auto found = welded.find(key);
                    if (found == welded.end()) {
                        found = welded.emplace(key, std::uint32_t(out.vertices.size() / 3)).first;
                        out.vertices.insert(out.vertices.end(), {key[0], key[1], key[2]});
                    }
                    ids[k] = found->second;
                }
                if (flip) std::swap(ids[1], ids[2]);
                out.triangles.insert(out.triangles.end(), {ids[0], ids[1], ids[2]});
                out.faceOf.push_back(faceIndex);
            }
        }
        out.closed = closedMesh(out.triangles);
        return out;
    }

    // The package: the manifest, the mesh chunk and the header.
    StepExportCapture assemble(const QString &fileName) {
        if (nodes_.size() > kMaxNodes || meshes_.size() > kMaxMeshes)
            refuse("packageTooLarge", "The model has more bodies than the STEP export takes; nothing was written.");
        QByteArray chunk;
        QJsonArray meshes;
        for (const StepMesh &m : meshes_) {
            chunk += m.chunk;
            meshes.append(QJsonObject{{"vertices", qint64(m.vertices.size() / 3)},
                                      {"triangles", qint64(m.triangles.size() / 3)},
                                      {"faces", qint64(m.faces)},
                                      {"colors", qint64(m.colors.size())},
                                      {"closed", m.closed}});
        }
        if (std::size_t(chunk.size()) > kMaxMeshChunkBytes)
            refuse("packageTooLarge", "The model's geometry is larger than the STEP export package takes; nothing was written.");
        QJsonArray nodes;
        std::size_t hidden = 0, instances = 0;
        for (const StepNode &n : nodes_) {
            QJsonArray placement;
            for (const double v : n.placement)
                placement.append(v);
            nodes.append(QJsonObject{{"role", n.assembly ? "assembly" : "element"},
                                     {"name", n.name},
                                     {"layer", n.layer},
                                     {"parent", qint64(n.parent)},
                                     {"mesh", qint64(n.mesh)},
                                     {"placement", placement},
                                     {"hidden", n.hidden}});
            hidden += n.hidden ? 1 : 0;
            instances += n.parent >= 0 ? 1 : 0;
        }
        const QByteArray digest = QCryptographicHash::hash(chunk, QCryptographicHash::Sha256).toHex();
        const QJsonObject manifest{{"format", "sketchyup-step-export-package"},
                                   {"version", 1},
                                   {"meshSha256", QString::fromLatin1(digest)},
                                   {"application", QJsonObject{{"name", "SketchyUp"}, {"version", QStringLiteral(SKETCHYUP_VERSION)}}},
                                   {"fileName", fileName},
                                   {"meshes", meshes},
                                   {"nodes", nodes}};
        const QByteArray body = QJsonDocument(manifest).toJson(QJsonDocument::Compact);
        if (std::size_t(body.size()) > kMaxManifestBytes)
            refuse("packageTooLarge", "The model's description is larger than the STEP export package takes; nothing was written.");

        StepExportCapture capture;
        capture.package.reserve(24 + body.size() + chunk.size());
        capture.package.append("SKSTPX01", 8);
        putU32(capture.package, std::uint32_t(body.size()));
        putU32(capture.package, 0);
        for (int k = 0; k < 8; ++k)
            capture.package.append(char((std::uint64_t(chunk.size()) >> (8 * k)) & 0xFF));
        capture.package += body;
        capture.package += chunk;

        QJsonObject plan;
        plan["groups"] = qint64(groups_);
        plan["elements"] = qint64(elements_);
        plan["instances"] = qint64(instances);
        plan["meshes"] = qint64(meshes_.size());
        std::uint64_t triangles = 0, vertices = 0, faces = 0;
        for (const StepMesh &m : meshes_) {
            triangles += m.triangles.size() / 3;
            vertices += m.vertices.size() / 3;
            faces += m.faces;
        }
        plan["triangles"] = qint64(triangles);
        plan["vertices"] = qint64(vertices);
        plan["faces"] = qint64(faces);
        plan["hidden"] = qint64(hidden);
        plan["notWritten"] = qint64(notWritten_);
        plan["notTriangulated"] = qint64(notTriangulated_);
        plan["degenerateTriangles"] = qint64(degenerateTriangles_);
        capture.plan = plan;
        return capture;
    }

    const Document &doc_;
    std::map<Id, std::vector<Id>> children_;
    std::vector<Id> roots_;
    std::vector<StepNode> nodes_;
    std::vector<StepMesh> meshes_;
    std::map<std::string, std::size_t> meshIndex_;
    std::uint64_t totalTriangles_{};
    std::uint64_t groups_{}, elements_{}, notWritten_{}, notTriangulated_{}, degenerateTriangles_{}, noFaces_{}, noTriangles_{};
    bool anyElement_{};
};

// Messages in the report are short and plain: a control character becomes a space, the text is clipped to 240 bytes.
QString clipMessage(QString message) {
    message.replace(QRegularExpression(QStringLiteral("[\\x00-\\x1f\\x7f]")), QStringLiteral(" "));
    while (message.toUtf8().size() > 240)
        message.chop(1);
    return message;
}

} // namespace

StepExportCapture captureStepExport(const Document &document, const QString &fileName) {
    return StepCapture(document).run(fileName);
}

StepExportResult exportStepFile(const Document &document, const QString &path, const StepExportOptions &options,
                                const IfcWorkerOptions &worker) {
    const auto progress = [&](const char *phase) {
        if (worker.progress)
            worker.progress(phase, 0);
    };
    const auto checkCancelled = [&] {
        if (worker.cancelled && worker.cancelled())
            refuse("cancelled", "The export was cancelled; nothing was written.");
    };
    // The destination first: nothing is built for a path that cannot be used.
    {
        const QFileInfo info(path);
        if (path.isEmpty() || info.fileName().isEmpty())
            refuse("destination", "Choose a new file for the STEP export.");
        if (info.exists() || info.isSymLink())
            refuse("destinationExists", "That file already exists. An existing file is never replaced; choose a new name.");
        if (info.dir().canonicalPath().isEmpty())
            refuse("destination", "The folder for the STEP export does not exist.");
    }
    if (options.schema != "ap214" && options.schema != "ap203")
        refuse("schema", "The STEP export writes AP214 (the default) or AP203; AP242 writing is not supported.");
    const QString workerPath = findIfcWorker(worker.workerPath);
    if (workerPath.isEmpty())
        refuse("workerMissing", "The STEP helper (sketchyup-ifc-worker) is not installed or cannot be run; install the sketchyup-ifc package. "
                                "Nothing else is affected.");
    progress("capture");
    const QString fileName = QFileInfo(path).fileName();
    auto capture = captureStepExport(document, QString::fromStdString(cleanText(fileName.toStdString(), 255)));
    checkCancelled();

    const QString root = worker.scratchRoot.isEmpty() ? QDir::tempPath() : worker.scratchRoot;
    QTemporaryDir directory(QDir(root).filePath(QString(privatePrefix) + "XXXXXX"));
    if (!directory.isValid())
        refuse("scratch", "A private working directory could not be created.");
    QFile::setPermissions(directory.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    const QString packagePath = QDir(directory.path()).filePath("export.skstp");
    const QString outputPath = QDir(directory.path()).filePath("export.step");
    {
        QFile file(packagePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly) || file.write(capture.package) != capture.package.size() || !file.flush())
            refuse("scratch", "The export package could not be written to the private working directory.");
        file.close();
        QFile::setPermissions(packagePath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
    progress("build");
    QStringList exportArguments{"--export-step", packagePath, "--output", outputPath, "--step-schema", options.schema};
    QStringList limits;
    if (worker.addressSpaceBytes > 0)
        limits << "--rlimit-as" << QString::number(worker.addressSpaceBytes);
    limits << "--rlimit-cpu" << QString::number(qint64(worker.deadlineSeconds) * 4);
    exportArguments += limits;
    exportArguments << "--rlimit-fsize" << QString::number(kMaxStepBytes + 1024 * 1024);
    const auto run = runIfcWorker(workerPath, directory.path(), exportArguments, worker);
    QJsonObject report{{"plan", capture.plan}};
    if (run.kind == IfcWorkerRun::Cancelled)
        refuse("cancelled", "The export was cancelled; nothing was written.");
    if (run.kind == IfcWorkerRun::Refused) {
        static const QRegularExpression identifier("^[A-Za-z][A-Za-z0-9]{0,39}\\z");
        QString code = run.result["code"].toString();
        if (!identifier.match(code).hasMatch())
            code = "workerRefused";
        refuse(code.toStdString(), clipMessage(run.result["message"].toString()).toStdString(), report);
    }
    if (run.kind != IfcWorkerRun::Finished) {
        if (run.reason == "sandboxViolation")
            refuse("sandboxViolation",
                   "The IFC helper was stopped by its sandbox; nothing was written.", report);
        const QFileInfo produced(outputPath);
        if (produced.exists() && produced.size() >= kMaxStepBytes)
            refuse("outputTooLarge", "The STEP file would be larger than 256 MiB; nothing was written.", report);
        report["logTail"] = QString(run.logTail).replace(directory.path(), "<private directory>").replace(root, "<temporary directory>");
        refuse("workerFailed", "The STEP helper stopped (" + run.reason.toStdString() + "); nothing was written.", report);
    }
    progress("check");
    checkCancelled();
    const QFileInfo produced(outputPath);
    if (!produced.isFile() || produced.isSymLink() || produced.size() <= 0)
        refuse("workerOutput", "The STEP helper produced no file.", report);
    if (produced.size() > kMaxStepBytes)
        refuse("outputTooLarge", "The STEP file would be larger than 256 MiB; nothing was written.", report);
    QByteArray bytes;
    {
        QFile file(outputPath);
        if (!file.open(QIODevice::ReadOnly))
            refuse("workerOutput", "The STEP helper's file cannot be read.", report);
        bytes = file.read(kMaxStepBytes + 1);
    }
    if (bytes.size() != produced.size() || !bytes.startsWith("ISO-10303-21;") || !bytes.right(64).trimmed().endsWith("END-ISO-10303-21;"))
        refuse("workerOutput", "The STEP helper's file is not a complete STEP file.", report);

    // What the helper wrote must be what the document said it would: the assemblies and the instances.
    const auto counts = run.result;
    const auto planned = capture.plan;
    QStringList mismatches;
    if (counts["assemblies"].toDouble(-1) != planned["groups"].toDouble())
        mismatches << QStringLiteral("assemblies: file %1, document %2").arg(counts["assemblies"].toInt()).arg(planned["groups"].toInt());
    if (counts["instances"].toDouble(-1) != planned["instances"].toDouble())
        mismatches << QStringLiteral("instances: file %1, document %2").arg(counts["instances"].toInt()).arg(planned["instances"].toInt());
    if (!mismatches.isEmpty())
        refuse("workerMismatch", ("The STEP file does not match the model: " + mismatches.join("; ")).toStdString(), report);

    report["engine"] = QJsonObject{{"library", "OpenCASCADE"},
                                   {"version", counts["occt"]},
                                   {"completed", true},
                                   {"deadlineSeconds", worker.deadlineSeconds},
                                   {"addressSpaceBytes", double(worker.addressSpaceBytes)}};
    report["target"] = QJsonObject{{"schema", options.schema == "ap203" ? "AP203" : "AP214"},
                                   {"geometry", "tessellated"},
                                   {"units", "millimetre"},
                                   {"fileName", fileName},
                                   {"bytes", double(bytes.size())},
                                   {"sha256", QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())},
                                   {"documentId", QString::fromStdString(document.identity())}};
    report["file"] = QJsonObject{{"parts", counts["parts"]},
                                 {"assemblies", counts["assemblies"]},
                                 {"instances", counts["instances"]},
                                 {"faces", counts["faces"]},
                                 {"solids", counts["solids"]},
                                 {"shells", counts["shells"]},
                                 {"fallbacks", counts["fallbacks"]},
                                 {"hidden", counts["hidden"]},
                                 {"layers", counts["layers"]},
                                 {"colouredParts", counts["colouredParts"]}};
    progress("publish");
    checkCancelled();
    try {
        publishNewFile(path, bytes);
    } catch (const std::exception &error) {
        refuse("destination", error.what(), report);
    }
    return {report, qint64(bytes.size())};
}

} // namespace sketchy
