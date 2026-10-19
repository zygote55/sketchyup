// R095.c terrain importers (ADR 0168 sections 1, 4, 5 and 6): DXF terrain mode and point
// files, preparation (conversion, roles, pre-clipping, cleaning, canonical order), the bounded
// report, terrain.inspect_source and terrain.import as one undoable edit with the site origin,
// and the CLI. Fixtures are generated in tests/terrain_import_fixtures.hpp.
#include "automation/commands.hpp"
#include "automation/inspection.hpp"
#include "automation/mcp.hpp"
#include "automation/terrain_commands.hpp"
#include "core/terrain.hpp"
#include "io/document_io.hpp"
#include "io/dxf_source.hpp"
#include "io/terrain_import.hpp"
#include "io/terrain_source.hpp"
#include "terrain_import_fixtures.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <chrono>
#include <cmath>
#include <iostream>
#include <map>
#include <set>
using namespace sketchy;
namespace fixture = sketchy::terrain_fixture;
namespace {
void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> InspectionError refusal(F operation) {
    try {
        operation();
    } catch (const InspectionError &error) {
        return error;
    } catch (const std::exception &error) {
        return InspectionError("not an inspection error", error.what());
    }
    return InspectionError("", "no refusal");
}
QJsonObject batch(const Document &doc, QJsonArray commands) {
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(doc.identity())},
            {"expectedRevision", QString::number(doc.revision())},
            {"commands", commands}};
}
QJsonObject importCommand(const QByteArray &bytes, const QString &format, QJsonObject extra = {}) {
    extra["command"] = "terrain.import";
    extra["data"] = QString::fromLatin1(bytes.toBase64());
    extra["format"] = format;
    return extra;
}
const QJsonObject siteRoles{{"BOUNDARY", "boundary"}, {"POND", "hole"}};
TerrainImportOptions siteOptions() {
    TerrainImportOptions options;
    options.roles = {{"BOUNDARY", TerrainImportRole::Boundary}, {"POND", TerrainImportRole::Hole}};
    return options;
}
std::size_t compactBytes(const QJsonObject &object) {
    return std::size_t(QJsonDocument(object).toJson(QJsonDocument::Compact).size());
}
// Every message and detail within 256 bytes, every kind within 32 locations, the counts
// complete, at most 64 kinds, the whole report within 64 KiB.
void checkBounded(const QJsonObject &report) {
    check(compactBytes(report) <= 64 * 1024, "the report is at most 64 KiB");
    const auto terrain = report["terrain"].toObject();
    const auto kinds = terrain["refusals"].toArray().size() + terrain["notices"].toArray().size();
    check(kinds <= 64, "at most 64 kinds");
    for (const auto *list : {"refusals", "notices"})
        for (const auto &value : terrain[list].toArray()) {
            const auto kind = value.toObject();
            const auto locations = kind["locations"].toArray();
            check(locations.size() <= 32, "at most 32 locations per kind");
            check(kind["message"].toString().toUtf8().size() <= 256, "messages within 256 bytes");
            check(qint64(kind["count"].toDouble()) ==
                      locations.size() + qint64(kind["locationsOmitted"].toDouble()),
                  "counts are complete");
            for (const auto &location : locations) {
                const auto entry = location.toObject();
                check(entry["detail"].toString().toUtf8().size() <= 256 &&
                          entry["reference"].toString().toUtf8().size() <= 128,
                      "located entries are bounded");
            }
        }
}
QJsonObject kind(const QJsonObject &report, const char *list, const QString &name) {
    for (const auto &value : report["terrain"].toObject()[list].toArray())
        if (value.toObject()["kind"] == name)
            return value.toObject();
    return {};
}
// A digest of the mesh with vertices rounded to a micrometre, for comparisons after
// translation or a unit change.
QByteArray roundedDigest(const TerrainMesh &mesh, std::array<long double, 3> shift = {}) {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const auto &v : mesh.vertices)
        for (const long double value : {v.x - shift[0], v.y - shift[1], v.z - shift[2]}) {
            const auto micro = std::llround(double(value * 1e6L));
            hash.addData(QByteArrayView(reinterpret_cast<const char *>(&micro), sizeof micro));
        }
    for (const auto &t : mesh.triangles)
        hash.addData(QByteArrayView(reinterpret_cast<const char *>(t.data()), 12));
    return hash.result().toHex();
}

void reportBounds() {
    TerrainReport report;
    const std::string longText(400, 'x');
    for (int k = 0; k < 100; ++k)
        for (int i = 0; i < 100; ++i)
            report.locate(TerrainReport::Severity::Notice, "kind-" + std::to_string(k), longText,
                          {longText, longText, std::array<double, 2>{1.5, 2.5}, longText});
    report.add(TerrainReport::Severity::Refusal, "late-refusal", 3, "Past the kind bound");
    const auto json = report.json();
    check(compactBytes(json) <= 64 * 1024, "a crowded report fits 64 KiB");
    check(json["notices"].toArray().size() == 64 && json["kindsOmitted"].toInt() == 37 &&
              json["occurrencesInOmittedKinds"].toInt() == 36 * 100 + 3,
          "at most 64 kinds, the rest counted");
    check(report.refused(), "a refusal past the kind bound still refuses");
    check(json["locationsThinned"].toBool() && json["locationsPerKind"].toInt() < 32,
          "locations are thinned to fit");
    for (const auto &value : json["notices"].toArray()) {
        const auto entry = value.toObject();
        check(entry["count"].toInt() == 100, "counts stay complete when thinned");
        check(entry["message"].toString().toUtf8().size() <= 256, "message bound");
        for (const auto &location : entry["locations"].toArray())
            check(location.toObject()["detail"].toString().toUtf8().size() <= 256 &&
                      location.toObject()["reference"].toString().toUtf8().size() <= 128,
                  "location bounds");
    }
    const auto quoted = terrainQuote(std::string(100, 'q') + "\n");
    check(quoted.size() <= 64 + 2 + 3 && quoted.find('\n') == std::string::npos,
          "quotes carry at most 64 bytes of user text");
    check(terrainQuote("\xc3\xa9" + std::string(70, 'a')).size() <= 69, "quotes cut on UTF-8");
}

void units() {
    check(terrainUnit("usft")->metresPerUnit == 1200.0 / 3937.0 &&
              terrainUnit("usft")->exact == "1200/3937",
          "the US survey foot is exactly 1200/3937 m");
    check(terrainUnit("ft")->metresPerUnit == .3048 && !terrainUnit("yd"), "unit names");
}

// Elevations preserved; the metre fixture's mesh digest is pinned.
void metresFixture() {
    const auto bytes = fixture::terrainContoursMetres();
    const auto set = parseDxfTerrain(bytes);
    check(set.unit.name == "m" && set.polylines.size() == 28, "the metre fixture parses");
    const auto result = runTerrainImport(set, siteOptions(), std::nullopt);
    check(result.ok, "the metre fixture imports: " + result.message);
    check(result.origin == TerrainOriginMetres{0, 0, 0} && result.originSource == "proposed",
          "the proposed origin of a site near zero is zero");
    std::map<std::pair<double, double>, double> sourceZ, meshZ;
    for (const auto &polyline : set.polylines)
        for (const auto &p : polyline.points)
            sourceZ[{p.x, p.y}] = p.z;
    for (const auto &v : result.mesh.vertices)
        meshZ[{v.x, v.y}] = v.z;
    std::vector<std::array<double, 2>> kept;
    std::vector<double> keptZ;
    for (const auto &source : result.record.sources) {
        if (source.role != TerrainSourceRole::Contour &&
            source.role != TerrainSourceRole::Breakline)
            continue;
        for (const auto &p : source.points) {
            const auto mesh = meshZ.find({p.x, p.y});
            check(mesh != meshZ.end() && mesh->second == p.z, "every kept vertex is in the mesh");
            const auto original = sourceZ.find({p.x, p.y});
            if (original == sourceZ.end())
                continue; // a clipping point on the boundary
            check(original->second == p.z && mesh->second == original->second,
                  "source Z is preserved exactly in metres");
            kept.push_back({p.x, p.y});
            keptZ.push_back(p.z);
        }
    }
    check(kept.size() > 1000, "most source vertices are kept");
    const auto terrain = result.report["terrain"].toObject();
    check(terrain["outcome"] == "importedWithNotices" &&
              terrain["counts"].toObject()["clippedPolylines"].toInt() >= 8 &&
              terrain["tiles"].toInt() >= 1 && terrain["triangles"].toInt() > 2000,
          "the valley arms are clipped to the boundary");
    check(kind(result.report, "notices", "flat-triangles")["count"].toInt() > 0 &&
              terrain["flatTriangles"].toObject()["flips"].toInt() > 0,
          "the hairpin flat triangles are flipped and the rest reported");
    check(terrain["conversion"] == "model = (source × 1 m/m) − (0 m E, 0 m N, 0 m)",
          "the conversion formula");
    checkBounded(result.report);
    // The same through the command, into a document; elevation_at on source vertices.
    Document doc;
    const auto response =
        executeBatch(doc, batch(doc, {importCommand(bytes, "dxf", {{"roles", siteRoles}})}));
    const auto operation = response["terrainOperations"].toArray().at(0).toObject();
    const Id id = operation["terrain"].toString().toULongLong();
    check(isTerrain(doc, id) && operation["siteEstablished"].toBool(), "terrain.import");
    const auto &record = *doc.bodies().at(id)->terrain;
    check(record.generator == "cdt-2.0.0/terrain-v1" &&
              record.parameters.boundary == TerrainBoundaryMode::Boundary &&
              record.provenance.units == "m" && record.provenance.metresPerUnit == 1 &&
              record.provenance.sha256 == set.sha256.toStdString(),
          "the record stores generator, parameters and provenance");
    check(record.sources == result.record.sources, "the record stores the prepared sources");
    for (std::size_t start = 0; start < kept.size(); start += terrainPointBatch) {
        const std::vector<std::array<double, 2>> chunk(
            kept.begin() + std::ptrdiff_t(start),
            kept.begin() + std::ptrdiff_t(std::min(kept.size(), start + terrainPointBatch)));
        const auto found = terrainElevationAt(doc, id, chunk);
        for (std::size_t i = 0; i < chunk.size(); ++i)
            check(found[i].found && std::abs(found[i].elevation - keptZ[start + i]) <= 1e-9,
                  "terrain.elevation_at returns source elevations");
    }
    // Pinned digest (ADR 0168 section 3): a CDT, flag or preparation change that alters the
    // mesh fails here and must update the generator identity.
    const auto digest = terrainMeshDigest(doc, id);
    std::cout << "metre fixture mesh digest " << digest << '\n';
    check(digest == "c093a02ec36dc4d8544eefb4fc7d99c2d62f7b9ba568331feedf46e974529584",
          "the metre fixture's mesh digest is pinned");
}

void usftFixture() {
    const auto metres = runTerrainImport(parseDxfTerrain(fixture::terrainContoursMetres()),
                                         siteOptions(), std::nullopt);
    check(refusal([] { (void)parseDxfTerrain(fixture::terrainContoursUsft()); }).code() ==
              "not an inspection error",
          "a unitless DXF is refused without an explicit unit");
    bool refused = false;
    try {
        (void)parseDxfTerrain(fixture::terrainContoursUsft());
    } catch (const std::exception &error) {
        refused = std::string(error.what()).find("unitless") != std::string::npos;
    }
    check(refused, "the unitless refusal names the problem");
    const auto set = parseDxfTerrain(fixture::terrainContoursUsft(), {terrainUnit("usft"), 0.05});
    const auto usft = runTerrainImport(set, siteOptions(), std::nullopt);
    check(usft.ok, "the usft fixture imports: " + usft.message);
    const auto terrain = usft.report["terrain"].toObject();
    check(usft.report["metresPerUnitExact"] == "1200/3937" &&
              terrain["metresPerUnitExact"] == "1200/3937" &&
              terrain["conversion"].toString().contains("1200/3937 m/usft"),
          "the report states 1200/3937");
    check(usft.mesh.triangles == metres.mesh.triangles &&
              usft.mesh.vertices.size() == metres.mesh.vertices.size(),
          "the usft fixture gives the metre fixture's triangles");
    double worst = 0;
    for (std::size_t i = 0; i < usft.mesh.vertices.size(); ++i) {
        const auto &a = usft.mesh.vertices[i], &b = metres.mesh.vertices[i];
        const double d = std::max({std::abs(a.x - b.x), std::abs(a.y - b.y), std::abs(a.z - b.z)});
        if (d > 1e-6)
            std::cout << "usft vertex " << i << ": " << a.x << ' ' << a.y << ' ' << a.z << " vs "
                      << b.x << ' ' << b.y << ' ' << b.z << '\n';
        worst = std::max(worst, d);
    }
    check(worst <= 1e-6, "the usft fixture matches the metre fixture within 1e-6 m (worst " +
                             std::to_string(worst) + ")");
    check(usft.record.provenance.units == "usft" &&
              usft.record.provenance.metresPerUnit == 1200.0 / 3937.0,
          "usft provenance");
}

void utmFixture() {
    const auto metres = runTerrainImport(parseDxfTerrain(fixture::terrainContoursMetres()),
                                         siteOptions(), std::nullopt);
    const auto set = parseDxfTerrain(fixture::terrainUtm());
    const auto utm = runTerrainImport(set, siteOptions(), std::nullopt);
    check(utm.ok, "the UTM fixture imports: " + utm.message);
    check(utm.originSource == "proposed" && std::fmod(utm.origin.easting, 100) == 0 &&
              std::fmod(utm.origin.northing, 100) == 0 && utm.origin.elevation == 0 &&
              std::abs(utm.origin.easting - 512345) < 100 &&
              std::abs(utm.origin.northing - 5234567) < 100,
          "the proposed origin is the extents' centre rounded to 100 m");
    const std::array<long double, 3> shift{512345.678L - utm.origin.easting,
                                           5234567.891L - utm.origin.northing, 1850};
    check(utm.mesh.triangles == metres.mesh.triangles, "the UTM fixture gives the same triangles");
    for (std::size_t i = 0; i < utm.mesh.vertices.size(); ++i) {
        const auto &a = utm.mesh.vertices[i], &b = metres.mesh.vertices[i];
        check(std::abs((long double)a.x - (b.x + shift[0])) <= 1e-9L &&
                  std::abs((long double)a.y - (b.y + shift[1])) <= 1e-9L &&
                  std::abs((long double)a.z - (b.z + shift[2])) <= 1e-9L,
              "every vertex is within 1e-9 m of source x scale - origin");
    }
    check(roundedDigest(utm.mesh, shift) == roundedDigest(metres.mesh),
          "the UTM mesh digest matches the local fixture's after translation");
    // Source vertices convert exactly: local = source x 1 - origin.
    std::set<std::pair<double, double>> local;
    for (const auto &polyline : set.polylines)
        for (const auto &p : polyline.points)
            local.insert({p.x - utm.origin.easting, p.y - utm.origin.northing});
    std::size_t exact = 0;
    for (const auto &v : utm.mesh.vertices)
        exact += local.contains({v.x, v.y});
    check(exact > 1000, "mesh vertices are source x scale - origin exactly");
    // Save, reopen, undo and redo are bit for bit.
    Document doc;
    executeBatch(doc, batch(doc, {importCommand(fixture::terrainUtm(), "dxf",
                                                {{"roles", siteRoles},
                                                 {"crs", "EPSG:32633"},
                                                 {"sourceName", "terrain-utm.dxf"}})}));
    check(doc.site() && doc.site()->easting == utm.origin.easting &&
              doc.site()->northing == utm.origin.northing && doc.site()->crs == "EPSG:32633" &&
              doc.site()->established.source == "terrain-utm.dxf" &&
              doc.site()->established.sha256 == set.sha256.toStdString(),
          "the import establishes the site origin with CRS and provenance");
    const auto history = doc.history().total;
    check(doc.history().entries.back().label == "Import terrain", "one labelled edit");
    const auto saved = encodeContainer(doc);
    const auto reopened = decodeContainer(saved);
    check(encodeContainer(reopened) == saved, "save and reopen are bit for bit");
    doc.undo();
    check(!doc.site() && doc.bodies().empty(), "Undo removes the terrain and the site origin");
    doc.redo();
    // The revision counts every step, so compare the model without it.
    auto model = [](const Document &document) {
        auto json = QJsonDocument::fromJson(encodeDocument(document)).object();
        json.remove("revision");
        return json;
    };
    check(model(doc) == model(reopened) && doc.history().total == history,
          "Redo restores both bit for bit");
    // The point file: the same terrain's vertices as an XYZ file, translated.
    TerrainPointOptions xyz;
    xyz.unit = *terrainUnit("m");
    const auto localPoints =
        runTerrainImport(parseTerrainPoints(fixture::terrainXyz(false), xyz), {}, std::nullopt);
    const auto utmPoints =
        runTerrainImport(parseTerrainPoints(fixture::terrainXyz(true), xyz), {}, std::nullopt);
    check(localPoints.ok && utmPoints.ok, "the XYZ fixtures import");
    const std::array<long double, 3> pointShift{512345.678L - utmPoints.origin.easting,
                                                5234567.891L - utmPoints.origin.northing, 1850};
    // Unconstrained points near the hull are nearly collinear, so a 1e-10 m perturbation from
    // the translation may resolve such ties differently (ADR 0168 section 3, Determinism); the
    // vertices are compared, and the DXF fixture above pins the triangles.
    check(utmPoints.mesh.vertices.size() == localPoints.mesh.vertices.size(),
          "the UTM point file has the local one's vertices");
    for (std::size_t i = 0; i < utmPoints.mesh.vertices.size(); ++i) {
        const auto &a = utmPoints.mesh.vertices[i], &b = localPoints.mesh.vertices[i];
        check(std::abs((long double)a.x - pointShift[0] - (b.x + localPoints.origin.easting)) <=
                      1e-9L &&
                  std::abs((long double)a.y - pointShift[1] -
                           (b.y + localPoints.origin.northing)) <= 1e-9L &&
                  std::abs((long double)a.z - pointShift[2] - b.z) <= 1e-9L,
              "the UTM point file matches the local one after translation");
    }
}

void determinism() {
    const auto first = runTerrainImport(parseDxfTerrain(fixture::terrainContoursMetres()),
                                        siteOptions(), std::nullopt);
    const auto second = runTerrainImport(parseDxfTerrain(fixture::terrainContoursMetres()),
                                         siteOptions(), std::nullopt);
    check(first.mesh.vertices == second.mesh.vertices &&
              first.mesh.triangles == second.mesh.triangles && first.record == second.record,
          "two imports give identical meshes and records");
    const auto shuffled = runTerrainImport(parseDxfTerrain(fixture::terrainContoursMetres(true)),
                                           siteOptions(), std::nullopt);
    check(shuffled.ok, "the shuffled fixture imports");
    check(shuffled.mesh.vertices == first.mesh.vertices &&
              shuffled.mesh.triangles == first.mesh.triangles,
          "a shuffled source gives the identical mesh");
    std::vector<TerrainSource> a = first.record.sources, b = shuffled.record.sources;
    check(a.size() == b.size(), "the same source count");
    for (std::size_t i = 0; i < a.size(); ++i)
        check(a[i].points == b[i].points && a[i].role == b[i].role,
              "canonical sources are identical");
    TerrainPointOptions xyz;
    xyz.unit = *terrainUnit("m");
    const auto points =
        runTerrainImport(parseTerrainPoints(fixture::terrainXyz(false), xyz), {}, std::nullopt);
    const auto shuffledPoints = runTerrainImport(
        parseTerrainPoints(fixture::terrainXyz(false, true), xyz), {}, std::nullopt);
    check(points.mesh.vertices == shuffledPoints.mesh.vertices &&
              points.mesh.triangles == shuffledPoints.mesh.triangles,
          "a shuffled point file gives the identical mesh");
    // Two document imports: identical records and IDs.
    Document one, two;
    for (auto *doc : {&one, &two})
        executeBatch(*doc, batch(*doc, {importCommand(fixture::terrainContoursMetres(), "dxf",
                                                      {{"roles", siteRoles}})}));
    check(one.bodies().size() == two.bodies().size(), "the same bodies");
    for (const auto &[id, body] : one.bodies()) {
        check(two.bodies().contains(id), "the same IDs");
        const auto &other = *two.bodies().at(id);
        check(body->terrain == other.terrain && body->surface.vertices == other.surface.vertices &&
                  body->name == other.name,
              "identical records and tiles");
    }
}

void invalidFixture() {
    Document doc;
    doc.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}}});
    const auto revision = doc.revision();
    const auto history = doc.history().total;
    const auto before = encodeContainer(doc);
    const QJsonObject roles{{"BOUNDARY", "boundary"}, {"CONTOURS", "contour"}};
    const auto error = refusal([&] {
        executeBatch(
            doc, batch(doc, {importCommand(fixture::terrainInvalid(), "dxf", {{"roles", roles}})}));
    });
    check(error.code() == "INVALID_REQUEST",
          std::string("the invalid fixture is refused: ") + error.code() + " " + error.what());
    check(doc.revision() == revision && doc.history().total == history &&
              encodeContainer(doc) == before && !doc.site(),
          "a refusal leaves revision, history and records unchanged");
    const auto report = error.details()["importReport"].toObject();
    check(report["terrain"].toObject()["outcome"] == "refused", "the outcome is refused");
    checkBounded(report);
    const auto crossing = kind(report, "refusals", "crossing-constraints");
    check(crossing["count"].toInt() >= 41 && crossing["locations"].toArray().size() == 32 &&
              crossing["locationsOmitted"].toInt() == crossing["count"].toInt() - 32,
          "more than 32 crossings: complete count, 32 located");
    const auto first = crossing["locations"].toArray().at(0).toObject();
    check(first["reference"].toString().startsWith("handle ") &&
              first["reference2"].toString().startsWith("handle ") && first["xy"].isArray(),
          "crossings name both handles and the place in source units");
    // The figure-eight on its own: a self-crossing names the same contour twice.
    {
        std::vector<fixture::Item> items{
            {fixture::Kind::LwPolyline,
             "C",
             false,
             {fixture::at(50, 0, 12), fixture::at(54, 4, 12), fixture::at(54, 0, 12),
              fixture::at(50, 4, 12)},
             {},
             0x2F0},
            {fixture::Kind::LwPolyline,
             "C",
             false,
             {fixture::at(40, 0, 11), fixture::at(40, 9, 11), fixture::at(45, 9, 11)},
             {},
             0x2F1}};
        const auto self =
            runTerrainImport(parseDxfTerrain(fixture::dxf(items, {}, 6)), {}, std::nullopt);
        const auto located = kind(self.report, "refusals", "crossing-constraints");
        check(!self.ok && located["count"].toInt() == 1 &&
                  located["locations"].toArray().at(0).toObject()["reference"] == "handle 2F0" &&
                  located["locations"].toArray().at(0).toObject()["reference2"] == "handle 2F0",
              "a self-crossing contour is refused and named");
    }
    check(kind(report, "refusals", "conflicting-duplicate")["count"].toInt() >= 1,
          "the conflicting duplicate is refused");
    for (const auto *notice : {"contour-not-constant", "arcs-segmented", "zero-length-segments",
                               "collinear-spikes", "dropped-outside", "ignored-entities"})
        check(kind(report, "notices", notice)["count"].toInt() >= 1,
              std::string("the invalid fixture reports ") + notice);
    check(report["omittedEntities"].toObject()["POINT:notTerrain"].toInt() == 1 &&
              report["omittedEntities"].toObject()["TEXT:notTerrain"].toInt() == 1,
          "ignored entity types are counted");
    // Inspection reports the same without refusing.
    const auto inspected = executeBatch(
        doc,
        batch(doc, {QJsonObject{{"command", "terrain.inspect_source"},
                                {"data", QString::fromLatin1(fixture::terrainInvalid().toBase64())},
                                {"format", "dxf"},
                                {"roles", roles}}}));
    check(inspected["status"] == "inspected" && doc.revision() == revision &&
              inspected["inspection"]
                      .toObject()["report"]
                      .toObject()["terrain"]
                      .toObject()["outcome"] == "refused",
          "terrain.inspect_source reports a refusal without changing anything");
}

void inspection() {
    Document doc;
    const auto bytes = fixture::terrainUtm();
    const auto response =
        executeBatch(doc, batch(doc, {QJsonObject{{"command", "terrain.inspect_source"},
                                                  {"data", QString::fromLatin1(bytes.toBase64())},
                                                  {"format", "dxf"},
                                                  {"roles", siteRoles}}}));
    check(response["status"] == "inspected" && doc.revision() == 0 && doc.bodies().empty(),
          "terrain.inspect_source changes nothing");
    const auto inspection = response["inspection"].toObject();
    std::map<QString, QJsonObject> layers;
    for (const auto &value : inspection["layers"].toArray())
        layers[value.toObject()["name"].toString()] = value.toObject();
    check(layers["CONTOURS"]["polylines"].toInt() == 25 &&
              layers["CONTOURS"]["defaultRole"] == "contour" &&
              layers["BREAKLINES"]["defaultRole"] == "breakline" &&
              layers["BOUNDARY"]["closed"].toInt() == 1,
          "layers with counts and default roles");
    check(inspection["proposedOrigin"].toObject()["easting"].toDouble() == 512300 &&
              inspection["conversion"].toString().startsWith("model = (source × 1 m/m)") &&
              inspection["sourceExtents"].toObject()["min"].toArray().size() == 3,
          "extents, proposed origin and the conversion");
    const auto report = inspection["report"].toObject();
    check(report["terrain"].toObject()["triangles"].toInt() > 2000 &&
              report["sha256"].toString().size() == 64,
          "the triangulation dry run");
    check(refusal([&] {
              executeBatch(
                  doc,
                  batch(doc,
                        {QJsonObject{{"command", "terrain.inspect_source"},
                                     {"data", "AAAA"},
                                     {"format", "dxf"}},
                         QJsonObject{{"command", "site.set"}, {"easting", 1}, {"northing", 1}}}));
          }).code() != "",
          "terrain.inspect_source runs alone");
    // Dry run of terrain.import.
    const auto preview =
        previewBatch(doc, batch(doc, {importCommand(fixture::terrainContoursMetres(), "dxf",
                                                    {{"roles", siteRoles}})}));
    check(preview["status"] == "preview" && doc.revision() == 0 && doc.bodies().empty() &&
              preview["terrainOperations"].toArray().size() == 1,
          "a dry run of terrain.import leaves the document unchanged");
    // Origins: explicit; the document's; a different explicit one refuses.
    Document site;
    executeBatch(site,
                 batch(site, {importCommand(fixture::terrainUtm(), "dxf",
                                            {{"roles", siteRoles},
                                             {"origin", QJsonObject{{"easting", 512000},
                                                                    {"northing", 5234000},
                                                                    {"elevation", 1800}}}})}));
    check(site.site()->easting == 512000 && site.site()->elevation == 1800,
          "an explicit origin is used");
    const auto revision = site.revision();
    check(refusal([&] {
              executeBatch(
                  site,
                  batch(site, {importCommand(fixture::terrainUtm(), "dxf",
                                             {{"origin", QJsonObject{{"easting", 512100},
                                                                     {"northing", 5234000}}}})}));
          }).code() == "INVALID_REQUEST" &&
              site.revision() == revision,
          "an origin different from the document's is refused");
    const auto again = executeBatch(
        site, batch(site, {importCommand(fixture::terrainUtm(), "dxf",
                                         {{"roles", siteRoles}, {"name", "Second"}})}));
    const auto operation = again["terrainOperations"].toArray().at(0).toObject();
    check(!operation["siteEstablished"].toBool() &&
              operation["importReport"].toObject()["terrain"].toObject()["originSource"] ==
                  "document" &&
              site.history().entries.back().label == "Import terrain",
          "a second import uses the document's origin");
    // Elevation: local z = source z - origin elevation.
    const Id second = operation["terrain"].toString().toULongLong();
    const auto &record = *site.bodies().at(second)->terrain;
    check(std::any_of(record.sources.begin(), record.sources.end(),
                      [](const TerrainSource &source) {
                          return source.role == TerrainSourceRole::Contour &&
                                 std::abs(source.points.front().z - (1950.5 - 1800)) < 1e-9;
                      }),
          "elevations are offset by the origin's elevation");
    // Catalogue and assistant visibility.
    std::set<QString> commands;
    for (const auto &command : commandCatalog())
        commands.insert(command.toObject()["name"].toString());
    check(commands.contains("terrain.import") && commands.contains("terrain.inspect_source"),
          "the command catalogue has both commands");
    check(mcpToolCatalog().size() <= 64, "the MCP tool list stays within its bound");
    // Units are required for point files.
    check(refusal([&] {
              executeBatch(doc, batch(doc, {importCommand("1 2 3\n4 5 6\n7 8 9\n", "points")}));
          }).code() == "INVALID_REQUEST",
          "point files require units");
    check(refusal([&] {
              executeBatch(doc, batch(doc, {importCommand(fixture::terrainContoursUsft(), "dxf")}));
          }).code() == "INVALID_REQUEST",
          "unitless DXF requires units");
}

TerrainSourceSet points(const QByteArray &bytes, TerrainPointOptions options = {}) {
    if (options.unit.name.isEmpty())
        options.unit = *terrainUnit("m");
    return parseTerrainPoints(bytes, options);
}
void pointFiles() {
    // PNEZD with a header (detected), comma (detected).
    auto set = points("P,N,E,Z,D\n1,200.5,100.25,10.5,GND\n2,201,103,11,GND\n3,205,101,12,"
                      "TREE oak\n",
                      {{}, {}, {}, terrainColumnPreset("PNEZD"), "PNEZD"});
    check(set.points.size() == 3 && set.report["header"].toBool() &&
              set.report["headerDetected"].toBool() && set.report["delimiter"] == "comma" &&
              set.report["delimiterDetected"].toBool(),
          "PNEZD with a detected header and comma");
    check(set.points[0].position == TerrainVec3{100.25, 200.5, 10.5} &&
              set.points[0].ref.text() == "line 2" &&
              set.layers[set.points[2].layer].name == "TREE",
          "PNEZD columns, line references and the first word as the code");
    // PENZD, tab.
    set = points("1\t100\t200\t10\tA\n2\t103\t201\t11\tA\n",
                 {{}, {}, {}, terrainColumnPreset("PENZD"), "PENZD"});
    check(set.points[0].position == TerrainVec3{100, 200, 10} && set.report["delimiter"] == "tab" &&
              !set.report["header"].toBool(),
          "PENZD and tab");
    // ENZ, semicolon; XYZ, whitespace with comments; preset detection by field count.
    set = points("100;200;10\n101;201;11\n", {{}, {}, {}, terrainColumnPreset("ENZ"), "ENZ"});
    check(set.report["delimiter"] == "semicolon" && set.points[1].position.y == 201, "ENZ");
    set = points("# survey\nX Y Z\n100  200 10\n\n101 201   11\n");
    check(set.report["delimiter"] == "whitespace" && set.report["header"].toBool() &&
              set.report["columnsPreset"] == "ENZ" && set.points.size() == 2 &&
              set.points[1].ref.text() == "line 5" && set.report["commentLines"].toInt() == 1,
          "XYZ, whitespace, header detection and comments");
    set = points("1,100,200,10,GND\n", {{}, {}, false, std::nullopt, {}});
    check(set.report["columnsPreset"] == "PNEZD" && set.points[0].position.x == 200,
          "five columns default to PNEZD");
    // Explicit column list and delimiter.
    set = points("a 100 200 10\nb 101 201 11\n",
                 {{},
                  TerrainDelimiter::Whitespace,
                  false,
                  std::vector{TerrainColumn::Ignore, TerrainColumn::Easting,
                              TerrainColumn::Northing, TerrainColumn::Elevation},
                  {}});
    check(set.points.size() == 2 && set.points[0].position == TerrainVec3{100, 200, 10},
          "explicit column meanings");
    // Line-numbered errors.
    set = points("100,200,10\n101,201,11\n102,abc,12\n103,204,inf\n104\n" + QByteArray("105,") +
                     QByteArray(300, 'z') + ",1\n",
                 {{}, {}, false, terrainColumnPreset("ENZ"), "ENZ"});
    check(set.problems.refused() && set.points.size() == 2, "bad lines are refused");
    const auto json = set.problems.json();
    auto located = [&](const char *name) {
        for (const auto &value : json["refusals"].toArray())
            if (value.toObject()["kind"] == name)
                return value.toObject()["locations"].toArray();
        return QJsonArray();
    };
    check(located("non-numeric").size() == 2 &&
              located("non-numeric").at(0).toObject()["reference"] == "line 3" &&
              located("non-numeric").at(0).toObject()["detail"].toString().contains("'abc'"),
          "non-numeric values with line numbers");
    check(located("non-numeric").at(1).toObject()["detail"].toString().toUtf8().size() < 100,
          "long values are quoted at 64 bytes");
    check(located("non-finite").at(0).toObject()["reference"] == "line 4", "non-finite values");
    check(located("missing-columns").at(0).toObject()["reference"] == "line 5", "missing columns");
    // Many bad lines: complete count, 32 located.
    QByteArray many;
    for (int i = 0; i < 2000; ++i)
        many += "x,y,z\n";
    set = points("1,2,3\n" + many, {{}, {}, false, terrainColumnPreset("ENZ"), "ENZ"});
    const auto crowded = set.problems.json();
    check(crowded["refusals"].toArray().at(0).toObject()["count"].toInt() == 6000 &&
              crowded["refusals"].toArray().at(0).toObject()["locations"].toArray().size() == 32,
          "2,000 bad lines: complete count, 32 located");
    // Description-code breaklines.
    set = points("1,0,0,10,GND\n2,0,10,10,GND\n3,10,0,10,GND\n4,10,10,12,GND\n"
                 "5,2,5,11,BL\n6,5,5,11.5,BL\n7,8,5,11.2,BL\n",
                 {{}, {}, false, terrainColumnPreset("PENZD"), "PENZD"});
    TerrainImportOptions options;
    options.roles = {{"BL", TerrainImportRole::Breakline}};
    const auto result = runTerrainImport(set, options, std::nullopt);
    check(result.ok, "description-code breaklines import: " + result.message);
    std::size_t breaklines = 0, ground = 0;
    for (const auto &source : result.record.sources) {
        if (source.role == TerrainSourceRole::Breakline) {
            ++breaklines;
            check(source.points.size() == 3 && source.points.front() == Vec3{2, 5, 11} &&
                      source.points.back() == Vec3{8, 5, 11.2} && source.reference == "code BL",
                  "the coded points in file order form one breakline");
        }
        if (source.role == TerrainSourceRole::Points)
            ground += source.points.size();
    }
    check(breaklines == 1 && ground == 4, "one breakline and four ground points");
    check(result.report["terrain"].toObject()["parameters"].toObject()["boundary"] == "convexHull",
          "no boundary and no edge limit: convex hull");
    // Units are required.
    check(refusal([&] {
              Document doc;
              executeBatch(doc, batch(doc, {importCommand("1 2 3\n", "points")}));
          }).code() == "INVALID_REQUEST",
          "point files need units");
}

void cli() {
#ifdef CLI_PATH
    QTemporaryDir directory;
    check(directory.isValid(), "CLI scratch");
    const auto cli = QString::fromUtf8(CLI_PATH);
    auto write = [&](const QString &name, const QByteArray &bytes) {
        QFile file(directory.filePath(name));
        check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "write");
        return directory.filePath(name);
    };
    auto invoke = [&](const QStringList &args, bool success) {
        QProcess p;
        p.start(cli, args);
        check(p.waitForStarted(10000) && p.waitForFinished(120000), "terrain CLI completed");
        const auto out = p.readAllStandardOutput(), err = p.readAllStandardError();
        check((p.exitCode() == 0) == success,
              "terrain CLI expected status: " + (out + err).left(400).toStdString());
        const auto json = QJsonDocument::fromJson(success ? out : err);
        check(json.isObject(), "terrain CLI structured result");
        return json.object();
    };
    const auto dxf = write("terrain-contours-m.dxf", fixture::terrainContoursMetres());
    const auto native = directory.filePath("new.sketchyup");
    const auto imported =
        invoke({"--import-terrain", dxf, "--terrain-units", "header", "--terrain-roles",
                "BOUNDARY=boundary,POND=hole", "--output", native},
               true);
    check(imported["status"] == "imported" &&
              imported["importReport"].toObject()["terrain"].toObject()["outcome"] ==
                  "importedWithNotices" &&
              imported["siteEstablished"].toBool(),
          "--import-terrain prints the report");
    const auto document = loadDocument(native);
    check(document.site() && isTerrain(document, imported["terrain"].toString().toULongLong()),
          "--output writes the terrain");
    invoke({"--import-terrain", dxf, "--terrain-units", "header", "--output", native}, false);
    const auto utm = write("terrain-utm.xyz", fixture::terrainXyz(true));
    const auto points =
        invoke({"--import-terrain", utm, "--terrain-units", "m", "--terrain-columns", "XYZ",
                "--terrain-delimiter", "whitespace", "--terrain-header", "no", "--origin",
                "512300,5234600,1800", "--terrain-crs", "EPSG:32633"},
               true);
    const auto origin =
        points["importReport"].toObject()["terrain"].toObject()["origin"].toObject();
    check(origin["easting"].toDouble() == 512300 && origin["elevation"].toDouble() == 1800 &&
              points["importReport"].toObject()["format"] == "points",
          "--origin and the point-file options");
    invoke({"--import-terrain", utm}, false); // a point file needs units
    invoke({"--terrain-units", "m"}, false);  // terrain options need --import-terrain
    invoke({"--import-terrain", utm, "--terrain-units", "m", "--origin", "1"}, false);
    const auto invalid = write("terrain-invalid.dxf", fixture::terrainInvalid());
    const auto refused = invoke({"--import-terrain", invalid, "--terrain-units", "m",
                                 "--terrain-roles", "BOUNDARY=boundary,CONTOURS=contour"},
                                false);
    check(refused["code"] == "INVALID_REQUEST" && refused["details"]
                                                          .toObject()["importReport"]
                                                          .toObject()["terrain"]
                                                          .toObject()["outcome"] == "refused",
          "a refused import prints its report");
    const auto usft = write("terrain-contours-usft.dxf", fixture::terrainContoursUsft());
    invoke({"--import-terrain", usft, "--terrain-units", "header"}, false);
    invoke({"--import-terrain", usft, "--terrain-units", "usft", "--input", native,
            "--terrain-roles", "BOUNDARY=boundary,POND=hole", "--output",
            directory.filePath("two.sketchyup")},
           true);
    check(loadDocument(directory.filePath("two.sketchyup")).bodies().size() >
              document.bodies().size(),
          "--input adds a second terrain to the open model");
#endif
}

// Timings for the ADR note: a large point file and a large contour DXF.
void timings(bool large) {
    if (!large)
        return;
    QByteArray file;
    std::uint32_t state = 12345;
    auto random = [&] {
        state = state * 1664525u + 1013904223u;
        return double(state >> 8) / double(1u << 24);
    };
    for (int i = 0; i < 120000; ++i)
        file += QByteArray::number(512000 + random() * 800, 'f', 3) + ' ' +
                QByteArray::number(5234000 + random() * 800, 'f', 3) + ' ' +
                QByteArray::number(1850 + 20 * random(), 'f', 3) + '\n';
    TerrainPointOptions options;
    options.unit = *terrainUnit("m");
    auto start = std::chrono::steady_clock::now();
    const auto set = parseTerrainPoints(file, options);
    auto parsed = std::chrono::steady_clock::now();
    const auto result = runTerrainImport(set, {}, std::nullopt);
    auto done = std::chrono::steady_clock::now();
    auto ms = [](auto a, auto b) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count();
    };
    std::cout << "points: " << set.points.size() << " points, " << file.size() << " bytes, parse "
              << ms(start, parsed) << " ms, prepare+triangulate " << ms(parsed, done) << " ms, "
              << result.mesh.triangles.size() << " triangles\n";
    // Contours: 400 concentric rings of 300 vertices.
    std::vector<fixture::Item> items;
    for (int k = 0; k < 400; ++k) {
        fixture::Item ring{fixture::Kind::LwPolyline, "C", true, {}, {}, std::uint64_t(0x100 + k)};
        const double r = 10 + k * 0.9;
        for (int i = 0; i < 300; ++i) {
            const double t = 2 * std::numbers::pi * i / 300 + 0.01 * k;
            ring.q.push_back(fixture::at(r * std::cos(t) * 1.6, r * std::sin(t), 100 + 0.5 * k));
        }
        items.push_back(std::move(ring));
    }
    const auto dxf = fixture::dxf(items, {}, 6);
    start = std::chrono::steady_clock::now();
    const auto contours = parseDxfTerrain(dxf);
    parsed = std::chrono::steady_clock::now();
    const auto meshed = runTerrainImport(contours, {}, std::nullopt);
    done = std::chrono::steady_clock::now();
    std::cout << "contours: " << items.size() * 300 << " vertices, " << dxf.size()
              << " bytes, parse " << ms(start, parsed) << " ms, prepare+triangulate "
              << ms(parsed, done) << " ms, " << meshed.mesh.triangles.size() << " triangles\n";
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        const bool large = argc > 1 && std::string(argv[1]) == "--timings";
        reportBounds();
        units();
        pointFiles();
        metresFixture();
        usftFixture();
        utmFixture();
        determinism();
        invalidFixture();
        inspection();
        cli();
        timings(large);
        std::cout << "terrain import tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
