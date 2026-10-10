#include "core/components.hpp"
#include "core/entity_measure.hpp"
#include "core/groups.hpp"
#include "core/materials.hpp"
#include "core/tags.hpp"
#include "geometry/guides.hpp"
#include "io/document_io.hpp"
#include "io/native_format.hpp"
#include "io/recovery.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtEndian>
#include <cmath>
#include <functional>
#include <iostream>
#include <numbers>
#include <set>
#include <sys/resource.h>
using namespace sketchy;
namespace {
void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
QByteArray read(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Read fixture");
    return file.readAll();
}
void write(const QString &path, const QByteArray &data) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(data) == data.size(), "Write fixture");
}
int rejectionCase = 0;
std::string rejection(const std::function<void()> &operation) {
    ++rejectionCase;
    try {
        operation();
    } catch (const std::exception &error) {
        if (qEnvironmentVariableIsSet("INSTANCED_STORAGE_VERBOSE"))
            std::cerr << "case " << rejectionCase << ": " << error.what() << '\n';
        return error.what();
    }
    throw std::runtime_error("Invalid instanced storage must be rejected (case " +
                             std::to_string(rejectionCase) + ")");
}
QString fixturePath() { return QString(SOURCE_DIR) + "/tests/fixtures/instances-v24.sketchyup"; }

// Shared, hidden, tagged, locked, renamed, mirrored, nested and make-unique placements.
Document instancedFixture() {
    Document doc;
    const auto paint = createMaterial(doc, "Paint", {.8f, .2f, .1f});
    const auto tag = createTag(doc, "Furniture");
    const auto box = doc.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}}, "Seat");
    doc.extrude(box, doc.bodies().at(box)->surface.faces.begin()->first, .5);
    assignMaterial(doc, box, {}, paint);
    doc.addGuide(box, guidePoint({.5, .5, .5}));
    const auto chair = createComponent(doc, box, "Chair");
    const auto hidden =
        placeComponent(doc, chair.definition, Transform::translation({2, 0, 0}), 0, "Hidden chair");
    setEntityState(doc, hidden.instance, true, {});
    const auto tagged = placeComponent(doc, chair.definition, Transform::translation({4, 0, 0}));
    assignTag(doc, tagged.instance, tag);
    const auto mirrored =
        placeComponent(doc, chair.definition,
                       Transform::translation({6, 0, 0}) * Transform::scaling({-1, 1, 2}), 0,
                       "Mirrored chair");
    setEntityProperties(doc, mirrored.instance, {{"seat", 3.}, {"label", std::string("north")}});
    setEntityState(doc, mirrored.instance, {}, true);
    // A nested assembly: a placed chair plus loose geometry become a second definition.
    const auto inner = placeComponent(doc, chair.definition, Transform::translation({0, 3, 0}));
    const auto top = doc.addFace({{{0, 3, 1}, {2, 3, 1}, {2, 5, 1}, {0, 5, 1}}}, "Table top");
    const auto set = createGroup(doc, {inner.instance, top}, "Dining set");
    const auto dining = createComponent(doc, set, "Dining set");
    placeComponent(doc, dining.definition, Transform::translation({0, 8, 0}), 0, "Second set");
    // Make-unique turns one sibling into its own definition with copied geometry.
    const auto sibling = placeComponent(doc, chair.definition, Transform::translation({8, 0, 0}));
    makeComponentUnique(doc, sibling.instance);
    return doc;
}

// The real_model_benchmark repeated definition: a 26-sided prism, 100 triangles.
Document benchmarkInstances(int count) {
    Document doc;
    std::vector<Vec3> loop;
    for (int i = 0; i < 26; ++i) {
        const auto angle = 2 * std::numbers::pi * i / 26;
        loop.push_back({std::cos(angle), std::sin(angle), 0});
    }
    const auto body = doc.addFace({loop});
    doc.extrude(body, doc.bodies().at(body)->surface.faces.begin()->first, 1);
    const int columns = int(std::ceil(std::sqrt(count)));
    const auto component = createComponent(doc, body, "26-sided benchmark prism");
    for (int i = 1; i < count; ++i)
        placeComponent(doc, component.definition,
                       Transform::translation({4.0 * (i % columns), 4.0 * (i / columns), 0}));
    return doc;
}

// Test-only: replicate the last placement's records in a document JSON tree to `count`
// placements with fresh identities. Never decoded; used only to size files above limits.
QByteArray replicatePlacements(const QByteArray &bytes, int count) {
    auto root = QJsonDocument::fromJson(bytes).object();
    auto bodies = root["bodies"].toArray();
    auto instances = root["instances"].toArray();
    const auto prototype = instances.last().toObject();
    const auto members = prototype["members"].toObject();
    std::map<QString, QJsonObject> records;
    for (const auto &value : bodies) {
        const auto body = value.toObject();
        for (auto it = members.begin(); it != members.end(); ++it)
            if (it.value().toString() == body["id"].toString())
                records[body["id"].toString()] = body;
    }
    auto next = root["nextId"].toString().toULongLong();
    for (int copy = int(instances.size()); copy < count; ++copy) {
        std::map<QString, QString> ids;
        for (auto it = members.begin(); it != members.end(); ++it)
            ids[it.value().toString()] = QString::number(next++);
        auto instance = prototype;
        QJsonObject mapped;
        for (auto it = members.begin(); it != members.end(); ++it)
            mapped[it.key()] = ids.at(it.value().toString());
        instance["members"] = mapped;
        instance["root"] = ids.at(prototype["root"].toString());
        if (prototype.contains("floors")) {
            QJsonObject floors;
            const auto old = prototype["floors"].toObject();
            for (auto it = old.begin(); it != old.end(); ++it)
                floors[ids.at(it.key())] = it.value();
            instance["floors"] = floors;
        }
        instances.append(instance);
        for (auto [id, record] : records) {
            record["id"] = ids.at(id);
            if (ids.contains(record["parent"].toString()))
                record["parent"] = ids.at(record["parent"].toString());
            auto transform = record["transform"].toArray();
            if (id == prototype["root"].toString())
                transform[12] = transform[12].toDouble() + 4.0 * copy;
            record["transform"] = transform;
            bodies.append(record);
        }
    }
    root["bodies"] = bodies;
    root["instances"] = instances;
    root["nextId"] = QString::number(next);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

long peakKiB() {
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    return usage.ru_maxrss;
}

// Separate processes isolate the peak resident set of each phase.
int measureLoad(const QString &path) {
    const auto bytes = read(path);
    QElapsedTimer timer;
    timer.start();
    const auto doc = decodeContainer(bytes);
    QJsonObject report{{"phase", "load"},
                       {"bodies", int(doc.bodies().size())},
                       {"containerBytes", QString::number(bytes.size())},
                       {"decodeMs", timer.elapsed()},
                       {"loadPeakKiB", qint64(peakKiB())}};
    std::cout << QJsonDocument(report).toJson(QJsonDocument::Compact).toStdString() << '\n';
    return 0;
}
int measure(int count, const QString &phase, const QString &output = {}) {
    QElapsedTimer timer;
    timer.start();
    auto doc = benchmarkInstances(count);
    const auto built = peakKiB();
    const auto buildMs = timer.elapsed();
    QJsonObject report{{"count", count},
                       {"phase", phase},
                       {"documentVersion", nativeDocumentVersion},
                       {"bodies", int(doc.bodies().size())},
                       {"buildMs", buildMs},
                       {"buildPeakKiB", qint64(built)}};
    if (phase == "replicate") {
        const auto source = encodeDocument(doc);
        const auto replicated = replicatePlacements(source, 10000);
        report["sourceJsonBytes"] = QString::number(source.size());
        report["replicatedPlacements"] = 10000;
        report["replicatedJsonBytes"] = QString::number(replicated.size());
    } else if (phase != "build") {
        timer.restart();
        auto json = encodeDocument(doc);
        report["encodeMs"] = timer.elapsed();
        report["jsonBytes"] = QString::number(json.size());
        const auto container = encodeContainer(doc);
        report["containerBytes"] = QString::number(container.size());
        if (!output.isEmpty())
            write(output, container);
        report["encodePeakKiB"] = qint64(peakKiB());
        if (phase == "decode") {
            doc = Document{};
            timer.restart();
            auto reopened = decodeContainer(container);
            report["decodeMs"] = timer.elapsed();
            report["decodePeakKiB"] = qint64(peakKiB());
            check(reopened.bodies().size() == size_t(count) * 2, "Benchmark reopens every member");
        }
    }
    std::cout << QJsonDocument(report).toJson(QJsonDocument::Compact).toStdString() << '\n';
    return 0;
}

QByteArray repack(const QByteArray &container, const QByteArray &document,
                  const std::function<void(QJsonObject &)> &manifestChange = {}) {
    const auto length = qFromLittleEndian<quint32>(container.constData() + 12);
    auto manifest = QJsonDocument::fromJson(container.mid(16, length)).object();
    auto chunks = manifest["chunks"].toArray();
    const auto old = chunks[0].toObject()["bytes"].toString().toLongLong();
    auto chunk = chunks[0].toObject();
    chunk["bytes"] = QString::number(document.size());
    chunk["sha256"] = QString::fromLatin1(
        QCryptographicHash::hash(document, QCryptographicHash::Sha256).toHex());
    chunks[0] = chunk;
    manifest["chunks"] = chunks;
    check(chunks.size() == 1, "Instanced fixtures have no asset chunks");
    if (manifestChange)
        manifestChange(manifest);
    const auto encoded = QJsonDocument(manifest).toJson(QJsonDocument::Compact);
    auto header = container.first(16);
    qToLittleEndian<quint32>(encoded.size(), header.data() + 12);
    return header + encoded + document + container.mid(16 + length + old);
}
QJsonObject documentTree(const QByteArray &container) {
    const auto length = qFromLittleEndian<quint32>(container.constData() + 12);
    const auto manifest = QJsonDocument::fromJson(container.mid(16, length)).object();
    const auto bytes = manifest["chunks"].toArray()[0].toObject()["bytes"].toString().toLongLong();
    return QJsonDocument::fromJson(container.mid(16 + length, bytes)).object();
}
QByteArray compact(const QJsonObject &tree) {
    return QJsonDocument(tree).toJson(QJsonDocument::Compact);
}
// Every serialized record, allocator floor and the history baseline must agree.
void identical(const Document &a, const Document &b, const std::string &label) {
    check(a.identity() == b.identity() && a.revision() == b.revision() && a.nextId() == b.nextId() &&
              a.nextDefinitionId() == b.nextDefinitionId(),
          label + ": identity, revision and allocators");
    check(a.bodies().size() == b.bodies().size(), label + ": body count");
    for (const auto &[id, body] : a.bodies())
        check(b.bodies().contains(id) && *b.bodies().at(id) == *body &&
                  b.bodies().at(id)->surface.nextId == body->surface.nextId &&
                  b.bodies().at(id)->topology.nextId == body->topology.nextId,
              label + ": body " + std::to_string(id) + " and its allocator floors");
    check(a.instances().size() == b.instances().size(), label + ": instance count");
    for (const auto &[root, instance] : a.instances())
        check(b.instances().contains(root) && *b.instances().at(root) == *instance,
              label + ": member identity map " + std::to_string(root));
    check(encodeBodies(a.bodies()) == encodeBodies(b.bodies()), label + ": expanded body records");
    check(encodeContainer(a) == encodeContainer(b), label + ": canonical container");
    check(!b.dirty() && !b.canUndo() && !b.canRedo(), label + ": clean history baseline");
}
size_t storedMembers(const QJsonObject &tree) {
    std::set<QString> owned;
    for (const auto &value : tree["instances"].toArray()) {
        const auto instance = value.toObject();
        const auto members = instance["members"].toObject();
        for (auto it = members.begin(); it != members.end(); ++it)
            if (it.value() != instance["root"])
                owned.insert(it.value().toString());
    }
    size_t stored = 0;
    for (const auto &value : tree["bodies"].toArray())
        stored += owned.contains(value.toObject()["id"].toString());
    return stored;
}
void roundTrip(const Document &doc, const std::string &label) {
    const auto container = encodeContainer(doc);
    const auto tree = documentTree(container);
    check(tree["version"] == 25 && storedMembers(tree) == 0,
          label + ": schema 25 writes no projected member bodies");
    const auto reopened = decodeContainer(container);
    identical(doc, reopened, label);
    check(encodeContainer(reopened) == container, label + ": byte-exact reopen");
    const auto raw = encodeDocument(doc);
    identical(doc, decodeDocument(raw), label + " (raw JSON)");
}
QString memberBody(const QJsonObject &tree, QString *instanceRoot = nullptr,
                   QString *definition = nullptr) {
    // A geometry member of a shared top-level placement.
    std::set<QString> nested;
    for (const auto &value : tree["instances"].toArray()) {
        const auto members = value.toObject()["members"].toObject();
        for (auto it = members.begin(); it != members.end(); ++it)
            if (it.value() != value.toObject()["root"])
                nested.insert(it.value().toString());
    }
    for (const auto &value : tree["instances"].toArray()) {
        const auto instance = value.toObject();
        if (nested.contains(instance["root"].toString()))
            continue;
        const auto members = instance["members"].toObject();
        for (auto it = members.begin(); it != members.end(); ++it)
            if (it.value() != instance["root"]) {
                for (const auto &body : tree["bodies"].toArray())
                    if (body.toObject()["id"] == it.value() &&
                        !body.toObject()["vertices"].toArray().isEmpty()) {
                        if (instanceRoot)
                            *instanceRoot = instance["root"].toString();
                        if (definition)
                            *definition = instance["definition"].toString();
                        return it.value().toString();
                    }
            }
    }
    throw std::runtime_error("Fixture has a stored geometry member");
}
QJsonObject editBody(QJsonObject tree, const QString &id,
                     const std::function<void(QJsonObject &)> &change) {
    auto bodies = tree["bodies"].toArray();
    for (qsizetype i = 0; i < bodies.size(); ++i)
        if (bodies[i].toObject()["id"] == id) {
            auto body = bodies[i].toObject();
            change(body);
            bodies[i] = body;
        }
    tree["bodies"] = bodies;
    return tree;
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        const auto args = app.arguments();
        if (args.size() == 3 && args[1] == "--write-fixture") {
            auto doc = instancedFixture();
            saveDocument(doc, args[2]);
            return 0;
        }
        if ((args.size() == 4 || args.size() == 5) && args[1] == "--measure")
            return measure(args[2].toInt(), args[3], args.size() == 5 ? args[4] : QString{});
        if (args.size() == 3 && args[1] == "--measure-load")
            return measureLoad(args[2]);
        check(args.size() == 1, "Usage: instanced_storage_tests [--write-fixture PATH | "
                                "--measure COUNT build|encode|decode|replicate [OUT] | --measure-load FILE]");
        QTemporaryDir files;
        check(files.isValid(), "Temporary directory");

        // Round trips: 0, 1 and many placements, nested, hidden, tagged, locked and unique.
        roundTrip(Document{}, "empty");
        {
            Document doc;
            doc.addFace({{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}});
            roundTrip(doc, "no placements");
        }
        roundTrip(benchmarkInstances(1), "one placement");
        const auto fixture = instancedFixture();
        check(fixture.instances().size() >= 9 && fixture.definitions().size() == 3,
              "Fixture has shared, nested and make-unique placements");
        roundTrip(fixture, "mixed placements");
        {
            const auto doc = benchmarkInstances(1000);
            const auto container = encodeContainer(doc);
            roundTrip(doc, "1000 placements");
            // Schema 24 wrote 1,000 x 2 expanded bodies (about 7 MB); a bound well
            // above the measured compact size still proves members are not stored.
            check(container.size() < 1500000, "1000 placements save compactly");
        }

        // Saving through an immutable snapshot and a recovery checkpoint.
        {
            auto doc = instancedFixture();
            const auto path = files.filePath("snapshot.sketchyup");
            const auto captured = captureSave(doc);
            const auto state = doc.readSnapshot();
            doc.undo();
            saveSnapshot(doc, captured, path);
            const auto reopened = loadDocument(path);
            check(encodeContainer(reopened) == captured.bytes(),
                  "Immutable save snapshot reopens byte-exact");
            identical(state, reopened, "save snapshot");
            doc.redo();
            QString key;
            {
                RecoveryWriter writer(files.filePath("recovery"),
                                      QString::fromStdString(doc.identity()));
                writer.write(captureRecovery(doc));
                key = writer.key();
            }
            const auto recovered = readRecovery(files.filePath("recovery"), key);
            check(recovered.verified && recovered.document &&
                      encodeContainer(*recovered.document) == encodeContainer(doc),
                  "Recovery checkpoint restores instanced placements exactly");
            check(recovered.document->dirty(), "Recovered document remains unsaved");
        }

        // Golden v24 file with expanded members migrates exactly.
        const auto golden = read(fixturePath());
        const auto goldenTree = documentTree(golden);
        check(goldenTree["version"] == 24 && storedMembers(goldenTree) > 10,
              "Retained v24 fixture stores expanded members");
        const auto legacy = loadDocument(fixturePath());
        check(QJsonValue(encodeBodies(legacy.bodies())) == goldenTree["bodies"],
              "v24 bodies decode into identical records");
        roundTrip(legacy, "v24 golden");
        {
            const auto output = files.filePath("migrated.sketchyup");
            const auto report = migrateNativeFile(fixturePath(), output);
            check(report["outputDocumentVersion"] == 25 && read(fixturePath()) == golden,
                  "Migration leaves the v24 original unchanged");
            const auto migrated = loadDocument(output);
            identical(legacy, migrated, "migrated v24");
            check(QJsonValue(encodeBodies(migrated.bodies())) == goldenTree["bodies"],
                  "Migrated placements rebuild every v24 member record");
        }

        // A v24 member that differs from its definition is rejected by name.
        {
            QString instance, definition;
            const auto member = memberBody(goldenTree, &instance, &definition);
            const auto corrupt = editBody(goldenTree, member, [](QJsonObject &body) {
                auto vertices = body["vertices"].toArray();
                auto vertex = vertices[0].toArray();
                vertex[1] = vertex[1].toDouble() + .25;
                vertices[0] = vertex;
                body["vertices"] = vertices;
            });
            const auto path = files.filePath("corrupt-v24.sketchyup");
            const auto bytes = repack(golden, compact(corrupt));
            write(path, bytes);
            const auto message = rejection([&] { (void)loadDocument(path); });
            check(message.find("instance " + instance.toStdString()) != std::string::npos &&
                      message.find("definition " + definition.toStdString()) !=
                          std::string::npos &&
                      message.size() < 256,
                  "Bounded diagnostic names the placement and definition: " + message);
            const auto output = files.filePath("corrupt-migrated.sketchyup");
            rejection([&] { (void)migrateNativeFile(path, output); });
            rejection([&] { (void)inspectNativeFile(path); });
            check(read(path) == bytes && !QFile::exists(output),
                  "Rejected migration leaves the original untouched");
            const auto raw = rejection([&] { (void)decodeDocument(compact(corrupt)); });
            check(raw == message, "Raw JSON migration uses the same diagnostic");
            // A scene member floor below its canonical member floor is also a mismatch.
            auto lowered = goldenTree;
            auto definitions = lowered["definitions"].toArray();
            QString canonical;
            for (const auto &value : goldenTree["instances"].toArray())
                if (value.toObject()["root"] == instance) {
                    const auto members = value.toObject()["members"].toObject();
                    for (auto it = members.begin(); it != members.end(); ++it)
                        if (it.value() == member)
                            canonical = it.key();
                }
            for (qsizetype i = 0; i < definitions.size(); ++i) {
                auto record = definitions[i].toObject();
                if (record["id"] != definition)
                    continue;
                auto members = record["members"].toArray();
                for (qsizetype j = 0; j < members.size(); ++j) {
                    auto body = members[j].toObject();
                    if (body["id"] != canonical)
                        continue;
                    body["nextEdgeId"] =
                        QString::number(body["nextEdgeId"].toString().toULongLong() + 5);
                    members[j] = body;
                }
                record["members"] = members;
                definitions[i] = record;
            }
            lowered["definitions"] = definitions;
            check(!canonical.isEmpty() &&
                      rejection([&] { (void)decodeDocument(compact(lowered)); })
                              .find("definition " + definition.toStdString()) != std::string::npos,
                  "Member floor below its definition floor is rejected by name");
        }

        // A raised member allocator floor in v24 survives the compact v25 form.
        {
            const auto member = memberBody(goldenTree);
            const auto raised = editBody(goldenTree, member, [](QJsonObject &body) {
                body["nextId"] = "900";
                body["nextEdgeId"] = "901";
            });
            const auto doc = decodeDocument(compact(raised));
            const auto id = member.toULongLong();
            check(doc.bodies().at(id)->surface.nextId == 900 &&
                      doc.bodies().at(id)->topology.nextId == 901,
                  "Raised member floor decodes");
            roundTrip(doc, "raised member floor");
            const auto tree = QJsonDocument::fromJson(encodeDocument(doc)).object();
            int floors = 0;
            for (const auto &value : tree["instances"].toArray()) {
                const auto record = value.toObject()["floors"].toObject();
                floors += int(record.size());
                if (record.contains(member))
                    check(record[member] == QJsonArray{"900", "901"},
                          "Floor record stores exact surface and edge floors");
            }
            check(floors == 1, "Only the raised member writes a floor record");
        }

        // Strict schema 25 placement records.
        {
            const auto tree = QJsonDocument::fromJson(encodeDocument(fixture)).object();
            check(decodeDocument(compact(tree)).revision() == fixture.revision(),
                  "Unmodified v25 tree decodes");
            auto instances = [&](const std::function<void(QJsonObject &)> &change) {
                auto copy = tree;
                auto records = copy["instances"].toArray();
                auto record = records.last().toObject();
                change(record);
                records[records.size() - 1] = record;
                copy["instances"] = records;
                return compact(copy);
            };
            QString root, member;
            {
                const auto record = tree["instances"].toArray().last().toObject();
                root = record["root"].toString();
                const auto members = record["members"].toObject();
                for (auto it = members.begin(); it != members.end(); ++it)
                    if (it.value().toString() != root)
                        member = it.value().toString();
            }
            rejection([&] { (void)decodeDocument(instances([](auto &r) { r.remove("floors"); })); });
            rejection([&] {
                (void)decodeDocument(instances([](auto &r) { r["floors"] = QJsonArray{}; }));
            });
            rejection([&] {
                (void)decodeDocument(
                    instances([&](auto &r) { r["floors"] = QJsonObject{{root, QJsonArray{"50", "50"}}}; }));
            });
            rejection([&] {
                (void)decodeDocument(instances(
                    [&](auto &r) { r["floors"] = QJsonObject{{member, QJsonArray{"1", "1"}}}; }));
            });
            rejection([&] {
                (void)decodeDocument(instances([&](auto &r) {
                    auto members = r["members"].toObject();
                    members.remove(members.begin().key() == r["root"].toString()
                                       ? std::next(members.begin()).key()
                                       : members.begin().key());
                    r["members"] = members;
                }));
            });
            rejection([&] {
                (void)decodeDocument(instances([&](auto &r) { r["definition"] = "999"; }));
            });
            // A projected member must not also be stored as a body.
            {
                auto copy = tree;
                auto bodies = copy["bodies"].toArray();
                for (const auto &value : encodeBodies(fixture.bodies()))
                    if (value.toObject()["id"] == member)
                        bodies.append(value);
                copy["bodies"] = bodies;
                rejection([&] { (void)decodeDocument(compact(copy)); });
            }
            // An older reader's feature set cannot open a v25 chunk.
            const auto container = encodeContainer(fixture);
            const auto withoutFeature = repack(container, compact(documentTree(container)),
                                               [](QJsonObject &manifest) {
                                                   auto features =
                                                       manifest["requiredFeatures"].toArray();
                                                   check(features.last() == "instanced-placements-v1",
                                                         "Container requires instanced placements");
                                                   features.removeLast();
                                                   manifest["requiredFeatures"] = features;
                                               });
            rejection([&] { (void)decodeContainer(withoutFeature); });
            auto v24 = documentTree(container);
            v24["version"] = 24;
            rejection([&] { (void)decodeContainer(repack(container, compact(v24))); });
        }
        std::cout << "Instanced component storage: schema 25 round trips, v24 golden migration, "
                     "named mismatch rejection and compact placements passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
