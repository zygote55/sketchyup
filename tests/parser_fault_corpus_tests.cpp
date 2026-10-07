#include "automation/extension.hpp"
#include "core/components.hpp"
#include "integrations/glb_export.hpp"
#include "io/dxf_export.hpp"
#include "io/gltf_import.hpp"
#include "io/library_bundle.hpp"
#include "io/obj_source.hpp"
#include "io/stl_export.hpp"
#include "io/texture_image.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <functional>
#include <iostream>
#include <new>
using namespace sketchy;
namespace {
class CorpusFailure : public std::runtime_error {
    using std::runtime_error::runtime_error;
};
void check(bool value, const char *message) {
    if (!value)
        throw CorpusFailure(message);
}
void coordinate(Vec3 p) {
    check(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::abs(p.x) <= 1e6 &&
              std::abs(p.y) <= 1e6 && std::abs(p.z) <= 1e6,
          "Accepted parser result contains invalid native coordinates");
}
void roundtrip(const Document &document) {
    const auto bytes = encodeContainer(document);
    check(encodeContainer(decodeContainer(bytes)) == bytes,
          "Accepted document does not survive canonical container validation");
}
QByteArray read(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Read corpus seed");
    return file.readAll();
}
void write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(),
          "Write private corpus input");
    file.close();
}
QJsonObject exercise(const QString &name, const QByteArray &seed,
                     const std::function<void(const QByteArray &, bool &)> &parse) {
    bool valid{};
    parse(seed, valid);
    check(valid, "Original corpus seed must parse successfully");
    std::uint32_t state = 0x83a5c0de;
    const auto random = [&]() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    };
    size_t accepted{}, rejected{};
    for (int iteration = 0; iteration < 512; ++iteration) {
        auto bytes = seed;
        const auto at = qsizetype(random() % std::uint32_t(seed.size()));
        switch (iteration % 4) {
        case 0:
            bytes.truncate(at);
            break;
        case 1:
            bytes[at] = char(static_cast<unsigned char>(bytes[at]) ^ (1u << (random() % 8)));
            break;
        case 2:
            for (int i = 0; i < 1 + iteration % 31; ++i)
                bytes.append(char(random() & 255));
            break;
        case 3:
            for (qsizetype i = at; i < std::min(at + 16, bytes.size()); ++i)
                bytes[i] = char(random() & 255);
            break;
        }
        bool parsed{};
        try {
            parse(bytes, parsed);
            check(parsed, "Parser returned without declaring acceptance");
            ++accepted;
        } catch (const std::bad_alloc &) {
            throw; // Resource exhaustion is never classified as ordinary validation rejection.
        } catch (const CorpusFailure &) {
            throw;
        } catch (const std::exception &error) {
            if (parsed)
                throw CorpusFailure(
                    (name + ": accepted result invariant failed: " + error.what()).toStdString());
            ++rejected;
        }
    }
    check(rejected > 0, "Corpus must exercise rejection paths");
    return {
        {"format", name},
        {"mutations", 512},
        {"accepted", qint64(accepted)},
        {"rejected", qint64(rejected)},
        {"seedBytes", seed.size()},
        {"seedSha256",
         QString::fromLatin1(QCryptographicHash::hash(seed, QCryptographicHash::Sha256).toHex())}};
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir temporary;
        check(temporary.isValid(), "Corpus scratch");
        Document source;
        const auto body = source.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}});
        source.extrude(body, source.bodies().at(body)->surface.faces.begin()->first, 4);
        const auto component = createComponent(source, body, "Corpus block");
        auto canonical = QJsonDocument::fromJson(encodeDocument(source)).object();
        canonical["documentId"] = "0000000000000000000000000000083a";
        source = decodeDocument(QJsonDocument(canonical).toJson(QJsonDocument::Compact));
        const auto original = encodeContainer(source);
        QJsonArray results;
        results.append(
            exercise("native-json", encodeDocument(source), [](const auto &bytes, bool &parsed) {
                const auto document = decodeDocument(bytes);
                parsed = true;
                roundtrip(document);
            }));
        results.append(exercise("native-container", original, [](const auto &bytes, bool &parsed) {
            const auto document = decodeContainer(bytes);
            parsed = true;
            roundtrip(document);
        }));
        const auto glb = exportGlb(RenderSnapshot::capture(source)).glb;
        const auto glbPath = temporary.filePath("mutated.glb");
        results.append(exercise("glb", glb, [&](const auto &bytes, bool &parsed) {
            write(glbPath, bytes);
            auto result = loadGltf(glbPath);
            parsed = true;
            roundtrip(result.document);
        }));
        for (const auto encoding : {StlEncoding::Binary, StlEncoding::Ascii}) {
            const auto seed = exportStl(source, {1, StlUpAxis::Z}, encoding).bytes;
            results.append(exercise(encoding == StlEncoding::Binary ? "stl-binary" : "stl-ascii",
                                    seed, [](const auto &bytes, bool &parsed) {
                                        const auto result = parseStl(bytes, {1, StlUpAxis::Z});
                                        parsed = true;
                                        for (const auto &facet : result.facets)
                                            for (const auto p : facet.vertices)
                                                coordinate(p);
                                    }));
        }
        results.append(exercise("obj", "v 0 0 0\nv 2 0 0\nv 0 3 0\nf 1 2 3\n",
                                [](const auto &bytes, bool &parsed) {
                                    const auto result = parseObj(bytes, {1, ObjUpAxis::Z});
                                    parsed = true;
                                    for (const auto point : result.vertices)
                                        coordinate(point);
                                    for (const auto &face : result.faces)
                                        for (const auto corner : face.corners)
                                            check(corner.vertex < result.vertices.size(),
                                                  "Accepted OBJ references a missing vertex");
                                }));
        Document planar;
        planar.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}});
        results.append(
            exercise("dxf", exportDxf(planar, 1).bytes, [](const auto &bytes, bool &parsed) {
                const auto result = parseDxf(bytes);
                parsed = true;
                for (const auto &entity : result.entities)
                    for (const auto &segment : entity.segments) {
                        coordinate(segment.start);
                        coordinate(segment.end);
                        coordinate(segment.center);
                    }
            }));
        const auto png = encodeTexturePng(TextureImage(1, 1, {10, 20, 30, 255}));
        const TemplateMetadata metadata{"Corpus", "Bounded corruption fixture", {"test"}, 0};
        results.append(exercise("template", encodeTemplateBundle(source, metadata, png),
                                [](const auto &bytes, bool &parsed) {
                                    const auto result = decodeTemplateBundle(bytes);
                                    parsed = true;
                                    roundtrip(result.document);
                                }));
        results.append(exercise("component",
                                encodeComponentBundle(source, component.definition, metadata, png),
                                [](const auto &bytes, bool &parsed) {
                                    const auto result = decodeComponentBundle(bytes);
                                    parsed = true;
                                    roundtrip(result.document);
                                }));
        results.append(exercise(
            "extension", read(QStringLiteral(SOURCE_DIR "/examples/extensions/panel.sketchyext")),
            [](const auto &bytes, bool &parsed) {
                const auto manifest = parseExtensionManifest(bytes);
                parsed = true;
                check(!manifest.actions.isEmpty(), "Accepted manifest has actions");
                // Manifest parsing owns structural/capability validity; expanded command
                // geometry is validated only when executed against a document.
                check(manifest.source == bytes, "Extension retains exact validated source");
            }));
        check(encodeContainer(source) == original, "Corpus never mutates its source model");
        std::cout << QJsonDocument(QJsonObject{{"corpusVersion", 1},
                                               {"seed", "0x83a5c0de"},
                                               {"formats", results},
                                               {"mutations", 5120},
                                               {"sourcePreserved", true}})
                         .toJson()
                         .toStdString();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
