#include "core/assets.hpp"
#include "core/components.hpp"
#include "core/entity_measure.hpp"
#include "core/groups.hpp"
#include "core/reference_images.hpp"
#include "integrations/glb_export.hpp"
#include "io/document_io.hpp"
#include "io/recovery.hpp"
#include "io/reference_image_io.hpp"
#include "io/texture_image.hpp"
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
    throw std::runtime_error("Invalid reference-image edit accepted");
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        Document doc;
        const auto png = encodeTexturePng(TextureImage(2, 1, {255, 0, 0, 255, 0, 255, 0, 128}));
        const auto asset = createAsset(
            doc, "Reference pixels", "image/png",
            std::make_shared<AssetPayload>(std::vector<std::uint8_t>(png.begin(), png.end())));
        const ReferenceImage image{asset, 4, 2, .6};
        const auto id = createReferenceImage(doc, image, Transform::translation({10, 20, 0}));
        check(doc.bodies().at(id)->kind == BodyKind::ReferenceImage &&
                  doc.worldTriangles(id).empty() && doc.bodies().at(id)->surface.vertices.empty(),
              "Image planes own no modeled topology");
        Selection selection;
        check(selection.selectable(doc, {id, SelectionKind::Body, 0}) &&
                  !selection.inContext(doc, id),
              "Images select as whole entities without joining raw geometry");
        rejects([&] { selection.enter(doc, id); });
        const auto measured = measureEntity(doc, {id, SelectionKind::Body, 0});
        check(measured.world.bounds && measured.world.bounds->dimensions() == Vec3{4, 2, 0} &&
                  measured.vertices == 0 && measured.faces == 0 && measured.world.area == 0,
              "Reference bounds remain distinct from modeled measurements");
        const auto frozen = doc.readSnapshot();
        doc.markSaved();
        const auto history = doc.history().total;
        calibrateReferenceImage(doc, id, {.25, .5}, {.75, .5}, 5);
        const auto calibrated = *doc.bodies().at(id)->referenceImage;
        check(calibrated.width == 10 && calibrated.height == 5 &&
                  doc.worldTransform(id).point(referenceImagePoint(calibrated, {.25, .5})) ==
                      Vec3{11, 21, 0} &&
                  doc.history().total == history + 1,
              "Calibration publishes dimensions and anchored placement in one edit");
        doc.undo();
        check(*doc.bodies().at(id)->referenceImage == image && !doc.dirty(),
              "Calibration Undo restores image and saved state");
        doc.redo();
        check(*frozen.bodies().at(id)->referenceImage == image,
              "Read snapshot retains earlier image dimensions");
        const auto bytes = encodeContainer(doc);
        check(encodeContainer(decodeContainer(bytes)) == bytes,
              "Image placement and embedded pixels roundtrip byte-exactly");
        auto raw = QJsonDocument::fromJson(encodeDocument(doc)).object();
        check(raw["version"] == 24, "Reference image schema is explicit");
        auto oldSchema = raw;
        oldSchema["version"] = 23;
        rejects([&] { decodeDocument(QJsonDocument(oldSchema).toJson()); });
        const auto encoded = encodeReferenceImage(image);
        check(decodeReferenceImage(encoded) == image, "Strict image codec retains exact fields");
        for (const auto &key : encoded.keys()) {
            auto missing = encoded;
            missing.remove(key);
            rejects([&] { decodeReferenceImage(missing); });
            auto null = encoded;
            null[key] = QJsonValue::Null;
            rejects([&] { decodeReferenceImage(null); });
        }
        auto extra = encoded;
        extra["path"] = "/external/image.png";
        rejects([&] { decodeReferenceImage(extra); });
        for (const auto bad : {"0", "01", "-1", "18446744073709551616"}) {
            auto invalid = encoded;
            invalid["asset"] = bad;
            rejects([&] { decodeReferenceImage(invalid); });
        }
        auto invalid = image;
        invalid.asset = 999;
        rejects([&] { setReferenceImage(doc, id, invalid); });
        rejects([&] { eraseAsset(doc, asset); });
        rejects([&] { doc.addWire(id, {0, 0, 0}, {1, 0, 0}); });
        rejects([&] { createReferenceImage(doc, image, {}, id); });
        for (int variant = 0; variant < 3; ++variant) {
            const auto before = doc.bodies().at(id);
            auto hybrid = std::make_shared<Body>(*before);
            if (variant == 0)
                hybrid->kind = BodyKind::Group;
            if (variant == 1)
                hybrid->referenceImage.reset();
            if (variant == 2)
                hybrid->surface.vertex({0, 0, 0});
            rejects(
                [&] { doc.apply({"Invalid image body", {{id, before, hybrid}}}, doc.revision()); });
        }
        auto locked = doc.readSnapshot();
        setEntityState(locked, id, {}, true);
        const auto lockedBytes = encodeContainer(locked);
        rejects([&] { calibrateReferenceImage(locked, id, {0, 0}, {1, 1}, 1); });
        check(encodeContainer(locked) == lockedBytes,
              "Locked images reject calibration atomically");
        check(encodeContainer(doc) == bytes, "Failed image edits and asset deletion are atomic");
        replaceAsset(doc, asset, {});
        check(doc.bodies().at(id)->referenceImage == calibrated &&
                  decodeTextureImage(*doc.assets().at(asset)).status == TextureImageStatus::Missing,
              "Missing pixels preserve explicit reference placement");
        check(encodeContainer(decodeContainer(encodeContainer(doc))) == encodeContainer(doc),
              "Missing image assets roundtrip explicitly");
        doc.undo();
        QTemporaryDir directory;
        QString recoveryKey;
        {
            RecoveryWriter writer(directory.path(), QString::fromStdString(doc.identity()));
            recoveryKey = writer.write(captureRecovery(doc)).key;
        }
        const auto recovered = readRecovery(directory.path(), recoveryKey);
        check(recovered.verified && recovered.document &&
                  encodeContainer(*recovered.document) == encodeContainer(doc),
              "Durable recovery retains image records and pixels");
        const auto prepared = doc.prepareEdit([&](Document &candidate) {
            auto next = calibrated;
            next.opacity = .2;
            setReferenceImage(candidate, id, next);
        });
        check(doc.bodies().at(id)->referenceImage->opacity == .6 &&
                  prepared.snapshot().bodies().at(id)->referenceImage->opacity == .2,
              "Prepared reference changes remain private");
        doc.applyPrepared(prepared);
        rejects([&] { doc.applyPrepared(prepared); });
        doc.undo();
        const auto group = createGroup(doc, {id}, "Reference folder");
        reparentPreservingWorld(doc, id, 0);
        check(doc.worldTransform(id).point(referenceImagePoint(calibrated, {.25, .5})) ==
                  Vec3{11, 21, 0},
              "Group and reparent retain image calibration");
        doc.erase(group);
        const auto component = createComponent(doc, id, "Reference component");
        check(doc.bodies().at(id)->kind == BodyKind::Group && !doc.bodies().at(id)->referenceImage,
              "Component normalization moves the image into a distinct child");
        const auto instance =
            placeComponent(doc, component.definition, Transform::translation({20, 0, 0}));
        size_t references{};
        for (const auto &[bodyId, body] : doc.bodies())
            references += body->referenceImage.has_value();
        check(references == 2 && instance.instance != component.instance,
              "Component placements preserve typed image members");
        check(encodeContainer(decodeContainer(encodeContainer(doc))) == encodeContainer(doc),
              "Component reference images and shared assets roundtrip exactly");
        Id member{};
        for (const auto &[memberId, body] : doc.definitions().at(component.definition)->members)
            if (body->referenceImage)
                member = memberId;
        check(member != 0, "Canonical definition retains an image member");
        editComponentDefinition(doc, component.definition, [&](Document &draft) {
            auto next = *draft.bodies().at(member)->referenceImage;
            next.opacity = .25;
            return setReferenceImage(draft, member, next);
        });
        for (const auto &[bodyId, body] : doc.bodies())
            if (body->referenceImage)
                check(body->referenceImage->opacity == .25,
                      "Shared component edits update every reference image placement");
        doc.undo();
        doc.setDisplayUnits(DisplayUnit::FeetInches);
        doc.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
        const auto exported = exportGlb(RenderSnapshot::capture(doc));
        check(exported.manifest["losses"].toObject()["referenceImagesOmitted"] == 2,
              "Surface export explicitly reports omitted visual references");
        const auto migrated =
            loadDocument(QStringLiteral(SOURCE_DIR "/tests/fixtures/sun-study-v23.sketchyup"));
        check(migrated.solar().enabled && migrated.scenes().size() == 3 &&
                  migrated.bodies().size() == 1 &&
                  !migrated.bodies().begin()->second->referenceImage,
              "Actual schema-23 study migrates without invented reference images");
        std::cout << "Reference image records, assets, calibration history, components and "
                     "persistence passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
