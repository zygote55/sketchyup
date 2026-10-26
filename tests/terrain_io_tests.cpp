// R095.a terrain persistence and automation (ADR 0168 §2, §4, §5): schema 29 with
// terrain-v1, the terrain-mesh chunk and its bounded decoder, lower schemas for
// documents without terrain, UTM-scale precision through a save, and the site and
// terrain commands and queries.
#include "automation/commands.hpp"
#include "automation/inspection.hpp"
#include "automation/mcp.hpp"
#include "core/annotations.hpp"
#include "core/component_parameters.hpp"
#include "core/components.hpp"
#include "core/assets.hpp"
#include "core/drawing_sheets.hpp"
#include "core/groups.hpp"
#include "core/reference_images.hpp"
#include "core/scenes.hpp"
#include "core/sections.hpp"
#include "core/tags.hpp"
#include "core/terrain.hpp"
#include "io/document_io.hpp"
#include "io/native_format.hpp"
#include "io/native_limits.hpp"
#include "io/terrain_io.hpp"
#include "sketchyup/version.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QtEndian>
#include <cmath>
#include <iostream>
using namespace sketchy;
namespace {
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
template <class F> std::string code(F operation) {
    try {
        operation();
    } catch (const InspectionError &error) {
        return error.code();
    } catch (const std::exception &error) {
        return std::string("not an inspection error: ") + error.what();
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
    check(file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size(),
          "Write " + path.toStdString());
}
QString sha256(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
QJsonObject manifest(const QByteArray &container) {
    const auto length = qFromLittleEndian<quint32>(container.constData() + 12);
    return QJsonDocument::fromJson(container.mid(16, length)).object();
}
QByteArray chunk(const QByteArray &container, qsizetype index) {
    const auto length = qFromLittleEndian<quint32>(container.constData() + 12);
    const auto record = manifest(container)["chunks"].toArray()[index].toObject();
    return container.mid(16 + length + record["offset"].toString().toLongLong(),
                         record["bytes"].toString().toLongLong());
}
QJsonObject documentTree(const QByteArray &container) {
    return QJsonDocument::fromJson(chunk(container, 0)).object();
}
// Replace the last chunk (the terrain mesh) and refresh its manifest entry.
QByteArray withMesh(const QByteArray &container, const QByteArray &mesh) {
    auto head = manifest(container);
    auto chunks = head["chunks"].toArray();
    auto last = chunks.last().toObject();
    check(last["kind"] == "terrain-mesh", "The mesh is the last chunk");
    const auto length = qFromLittleEndian<quint32>(container.constData() + 12);
    const auto payload =
        container.mid(16 + length, last["offset"].toString().toLongLong()) + mesh;
    last["bytes"] = QString::number(mesh.size());
    last["sha256"] = sha256(mesh);
    chunks[chunks.size() - 1] = last;
    head["chunks"] = chunks;
    const auto encoded = QJsonDocument(head).toJson(QJsonDocument::Compact);
    QByteArray header = container.left(16);
    qToLittleEndian<quint32>(quint32(encoded.size()), header.data() + 12);
    return header + encoded + payload;
}
TerrainMesh grid(int nx, int ny, double spacing, Vec3 origin) {
    TerrainMesh mesh;
    for (int j = 0; j <= ny; ++j)
        for (int i = 0; i <= nx; ++i)
            mesh.vertices.push_back({origin.x + i * spacing, origin.y + j * spacing,
                                     origin.z + .3 * std::sin(i * .37) * std::cos(j * .21)});
    auto at = [&](int i, int j) { return std::uint32_t(j * (nx + 1) + i); };
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) {
            mesh.triangles.push_back({at(i, j), at(i + 1, j), at(i + 1, j + 1)});
            mesh.triangles.push_back({at(i, j), at(i + 1, j + 1), at(i, j + 1)});
        }
    return mesh;
}
TerrainRecord record(const TerrainMesh &mesh) {
    TerrainRecord result;
    result.generator = "test/terrain-v1";
    result.parameters.boundary = TerrainBoundaryMode::Boundary;
    result.parameters.maxEdgeLength = 2.5;
    result.provenance = {std::string(64, 'a'), "usft", 1200.0 / 3937.0};
    TerrainSource boundary;
    boundary.role = TerrainSourceRole::Boundary;
    boundary.reference = "DXF handle 1A";
    boundary.points = {mesh.vertices[0], mesh.vertices[1], mesh.vertices.back()};
    result.sources.push_back(boundary);
    TerrainSource contour;
    contour.role = TerrainSourceRole::Contour;
    contour.reference = "DXF handle 2F";
    contour.points = {mesh.vertices[2], mesh.vertices[3]};
    result.sources.push_back(contour);
    return result;
}
bool sameDocument(const Document &a, const Document &b) {
    if (a.bodies().size() != b.bodies().size() || a.site() != b.site())
        return false;
    for (const auto &[id, body] : a.bodies())
        if (!b.bodies().contains(id) || !(*b.bodies().at(id) == *body))
            return false;
    return true;
}
QJsonObject batch(const Document &doc, QJsonArray commands) {
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(doc.identity())},
            {"expectedRevision", QString::number(doc.revision())},
            {"commands", commands}};
}
QJsonObject query(const Document &doc, const QString &name, QJsonObject arguments = {}) {
    arguments["apiVersion"] = 1;
    arguments["documentId"] = QString::fromStdString(doc.identity());
    arguments["expectedRevision"] = QString::number(doc.revision());
    arguments["query"] = name;
    return inspectDocument(doc, arguments)["data"].toObject();
}
// A terrain document at UTM scale: site origin, a painted and tagged tile, a second
// ordinary body.
struct Fixture {
    Document doc;
    Id terrain{};
    std::vector<Id> tiles;
    std::vector<Vec3> source; // Site coordinates of every mesh vertex.
};
Fixture fixture() {
    Fixture f;
    const SiteOrigin origin{512300, 5234500, 0, "EPSG:32633", {"terrain-utm.dxf", std::string(64, 'b')}};
    setSiteOrigin(f.doc, origin);
    auto mesh = grid(60, 45, .37, {});
    for (auto &point : mesh.vertices) {
        const Vec3 utm{512345.678 + point.x, 5234567.891 + point.y, 1850.125 + point.z};
        f.source.push_back(utm);
        point = siteToModel(origin, utm);
    }
    const auto built = createTerrain(f.doc, mesh, record(mesh), 0, "Site terrain");
    f.terrain = built.terrain;
    f.tiles = built.tiles;
    f.doc.paint(f.tiles.front(), {.2f, .6f, .3f});
    const auto tag = createTag(f.doc, "Topography");
    assignTag(f.doc, f.terrain, tag);
    f.doc.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
    return f;
}
void persistence(const QTemporaryDir &files) {
    auto f = fixture();
    check(f.tiles.size() > 1, "The fixture has several tiles");
    check(nativeDocumentVersionFor(f.doc) == 29, "Terrain needs schema 29");
    const auto bytes = encodeContainer(f.doc);
    const auto head = manifest(bytes);
    check(head["requiredFeatures"].toArray().last() == "terrain-v1" &&
              head["requiredFeatures"].toArray().size() == 28,
          "Schema 29 requires terrain-v1 after component-parameters-v1");
    const auto chunks = head["chunks"].toArray();
    check(chunks[0].toObject()["encoding"] == "json-v29" && documentTree(bytes)["version"] == 29,
          "Schema 29 document chunk");
    const auto mesh = chunks.last().toObject();
    check(mesh["kind"] == "terrain-mesh" && mesh["encoding"] == "f64-u32-le-v1" &&
              mesh["sha256"] == sha256(chunk(bytes, chunks.size() - 1)),
          "The terrain-mesh chunk carries its SHA-256 in the manifest");
    const auto tree = documentTree(bytes);
    check(tree["terrainMesh"].isNull() && tree["site"].toObject()["crs"] == "EPSG:32633",
          "The model JSON holds the site and refers to the packaged mesh");
    for (const auto &value : tree["bodies"].toArray()) {
        const auto body = value.toObject();
        if (body["terrainTile"].toBool())
            check(body["vertices"].toArray().isEmpty() && body["faces"].toArray().isEmpty() &&
                      body["edges"].toArray().isEmpty() &&
                      body["edgeAppearances"].toObject().isEmpty(),
                  "Tile meshes, edges and implied appearance are not in the JSON");
    }
    // Save, reopen, save: identical bytes, identical records.
    const auto path = files.filePath("terrain.sketchyup");
    saveDocument(f.doc, path);
    const auto saved = read(path);
    const auto reopened = loadDocument(path);
    check(sameDocument(f.doc, reopened), "Reopened records are identical");
    const auto again = files.filePath("terrain-again.sketchyup");
    auto copy = loadDocument(path);
    saveDocument(copy, again);
    check(read(again) == saved, "Save, reopen and save give identical bytes");
    check(encodeContainer(decodeContainer(bytes)) == bytes, "Container round trip is canonical");
    check(!terrainChangedSinceGenerated(reopened, f.terrain), "The digest survives a save");
    // The inline JSON form (legacy raw documents and in-memory comparisons).
    const auto json = encodeDocument(f.doc);
    check(QJsonDocument::fromJson(json).object()["terrainMesh"].isString() &&
              sameDocument(decodeDocument(json), f.doc) &&
              encodeDocument(decodeDocument(json)) == json,
          "The inline JSON form embeds and restores the mesh");
    // Precision: every vertex back in site coordinates within 1e-6 m, bit-identical
    // through the save.
    double worst = 0;
    for (const auto id : f.tiles) {
        const auto &before = *f.doc.bodies().at(id), &after = *reopened.bodies().at(id);
        check(before.surface == after.surface, "Tile surfaces are bit-identical after reopening");
        const auto world = reopened.worldTransform(id);
        for (const auto &[vertex, point] : after.surface.vertices) {
            const auto site = modelToSite(*reopened.site(), world.point(point));
            double nearest = 1e9;
            for (const auto &candidate : f.source)
                if (std::abs(candidate.x - site.x) < 1e-3 && std::abs(candidate.y - site.y) < 1e-3)
                    nearest = std::min(nearest, length(candidate - site));
            worst = std::max(worst, nearest);
        }
    }
    check(worst <= 1e-6, "UTM-scale vertices survive conversion and a round trip within 1e-6 m");
    std::cout << "UTM round-trip error: " << worst << " m\n";
    // Native inspection and migration agree on schema 29.
    const auto inspected = inspectNativeFile(path);
    check(inspected["documentVersion"] == 29 && !inspected["migrationNeeded"].toBool(),
          "A schema-29 save needs no migration");
    // Undo back to no terrain and no site: the document writes its lower schema again
    // and the bytes are those of the same document never given a terrain.
    Document plain;
    plain.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
    const auto before = encodeContainer(plain);
    setSiteOrigin(plain, SiteOrigin{1000, 2000, 0, {}, {}});
    check(nativeDocumentVersionFor(plain) == 29, "A site origin alone is schema 29");
    const auto siteOnly = encodeContainer(plain);
    check(manifest(siteOnly)["chunks"].toArray().size() == 1 &&
              documentTree(siteOnly)["terrainMesh"].isNull() &&
              sameDocument(decodeContainer(siteOnly), plain),
          "A site-only document has no mesh chunk");
    plain.undo();
    // Only the revision counter differs after the round trip through history.
    auto unrevised = [](const QByteArray &container) {
        auto tree = documentTree(container);
        tree.remove("revision");
        auto head = manifest(container);
        head.remove("revision");
        auto chunks = head["chunks"].toArray();
        auto first = chunks[0].toObject();
        first.remove("bytes");
        first.remove("sha256");
        chunks[0] = first;
        head["chunks"] = chunks;
        return QJsonDocument(tree).toJson(QJsonDocument::Compact) +
               QJsonDocument(head).toJson(QJsonDocument::Compact);
    };
    check(nativeDocumentVersionFor(plain) == 26 && unrevised(encodeContainer(plain)) == unrevised(before),
          "Without terrain or site the document keeps schema 26 and its exact encoding");
    // Explode makes ordinary geometry; the document no longer needs schema 29 once the
    // site is gone too (here: a terrain-only document).
    Document only;
    const auto small = grid(3, 3, 1, {});
    const auto created = createTerrain(only, small, record(small));
    explodeTerrain(only, created.terrain);
    check(nativeDocumentVersionFor(only) == 26 &&
              documentTree(encodeContainer(only))["version"] == 26,
          "An exploded terrain is ordinary schema-26 geometry");
    // R095.f (ADR 0168 section 6, "a terrain-free document still writes 28"): a document that
    // needs schema 28 (component parameters) keeps it without terrain, a terrain makes it 29,
    // and Undo returns it to 28.
    Document parametric;
    const auto panel = parametric.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
    const auto component = createComponent(parametric, panel, "Panel");
    ComponentParameterSpec width;
    width.name = "width";
    width.label = "Width";
    width.expression = "1 m";
    defineComponentParameter(parametric, component.definition, width);
    check(nativeDocumentVersionFor(parametric) == 28 &&
              documentTree(encodeContainer(parametric))["version"] == 28,
          "A terrain-free document with parameters writes schema 28");
    const auto parametricMesh = grid(3, 3, 1, {10, 0, 0});
    createTerrain(parametric, parametricMesh, record(parametricMesh));
    check(nativeDocumentVersionFor(parametric) == 29 &&
              documentTree(encodeContainer(parametric))["version"] == 29,
          "A terrain makes it schema 29");
    parametric.undo();
    check(nativeDocumentVersionFor(parametric) == 28 &&
              documentTree(encodeContainer(parametric))["version"] == 28 &&
              sameDocument(decodeContainer(encodeContainer(parametric)), parametric),
          "Without the terrain it writes schema 28 again");
}
void decoderLimits(const QTemporaryDir &files) {
    auto f = fixture();
    const auto bytes = encodeContainer(f.doc);
    const auto chunks = manifest(bytes)["chunks"].toArray();
    const auto mesh = chunk(bytes, chunks.size() - 1);
    check(decodeTerrainMesh(mesh).size() == f.tiles.size(), "The chunk decodes");
    auto header = [](std::uint32_t tiles) {
        QByteArray out("SKTMESH1", 8);
        char count[4];
        qToLittleEndian<quint32>(tiles, count);
        out.append(count, 4);
        return out;
    };
    auto entry = [](std::uint64_t body, std::uint32_t vertices, std::uint32_t triangles) {
        QByteArray out(16, '\0');
        qToLittleEndian<quint64>(body, out.data());
        qToLittleEndian<quint32>(vertices, out.data() + 8);
        qToLittleEndian<quint32>(triangles, out.data() + 12);
        return out;
    };
    // Oversized counts refuse from the header alone: no tile data follows.
    auto total = header(123);
    for (std::uint32_t i = 0; i < 123; ++i)
        total += entry(i + 1, 3000, 2048);
    const auto overLimit = rejection([&] { decodeTerrainMesh(total); });
    check(overLimit.find("terrain limit") != std::string::npos &&
              overLimit.find("251,904") != std::string::npos,
          "Total triangles over 250,000 refuse before allocation: " + overLimit);
    check(rejection([&] { decodeTerrainMesh(header(1) + entry(1, 3, 2049)); })
                  .find("tile limits") != std::string::npos,
          "A tile over 2,048 triangles refuses");
    check(rejection([&] { decodeTerrainMesh(header(1) + entry(1, 3001, 1000)); })
                  .find("tile limits") != std::string::npos,
          "More than three vertices per triangle refuses");
    check(rejection([&] { decodeTerrainMesh(header(10001)); }).find("tile count") !=
              std::string::npos,
          "More tiles than the body limit refuses");
    check(rejection([&] { decodeTerrainMesh(header(1) + entry(1, 3, 1)); }).find("length") !=
              std::string::npos,
          "A short chunk refuses");
    check(rejection([&] { decodeTerrainMesh(header(2) + entry(2, 3, 1) + entry(1, 3, 1)); })
                  .find("ascending") != std::string::npos,
          "Tiles must be in ID order");
    {
        auto bad = header(1) + entry(1, 3, 1);
        bad += QByteArray(72, '\0');
        QByteArray indices(12, '\0');
        qToLittleEndian<quint32>(0, indices.data());
        qToLittleEndian<quint32>(1, indices.data() + 4);
        qToLittleEndian<quint32>(3, indices.data() + 8);
        check(rejection([&] { decodeTerrainMesh(bad + indices); }).find("out of range") !=
                  std::string::npos,
              "Triangle indices are range-checked");
    }
    check(!rejection([&] { decodeTerrainMesh(QByteArray(NativeLimits::terrainMeshBytes + 1, 'x')); })
               .empty(),
          "An oversized chunk refuses");
    // Through the container: a consistent but oversized chunk (its SHA-256 updated)
    // refuses, and so do a missing or disagreeing mesh.
    check(!rejection([&] { decodeContainer(withMesh(bytes, total)); }).empty(),
          "The container decoder refuses oversized counts");
    auto shifted = mesh;
    qToLittleEndian<quint64>(999999, shifted.data() + 12);
    check(!rejection([&] { decodeContainer(withMesh(bytes, shifted)); }).empty(),
          "A chunk naming other bodies refuses");
    auto corrupted = bytes;
    corrupted[corrupted.size() - 1] ^= 1;
    check(rejection([&] { decodeContainer(corrupted); }).find("checksum") != std::string::npos,
          "A corrupted mesh refuses by checksum");
    // A tampered but consistent mesh loads and reports "changed since generated".
    auto moved = mesh;
    const auto firstVertex = 12 + qsizetype(f.tiles.size()) * 16;
    double z{};
    std::memcpy(&z, moved.constData() + firstVertex + 16, 8);
    z += .25;
    std::memcpy(moved.data() + firstVertex + 16, &z, 8);
    const auto tampered = decodeContainer(withMesh(bytes, moved));
    check(terrainChangedSinceGenerated(tampered, f.terrain),
          "A mesh that no longer matches its digest loads as changed since generated");
    // A schema newer than this build refuses with the newer-format error.
    auto future = documentTree(bytes);
    future["version"] = nativeDocumentVersion + 1;
    check(rejection([&] {
              decodeDocument(QJsonDocument(future).toJson(QJsonDocument::Compact));
          }).find("Unsupported document format, version") != std::string::npos,
          "A newer schema refuses");
    // Terrain fields are refused below schema 29.
    auto older = QJsonDocument::fromJson(encodeDocument(f.doc)).object();
    older["version"] = 28;
    older.remove("site");
    older.remove("terrainMesh");
    check(!rejection([&] {
               decodeDocument(QJsonDocument(older).toJson(QJsonDocument::Compact));
           }).empty(),
          "Schema 28 cannot carry terrain");
    // A released reader (SKETCHYUP_PRIOR_CLI, 1.2.x) refuses a schema-29 file.
    const auto prior = qEnvironmentVariable("SKETCHYUP_PRIOR_CLI");
    if (!prior.isEmpty()) {
        const auto path = files.filePath("prior-terrain.sketchyup");
        write(path, bytes);
        QProcess process;
        process.start(prior, {"--validate-native", path});
        check(process.waitForFinished(30000) && process.exitCode() != 0 && read(path) == bytes,
              "The prior release refuses a schema-29 file unchanged");
        std::cout << "prior release refusal: "
                  << process.readAllStandardError().trimmed().toStdString() << '\n';
    } else
        std::cout << "SKETCHYUP_PRIOR_CLI not set; prior-release refusal not exercised\n";
}
void automation() {
    auto f = fixture();
    auto &doc = f.doc;
    // Queries.
    const auto site = query(doc, "site.describe");
    check(site["established"] == true && site["origin"].toObject()["easting"] == 512300,
          "site.describe");
    const auto converted = query(doc, "site.convert",
                                 {{"to", "model"},
                                  {"points", QJsonArray{QJsonArray{512345.678, 5234567.891, 1850}}}});
    const auto local = converted["points"].toArray()[0].toArray();
    check(std::abs(local[0].toDouble() - 45.678) < 1e-9 &&
              std::abs(local[1].toDouble() - 67.891) < 1e-9 && local[2] == 1850,
          "site.convert to model");
    const auto terrain = QString::number(f.terrain);
    const auto described = query(doc, "terrain.describe", {{"body", terrain}});
    check(described["tileCount"].toInt() == int(f.tiles.size()) &&
              described["triangles"].toInt() == 60 * 45 * 2 &&
              described["generator"] == "test/terrain-v1" &&
              described["sources"].toObject()["count"] == 2 &&
              described["provenance"].toObject()["units"] == "usft" &&
              described["changedSinceGenerated"] == false,
          "terrain.describe");
    const auto elevations =
        query(doc, "terrain.elevation_at",
              {{"body", terrain}, {"points", QJsonArray{QJsonArray{45.678, 67.891},
                                                        QJsonArray{-1000, -1000}}}});
    const auto points = elevations["points"].toArray();
    check(points[0].toObject()["found"] == true &&
              std::abs(points[0].toObject()["elevation"].toDouble() - 1850.125) < 1e-9 &&
              points[1].toObject()["found"] == false && points[1].toObject()["elevation"].isNull(),
          "terrain.elevation_at");
    check(code([&] { query(doc, "terrain.describe", {{"body", QString::number(f.tiles[0])}}); }) ==
              "NOT_FOUND",
          "terrain.describe needs a terrain");
    check(code([&] {
              QJsonArray many;
              for (int i = 0; i < 201; ++i)
                  many.append(QJsonArray{0, 0});
              query(doc, "terrain.elevation_at", {{"body", terrain}, {"points", many}});
          }) == "LIMIT_EXCEEDED",
          "Point batches are bounded");
    // Commands: dry run, revision checks and the shared error codes.
    const auto revision = doc.revision();
    const auto reanchor = QJsonArray{
        QJsonObject{{"command", "site.reanchor"}, {"easting", 512000}, {"northing", 5234000}}};
    const auto preview = previewBatch(doc, batch(doc, reanchor));
    check(preview["status"] == "preview" && doc.revision() == revision &&
              doc.site()->easting == 512300,
          "A dry run leaves the document unchanged");
    auto stale = batch(doc, reanchor);
    stale["expectedRevision"] = QString::number(revision + 7);
    check(!rejection([&] { executeBatch(doc, stale); }).empty() && doc.revision() == revision,
          "A stale revision refuses");
    executeBatch(doc, batch(doc, reanchor));
    check(doc.revision() == revision + 1 && doc.site()->easting == 512000 &&
              doc.history().entries.back().label == "Re-anchor site origin",
          "site.reanchor is one labelled edit");
    check(code([&] {
              executeBatch(doc, batch(doc, {QJsonObject{{"command", "site.set"},
                                                        {"easting", 1},
                                                        {"northing", 2}}}));
          }) == "INVALID_REQUEST",
          "site.set refuses when an origin exists");
    const auto tile = QString::number(f.tiles[0]);
    const auto face = QString::number(doc.bodies().at(f.tiles[0])->surface.faces.begin()->first);
    check(code([&] {
              executeBatch(doc, batch(doc, {QJsonObject{{"command", "geometry.push_pull"},
                                                        {"body", tile},
                                                        {"face", face},
                                                        {"distance", 1}}}));
          }) == "UNSUPPORTED_TARGET",
          "geometry.push_pull refuses a terrain tile with UNSUPPORTED_TARGET");
    check(code([&] {
              executeBatch(doc, batch(doc, {QJsonObject{{"command", "terrain.explode"},
                                                        {"body", tile}}}));
          }) == "NOT_FOUND",
          "terrain.explode needs a terrain");
    executeBatch(doc, batch(doc, {QJsonObject{{"command", "terrain.explode"}, {"body", terrain}}}));
    check(!isTerrain(doc, f.terrain) && doc.bodies().at(f.terrain)->kind == BodyKind::Group &&
              doc.history().entries.back().label == "Explode terrain",
          "terrain.explode");
    doc.undo();
    check(isTerrain(doc, f.terrain), "Undo restores the terrain");
    Document empty;
    executeBatch(empty, batch(empty, {QJsonObject{{"command", "site.set"},
                                                  {"easting", 512300},
                                                  {"northing", 5234500},
                                                  {"crs", "EPSG:32633"}}}));
    check(empty.site() && empty.site()->crs == "EPSG:32633" && empty.site()->elevation == 0,
          "site.set establishes the origin");
    check(code([&] {
              executeBatch(empty, batch(empty, {QJsonObject{{"command", "site.reanchor"},
                                                            {"easting", 512300},
                                                            {"northing", 5234500}}}));
          }) == "INVALID_REQUEST",
          "Re-anchor to the same origin refuses");
    // Discovery: MCP exposes every operation and query; the command catalogue has them.
    std::set<QString> tools, commands;
    for (const auto &tool : mcpToolCatalog())
        tools.insert(tool.toObject()["name"].toString());
    for (const auto &command : commandCatalog())
        commands.insert(command.toObject()["name"].toString());
    for (const auto *name : {"site.describe", "site.convert", "terrain.describe",
                             "terrain.elevation_at"})
        check(tools.contains(name), std::string("MCP exposes ") + name);
    for (const auto *name : {"site.set", "site.reanchor", "terrain.explode"})
        check(commands.contains(name), std::string("The command catalogue has ") + name);
    check(mcpToolCatalog().size() <= 64, "The MCP tool list stays within its 64-tool bound");
}
// Re-anchoring moves every stored world position by the same delta and leaves the
// origin-independent records alone; Undo restores the exact encoding.
void reanchorAudit() {
    Document doc;
    setSiteOrigin(doc, SiteOrigin{512300, 5234500, 100, "EPSG:32633", {}});
    const auto box = doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 1, 0}, {0, 1, 0}}});
    doc.addGuide(box, guidePoint({1, 1, 1}));
    const auto inner = doc.addFace({{{5, 5, 0}, {6, 5, 0}, {6, 6, 0}, {5, 6, 0}}});
    const auto group = createGroup(doc, {inner}, "Group");
    const auto asset = createAsset(doc, "Plan", "image/png");
    const auto image = createReferenceImage(doc, {asset, 2, 1, 1});
    const auto worldSection =
        createSection(doc, "World", 0, SectionPlane::through({0, 0, 1}, {0, 0, 1}));
    const auto localSection =
        createSection(doc, "Local", group, SectionPlane::through({0, 0, 1}, {1, 0, 0}));
    auto style = doc.style();
    style.groundHeight = -1;
    doc.setStyle(style);
    SolarSettings solar;
    solar.enabled = true;
    solar.latitude = 47;
    solar.longitude = 9;
    doc.setSolar(solar);
    SceneSnapshot snapshot;
    snapshot.camera = SceneCamera{{1, 2, 3}};
    snapshot.camera->orthographic = true;
    snapshot.section = SceneSection{std::array<double, 4>{0, 0, 1, -2}, {}};
    snapshot.style = doc.style();
    const auto scene = createScene(doc, "Plan", snapshot);
    AnnotationRecord dimension;
    dimension.name = "Width";
    dimension.anchors = {pointAnchor({0, 0, 0}),
                         vertexAnchor(doc, box, doc.bodies().at(box)->surface.vertices.begin()->first)};
    dimension.offset = {0, -1, 0};
    const auto annotation = createAnnotation(doc, dimension);
    const auto sheet = createDrawingSheet(doc, "A-101", {420, 297});
    DrawingViewRecord view;
    view.name = "Plan view";
    view.sheet = sheet;
    view.source = {DrawingViewSourceKind::Scene, scene};
    view.frame = {20, 20, 200, 150};
    const auto viewId = createDrawingView(doc, view);
    DrawingDimensionItem paperDimension;
    paperDimension.view = viewId;
    const auto &vertices = doc.bodies().at(box)->surface.vertices;
    paperDimension.anchors = {vertexAnchor(doc, box, vertices.begin()->first),
                              vertexAnchor(doc, box, std::next(vertices.begin())->first)};
    const auto paperItem = createDrawingItem(doc, sheet, paperDimension);
    DrawingLabelItem label;
    label.target.kind = DrawingLabelTargetKind::View;
    label.target.view = viewId;
    label.target.anchor = vertexAnchor(doc, box, std::prev(vertices.end())->first);
    const auto labelItem = createDrawingItem(doc, sheet, label);
    DrawingTextItem note;
    note.frame = {20, 180, 120, 20};
    note.text = "Notes";
    const auto noteItem = createDrawingItem(doc, sheet, note);

    const Vec3 delta{100, -50, 2};
    const auto before = doc.readSnapshot();
    auto tree = [](const Document &state) {
        auto json = QJsonDocument::fromJson(encodeDocument(state)).object();
        json.remove("revision");
        return QJsonDocument(json).toJson(QJsonDocument::Compact);
    };
    const auto original = tree(doc);
    const auto &site = *doc.site();
    reanchorSite(doc, site.easting - delta.x, site.northing - delta.y, site.elevation - delta.z);
    auto moved = [&](Vec3 a, Vec3 b) { return length(b - (a + delta)) <= 1e-9; };
    auto world = [](const Document &state, Id body, Vec3 local) {
        return state.worldTransform(body).point(local);
    };
    // Moved: geometry and everything placed in it.
    for (const auto body : {box, inner, image})
        check(moved(world(before, body, {}), world(doc, body, {})), "Body moved");
    check(moved(world(before, box, doc.bodies().at(box)->guides.begin()->second.origin),
                world(doc, box, doc.bodies().at(box)->guides.begin()->second.origin)),
          "Guides move with their body");
    check(std::abs(doc.sections().at(worldSection)->plane.distance(Vec3{0, 0, 1} + delta)) < 1e-12,
          "World section plane moved");
    check(doc.style().groundHeight == -1 + delta.z, "Ground height keeps its site elevation");
    const auto &shot = doc.scenes().at(scene)->snapshot;
    check(moved(before.scenes().at(scene)->snapshot.camera->target,
                shot.camera->target) &&
              std::abs((*shot.section->plane)[3] - (-2 - delta.z)) < 1e-12 &&
              shot.style->groundHeight == -1 + delta.z,
          "Saved camera, clip plane and ground height moved");
    const auto &anchors = doc.annotations().at(annotation)->anchors;
    const auto &oldAnchors = before.annotations().at(annotation)->anchors;
    check(moved(oldAnchors[0].fallback, anchors[0].fallback) &&
              moved(oldAnchors[1].fallback, anchors[1].fallback) &&
              doc.annotations().at(annotation)->offset == Vec3{0, -1, 0},
          "Annotation anchors moved; the text offset is a vector");
    const auto measured = measureAnnotation(doc, *doc.annotations().at(annotation));
    const auto measuredBefore = measureAnnotation(before, *before.annotations().at(annotation));
    check(moved(measuredBefore.textPoint, measured.textPoint) &&
              measured.distance && measuredBefore.distance &&
              std::abs(*measured.distance - *measuredBefore.distance) < 1e-9,
          "Annotations measure the same, in the moved place");
    const auto &dimensionItem =
        std::get<DrawingDimensionItem>(doc.drawingItems().at(paperItem)->data);
    const auto &dimensionBefore =
        std::get<DrawingDimensionItem>(before.drawingItems().at(paperItem)->data);
    check(moved(dimensionBefore.anchors[0].fallback, dimensionItem.anchors[0].fallback) &&
              moved(dimensionBefore.anchors[1].fallback, dimensionItem.anchors[1].fallback),
          "Paper dimension anchors moved");
    check(moved(std::get<DrawingLabelItem>(before.drawingItems().at(labelItem)->data)
                    .target.anchor.fallback,
                std::get<DrawingLabelItem>(doc.drawingItems().at(labelItem)->data)
                    .target.anchor.fallback),
          "Paper label anchor moved");
    // Unchanged: origin-independent records.
    check(doc.sections().at(localSection) == before.sections().at(localSection) &&
              doc.bodies().at(inner) == before.bodies().at(inner) &&
              doc.solar() == before.solar() && doc.drawingViews() == before.drawingViews() &&
              doc.drawingSheets() == before.drawingSheets() &&
              doc.drawingItems().at(noteItem) == before.drawingItems().at(noteItem) &&
              doc.assets() == before.assets(),
          "Body-local records, the sun study, views, sheets and paper text are unchanged");
    std::cout << "re-anchor audit: bodies, guides, reference image, world section, ground, "
                 "saved camera/clip/ground, annotation and paper anchors moved\n";
    doc.undo();
    check(tree(doc) == original, "Undo restores the exact encoding");
    doc.redo();
    doc.undo();
    check(tree(doc) == original, "Redo and Undo restore the exact encoding");
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir files;
        check(files.isValid(), "Temporary directory");
        persistence(files);
        decoderLimits(files);
        automation();
        reanchorAudit();
        std::cout << "terrain io tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
