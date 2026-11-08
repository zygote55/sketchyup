// R114 (benchmark only; EXCLUDE_FROM_ALL): how far the model limits go on real hardware.
// Build with -DSKETCHYUP_BENCHMARK_LIMIT_SCALE=8 (Release). The scale is never defined by a
// product target, so the limits this binary reports are the scaled ones. One family and one tier
// per process; each prints one JSON object on stdout.
//
//   placements N          a 26-sided prism, one definition placed N times (N = 10,000 ... 160,000)
//   dense-definition P    one 176,000-face closed sphere definition placed P times (P = 3, 7, 14,
//   27) unique F              closed spheres of 10,000 faces, each distinct, about F faces in all
//   dense-body F          one closed sphere of F faces (F = 100,000 ... 800,000)
//   textures MIB          prisms, each with its own 4096 x 4096 PNG, about MIB of stored bytes
//   texture-large MIB     as textures, with 8192 x 8192 PNGs (R118: the largest a 64 MiB asset holds)
//   definitions D         D prism definitions, one placement each (D = 1,024 ... 16,384)
//
// Arguments: FAMILY TIER [EDIT_SAMPLES]. Environment: LIMIT_WORK_DIR, a directory on disk for the
// native save (required on the GPU PC; never tmpfs); SKETCHYUP_BENCHMARK_FULLSCREEN=1 for the
// fullscreen window used on the PC.
#include "app/surface_format.hpp"
#include "app/viewport.hpp"
#include "core/assets.hpp"
#include "core/components.hpp"
#include "core/document_limits.hpp"
#include "core/materials.hpp"
#include "drm_memory.hpp"
#include "geometry/inference.hpp"
#include "geometry/topology.hpp"
#include "integrations/glb_export.hpp"
#include "integrations/render_snapshot.hpp"
#include "io/document_io.hpp"
#include "io/native_limits.hpp"
#include <QApplication>
#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <numbers>
#include <random>
#include <sys/resource.h>
#include <unistd.h>
using namespace sketchy;
namespace {
void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
double percentile(std::vector<double> samples, double quantile) {
    if (samples.empty())
        return 0;
    std::sort(samples.begin(), samples.end());
    const auto index =
        std::min(samples.size() - 1, size_t(std::ceil(double(samples.size()) * quantile)) - 1);
    return samples[index];
}
QJsonObject summary(const std::vector<double> &samples) {
    return QJsonObject{
        {"n", qint64(samples.size())},
        {"p50", percentile(samples, .5)},
        {"p95", percentile(samples, .95)},
        {"p99", percentile(samples, .99)},
        {"max", samples.empty() ? 0.0 : *std::max_element(samples.begin(), samples.end())}};
}
double peakMiB() {
    rusage usage{};
    check(getrusage(RUSAGE_SELF, &usage) == 0, "Read peak RSS");
    return usage.ru_maxrss / 1024.0;
}
double nowSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
class TimedViewport final : public Viewport {
  public:
    using Viewport::Viewport;
    double completeFrameMs() const { return completeFrameMs_; }
    void beginEditFrame(const QElapsedTimer &started) {
        check(!editFramePending_, "Previous edit frame was completed");
        editStarted_ = started;
        editFrameMs_ = 0;
        editFramePending_ = true;
    }
    double editFrameMs() const {
        check(!editFramePending_ && editFrameMs_ > 0, "Edit reached a complete viewport frame");
        return editFrameMs_;
    }

  protected:
    void paintGL() override {
        QElapsedTimer timer;
        timer.start();
        Viewport::paintGL(); // Returns after QPainter finishes every viewport overlay.
        context()->functions()->glFinish();
        completeFrameMs_ = timer.nsecsElapsed() / 1e6;
        if (editFramePending_) {
            editFrameMs_ = editStarted_.nsecsElapsed() / 1e6;
            editFramePending_ = false; // Keep the first frame, before any readback repaint.
        }
    }

  private:
    double completeFrameMs_{};
    QElapsedTimer editStarted_;
    double editFrameMs_{};
    bool editFramePending_{};
};
// A closed triangulated sphere of `sides` by `rings` (the R110 densest shape): 2 + sides * rings
// vertices and 2 * sides * rings faces. The radius is a parameter so that each body is distinct.
std::shared_ptr<Body> sphere(Id id, Vec3 centre, double radius, int sides, int rings) {
    auto body = std::make_shared<Body>();
    body->id = id;
    body->name = "Sphere " + std::to_string(id);
    auto &surface = body->surface;
    auto add = [&](Vec3 point) {
        const Id vertex = surface.nextId++;
        surface.vertices.emplace(vertex, centre + point);
        return vertex;
    };
    const Id north = add({0, 0, radius});
    std::vector<std::vector<Id>> ring(static_cast<std::size_t>(rings));
    for (auto &row : ring)
        row.resize(size_t(sides));
    for (int r = 0; r < rings; ++r) {
        const double theta = std::numbers::pi * (r + 1) / (rings + 1);
        for (int j = 0; j < sides; ++j) {
            const double phi = 2 * std::numbers::pi * j / sides;
            ring[size_t(r)][size_t(j)] =
                add({radius * std::sin(theta) * std::cos(phi),
                     radius * std::sin(theta) * std::sin(phi), radius * std::cos(theta)});
        }
    }
    const Id south = add({0, 0, -radius});
    for (int j = 0; j < sides; ++j) {
        const int next = (j + 1) % sides;
        surface.addFaceIds({{north, ring.front()[size_t(j)], ring.front()[size_t(next)]}});
    }
    for (int r = 0; r + 1 < rings; ++r)
        for (int j = 0; j < sides; ++j) {
            const int next = (j + 1) % sides;
            const auto &a = ring[size_t(r)], &b = ring[size_t(r + 1)];
            surface.addFaceIds({{a[size_t(j)], b[size_t(j)], b[size_t(next)]}});
            surface.addFaceIds({{a[size_t(j)], b[size_t(next)], a[size_t(next)]}});
        }
    for (int j = 0; j < sides; ++j) {
        const int next = (j + 1) % sides;
        surface.addFaceIds({{south, ring.back()[size_t(next)], ring.back()[size_t(j)]}});
    }
    body->topology = Topology::rebuild(body->surface, body->topology);
    return body;
}
// The 26-sided prism of the placement families: 28 faces, 100 triangles.
std::shared_ptr<Body> prism(Id id, int sides, double radius, Vec3 offset) {
    std::vector<Vec3> loop;
    for (int i = 0; i < sides; ++i) {
        const auto angle = 2 * std::numbers::pi * i / sides;
        loop.push_back({radius * std::cos(angle), radius * std::sin(angle), 0});
    }
    auto body = std::make_shared<Body>();
    body->id = id;
    body->name = "Prism " + std::to_string(id);
    const Id face = body->surface.addFace({loop});
    body->surface.extrude(face, 1);
    body->transform = Transform::translation(offset);
    body->topology = Topology::rebuild(body->surface, body->topology);
    return body;
}
// CRC-32 of a PNG chunk (type and data), bit by bit: a few hundred bytes, so the cost is nil.
std::uint32_t pngCrc(const std::uint8_t *bytes, std::size_t count) {
    std::uint32_t crc = 0xffffffffu;
    for (std::size_t i = 0; i < count; ++i) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 1u) ? (crc >> 1) ^ 0xedb88320u : crc >> 1;
    }
    return ~crc;
}
// A deterministic side x side RGB image whose channels carry 2 bits of noise each, so that it
// encodes to roughly a quarter of its raw size. It is encoded once. R118: 4096 for the textures
// family, 8192 (the largest a 64 MiB asset holds) for texture-large.
std::vector<std::uint8_t> texturePngBase(int side) {
    QImage image(side, side, QImage::Format_RGB888);
    std::mt19937 generator(7);
    for (int y = 0; y < image.height(); ++y) {
        auto *row = image.scanLine(y);
        for (int x = 0; x < image.width() * 3; ++x)
            row[x] = uchar((generator() & 3u) * 85u);
    }
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    check(image.save(&buffer, "PNG"), "Encode a square texture");
    return {bytes.begin(), bytes.end()};
}
// Variant `seed`: the same pixels with a tEXt chunk before IEND that names the seed, so that every
// texture's bytes differ while each decodes to the same image. The chunk is ancillary, so a
// conforming decoder ignores it.
std::vector<std::uint8_t> texturePng(const std::vector<std::uint8_t> &base, unsigned seed) {
    // A tEXt chunk is keyword, NUL, text.
    const std::string text = std::string("bench", 5) + std::string(1, '\0') + std::to_string(seed);
    std::vector<std::uint8_t> chunk;
    auto put32 = [&](std::uint32_t value) {
        for (int shift = 24; shift >= 0; shift -= 8)
            chunk.push_back(std::uint8_t(value >> shift));
    };
    put32(std::uint32_t(text.size()));
    const std::size_t typeAt = chunk.size();
    chunk.insert(chunk.end(), {'t', 'E', 'X', 't'});
    chunk.insert(chunk.end(), text.begin(), text.end());
    put32(pngCrc(chunk.data() + typeAt, chunk.size() - typeAt));
    // The last 12 bytes are the IEND chunk: length 0, type, CRC.
    check(base.size() > 12, "The base texture is a PNG");
    std::vector<std::uint8_t> out(base.begin(), base.end() - 12);
    out.insert(out.end(), chunk.begin(), chunk.end());
    out.insert(out.end(), base.end() - 12, base.end());
    return out;
}
// The bodies a family builds before any component or edit exists.
struct Fixture {
    Document doc;
    std::vector<Id> instances; // placements for picks and moves (component families)
    Id definition{};           // the definition the definition edit changes (component families)
    Id body{};                 // the body the move and push-pull change (body families)
    size_t textureAssets{};
    std::string description;
};
// Restores bodies as the native decoder would: one call, no edit history.
void restoreBodies(Document &doc, const char *identity, std::map<Id, BodyPtr> &&bodies, Id next) {
    doc.restore(identity, next, std::move(bodies));
}
// Fills `f`. A refused edit throws with the product's own message; `f` then holds what was built.
void buildFamily(Fixture &f, const QString &family, long long tier) {
    auto &doc = f.doc;
    if (family == "placements") {
        check(tier >= 1 && tier <= DocumentLimits::materialized.placements,
              "Placements tier is within the placement limit");
        std::map<Id, BodyPtr> bodies{{1, prism(1, 26, 1, {0, 0, 0})}};
        restoreBodies(doc, "00000000000000000000000000000a01", std::move(bodies), 2);
        const auto component = createComponent(doc, 1, "26-sided benchmark prism");
        f.instances.push_back(component.instance);
        const int columns = int(std::ceil(std::sqrt(double(tier))));
        for (long long i = 1; i < tier; ++i)
            f.instances.push_back(placeComponent(doc, component.definition,
                                                 Transform::translation(
                                                     {4.0 * (i % columns), 4.0 * (i / columns), 0}))
                                      .instance);
        f.definition = component.definition;
        f.description = "26-sided prism, one definition placed " + std::to_string(tier) + " times";
    } else if (family == "dense-definition") {
        check(tier >= 1 && tier <= 27, "Dense definition tier is one of 3, 7, 14 and 27");
        // 176,000 faces: 352 sides by 250 rings (2 * 352 * 250).
        std::map<Id, BodyPtr> bodies{{1, sphere(1, {0, 0, 0}, 20, 352, 250)}};
        restoreBodies(doc, "00000000000000000000000000000a02", std::move(bodies), 2);
        const auto component = createComponent(doc, 1, "Dense sphere");
        f.instances.push_back(component.instance);
        const int columns = int(std::ceil(std::sqrt(double(tier))));
        for (long long i = 1; i < tier; ++i)
            f.instances.push_back(placeComponent(doc, component.definition,
                                                 Transform::translation({50.0 * (i % columns),
                                                                         50.0 * (i / columns), 0}))
                                      .instance);
        f.definition = component.definition;
        f.description =
            "one 176,000-face sphere definition placed " + std::to_string(tier) + " times";
    } else if (family == "unique") {
        // 200-face spheres (20 sides by 5 rings). Push-pull refuses a body above 1,000 faces in the
        // product, and above about 1,414 faces at any scale (its face-pair budget is quadratic); a
        // push-pull adds faces to its body, so the bodies stay small enough for repeated
        // push-pulls.
        const long long bodiesWanted = std::max<long long>(1, (tier + 199) / 200);
        std::map<Id, BodyPtr> bodies;
        const int columns = int(std::ceil(std::sqrt(double(bodiesWanted))));
        for (long long i = 0; i < bodiesWanted; ++i) {
            auto body = sphere(Id(i + 1), {50.0 * (i % columns), 50.0 * (i / columns), 0},
                               20 + .01 * double(i), 20, 5); // 200 faces each
            bodies.emplace(Id(i + 1), body);
        }
        restoreBodies(doc, "00000000000000000000000000000a03", std::move(bodies),
                      Id(bodiesWanted + 1));
        f.body = 1;
        f.description = std::to_string(bodiesWanted) + " distinct spheres of 200 faces";
    } else if (family == "dense-body") {
        // Faces = 2 * sides * rings: 100,000 = 200 x 250; 200,000 = 400 x 250; 400,000 = 400 x 500;
        // 800,000 = 800 x 500.
        check(tier >= 1 && tier % 2 == 0, "Dense body tier is an even face count");
        const int rings = tier <= 200000 ? 250 : 500;
        const int sides = int(tier / (2 * rings));
        std::map<Id, BodyPtr> bodies{{1, sphere(1, {0, 0, 0}, 20, sides, rings)}};
        restoreBodies(doc, "00000000000000000000000000000a04", std::move(bodies), 2);
        f.body = 1;
        f.description = "one closed sphere of " + std::to_string(tier) + " faces";
    } else if (family == "textures" || family == "texture-large") {
        const long long targetBytes = tier * 1024 * 1024;
        // Generate the first texture to learn its stored size, then choose the count.
        const int side = family == "texture-large" ? 8192 : 4096;
        const auto base = texturePngBase(side);
        auto first = texturePng(base, 0);
        const auto perTexture = first.size();
        const auto size = static_cast<long long>(perTexture);
        // Whole textures that fit the target: rounding up would exceed the total the tier names.
        const long long count = std::max<long long>(1, targetBytes / size);
        std::map<Id, BodyPtr> bodies;
        const int columns = int(std::ceil(std::sqrt(double(count))));
        for (long long i = 0; i < count; ++i)
            bodies.emplace(Id(i + 1),
                           prism(Id(i + 1), 26, 1, {4.0 * (i % columns), 4.0 * (i / columns), 0}));
        restoreBodies(doc, "00000000000000000000000000000a05", std::move(bodies), Id(count + 1));
        for (long long i = 0; i < count; ++i) {
            auto bytes = i == 0 ? std::move(first) : texturePng(base, unsigned(i));
            const auto payload = std::make_shared<const AssetPayload>(bytes);
            const Id asset = createAsset(doc, "Texture " + std::to_string(i), "image/png", payload);
            const Id material =
                createMaterial(doc, "Material " + std::to_string(i), {1, 1, 1}, 1, asset);
            assignMaterial(doc, Id(i + 1), std::nullopt, material, true, false);
            ++f.textureAssets;
        }
        f.body = 1;
        f.description = std::to_string(count) + " prisms, each with a distinct " +
                        std::to_string(side) + " x " + std::to_string(side) + " PNG (about " +
                        std::to_string(perTexture) + " bytes each)";
    } else if (family == "definitions") {
        check(tier >= 1, "Definition count is positive");
        std::map<Id, BodyPtr> bodies;
        const int columns = int(std::ceil(std::sqrt(double(tier))));
        for (long long i = 0; i < tier; ++i)
            bodies.emplace(Id(i + 1),
                           prism(Id(i + 1), 26, 1, {4.0 * (i % columns), 4.0 * (i / columns), 0}));
        restoreBodies(doc, "00000000000000000000000000000a06", std::move(bodies), Id(tier + 1));
        for (long long i = 1; i <= tier; ++i) {
            const auto component = createComponent(doc, Id(i), "Part " + std::to_string(i));
            f.instances.push_back(component.instance);
            if (i == 1)
                f.definition = component.definition;
        }
        f.description = std::to_string(tier) + " prism definitions, one placement each";
    } else {
        throw std::runtime_error("Unknown family: " + family.toStdString());
    }
}
// Counts for the record: records as the limits count them, and the unique faces the limit compares.
QJsonObject countsOf(const Document &doc) {
    // As DocumentLimits counts unique geometry: a body that a placement projects (its parent is a
    // placement) does not count; each definition's members count once.
    size_t uniqueFaces{}, uniqueVertices{}, uniqueEdges{};
    for (const auto &[id, body] : doc.bodies()) {
        (void)id;
        if (doc.instances().contains(body->parent))
            continue;
        uniqueFaces += body->surface.faces.size();
        uniqueVertices += body->surface.vertices.size();
        uniqueEdges += body->topology.edges.size();
    }
    for (const auto &[id, definition] : doc.definitions()) {
        (void)id;
        for (const auto &[member, body] : definition->members) {
            (void)member;
            uniqueFaces += body->surface.faces.size();
            uniqueVertices += body->surface.vertices.size();
            uniqueEdges += body->topology.edges.size();
        }
    }
    size_t totalAssetBytes{};
    for (const auto &[id, asset] : doc.assets()) {
        (void)id;
        totalAssetBytes += assetBytes(asset);
    }
    const auto &totals = doc.materializedTotals();
    return QJsonObject{{"bodies", qint64(doc.bodies().size())},
                       {"definitions", qint64(doc.definitions().size())},
                       {"placements", qint64(doc.instances().size())},
                       {"uniqueFaces", qint64(uniqueFaces)},
                       {"uniqueVertices", qint64(uniqueVertices)},
                       {"uniqueEdges", qint64(uniqueEdges)},
                       {"materializedFaces", qint64(totals.faces)},
                       {"materializedVertices", qint64(totals.vertices)},
                       {"materializedEdges", qint64(totals.edges)},
                       {"assets", qint64(doc.assets().size())},
                       {"assetBytes", qint64(totalAssetBytes)}};
}
} // namespace
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(0);
    sketchy::setDefaultViewportFormat(format);
    QTemporaryDir settings;
    qputenv("XDG_CONFIG_HOME", settings.path().toUtf8());
    qputenv("XDG_DATA_HOME", settings.path().toUtf8());
    QApplication app(argc, argv);
    try {
        check(argc >= 3 && argc <= 4,
              "Usage: limits_hardware_benchmark FAMILY TIER [EDIT_SAMPLES]");
        const QString family = QString::fromLocal8Bit(argv[1]);
        bool ok = false;
        const long long tier = QString::fromLocal8Bit(argv[2]).toLongLong(&ok);
        check(ok && tier >= 1, "Tier is a positive integer");
        const int samples = argc == 4 ? QString::fromLocal8Bit(argv[3]).toInt(&ok) : 20;
        check(ok && samples >= 1 && samples <= 200, "Edit samples are 1..200");
        const auto workDir = qEnvironmentVariable("LIMIT_WORK_DIR");
        QTemporaryDir scratch((workDir.isEmpty() ? QDir::tempPath() : workDir) + "/r114-XXXXXX");
        check(scratch.isValid(), "Create the native save directory");
        QElapsedTimer timer;
        timer.start();
        QJsonObject report{
            {"family", family},
            {"tier", qint64(tier)},
            {"editSamples", samples},
            {"buildType", SKETCHYUP_BENCHMARK_BUILD_TYPE},
            {"limitScale", int(DocumentLimits::scale)},
            {"limits",
             QJsonObject{
                 {"uniqueFaces", qint64(DocumentLimits::unique.faces)},
                 {"uniqueVertices", qint64(DocumentLimits::unique.vertices)},
                 {"uniqueEdges", qint64(DocumentLimits::unique.edges)},
                 {"materializedFaces", qint64(DocumentLimits::materialized.faces)},
                 {"materializedVertices", qint64(DocumentLimits::materialized.vertices)},
                 {"materializedEdges", qint64(DocumentLimits::materialized.edges)},
                 {"materializedPlacements", qint64(DocumentLimits::materialized.placements)},
                 {"uniqueBodies", qint64(DocumentLimits::unique.bodies)},
                 {"oneBodyFaces", qint64(800000 * DocumentLimits::scale)},
                 {"readSnapshotBytes", qint64(Document::readSnapshotByteLimit)},
                 {"fileBytes", qint64(NativeLimits::fileBytes)},
                 {"modelBytes", qint64(NativeLimits::modelBytes)},
                 {"assetBytes", qint64(AssetPayload::limit)},
                 {"totalAssetBytes", qint64(assetTotalLimit)},
                 {"glbVisibleFaces", qint64(glbVisibleFaceLimit)},
                 {"renderSnapshotBodies", qint64(renderSnapshotBodyLimit)},
                 {"componentDefinitions", qint64(1024 * DocumentLimits::scale)}}},
            {"platform", QGuiApplication::platformName()},
            {"qtVersion", qVersion()},
            {"kernel", QSysInfo::kernelVersion()}};
        // Fixture: the build is timed. A refusal is data: the record names it and the counts so
        // far.
        Fixture fixture;
        try {
            buildFamily(fixture, family, tier);
        } catch (const std::exception &error) {
            report["fixtureRefusal"] = QString::fromUtf8(error.what());
            report["fixtureDescription"] = QString::fromStdString(fixture.description);
            report["fixtureBuildSeconds"] = timer.nsecsElapsed() / 1e9;
            report["fixturePeakRssMiB"] = peakMiB();
            report["counts"] = countsOf(fixture.doc);
            report["finishedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
            std::cout << QJsonDocument(report).toJson().toStdString();
            return 3;
        }
        Document &doc = fixture.doc;
        report["fixtureDescription"] = QString::fromStdString(fixture.description);
        report["fixtureBuildSeconds"] = timer.nsecsElapsed() / 1e9;
        report["fixturePeakRssMiB"] = peakMiB();
        report["counts"] = countsOf(doc);
        // Native fixture identity and the model JSON, which a binary format (R116) would replace.
        timer.restart();
        const auto json = encodeDocument(doc, AssetStorage::External);
        const auto encodeSeconds = timer.nsecsElapsed() / 1e9;
        AssetPayloads payloads;
        for (const auto &[id, asset] : doc.assets())
            payloads.emplace(id, asset->payload);
        const auto jsonHash = QCryptographicHash::hash(json, QCryptographicHash::Sha256).toHex();
        const auto counts = report["counts"].toObject();
        const double uniqueFaces = counts["uniqueFaces"].toDouble(),
                     placedFaces = counts["materializedFaces"].toDouble();
        report["modelJson"] =
            QJsonObject{{"bytes", qint64(json.size())},
                        {"bytesPerUniqueFace", uniqueFaces > 0 ? json.size() / uniqueFaces : 0.0},
                        {"bytesPerPlacedFace", placedFaces > 0 ? json.size() / placedFaces : 0.0},
                        {"encodeSeconds", encodeSeconds},
                        {"sha256", QString::fromLatin1(jsonHash)}};
        timer.restart();
        {
            Document parsed = decodeDocument(json, payloads);
            report["modelJsonParseSeconds"] = timer.nsecsElapsed() / 1e9;
            check(parsed.bodies().size() == doc.bodies().size(), "The parsed JSON has every body");
        }
        // Native container: save and open through the production path.
        const auto file = scratch.path() + "/model.sketchyup";
        timer.restart();
        saveDocument(doc, file);
        const auto saveSeconds = timer.nsecsElapsed() / 1e9;
        QJsonObject native{{"saveSeconds", saveSeconds},
                           {"fileBytes", qint64(QFileInfo(file).size())},
                           {"peakRssAfterSaveMiB", peakMiB()}};
        // The working document is the one a user gets by opening the file, so its undo history is
        // empty: the fixture's own build edits do not count against the undo budget of the edits.
        const auto bodiesBefore = doc.bodies().size();
        timer.restart();
        doc = loadDocument(file);
        native["openSeconds"] = timer.nsecsElapsed() / 1e9;
        native["peakRssAfterOpenMiB"] = peakMiB();
        native["reopenedBodies"] = qint64(doc.bodies().size());
        native["historyBytesAfterOpen"] = qint64(doc.historyBytes());
        report["native"] = native;
        check(doc.bodies().size() == bodiesBefore && !doc.canUndo(),
              "The reopened document has every body and an empty undo history");
        check(fixture.body ? doc.bodies().count(fixture.body) == 1
                           : doc.bodies().count(fixture.instances.front()) == 1,
              "The reopened document keeps the fixture's ids");
        QFile::remove(file);
        // GLB export of the whole visible model, when the scaled caps allow it.
        timer.restart();
        try {
            const auto snapshot = RenderSnapshot::capture(doc);
            const auto scene = exportGlb(snapshot);
            report["glb"] = QJsonObject{{"exported", true},
                                        {"seconds", timer.nsecsElapsed() / 1e9},
                                        {"bytes", qint64(scene.glb.size())},
                                        {"peakRssMiB", peakMiB()}};
        } catch (const std::exception &error) {
            report["glb"] = QJsonObject{{"exported", false},
                                        {"seconds", timer.nsecsElapsed() / 1e9},
                                        {"refusal", QString::fromUtf8(error.what())},
                                        {"peakRssMiB", peakMiB()}};
        }
        // Viewport at 1920 x 1080. Warm-up, then frames, picks, inference and edits.
        QWidget host;
        host.setWindowTitle("SketchyUp limits benchmark");
        host.resize(1920, 1080);
        TimedViewport view(doc, &host);
        // A viewport that cannot paint says why by signal, and skips the frame: print the reason.
        QObject::connect(&view, &Viewport::message, &app, [](const QString &text) {
            std::cerr << "viewport: " << text.toStdString() << '\n';
        });
        view.setPlacementMeshSharing(true);
        view.resize(1920, 1080);
        view.setSynchronousFrameTiming(true);
        QElapsedTimer textureReady;
        textureReady.start();
        if (qEnvironmentVariable("SKETCHYUP_BENCHMARK_FULLSCREEN") == "1")
            host.showFullScreen();
        else
            host.show();
        check(QTest::qWaitForWindowExposed(&host), "Benchmark viewport exposed");
        bool sized{};
        QSize observed;
        for (int attempt = 0; attempt < 100 && !sized; ++attempt) {
            view.setFixedSize(qRound(1920 / view.devicePixelRatioF()),
                              qRound(1080 / view.devicePixelRatioF()));
            QTest::qWait(25);
            observed = view.grabFramebuffer().size();
            sized = observed == QSize(1920, 1080);
        }
        check(sized, "Benchmark framebuffer is 1920 by 1080");
        view.fit();
        QCoreApplication::processEvents();
        auto frame = [&] {
            QCoreApplication::processEvents();
            const auto before = view.renderStats().frames;
            const auto image = view.grabFramebuffer();
            check(!image.isNull() && view.renderStats().frames > before, "Fresh benchmark frame");
            return image.size();
        };
        if (fixture.textureAssets > 0) {
            // Textures decode off the document thread; wait for them before measuring.
            frame();
            while (view.texturesPending() && textureReady.elapsed() < 900000)
                QTest::qWait(20);
            frame();
            report["texturesReadyAfterShowSeconds"] = textureReady.nsecsElapsed() / 1e9;
            // R118: a reduced texture is shown and measured, with its sizes; a placeholder is not a
            // textured measurement, so the case is refused instead of recorded.
            QJsonArray sizes;
            size_t placeholders{}, reduced{};
            for (const auto &[shown, stored] : view.textureSizes()) {
                placeholders += shown.isEmpty();
                reduced += !shown.isEmpty() && shown != stored;
                sizes.append(QJsonObject{
                    {"stored", QString("%1 x %2").arg(stored.width()).arg(stored.height())},
                    {"shown", QString("%1 x %2").arg(shown.width()).arg(shown.height())}});
            }
            report["textureSummary"] = view.textureSummary();
            report["textures"] = QJsonObject{{"requested", qint64(sizes.size())},
                                             {"placeholders", qint64(placeholders)},
                                             {"reduced", qint64(reduced)},
                                             {"sizes", sizes}};
            check(placeholders == 0, "Textures unavailable: " + view.textureSummary().toStdString());
            // R118: an export shows the textures it draws, so a reduced set is refused rather than exported.
            try {
                const auto exported = view.renderRaster(QSize(64, 64));
                report["rasterExport"] = QString("exported %1 x %2").arg(exported.width()).arg(exported.height());
            } catch (const std::exception &error) {
                report["rasterExport"] = QString("refused: %1").arg(QString::fromUtf8(error.what()));
            }
        }
        for (int i = 0; i < 10; ++i)
            frame();
        std::vector<double> completeFrames, picks, inferenceQueries;
        const auto &instances = fixture.instances;
        const Id target = fixture.body ? fixture.body : instances.front();
        // Picks land on the target's own geometry, as the benchmark does.
        for (int i = 0; i < 100; ++i) {
            frame();
            completeFrames.push_back(view.completeFrameMs());
            const auto point = view.project(doc.worldTransform(target).point({0, 0, 1}));
            timer.restart();
            const auto hit = view.pick(point);
            picks.push_back(timer.nsecsElapsed() / 1e6);
            check(hit.first != 0, "A pick finds the target's geometry");
        }
        report["frameCompleteMs"] = summary(completeFrames);
        report["pickMs"] = summary(picks);
        report["graphics"] = view.graphicsDescription();
        report["geometryCacheMemory"] = QJsonObject{
            {"bodyVectorCapacityBytes", qint64(view.geometryCacheMemory().bodyVectorCapacityBytes)},
            {"bodyGpuPayloadBytes", qint64(view.geometryCacheMemory().bodyGpuPayloadBytes)}};
        report["drmMemoryAfterFrames"] = benchmark::processDrmMemory();
        // Inference: the production geometry index over every body and placement.
        InferenceIndex inference;
        timer.restart();
        inference.sync(doc);
        report["inferenceInitialBuildSeconds"] = timer.nsecsElapsed() / 1e9;
        for (int i = 0; i < 40; ++i) {
            const auto probe = doc.worldTransform(target).point({0, 0, 1});
            const auto point = view.project(probe);
            const InferenceQuery query{view.inferenceCamera(), point.x(), point.y(), 8};
            timer.restart();
            const auto result = inference.query(query);
            inferenceQueries.push_back(timer.nsecsElapsed() / 1e6);
            check(result.candidates.size() <= 32 || result.truncated,
                  "Inference returns a bounded list");
        }
        report["inferenceQueryMs"] = summary(inferenceQueries);
        // Edits. Move: the placement (component families) or the body. Deform: a push-pull on one
        // body, or a push-pull on the definition's root, which every placement of it regenerates.
        const bool component = fixture.definition != 0;
        auto movedBase = doc.bodies().at(target)->transform;
        const auto movedParent = doc.bodies().at(target)->parent;
        // One edit kind. Each sample edits the same base state, then undoes and redoes that edit,
        // then undoes it again. So one edit is in history at a time, its charge against the 192 MiB
        // undo budget is measured per edit, and the next edit clears the redo entry. A refused edit
        // stops the kind and is recorded with its message.
        auto runEdits = [&](const char *label, bool deform) {
            std::vector<double> feedback, readback, inferenceUpdate, undo, redo, historyCharge;
            std::string refusal;
            int done = 0;
            for (int i = 0; i < samples; ++i) {
                timer.restart();
                const QElapsedTimer started = timer;
                try {
                    if (!deform) {
                        doc.transform(target,
                                      Transform::translation({0, 0, .001 * (i % 2 ? -1 : 1)}) *
                                          movedBase,
                                      movedParent);
                    } else if (component) {
                        // The definition's root is a group; its geometry is the member body with
                        // faces. Moving the member changes the definition, so every placement
                        // regenerates.
                        const auto definition = doc.definitions().at(fixture.definition);
                        Id member = 0;
                        for (const auto &[id, body] : definition->members)
                            if (member == 0 && !body->surface.faces.empty())
                                member = id;
                        check(member != 0, "The definition has a member with faces");
                        const auto memberBody = definition->members.at(member);
                        const Transform base = memberBody->transform;
                        const Id parent = memberBody->parent;
                        editComponentDefinition(doc, fixture.definition, [&](Document &draft) {
                            draft.transform(
                                member,
                                Transform::translation({0, 0, .001 * (i % 2 ? -1 : 1)}) * base,
                                parent);
                            return ChangeReport{};
                        });
                    } else {
                        // A push-pull renews face identities, so the face is read for each edit. It
                        // grows the solid by 1 cm: a reverse push would intersect the solid.
                        const Id face = doc.bodies().at(target)->surface.faces.begin()->first;
                        doc.pushPull(target, face, .01);
                    }
                } catch (const std::exception &error) {
                    refusal = error.what();
                    break;
                }
                historyCharge.push_back(double(doc.historyBytes()));
                view.beginEditFrame(started);
                view.refresh();
                frame();
                readback.push_back(started.nsecsElapsed() / 1e6);
                feedback.push_back(view.editFrameMs());
                const auto t0 = nowSeconds();
                inference.sync(doc);
                inferenceUpdate.push_back((nowSeconds() - t0) * 1e3);
                ++done;
                check(doc.canUndo(), "The edit is in history");
                timer.restart();
                const QElapsedTimer undoStarted = timer;
                doc.undo();
                view.beginEditFrame(undoStarted);
                view.refresh();
                frame();
                undo.push_back(view.editFrameMs());
                inference.sync(doc);
                check(doc.canRedo(), "The undone edit is redoable");
                timer.restart();
                const QElapsedTimer redoStarted = timer;
                doc.redo();
                view.beginEditFrame(redoStarted);
                view.refresh();
                frame();
                redo.push_back(view.editFrameMs());
                inference.sync(doc);
                doc.undo(); // Back to the base state; the next edit clears the redo entry.
                view.refresh();
                inference.sync(doc);
            }
            QJsonObject result{{"edits", done},
                               {"requested", samples},
                               {"editFeedbackMs", summary(feedback)},
                               {"editAndReadbackMs", summary(readback)},
                               {"inferenceUpdateMs", summary(inferenceUpdate)},
                               {"undoMs", summary(undo)},
                               {"redoMs", summary(redo)},
                               {"historyChargeBytes", summary(historyCharge)}};
            if (!refusal.empty())
                result["refusal"] = QString::fromStdString(refusal);
            report[label] = result;
        };
        runEdits("editMove", false);
        runEdits("editDeform", true);
        report["renderStats"] =
            QJsonObject{{"bodyMeshBuilds", qint64(view.renderStats().bodyMeshBuilds)},
                        {"tessellations", qint64(view.renderStats().tessellations)}};
        check(view.renderStats().glError == 0, "The benchmark leaves no OpenGL error");
        report["finalPeakRssMiB"] = peakMiB();
        report["finishedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        std::cout << QJsonDocument(report).toJson().toStdString();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
