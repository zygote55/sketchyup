#include "core/components.hpp"
#include "core/geometry_subset.hpp"
#include "integrations/glb_export.hpp"
#include "io/document_io.hpp"
#include "io/recovery.hpp"
#include "io/text_source_io.hpp"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QTemporaryDir>
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
    throw std::runtime_error("Malformed text source accepted");
}
TextSource source() {
    TextSource s;
    s.text = "O café\nلا";
    s.family = s.actualFamily = "Unavailable fixture font 82b";
    s.style = s.actualStyle = "Regular";
    s.height = .2;
    s.depth = .03;
    s.fonts = {{s.family, s.style, std::string(64, 'a'), 8}};
    s.regions = 1;
    s.geometryDigest = std::string(64, 'b');
    return s;
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        const auto record = source();
        check(decodeTextSource(encodeTextSource(record)) == record, "Source codec is exact");
        Document doc;
        const auto id = doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}},
                                     {{.5, .5, 0}, {.5, 1.5, 0}, {1.5, 1.5, 0}, {1.5, .5, 0}}});
        auto old = doc.bodies().at(id);
        auto body = std::make_shared<Body>(*old);
        body->surface.extrude(body->surface.faces.begin()->first, .03);
        body->textSource = record;
        doc.apply({"Attach text source", {{id, old, body}}}, doc.revision());
        const auto stored = encodeContainer(doc);
        const auto snapshot = doc.readSnapshot();
        auto loaded = decodeContainer(stored);
        check(encodeContainer(loaded) == stored && loaded.bodies().at(id)->textSource == record,
              "Reopen keeps cached geometry and source without loading any font");
        check(QJsonDocument::fromJson(encodeDocument(doc)).object()["version"] == 25,
              "Editable text uses explicit schema 24");
        check(exportGlb(RenderSnapshot::capture(doc))
                      .manifest["losses"]
                      .toObject()["editableTextSourcesOmitted"] == 1,
              "Surface export retains geometry and reports source metadata loss");
        QTemporaryDir files;
        const auto beforeRecovery = captureRecovery(doc);
        old = doc.bodies().at(id);
        body = std::make_shared<Body>(*old);
        body->textSource->text = "Changed";
        doc.apply({"Change text source", {{id, old, body}}}, doc.revision());
        check(snapshot.bodies().at(id)->textSource == record,
              "Read snapshot owns immutable source");
        QString recoveryKey;
        {
            RecoveryWriter writer(files.path(), QString::fromStdString(doc.identity()));
            writer.write(beforeRecovery);
            writer.write(captureRecovery(doc));
            recoveryKey = writer.key();
        }
        const auto recovered = readRecovery(files.path(), recoveryKey);
        check(recovered.verified && recovered.document &&
                  encodeContainer(*recovered.document) == encodeContainer(doc),
              "Recovery restores source metadata and cached geometry exactly");
        doc.undo();
        check(doc.bodies().at(id)->textSource == record, "Undo restores text source");
        doc.redo();
        check(doc.bodies().at(id)->textSource->text == "Changed", "Redo restores edit");
        doc.undo();
        const auto component = createComponent(doc, id, "Cached text");
        check(!doc.bodies().at(id)->textSource && component.movedGeometry.contains(id) &&
                  doc.bodies().at(component.movedGeometry.at(id))->textSource == record,
              "Component normalization moves source with its geometry");
        const auto placed =
            placeComponent(doc, component.definition, Transform::translation({3, 0, 0}));
        size_t sources{};
        for (const auto &[_, b] : doc.bodies())
            sources += b->textSource.has_value();
        check(sources == 2 && placed.instance, "Component placement retains member text source");
        check(encodeContainer(decodeContainer(encodeContainer(doc))) == encodeContainer(doc),
              "Component member sources roundtrip exactly");
        const auto &member = *doc.bodies().at(component.movedGeometry.at(id));
        GeometrySubset part;
        includeGeometryFace(member, part, member.surface.faces.begin()->first);
        check(!extractGeometry(member, part)->textSource, "Partial geometry extraction is baked");
        auto malformed = encodeTextSource(record);
        auto bad = [&](const QString &key, QJsonValue value) {
            auto object = malformed;
            object[key] = value;
            rejects([&] { decodeTextSource(object); });
        };
        bad("version", 2);
        bad("unknown", true);
        bad("height", "0.2");
        bad("depth", -1);
        bad("height", 0);
        bad("depth", 1e-8);
        bad("lineSpacing", 11);
        bad("regions", 0);
        bad("regions", 1.5);
        bad("geometryDigest", "bad");
        bad("fonts", QJsonArray{});
        bad("text", QString(4097, 'a'));
        bad("text", QString(QChar::Null));
        bad("allowSubstitution", 1);
        bad("substituted", true);
        auto invalid = record;
        invalid.text = std::string("\xc0\xaf", 2);
        rejects([&] { validateTextSource(invalid); });
        invalid = record;
        invalid.fonts.push_back(invalid.fonts.front());
        rejects([&] { validateTextSource(invalid); });
        auto raw = QJsonDocument::fromJson(encodeDocument(loaded)).object();
        auto rows = raw["bodies"].toArray();
        auto row = rows[0].toObject();
        row["textSource"] = QJsonValue::Null;
        rows[0] = row;
        raw["bodies"] = rows;
        rejects([&] { decodeDocument(QJsonDocument(raw).toJson()); });
        const auto migrated = loadDocument(SOURCE_DIR "/tests/fixtures/annotations-v21.sketchyup");
        check(migrated.annotations().size() == 3, "Actual schema 21 preserves annotations");
        for (const auto &[_, b] : migrated.bodies())
            check(!b->textSource, "Old bodies have no source");
        std::cout << "Typed editable source, font-independent persistence, recovery and components "
                     "passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
