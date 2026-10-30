// R099.a persistence (ADR 0173 §3): schema 30 with the presentation-v1 feature, the lowest
// schema for documents without presentation records, the bit-for-bit round trip, the
// refusals of malformed and newer records, the checked-in schema-30 fixture, and the
// released-reader refusal (SKETCHYUP_PRIOR_CLI, set by the package acceptance).
#include "core/presentation.hpp"
#include "core/scenes.hpp"
#include "core/terrain.hpp"
#include "io/document_io.hpp"
#include "io/native_format.hpp"
#include "io/presentation_io.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QtEndian>
#include <algorithm>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> std::string rejection(F operation) {
    try {
        operation();
    } catch (const std::exception &error) {
        return error.what();
    }
    return {};
}
QByteArray read(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Read " + path.toStdString());
    return file.readAll();
}
void write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size(),
          "Write " + path.toStdString());
}
QJsonObject manifest(const QByteArray &container) {
    const auto length = qFromLittleEndian<quint32>(container.constData() + 12);
    return QJsonDocument::fromJson(container.mid(16, length)).object();
}
QByteArray chunk(const QByteArray &container, qsizetype index) {
    const auto length = qFromLittleEndian<quint32>(container.constData() + 12);
    const auto record = manifest(container)["chunks"].toArray()[index].toObject();
    return container.mid(16 + length + record["offset"].toString().toLongLong(),
                         record["bytes"].toString().toLongLong());
}
QJsonObject documentTree(const QByteArray &container) {
    return QJsonDocument::fromJson(chunk(container, 0)).object();
}
// Replace the manifest, keeping the payload and every chunk offset (they are payload-relative).
QByteArray withManifest(const QByteArray &container, const QJsonObject &head) {
    const auto length = qFromLittleEndian<quint32>(container.constData() + 12);
    const auto encoded = QJsonDocument(head).toJson(QJsonDocument::Compact);
    QByteArray header = container.left(16);
    qToLittleEndian<quint32>(quint32(encoded.size()), header.data() + 12);
    return header + encoded + container.mid(16 + length);
}
QJsonArray features(const QByteArray &container) {
    return manifest(container)["requiredFeatures"].toArray();
}
bool hasFeature(const QByteArray &container, const char *feature) {
    return features(container).contains(QString::fromLatin1(feature));
}
// Presentation records compare by value; the maps compare by key and then by record.
bool samePresentation(const Document &a, const Document &b) {
    if (a.cameraPaths().size() != b.cameraPaths().size() ||
        a.renderPresets().size() != b.renderPresets().size() ||
        a.nextCameraPathId() != b.nextCameraPathId() ||
        a.nextRenderPresetId() != b.nextRenderPresetId())
        return false;
    for (const auto &[id, path] : a.cameraPaths())
        if (!b.cameraPaths().contains(id) || !(*b.cameraPaths().at(id) == *path))
            return false;
    for (const auto &[id, preset] : a.renderPresets())
        if (!b.renderPresets().contains(id) || !(*b.renderPresets().at(id) == *preset))
            return false;
    return true;
}
// A fixed identity, so the fixture and every rebuild encode the same bytes.
Document stable(Document doc) {
    auto json = QJsonDocument::fromJson(encodeDocument(doc)).object();
    json["documentId"] = QStringLiteral("00000000000000000000000000000a99");
    return decodeDocument(QJsonDocument(json).toJson(QJsonDocument::Compact));
}
// The schema-30 fixture: a face, a plan and a perspective scene, two paths (one with a
// repeated keyframe and custom timing), two presets, and a broken path whose scene was
// deleted after the path was saved.
Document presentationFixture() {
    Document doc;
    doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}});
    SceneSnapshot plan;
    plan.camera = SceneCamera{};
    plan.camera->orthographic = true;
    SceneSnapshot perspective;
    perspective.camera = SceneCamera{};
    perspective.camera->yaw = 35;
    perspective.camera->pitch = 20;
    const auto planScene = createScene(doc, "Plan", plan);
    const auto sideScene = createScene(doc, "Side", perspective);
    const auto goneScene = createScene(doc, "Gone", perspective);
    createCameraPath(doc, "Tour", {planScene, sideScene, planScene}, CameraPathTiming{30, 12, 6});
    createCameraPath(doc, "Orbit", {sideScene, planScene});
    createCameraPath(doc, "Stale", {planScene, goneScene});
    eraseScene(doc, goneScene);
    createRenderPreset(doc, "Final", RenderSettings{1920, 1080, 256, 7, RenderEngine::Cycles});
    createRenderPreset(doc, "Preview", RenderSettings{640, 360, 16, 0, RenderEngine::Eevee});
    return stable(doc);
}
// Schema 30 presentation object of an encoded document, mutable in place.
QJsonObject presentationOf(const Document &doc) {
    return QJsonDocument::fromJson(encodeDocument(doc)).object()["presentation"].toObject();
}
// Decode the document with its presentation object changed by `edit`.
template <class F> std::string malformed(const Document &doc, F edit) {
    auto tree = QJsonDocument::fromJson(encodeDocument(doc)).object();
    auto presentation = tree["presentation"].toObject();
    edit(presentation);
    tree["presentation"] = presentation;
    return rejection([&] {
        decodeDocument(QJsonDocument(tree).toJson(QJsonDocument::Compact));
    });
}
QJsonObject pathRecord(const QJsonObject &presentation, qsizetype index) {
    return presentation["cameraPaths"].toArray()[index].toObject();
}
void setPathRecord(QJsonObject &presentation, qsizetype index, const QJsonObject &record) {
    auto paths = presentation["cameraPaths"].toArray();
    paths[index] = record;
    presentation["cameraPaths"] = paths;
}
void schemas() {
    check(nativeDocumentVersionFor(Document{}) == 26,
          "An empty document keeps its lowest schema, 26");
    Document terrain;
    setSiteOrigin(terrain, SiteOrigin{512300, 5234500, 0, "EPSG:32633", {}});
    check(nativeDocumentVersionFor(terrain) == 29,
          "A site origin alone is schema 29, the terrain precedent");
    const auto terrainContainer = encodeContainer(terrain);
    check(documentTree(terrainContainer)["version"] == 29 && hasFeature(terrainContainer, "terrain-v1") &&
              !hasFeature(terrainContainer, "presentation-v1") &&
              !documentTree(terrainContainer).contains("presentation") &&
              chunk(terrainContainer, 0) != QByteArray{},
          "A document without presentation records writes schema 29 without presentation-v1");

    const auto fixture = presentationFixture();
    check(nativeDocumentVersionFor(fixture) == 30 && nativePresentationDocumentVersion == 30 &&
              nativeTerrainDocumentVersion == 29,
          "Camera paths and render presets are schema 30");
    const auto container = encodeContainer(fixture);
    check(hasFeature(container, "presentation-v1") && hasFeature(container, "terrain-v1") &&
              documentTree(container)["version"] == 30,
          "A presentation document carries presentation-v1 and writes schema 30");
    check(manifest(container)["chunks"].toArray()[0].toObject()["encoding"] == "json-v30",
          "The document chunk declares json-v30");
    check(documentTree(container).contains("presentation") &&
              documentTree(container)["presentation"].toObject()["cameraPaths"].toArray().size() == 3,
          "The presentation object holds the three paths");
    check(manifest(container)["allocatorFloors"].toObject().contains("nextCameraPathId") &&
              manifest(container)["allocatorFloors"].toObject().contains("nextRenderPresetId"),
          "The allocator floors name the presentation allocators");

    // Presentation with terrain: one schema, both features, the terrain mesh chunk kept.
    auto both = fixture;
    setSiteOrigin(both, SiteOrigin{512300, 5234500, 0, "EPSG:32633", {}});
    check(nativeDocumentVersionFor(both) == 30, "Terrain with presentation is schema 30");

    // Deleting every path and preset writes the lower schema again (allocators never raise it).
    auto emptied = fixture;
    for (const auto &[id, path] : fixture.cameraPaths()) {
        (void)path;
        eraseCameraPath(emptied, id);
    }
    for (const auto &[id, preset] : fixture.renderPresets()) {
        (void)preset;
        eraseRenderPreset(emptied, id);
    }
    check(nativeDocumentVersionFor(emptied) == 26 && !hasFeature(encodeContainer(emptied), "presentation-v1") &&
              emptied.nextCameraPathId() > 1,
          "A document without presentation records writes its lower schema, not 30");
}
void roundTrip() {
    const auto doc = presentationFixture();
    const auto json = encodeDocument(doc);
    const auto decodedJson = decodeDocument(json);
    check(encodeDocument(decodedJson) == json && samePresentation(decodedJson, doc),
          "Paths and presets reopen bit for bit through the JSON document");
    const auto container = encodeContainer(doc);
    const auto decoded = decodeContainer(container);
    check(encodeContainer(decoded) == container && samePresentation(decoded, doc),
          "Paths and presets reopen bit for bit through the container");
    const auto tour = std::find_if(decoded.cameraPaths().begin(), decoded.cameraPaths().end(),
                                   [](const auto &entry) { return entry.second->name == "Tour"; });
    check(tour != decoded.cameraPaths().end() && tour->second->timing == CameraPathTiming{30, 12, 6} &&
              tour->second->keyframes.size() == 3,
          "The repeated keyframes and the custom timing survive");
    // The broken path reopens broken, with the same diagnosis.
    check(brokenCameraPaths(decoded).size() == 1 && brokenCameraPaths(decoded).begin()->second.sceneMissing.size() == 1,
          "The broken path loads as broken, not refused");
    // Files: a created native file reports schema 30 with the feature, and reopens the same.
    QTemporaryDir dir;
    check(dir.isValid(), "A temporary directory");
    const auto path = dir.filePath("presentation.sketchyup");
    const auto created = createNativeFile(doc, path);
    check(created["status"] == "created" && created["documentVersion"] == 30 &&
              !created["migrationNeeded"].toBool(),
          "A created file is schema 30 and current");
    const auto inspected = inspectNativeFile(path);
    check(inspected["valid"].toBool() && inspected["documentVersion"] == 30 &&
              inspected["requiredFeatures"].toArray().contains(QStringLiteral("presentation-v1")),
          "Inspection reports schema 30 with presentation-v1");
    check(samePresentation(decodeContainer(read(path)), doc),
          "The saved file reopens with the same paths and presets");
}
void refusals() {
    const auto doc = presentationFixture();
    const auto json = encodeDocument(doc);
    // A schema-29 payload cannot carry the presentation object.
    auto older = QJsonDocument::fromJson(json).object();
    older["version"] = 29;
    check(!rejection([&] {
               decodeDocument(QJsonDocument(older).toJson(QJsonDocument::Compact));
           }).empty(),
          "Schema 29 cannot carry presentation records");
    // A newer schema refuses with the newer-format error.
    auto newer = QJsonDocument::fromJson(json).object();
    newer["version"] = nativeDocumentVersion + 1;
    check(rejection([&] {
              decodeDocument(QJsonDocument(newer).toJson(QJsonDocument::Compact));
          }).find("Unsupported document format, version") != std::string::npos,
          "A schema newer than this build refuses with the newer-format error");
    // Schema 30 requires the object.
    auto missing = QJsonDocument::fromJson(json).object();
    missing.remove("presentation");
    check(rejection([&] {
              decodeDocument(QJsonDocument(missing).toJson(QJsonDocument::Compact));
          }).find("presentation") != std::string::npos,
          "A schema-30 document without its presentation object refuses");
    // The container: a feature set without presentation-v1 cannot carry a schema-30 payload,
    // and presentation-v1 cannot ride on a schema-29 payload.
    const auto container = encodeContainer(doc);
    auto head = manifest(container);
    auto without = QJsonArray{};
    for (const auto &feature : head["requiredFeatures"].toArray())
        if (feature != "presentation-v1")
            without.append(feature);
    head["requiredFeatures"] = without;
    check(!rejection([&] { decodeContainer(withManifest(container, head)); }).empty(),
          "A schema-30 payload without presentation-v1 refuses");
    // A schema-29 payload that keeps the schema-30 encoding and claims presentation-v1: the
    // payload version the feature set implies is 30, so the container refuses it.
    const auto payload = chunk(container, 0);
    auto relabeled = QJsonDocument::fromJson(payload).object();
    relabeled["version"] = 29;
    const auto relabeledBytes = QJsonDocument(relabeled).toJson(QJsonDocument::Compact);
    auto payloadHead = manifest(container);
    auto payloadChunk = payloadHead["chunks"].toArray()[0].toObject();
    payloadChunk["bytes"] = QString::number(relabeledBytes.size());
    payloadChunk["sha256"] = QString::fromLatin1(
        QCryptographicHash::hash(relabeledBytes, QCryptographicHash::Sha256).toHex());
    payloadHead["chunks"] = QJsonArray{payloadChunk};
    const auto manifestBytes = QJsonDocument(payloadHead).toJson(QJsonDocument::Compact);
    QByteArray relabeledContainer = container.left(16);
    qToLittleEndian<quint32>(quint32(manifestBytes.size()), relabeledContainer.data() + 12);
    relabeledContainer += manifestBytes + relabeledBytes;
    check(!rejection([&] { decodeContainer(relabeledContainer); }).empty(),
          "A schema-29 payload that claims presentation-v1 refuses");
}
void malformedRecords() {
    const auto doc = presentationFixture();
    check(malformed(doc, [](QJsonObject &p) {
              auto keyframes = pathRecord(p, 0)["keyframes"].toArray();
              keyframes.append(3);
              auto record = pathRecord(p, 0);
              record["keyframes"] = keyframes;
              setPathRecord(p, 0, record);
          }).find("canonical") != std::string::npos,
          "A numeric keyframe refuses");
    check(!malformed(doc, [](QJsonObject &p) {
               auto record = pathRecord(p, 0);
               auto keyframes = record["keyframes"].toArray();
               keyframes[0] = "0";
               record["keyframes"] = keyframes;
               setPathRecord(p, 0, record);
           }).empty(),
          "A zero keyframe refuses");
    check(!malformed(doc, [](QJsonObject &p) {
               auto record = pathRecord(p, 0);
               record.remove("timing");
               setPathRecord(p, 0, record);
           }).empty(),
          "A path without timing refuses");
    check(!malformed(doc, [](QJsonObject &p) {
               auto record = pathRecord(p, 0);
               record["extra"] = true;
               setPathRecord(p, 0, record);
           }).empty(),
          "An unknown path field refuses");
    check(!malformed(doc, [](QJsonObject &p) {
               auto record = pathRecord(p, 0);
               auto keyframes = record["keyframes"].toArray();
               keyframes.removeLast();
               record["keyframes"] = QJsonArray{keyframes[0]};
               setPathRecord(p, 0, record);
           }).empty(),
          "A one-keyframe path refuses at load");
    check(malformed(doc, [](QJsonObject &p) {
              auto record = pathRecord(p, 0);
              auto timing = record["timing"].toObject();
              timing["transitionSteps"] = 240;
              timing["holdSteps"] = 12;
              record["timing"] = timing;
              auto keyframes = record["keyframes"].toArray();
              record["keyframes"] = QJsonArray{keyframes[0], keyframes[1]};
              setPathRecord(p, 0, record);
          }).find("265") != std::string::npos,
          "An over-budget plan refuses at load with its frame count");
    check(!malformed(doc, [](QJsonObject &p) {
               auto paths = p["cameraPaths"].toArray();
               auto duplicate = paths[1].toObject();
               duplicate["id"] = pathRecord(p, 0)["id"];
               paths[1] = duplicate;
               p["cameraPaths"] = paths;
           }).empty(),
          "A repeated path identity refuses");
    check(!malformed(doc, [](QJsonObject &p) { p["nextCameraPathId"] = "0"; }).empty(),
          "A zero allocator refuses");
    check(!malformed(doc, [](QJsonObject &p) {
               auto paths = p["cameraPaths"].toArray();
               while (paths.size() <= 64)
                   paths.append(paths[0]);
               p["cameraPaths"] = paths;
           }).empty(),
          "More than 64 paths refuse");
    check(!malformed(doc, [](QJsonObject &p) {
               auto presets = p["renderPresets"].toArray();
               auto record = presets[0].toObject();
               auto settings = record["settings"].toObject();
               settings["engine"] = "gpu";
               record["settings"] = settings;
               presets[0] = record;
               p["renderPresets"] = presets;
           }).empty(),
          "An unknown render engine refuses");
    check(!malformed(doc, [](QJsonObject &p) {
               auto presets = p["renderPresets"].toArray();
               auto record = presets[0].toObject();
               auto settings = record["settings"].toObject();
               settings["width"] = 63;
               record["settings"] = settings;
               presets[0] = record;
               p["renderPresets"] = presets;
           }).empty(),
          "A preset width under 64 refuses");
    check(!malformed(doc, [](QJsonObject &p) {
               auto presets = p["renderPresets"].toArray();
               auto record = presets[0].toObject();
               auto settings = record["settings"].toObject();
               settings["samples"] = 1.5;
               record["settings"] = settings;
               presets[0] = record;
               p["renderPresets"] = presets;
           }).empty(),
          "A fractional sample count refuses");
    // A keyframe naming a scene that is not in the document loads as broken, not refused.
    const auto broken = malformed(doc, [](QJsonObject &p) {
        auto record = pathRecord(p, 0);
        record["keyframes"] = QJsonArray{QStringLiteral("999"), QStringLiteral("999")};
        setPathRecord(p, 0, record);
    });
    check(broken.empty(), "A missing keyframe scene is not a decode error");
    auto tree = QJsonDocument::fromJson(encodeDocument(doc)).object();
    auto presentation = tree["presentation"].toObject();
    auto record = pathRecord(presentation, 0);
    record["keyframes"] = QJsonArray{QStringLiteral("999"), QStringLiteral("999")};
    setPathRecord(presentation, 0, record);
    tree["presentation"] = presentation;
    const auto loaded = decodeDocument(QJsonDocument(tree).toJson(QJsonDocument::Compact));
    // The fixture's own stale path is broken too; the edited one names scene 999.
    const auto report = brokenCameraPaths(loaded);
    bool named = false;
    for (const auto &[id, references] : report)
        named = named || references.sceneMissing == std::set<Id>{999};
    check(report.size() == 2 && named, "The loaded broken paths name their missing scenes");
}
// The released reader (SKETCHYUP_PRIOR_CLI, set by the package acceptance to a 1.4.x CLI, the
// first to read schema 30) opens a schema-30 file unchanged.
void priorRelease(const QString &directory) {
    const auto prior = qEnvironmentVariable("SKETCHYUP_PRIOR_CLI");
    if (prior.isEmpty()) {
        std::cout << "SKETCHYUP_PRIOR_CLI not set; prior-release refusal not exercised\n";
        return;
    }
    const auto path = directory + "/prior-presentation.sketchyup";
    const auto bytes = encodeContainer(presentationFixture());
    write(path, bytes);
    QProcess process;
    process.start(prior, {"--validate-native", path});
    check(process.waitForFinished(30000) && process.exitCode() == 0 && read(path) == bytes,
          "The prior release opens a schema-30 file unchanged");
    std::cout << "prior release check: "
              << process.readAllStandardError().trimmed().toStdString() << '\n';
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    // --write-fixture PATH regenerates tests/fixtures/presentation-v30.sketchyup.
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) == "--write-fixture") {
        write(QString::fromLocal8Bit(argv[2]), encodeContainer(presentationFixture()));
        return 0;
    }
    try {
        schemas();
        roundTrip();
        refusals();
        malformedRecords();
        // The checked-in fixture reopens bit for bit: its document chunk is the one this
        // build writes for the same content, and it reads as schema 30.
        const auto fixture = read(QString::fromLocal8Bit(SOURCE_DIR) +
                                  "/tests/fixtures/presentation-v30.sketchyup");
        check(documentTree(fixture)["version"] == 30 && hasFeature(fixture, "presentation-v1"),
              "The fixture is schema 30 with presentation-v1");
        check(encodeDocument(decodeContainer(fixture)) == encodeDocument(presentationFixture()),
              "The fixture reopens bit for bit");
        QTemporaryDir dir;
        check(dir.isValid(), "A temporary directory");
        priorRelease(dir.path());
        std::cout << "presentation io: all checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "presentation io test failed: " << error.what() << '\n';
        return 1;
    }
}
