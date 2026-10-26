// R100.c (ADR 0174 sections 4, 5 and 10): the `.sketchycat` catalog format.
//   catalog_tests                          check the format against the generated fixtures
//   catalog_tests --write-fixtures DIR     write the fixtures into a new directory
#include "automation/inspection.hpp"
#include "automation/mcp.hpp"
#include "catalog_fixtures.hpp"
#include "io/model_style_io.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <iostream>
using namespace sketchy;
using namespace catalog_fixture;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f, const char *message) {
    try {
        f();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error(message);
}
const CatalogEntryReport &entryNamed(const CatalogPackage &package, const QString &name) {
    for (const auto &entry : package.entries)
        if (entry.name == name)
            return entry;
    throw std::runtime_error("catalog entry is named");
}
QByteArray fixture(const QString &name) { return fixtures().value(name); }
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        if (argc == 3 && QString::fromLocal8Bit(argv[1]) == "--write-fixtures") {
            const auto generated = fixtures();
            for (auto it = generated.constBegin(); it != generated.constEnd(); ++it) {
                QFile file(
                    QDir(QString::fromLocal8Bit(argv[2])).filePath(it.key() + ".sketchycat"));
                check(file.open(QIODevice::WriteOnly | QIODevice::NewOnly) &&
                          file.write(it.value()) == it.value().size(),
                      "write fixture");
            }
            return 0;
        }
        // Round trip: every entry kind decodes and is importable, in catalog order.
        const auto good = basic();
        const auto package = decodeCatalog(good);
        check(package.metadata.catalogId == "fixture.basic" && package.metadata.version == "1.0.0",
              "metadata round trips");
        check(package.metadata.labels == QStringList({"fixture", "catalog"}), "labels round trip");
        check(package.entries.size() == 5, "five entries round trip");
        const CatalogEntryKind kinds[] = {CatalogEntryKind::Style, CatalogEntryKind::Material,
                                          CatalogEntryKind::Material, CatalogEntryKind::Template,
                                          CatalogEntryKind::Component};
        for (std::size_t i = 0; i < 5; ++i) {
            check(package.entries[i].kind == kinds[i], "entry kinds keep their order");
            check(package.entries[i].importable && package.entries[i].reason.isEmpty(),
                  "every entry of the good catalog is importable");
        }
        check(package.entries[4].definition.toULongLong() != 0, "component records its definition");
        // The same inputs give the same bytes, and the committed fixtures are those bytes.
        check(basic() == good, "the same inputs give identical bytes");
        const auto generated = fixtures();
        for (auto it = generated.constBegin(); it != generated.constEnd(); ++it) {
            QFile file(QStringLiteral(SOURCE_DIR "/tests/fixtures/catalog/") + it.key() +
                       QStringLiteral(".sketchycat"));
            check(file.open(QIODevice::ReadOnly) && file.readAll() == it.value(),
                  "committed catalog fixture matches the generator");
        }
        // Deduplication: the texture both materials use is stored once.
        check(package.assets.size() == 1 && package.assets[0].verified,
              "one shared asset is stored");
        check(entryNamed(package, "Oak").dependencies ==
                      std::vector<CatalogDependency>{{CatalogDependency::Kind::Asset, 1}} &&
                  entryNamed(package, "Oak tint").dependencies ==
                      std::vector<CatalogDependency>{{CatalogDependency::Kind::Asset, 1}},
              "two entries depend on the one stored asset");
        {
            CatalogBuilder twice(metadata());
            const auto textureBytes = texture();
            check(twice.addAsset("Oak texture", "image/png", textureBytes) ==
                      twice.addAsset("Oak texture", "image/png", textureBytes),
                  "equal media type and bytes share one id");
            check(twice.addAsset("Oak texture", "image/jpeg", textureBytes) != 1,
                  "a different media type is not shared");
        }
        // The style entry is encoded and decoded with the existing style codec.
        const auto style =
            decodeModelStyle(QJsonDocument::fromJson(
                                 catalogEntryPayload(package, entryNamed(package, "Plain edges")))
                                 .object());
        check(style.profiles && style.profileWidth == 2, "style entry carries the style record");
        check(QJsonDocument(encodeModelStyle(style)).toJson() ==
                  QJsonDocument::fromJson(
                      catalogEntryPayload(package, entryNamed(package, "Plain edges")))
                      .toJson(),
              "style payload is the style codec's canonical form");
        // Relocation: a moved catalog resolves to the same entries and payload bytes.
        QTemporaryDir origin, destination;
        check(origin.isValid() && destination.isValid(), "relocation scratch");
        const auto original = origin.filePath("room.sketchycat");
        writeCatalog(good, original);
        const auto moved = QDir(destination.path()).filePath("moved/room.sketchycat");
        check(QDir().mkpath(QFileInfo(moved).absolutePath()) && QFile::rename(original, moved),
              "move the catalog");
        const auto relocated = loadCatalog(moved);
        check(relocated.file == good && relocated.entries.size() == package.entries.size(),
              "a moved catalog reads back identically");
        for (std::size_t i = 0; i < package.entries.size(); ++i) {
            check(relocated.entries[i].importable, "a moved catalog stays importable");
            check(catalogEntryPayload(relocated, relocated.entries[i]) ==
                      catalogEntryPayload(package, package.entries[i]),
                  "a moved entry resolves to the same bytes");
        }
        rejects([&] { writeCatalog(good, moved); }, "writing never overwrites a catalog");
        // A tampered entry hash: that entry is refused and named; the rest stay importable.
        const auto tamperedEntry = decodeCatalog(fixture("tampered-entry"));
        const auto &styleEntry = entryNamed(tamperedEntry, "Plain edges");
        check(!styleEntry.importable && styleEntry.reason.contains("hash"),
              "a tampered entry hash is refused");
        check(entryNamed(tamperedEntry, "Oak").importable, "other entries are unaffected");
        // A tampered asset hash: the asset and each entry that uses it are refused, by name.
        const auto tamperedAsset = decodeCatalog(fixture("tampered-asset"));
        check(!tamperedAsset.assets[0].verified, "a tampered asset fails its hash");
        for (const auto *name : {"Oak", "Oak tint"}) {
            const auto &entry = entryNamed(tamperedAsset, name);
            check(!entry.importable && entry.reason.contains("asset 1") && entry.missing &&
                      entry.missing->id == 1,
                  "an entry using a tampered asset is refused with the asset named");
        }
        check(entryNamed(tamperedAsset, "Plain edges").importable &&
                  entryNamed(tamperedAsset, "Block").importable,
              "entries that use no tampered asset stay importable");
        // A missing dependency: the entry and the dependency id are reported, and nothing is
        // substituted for it.
        const auto missing = decodeCatalog(fixture("missing-dependency"));
        const auto &oak = entryNamed(missing, "Oak");
        check(!oak.importable && oak.reason.contains("asset 9") && oak.missing &&
                  oak.missing->kind == CatalogDependency::Kind::Asset && oak.missing->id == 9,
              "a missing dependency is reported with the entry and its id");
        check(entryNamed(missing, "Oak tint").importable,
              "a sibling of a missing dependency stays");
        rejects(
            [&] {
                writeCatalog(fixture("missing-dependency"),
                             QDir(destination.path()).filePath("bad.sketchycat"));
            },
            "writeCatalog publishes nothing for a catalog with a missing dependency");
        check(!QFile::exists(QDir(destination.path()).filePath("bad.sketchycat")),
              "no file is published for a refused catalog");
        // A material whose declared dependencies do not match its asset is not importable.
        const auto undeclared = decodeCatalog(withManifest(good, [](QJsonObject &m) {
            auto entries = m["entries"].toArray();
            auto entry = entries[1].toObject();
            entry["dependencies"] = QJsonArray();
            entries[1] = entry;
            m["entries"] = entries;
        }));
        check(!entryNamed(undeclared, "Oak").importable &&
                  entryNamed(undeclared, "Oak").reason.contains("do not match"),
              "a material must declare exactly its asset");
        // Refusals of the envelope and layout refuse the whole file.
        rejects([&] { decodeCatalog(good.left(good.size() - 1)); },
                "a truncated catalog is refused");
        rejects([&] { decodeCatalog(good + "x"); }, "trailing data is refused");
        {
            auto bad = good;
            bad[0] = 0;
            rejects([&] { decodeCatalog(bad); }, "a bad magic is refused");
        }
        {
            auto bad = good;
            qToLittleEndian<quint32>(32769, bad.data() + 8);
            rejects([&] { decodeCatalog(bad); }, "a manifest over 32 KiB is refused");
        }
        {
            auto bad = good;
            qToLittleEndian<quint32>(1, bad.data() + 20);
            rejects([&] { decodeCatalog(bad); }, "a catalog with a thumbnail is refused");
        }
        rejects(
            [&] {
                decodeCatalog(withManifest(good, [](QJsonObject &m) { m["kind"] = "template"; }));
            },
            "a template kind is refused as a catalog");
        rejects(
            [&] {
                decodeCatalog(withManifest(good, [](QJsonObject &m) {
                    auto entries = m["entries"].toArray();
                    auto entry = entries[0].toObject();
                    entry["offset"] = 1;
                    entries[0] = entry;
                    m["entries"] = entries;
                }));
            },
            "a non-contiguous payload offset is refused");
        rejects(
            [&] {
                decodeCatalog(withManifest(good, [](QJsonObject &m) {
                    auto entries = m["entries"].toArray();
                    auto entry = entries[1].toObject();
                    entry["id"] = 3;
                    entries[1] = entry;
                    m["entries"] = entries;
                }));
            },
            "a non-canonical entry id is refused");
        rejects(
            [&] {
                decodeCatalog(withManifest(good, [](QJsonObject &m) {
                    auto entries = m["entries"].toArray();
                    for (auto i : {1, 2}) {
                        auto entry = entries[i].toObject();
                        entry["dependencies"] = QJsonArray();
                        entries[i] = entry;
                    }
                    m["entries"] = entries;
                }));
            },
            "an unreferenced asset is refused");
        // The writer's bounds: each limit is refused before a catalog is produced.
        rejects(
            [] {
                CatalogBuilder builder(metadata());
                for (int i = 0; i < 129; ++i)
                    builder.addStyle(QStringLiteral("Style %1").arg(i), {}, ModelStyle{});
            },
            "more than 128 entries is refused");
        rejects(
            [] {
                CatalogBuilder builder(metadata());
                for (int i = 0; i < 257; ++i)
                    builder.addAsset(QStringLiteral("Asset %1").arg(i), "image/png",
                                     QByteArray(1, char(i)) + QByteArray(1, char(i >> 8)));
            },
            "more than 256 assets is refused");
        rejects(
            [] {
                CatalogBuilder builder(metadata());
                builder.addAsset("Large", "image/png", QByteArray(16 * 1024 * 1024 + 1, 'x'));
            },
            "an asset over 16 MiB is refused");
        rejects(
            [] {
                CatalogBuilder builder(metadata());
                builder.addAsset("Empty", "image/png", {});
            },
            "an empty asset is refused");
        rejects(
            [] {
                CatalogBuilder builder(metadata());
                builder.addAsset("Upper", "Image/PNG", QByteArray("x"));
            },
            "an uppercase media type is refused");
        rejects(
            [] {
                CatalogBuilder builder(metadata());
                builder.addStyle(QString(257, 'n'), {}, ModelStyle{});
            },
            "a name over 256 bytes is refused");
        rejects(
            [] {
                CatalogBuilder builder(metadata());
                builder.addStyle("Control", "tab\x01here", ModelStyle{});
            },
            "a control character in a description is refused");
        rejects(
            [] {
                CatalogBuilder builder(metadata());
                const auto oak = builder.addAsset("Oak", "image/png", QByteArray("x"));
                builder.addStyle(
                    "Repeats", {}, ModelStyle{},
                    {{CatalogDependency::Kind::Asset, oak}, {CatalogDependency::Kind::Asset, oak}});
            },
            "a repeated dependency is refused");
        rejects(
            [] {
                CatalogBuilder builder(metadata());
                std::vector<CatalogDependency> dependencies;
                for (int i = 0; i < 65; ++i)
                    dependencies.push_back(
                        {CatalogDependency::Kind::Asset,
                         builder.addAsset(QStringLiteral("A%1").arg(i), "image/png",
                                          QByteArray(1, char(i + 1)))});
                builder.addStyle("Too many", {}, ModelStyle{}, dependencies);
            },
            "more than 64 dependencies is refused");
        rejects(
            [] {
                CatalogBuilder builder(metadata());
                builder.addStyle("Forward", {}, ModelStyle{},
                                 {{CatalogDependency::Kind::Entry, 1}});
            },
            "an entry dependency on no earlier entry is refused");
        rejects(
            [] {
                CatalogBuilder builder(metadata());
                builder.addMaterial("Missing", {}, CatalogMaterial{"Missing", {1, 1, 1}, 1, 4});
            },
            "a material naming an absent asset is refused");
        rejects(
            [] {
                CatalogBuilder builder(metadata());
                builder.addMaterial("Bright", {}, CatalogMaterial{"Bright", {1.5f, 1, 1}, 1, 0});
            },
            "a colour channel above one is refused");
        rejects(
            [] {
                CatalogBuilder builder(metadata());
                (void)builder.encode();
            },
            "a catalog needs an entry");
        // catalog.list: the listing shape, and the reasons for the refused entries.
        {
            Document empty;
            const auto request = [&](const QByteArray &file) {
                auto encoded =
                    file.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
                return QJsonObject{{"apiVersion", 1},
                                   {"documentId", QString::fromStdString(empty.identity())},
                                   {"expectedRevision", QString::number(empty.revision())},
                                   {"query", "catalog.list"},
                                   {"catalog", QString::fromLatin1(encoded)}};
            };
            const auto listing =
                inspectDocument(empty, request(fixture("tampered-asset")))["data"].toObject();
            check(listing["entryCount"] == 5 && listing["importableCount"] == 3 &&
                      listing["missingAssets"] == 1,
                  "catalog.list counts entries, importable entries and missing assets");
            bool found = false;
            for (const auto &value : listing["entries"].toArray()) {
                const auto entry = value.toObject();
                if (entry["name"] == "Oak") {
                    found = true;
                    check(entry["importable"] == false && entry["kind"] == "material" &&
                              entry["reason"].toString().contains("asset 1"),
                          "catalog.list names the entry and the reason it is not importable");
                }
            }
            check(found, "catalog.list lists every entry by name");
            const auto good_listing = inspectDocument(empty, request(good))["data"].toObject();
            check(good_listing["importableCount"] == 5 && good_listing["missingAssets"] == 0 &&
                      good_listing["assets"].toArray()[0].toObject()["assetStatus"] == "present",
                  "catalog.list reports a good catalog as all importable");
            const auto dependency =
                inspectDocument(empty, request(fixture("missing-dependency")))["data"]
                    .toObject()["entries"]
                    .toArray()[1]
                    .toObject()["missing"]
                    .toObject();
            check(dependency["kind"] == "asset" && dependency["id"] == 9,
                  "catalog.list reports the missing dependency id");
            // A catalog that is not valid base64url, or not a catalog, is an invalid request.
            rejects(
                [&] {
                    QJsonObject padded = request(good);
                    padded["catalog"] =
                        QString::fromLatin1(good.toBase64(QByteArray::Base64UrlEncoding));
                    inspectDocument(empty, padded);
                },
                "catalog.list requires unpadded base64url");
            rejects(
                [&] {
                    QJsonObject notCatalog = request(good);
                    notCatalog["catalog"] =
                        QString::fromLatin1(QByteArray("not a catalog")
                                                .toBase64(QByteArray::Base64UrlEncoding |
                                                          QByteArray::OmitTrailingEquals));
                    inspectDocument(empty, notCatalog);
                },
                "catalog.list refuses bytes that are not a catalog");
        }
        // MCP lists catalog.list, and the surface stays within its bound (mcpToolLimit).
        {
            QStringList names;
            for (const auto &tool : mcpToolCatalog())
                names.append(tool.toObject()["name"].toString());
            check(names.contains("catalog.list"), "MCP lists catalog.list");
            check(names.size() <= mcpToolLimit, "the MCP tool list stays within its bound");
        }
        std::cout << "catalog tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
