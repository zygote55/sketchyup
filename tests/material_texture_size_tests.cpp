// R100.b2 (ADR 0174 section 3): a material's real-world texture size. The implicit face mapping
// uses it, the document carries it as schema 33 with material-texture-size-v1, and the material
// commands, materials.describe and catalogs follow the same rule. The released-reader refusal runs
// when SKETCHYUP_PRIOR_CLI is set (package acceptance); the S6 .skp check is in skp_mapping_tests
// and the GLB and OBJ UV checks are in texture_export_tests.
#include "automation/commands.hpp"
#include "catalog_fixtures.hpp"
#include "core/face_textures.hpp"
#include "core/materials.hpp"
#include "core/terrain.hpp"
#include "geo/geographic.hpp"
#include "io/catalog_insert.hpp"
#include "io/document_io.hpp"
#include "io/native_format.hpp"
#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QtEndian>
#include <cmath>
#include <iostream>
#include <optional>

using namespace sketchy;

namespace {

void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}

void near(double actual, double expected, const std::string &message) {
    check(std::abs(actual - expected) <= 1e-9,
          message + ": " + std::to_string(actual) + " != " + std::to_string(expected));
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
    check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "Write " + path.toStdString());
    check(file.write(bytes) == bytes.size(), "Write all bytes");
}

QJsonObject containerManifest(const QByteArray &container) {
    const auto length = qFromLittleEndian<quint32>(container.constData() + 12);
    return QJsonDocument::fromJson(container.mid(16, length)).object();
}

bool hasFeature(const QByteArray &container, const char *feature) {
    return containerManifest(container)["requiredFeatures"].toArray().contains(
        QString::fromLatin1(feature));
}

QString documentChunkEncoding(const QByteArray &container) {
    return containerManifest(container)["chunks"].toArray()[0].toObject()["encoding"].toString();
}

// A fixed identity, so the fixture encodes the same bytes on every rebuild.
Document stable(Document doc) {
    auto json = QJsonDocument::fromJson(encodeDocument(doc)).object();
    json["documentId"] = QStringLiteral("00000000000000000000000000000b32");
    return decodeDocument(QJsonDocument(json).toJson(QJsonDocument::Compact));
}

// The body and its one face: a horizontal 2 m square on z = 0.
std::pair<Id, Id> horizontalSquare(Document &doc) {
    const auto body = doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}}});
    return {body, doc.bodies().at(body)->surface.faces.begin()->first};
}

// The schema-33 fixture: a textured tile at a non-square size, a size-free material and a
// square size, with the tile assigned to the front of one face.
Document sizedFixture() {
    Document doc;
    const auto [body, face] = horizontalSquare(doc);
    const auto brick = createMaterial(doc, "Brick", {.61f, .29f, .21f}, 1, 0, TextureSize{.9, .9});
    createMaterial(doc, "Plain", {.5f, .5f, .5f});
    createMaterial(doc, "Wool", {.8f, .8f, .8f}, 1, 0, TextureSize{.27, .28});
    assignMaterial(doc, body, face, brick, true, false);
    return doc;
}

// Commands run as one batch each, so a batch that fails leaves the model unchanged.
QJsonObject batch(Document &doc, const QJsonArray &commands) {
    return executeBatch(doc, {{"apiVersion", 1},
                              {"documentId", QString::fromStdString(doc.identity())},
                              {"expectedRevision", QString::number(doc.revision())},
                              {"commands", commands}});
}

Id materialNamed(const Document &doc, const std::string &name) {
    for (const auto &[id, material] : doc.materials())
        if (material->name == name)
            return id;
    throw std::runtime_error("No material named " + name);
}

void validation() {
    validateTextureSize({minimumTextureSize, maximumTextureSize});
    validateTextureSize({.9, .27});
    for (const auto size : {TextureSize{minimumTextureSize / 2, 1}, TextureSize{1, 100.5},
                            TextureSize{std::nan(""), 1}, TextureSize{1, HUGE_VAL}}) {
        check(!rejection([&] { validateTextureSize(size); }).empty(),
              "A texture size outside 1 cm to 100 m is refused");
    }
    Document doc;
    const auto before = encodeDocument(doc);
    check(!rejection([&] {
               createMaterial(doc, "Tiny", {1, 1, 1}, 1, 0, TextureSize{.001, 1});
           }).empty(),
          "createMaterial refuses a texture size below the bound");
    check(encodeDocument(doc) == before, "A refused material leaves the document unchanged");
    const auto dictionary = QJsonObject{{"width", .9}, {"height", .9}};
    check(decodeTextureSize(dictionary).width == .9, "The width decodes");
    for (const auto &bad : {QJsonValue(QJsonObject{{"width", .001}, {"height", 1}}),
                            QJsonValue(QJsonObject{{"width", 1}}),
                            QJsonValue(QJsonObject{{"width", 1}, {"height", 1}, {"depth", 1}}),
                            QJsonValue(QJsonObject{{"width", "1"}, {"height", 1}}),
                            QJsonValue(QJsonValue::Null), QJsonValue(1.0)}) {
        check(!rejection([&] { decodeTextureSize(bad); }).empty(),
              "The size decoder refuses an invalid value");
    }
}

// Implicit mapping: one repeat covers the side's material size. A sized mapping is the one-metre
// mapping with each gradient divided by the size, on any face.
void implicitMapping() {
    Document doc;
    const auto [body, face] = horizontalSquare(doc);
    const auto &record = *doc.bodies().at(body);
    const auto unit = effectiveFaceTextureMapping(record, face, false, doc.materials());
    check(unit == TextureMapping{}, "An unsized face keeps the canonical one-metre mapping");

    const auto wide = createMaterial(doc, "Wide", {1, 1, 1}, 1, 0, TextureSize{.9, .5});
    assignMaterial(doc, body, face, wide, true, false);
    const auto front =
        effectiveFaceTextureMapping(*doc.bodies().at(body), face, false, doc.materials());
    const auto point = Vec3{.9, .5, 0};
    near(front.coordinates(point).u, 1, "The front repeat covers the width");
    near(front.coordinates(point).v, 1, "The front repeat covers the height");
    const auto back =
        effectiveFaceTextureMapping(*doc.bodies().at(body), face, true, doc.materials());
    near(back.coordinates(point).u, .9, "The back, with no material, keeps one metre");
    near(back.coordinates(point).v, .5, "The back, with no material, keeps one metre");

    // A sized back material scales only the back; the front keeps its own size.
    const auto backSized = createMaterial(doc, "Back", {1, 1, 1}, 1, 0, TextureSize{2, 4});
    assignMaterial(doc, body, face, backSized, false, true);
    const auto backNow =
        effectiveFaceTextureMapping(*doc.bodies().at(body), face, true, doc.materials());
    near(backNow.coordinates(point).u, .45, "The back size scales the back repeat");
    near(backNow.coordinates(point).v, .125, "The back size scales the back repeat in V");
    near(effectiveFaceTextureMapping(*doc.bodies().at(body), face, false, doc.materials())
             .coordinates(point)
             .u,
         1, "The front size is independent of the back size");

    // Explicit side records take precedence over any size, and clearing them restores it.
    assignTextureMapping(doc, body, face, TextureMapping{}, true, false);
    const auto explicitFront =
        effectiveFaceTextureMapping(*doc.bodies().at(body), face, false, doc.materials());
    check(explicitFront == TextureMapping{}, "An explicit mapping ignores the material size");
    assignTextureMapping(doc, body, face, std::nullopt, true, false);
    near(effectiveFaceTextureMapping(*doc.bodies().at(body), face, false, doc.materials())
             .coordinates(point)
             .u,
         1, "Clearing the explicit mapping restores the material size");

    // A vertical face: the sized gradients are the one-metre ones divided by the size.
    Document wall;
    const auto vertical = wall.addFace({{{2, 3, 4}, {4, 3, 4}, {4, 3, 6}, {2, 3, 6}}});
    const auto wf = wall.bodies().at(vertical)->surface.faces.begin()->first;
    const auto plain =
        effectiveFaceTextureMapping(*wall.bodies().at(vertical), wf, false, wall.materials());
    const auto tile = createMaterial(wall, "Tile", {1, 1, 1}, 1, 0, TextureSize{.5, .25});
    assignMaterial(wall, vertical, wf, tile, true, false);
    const auto sized =
        effectiveFaceTextureMapping(*wall.bodies().at(vertical), wf, false, wall.materials());
    for (const auto p : {Vec3{2.5, 3, 5.5}, Vec3{3, 3, 4.25}, Vec3{3.9, 3, 5.7}}) {
        near(sized.coordinates(p).u, plain.coordinates(p).u / .5, "A wall scales in U");
        near(sized.coordinates(p).v, plain.coordinates(p).v / .25, "A wall scales in V");
    }
}

// The schema-33 writer, the lowest-schema writer and the feature gate.
void schemaAndRoundTrip() {
    const auto sized = sizedFixture();
    const auto bytes = encodeContainer(sized);
    check(nativeDocumentVersionFor(sized) == 33 && nativeMaterialTextureSizeDocumentVersion == 33,
          "A sized material needs schema 33");
    check(documentChunkEncoding(bytes) == "json-v33" &&
              hasFeature(bytes, "material-texture-size-v1") && hasFeature(bytes, "materials-v1"),
          "A sized material writes json-v33 with material-texture-size-v1");
    check(QJsonDocument::fromJson(encodeDocument(sized)).object()["version"] == 33,
          "The payload is version 33");
    const auto reopened = decodeContainer(bytes);
    check(reopened.materials().size() == 3, "The three materials reopen");
    check(reopened.materials().at(materialNamed(reopened, "Brick"))->textureSize ==
                  TextureSize{.9, .9} &&
              !reopened.materials().at(materialNamed(reopened, "Plain"))->textureSize &&
              reopened.materials().at(materialNamed(reopened, "Wool"))->textureSize ==
                  TextureSize{.27, .28},
          "Each material keeps its own size, and an unsized one keeps none");
    check(encodeContainer(reopened) == bytes, "A schema-33 container re-encodes bit for bit");

    // A document without a sized material writes what it wrote before.
    Document plain;
    horizontalSquare(plain);
    createMaterial(plain, "Plain", {.5f, .5f, .5f});
    const auto plainBytes = encodeContainer(plain);
    check(nativeDocumentVersionFor(plain) == 26 &&
              !hasFeature(plainBytes, "material-texture-size-v1") &&
              !encodeDocument(plain).contains("textureSize"),
          "An unsized material keeps the schema and the bytes of before");

    // Clearing the only size gives the bytes of a document that never had one. The revision is
    // left out, because the two edits raised it.
    const auto withoutRevision = [](const Document &doc) {
        auto tree = QJsonDocument::fromJson(encodeDocument(stable(doc))).object();
        tree.remove("revision");
        return QJsonDocument(tree).toJson(QJsonDocument::Compact);
    };
    Document onlyBrick;
    const auto [solo, soloFace] = horizontalSquare(onlyBrick);
    const auto soloBrick =
        createMaterial(onlyBrick, "Brick", {.61f, .29f, .21f}, 1, 0, TextureSize{.9, .9});
    assignMaterial(onlyBrick, solo, soloFace, soloBrick, true, false);
    editMaterial(onlyBrick, soloBrick, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                 std::optional<TextureSize>{});
    Document neverSolo;
    const auto [soloNever, soloNeverFace] = horizontalSquare(neverSolo);
    const auto soloNeverBrick = createMaterial(neverSolo, "Brick", {.61f, .29f, .21f});
    assignMaterial(neverSolo, soloNever, soloNeverFace, soloNeverBrick, true, false);
    check(withoutRevision(onlyBrick) == withoutRevision(neverSolo) &&
              nativeDocumentVersionFor(onlyBrick) == 26 &&
              !encodeDocument(onlyBrick).contains("textureSize"),
          "Clearing the only size gives the bytes of a material that never had one");

    // A version-31 payload must not carry a size: the reader refuses it.
    auto tree = QJsonDocument::fromJson(encodeDocument(sized)).object();
    tree["version"] = 31;
    check(!rejection([&] {
               decodeDocument(QJsonDocument(tree).toJson(QJsonDocument::Compact));
           }).empty(),
          "A schema-31 document with a material size is refused");
}

// Commands: create, edit and clear the size as one undo step each, and describe reports it.
void commandsAndUndo() {
    Document doc;
    horizontalSquare(doc);
    const auto before = doc.history().total;
    batch(doc, {QJsonObject{{"command", "material.create"},
                            {"name", "Brick"},
                            {"color", QJsonArray{.6, .3, .2}},
                            {"textureSize", QJsonObject{{"width", .9}, {"height", .9}}}}});
    check(doc.history().total == before + 1, "Creating a sized material is one undo step");
    const auto brick = materialNamed(doc, "Brick");
    check(doc.materials().at(brick)->textureSize == TextureSize{.9, .9},
          "material.create stores the size");

    const auto afterCreate = doc.history().total;
    batch(doc, {QJsonObject{{"command", "material.edit"},
                            {"material", QString::number(brick)},
                            {"textureSize", QJsonObject{{"width", .5}, {"height", .25}}}}});
    check(doc.history().total == afterCreate + 1, "Resizing a material is one undo step");
    check(doc.materials().at(brick)->textureSize == TextureSize{.5, .25},
          "material.edit sets the size");
    doc.undo();
    check(doc.materials().at(brick)->textureSize == TextureSize{.9, .9},
          "Undoing a resize restores the previous size");

    batch(doc, {QJsonObject{{"command", "material.edit"},
                            {"material", QString::number(brick)},
                            {"textureSize", QJsonValue(QJsonValue::Null)}}});
    check(!doc.materials().at(brick)->textureSize, "A null size clears the size");

    // An edit that names no size field leaves the size as it is.
    batch(doc, {QJsonObject{{"command", "material.edit"},
                            {"material", QString::number(brick)},
                            {"opacity", .5}}});
    check(!doc.materials().at(brick)->textureSize && doc.materials().at(brick)->opacity == .5f,
          "An edit without a size field keeps the size");

    const auto beforeRefusal = encodeDocument(doc);
    check(!rejection([&] {
               batch(doc,
                     {QJsonObject{{"command", "material.edit"},
                                  {"material", QString::number(brick)},
                                  {"textureSize", QJsonObject{{"width", .001}, {"height", 1}}}}});
           }).empty(),
          "An out-of-bound size is refused");
    check(encodeDocument(doc) == beforeRefusal && !doc.materials().at(brick)->textureSize,
          "A refused size leaves the material unchanged");
    check(!rejection([&] {
               batch(doc, {QJsonObject{{"command", "material.edit"},
                                       {"material", QString::number(brick)},
                                       {"textureSize", "1 m"}}});
           }).empty(),
          "A texture size that is not an object or null is refused");
    check(!rejection([&] {
               batch(doc, {QJsonObject{{"command", "material.edit"},
                                       {"material", QString::number(brick)},
                                       {"textureSize",
                                        QJsonObject{{"width", 1}, {"height", 1}, {"x", 1}}}}});
           }).empty(),
          "A texture size with an extra field is refused");

    batch(doc, {QJsonObject{{"command", "material.edit"},
                            {"material", QString::number(brick)},
                            {"textureSize", QJsonObject{{"width", 2}, {"height", 3}}}}});
    const auto described =
        executeQuery(doc, {{"query", "materials.describe"}})["materials"].toArray();
    bool found = false;
    for (const auto &entry : described) {
        const auto record = entry.toObject();
        if (record["name"] == "Brick") {
            found = true;
            check(record["textureSize"].toObject()["width"].toDouble() == 2 &&
                      record["textureSize"].toObject()["height"].toDouble() == 3,
                  "materials.describe reports the size");
        }
    }
    check(found, "materials.describe lists the material");
    // Describe is the same shape for an unsized material: null, not missing.
    batch(doc, {QJsonObject{{"command", "material.create"},
                            {"name", "Plain"},
                            {"color", QJsonArray{.5, .5, .5}}}});
    for (const auto &entry :
         executeQuery(doc, {{"query", "materials.describe"}})["materials"].toArray()) {
        if (entry.toObject()["name"] == "Plain")
            check(entry.toObject().contains("textureSize") &&
                      entry.toObject()["textureSize"].isNull(),
                  "An unsized material reports a null size");
    }
}

// Catalogs carry the size; a reused material must match it too (ADR 0131, R100.b2).
void catalogCarriesSize() {
    CatalogBuilder builder(catalog_fixture::metadata());
    builder.addMaterial("Brick", "", {"Brick", {.6f, .3f, .2f}, 1, 0, TextureSize{.9, .9}});
    builder.addMaterial("Plain", "", {"Plain", {.8f, .8f, .8f}, 1, 0});
    const auto package = decodeCatalog(builder.encode());
    Document doc;
    const auto imported = importCatalogEntries(doc, package, {"Brick", "Plain"});
    check(imported.reusedMaterials == 0, "Two new materials are not reused");
    check(doc.materials().at(materialNamed(doc, "Brick"))->textureSize == TextureSize{.9, .9},
          "The catalog's size reaches the imported material");
    check(!doc.materials().at(materialNamed(doc, "Plain"))->textureSize,
          "An unsized catalog material imports unsized");

    // The same name with another size is not a reuse: it is a new material.
    Document other;
    createMaterial(other, "Brick", {.6f, .3f, .2f}, 1, 0, TextureSize{.5, .5});
    const auto apart = importCatalogEntries(other, package, {"Brick"});
    check(apart.reusedMaterials == 0 && other.materials().size() == 2,
          "A material with a different size is not reused");
    // The same name, colour, opacity, asset and size is reused. A batch with only a reused entry
    // is an empty edit, so the import also takes the new "Plain" entry.
    Document same;
    createMaterial(same, "Brick", {.6f, .3f, .2f}, 1, 0, TextureSize{.9, .9});
    const auto reused = importCatalogEntries(same, package, {"Brick", "Plain"});
    check(reused.reusedMaterials == 1 && same.materials().size() == 2,
          "A material with the same size is reused");
    check(!rejection([&] {
               CatalogBuilder bad(catalog_fixture::metadata());
               bad.addMaterial("Bad", "", {"Bad", {1, 1, 1}, 1, 0, TextureSize{.001, 1}});
           }).empty(),
          "The catalog writer refuses an out-of-bound size");
}

// A site origin and its geographic record at London (UTM zone 30N), so the two agree.
SiteOrigin londonOrigin() {
    const auto grid = geoToUtm({51.5, -0.1}, 30);
    return SiteOrigin{grid.easting, grid.northing, 0, "EPSG:32630", {"", ""}, {}};
}
SiteGeographic londonRecord() {
    const GeoPosition position{51.5, -0.1};
    SiteGeographic record;
    record.latitude = position.latitude;
    record.longitude = position.longitude;
    record.datum = "WGS84";
    record.utmZone = 30;
    record.utmNorth = true;
    record.convergence = utmGridConvergence(position, 30);
    record.verticalReference = "NAVD88";
    record.provenance = {"typed", "surveyed by hand", ""};
    return record;
}

// The two optional features are independent and cumulative (ADR 0174 section 3, ADR 0181 §2.1).
// A sized material alone is schema 33 and requires only material-texture-size-v1; a geographic
// record alone is schema 32 and requires only geographic-site-v1; both are schema 33 and require
// both.
void independentFeatures() {
    const auto sized = sizedFixture();
    const auto sizedBytes = encodeContainer(sized);
    check(nativeDocumentVersionFor(sized) == 33 && documentChunkEncoding(sizedBytes) == "json-v33" &&
              hasFeature(sizedBytes, "material-texture-size-v1") &&
              !hasFeature(sizedBytes, "geographic-site-v1"),
          "A sized material alone is schema 33 and requires only material-texture-size-v1");

    Document geographic;
    setSiteOrigin(geographic, londonOrigin());
    setSiteGeographic(geographic, londonRecord());
    const auto geographicBytes = encodeContainer(geographic);
    check(nativeDocumentVersionFor(geographic) == 32 &&
              documentChunkEncoding(geographicBytes) == "json-v32" &&
              hasFeature(geographicBytes, "geographic-site-v1") &&
              !hasFeature(geographicBytes, "material-texture-size-v1"),
          "A geographic record alone is schema 32 and requires only geographic-site-v1");
    check(decodeContainer(geographicBytes).site() &&
              decodeContainer(geographicBytes).site()->geographic == londonRecord(),
          "A schema-32 file reopens its geographic record");

    Document both = sizedFixture();
    setSiteOrigin(both, londonOrigin());
    setSiteGeographic(both, londonRecord());
    const auto bothBytes = encodeContainer(both);
    check(nativeDocumentVersionFor(both) == 33 && documentChunkEncoding(bothBytes) == "json-v33" &&
              hasFeature(bothBytes, "geographic-site-v1") &&
              hasFeature(bothBytes, "material-texture-size-v1"),
          "A document with both features is schema 33 and requires both");
    const auto reopened = decodeContainer(bothBytes);
    check(reopened.site() && reopened.site()->geographic == londonRecord() &&
              reopened.materials().size() == 3 && encodeContainer(reopened) == bothBytes,
          "A schema-33 file with both features reopens both and re-encodes bit for bit");
}

// The released-reader refusal (ADR 0174 section 6): the schema-33 file is refused unchanged.
void priorRelease(const QString &directory, const QByteArray &bytes) {
    const auto prior = qEnvironmentVariable("SKETCHYUP_PRIOR_CLI");
    if (prior.isEmpty()) {
        std::cout << "SKETCHYUP_PRIOR_CLI not set; prior-release refusal not exercised\n";
        return;
    }
    const auto path = directory + "/prior-material-texture-size.sketchyup";
    write(path, bytes);
    QProcess process;
    process.start(prior, {"--validate-native", path});
    check(process.waitForFinished(30000) && process.exitCode() != 0 && read(path) == bytes,
          "The prior release refuses a schema-33 file unchanged");
    std::cout << "prior release refusal: " << process.readAllStandardError().trimmed().toStdString()
              << '\n';
}

} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    // --write-fixture PATH regenerates the checked-in schema-33 fixture.
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) == "--write-fixture") {
        write(QString::fromLocal8Bit(argv[2]), encodeContainer(stable(sizedFixture())));
        return 0;
    }
    try {
        validation();
        implicitMapping();
        schemaAndRoundTrip();
        independentFeatures();
        commandsAndUndo();
        catalogCarriesSize();
        // The checked-in fixture reads as a schema-33 file and reopens bit for bit.
        const auto fixture = read(QString::fromLocal8Bit(SOURCE_DIR) +
                                  "/tests/fixtures/material-texture-size-v33.sketchyup");
        check(documentChunkEncoding(fixture) == "json-v33" &&
                  hasFeature(fixture, "material-texture-size-v1"),
              "The fixture is schema 33 with material-texture-size-v1");
        check(encodeContainer(decodeContainer(fixture)) == fixture,
              "The schema-33 fixture reopens bit for bit");
        QTemporaryDir dir;
        check(dir.isValid(), "A temporary directory");
        priorRelease(dir.path(), fixture);
        std::cout << "material texture size: all checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "material texture size test failed: " << error.what() << '\n';
        return 1;
    }
}
