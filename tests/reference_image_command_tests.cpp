#include "automation/commands.hpp"
#include "automation/inspection.hpp"
#include "automation/reference_image_commands.hpp"
#include "automation/staging.hpp"
#include "core/components.hpp"
#include "core/terrain.hpp"
#include "io/document_io.hpp"
#include "io/image_refusal.hpp"
#include "io/orthophoto_import.hpp"
#include "io/texture_image.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <cmath>
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
    throw std::runtime_error("Invalid reference image request accepted");
}
QJsonObject batch(const Document &doc, QJsonArray commands) {
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(doc.identity())},
            {"expectedRevision", QString::number(doc.revision())},
            {"commands", commands}};
}
QJsonObject query(const Document &doc, QString name, QJsonObject arguments = {}) {
    arguments["apiVersion"] = 1;
    arguments["documentId"] = QString::fromStdString(doc.identity());
    arguments["expectedRevision"] = QString::number(doc.revision());
    arguments["query"] = name;
    return arguments;
}
} // namespace

// R107.b2.1 (ADR 0181, R107.b2 design "Inputs", "Zone, CRS and datum", "Placement", "Credits"): the
// importer on the committed fixtures. An import is one edit; a site origin is set only for a new site;
// a site record's zone must match the image's; and each refusal is named by its code.
const std::string referenceFixtures = std::string(SOURCE_DIR) + "/tests/fixtures/reference/";

template <class F> std::string refusedAsRuntime(F action) {
    try {
        action();
    } catch (const std::runtime_error &error) {
        return error.what();
    }
    throw std::runtime_error("The import was not refused");
}

template <class F> ImageRefusal refusedAs(F action) {
    try {
        action();
    } catch (const ImageRefusal &refusal) {
        return refusal;
    }
    throw std::runtime_error("The import was not refused");
}

void orthophotoImports() {
    // A PNG with its world file, and no site: the site is set at the image's centre, rounded to 100 m.
    {
        Document doc;
        const auto revision = doc.revision();
        const auto report = importOrthophoto(doc, QString::fromStdString(referenceFixtures + "north-up.png"),
                                             OrthophotoOptions{30, true});
        check(report["status"] == "imported" && report["format"] == "png" && report["originEstablished"] == true,
              "A PNG imports with its origin established");
        check(doc.revision() != revision, "The import is recorded");
        check(doc.site() && doc.site()->geographic && doc.site()->geographic->utmZone == 30,
              "The new site carries the image's zone");
        // The centre is (499800 + 8, 5710000 - 6), which rounds to 100 m: (499800, 5710000).
        check(doc.site()->easting == 499800 && doc.site()->northing == 5710000,
              "The origin is at the image's centre, rounded to 100 m");
        check(doc.site()->geographic->provenance.kind == "imported" &&
                  doc.site()->geographic->verticalReference == "unknown",
              "The record is imported, with an unknown vertical reference");
        std::size_t images = 0;
        for (const auto &[id, body] : doc.bodies())
            if (body->referenceImage) {
                ++images;
                check(body->referenceImage->georeference && body->referenceImage->georeference->zone == 30 &&
                          body->referenceImage->georeference->columns == 8 &&
                          body->referenceImage->georeference->rows == 6,
                      "The image record holds its zone and its pixel counts");
                const auto corners = referenceImageCorners(*body->referenceImage, doc.worldTransform(id));
                // The image's lower-left corner is at its site position less the origin.
                const auto &geo = *body->referenceImage->georeference;
                check(std::abs(corners[0].x - (geo.easting - doc.site()->easting)) <= 1e-6 &&
                          std::abs(corners[0].y - (geo.northing - 12 - doc.site()->northing)) <= 1e-6,
                      "The placed lower-left corner matches the world file");
            }
        check(images == 1, "One reference image is imported");
        check(encodeContainer(decodeContainer(encodeContainer(doc))) == encodeContainer(doc),
              "An imported orthophoto saves and reopens byte-exactly");
    }

    // A world file and a site with a record: the zone comes from the record, and a different zone is refused.
    {
        Document doc;
        importOrthophoto(doc, QString::fromStdString(referenceFixtures + "north-up.png"), OrthophotoOptions{30, true});
        Document other;
        const auto refused = refusedAs([&] {
            importOrthophoto(doc, QString::fromStdString(referenceFixtures + "utm-rgb-south.tif"), OrthophotoOptions{});
        });
        check(refused.code() == "zone-mismatch", "A GeoTIFF in another zone is refused by name");
        const auto ok = importOrthophoto(doc, QString::fromStdString(referenceFixtures + "utm-rgb-strip-none.tif"),
                                         OrthophotoOptions{});
        check(ok["status"] == "imported" && ok["format"] == "geotiff" && ok["originEstablished"] == false,
              "A GeoTIFF in the site's zone imports against the existing origin");
        check(importOrthophoto(other, QString::fromStdString(referenceFixtures + "utm-rgb-strip-none.tif"),
                               OrthophotoOptions{})["notices"].toArray().isEmpty() == false,
              "A site-less import reports the new origin");
    }

    // A world file with a site that has no record: no zone check, a notice says so, and the zone is the
    // person's choice, which is required.
    {
        Document bare;
        setSiteOrigin(bare, SiteOrigin{499800, 5710000, 0, "EPSG:32630", {"", ""}, {}});
        const auto needed = refusedAsRuntime([&] {
            importOrthophoto(bare, QString::fromStdString(referenceFixtures + "north-up.png"), OrthophotoOptions{});
        });
        check(needed.find("needs its UTM zone") != std::string::npos,
              "A world file with no zone to take is asked for one");
        const auto report = importOrthophoto(bare, QString::fromStdString(referenceFixtures + "north-up.png"),
                                             OrthophotoOptions{30, true});
        bool notified = false;
        for (const auto &notice : report["notices"].toArray())
            notified = notified || notice.toString().contains("no geographic record");
        check(notified && report["originEstablished"] == false, "A site with no record says it is not checked");
    }

    // Ambiguous world files: the first (.pngw) is used, and the other is named as ignored.
    {
        Document doc;
        const auto report = importOrthophoto(doc, QString::fromStdString(referenceFixtures + "ambiguous.png"),
                                             OrthophotoOptions{30, true});
        bool named = false;
        for (const auto &notice : report["notices"].toArray())
            named = named || notice.toString().contains("ambiguous.pgw is ignored");
        check(named, "The ignored world file is named");
        for (const auto &[id, body] : doc.bodies())
            if (body->referenceImage)
                check(body->referenceImage->georeference->easting == 499800 &&
                          body->referenceImage->georeference->source.worldFile == "ambiguous.pngw",
                      "The first world file is used");
    }

    // A rotated world file is refused by its code; a zone band refusal names the zone.
    {
        Document doc;
        const auto rotated = refusedAs([&] {
            importOrthophoto(doc, QString::fromStdString(referenceFixtures + "refuse-rotated.png"), OrthophotoOptions{30, true});
        });
        check(rotated.code() == "world-rotated", "A rotated world file is named");
        const auto band = refusedAs([&] {
            importOrthophoto(doc, QString::fromStdString(referenceFixtures + "refuse-ortho-zone-band.tif"), OrthophotoOptions{});
        });
        check(band.code() == "zone-band", "A corner outside the zone's band is named");
        check(doc.bodies().empty() && !doc.site(), "A refused import changes nothing");
    }

    // A PNG is stored as its own bytes, so a file over 16 MiB is refused as stored-size; a file that is not
    // an image is refused as image-type whatever its name.
    {
        QTemporaryDir scratch;
        QFile big(scratch.filePath("big.png"));
        check(big.open(QIODevice::WriteOnly), "the scratch PNG is written");
        big.write("\x89PNG\r\n\x1a\n");
        big.write(QByteArray(17 * 1024 * 1024, '\0'));
        big.close();
        QFile world(scratch.filePath("big.pngw"));
        check(world.open(QIODevice::WriteOnly) && world.write("2\n0\n0\n-2\n499801\n5709999\n") > 0,
              "the scratch world file is written");
        world.close();
        Document doc;
        const auto oversized = refusedAs([&] {
            importOrthophoto(doc, scratch.filePath("big.png"), OrthophotoOptions{30, true});
        });
        check(oversized.code() == "stored-size" && doc.bodies().empty(),
              "A PNG over 16 MiB is refused as stored-size, and nothing is imported");
        QFile text(scratch.filePath("notes.png"));
        check(text.open(QIODevice::WriteOnly) && text.write("This is text.\n") > 0, "the scratch text is written");
        text.close();
        const auto notImage = refusedAs([&] {
            importOrthophoto(doc, scratch.filePath("notes.png"), OrthophotoOptions{30, true});
        });
        check(notImage.code() == "image-type", "A file that is not an image is refused as image-type");
    }

    // The image's credit is stored with the placement, and too long a credit is refused.
    {
        Document doc;
        importOrthophoto(doc, QString::fromStdString(referenceFixtures + "utm-rgb-strip-none.tif"),
                         OrthophotoOptions{0, true, "WGS84", "Survey (test)"});
        for (const auto &[id, body] : doc.bodies())
            if (body->referenceImage)
                check(body->referenceImage->georeference->credit == "Survey (test)",
                      "The credit is kept with the placement");
        const auto long_ = refusedAs([&] {
            importOrthophoto(doc, QString::fromStdString(referenceFixtures + "utm-rgb-strip-none.tif"),
                             OrthophotoOptions{0, true, "WGS84", std::string(257, 'c')});
        });
        check(long_.code() == "credit", "A credit over 256 bytes is refused");
    }
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        Document doc;
        const auto png = encodeTexturePng(TextureImage(2, 1, {255, 0, 0, 255, 0, 255, 0, 128}));
        const QJsonObject import{{"command", "asset.import"},
                                 {"name", "Reference pixels"},
                                 {"mediaType", "image/png"},
                                 {"data", QString::fromLatin1(png.toBase64())}};
        const QJsonObject create{{"command", "reference_image.create"},
                                 {"name", "Plan"},
                                 {"asset", "1"},
                                 {"width", 4},
                                 {"height", 2},
                                 {"opacity", .8},
                                 {"position", QJsonArray{10, 20, 0}}};
        const auto original = encodeContainer(doc);
        const auto request = batch(doc, {import, create});
        previewBatch(doc, request);
        check(encodeContainer(doc) == original,
              "Image and asset preview leaves live state unchanged");
        StagingSession staging;
        const auto prepared = staging.prepare(doc, request);
        const auto token = prepared["stageId"].toString();
        const auto proposal = staging.proposal(doc, token);
        check(doc.assets().empty() && proposal->snapshot().assets().size() == 1 &&
                  proposal->snapshot().bodies().at(1)->referenceImage->width == 4,
              "Private image proposal retains pixels and placement separately from live state");
        check(staging.changes(doc, token)["changes"].toArray().size() >= 2,
              "Stage reports image and asset changes");
        doc.applyPrepared(*proposal);
        check(doc.history().total == 1, "Image plus managed asset publish as one history entry");
        doc.undo();
        check(doc.assets().empty() && doc.bodies().empty() && !doc.dirty(),
              "One Undo removes both image and imported pixels");
        doc.redo();
        const auto frozen = encodeContainer(doc);
        auto detailRequest = query(doc, "reference_image.describe", {{"body", "1"}});
        const auto detail = inspectDocument(doc, detailRequest)["data"].toObject();
        check(detail["pixels"].toObject()["status"] == "ready" &&
                  detail["pixels"].toObject()["width"] == 2 &&
                  detail["pixels"].toObject()["height"] == 1,
              "Image inspection decodes bounded original pixels");
        const auto page =
            inspectDocument(doc, query(doc, "reference_images.query"))["data"].toObject();
        check(page["total"] == 1 && !page["items"].toArray()[0].toObject().contains("pixels"),
              "Reference listing is bounded and does not decode every asset");
        check(inspectDocument(doc,
                              query(doc, "entities.query", {{"kind", "reference_image"}}))["data"]
                      .toObject()["total"] == 1,
              "Entity kind filter distinguishes image planes from geometry");
        check(encodeContainer(doc) == frozen, "Image inspection is read-only");
        const QJsonObject calibrate{{"command", "reference_image.calibrate"},
                                    {"body", "1"},
                                    {"first", QJsonArray{.25, .5}},
                                    {"second", QJsonArray{.75, .5}},
                                    {"knownLength", 5}};
        executeBatch(doc, batch(doc, {calibrate}));
        check(doc.bodies().at(1)->referenceImage->width == 10 &&
                  doc.worldTransform(1).point({2.5, 2.5, 0}) == Vec3{11, 21, 0},
              "Shared calibration preserves the first world anchor");
        rejects([&] { inspectDocument(doc, detailRequest); });
        const auto stable = encodeContainer(doc);
        for (const auto &change : {QJsonObject{{"opacity", 1.1}}, QJsonObject{{"width", 0}},
                                   QJsonObject{{"asset", "999"}}, QJsonObject{{"name", ""}},
                                   QJsonObject{{"path", "/external.png"}}}) {
            auto invalid = change;
            invalid["command"] = "reference_image.update";
            invalid["body"] = "1";
            rejects([&] { executeBatch(doc, batch(doc, {invalid})); });
            check(encodeContainer(doc) == stable, "Invalid image edits reject atomically");
        }
        rejects([&] {
            executeBatch(doc, batch(doc, {QJsonObject{{"command", "reference_image.update"},
                                                      {"body", "1"}}}));
        });
        rejects([&] {
            executeBatch(doc, batch(doc, {QJsonObject{{"command", "reference_image.update"},
                                                      {"body", "1"},
                                                      {"opacity", .8}}}));
        });
        auto invalidCalibration = calibrate;
        invalidCalibration["second"] = invalidCalibration["first"];
        rejects([&] { executeBatch(doc, batch(doc, {invalidCalibration})); });
        rejects([&] {
            executeBatch(
                doc, batch(doc, {QJsonObject{{"command", "reference_image.update"},
                                             {"body", "1"},
                                             {"opacity", .5}},
                                 QJsonObject{{"command", "geometry.delete"}, {"body", "999"}}}));
        });
        check(encodeContainer(doc) == stable, "Later command failure rolls back image changes");
        const auto component = createComponent(doc, 1, "Image component");
        const auto second =
            placeComponent(doc, component.definition, Transform::translation({20, 0, 0}));
        Id firstImage{}, otherImage{};
        for (const auto &[id, body] : doc.bodies())
            if (body->referenceImage) {
                if (body->parent == component.instance)
                    firstImage = id;
                if (body->parent == second.instance)
                    otherImage = id;
            }
        check(firstImage && otherImage, "Both image placements exist");
        executeBatch(doc,
                     batch(doc, {QJsonObject{{"command", "component.edit_instance"},
                                             {"body", QString::number(component.instance)},
                                             {"commands", QJsonArray{QJsonObject{
                                                              {"command", "reference_image.update"},
                                                              {"body", QString::number(firstImage)},
                                                              {"opacity", .2}}}}}}));
        check(doc.bodies().at(firstImage)->referenceImage->opacity == .2 &&
                  doc.bodies().at(otherImage)->referenceImage->opacity == .8,
              "Instance reference editing leaves other component placements unchanged");
        doc.undo();
        check(doc.bodies().at(firstImage)->referenceImage->opacity == .8,
              "Instance reference edit Undo restores the shared placement");
        const auto encoded = encodeContainer(doc);
        check(encodeContainer(decodeContainer(encoded)) == encoded,
              "Command-authored component images roundtrip exactly");
        orthophotoImports();
        std::cout
            << "Reference image commands, inspection, atomic staging and instance scopes passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
