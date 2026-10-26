// R100.a (ADR 0174 §1, §2, §5, §6, §7): the optional edge stroke, schema 31 with edge-styles-v1,
// the lowest-schema writer, the `.sketchystyle` envelope, the style commands and the checked-in
// fixtures. The released-reader refusal runs when SKETCHYUP_PRIOR_CLI is set (package acceptance).
#include "automation/commands.hpp"
#include "automation/inspection.hpp"
#include "core/assets.hpp"
#include "core/model.hpp"
#include "core/presentation.hpp"
#include "core/scenes.hpp"
#include "io/assets.hpp"
#include "io/document_io.hpp"
#include "io/library_bundle.hpp"
#include "io/model_style_io.hpp"
#include "io/native_format.hpp"
#include "io/style_bundle.hpp"
#include <QByteArray>
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

// A 4x4 red and white checkerboard, 83 bytes, generated once by a zlib and struct script. The
// fixtures embed these bytes, so the texture never depends on a codec's output.
const char texturePngBase64[] = "iVBORw0KGgoAAAANSUhEUgAAAAQAAAAECAIAAAAmkwkpAAAAGklEQVR42mM4oSEHRF"
                                "+/fgAiBhQOhIIIoXAAMlkfQfZZBlEAAAAASUVORK5CYII=";

QByteArray texturePng() { return QByteArray::fromBase64(texturePngBase64); }

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
    check(file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
              file.write(bytes) == bytes.size(),
          "Write " + path.toStdString());
}

// The container manifest: a 16-byte header, then the JSON manifest.
QJsonObject containerManifest(const QByteArray &container) {
    const auto length = qFromLittleEndian<quint32>(container.constData() + 12);
    return QJsonDocument::fromJson(container.mid(16, length)).object();
}

QByteArray withContainerManifest(const QByteArray &container, const QJsonObject &manifest) {
    const auto length = qFromLittleEndian<quint32>(container.constData() + 12);
    const auto json = QJsonDocument(manifest).toJson(QJsonDocument::Compact);
    QByteArray result = container.left(16);
    qToLittleEndian<quint32>(quint32(json.size()), result.data() + 12);
    return result + json + container.mid(16 + qsizetype(length));
}

bool hasFeature(const QByteArray &container, const char *feature) {
    return containerManifest(container)["requiredFeatures"].toArray().contains(
        QString::fromLatin1(feature));
}

// A fixed identity, so the fixtures and every rebuild encode the same bytes.
Document stable(Document doc) {
    auto json = QJsonDocument::fromJson(encodeDocument(doc)).object();
    json["documentId"] = QStringLiteral("00000000000000000000000000000b31");
    return decodeDocument(QJsonDocument(json).toJson(QJsonDocument::Compact));
}

// The schema-31 fixture: a face, one embedded texture, and a document style with a dashed,
// semi-transparent edge stroke that names the texture.
Document styleFixture() {
    Document doc;
    doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}});
    const auto texture = createAsset(doc, "Sketch edge", "image/png", assetPayload(texturePng()));
    auto style = doc.style();
    style.profiles = true;
    style.profileWidth = 3;
    style.edgeStroke = EdgeStroke{2.5, .6, EdgeDash::Dashed, texture};
    doc.setStyle(style);
    return stable(doc);
}

StyleMetadata fixtureMetadata() {
    return StyleMetadata{
        "Sketch edges", "Dashed sketch edges with a checkerboard texture", {"sketch", "edge"}};
}

// A style file with its manifest and payload, assembled for the envelope checks below.
QByteArray styleEnvelope(const QJsonObject &manifest, const QByteArray &payload,
                         quint32 thumbnail = 0) {
    const auto json = QJsonDocument(manifest).toJson(QJsonDocument::Compact);
    QByteArray result(24, '\0');
    result.replace(0, 8, QByteArray("SKYLIB\0\1", 8));
    qToLittleEndian<quint32>(quint32(json.size()), result.data() + 8);
    qToLittleEndian<quint64>(quint64(payload.size()), result.data() + 12);
    qToLittleEndian<quint32>(thumbnail, result.data() + 20);
    return result + json + payload;
}

QJsonObject styleManifest(const QByteArray &file) {
    const auto length = qFromLittleEndian<quint32>(file.constData() + 8);
    return QJsonDocument::fromJson(file.mid(24, qsizetype(length))).object();
}

QByteArray stylePayload(const QByteArray &file) {
    const auto length = qFromLittleEndian<quint32>(file.constData() + 8);
    const auto size = qFromLittleEndian<quint64>(file.constData() + 12);
    return file.mid(24 + qsizetype(length), qsizetype(size));
}

QString sha256(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QJsonObject batch(const Document &doc, QJsonArray commands) {
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(doc.identity())},
            {"expectedRevision", QString::number(doc.revision())},
            {"commands", commands}};
}

// The schema-31 flag: a style with an edge stroke writes 31, and nothing else raises it.
void schemaSelection() {
    check(nativeDocumentVersionFor(Document{}) == 26, "An empty document stays schema 26");
    Document plain;
    plain.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
    const auto plainBytes = encodeContainer(plain);
    check(nativeDocumentVersionFor(plain) == 26 && !hasFeature(plainBytes, "edge-styles-v1") &&
              !encodeDocument(plain).contains("edgeStroke"),
          "A document without an edge stroke writes no edge-styles-v1 and no edgeStroke");

    Document presentation;
    SceneSnapshot plan;
    plan.camera = SceneCamera{};
    const auto scene = createScene(presentation, "Plan", plan);
    const auto other = createScene(presentation, "Plan again", plan);
    createCameraPath(presentation, "Tour", {scene, other}, CameraPathTiming{30, 12, 6});
    check(nativeDocumentVersionFor(presentation) == nativePresentationDocumentVersion &&
              nativePresentationDocumentVersion == 30,
          "Presentation records without an edge stroke stay schema 30");

    const auto fixture = styleFixture();
    check(nativeDocumentVersionFor(fixture) == 31 && nativeDocumentVersion == 31,
          "An edge stroke on the document style is schema 31");
    const auto bytes = encodeContainer(fixture);
    check(hasFeature(bytes, "edge-styles-v1") && hasFeature(bytes, "presentation-v1"),
          "Schema 31 carries edge-styles-v1 and, cumulatively, presentation-v1");
    check(containerManifest(bytes)["chunks"].toArray()[0].toObject()["encoding"] == "json-v31",
          "The schema-31 document chunk is encoded as json-v31");

    Document inScene;
    SceneSnapshot snapshot;
    snapshot.style = ModelStyle{};
    snapshot.style->edgeStroke = EdgeStroke{};
    createScene(inScene, "Edge scene", snapshot);
    check(nativeDocumentVersionFor(inScene) == 31,
          "An edge stroke in a saved scene's style is schema 31");
}

// Schema 31 round-trips to the same bytes, and the checked-in fixture reopens bit for bit.
void roundTrip() {
    const auto doc = styleFixture();
    const auto bytes = encodeContainer(doc);
    const auto reopened = decodeContainer(bytes);
    check(reopened.style().edgeStroke && *reopened.style().edgeStroke == *doc.style().edgeStroke,
          "The edge stroke survives the container round trip");
    check(encodeContainer(reopened) == bytes, "Schema 31 re-encodes bit for bit");
    check(encodeDocument(reopened) == encodeDocument(doc), "The document records are identical");
    check(reopened.assets().size() == 1 &&
              assetByteArray(reopened.assets().begin()->second->payload) == texturePng(),
          "The embedded texture round-trips byte for byte");

    const auto fixture =
        read(QString::fromLocal8Bit(SOURCE_DIR) + "/tests/fixtures/edge-styles-v31.sketchyup");
    check(containerManifest(fixture)["chunks"].toArray()[0].toObject()["encoding"] == "json-v31" &&
              hasFeature(fixture, "edge-styles-v1"),
          "The fixture is schema 31 with edge-styles-v1");
    check(encodeContainer(decodeContainer(fixture)) == bytes,
          "The schema-31 fixture reopens bit for bit");
}

// A document without an edge stroke writes what it wrote before: clearing the stroke gives
// the same bytes as a document that never had one.
void lowestSchemaWriter() {
    Document never;
    never.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
    Document cleared = stable(never);
    auto style = cleared.style();
    style.edgeStroke = EdgeStroke{};
    cleared.setStyle(style);
    style.edgeStroke.reset();
    cleared.setStyle(style);
    // The two edits raised the revision, so the comparison leaves it out; every other byte of the
    // payload must match.
    const auto withoutRevision = [](const Document &doc) {
        auto tree = QJsonDocument::fromJson(encodeDocument(stable(doc))).object();
        tree.remove("revision");
        return QJsonDocument(tree).toJson(QJsonDocument::Compact);
    };
    check(withoutRevision(cleared) == withoutRevision(never) &&
              !encodeDocument(cleared).contains("edgeStroke") &&
              nativeDocumentVersionFor(cleared) == 26,
          "Clearing the edge stroke gives the bytes of a document that never had one");
}

// The decoders refuse every out-of-bound value before storing anything.
void refusals() {
    const auto stroke = [](QJsonObject overrides, const QString &drop = {}) {
        QJsonObject object{{"width", 2}, {"opacity", 1}, {"dash", "solid"}, {"texture", "0"}};
        for (auto it = overrides.begin(); it != overrides.end(); ++it)
            object[it.key()] = it.value();
        if (!drop.isEmpty())
            object.remove(drop);
        return object;
    };
    for (const auto &bad :
         {stroke({{"width", 0.5}}), stroke({{"width", 8.5}}), stroke({{"width", "2"}}),
          stroke({{"opacity", 0}}), stroke({{"opacity", 1.01}}), stroke({{"dash", "wavy"}}),
          stroke({{"texture", "01"}}), stroke({{"texture", "-1"}}), stroke({{"texture", 7}}),
          stroke({}, "texture"), stroke({{"extra", true}})})
        check(!rejection([&] { decodeEdgeStroke(bad); }).empty(),
              "A malformed edge stroke refuses");
    check(decodeEdgeStroke(stroke({{"width", 8}, {"opacity", .01}, {"dash", "dashDot"}})).dash ==
              EdgeDash::DashDot,
          "The bounds are inclusive and dashDot is a named pattern");

    // A schema-30 style may not carry the sub-record, and a schema-31 style needs it allowed.
    const auto encoded = encodeModelStyle([] {
        auto style = ModelStyle{};
        style.edgeStroke = EdgeStroke{};
        return style;
    }());
    check(!rejection([&] { decodeModelStyle(encoded, false); }).empty(),
          "A style with an edge stroke refuses where schema 31 is not in force");
    check(decodeModelStyle(encoded, true).edgeStroke.has_value(),
          "A style with an edge stroke decodes in schema 31");

    // A document labelled schema 30 with the sub-record, and a schema-31 payload that does not
    // list edge-styles-v1, both refuse.
    const auto fixture = styleFixture();
    auto json = QJsonDocument::fromJson(encodeDocument(fixture)).object();
    json["version"] = 30;
    check(!rejection([&] {
               decodeDocument(QJsonDocument(json).toJson(QJsonDocument::Compact));
           }).empty(),
          "A schema-30 document with an edge stroke refuses");
    auto head = containerManifest(encodeContainer(fixture));
    auto without = QJsonArray{};
    for (const auto &feature : head["requiredFeatures"].toArray())
        if (feature != "edge-styles-v1")
            without.append(feature);
    head["requiredFeatures"] = without;
    check(!rejection([&] {
               decodeContainer(withContainerManifest(encodeContainer(fixture), head));
           }).empty(),
          "A schema-31 payload without edge-styles-v1 refuses");

    // The texture must name an asset of the document; an unknown id is never stored.
    Document unknown = stable(fixture);
    auto dangling = unknown.style();
    dangling.edgeStroke->texture = 99;
    check(!rejection([&] { unknown.setStyle(dangling); }).empty(),
          "An edge texture naming an unknown asset is refused");
    auto tree = QJsonDocument::fromJson(encodeDocument(fixture)).object();
    auto style = tree["style"].toObject();
    auto edge = style["edgeStroke"].toObject();
    edge["texture"] = "99";
    style["edgeStroke"] = edge;
    tree["style"] = style;
    check(!rejection([&] {
               decodeDocument(QJsonDocument(tree).toJson(QJsonDocument::Compact));
           }).empty(),
          "A decoded document with a dangling edge texture refuses");
}

// The `.sketchystyle` envelope: a bit-for-bit round trip, the header layout, and refusals.
void styleFileFormat() {
    const auto fixture = styleFixture();
    const auto file = encodeStyleBundle(StyleBundle{fixtureMetadata(), fixture.style(),
                                                    *fixture.assets().begin()->second,
                                                    StyleTextureStatus::Present});
    const auto decoded = decodeStyleBundle(file);
    check(decoded.textureStatus == StyleTextureStatus::Present &&
              decoded.style == fixture.style() && decoded.metadata.name == fixtureMetadata().name,
          "The style file decodes to the same record and texture");
    check(encodeStyleBundle(decoded) == file, "The style file re-encodes bit for bit");
    const auto manifest = styleManifest(file);
    check(manifest["kind"] == "style" && !manifest.contains("model") &&
              !manifest.contains("thumbnail") && !manifest.contains("defaultScene"),
          "A style manifest has kind style and no model, thumbnail or scene");
    check(qFromLittleEndian<quint32>(file.constData() + 20) == 0, "A style file has no thumbnail");
    check(stylePayload(file) == texturePng(), "The texture is the only payload");

    check(!rejection([&] {
               decodeStyleBundle(styleEnvelope(manifest, stylePayload(file), 1));
           }).empty(),
          "A nonzero thumbnail length refuses");
    check(!rejection([&] { decodeStyleBundle(file + QByteArray(1, '\0')); }).empty(),
          "Trailing data refuses");

    auto wrongKind = manifest;
    wrongKind["kind"] = "template";
    check(!rejection([&] {
               decodeStyleBundle(styleEnvelope(wrongKind, stylePayload(file)));
           }).empty(),
          "A template manifest is not read as a style");
    check(!rejection([&] {
               decodeTemplateBundle(styleEnvelope(wrongKind, stylePayload(file)));
           }).empty(),
          "The template reader refuses a style bundle");

    auto gif = manifest;
    auto gifTexture = gif["texture"].toObject();
    gifTexture["mediaType"] = "image/gif";
    gif["texture"] = gifTexture;
    check(!rejection([&] { decodeStyleBundle(styleEnvelope(gif, stylePayload(file))); }).empty(),
          "A GIF texture refuses before it can be stored");

    const QByteArray notImage("not a picture");
    auto notPng = manifest;
    auto descriptor = notPng["texture"].toObject();
    descriptor["bytes"] = notImage.size();
    descriptor["sha256"] = sha256(notImage);
    notPng["texture"] = descriptor;
    check(!rejection([&] { decodeStyleBundle(styleEnvelope(notPng, notImage)); }).empty(),
          "An undecodable texture refuses");

    auto unnamed = manifest;
    unnamed["texture"] = QJsonValue::Null;
    check(!rejection([&] { decodeStyleBundle(styleEnvelope(unnamed, {})); }).empty(),
          "A style that names a texture the file does not carry refuses");
}

// A missing texture is reported and never replaced; a hash mismatch is reported the same way.
void missingAndMismatchedTextures() {
    const auto fixture = styleFixture();
    const auto good = encodeStyleBundle(StyleBundle{fixtureMetadata(), fixture.style(),
                                                    *fixture.assets().begin()->second,
                                                    StyleTextureStatus::Present});
    auto manifest = styleManifest(good);

    auto missing = manifest;
    auto descriptor = missing["texture"].toObject();
    descriptor["missing"] = true;
    descriptor["bytes"] = 0;
    descriptor["sha256"] = QJsonValue::Null;
    missing["texture"] = descriptor;
    const auto missingFile = styleEnvelope(missing, {});
    const auto missingBundle = decodeStyleBundle(missingFile);
    check(missingBundle.textureStatus == StyleTextureStatus::Missing &&
              !missingBundle.texture.payload && missingBundle.texture.id == 1,
          "A file that records the texture as missing reads as missing");
    check(!rejection([&] { encodeStyleBundle(missingBundle); }).empty(),
          "A missing texture is never written into a style file");

    auto tampered = stylePayload(good);
    tampered[0] = char(tampered[0] ^ 0x5A);
    const auto mismatch = decodeStyleBundle(styleEnvelope(manifest, tampered));
    check(mismatch.textureStatus == StyleTextureStatus::HashMismatch && !mismatch.texture.payload,
          "A texture whose bytes do not match the recorded hash is reported");

    // Applying either one refuses by name and leaves the document alone.
    Document target = stable(fixture);
    auto before = target.style();
    const auto revision = target.revision();
    const auto history = target.history().total;
    const auto reportMissing = rejection([&] {
        executeBatch(
            target,
            batch(target, {QJsonObject{{"command", "style.apply"},
                                       {"data", QString::fromLatin1(missingFile.toBase64())}}}));
    });
    check(reportMissing.find("missing") != std::string::npos &&
              reportMissing.find("not applied") != std::string::npos,
          "A missing texture refuses the style and names it: " + reportMissing);
    const auto reportMismatch = rejection([&] {
        executeBatch(
            target,
            batch(target,
                  {QJsonObject{{"command", "style.apply"},
                               {"data", QString::fromLatin1(
                                            styleEnvelope(manifest, tampered).toBase64())}}}));
    });
    check(reportMismatch.find("hash-mismatched") != std::string::npos,
          "A mismatched texture refuses the style and names it: " + reportMismatch);
    check(target.revision() == revision && target.history().total == history &&
              target.style() == before,
          "A refused style leaves the document, revision and history unchanged");

    // Export refuses to write a file whose texture the document does not hold.
    Document lost;
    const auto lostTexture = createAsset(lost, "Gone", "image/png");
    auto lostStyle = lost.style();
    lostStyle.edgeStroke = EdgeStroke{2, 1, EdgeDash::Solid, lostTexture};
    lost.setStyle(lostStyle);
    check(rejection([&] { exportStyleBundle(lost, fixtureMetadata()); }).find("missing") !=
              std::string::npos,
          "Export refuses a document whose edge texture is missing");
}

// Apply is one undoable edit and reuses an identical texture; export leaves the document alone.
void applyAndExport() {
    const auto fixture = styleFixture();
    const auto file = exportStyleBundle(fixture, fixtureMetadata());
    const auto decoded = decodeStyleBundle(file);
    check(decoded.textureStatus == StyleTextureStatus::Present && decoded.style == fixture.style(),
          "Export captures the style record and its texture");

    Document target;
    target.addFace({{{0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {0, 4, 0}}});
    const auto revision = target.revision();
    const auto history = target.history().total;
    const auto result = executeBatch(
        target, batch(target, {QJsonObject{{"command", "style.apply"},
                                           {"data", QString::fromLatin1(file.toBase64())}}}));
    check(result["styleOperations"].toArray()[0].toObject()["status"] == "applied",
          "Applying a style reports applied");
    check(target.history().total == history + 1, "Applying a style is one undoable edit");
    check(target.style().edgeStroke && target.style().edgeStroke->texture &&
              target.assets().size() == 1 &&
              assetByteArray(target.assets().begin()->second->payload) == texturePng(),
          "The applied texture is a managed asset with the file's bytes");
    check(target.assets().contains(target.style().edgeStroke->texture),
          "The applied stroke names the document's asset");

    // Applying the same style again changes nothing, so the batch has no committed change and is
    // refused as every other no-op batch is.
    check(rejection([&] {
              executeBatch(
                  target,
                  batch(target, {QJsonObject{{"command", "style.apply"},
                                             {"data", QString::fromLatin1(file.toBase64())}}}));
          }).find("no committed changes") != std::string::npos &&
              target.history().total == history + 1,
          "Applying the same style again is a no-op and writes no history");

    // An identical texture already in the document is reused, not duplicated.
    Document shared;
    const auto existing =
        createAsset(shared, "Other name", "image/png", assetPayload(texturePng()));
    const auto sharedBefore = shared.assets().size();
    const auto sharedResult = executeBatch(
        shared, batch(shared, {QJsonObject{{"command", "style.apply"},
                                           {"data", QString::fromLatin1(file.toBase64())}}}));
    check(shared.assets().size() == sharedBefore &&
              shared.style().edgeStroke->texture == existing &&
              sharedResult["styleOperations"].toArray()[0].toObject()["assetCreated"] == false,
          "An identical texture is reused by byte equality, not duplicated");

    // Undo removes the style and its texture in the one step.
    target.undo();
    check(target.assets().empty() && !target.style().edgeStroke,
          "Undo restores the style and removes the texture it added");
    target.redo();
    check(target.style().edgeStroke && target.assets().size() == 1, "Redo restores both");

    // Export publishes nothing and changes no revision.
    Document exporting = stable(fixture);
    const auto exportRevision = exporting.revision();
    QJsonObject query{{"apiVersion", 1},
                      {"documentId", QString::fromStdString(exporting.identity())},
                      {"expectedRevision", QString::number(exporting.revision())},
                      {"query", "style.export"},
                      {"name", "Sketch edges"},
                      {"labels", QJsonArray{"sketch"}}};
    const auto exported = inspectDocument(exporting, query)["data"].toObject();
    check(exporting.revision() == exportRevision && exporting.history().total == 0 &&
              exported["name"] == "Sketch edges",
          "Export leaves the document, its revision and its history unchanged");
    const auto data = QByteArray::fromBase64(exported["data"].toString().toLatin1());
    check(decodeStyleBundle(data).textureStatus == StyleTextureStatus::Present,
          "The exported command data decodes to a style with its texture");
}

// The style needs nothing outside its file: a copy in another folder applies to an empty
// document with no missing resource, the same record and the same texture bytes.
void noExternalDependency() {
    QTemporaryDir elsewhere;
    check(elsewhere.isValid(), "A temporary directory");
    const auto path = elsewhere.filePath("moved-to-a-new-folder.sketchystyle");
    writeStyleBundle(exportStyleBundle(styleFixture(), fixtureMetadata()), path);
    const auto loaded = loadStyleBundle(path);
    check(loaded.textureStatus == StyleTextureStatus::Present,
          "The moved style reads with its texture present");
    check(!styleManifest(read(path)).value("texture").toObject().contains("path") &&
              !read(path).contains(elsewhere.path().toUtf8()),
          "A style file stores no path");
    Document empty;
    const auto result = executeBatch(
        empty, batch(empty, {QJsonObject{{"command", "style.apply"},
                                         {"data", QString::fromLatin1(read(path).toBase64())}}}));
    check(result["styleOperations"].toArray()[0].toObject()["status"] == "applied" &&
              empty.assets().size() == 1 && empty.style().edgeStroke &&
              empty.assets().begin()->second->payload &&
              sha256(assetByteArray(empty.assets().begin()->second->payload)) ==
                  sha256(texturePng()),
          "A style applies to an empty document with its texture hash unchanged");
    check(encodeStyleBundle(loaded) == read(path), "The moved file re-encodes bit for bit");
    writeStyleBundle(read(path), elsewhere.filePath("copy.sketchystyle"));
    check(!rejection([&] { writeStyleBundle(read(path), path); }).empty(),
          "A style file is never replaced by a publish");
}

// A style folder lists only .sketchystyle files, decodes each one, and reports a bad one by name.
void libraryFolder() {
    QTemporaryDir folder;
    check(folder.isValid(), "A temporary directory");
    write(folder.filePath("good.sketchystyle"),
          exportStyleBundle(styleFixture(), fixtureMetadata()));
    write(folder.filePath("bad.sketchystyle"), QByteArray("garbage"));
    write(folder.filePath("other.sketchylib"), QByteArray("SKYLIB", 6));
    const auto library = scanStyleDirectory(folder.path());
    check(library.entries.size() == 2, "The scan lists the two style files and no library file");
    // Entries sort by name, so the bad file comes first; find each by its file.
    const auto &good =
        library.entries[library.entries[0].path.endsWith("good.sketchystyle") ? 0 : 1];
    const auto &bad = library.entries[library.entries[0].path.endsWith("bad.sketchystyle") ? 0 : 1];
    check(good.error.isEmpty() && good.metadata.name == "Sketch edges" &&
              good.textureStatus == StyleTextureStatus::Present,
          "A valid style reports its metadata and texture status");
    check(!bad.error.isEmpty() && bad.path.endsWith("bad.sketchystyle"),
          "A bad style reports its error by file");
}

// The style commands: canonical base64 only, bounded, and refused without changing anything.
void commands() {
    Document doc = stable(styleFixture());
    const auto history = doc.history().total;
    const auto refused = rejection([&] {
        executeBatch(doc, batch(doc, {QJsonObject{{"command", "style.apply"}, {"data", "%%%"}}}));
    });
    check(!refused.empty() && doc.history().total == history,
          "Non-base64 style data is refused without a change");
    check(!rejection([&] {
               executeBatch(doc,
                            batch(doc, {QJsonObject{{"command", "style.apply"},
                                                    {"data", QString::fromLatin1(
                                                                 QByteArray("abc").toBase64())}}}));
           }).empty(),
          "Data that is not a style file is refused");
    bool apply = false;
    for (const auto &entry : commandCatalog())
        apply = apply || entry.toObject()["name"] == "style.apply";
    check(apply, "The command catalogue lists style.apply");
}

// The released reader refuses the schema-31 fixture unchanged (SKETCHYUP_PRIOR_CLI is set by the
// package acceptance; see ADR 0174 §6 and packaging/arch/verify-package.sh).
void priorRelease(const QString &directory) {
    const auto prior = qEnvironmentVariable("SKETCHYUP_PRIOR_CLI");
    if (prior.isEmpty()) {
        std::cout << "SKETCHYUP_PRIOR_CLI not set; prior-release refusal not exercised\n";
        return;
    }
    const auto path = directory + "/prior-edge-styles.sketchyup";
    const auto bytes = encodeContainer(styleFixture());
    write(path, bytes);
    QProcess process;
    process.start(prior, {"--validate-native", path});
    check(process.waitForFinished(30000) && process.exitCode() != 0 && read(path) == bytes,
          "The prior release refuses a schema-31 file unchanged");
    std::cout << "prior release refusal: " << process.readAllStandardError().trimmed().toStdString()
              << '\n';
}

} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    // --write-fixture PATH and --write-style PATH regenerate the checked-in fixtures.
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) == "--write-fixture") {
        write(QString::fromLocal8Bit(argv[2]), encodeContainer(styleFixture()));
        return 0;
    }
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) == "--write-style") {
        write(QString::fromLocal8Bit(argv[2]),
              exportStyleBundle(styleFixture(), fixtureMetadata()));
        return 0;
    }
    try {
        schemaSelection();
        roundTrip();
        lowestSchemaWriter();
        refusals();
        styleFileFormat();
        missingAndMismatchedTextures();
        applyAndExport();
        noExternalDependency();
        libraryFolder();
        commands();
        // The checked-in style reads as a present, bit-for-bit style file.
        const auto style =
            read(QString::fromLocal8Bit(SOURCE_DIR) + "/tests/fixtures/edge-style-v1.sketchystyle");
        check(encodeStyleBundle(decodeStyleBundle(style)) == style,
              "The checked-in style file reopens bit for bit");
        QTemporaryDir dir;
        check(dir.isValid(), "A temporary directory");
        priorRelease(dir.path());
        std::cout << "style bundle io: all checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "style bundle test failed: " << error.what() << '\n';
        return 1;
    }
}
