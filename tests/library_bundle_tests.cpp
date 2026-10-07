#include "core/annotations.hpp"
#include "core/assets.hpp"
#include "core/materials.hpp"
#include "core/sections.hpp"
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
        TemplateMetadata metadata{"Metric room", "Embedded starting model", {"Metric", "Room"}, 0};
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
                  first.annotations().size() == 1 && first.materials().size() == 1 &&
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
        std::cout << "Template bundles: embedded resources, relocation, fresh documents, "
                     "immutability and malformed bounds passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
