// R107.a (ADR 0181 §2.1, §2.6, §2.11 V9): the optional geographic record on the site origin, schema
// 32 with geographic-site-v1, the lowest-schema writer, the site and solar commands, and the
// released reader's refusal of the new schema (measured with SKETCHYUP_PRIOR_CLI).
#include "automation/commands.hpp"
#include "automation/inspection.hpp"
#include "automation/solar_commands.hpp"
#include "core/assets.hpp"
#include "core/reference_images.hpp"
#include "core/terrain.hpp"
#include "io/reference_image_io.hpp"
#include "io/texture_image.hpp"
#include "geo/geographic.hpp"
#include "io/document_io.hpp"
#include "io/native_format.hpp"
#include "io/terrain_io.hpp"
#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace sketchy;

namespace {

void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}

void near(double actual, double expected, double tolerance, const std::string &message) {
    if (!(std::abs(actual - expected) <= tolerance)) {
        std::cerr << message << ": " << actual << " != " << expected << '\n';
        throw std::runtime_error(message);
    }
}

// The message of an action that must be refused.
template <class F> std::string refusal(F action, const std::string &message) {
    try {
        action();
    } catch (const std::exception &error) {
        return error.what();
    }
    throw std::runtime_error(message);
}

bool contains(const std::string &text, const std::string &part) {
    return text.find(part) != std::string::npos;
}

// A site at Greenwich-west London (UTM zone 30N), with its record at the origin's own position.
SiteGeographic londonRecord(const std::string &datum = "WGS84") {
    const GeoPosition position{51.5, -0.1};
    SiteGeographic record;
    record.latitude = position.latitude;
    record.longitude = position.longitude;
    record.datum = datum;
    record.utmZone = 30;
    record.utmNorth = true;
    record.convergence = utmGridConvergence(position, 30);
    record.verticalReference = "NAVD88";
    record.provenance = {"typed", "surveyed by hand", ""};
    return record;
}

// The origin at the record's UTM position, so the two agree.
SiteOrigin londonOrigin() {
    const auto grid = geoToUtm({51.5, -0.1}, 30);
    return SiteOrigin{grid.easting, grid.northing, 0, "EPSG:32630", {"", ""}, {}};
}

Document geographicFixture() {
    Document doc;
    setSiteOrigin(doc, londonOrigin());
    setSiteGeographic(doc, londonRecord());
    return doc;
}

QByteArray read(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "The file opens for reading");
    return file.readAll();
}

void write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(),
          "The file is written");
}

// Schema 32 is the lowest that carries the record, and the record is written only when present.
void schemaVersions() {
    const auto doc = geographicFixture();
    check(nativeDocumentVersionFor(doc) == 32 && nativeGeographicSiteDocumentVersion == 32,
          "A site with a geographic record is schema 32");
    const auto bytes = encodeContainer(doc);
    check(bytes.contains("geographic-site-v1") && bytes.contains("json-v32"),
          "Schema 32 carries geographic-site-v1 and is encoded as json-v32");
    Document plain;
    setSiteOrigin(plain, londonOrigin());
    check(nativeDocumentVersionFor(plain) == nativeTerrainDocumentVersion,
          "A site origin without the record stays schema 29");
    const auto plainBytes = encodeContainer(plain);
    check(!plainBytes.contains("geographic") && plainBytes.contains("json-v29"),
          "Without the record the writer emits no geographic field and the schema-29 encoding");
    check(encodeContainer(decodeContainer(plainBytes)) == plainBytes,
          "A schema-29 site without the record reopens bit for bit");
}

void roundTrip() {
    const auto doc = geographicFixture();
    const auto bytes = encodeContainer(doc);
    const auto reopened = decodeContainer(bytes);
    check(reopened.site() && reopened.site()->geographic &&
              *reopened.site()->geographic == *doc.site()->geographic,
          "The geographic record survives a round trip");
    check(encodeContainer(reopened) == bytes, "Schema 32 re-encodes bit for bit");
}

// The record is refused in a schema-31 site, and accepted in schema 32.
void schemaGate() {
    const auto site = encodeSiteOrigin(geographicFixture().site());
    check(bool(decodeSiteOrigin(site, true)->geographic), "Schema 32 admits the record");
    check(contains(refusal([&] { decodeSiteOrigin(site, false); }, "Schema 31 refuses the record"),
                   "Unexpected fields"),
          "A schema-31 site refuses the geographic field");
}

void validation() {
    // A record 0.5 m from the origin is within the 1 m tolerance; one 2 m away is refused.
    auto close = londonOrigin();
    close.easting += 0.5;
    close.geographic = londonRecord();
    validateSiteOrigin(close);
    auto far = londonOrigin();
    far.easting += 2;
    far.geographic = londonRecord();
    check(contains(refusal([&] { validateSiteOrigin(far); }, "A 2 m offset is refused"),
                   "1 m tolerance"),
          "A record more than 1 m from the origin is refused by name");
    // The record on its own checks its labels and provenance.
    auto vertical = londonRecord();
    vertical.verticalReference = "NGVD29";
    check(contains(refusal([&] { validateSiteGeographicRecord(vertical); }, "Vertical refused"),
                   "vertical reference"),
          "An unknown vertical reference is refused");
    auto digest = londonRecord();
    digest.provenance = {"imported", "a file", ""};
    check(contains(refusal([&] { validateSiteGeographicRecord(digest); }, "Digest refused"),
                   "provenance"),
          "An imported record without its digest is refused");
    auto drift = londonRecord();
    drift.convergence += 0.01;
    check(contains(refusal([&] { validateSiteGeographicRecord(drift); }, "Drift refused"),
                   "convergence"),
          "A stored convergence that does not match is refused");
}

void datums() {
    Document doc;
    setSiteOrigin(doc, londonOrigin());
    check(contains(refusal([&] { setSiteGeographic(doc, londonRecord("OSGB36")); }, "OSGB36"),
                   "OSGB36 is refused"),
          "OSGB36 is refused by name");
    check(contains(refusal([&] { setSiteGeographic(doc, londonRecord("ETRS89")); }, "ETRS89"),
                   "ETRS89 is refused"),
          "ETRS89 is refused by name");
    check(!doc.site()->geographic, "A refused record leaves the site unchanged");
    setSiteGeographic(doc, londonRecord("NAD83"));
    check(doc.site()->geographic->datum == "NAD83",
          "NAD83 is accepted as a labelled approximation");
}

// One undo step per edit, and the re-anchor moves the origin's geographic position with it.
void editsAndReanchor() {
    Document doc;
    setSiteOrigin(doc, londonOrigin());
    setSiteGeographic(doc, londonRecord());
    check(doc.site()->geographic.has_value(), "The record is set");
    doc.undo();
    check(!doc.site()->geographic && doc.site(), "Undo removes the record in one step");
    doc.redo();
    check(doc.site()->geographic.has_value(), "Redo restores the record");
    check(contains(refusal([&] { setSiteGeographic(doc, londonRecord()); }, "Same record"),
                   "already has"),
          "Setting the same record again is refused");
    const auto before = *doc.site()->geographic;
    reanchorSite(doc, doc.site()->easting + 1000, doc.site()->northing + 500,
                 doc.site()->elevation);
    const auto &after = *doc.site()->geographic;
    check(after.utmZone == before.utmZone && after.utmNorth == before.utmNorth,
          "Re-anchor keeps the zone and hemisphere");
    const auto grid = geoToUtm({after.latitude, after.longitude}, after.utmZone);
    near(grid.easting, doc.site()->easting, 1e-6,
         "Re-anchor moves the record to the origin's easting");
    near(grid.northing, doc.site()->northing, 1e-6,
         "Re-anchor moves the record to the origin's northing");
    check(after.latitude != before.latitude, "Re-anchor moves the geographic position");
    // A re-anchor that leaves the zone's band is refused, and the record is not changed.
    check(contains(refusal(
                       [&] {
                           reanchorSite(doc, doc.site()->easting + 900000, doc.site()->northing,
                                        doc.site()->elevation);
                       },
                       "Re-anchor out of band"),
                   "Re-anchor would move the geographic record"),
          "A re-anchor that leaves the zone is refused by name");
    // Clearing the record is one edit as well.
    setSiteGeographic(doc, std::nullopt);
    check(!doc.site()->geographic, "The record is cleared");
    check(contains(refusal([&] { setSiteGeographic(doc, std::nullopt); }, "Clear twice"),
                   "no geographic record to clear"),
          "Clearing with no record is refused");
}

// The command surface: set, describe with the read-only convergence and the NAD83 warning, clear.
void commands() {
    Document doc;
    setSiteOrigin(doc, londonOrigin());
    QJsonObject set{{"command", "site.set_geographic"},
                    {"latitude", 51.5},
                    {"longitude", -0.1},
                    {"zone", 30},
                    {"datum", "NAD83"},
                    {"verticalReference", "EGM2008"},
                    {"provenance", "typed"},
                    {"source", "phone"}};
    executeBatch(doc, {{"apiVersion", 1},
                       {"documentId", QString::fromStdString(doc.identity())},
                       {"expectedRevision", QString::number(doc.revision())},
                       {"commands", QJsonArray{set}}});
    check(doc.site()->geographic && doc.site()->geographic->verticalReference == "EGM2008",
          "site.set_geographic sets the record");
    // site.describe is a read-only query, not an edit.
    const auto description =
        inspectDocument(doc, {{"apiVersion", 1},
                              {"documentId", QString::fromStdString(doc.identity())},
                              {"expectedRevision", QString::number(doc.revision())},
                              {"query", "site.describe"}});
    const auto text = QJsonDocument(description).toJson(QJsonDocument::Compact).toStdString();
    check(contains(text, "\"convergence\"") && contains(text, "read only") &&
              contains(text, "treated as WGS84"),
          "site.describe reports the convergence read-only and warns about NAD83");
    QJsonObject refused{{"command", "site.set_geographic"},
                        {"latitude", 51.5},
                        {"longitude", -0.1},
                        {"datum", "OSGB36"}};
    check(contains(refusal(
                       [&] {
                           executeBatch(doc,
                                        {{"apiVersion", 1},
                                         {"documentId", QString::fromStdString(doc.identity())},
                                         {"expectedRevision", QString::number(doc.revision())},
                                         {"commands", QJsonArray{refused}}});
                       },
                       "OSGB36 command refused"),
                   "OSGB36"),
          "A command with OSGB36 is refused by name");
    executeBatch(doc,
                 {{"apiVersion", 1},
                  {"documentId", QString::fromStdString(doc.identity())},
                  {"expectedRevision", QString::number(doc.revision())},
                  {"commands", QJsonArray{QJsonObject{{"command", "site.clear_geographic"}}}}});
    check(!doc.site()->geographic, "site.clear_geographic clears the record");
}

// The solar study and the site agree, or the distance is reported (ADR 0181 §2.1, 1 m).
void solarAgreement() {
    Document doc;
    setSiteOrigin(doc, londonOrigin());
    check(!solarSiteDistance(doc), "No record, no comparison");
    setSiteGeographic(doc, londonRecord());
    SolarSettings solar;
    solar.latitude = 51.5;
    solar.longitude = -0.1;
    doc.setSolar(solar);
    near(*solarSiteDistance(doc), 0, 1e-6, "The solar location equals the site location");
    solar.latitude = 51.5 + 0.0001; // About 11 m north.
    doc.setSolar(solar);
    check(*solarSiteDistance(doc) > 10 && *solarSiteDistance(doc) < 12,
          "A 0.0001° difference is about 11 m");
    const auto description = solarDescription(doc);
    check(description.contains("warnings") && !description["siteAgrees"].toBool(),
          "The solar description warns when the locations differ by more than 1 m");
}

// The released 1.4.x reader, the first to read schema 32, opens a schema-32 file and leaves it
// unchanged (ADR 0181 §2.11; the 1.3.0 reader refused it).
void priorRelease(const QString &directory) {
    const auto prior = qEnvironmentVariable("SKETCHYUP_PRIOR_CLI");
    if (prior.isEmpty()) {
        std::cout << "SKETCHYUP_PRIOR_CLI not set; prior-release check not exercised\n";
        return;
    }
    const auto path = directory + "/prior-geographic-site.sketchyup";
    const auto bytes = encodeContainer(geographicFixture());
    write(path, bytes);
    QProcess process;
    process.start(prior, {"--validate-native", path});
    check(process.waitForFinished(30000) && process.exitCode() == 0 && read(path) == bytes,
          "The prior release opens a schema-32 file unchanged");
    std::cout << "prior release check: " << process.readAllStandardError().trimmed().toStdString()
              << '\n';
}

} // namespace


// R107.b2.1 (ADR 0181, R107.b2 design "Re-anchor and zone changes"): a georeferenced image keeps its zone.
// The site's zone may not change while the image uses the old one, clearing the record is not refused,
// and a re-anchor that would leave the image's zone is refused by name.
ReferenceImage orthophotoIn(Document &doc, int zone, bool north) {
    const auto png = encodeTexturePng(TextureImage(2, 1, {255, 0, 0, 255, 0, 255, 0, 128}));
    const auto asset = createAsset(
        doc, "Orthophoto pixels", "image/png",
        std::make_shared<AssetPayload>(std::vector<std::uint8_t>(png.begin(), png.end())));
    ImageGeoreference geo;
    geo.zone = zone;
    geo.north = north;
    geo.datum = "WGS84";
    const auto origin = doc.site()->easting;
    geo.easting = origin - 100;
    geo.northing = doc.site()->northing + 100;
    geo.pixelWidth = 2;
    geo.pixelHeight = 2;
    geo.columns = 8;
    geo.rows = 6;
    geo.source = {"png", "north-up.png", std::string(64, 'a'), "north-up.pgw", std::string(64, 'b')};
    const ReferenceImage image{asset, 16, 12, 1, geo};
    createReferenceImage(doc, image, Transform::translation({0, 0, 0}));
    return image;
}

void orthophotoZones() {
    // A site with a record in zone 30N and an image in the same zone.
    Document doc = geographicFixture();
    const auto image = orthophotoIn(doc, 30, true);
    auto eastern = londonRecord();
    eastern.utmZone = 31;
    const auto zoneRefusal =
        refusal([&] { setSiteGeographic(doc, eastern); }, "A zone change under an image was accepted");
    check(contains(zoneRefusal, "georeferenced reference image is in UTM zone 30"),
          "A zone change names the image's zone: " + zoneRefusal);
    check(doc.site()->geographic->utmZone == 30, "A refused zone change leaves the record in zone 30");
    setSiteGeographic(doc, std::nullopt);
    check(!doc.site()->geographic, "Clearing the record is not refused under an image");
    for (const auto &[id, body] : doc.bodies())
        if (body->referenceImage)
            check(*body->referenceImage->georeference == *image.georeference,
                  "The image keeps its own zone when the record is cleared");

    // A site with no record: a re-anchor that leaves the zone is refused; one inside it moves the origin
    // and leaves the image where it is on the ground.
    Document plain;
    setSiteOrigin(plain, londonOrigin());
    const auto plainImage = orthophotoIn(plain, 30, true);
    const auto origin = *plain.site();
    const auto leftZone = refusal([&] { reanchorSite(plain, origin.easting + 400000, origin.northing, 0); },
                                  "A re-anchor out of the image's zone was accepted");
    check(contains(leftZone, "Re-anchor would leave the zone of a georeferenced reference image"),
          "A re-anchor out of the zone is named: " + leftZone);
    reanchorSite(plain, origin.easting + 500, origin.northing + 300, 0);
    for (const auto &[id, body] : plain.bodies())
        if (body->referenceImage)
            check(*body->referenceImage->georeference == *plainImage.georeference,
                  "A re-anchor inside the zone leaves the image's record where it was");
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    // --write-fixture PATH regenerates the checked-in schema-32 fixture.
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) == "--write-fixture") {
        write(QString::fromLocal8Bit(argv[2]), encodeContainer(geographicFixture()));
        return 0;
    }
    try {
        schemaVersions();
        roundTrip();
        schemaGate();
        validation();
        datums();
        editsAndReanchor();
        commands();
        solarAgreement();
        QTemporaryDir directory;
        check(directory.isValid(), "A temporary directory is available");
        priorRelease(directory.path());
        orthophotoZones();
        // The checked-in fixture reopens bit for bit as schema 32.
        const auto fixture = read(QString::fromLocal8Bit(SOURCE_DIR) +
                                  "/tests/fixtures/geographic-site-v32.sketchyup");
        check(encodeContainer(decodeContainer(fixture)) == fixture,
              "The schema-32 fixture reopens bit for bit");
    } catch (const std::exception &error) {
        std::cerr << "geographic site test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "geographic site tests passed\n";
    return 0;
}
