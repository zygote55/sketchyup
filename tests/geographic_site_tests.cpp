// R107.a (ADR 0181 §2.1, §2.6, §2.11 V9): the optional geographic record on the site origin, schema
// 32 with geographic-site-v1, the lowest-schema writer, the site and solar commands, and the
// released reader's refusal of the new schema (measured with SKETCHYUP_PRIOR_CLI).
#include "automation/commands.hpp"
#include "automation/inspection.hpp"
#include "automation/solar_commands.hpp"
#include "core/terrain.hpp"
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
    check(nativeDocumentVersionFor(doc) == 32 && nativeDocumentVersion == 32,
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

// The released 1.3.0 reader refuses a schema-32 file and leaves it unchanged (ADR 0181 §2.11).
void priorRelease(const QString &directory) {
    const auto prior = qEnvironmentVariable("SKETCHYUP_PRIOR_CLI");
    if (prior.isEmpty()) {
        std::cout << "SKETCHYUP_PRIOR_CLI not set; prior-release refusal not exercised\n";
        return;
    }
    const auto path = directory + "/prior-geographic-site.sketchyup";
    const auto bytes = encodeContainer(geographicFixture());
    write(path, bytes);
    QProcess process;
    process.start(prior, {"--validate-native", path});
    check(process.waitForFinished(30000) && process.exitCode() != 0 && read(path) == bytes,
          "The prior release refuses a schema-32 file unchanged");
    std::cout << "prior release refusal: " << process.readAllStandardError().trimmed().toStdString()
              << '\n';
}

} // namespace

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
