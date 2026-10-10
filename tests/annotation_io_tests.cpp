#include "core/annotations.hpp"
#include "integrations/glb_export.hpp"
#include "io/annotations_io.hpp"
#include "io/document_io.hpp"
#include "io/recovery.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F operation) {
    try {
        operation();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Malformed annotation file accepted");
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        Document doc;
        const auto body = doc.addFace({{{0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {0, 4, 0}}});
        const auto face = doc.bodies().at(body)->surface.faces.begin()->first;
        AnnotationRecord label;
        label.kind = AnnotationKind::Label;
        label.name = "Face note";
        label.text = "Café\n入口";
        label.anchors = {faceAnchor(doc, body, face, {1, 1, 0})};
        label.offset = {.5, .25, 1};
        label.color = {.2f, .4f, .6f};
        const auto id = createAnnotation(doc, label);
        const auto raw = encodeDocument(doc), container = encodeContainer(doc);
        check(exportGlb(RenderSnapshot::capture(doc))
                      .manifest["losses"]
                      .toObject()["annotationsOmitted"] == 1,
              "Surface export explicitly reports annotation graphics loss");
        check(encodeContainer(decodeContainer(container)) == container,
              "Annotation container reopens byte-exact");
        const auto tree = QJsonDocument::fromJson(raw).object();
        check(tree["version"] == 25, "Schema 25 explicit");
        check(decodeDocument(raw).annotations().at(id)->text == label.text,
              "Unicode multiline label persists");
        auto broken = *doc.annotations().at(id);
        const auto checkpoint = captureRecovery(doc);
        doc.erase(body);
        const auto missing = encodeContainer(doc);
        check(decodeContainer(missing).annotations().at(id)->anchors[0].state ==
                  AnchorState::Missing,
              "Missing anchor state persists with last world position");
        QTemporaryDir recovery;
        QString key;
        {
            RecoveryWriter writer(recovery.path(), QString::fromStdString(doc.identity()));
            writer.write(checkpoint);
            writer.write(captureRecovery(doc));
            key = writer.key();
        }
        const auto recovered = readRecovery(recovery.path(), key);
        check(recovered.verified && recovered.document &&
                  encodeContainer(*recovered.document) == missing,
              "Recovery journal restores exact missing-reference annotation snapshot");
        doc.undo();
        check(*doc.annotations().at(id) == broken, "IO path does not alter Undo snapshots");
        auto mutate = [&](const std::function<void(QJsonObject &)> &change) {
            auto root = tree;
            change(root);
            rejects([&] { decodeDocument(QJsonDocument(root).toJson()); });
        };
        mutate([](auto &o) { o.remove("solar"); o.remove("annotations"); });
        mutate([](auto &o) { o["nextAnnotationId"] = 2; });
        mutate([](auto &o) { o["nextAnnotationId"] = "01"; });
        mutate([](auto &o) { o["nextAnnotationId"] = "1"; });
        mutate([](auto &o) {
            auto a = o["annotations"].toArray();
            a.append(a[0]);
            o["annotations"] = a;
        });
        const auto rows = tree["annotations"].toArray();
        auto badRecord = [&](const QString &key, QJsonValue value) {
            auto record = rows[0].toObject();
            record[key] = value;
            rejects([&] { decodeAnnotations(QJsonArray{record}, doc.nextAnnotationId()); });
        };
        badRecord("id", 1);
        badRecord("id", "01");
        badRecord("kind", "angle");
        badRecord("unknown", true);
        badRecord("textSize", 100);
        badRecord("leader", 1);
        badRecord("offset", QJsonArray{1, 2});
        badRecord("color", QJsonArray{1, 2, 0});
        badRecord("text", QJsonValue::Null);
        auto badAnchor = [&](const QString &key, QJsonValue value) {
            auto record = rows[0].toObject();
            auto anchor = record["anchors"].toArray()[0].toObject();
            anchor[key] = value;
            record["anchors"] = QJsonArray{anchor};
            rejects([&] { decodeAnnotations(QJsonArray{record}, doc.nextAnnotationId()); });
        };
        badAnchor("state", "lost");
        badAnchor("kind", "solid");
        badAnchor("body", 1);
        badAnchor("entity", "0");
        badAnchor("parameter", .5);
        badAnchor("vertices", QJsonArray{"1", "1", "2"});
        badAnchor("weights", QJsonArray{1, 1, 1});
        badAnchor("fallback", QJsonArray{1, 2});
        badAnchor("unknown", 0);
        QFile old(QString(SOURCE_DIR) + "/tests/fixtures/section-scenes-v20.sketchyup");
        check(old.open(QIODevice::ReadOnly), "Actual prior-writer fixture opens");
        const auto upgraded = decodeContainer(old.readAll());
        check(upgraded.annotations().empty() && upgraded.nextAnnotationId() == 1 &&
                  upgraded.scenes().size() == 1 && upgraded.activeSections().size() == 3,
              "Actual v20 named-section scene migrates without data loss");
        check(encodeContainer(decodeContainer(encodeContainer(upgraded))) ==
                  encodeContainer(upgraded),
              "Migrated schema21 roundtrip exact");
        auto legacy = tree;
        legacy["version"] = 20;
        rejects([&] { decodeDocument(QJsonDocument(legacy).toJson()); });
        legacy.remove("solar");
        legacy.remove("annotations");
        legacy.remove("nextAnnotationId");
        check(decodeDocument(QJsonDocument(legacy).toJson()).annotations().empty(),
              "Legacy schema excludes new annotation fields");
        auto alias = std::make_shared<AnnotationRecord>(*doc.annotations().at(id));
        Document restored;
        restored.restore(doc.identity(), doc.nextId(), doc.bodies(), doc.revision(), {}, {}, 1, {},
                         1, {}, 1, {}, 1, DisplayUnit::Meters,
                         std::make_shared<const HostedComponents>(), {}, {}, 1, {}, 1, {},
                         {{id, alias}}, doc.nextAnnotationId());
        alias->text = "Mutable alias";
        check(restored.annotations().at(id)->text == label.text,
              "Restore freezes incoming annotation records");
        std::cout << "Annotation schema21, strict fields, Unicode, broken states and actual v20 "
                     "migration passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
