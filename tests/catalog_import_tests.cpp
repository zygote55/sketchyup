// R100.d (ADR 0174 sections 4, 5 and 7): the catalog library reference, catalog.import through the
// batch engine, catalog.export, the relocation, appearance and refusal checks, one undo step, and
// save and reopen. Every catalog library folder is redirected into a temporary home before any
// location is read, so no test touches the real user's data.
#include "automation/catalog_commands.hpp"
#include "automation/commands.hpp"
#include "automation/inspection.hpp"
#include "catalog_fixtures.hpp"
#include "core/assets.hpp"
#include "core/materials.hpp"
#include "io/assets.hpp"
#include "io/catalog_insert.hpp"
#include "io/document_io.hpp"
#include "io/model_style_io.hpp"
#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace sketchy;
using namespace catalog_fixture;

namespace {

void check(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}

// The refusal message of a call that must throw. A call that succeeds fails the check.
template <class F> std::string rejection(F f, const std::string &what) {
    try {
        f();
    } catch (const std::exception &error) {
        return error.what();
    }
    throw std::runtime_error("Expected a refusal: " + what);
}

// Sandboxes the process: the catalog library lives under `home`, never under the real folders.
void isolate(const QString &home) {
    qputenv("HOME", home.toUtf8());
    qputenv("XDG_DATA_HOME", (home + "/data").toUtf8());
    QCoreApplication::setOrganizationName("SketchyUp");
    QCoreApplication::setApplicationName("SketchyUp");
}

void writeFile(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(),
          "Cannot write " + path.toStdString());
}

QByteArray readFile(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Cannot read " + path.toStdString());
    return file.readAll();
}

QString base64url(const QByteArray &bytes) {
    return QString::fromLatin1(
        bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QByteArray fromBase64url(const QString &text) {
    return QByteArray::fromBase64(text.toLatin1(), QByteArray::Base64UrlEncoding);
}

// The library folder, created on demand. Only the redirected home is ever written.
QString libraryFolder() {
    const auto folder = catalogLibraryFolder();
    check(!folder.isEmpty() && QDir().mkpath(folder), "Cannot create the catalog library folder");
    return folder;
}

// The authored document: a block component, a textured material and an edge-textured style.
struct Authored {
    Document doc;
    Id definition{}, material{}, texture{};
};

Authored authored() {
    auto source = catalog_fixture::source();
    Authored result{std::move(source.document), source.definition, 0, 0};
    result.texture = createAsset(result.doc, "Oak texture", "image/png",
                                 assetPayload(catalog_fixture::texture()));
    result.material = createMaterial(result.doc, "Oak", {0.6f, 0.45f, 0.3f}, 0.8f, result.texture);
    auto style = result.doc.style();
    style.profiles = true;
    style.profileWidth = 2;
    style.edgeStroke = EdgeStroke{3, 0.5, EdgeDash::Dashed, result.texture};
    result.doc.setStyle(style);
    return result;
}

struct Snapshot {
    QByteArray bytes;
    QString revision;
    qsizetype history{};
};

Snapshot snapshot(const Document &doc) {
    return {encodeContainer(doc), QString::number(doc.revision()), qsizetype(doc.history().total)};
}

void requireUnchanged(const Document &doc, const Snapshot &before, const std::string &what) {
    check(encodeContainer(doc) == before.bytes &&
              QString::number(doc.revision()) == before.revision &&
              qsizetype(doc.history().total) == before.history,
          "A refusal changed the document: " + what);
}

QJsonObject queryRequest(const Document &doc, const QString &query, QJsonObject extra = {}) {
    extra["apiVersion"] = 1;
    extra["documentId"] = QString::fromStdString(doc.identity());
    extra["expectedRevision"] = QString::number(doc.revision());
    extra["query"] = query;
    return extra;
}

QJsonObject batchRequest(const Document &doc, QJsonArray commands) {
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(doc.identity())},
            {"expectedRevision", QString::number(doc.revision())},
            {"commands", commands}};
}

QJsonObject importCommand(const QJsonArray &entries, const QString &library = "oak") {
    return {{"command", "catalog.import"}, {"library", library}, {"entries", entries}};
}

QJsonObject importInline(const QJsonArray &entries, const QByteArray &bytes) {
    return {{"command", "catalog.import"}, {"catalog", base64url(bytes)}, {"entries", entries}};
}

QJsonArray names(std::initializer_list<const char *> values) {
    QJsonArray result;
    for (const auto *value : values)
        result.append(QString::fromLatin1(value));
    return result;
}

// Runs one import through the batch engine, the way MCP and the session do.
void runImport(Document &doc, const QJsonObject &command) {
    executeBatch(doc, batchRequest(doc, QJsonArray{command}));
}

// The library reference: names, links, folders, the scan bounds and the listing query.
void libraryReference() {
    const auto folder = libraryFolder();
    const auto good = fixtures().value("edge-texture");
    writeFile(folder + "/oak.sketchycat", good);
    writeFile(folder + "/broken.sketchycat", QByteArray("not a catalog"));
    writeFile(folder + "/notes.txt", QByteArray("ignored: not a catalog"));
    check(QDir(folder).mkpath("dir.sketchycat"), "Cannot make the folder fixture");
    check(QFile::link(folder + "/oak.sketchycat", folder + "/linked.sketchycat"),
          "Cannot make the link fixture");

    // Names: a plain name is accepted; a path, a dot-prefix, a trailing space or a length over 64
    // is not.
    for (const auto *name : {"oak", "Oak kit", "oak-kit_2.v1", "a"})
        check(isCatalogLibraryName(QString::fromLatin1(name)),
              std::string("A library name is accepted: ") + name);
    for (const auto *name :
         {"", ".hidden", "../oak", "a/b", "a\\b", "oak ", "-oak", " oak", "oak\n"})
        check(!isCatalogLibraryName(QString::fromLatin1(name)),
              std::string("A library name is refused: ") + name);
    check(!isCatalogLibraryName(QString(65, QLatin1Char('a'))), "A 65-character name is refused");
    check(isCatalogLibraryName(QString(64, QLatin1Char('a'))), "A 64-character name is accepted");

    // The reference resolves to the file in the folder, and nothing else resolves.
    check(catalogReference(QJsonObject{{"library", "oak"}}).entries.size() == 3,
          "A library name reads its catalog");
    for (const auto *name : {"../oak", "a/b", "linked", "dir", "missing", "broken"}) {
        const auto message = rejection(
            [&] { catalogReference(QJsonObject{{"library", QString::fromLatin1(name)}}); },
            std::string("a library name must be refused: ") + name);
        check(!message.empty(), std::string("A refusal names the cause: ") + name);
    }
    check(rejection([&] { catalogReference(QJsonObject{}); }, "no reference").find("exactly one") !=
              std::string::npos,
          "A request with no catalog reference is refused");
    check(rejection(
              [&] {
                  catalogReference(QJsonObject{{"library", "oak"}, {"catalog", base64url(good)}});
              },
              "two references")
                  .find("exactly one") != std::string::npos,
          "A request with two catalog references is refused");
    check(catalogReference(QJsonObject{{"catalog", base64url(good)}}).entries.size() == 3,
          "The inline form still reads a catalog");

    // The scan lists catalogs by name and reports the ones it cannot read; notes are skipped.
    const auto library = scanCatalogLibrary(folder);
    check(library.entries.size() == 3, "The scan lists the three catalog files");
    bool sawBroken = false, sawLinked = false, sawOak = false;
    for (const auto &entry : library.entries) {
        if (entry.name == "broken") {
            sawBroken = !entry.error.isEmpty();
        } else if (entry.name == "linked") {
            sawLinked = !entry.error.isEmpty();
        } else if (entry.name == "oak") {
            sawOak = entry.error.isEmpty() && entry.entryCount == 3 && entry.importableCount == 3;
        }
    }
    check(sawBroken && sawLinked && sawOak, "The scan reports each file as readable or refused");
    check(scanCatalogLibrary(folder + "/absent").entries.empty(),
          "A folder that does not exist is an empty library");

    // The bound: at most 128 catalogs are listed, with a notice.
    const auto crowded = folder + "/crowded";
    check(QDir().mkpath(crowded), "Cannot make the crowded folder");
    for (int i = 0; i < 129; ++i)
        writeFile(QString("%1/c%2.sketchycat").arg(crowded).arg(i, 4, 10, QChar('0')),
                  QByteArray("junk"));
    const auto capped = scanCatalogLibrary(crowded);
    check(capped.entries.size() == 128 && !capped.notices.isEmpty(),
          "The scan lists at most 128 catalogs and says so");

    // The query surface: catalog.library lists names, catalog.list reads one by name.
    Document empty;
    const auto listing =
        inspectDocument(empty, queryRequest(empty, "catalog.library"))["data"].toObject();
    check(listing["catalogCount"] == 3 && listing["catalogs"].toArray().size() == 3,
          "catalog.library lists the catalogs in the library");
    const auto listed =
        inspectDocument(empty, queryRequest(empty, "catalog.list", {{"library", "oak"}}))["data"]
            .toObject();
    check(listed["entryCount"] == 3 && listed["importableCount"] == 3,
          "catalog.list reads a catalog by library name");
    check(rejection(
              [&] {
                  inspectDocument(empty,
                                  queryRequest(empty, "catalog.list", {{"library", "../oak"}}));
              },
              "catalog.list with a path")
                  .size() > 0,
          "catalog.list refuses a path");
    check(scanCatalogLibrary(folder).entries.size() == 3, "The library folder is unchanged");
}

// Export captures the chosen records, shares one texture between two records, and imports back.
void exportThenImport() {
    auto a = authored();
    const auto exported = inspectDocument(
        a.doc, queryRequest(a.doc, "catalog.export",
                            {{"name", "Oak kit"},
                             {"labels", QJsonArray{"oak"}},
                             {"components", QJsonArray{QString::number(a.definition)}},
                             {"materials", QJsonArray{QString::number(a.material)}},
                             {"style", true}}));
    const auto data = exported["data"].toObject();
    check(data["command"] == "catalog.export" && data["data"].isString(),
          "catalog.export returns the catalog as base64url");
    const auto bytes = fromBase64url(data["data"].toString());
    const auto package = decodeCatalog(bytes);
    check(package.metadata.name == "Oak kit" && package.entries.size() == 3,
          "The export holds the three chosen records");
    check(package.assets.size() == 1,
          "The material and the style share one stored texture (ADR 0131)");
    for (const auto &entry : package.entries)
        check(entry.importable, "Every exported entry is importable: " + entry.name.toStdString());

    // The export changed nothing in the source.
    const auto before = snapshot(a.doc);
    (void)inspectDocument(a.doc,
                          queryRequest(a.doc, "catalog.export",
                                       {{"name", "Oak kit"},
                                        {"materials", QJsonArray{QString::number(a.material)}}}));
    requireUnchanged(a.doc, before, "catalog.export");

    // Import the library copy into an empty document, one entry name at a time.
    writeFile(libraryFolder() + "/oak-kit.sketchycat", bytes);
    Document target;
    const auto empty = snapshot(target);
    runImport(target, importCommand(names({"Block", "Oak", "Document style"}), "oak-kit"));
    check(encodeContainer(target) != empty.bytes, "The import changed the document");
    check(target.instances().size() == 1 && target.definitions().size() == 1,
          "The component is inserted once with its definition");
    check(target.materials().size() == 1, "The material is added once");
    const auto &material = *target.materials().begin()->second;
    check(material.name == "Oak" && material.color == std::array<float, 3>{0.6f, 0.45f, 0.3f} &&
              material.opacity == 0.8f,
          "The material keeps its name, colour and opacity");
    const auto &texture = *target.assets().at(material.asset);
    check(texture.name == "Oak texture" && texture.mediaType == "image/png" && texture.payload &&
              QByteArray(reinterpret_cast<const char *>(texture.payload->bytes().data()),
                         qsizetype(texture.payload->bytes().size())) == catalog_fixture::texture(),
          "The material's texture keeps its name, type and bytes");

    // Appearance: the imported style equals the authored one, with the texture re-identified.
    auto expected = a.doc.style();
    expected.edgeStroke->texture = target.style().edgeStroke->texture;
    check(target.style() == expected, "The imported style keeps every field, including edgeStroke");
    check(target.style().edgeStroke && target.style().edgeStroke->texture == material.asset,
          "The style's edge texture is the material's texture, stored once");
    check(target.definitions().begin()->second->members.size() ==
              a.doc.definitions().at(a.definition)->members.size(),
          "The component keeps its member count");

    // Save and reopen gives the same bytes.
    QTemporaryDir dir;
    check(dir.isValid(), "Cannot make a temporary folder");
    const auto path = dir.filePath("imported.sketchyup");
    saveDocument(target, path);
    const auto reopened = loadDocument(path);
    check(encodeContainer(reopened) == encodeContainer(target),
          "Save and reopen keeps the imported document bit for bit");
    check(reopened.style() == target.style(), "Reopening keeps the imported style");
}

// Each import is one undo step, whichever entries it takes.
void oneUndoStep() {
    Document target;
    const auto before = snapshot(target);
    runImport(target, importInline(names({"Block", "Oak", "Edge style"}),
                                   fixtures().value("edge-texture")));
    check(target.materials().size() == 1 && target.instances().size() == 1 &&
              target.style().edgeStroke.has_value(),
          "The import adds the component, the material and the style together");
    check(qsizetype(target.history().total) == before.history + 1,
          "An import adds exactly one history item");
    target.undo();
    // Undo removes every record the import added. The allocator floors (the next ids) stay
    // advanced, as after any edit, so an id is never reused; they are not records.
    const Document blank;
    check(target.bodies().empty() && target.definitions().empty() && target.instances().empty() &&
              target.materials().empty() && target.assets().empty() && target.tags().empty() &&
              target.style() == blank.style(),
          "One undo removes every record the import added");
}

// R100.i (ADR 0174 sections 4 and 7; UX_DESIGN 18.5): a cancelled read or insert throws and changes
// nothing; a check that never cancels gives the result of no check at all. The reader is cancelled in
// its byte read and in its hash verification, so a long read stops early.
template <class E, class F> bool throwsType(F f) {
    try {
        f();
    } catch (const E &) {
        return true;
    }
    return false;
}

void cancellation() {
    const auto file = fixtures().value("edge-texture");
    const auto never = [] { return false; };
    const auto always = [] { return true; };
    check(throwsType<CatalogCancelled>([&] { decodeCatalog(file, always); }),
          "A cancelled decode stops with CatalogCancelled");
    // Cancelled at the third check: the reader stops inside its verification, not at the end.
    int checks = 0;
    check(throwsType<CatalogCancelled>([&] { decodeCatalog(file, [&] { return ++checks > 2; }); }),
          "A cancel inside the hash verification stops the decode");
    check(checks == 3, "The decode stopped at the third check");
    QTemporaryDir bytes;
    const auto path = bytes.path() + "/edge-texture.sketchycat";
    writeFile(path, file);
    check(throwsType<CatalogCancelled>([&] { loadCatalogBytes(path, always); }),
          "A cancelled read stops with CatalogCancelled");
    check(loadCatalogBytes(path, never) == loadCatalogBytes(path),
          "A read that never cancels returns the same bytes");
    const auto plain = decodeCatalog(file);
    const auto checked = decodeCatalog(file, never);
    check(checked.file == plain.file && encodeCatalogListing(checked) == encodeCatalogListing(plain),
          "A decode that never cancels gives the same catalog");

    const auto package = plain;
    const QStringList entries{"Block", "Oak", "Edge style"};
    Document target;
    const auto before = snapshot(target);
    check(throwsType<CatalogCancelled>([&] { importCatalogEntries(target, package, entries, 0, always); }),
          "A cancelled insert stops with CatalogCancelled");
    requireUnchanged(target, before, "a cancelled insert");
    // Cancelled after the first entry: nothing of it is published.
    int steps = 0;
    check(throwsType<CatalogCancelled>([&] {
              importCatalogEntries(target, package, entries, 0, [&] { return ++steps > 1; });
          }),
          "A cancel between entries stops the insert");
    requireUnchanged(target, before, "a cancel between entries");

    Document plainTarget, checkedTarget;
    const auto plainResult = importCatalogEntries(plainTarget, package, entries);
    const auto checkedResult = importCatalogEntries(checkedTarget, package, entries, 0, never);
    // Two documents have their own identities, so their bytes differ; the records and the report do not.
    check(catalogImportReport(package, checkedResult) == catalogImportReport(package, plainResult),
          "An insert that never cancels gives the same report");
    check(checkedTarget.bodies().size() == plainTarget.bodies().size() &&
              checkedTarget.definitions().size() == plainTarget.definitions().size() &&
              checkedTarget.materials().size() == plainTarget.materials().size() &&
              checkedTarget.assets().size() == plainTarget.assets().size() &&
              checkedTarget.style() == plainTarget.style() &&
              checkedTarget.history().total == plainTarget.history().total,
          "An insert that never cancels gives the same records and history");
}

// Relocation: a catalog copied elsewhere and read from there imports the same records, and a
// catalog renamed inside the library is found by its new name only.
void relocation() {
    const auto folder = libraryFolder();
    const auto bytes = fixtures().value("edge-texture");
    QTemporaryDir elsewhere;
    check(elsewhere.isValid(), "Cannot make a temporary folder");
    const auto moved = elsewhere.filePath("far/away/oak.sketchycat");
    check(QDir().mkpath(elsewhere.filePath("far/away")), "Cannot make the moved folder");
    writeFile(moved, bytes);

    Document inline_;
    runImport(inline_, importInline(names({"Oak"}), readFile(moved)));
    check(inline_.materials().size() == 1, "A catalog read from its moved location imports");

    writeFile(folder + "/first.sketchycat", bytes);
    check(QFile::rename(folder + "/first.sketchycat", folder + "/second.sketchycat"),
          "Cannot rename the library entry");
    check(rejection([&] { catalogReference(QJsonObject{{"library", "first"}}); }, "the old name")
                  .size() > 0,
          "The old library name no longer resolves");
    Document byName;
    runImport(byName, importCommand(names({"Oak"}), "second"));
    check(*byName.materials().begin()->second == *inline_.materials().begin()->second &&
              byName.instances().size() == inline_.instances().size(),
          "The renamed library entry imports the same records as the moved file");
}

// ADR 0174 section 10 (equal hash, different payload; ADR 0131): an asset whose recorded SHA-256 is
// another asset's hash, but whose bytes differ, does not verify. Its entry is refused by its own
// hash, the valid entry still imports with its own bytes, and nothing is reused or substituted.
void equalHashDifferentPayload() {
    const auto oak = texture();
    const auto pine = encodeTexturePng(TextureImage(2, 1, {0, 0, 255, 255, 255, 255, 0, 255}));
    CatalogBuilder builder(metadata());
    const auto oakAsset = builder.addAsset("Oak texture", "image/png", oak);
    const auto pineAsset = builder.addAsset("Pine texture", "image/png", pine);
    builder.addMaterial("Oak", "Valid", {"Oak", {0.6f, 0.45f, 0.3f}, 1, oakAsset});
    builder.addMaterial("Pine", "Forged hash", {"Pine", {0.2f, 0.4f, 0.2f}, 1, pineAsset});
    const auto recordedOak =
        QString::fromLatin1(QCryptographicHash::hash(oak, QCryptographicHash::Sha256).toHex());
    const auto forged = withManifest(builder.encode(), [&](QJsonObject &manifest) {
        auto assets = manifest["assets"].toArray();
        auto second = assets[1].toObject();
        second["sha256"] = recordedOak; // the hash of the other texture, over these other bytes
        assets[1] = second;
        manifest["assets"] = assets;
    });
    writeFile(libraryFolder() + "/forged.sketchycat", forged);
    const auto package = decodeCatalog(forged);
    check(package.assets.size() == 2 && package.assets[0].verified && !package.assets[1].verified,
          "The forged asset does not verify against its bytes");
    check(package.entries.size() == 2 && package.entries[0].importable &&
              !package.entries[1].importable &&
              package.entries[1].reason.contains("does not match its hash"),
          "The entry that names the forged asset is refused by its hash");

    Document target;
    const auto before = snapshot(target);
    const auto refused =
        rejection([&] { runImport(target, importCommand(names({"Pine"}), "forged")); }, "Pine");
    check(refused.find("does not match its hash") != std::string::npos,
          "The forged entry is refused, and the refusal says why: " + refused);
    requireUnchanged(target, before, "a forged hash");

    runImport(target, importCommand(names({"Oak"}), "forged"));
    const auto oakId = target.materials().begin()->second->asset;
    check(
        target.assets().size() == 1 && target.assets().at(oakId)->payload->bytes() ==
                                           std::vector<std::uint8_t>(oak.cbegin(), oak.cend()),
        "The valid entry keeps its own bytes: no reuse of, or substitution for, the forged asset");
}

// Missing and tampered dependencies refuse that entry, name it, and never substitute.
void missingDependencies() {
    const auto folder = libraryFolder();
    writeFile(folder + "/missing.sketchycat", fixtures().value("missing-dependency"));
    writeFile(folder + "/tampered.sketchycat", fixtures().value("tampered-asset"));
    writeFile(folder + "/entry.sketchycat", fixtures().value("tampered-entry"));
    writeFile(folder + "/basic.sketchycat", fixtures().value("basic"));

    // A missing asset: "Oak" names asset 9, which the catalog does not hold.
    Document target = authored().doc;
    auto before = snapshot(target);
    const auto missing = rejection(
        [&] { runImport(target, importCommand(names({"Oak"}), "missing")); }, "Oak is missing 9");
    check(missing.find("'Oak'") != std::string::npos &&
              missing.find("asset 9") != std::string::npos,
          "The refusal names the entry and the missing dependency id: " + missing);
    requireUnchanged(target, before, "a missing dependency");
    // The other entries of the same catalog stay importable.
    runImport(target, importCommand(names({"Oak tint"}), "missing"));
    check(target.materials().size() == 2, "A dependency that is present still imports");
    before = snapshot(target);
    const auto mixed =
        rejection([&] { runImport(target, importCommand(names({"Oak", "Oak tint"}), "missing")); },
                  "a mixed request");
    check(mixed.find("'Oak'") != std::string::npos, "A mixed request names the refused entry");
    requireUnchanged(target, before, "a mixed request is refused as a whole");

    // A tampered texture refuses every entry that uses it, and nothing else.
    Document t2;
    const auto tampered = rejection(
        [&] { runImport(t2, importCommand(names({"Oak tint"}), "tampered")); }, "tampered asset");
    check(tampered.find("does not match its hash") != std::string::npos,
          "A tampered texture is refused by its hash: " + tampered);
    runImport(t2, importCommand(names({"Block"}), "tampered"));
    check(t2.instances().size() == 1, "A component with no bad dependency still imports");

    // A tampered record is refused by its own hash.
    Document t3;
    check(rejection([&] { runImport(t3, importCommand(names({"Plain edges"}), "entry")); },
                    "tampered entry")
                  .find("hash") != std::string::npos,
          "A tampered record is refused by its hash");

    // A template opens as a new model, so it is refused rather than inserted.
    Document t4;
    check(rejection([&] { runImport(t4, importCommand(names({"Starting room"}), "basic")); },
                    "template")
                  .find("template") != std::string::npos,
          "A template entry is refused, not inserted");

    // A style whose edge texture is not a declared dependency is not importable (ADR 0131: no
    // asset is taken from a bare id). The rest of the catalog is unaffected.
    const auto undeclared =
        withManifest(fixtures().value("edge-texture"), [](QJsonObject &manifest) {
            auto entries = manifest["entries"].toArray();
            auto style = entries[0].toObject();
            style["dependencies"] = QJsonArray{};
            entries[0] = style;
            manifest["entries"] = entries;
        });
    writeFile(folder + "/undeclared.sketchycat", undeclared);
    const auto listing =
        inspectDocument(t4, queryRequest(t4, "catalog.list", {{"library", "undeclared"}}))["data"]
            .toObject();
    const auto styleEntry = listing["entries"].toArray()[0].toObject();
    check(styleEntry["importable"] == false &&
              styleEntry["reason"].toString().contains("is not a dependency"),
          "An undeclared style texture makes the style not importable");
    check(listing["entries"].toArray()[1].toObject()["importable"] == true,
          "The material beside it stays importable");
    Document t5;
    check(rejection([&] { runImport(t5, importCommand(names({"Edge style"}), "undeclared")); },
                    "undeclared texture")
                  .find("Edge style") != std::string::npos,
          "The undeclared style is refused by name");

    // Names: an unknown name is refused; a name shared by two entries is ambiguous.
    Document t6;
    check(rejection([&] { runImport(t6, importCommand(names({"Nothing"}), "basic")); }, "unknown")
                  .find("No catalog entry is named") != std::string::npos,
          "An unknown entry name is refused");
    CatalogBuilder twins(metadata());
    twins.addMaterial("Dup", "", {"Dup", {1, 1, 1}, 1, 0});
    twins.addMaterial("Dup", "", {"Dup", {0, 0, 0}, 1, 0});
    writeFile(folder + "/twins.sketchycat", twins.encode());
    check(rejection([&] { runImport(t6, importCommand(names({"Dup"}), "twins")); }, "ambiguous")
                  .find("ambiguous") != std::string::npos,
          "A name shared by two entries is refused, not chosen");
}

// The opened form (the desktop dialog's dry runs, ADR 0174 section 4) gives each entry the verdict
// the library form gives for the same file: it commits, or it is refused with the same message. A
// request names an opened catalog only with the catalogs in hand.
void openedForm() {
    const auto folder = libraryFolder();
    writeFile(folder + "/opened-missing.sketchycat", fixtures().value("missing-dependency"));
    writeFile(folder + "/opened-tampered.sketchycat", fixtures().value("tampered-asset"));
    writeFile(folder + "/opened-entry.sketchycat", fixtures().value("tampered-entry"));
    writeFile(folder + "/opened-basic.sketchycat", fixtures().value("basic"));

    // The verdict of one dry run: empty when the batch would commit, else the refusal's message.
    const auto verdict = [](Document &doc, const QJsonObject &command,
                            const OpenedCatalogs *opened) {
        try {
            previewBatch(doc, batchRequest(doc, QJsonArray{command}), opened);
        } catch (const std::exception &error) {
            return std::string(error.what());
        }
        return std::string();
    };
    const auto openedImport = [](const char *entry) {
        return QJsonObject{
            {"command", "catalog.import"}, {"opened", "1"}, {"entries", names({entry})}};
    };
    struct Case {
        const char *library;
        const char *entry;
    };
    const std::array<Case, 6> cases{{{"opened-missing", "Oak"},
                                     {"opened-missing", "Oak tint"},
                                     {"opened-tampered", "Oak tint"},
                                     {"opened-tampered", "Block"},
                                     {"opened-entry", "Plain edges"},
                                     {"opened-basic", "Starting room"}}};
    int insertable = 0, refused = 0;
    for (const auto &c : cases) {
        OpenedCatalogs opened;
        opened.emplace("1", loadCatalog(folder + "/" + c.library + ".sketchycat"));
        Document byLibrary = authored().doc;
        Document byOpened = authored().doc;
        const auto library =
            verdict(byLibrary, importCommand(names({c.entry}), c.library), nullptr);
        const auto openedVerdict = verdict(byOpened, openedImport(c.entry), &opened);
        check(library == openedVerdict, std::string("The opened and library forms agree on ") +
                                            c.entry + ": '" + library + "' and '" + openedVerdict +
                                            "'");
        if (library.empty())
            ++insertable;
        else
            ++refused;
    }
    check(insertable == 2 && refused == 4, "The cases cover insertable and refused entries");

    Document target = authored().doc;
    OpenedCatalogs held;
    held.emplace("1", loadCatalog(folder + "/opened-basic.sketchycat"));
    check(verdict(target, openedImport("Block"), &held).empty(),
          "An opened catalog's insertable entry commits in a dry run");
    check(verdict(target, openedImport("Block"), nullptr).find("Unknown command parameter") !=
              std::string::npos,
          "A batch from MCP or the CLI cannot name an opened catalog");
    auto unknown = openedImport("Block");
    unknown["opened"] = "2";
    check(verdict(target, unknown, &held).find("not open") != std::string::npos,
          "A key the caller does not hold is refused");
    check(rejection([&] { catalogReference(QJsonObject{{"opened", "1"}}); }, "opened, no map")
                  .find("not open") != std::string::npos,
          "catalog.list and the CLI cannot name an opened catalog");
}

// Every refusal leaves the document, its revision and its history as they were.
void refusalsLeaveDocumentUnchanged() {
    Document target = authored().doc;
    const auto before = snapshot(target);
    const auto refusals = std::vector<std::pair<std::string, QJsonObject>>{
        {"unknown name", importCommand(names({"Nothing"}))},
        {"template", importCommand(names({"Starting room"}), "basic")},
        {"missing dependency", importCommand(names({"Oak"}), "missing")},
        {"bad library name", importCommand(names({"Block"}), "../escape")},
        {"no reference", {{"command", "catalog.import"}, {"entries", names({"Block"})}}},
        {"two references",
         {{"command", "catalog.import"},
          {"library", "oak"},
          {"catalog", base64url(fixtures().value("edge-texture"))},
          {"entries", names({"Block"})}}},
        {"missing parent",
         {{"command", "catalog.import"},
          {"library", "oak"},
          {"entries", names({"Block"})},
          {"parent", "99999"}}},
        {"no entries",
         {{"command", "catalog.import"}, {"library", "oak"}, {"entries", QJsonArray{}}}},
    };
    for (const auto &[what, command] : refusals) {
        rejection([&] { runImport(target, command); }, what);
        requireUnchanged(target, before, what);
    }
}

// Export refuses what it cannot capture whole, and names it.
void exportRefusals() {
    auto a = authored();
    // A material named like the component repeats an entry name.
    createMaterial(a.doc, "Block", {1, 1, 1}, 1, 0);
    const auto before = snapshot(a.doc);
    const auto exportWith = [&](const QJsonObject &extra) {
        return inspectDocument(a.doc, queryRequest(a.doc, "catalog.export", [&] {
                                   QJsonObject request{{"name", "Oak kit"}};
                                   for (auto it = extra.begin(); it != extra.end(); ++it)
                                       request[it.key()] = it.value();
                                   return request;
                               }()));
    };
    check(rejection([&] { exportWith({}); }, "nothing chosen").find("Choose") != std::string::npos,
          "An export with nothing chosen is refused");
    check(rejection([&] { exportWith({{"components", QJsonArray{"999"}}}); }, "unknown definition")
                  .find("does not exist") != std::string::npos,
          "An export of an unknown component is refused");
    const auto clash = rejection(
        [&] {
            exportWith(
                {{"components", QJsonArray{QString::number(a.definition)}},
                 {"materials", QJsonArray{QString::number(a.doc.materials().rbegin()->first)}}});
        },
        "duplicate name");
    check(clash.find("used twice") != std::string::npos,
          "Duplicate entry names are refused: " + clash);
    // An asset without bytes is never left out of a catalog.
    Document gone;
    const auto missingAsset = createAsset(gone, "Gone", "image/png", nullptr);
    const auto goneMaterial = createMaterial(gone, "Gone material", {1, 1, 1}, 1, missingAsset);
    check(rejection(
              [&] {
                  inspectDocument(
                      gone,
                      queryRequest(gone, "catalog.export",
                                   {{"name", "Gone kit"},
                                    {"materials", QJsonArray{QString::number(goneMaterial)}}}));
              },
              "missing bytes")
                  .find("no bytes") != std::string::npos,
          "An export refuses an asset that has no bytes");
    check(rejection(
              [&] {
                  inspectDocument(
                      gone,
                      queryRequest(gone, "catalog.export",
                                   {{"name", ""},
                                    {"materials", QJsonArray{QString::number(goneMaterial)}}}));
              },
              "no name")
                  .size() > 0,
          "An export refuses a catalog without a name");
    requireUnchanged(a.doc, before, "catalog.export refusals");
}

// The command and query surfaces: catalog.import is a transaction command, and the export and the
// library listing are queries. The in-app assistant gets none of them (ADR 0174 decision 9).
void commandSurface() {
    bool importCommandListed = false;
    for (const auto &value : commandCatalog())
        if (value.toObject()["name"] == "catalog.import") {
            importCommandListed = true;
            const auto required = value.toObject()["parameters"].toObject()["required"].toArray();
            check(required.size() == 2 && required[0] == "entries",
                  "catalog.import requires its entries");
        }
    check(importCommandListed, "The command catalogue lists catalog.import");
    bool exportQueryListed = false, libraryQueryListed = false;
    for (const auto &value : inspectionCatalog()) {
        const auto name = value.toObject()["name"].toString();
        exportQueryListed = exportQueryListed || name == "catalog.export";
        libraryQueryListed = libraryQueryListed || name == "catalog.library";
    }
    check(exportQueryListed && libraryQueryListed, "The inspection registry lists the new queries");
}

} // namespace

int main() {
    try {
        QTemporaryDir home;
        check(home.isValid(), "Cannot make a temporary home");
        isolate(home.path());
        libraryReference();
        exportThenImport();
        oneUndoStep();
        relocation();
        missingDependencies();
        equalHashDifferentPayload();
        openedForm();
        refusalsLeaveDocumentUnchanged();
        exportRefusals();
        commandSurface();
        cancellation();
        std::cout << "catalog import tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
