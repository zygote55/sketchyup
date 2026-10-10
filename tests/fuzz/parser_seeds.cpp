#include "core/components.hpp"
#include "integrations/glb_export.hpp"
#include "io/dxf_export.hpp"
#include "io/stl_export.hpp"
#include "io/texture_image.hpp"
#include "parser_harness.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QtEndian>
#include <iostream>
#include <map>

using namespace sketchy;
namespace {
Document stableIdentity(const Document &source, const QString &identity) {
    auto json = QJsonDocument::fromJson(encodeDocument(source)).object();
    json["documentId"] = identity;
    return decodeDocument(QJsonDocument(json).toJson(QJsonDocument::Compact));
}
QByteArray stableComponent(QByteArray bytes) {
    const auto manifestSize = qFromLittleEndian<quint32>(bytes.constData() + 8);
    const auto modelSize = qFromLittleEndian<quint64>(bytes.constData() + 12);
    const auto thumbnail = bytes.mid(24 + manifestSize + qsizetype(modelSize));
    auto manifest = QJsonDocument::fromJson(bytes.mid(24, manifestSize)).object();
    const auto document =
        stableIdentity(decodeContainer(bytes.mid(24 + manifestSize, qsizetype(modelSize))),
                       QStringLiteral("0000000000000000000000000000083b"));
    const auto model = encodeContainer(document);
    auto record = manifest["model"].toObject();
    record["bytes"] = model.size();
    record["sha256"] =
        QString::fromLatin1(QCryptographicHash::hash(model, QCryptographicHash::Sha256).toHex());
    manifest["model"] = record;
    const auto encoded = QJsonDocument(manifest).toJson(QJsonDocument::Compact);
    bytes.resize(24);
    qToLittleEndian<quint32>(quint32(encoded.size()), bytes.data() + 8);
    qToLittleEndian<quint64>(quint64(model.size()), bytes.data() + 12);
    return bytes + encoded + model + thumbnail;
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        if (argc != 2)
            throw std::runtime_error("Usage: parser_fuzz_seeds NEW_DIRECTORY");
        const QString root = QString::fromLocal8Bit(argv[1]);
        if (QFileInfo::exists(root) || !QDir().mkpath(root))
            throw std::runtime_error("Seed output must be a new private directory");
        QTemporaryDir scratch;
        fuzz::require(scratch.isValid(), "seed validation scratch");
        Document source;
        const auto body = source.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}});
        source.extrude(body, source.bodies().at(body)->surface.faces.begin()->first, 4);
        const auto component = createComponent(source, body, "Corpus block");
        source = stableIdentity(source, QStringLiteral("0000000000000000000000000000083a"));
        Document planar;
        planar.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}});
        const auto png = encodeTexturePng(TextureImage(1, 1, {10, 20, 30, 255}));
        const TemplateMetadata metadata{"Corpus", "Bounded corruption fixture", {"test"}, 0};
        QFile extension(QStringLiteral(SOURCE_DIR "/examples/extensions/panel.sketchyext"));
        fuzz::require(extension.open(QIODevice::ReadOnly), "read shipped extension seed");
        const std::map<QString, QByteArray> seeds{
            {"native-json", encodeDocument(source)},
            {"native-container", encodeContainer(source)},
            {"glb", exportGlb(RenderSnapshot::capture(source)).glb},
            {"stl-binary", exportStl(source, {1, StlUpAxis::Z}, StlEncoding::Binary).bytes},
            {"stl-ascii", exportStl(source, {1, StlUpAxis::Z}, StlEncoding::Ascii).bytes},
            {"obj", "v 0 0 0\nv 2 0 0\nv 0 3 0\nf 1 2 3\n"},
            {"dxf", exportDxf(planar, 1).bytes},
            {"template", encodeTemplateBundle(source, metadata, png)},
            {"component",
             stableComponent(encodeComponentBundle(source, component.definition, metadata, png))},
            {"extension", extension.readAll()}};
        QJsonArray rows;
        const auto publish = [&](const QString &format, const QByteArray &bytes,
                                 const QString &name) {
            fuzz::require(bytes.size() <= qsizetype(fuzz::inputLimit), "bounded seed");
            fuzz::require(fuzz::parseInput(format, bytes, scratch.filePath("input.glb")),
                          "valid seed reaches accepted-result invariants");
            const auto directory = QDir(root).filePath(format);
            fuzz::require(QDir().mkpath(directory), "seed directory");
            const auto path = QDir(directory).filePath(name);
            QFile file(path);
            fuzz::require(file.open(QIODevice::WriteOnly | QIODevice::NewOnly) &&
                              file.write(bytes) == bytes.size(),
                          "publish original seed");
            file.close();
            rows.append(QJsonObject{
                {"format", format},
                {"name", name},
                {"bytes", bytes.size()},
                {"sha256",
                 QString::fromLatin1(
                     QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())}});
        };
        for (const auto &[format, bytes] : seeds)
            publish(format, bytes, "block");
        const auto empty =
            stableIdentity(Document{}, QStringLiteral("0000000000000000000000000000083c"));
        publish("native-json", encodeDocument(empty), "empty-document");
        publish("native-container", encodeContainer(empty), "empty-document");
        std::cout << QJsonDocument(QJsonObject{{"validSeeds", rows},
                                               {"formats", 10},
                                               {"inputLimitBytes", qint64(fuzz::inputLimit)},
                                               {"releaseAcceptance", false}})
                         .toJson(QJsonDocument::Compact)
                         .constData()
                  << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
