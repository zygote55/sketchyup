#include "automation/transaction_coordinator.hpp"
#include "core/components.hpp"
#include "core/hosted_components.hpp"
#include "io/document_io.hpp"
#include "io/recovery.hpp"
#include "legacy_texture_fields.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtEndian>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F operation) {
    try {
        operation();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Expected hosted persistence rejection");
}
QByteArray rewrite(const QByteArray &source,
                   const std::function<void(QJsonObject &, QJsonObject &)> &operation) {
    const auto length = qFromLittleEndian<quint32>(source.constData() + 12);
    auto manifest = QJsonDocument::fromJson(source.mid(16, length)).object();
    auto document = QJsonDocument::fromJson(source.mid(16 + length)).object();
    operation(manifest, document);
    const auto payload = QJsonDocument(document).toJson(QJsonDocument::Compact);
    auto chunks = manifest["chunks"].toArray();
    auto chunk = chunks[0].toObject();
    chunk["bytes"] = QString::number(payload.size());
    chunk["sha256"] =
        QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
    chunks[0] = chunk;
    manifest["chunks"] = chunks;
    const auto metadata = QJsonDocument(manifest).toJson(QJsonDocument::Compact);
    auto header = source.left(16);
    qToLittleEndian<quint32>(metadata.size(), header.data() + 12);
    return header + metadata + payload;
}
void roundtripAndRecovery(bool cutting) {
    Document doc;
    const auto host = doc.addFace({{{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}}});
    doc.extrude(host, doc.bodies().at(host)->surface.faces.begin()->first, 1);
    Id face = 0;
    for (const auto &[id, record] : doc.bodies().at(host)->surface.faces)
        if (doc.bodies().at(host)->surface.normal(id).z > .9)
            face = id;
    const auto pane = doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}}});
    const auto reference = doc.bodies().at(pane)->surface.faces.begin()->first;
    const auto component = createComponent(doc, pane, "Saved window");
    setComponentGlue(
        doc, component.definition,
        ComponentGlue{component.movedGeometry.at(pane), reference, {1, 1, 0}, {1, 0, 0}, cutting});
    doc.transform(component.instance, Transform::translation({2, 2, 1.25}));
    const auto before = doc.readSnapshot();
    bindComponentAtCurrentPose(doc, component.instance, host, face, .25);
    const auto raw = encodeDocument(doc), bytes = encodeContainer(doc);
    auto reopened = decodeContainer(bytes);
    check(encodeDocument(reopened) == raw && encodeContainer(reopened) == bytes &&
              reopened.hostedComponents() == doc.hostedComponents() && !reopened.canUndo(),
          "Baseline, attachment frame, inset and native opening maps roundtrip exactly");
    reopened.move(component.instance, {1, 0, 0});
    const auto moved = encodeDocument(reopened);
    check(encodeDocument(decodeDocument(moved)) == moved,
          "A reopened attachment still updates and persists with the component");
    reopened.undo();
    validateHostedComponents(reopened.hostedComponents(), reopened.bodies(), reopened.definitions(),
                             reopened.instances());
    const auto json = QJsonDocument::fromJson(raw).object();
    check(json["version"] == 18, "Hosted document schema version is explicit");
    for (int variant = 0; variant < 17; ++variant) {
        auto bad = json;
        auto hosted = bad["hosted"].toObject();
        auto attachments = hosted["attachments"].toArray();
        auto attachment = attachments[0].toObject();
        auto hosts = hosted["hosts"].toArray();
        auto surface = hosts[0].toObject();
        if (variant == 0)
            attachment["host"] = "999";
        if (variant == 1)
            attachment["face"] = 1;
        if (variant == 2)
            attachment["inset"] = "0";
        if (variant == 3)
            attachment.remove("inset");
        if (variant == 4)
            attachment["future"] = true;
        if (variant == 5)
            attachment["frame"] = QJsonArray{1, 2, 3};
        if (variant == 6) {
            auto frame = attachment["frame"].toArray();
            frame[14] = 5;
            attachment["frame"] = frame;
        }
        attachments[0] = attachment;
        if (variant == 7)
            attachments.append(attachment);
        if (variant == 8)
            attachments = {};
        if (variant == 9)
            surface.remove("uncut");
        if (variant == 10) {
            auto uncut = surface["uncut"].toObject();
            auto vertices = uncut["vertices"].toArray();
            auto vertex = vertices[0].toArray();
            vertex[1] = vertex[1].toDouble() + .1;
            vertices[0] = vertex;
            uncut["vertices"] = vertices;
            surface["uncut"] = uncut;
        }
        if (variant == 11) {
            auto uncut = surface["uncut"].toObject();
            uncut["materials"] = QJsonArray{};
            surface["uncut"] = uncut;
        }
        hosts[0] = surface;
        if (variant == 12)
            hosts.append(surface);
        if (variant == 13)
            hosts = {};
        if (variant == 14) {
            while (hosts.size() < 17)
                hosts.append(surface);
        }
        hosted["hosts"] = hosts;
        hosted["attachments"] = attachments;
        bad["hosted"] = hosted;
        if (variant == 15)
            bad.remove("hosted");
        if (variant == 16)
            bad["hosted"] = QJsonValue::Null;
        rejects([&] { decodeDocument(QJsonDocument(bad).toJson()); });
    }
    if (cutting) {
        for (int variant = 0; variant < 7; ++variant) {
            auto bad = json;
            auto hosted = bad["hosted"].toObject();
            auto hosts = hosted["hosts"].toArray();
            auto host = hosts[0].toObject();
            auto openings = host["openings"].toArray();
            auto opening = openings[0].toObject();
            if (variant == 0)
                opening["exit"] = "999";
            if (variant == 1)
                opening["instance"] = "999";
            if (variant == 2)
                opening["vertices"] = QJsonArray{};
            if (variant == 3) {
                auto corners = opening["corners"].toArray();
                corners.append(corners[0]);
                opening["corners"] = corners;
            }
            if (variant == 4) {
                auto maps = opening["vertices"].toArray();
                auto pair = maps[0].toArray();
                pair[1] = pair[2];
                maps[0] = pair;
                opening["vertices"] = maps;
            }
            if (variant == 5) {
                auto maps = opening["jambs"].toArray();
                auto pair = maps[0].toArray();
                const QJsonValue a = pair[0], b = pair[1];
                pair[0] = b;
                pair[1] = a;
                maps[0] = pair;
                opening["jambs"] = maps;
            }
            openings[0] = opening;
            if (variant == 6)
                openings.append(opening);
            host["openings"] = openings;
            hosts[0] = host;
            hosted["hosts"] = hosts;
            bad["hosted"] = hosted;
            rejects([&] { decodeDocument(QJsonDocument(bad).toJson()); });
        }
    }
    const auto legacy = rewrite(bytes, [](auto &manifest, auto &document) {
        auto features = manifest["requiredFeatures"].toArray();
        features.removeAt(features.size() - 1); // saved-scenes-v1
        features.removeAt(features.size() - 1); // model-style-v1
        features.removeAt(features.size() - 1);
        manifest["requiredFeatures"] = features;
        auto chunks = manifest["chunks"].toArray();
        auto chunk = chunks[0].toObject();
        chunk["encoding"] = "json-v14";
        chunks[0] = chunk;
        manifest["chunks"] = chunks;
        features = manifest["requiredFeatures"].toArray();
        features.removeAt(features.size() - 1);
        manifest["requiredFeatures"] = features;
        removeTextureMappingFields(document);
        document["version"] = 14;
        document.remove("style");
        document.remove("scenes");
        document.remove("nextSceneId");
        auto floors = manifest["allocatorFloors"].toObject();
        floors.remove("nextSceneId");
        manifest["allocatorFloors"] = floors;
        document.remove("hosted");
    });
    const auto old = decodeContainer(legacy);
    check(old.hostedComponents().attachments.empty() &&
              old.definitions().at(component.definition)->glue,
          "Schema 14 retains canonical glue without inventing host relationships");
    rejects([&] {
        decodeContainer(rewrite(bytes, [](auto &manifest, auto &) {
            auto features = manifest["requiredFeatures"].toArray();
            features.removeAt(features.size() - 1);
            manifest["requiredFeatures"] = features;
        }));
    });
    QTemporaryDir files;
    check(files.isValid(), "Isolated persistence directory");
    const auto save = captureSave(doc);
    const auto recovery = captureRecovery(doc);
    detachComponent(doc, component.instance);
    saveSnapshot(doc, save, files.filePath("window.sketchyup"));
    check(doc.dirty() && loadDocument(files.filePath("window.sketchyup")).hostedComponents() ==
                             reopened.hostedComponents(),
          "Immutable save retains captured attachments while current document remains dirty");
    QString key;
    {
        RecoveryWriter writer(files.filePath("recovery"), QString::fromStdString(doc.identity()));
        key = writer.key();
        writer.write(recovery);
    }
    const auto recovered = readRecovery(files.filePath("recovery"), key);
    check(recovered.verified && recovered.document && recovered.document->dirty() &&
              recovered.document->hostedComponents() == reopened.hostedComponents(),
          "Recovery preserves original surface and attachment state");
    const auto after = decodeContainer(bytes);
    const auto transactionRoot = files.filePath("transactions");
    {
        OutcomeStore store(transactionRoot, QString::fromStdString(before.identity()));
        const auto hash = QString(64, 'b');
        const auto pending = store.begin(before, hash);
        store.commit(
            pending["requestId"].toString(), hash, before, after,
            {{"transactionApiVersion", 1},
             {"revision", QString::number(after.revision())},
             {"undo", QJsonObject{{"label", "Bind component to face"},
                                  {"taskId", ""},
                                  {"request", ""},
                                  {"assistant", false},
                                  {"commitRevision", QString::number(after.revision())}}}});
    }
    TransactionCoordinator::Options options;
    options.mode = TransactionCoordinator::OpenMode::RecoverLatest;
    TransactionCoordinator coordinator(before, transactionRoot, options);
    check(encodeContainer(coordinator.document()) == bytes &&
              coordinator.document().history().total == 1,
          "Durable recovery reconstructs attachment, component and cut as one Undo, including "
          "metadata-only binding");
    coordinator.edit([](Document &document) { document.undo(); });
    check(coordinator.document().hostedComponents().attachments.empty(),
          "Recovered Undo restores the unbound baseline");
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        roundtripAndRecovery(true);
        roundtripAndRecovery(false);
        std::cout << "Hosted schema 15, strict identity maps, old schema migration, immutable "
                     "saves and durable Undo recovery passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
