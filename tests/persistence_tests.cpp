#include "core/components.hpp"
#include "core/groups.hpp"
#include "core/materials.hpp"
#include "core/tags.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtEndian>
#include <cerrno>
#include <csignal>
#include <functional>
#include <iostream>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
using namespace sketchy;
namespace {
int failSync, syncError = EIO, directorySyncs, killDirectory;
bool failWrite, killFile, failFileSync;
bool directory(int fd) {
    struct stat st{};
    return fstat(fd, &st) == 0 && S_ISDIR(st.st_mode);
}
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> std::string rejects(F f) {
    try {
        f();
    } catch (const std::exception &e) {
        return e.what();
    }
    throw std::runtime_error("Expected rejection");
}
QByteArray read(const QString &path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Read fixture");
    return file.readAll();
}
void write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "Write fixture");
}
QByteArray changeManifest(const QByteArray &bytes,
                          const std::function<void(QJsonObject &)> &change) {
    auto length = qFromLittleEndian<quint32>(bytes.constData() + 12);
    auto object = QJsonDocument::fromJson(bytes.mid(16, length)).object();
    change(object);
    auto manifest = QJsonDocument(object).toJson(QJsonDocument::Compact);
    auto header = bytes.first(16);
    qToLittleEndian<quint32>(manifest.size(), header.data() + 12);
    return header + manifest + bytes.mid(16 + length);
}
} // namespace
// Link-time syscall interposition confines fault injection to this test process.
extern "C" int fsync(int fd) {
    if (directory(fd)) {
        ++directorySyncs;
        if (killDirectory == directorySyncs)
            raise(SIGKILL);
        if (failSync == directorySyncs) {
            errno = syncError;
            return -1;
        }
    } else {
        if (killFile)
            raise(SIGKILL);
        if (failFileSync) {
            errno = EIO;
            return -1;
        }
    }
    return syscall(SYS_fsync, fd);
}
extern "C" ssize_t write(int fd, const void *buffer, size_t size) {
    struct stat st{};
    if (failWrite && fstat(fd, &st) == 0 && S_ISREG(st.st_mode)) {
        errno = ENOSPC;
        return -1;
    }
    return syscall(SYS_write, fd, buffer, size);
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir dir;
        require(dir.isValid(), "Temporary directory");
        {
            Document caller, receiver;
            const auto id = caller.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}}});
            receiver.addFace({{{5, 0, 0}, {6, 0, 0}, {6, 1, 0}, {5, 1, 0}}});
            const auto before = encodeContainer(receiver);
            const auto stamp = receiver.saveStamp();
            const auto historyBytes = receiver.historyBytes();
            for (int invalid = 0; invalid < 3; ++invalid) {
                auto body = std::make_shared<Body>(*caller.bodies().at(id));
                if (invalid == 0)
                    body->topology.edges.erase(body->topology.edges.begin());
                else if (invalid == 1)
                    body->topology.nextId = 1;
                else
                    body->topology.edges.begin()->second.b = 999999;
                rejects(
                    [&] { receiver.restore(caller.identity(), caller.nextId(), {{id, body}}); });
                require(
                    encodeContainer(receiver) == before && receiver.isCurrentSnapshot(stamp) &&
                        receiver.canUndo() && !receiver.canRedo() &&
                        receiver.historyBytes() == historyBytes,
                    "Public restore rejects invalid topology without changing records or history");
            }
            receiver.undo();
            require(receiver.bodies().empty(),
                    "Rejected restore preserves the existing undo action");
        }
        for (const auto &name :
             {"container-scene-v2.sketchyup", "raw-scene-v2.json", "raw-scene-v1.json"}) {
            const auto fixture = read(QString(SKETCHYUP_TEST_FIXTURES) + "/" + name);
            auto old = decodeContainer(fixture);
            require(old.bodies().size() == 1 &&
                        !old.bodies().begin()->second->topology.edges.empty(),
                    "Historical fixture builds persistent edge IDs");
            const auto roundtrip = decodeContainer(encodeContainer(old));
            require(roundtrip.bodies().begin()->second->topology ==
                        old.bodies().begin()->second->topology,
                    "Migrated edge IDs survive current container");
        }
        for (const auto &name : {"container-topology-v3.sketchyup", "raw-topology-v3.json"}) {
            const auto fixture = read(QString(SKETCHYUP_TEST_FIXTURES) + "/" + name);
            const auto old = decodeContainer(fixture);
            require(old.bodies().size() == 3, "Historical topology fixture contexts");
            const auto migrated = decodeContainer(encodeContainer(old));
            for (const auto &[id, body] : old.bodies()) {
                require(body->curves.empty() && migrated.bodies().at(id)->curves.empty(),
                        "Migration does not invent analytic curves");
                require(*body == *migrated.bodies().at(id), "Version-3 topology IDs preserved");
            }
        }
        for (const auto &name : {"container-curves-v4.sketchyup", "raw-curves-v4.json"}) {
            const auto fixture = read(QString(SKETCHYUP_TEST_FIXTURES) + "/" + name);
            const auto old = decodeContainer(fixture);
            require(old.bodies().size() == 5, "Historical curve fixture contexts");
            const auto migrated = decodeContainer(encodeContainer(old));
            for (const auto &[id, body] : old.bodies()) {
                require(body->guides.empty() && !body->curves.empty(),
                        "Version-4 migration preserves curves without inventing guides");
                require(*body == *migrated.bodies().at(id), "Version-4 records survive migration");
            }
        }
        Document guided;
        guided.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}}});
        guided.addGuide(1, guideLine({0, 0, .9}, {1, 0, 0}));
        guided.addGuide(1, guidePoint({1, 0, .9}));
        const auto guideBytes = encodeContainer(guided);
        require(encodeContainer(decodeContainer(guideBytes)) == guideBytes,
                "Guides exact container roundtrip");
        auto guideReopened = decodeContainer(guideBytes);
        const auto guideFloor = guideReopened.bodies().at(1)->surface.nextId;
        guideReopened.clearGuides();
        guideReopened.addGuide(1, guidePoint({2, 0, .9}));
        require(guideReopened.bodies().at(1)->guides.begin()->first >= guideFloor,
                "Guide allocator floor survives reopen and cleanup");
        auto corruptGuide = [&](auto mutate) {
            auto root = QJsonDocument::fromJson(encodeDocument(guided)).object();
            auto bodies = root["bodies"].toArray();
            auto body = bodies[0].toObject();
            auto guides = body["guides"].toArray();
            auto guide = guides[0].toObject();
            mutate(guide);
            guides[0] = guide;
            body["guides"] = guides;
            bodies[0] = body;
            root["bodies"] = bodies;
            rejects([&] { decodeDocument(QJsonDocument(root).toJson()); });
        };
        corruptGuide([](auto &g) { g["direction"] = QJsonArray{2, 0, 0}; });
        corruptGuide([](auto &g) { g["direction"] = QJsonArray{0, 0, 0}; });
        corruptGuide([](auto &g) { g["origin"] = QJsonArray{1, 2}; });
        corruptGuide([](auto &g) { g["origin"] = QJsonArray{1000001, 0, 0}; });
        corruptGuide([](auto &g) { g["id"] = "1"; });
        corruptGuide([](auto &g) { g["id"] = "7"; });
        corruptGuide([](auto &g) { g["id"] = "999999"; });
        corruptGuide([](auto &g) { g["kind"] = "ray"; });
        corruptGuide([](auto &g) { g["kind"] = "point"; });
        corruptGuide([](auto &g) { g["unrecognized"] = true; });
        Document curved;
        curved.addCurve(0, centerCurve(CurveKind::Circle, {}, 2, 0, 2 * std::acos(-1), 24));
        curved.addCurve(0, twoPointArc({3, 0, 0}, {5, 0, 0}, {0, 0, 1}, .5, 12));
        const auto curvedBytes = encodeContainer(curved);
        require(encodeContainer(decodeContainer(curvedBytes)) == curvedBytes,
                "Curves exact container roundtrip");
        auto corruptCurve = [&](auto mutate) {
            auto root = QJsonDocument::fromJson(encodeDocument(curved)).object();
            auto bodies = root["bodies"].toArray();
            auto body = bodies[0].toObject();
            auto curves = body["curves"].toArray();
            auto curve = curves[0].toObject();
            mutate(curve);
            curves[0] = curve;
            body["curves"] = curves;
            bodies[0] = body;
            root["bodies"] = bodies;
            try {
                decodeDocument(QJsonDocument(root).toJson());
            } catch (const std::exception &) {
                return;
            }
            throw std::runtime_error("Malformed curve accepted");
        };
        corruptCurve([](auto &c) { c["radius"] = 3; });
        corruptCurve([](auto &c) { c["id"] = "1"; });
        corruptCurve([](auto &c) { c["segments"] = 1.5; });
        corruptCurve([](auto &c) { c["edges"] = QJsonArray{}; });
        corruptCurve([](auto &c) { c["kind"] = "spline"; });
        const auto v6 = decodeContainer(
            read(QString(SKETCHYUP_TEST_FIXTURES) + "/container-groups-v6.sketchyup"));
        for (const auto &[id, body] : v6.bodies())
            require(body->faceColors.empty(),
                    "Version-6 migration preserves inherited body colors");
        const auto v7 = decodeContainer(
            read(QString(SKETCHYUP_TEST_FIXTURES) + "/container-face-colors-v7.sketchyup"));
        require(v7.definitions().empty() && v7.instances().empty() &&
                    !v7.bodies().at(1)->faceColors.empty(),
                "Version-7 migration retains face colors without inventing components");
        const auto v8 = decodeContainer(
            read(QString(SKETCHYUP_TEST_FIXTURES) + "/container-components-v8.sketchyup"));
        require(!v8.definitions().empty() && !v8.instances().empty() && v8.tags().empty(),
                "Version-8 migration preserves components with Untagged organization");
        for (const auto &[id, body] : v8.bodies())
            require(!body->tag, "Historical components start Untagged");
        const auto v9 = decodeContainer(
            read(QString(SKETCHYUP_TEST_FIXTURES) + "/container-tags-v9.sketchyup"));
        require(!v9.tags().empty() && v9.materials().empty(),
                "Version-9 migration preserves tags without inventing swatches");
        const auto v10 = decodeContainer(
            read(QString(SKETCHYUP_TEST_FIXTURES) + "/container-materials-v10.sketchyup"));
        require(v10.materials().size() == 2 && v10.assets().empty() &&
                    !v10.materials().at(2)->asset &&
                    std::abs(v10.materials().at(2)->opacity - .4f) < 1e-6,
                "Version-10 migration retains front/back materials without invented assets");
        Document coated = v8;
        const auto swatch = createMaterial(coated, "Blue glass", {.1f, .3f, .8f}, .35f);
        const auto coatedDefinition = coated.definitions().begin()->first;
        editComponentDefinition(coated, coatedDefinition, [&](Document &draft) {
            for (const auto &[id, body] : draft.bodies())
                if (!body->surface.faces.empty())
                    return assignMaterial(draft, id, body->surface.faces.begin()->first, swatch,
                                          false, true);
            throw std::runtime_error("Expected a component face");
        });
        const auto coatedBytes = encodeContainer(coated);
        require(encodeContainer(decodeContainer(coatedBytes)) == coatedBytes,
                "Front/back swatches, opacity and canonical shared assignments roundtrip exactly");
        auto corruptMaterial = [&](const std::function<void(QJsonObject &)> &edit) {
            auto root = QJsonDocument::fromJson(encodeDocument(coated)).object();
            edit(root);
            rejects([&] { decodeDocument(QJsonDocument(root).toJson()); });
        };
        corruptMaterial([](auto &root) { root["materials"] = QJsonArray{}; });
        corruptMaterial([](auto &root) { root["nextMaterialId"] = "1"; });
        corruptMaterial([](auto &root) {
            auto materials = root["materials"].toArray();
            auto first = materials[0].toObject();
            first["opacity"] = 2;
            materials[0] = first;
            root["materials"] = materials;
        });
        corruptMaterial([](auto &root) {
            auto materials = root["materials"].toArray();
            materials.append(materials[0]);
            root["materials"] = materials;
        });
        rejects([&] {
            decodeContainer(changeManifest(coatedBytes, [&](auto &manifest) {
                auto floors = manifest["allocatorFloors"].toObject();
                floors["nextMaterialId"] = "99";
                manifest["allocatorFloors"] = floors;
            }));
        });
        Document tagged = v8;
        const auto folder = createTag(tagged, "Building", 0, true);
        const auto tag = createTag(tagged, "Panels", folder);
        assignTag(tagged, tagged.instances().begin()->first, tag);
        const auto definitionId = tagged.definitions().begin()->first;
        Id memberId = 0;
        for (const auto &[id, body] : tagged.definitions().at(definitionId)->members)
            if (id != tagged.definitions().at(definitionId)->root) {
                memberId = id;
                break;
            }
        editComponentDefinition(tagged, definitionId,
                                [&](Document &draft) { return assignTag(draft, memberId, tag); });
        editTag(tagged, folder, {}, {}, false);
        const auto taggedBytes = encodeContainer(tagged);
        require(encodeContainer(decodeContainer(taggedBytes)) == taggedBytes,
                "Tags, folder visibility and placement/canonical assignments roundtrip exactly");
        auto invalidTags = QJsonDocument::fromJson(encodeDocument(tagged)).object();
        auto corruptTag = [&](auto mutate) {
            auto root = invalidTags;
            mutate(root);
            rejects([&] { decodeDocument(QJsonDocument(root).toJson()); });
        };
        corruptTag([](auto &root) { root["nextTagId"] = "1"; });
        corruptTag([](auto &root) { root["tags"] = QJsonArray{}; });
        corruptTag([](auto &root) {
            auto tags = root["tags"].toArray();
            tags.append(tags[0]);
            root["tags"] = tags;
        });
        corruptTag([](auto &root) {
            auto tags = root["tags"].toArray();
            auto folder = tags[0].toObject();
            folder["parent"] = folder["id"];
            tags[0] = folder;
            root["tags"] = tags;
        });
        corruptTag([](auto &root) {
            auto tags = root["tags"].toArray();
            auto tag = tags[1].toObject();
            tag["visible"] = "false";
            tags[1] = tag;
            root["tags"] = tags;
        });
        corruptTag([](auto &root) {
            auto bodies = root["bodies"].toArray();
            auto body = bodies[0].toObject();
            body["tag"] = "1";
            bodies[0] = body;
            root["bodies"] = bodies;
        });
        rejects([&] {
            decodeContainer(changeManifest(taggedBytes, [](auto &manifest) {
                auto floors = manifest["allocatorFloors"].toObject();
                floors["nextTagId"] = "99";
                manifest["allocatorFloors"] = floors;
            }));
        });
        Document component;
        component.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
        createGroup(component, {1});
        auto definition = std::make_shared<ComponentDefinition>();
        definition->id = 1;
        definition->root = 2;
        definition->nextMemberId = 3;
        definition->name = "Reusable panel";
        definition->members = component.bodies();
        auto instance = std::make_shared<ComponentInstance>();
        instance->definition = 1;
        instance->members = {{1, 1}, {2, 2}};
        Edit bind{"Bind component fixture", {}};
        bind.definitions.push_back({1, nullptr, definition});
        bind.instances.push_back({2, nullptr, instance});
        component.apply(bind, component.revision());
        auto secondRoot = std::make_shared<Body>(*component.bodies().at(2));
        secondRoot->id = 3;
        secondRoot->transform = Transform::translation({4, 0, 0}) * Transform::scaling({-2, 1, 1});
        auto secondMember = std::make_shared<Body>(*component.bodies().at(1));
        secondMember->id = 4;
        secondMember->parent = 3;
        auto secondInstance = std::make_shared<ComponentInstance>();
        secondInstance->definition = 1;
        secondInstance->members = {{1, 4}, {2, 3}};
        Edit place{"Place second fixture", {{3, nullptr, secondRoot}, {4, nullptr, secondMember}}};
        place.instances.push_back({3, nullptr, secondInstance});
        component.apply(place, component.revision());
        const auto componentBytes = encodeContainer(component);
        auto componentReopened = decodeContainer(componentBytes);
        require(encodeContainer(componentReopened) == componentBytes &&
                    componentReopened.instances().size() == 2 &&
                    componentReopened.definitions().size() == 1 &&
                    componentReopened.worldArea(4, 5) == 2,
                "Component definitions, instance bindings and affine placement roundtrip exactly");
        const auto componentRoot = QJsonDocument::fromJson(encodeDocument(component)).object();
        auto corruptComponent = [&](auto change) {
            auto root = componentRoot;
            change(root);
            rejects([&] { decodeDocument(QJsonDocument(root).toJson()); });
        };
        corruptComponent([](auto &root) { root["definitions"] = 1; });
        corruptComponent([](auto &root) { root["instances"] = QJsonObject{}; });
        corruptComponent([](auto &root) { root["nextDefinitionId"] = "1"; });
        corruptComponent([](auto &root) { root.remove("instances"); });
        corruptComponent([](auto &root) {
            auto records = root["definitions"].toArray();
            records.append(records[0]);
            root["definitions"] = records;
        });
        corruptComponent([](auto &root) {
            auto records = root["instances"].toArray();
            auto record = records[0].toObject();
            record["definition"] = "999";
            records[0] = record;
            root["instances"] = records;
        });
        corruptComponent([](auto &root) {
            auto records = root["bodies"].toArray();
            auto record = records[0].toObject();
            record["color"] = QJsonArray{1, 0, 0};
            records[0] = record;
            root["bodies"] = records;
        });
        rejects([&] {
            decodeContainer(changeManifest(componentBytes, [](auto &manifest) {
                auto floors = manifest["allocatorFloors"].toObject();
                floors["nextDefinitionId"] = "99";
                manifest["allocatorFloors"] = floors;
            }));
        });
        Document painted;
        painted.addFace({{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}});
        auto faceColored = std::make_shared<Body>(*painted.bodies().at(1));
        const auto tintedFace = faceColored->surface.faces.begin()->first;
        faceColored->faceColors[tintedFace] = {.2f, .3f, .7f};
        painted.apply({"Face tint", {{1, painted.bodies().at(1), faceColored}}},
                      painted.revision());
        const auto paintedBytes = encodeContainer(painted);
        const auto paintedReopened = decodeContainer(paintedBytes);
        require(encodeContainer(paintedReopened) == paintedBytes &&
                    paintedReopened.bodies().at(1)->faceColors ==
                        painted.bodies().at(1)->faceColors,
                "Face color assignments survive exact container roundtrip");
        for (const auto &bad : {QJsonObject{{"999", QJsonArray{.2, .3, .7}}},
                                QJsonObject{{QString::number(tintedFace), QJsonArray{1.1, .3, .7}}},
                                QJsonObject{{QString::number(tintedFace), QJsonArray{.2, .3}}}}) {
            auto root = QJsonDocument::fromJson(encodeDocument(painted)).object();
            auto bodies = root["bodies"].toArray();
            auto body = bodies[0].toObject();
            body["faceColors"] = bad;
            bodies[0] = body;
            root["bodies"] = bodies;
            rejects([&] { decodeDocument(QJsonDocument(root).toJson()); });
        }
        const auto v5 = decodeContainer(
            read(QString(SKETCHYUP_TEST_FIXTURES) + "/container-guides-v5.sketchyup"));
        for (const auto &[id, body] : v5.bodies())
            require(body->kind == BodyKind::Geometry && !body->locked && !body->hidden,
                    "Version-5 migration retains raw context behavior");
        Document grouped = v5;
        std::set<Id> members;
        for (const auto &[id, body] : grouped.bodies())
            members.insert(id);
        const auto groupId = createGroup(grouped, members, "Assembly");
        setEntityState(grouped, groupId, true, true);
        const auto groupBytes = encodeContainer(grouped);
        const auto groupReopened = decodeContainer(groupBytes);
        require(encodeContainer(groupReopened) == groupBytes &&
                    groupReopened.bodies().at(groupId)->kind == BodyKind::Group &&
                    groupReopened.bodies().at(groupId)->hidden &&
                    groupReopened.bodies().at(groupId)->locked,
                "Persistent group kind, visibility, lock and member identities roundtrip exactly");
        auto invalidGroup = QJsonDocument::fromJson(encodeDocument(grouped)).object();
        for (const auto &key : {"kind", "hidden", "locked", "faceColors"}) {
            auto invalid = invalidGroup;
            auto records = invalid["bodies"].toArray();
            auto body = records[0].toObject();
            body[key] = 42;
            records[0] = body;
            invalid["bodies"] = records;
            rejects([&] { decodeDocument(QJsonDocument(invalid).toJson()); });
        }
        Document doc;
        auto id = doc.addFace({{{0, 0, 0}, {3, 0, 0}, {0, 2, 0}}});
        auto bytes = encodeContainer(doc);
        require(bytes.startsWith(QByteArray("SKUPDOC\0", 8)), "Binary envelope");
        require(encodeContainer(decodeContainer(bytes)) == bytes, "Exact container roundtrip");
        require(encodeDocument(decodeContainer(encodeDocument(doc))) == encodeDocument(doc),
                "Raw current-schema roundtrip");
        auto legacy = QJsonDocument::fromJson(encodeDocument(doc)).object();
        legacy["version"] = 1;
        legacy.remove("solar");
        legacy.remove("displayPrecision");
        legacy.remove("style");
        legacy.remove("scenes");
        legacy.remove("sections");
        legacy.remove("annotations");
        legacy.remove("nextAnnotationId");
        legacy.remove("nextSectionId");
        legacy.remove("activeSections");
        legacy.remove("nextSceneId");
        legacy.remove("hosted");
        legacy.remove("displayUnits");
        legacy.remove("revision");
        legacy.remove("definitions");
        legacy.remove("instances");
        legacy.remove("nextDefinitionId");
        legacy.remove("tags");
        legacy.remove("nextTagId");
        legacy.remove("materials");
        legacy.remove("nextMaterialId");
        legacy.remove("assets");
        legacy.remove("nextAssetId");
        legacy.remove("assetStorage");
        auto records = legacy["bodies"].toArray();
        for (int i = 0; i < records.size(); ++i) {
            auto body = records[i].toObject();
            for (const auto &key : {"parent", "transform", "properties", "nextEdgeId", "edges",
                                    "curves", "guides", "kind", "hidden", "locked", "faceColors",
                                    "tag", "materials", "faceMaterials", "edgeAppearances", "faceTextureMappings"})
                body.remove(key);
            records[i] = body;
        }
        legacy["bodies"] = records;
        auto legacyBytes = QJsonDocument(legacy).toJson();
        auto path = dir.filePath("model.sketchyup");
        write(path, legacyBytes);
        auto migrated = loadDocument(path);
        require(read(path) == legacyBytes && migrated.identity() == doc.identity(),
                "Migration leaves source intact");
        saveDocument(migrated, path);
        require(read(path + ".bak") == legacyBytes, "First migration save preserves legacy backup");
        for (qsizetype i = 0; i < bytes.size(); ++i)
            rejects([&] { decodeContainer(bytes.first(i)); });
        auto corrupt = bytes;
        corrupt.back() ^= 1;
        rejects([&] { decodeContainer(corrupt); });
        corrupt = bytes;
        qToLittleEndian<quint32>(999, corrupt.data() + 8);
        rejects([&] { decodeContainer(corrupt); });
        corrupt = bytes;
        qToLittleEndian<quint32>(UINT32_MAX, corrupt.data() + 12);
        rejects([&] { decodeContainer(corrupt); });
        rejects([&] { decodeContainer(bytes + "extra"); });
        for (const auto &key :
             {"documentId", "epoch", "revision", "units", "allocatorFloors", "requiredFeatures"})
            rejects([&] {
                decodeContainer(changeManifest(bytes, [&](auto &m) { m[key] = "invalid"; }));
            });
        for (const auto &key : {"offset", "bytes", "sha256", "kind", "encoding"})
            rejects([&] {
                decodeContainer(changeManifest(bytes, [&](auto &m) {
                    auto chunks = m["chunks"].toArray();
                    auto c = chunks[0].toObject();
                    c[key] = "18446744073709551615";
                    chunks[0] = c;
                    m["chunks"] = chunks;
                }));
            });
        rejects([&] {
            decodeContainer(changeManifest(bytes, [](auto &m) {
                auto chunks = m["chunks"].toArray();
                chunks.append(chunks[0]);
                m["chunks"] = chunks;
            }));
        });
        rejects([&] {
            decodeContainer(changeManifest(bytes, [](auto &m) { m["futureRequiredData"] = true; }));
        });
        // Save completion belongs to captured content, including branching copies and reopen.
        auto snapshot = captureSave(doc);
        auto branch = doc;
        doc.move(id, {1, 0, 0});
        branch.move(id, {2, 0, 0});
        auto branchSnapshot = captureSave(branch);
        saveSnapshot(doc, snapshot, path);
        require(doc.dirty() && read(path) == bytes, "Later edits remain dirty");
        doc.undo();
        require(!doc.dirty(), "Undo to saved content is clean");
        doc.redo();
        saveSnapshot(doc, branchSnapshot, path);
        require(doc.dirty(),
                "Independent copy with same revision cannot falsely acknowledge current content");
        Document unrelated;
        rejects([&] { saveSnapshot(unrelated, snapshot, path); });
        auto reopened = loadDocument(path);
        rejects([&] { saveSnapshot(reopened, snapshot, path); });
        saveDocument(doc, path);
        require(!doc.dirty(), "Durable save acknowledged");
        const auto previous = read(path);
        doc.move(id, {0, 1, 0});
        const auto next = encodeContainer(doc);
        // Every process-kill boundary leaves a complete old/new target and, after replacement,
        // a verified old backup. A kill before target rename cannot destroy the old target.
        for (int stage = 0; stage < 3; ++stage) {
            write(path, previous);
            const auto pid = fork();
            require(pid >= 0, "fork");
            if (!pid) {
                directorySyncs = 0;
                killFile = stage == 0;
                killDirectory = stage;
                saveDocument(doc, path);
                _exit(90);
            }
            int status = 0;
            require(waitpid(pid, &status, 0) == pid, "waitpid");
            require(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL,
                    "Injected kill reached boundary");
            require(read(path) == (stage == 2 ? next : previous), "Kill preserves complete target");
            if (stage > 0)
                require(read(path + ".bak") == previous, "Kill preserves last good backup");
            (void)loadDocument(path);
        }
        for (int stage : {1, 2}) {
            write(path, previous);
            directorySyncs = 0;
            failSync = stage;
            auto message = rejects([&] { saveDocument(doc, path); });
            failSync = 0;
            require(doc.dirty(), "Failed sync remains dirty");
            require(read(path) == (stage == 2 ? next : previous),
                    "Sync failure reports actual replacement state");
            require((message.find("uncertain") != std::string::npos) == (stage == 2),
                    "Post-rename uncertainty is explicit");
        }
        write(path, previous);
        failWrite = true;
        rejects([&] { saveDocument(doc, path); });
        failWrite = false;
        require(read(path) == previous && doc.dirty(),
                "Disk-full write preserves target and dirty state");
        failFileSync = true;
        rejects([&] { saveDocument(doc, path); });
        failFileSync = false;
        require(read(path) == previous && doc.dirty(),
                "Disconnected storage sync preserves target");
        const auto locked = dir.filePath("locked");
        require(QDir().mkdir(locked), "Permission fixture directory");
        const auto lockedPath = locked + "/model.sketchyup";
        write(lockedPath, previous);
        require(::chmod(QFile::encodeName(dir.path()).constData(), 0755) == 0 &&
                    ::chmod(QFile::encodeName(locked).constData(), 0555) == 0,
                "Fixture permissions");
        const auto child = fork();
        require(child >= 0, "Permission fork");
        if (!child) {
            if (geteuid() == 0 && setuid(65534) != 0)
                _exit(91);
            try {
                saveDocument(doc, lockedPath);
                _exit(92);
            } catch (const std::exception &) {
                _exit(doc.dirty() ? 0 : 93);
            }
        }
        int permissionStatus = 0;
        require(waitpid(child, &permissionStatus, 0) == child && WIFEXITED(permissionStatus) &&
                    WEXITSTATUS(permissionStatus) == 0,
                "Permission denial is reported without acknowledging save");
        require(read(lockedPath) == previous, "Permission denial preserves previous file");
        require(::chmod(QFile::encodeName(locked).constData(), 0755) == 0,
                "Restore fixture permissions");
        // A backup obstruction fails before target replacement; existing good backup survives
        // replacement of an invalid file explicitly selected by the user.
        const auto blocked = dir.filePath("blocked.sketchyup");
        write(blocked, previous);
        require(QDir().mkdir(blocked + ".bak"), "Backup obstruction");
        rejects([&] { saveDocument(doc, blocked); });
        require(read(blocked) == previous, "Backup failure preserves target");
        write(path + ".bak", previous);
        write(path, "invalid existing document");
        saveDocument(doc, path);
        require(read(path + ".bak") == previous, "Invalid target never overwrites verified backup");
        const auto edge = doc.bodies().at(id)->topology.edges.begin()->first;
        doc.splitEdge(id, edge, .5);
        const auto edgeFloor = doc.bodies().at(id)->topology.nextId;
        doc.undo();
        auto restoredTopology = decodeContainer(encodeContainer(doc));
        require(restoredTopology.bodies().at(id)->topology == doc.bodies().at(id)->topology,
                "Exact edge records and allocator survive save after undo");
        restoredTopology.splitEdge(id, edge, .4);
        for (const auto &[newId, record] : restoredTopology.bodies().at(id)->topology.edges)
            require(doc.bodies().at(id)->topology.edges.contains(newId) || newId >= edgeFloor,
                    "Reopened edge split never reuses retired IDs");
        auto invalid = QJsonDocument::fromJson(encodeDocument(doc)).object();
        auto invalidBodies = invalid["bodies"].toArray();
        auto invalidBody = invalidBodies[0].toObject();
        invalidBody["nextEdgeId"] = "1";
        invalidBodies[0] = invalidBody;
        invalid["bodies"] = invalidBodies;
        rejects([&] { decodeDocument(QJsonDocument(invalid).toJson()); });
        // A symlink save syncs/replaces its actual target and leaves the link intact.
        const auto link = dir.filePath("alias.sketchyup");
        require(QFile::link(path, link), "Symlink fixture");
        doc.move(id, {0, 0, 1});
        saveDocument(doc, link);
        require(QFileInfo(link).isSymLink() && read(path) == encodeContainer(doc),
                "Symlink target save");
        std::cout
            << "Container, snapshot, migration, ENOSPC, fsync and process-kill checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        failWrite = false;
        failSync = 0;
        std::cerr << e.what() << '\n';
        return 1;
    }
}
