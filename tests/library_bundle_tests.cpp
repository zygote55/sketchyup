#include "core/annotations.hpp"
#include "core/assets.hpp"
#include "core/components.hpp"
#include "core/materials.hpp"
#include "core/scenes.hpp"
#include "core/sections.hpp"
#include "core/tags.hpp"
#include "io/library_bundle.hpp"
#include "io/texture_image.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QtEndian>
#include <iostream>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Expected library bundle rejection");
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        Document source(DisplayUnit::Millimeters);
        const auto body = source.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 1, 0}, {0, 1, 0}}});
        const auto image = encodeTexturePng(TextureImage(2, 1, {255, 0, 0, 255, 0, 255, 0, 255}));
        const auto asset = createAsset(
            source, "Embedded appearance", "image/png",
            std::make_shared<AssetPayload>(std::vector<std::uint8_t>(image.begin(), image.end())));
        const auto material = createMaterial(source, "Material", {1, 1, 1}, 1, asset);
        assignMaterial(source, body, {}, material);
        const auto section = createSection(source, "Default section", 0, {{1, 0, 0}, -.5});
        setActiveSection(source, 0, section);
        AnnotationRecord annotation;
        annotation.name = "Width";
        annotation.anchors = {pointAnchor({0, 0, 0}), pointAnchor({2, 0, 0})};
        createAnnotation(source, annotation);
        auto style = source.style();
        style.axesVisible = false;
        source.setStyle(style);
        createComponent(source, body, "Starting component");
        SceneSnapshot scene;
        scene.camera = SceneCamera{};
        scene.style = style;
        const auto defaultScene = createScene(source, "Starting view", scene);
        TemplateMetadata metadata{
            "Metric room", "Embedded starting model", {"Metric", "Room"}, defaultScene};
        const auto before = encodeContainer(source);
        const auto history = source.history().total;
        const auto bundle = encodeTemplateBundle(source, metadata, image);
        const auto decoded = decodeTemplateBundle(bundle);
        check(decoded.metadata.name == metadata.name &&
                  decoded.metadata.labels == metadata.labels && decoded.thumbnailPng == image,
              "Template manifest and thumbnail round trip");
        check(encodeContainer(decoded.document) == before && encodeContainer(source) == before &&
                  source.history().total == history,
              "Complete embedded native payload and source preserved");
        auto first = instantiateTemplate(decoded), second = instantiateTemplate(decoded);
        check(first.identity() != source.identity() && first.identity() != second.identity() &&
                  first.dirty() && !first.canUndo() && first.revision() == 0,
              "Every template instance has fresh unsaved identity and empty history");
        check(first.displayUnits() == DisplayUnit::Millimeters && first.style() == style &&
                  first.activeSections() == source.activeSections() &&
                  first.annotations().size() == 1 && first.definitions().size() == 1 &&
                  first.instances().size() == 1 && first.scenes().contains(defaultScene) &&
                  first.materials().size() == 1 &&
                  first.assets().at(asset)->payload->bytes() ==
                      source.assets().at(asset)->payload->bytes(),
              "Template defaults, sections, dimensions and embedded resources retained");
        const auto secondBefore = encodeContainer(second);
        first.move(body, {5, 0, 0});
        check(encodeContainer(second) == secondBefore && encodeContainer(source) == before &&
                  encodeContainer(decoded.document) == before,
              "Editing a new model never mutates template or sibling instance");
        QTemporaryDir files;
        check(files.isValid(), "Scratch folder");
        const auto path = files.filePath("Room.sketchylib");
        writeTemplateBundle(bundle, path);
        const auto relocated = files.filePath("Relocated.sketchylib");
        check(QFile::rename(path, relocated), "Bundle relocation");
        check(loadTemplateBundle(relocated).thumbnailPng == image,
              "Relocated single-file bundle resolves all resources");
        rejects([&] { writeTemplateBundle(bundle, relocated); });
        QFile retained(relocated);
        check(retained.open(QIODevice::ReadOnly) && retained.readAll() == bundle,
              "Library publication never replaces existing file");
        for (qsizetype length : {qsizetype(0), qsizetype(8), qsizetype(23), bundle.size() - 1})
            rejects([&] { decodeTemplateBundle(bundle.left(length)); });
        rejects([&] { decodeTemplateBundle(bundle + ' '); });
        auto corrupt = bundle;
        corrupt[corrupt.size() - 1] ^= 1;
        rejects([&] { decodeTemplateBundle(corrupt); });
        corrupt = bundle;
        qToLittleEndian<quint64>(UINT64_MAX, corrupt.data() + 12);
        rejects([&] { decodeTemplateBundle(corrupt); });
        corrupt = bundle;
        corrupt[7] = 2;
        rejects([&] { decodeTemplateBundle(corrupt); });
        const auto absent = files.filePath("Bad.sketchylib");
        rejects([&] { writeTemplateBundle(corrupt, absent); });
        check(!QFile::exists(absent), "Invalid bundle creates no file");
        auto bad = metadata;
        bad.defaultScene = 999;
        rejects([&] { encodeTemplateBundle(source, bad, image); });
        bad = metadata;
        bad.labels = {"Room", "room"};
        rejects([&] { encodeTemplateBundle(source, bad, image); });
        bad = metadata;
        bad.name = "\n";
        rejects([&] { encodeTemplateBundle(source, bad, image); });
        auto hugeImage = image;
        qToBigEndian<quint32>(1025, hugeImage.data() + 16);
        rejects([&] { encodeTemplateBundle(source, metadata, hugeImage); });
        rejects([&] { encodeTemplateBundle(source, metadata, QByteArray("not PNG")); });
        Document missing;
        createAsset(missing, "Missing", "image/png");
        rejects([&] { encodeTemplateBundle(missing, metadata, image); });
        Document library;
        const auto folder = createTag(library, "Furniture", 0, true);
        const auto tag = createTag(library, "Parts", folder);
        const auto part = library.addFace({{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}});
        const auto texture = createAsset(
            library, "Texture", "image/png",
            std::make_shared<AssetPayload>(std::vector<std::uint8_t>(image.begin(), image.end())));
        const auto finish = createMaterial(library, "Finish", {1, 1, 1}, 1, texture);
        assignMaterial(library, part, {}, finish);
        const auto inner = createComponent(library, part, "Part");
        editComponentDefinition(library, inner.definition, [&](Document &draft) {
            ChangeReport changes;
            for (const auto &[id, body] : draft.bodies())
                if (body->kind == BodyKind::Geometry)
                    changes = assignTag(draft, id, tag);
            return changes;
        });
        // A parent definition has a real nested reference, not a baked copy.
        const auto group = library.group({part}, "Assembly");
        const auto outer = createComponent(library, group, "Assembly");
        library.addFace({{{9, 0, 0}, {10, 0, 0}, {9, 1, 0}}});
        createAsset(library, "Unrelated missing asset", "image/png");
        createMaterial(library, "Unrelated material", {0, 0, 0});
        createTag(library, "Unrelated tag");
        TemplateMetadata componentMetadata{"Assembly", "Nested reusable component", {"Furniture"}};
        const auto libraryBefore = encodeContainer(library);
        const auto componentBytes =
            encodeComponentBundle(library, outer.definition, componentMetadata, image);
        const auto component = decodeComponentBundle(componentBytes);
        check(component.definition == outer.definition &&
                  component.document.definitions().size() == 2 &&
                  component.document.instances().size() == 2 &&
                  component.document.tags().size() == 2 &&
                  component.document.materials().size() == 1 &&
                  component.document.assets().size() == 1 &&
                  component.document.bodies().size() < library.bodies().size(),
              "Component bundles contain only recursive definitions and used resources");
        check(component.document.definitions().at(outer.definition)->references.size() == 1 &&
                  component.document.assets().at(texture)->payload->bytes() ==
                      library.assets().at(texture)->payload->bytes() &&
                  encodeContainer(library) == libraryBefore,
              "Nested bindings and embedded bytes retained without source mutation");
        rejects([&] { decodeTemplateBundle(componentBytes); });
        rejects([&] { decodeComponentBundle(bundle); });
        rejects([&] { encodeComponentBundle(library, 999, componentMetadata, image); });
        const auto componentPath = files.filePath("Assembly.sketchylib");
        writeComponentBundle(componentBytes, componentPath);
        const auto componentMoved = files.filePath("Moved-assembly.sketchylib");
        check(QFile::rename(componentPath, componentMoved), "Component relocation");
        check(loadComponentBundle(componentMoved).document.assets().size() == 1,
              "Relocated component resolves embedded dependencies");
        rejects([&] { writeComponentBundle(componentBytes, componentMoved); });
        auto brokenComponent = componentBytes;
        brokenComponent[brokenComponent.size() - 1] ^= 1;
        rejects([&] { decodeComponentBundle(brokenComponent); });
        std::cout << "Template bundles: embedded resources, relocation, fresh documents, "
                     "immutability and malformed bounds passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
