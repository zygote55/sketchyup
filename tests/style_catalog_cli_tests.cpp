// R100.e (ADR 0174 sections 7 and 10): the style and catalog flags of `sketchyup-cli`, run as the
// built binary. Each flag works on success. Each refusal names its cause, exits 1 and publishes
// nothing. An input model or catalog is never changed. A style or catalog exported and imported
// again keeps its appearance, and --capabilities and --describe-command list every flag. The user
// data folder is a temporary folder (HOME and XDG_DATA_HOME), so no test touches the real user's
// library. That isolation holds on Linux only, so the test is skipped (exit 77) elsewhere.
#include "core/model.hpp"
#include "io/assets.hpp"
#include "io/catalog_io.hpp"
#include "io/document_io.hpp"
#include "io/style_bundle.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>
#include <QTemporaryDir>
#include <QtEndian>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace sketchy;

namespace {

void check(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}

QString sourcePath(const QString &relative) {
    return QString::fromLocal8Bit(SOURCE_DIR) + QLatin1Char('/') + relative;
}

QByteArray readFile(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Cannot read " + path.toStdString());
    return file.readAll();
}

void writeFile(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(),
          "Cannot write " + path.toStdString());
}

QByteArray sha256(const QByteArray &bytes) {
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

// The file names in a folder, hidden ones included, so a temporary file left behind would show.
QStringList names(const QString &folder) {
    return QDir(folder).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name);
}

// A JSON string field equals the text (QJsonValue has no const char* comparison).
bool is(const QJsonValue &value, const char *text) {
    return value.toString() == QString::fromUtf8(text);
}

struct CliRun {
    int exitCode{};
    QByteArray out, err;
    QString code() const { return QJsonDocument::fromJson(err).object()["code"].toString(); }
    QString message() const { return QJsonDocument::fromJson(err).object()["error"].toString(); }
    QJsonObject json() const { return QJsonDocument::fromJson(out).object(); }
};

CliRun cli(const QStringList &args, const QProcessEnvironment &env) {
    QProcess process;
    process.setProcessEnvironment(env);
    process.start(QStringLiteral(CLI_PATH), args);
    check(process.waitForStarted(10000) && process.waitForFinished(120000),
          "The CLI runs to completion");
    check(process.exitStatus() == QProcess::NormalExit, "The CLI exits normally");
    return {process.exitCode(), process.readAllStandardOutput(), process.readAllStandardError()};
}

// The environment of the temporary home: the user's data folder is under `home/data`.
QProcessEnvironment isolated(const QString &home) {
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert("HOME", home);
    env.insert("XDG_DATA_HOME", home + "/data");
    return env;
}

// The catalog library folder inside the temporary home: where --to-library writes and names read.
QString libraryFolder(const QString &home) { return home + "/data/SketchyUp/SketchyUp/catalogs"; }

// The appearance of a style or a document: the style record with its texture id removed, and the
// texture's bytes. Ids differ between documents, so the bytes are what must match.
struct Appearance {
    ModelStyle style;
    QByteArray texture;
    bool operator==(const Appearance &) const = default;
};

Appearance appearanceOf(const Document &doc) {
    auto style = doc.style();
    QByteArray texture;
    if (style.edgeStroke && style.edgeStroke->texture) {
        texture = assetByteArray(doc.assets().at(style.edgeStroke->texture)->payload);
        style.edgeStroke->texture = 0;
    }
    return {style, texture};
}

Appearance appearanceOf(const StyleBundle &bundle) {
    auto style = bundle.style;
    QByteArray texture;
    if (bundle.textureStatus == StyleTextureStatus::Present)
        texture = assetByteArray(bundle.texture.payload);
    if (style.edgeStroke)
        style.edgeStroke->texture = 0;
    return {style, texture};
}

// A model without an edge stroke, saved into `path`.
QByteArray savePlainModel(const QString &path) {
    Document plain;
    plain.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}});
    saveDocument(plain, path);
    return sha256(readFile(path));
}

enum class Damage { Missing, HashMismatch };

// The R100.a style fixture with its texture recorded as missing, or with its texture bytes altered:
// the two ways a style file fails to supply its texture (ADR 0174 section 5).
QByteArray damagedStyle(const QString &path, Damage damage) {
    const auto bytes = readFile(path);
    const auto manifestSize = qFromLittleEndian<quint32>(bytes.constData() + 8);
    auto manifest = QJsonDocument::fromJson(bytes.mid(24, manifestSize)).object();
    auto payload = bytes.mid(24 + manifestSize);
    auto texture = manifest["texture"].toObject();
    if (damage == Damage::Missing) {
        texture["missing"] = true;
        texture["bytes"] = 0;
        texture["sha256"] = QJsonValue::Null;
        payload.clear();
    } else {
        check(!payload.isEmpty(), "The style fixture carries its texture bytes");
        payload[0] = char(payload[0] ^ 0x5A);
    }
    manifest["texture"] = texture;
    const auto json = QJsonDocument(manifest).toJson(QJsonDocument::Compact);
    QByteArray result = bytes.left(24);
    qToLittleEndian<quint32>(quint32(json.size()), result.data() + 8);
    qToLittleEndian<quint64>(quint64(payload.size()), result.data() + 12);
    qToLittleEndian<quint32>(0, result.data() + 20);
    return result + json + payload;
}

// A refused run exits 1 with the code, and leaves the folder's files exactly as they were.
void refused(const QStringList &args, const QProcessEnvironment &env, const QString &folder,
             const char *code, const std::string &why) {
    const auto before = names(folder);
    const auto run = cli(args, env);
    check(run.exitCode == 1 && run.code() == QString::fromLatin1(code),
          why + " is refused as " + code + ": " + run.err.toStdString());
    check(names(folder) == before, why + " publishes nothing");
}

// `--capabilities` and `--describe-command` list all five operations and every option they take.
void discovery(const QProcessEnvironment &env) {
    const QStringList operations{"import-style", "export-style", "list-catalog", "import-catalog",
                                 "export-catalog"};
    const QStringList options{"entries",           "name",
                              "description",       "label",
                              "catalog-version",   "catalog-license",
                              "catalog-component", "catalog-material",
                              "catalog-style",     "to-library"};
    const auto capabilities = cli({"--capabilities"}, env);
    check(capabilities.exitCode == 0, "--capabilities runs");
    const auto section = capabilities.json()["styleCatalog"].toObject();
    const auto listed = section["operations"].toObject();
    const auto listedOptions = section["options"].toObject();
    for (const auto &name : operations)
        check(listed.contains(name), "--capabilities lists " + name.toStdString());
    for (const auto &name : options)
        check(listedOptions.contains(name), "--capabilities lists --" + name.toStdString());
    for (const auto &name : operations) {
        const auto run = cli({"--describe-command", name}, env);
        check(run.exitCode == 0 && run.json()["name"].toString() == name &&
                  run.json()["flag"].toString() == "--" + name,
              "--describe-command names " + name.toStdString());
        const auto described = run.json()["options"].toObject();
        QStringList advertised;
        for (const auto &item : listed[name].toObject()["options"].toArray())
            advertised.append(item.toString());
        QStringList shown = described.keys();
        std::sort(advertised.begin(), advertised.end());
        std::sort(shown.begin(), shown.end());
        check(advertised == shown,
              "--describe-command lists the options --capabilities lists for " +
                  name.toStdString());
        for (const auto &option : shown)
            check(listedOptions.contains(option) || option == "input" || option == "output",
                  "--describe-command option --" + option.toStdString() + " is known");
    }
    check(cli({"--describe-command", "import-style-missing"}, env).exitCode == 1,
          "An unknown name is not described");
}

// --import-style, --export-style and their refusals, on the style fixtures.
void styleFlags(const QProcessEnvironment &env, const QString &dir) {
    const auto styled = sourcePath("tests/fixtures/edge-styles-v31.sketchyup");
    const auto styledBefore = sha256(readFile(styled));
    const auto plain = dir + "/plain.sketchyup";
    const auto plainBefore = savePlainModel(plain);
    const auto style =
        decodeStyleBundle(readFile(sourcePath("tests/fixtures/edge-style-v1.sketchystyle")));
    check(style.textureStatus == StyleTextureStatus::Present,
          "The R100.a style fixture carries its texture");

    auto run = cli({"--input", plain, "--import-style",
                    sourcePath("tests/fixtures/edge-style-v1.sketchystyle"), "--output",
                    dir + "/styled.sketchyup"},
                   env);
    check(run.exitCode == 0 && is(run.json()["status"], "imported"), "--import-style imports");
    check(is(run.json()["style"].toObject()["status"], "applied"),
          "A style is applied to a model that does not have it");
    const auto styledDoc = loadDocument(dir + "/styled.sketchyup");
    check(appearanceOf(styledDoc) == appearanceOf(style),
          "The imported model has the style's appearance, texture bytes included");
    check(sha256(readFile(plain)) == plainBefore, "--import-style leaves --input unchanged");

    run = cli({"--input", styled, "--export-style", dir + "/exported.sketchystyle", "--name",
               "Sketch edges", "--description", "Dashed sketch edges", "--label", "sketch",
               "--label", "edge"},
              env);
    check(run.exitCode == 0 && is(run.json()["status"], "exported"), "--export-style exports");
    const auto exported = decodeStyleBundle(readFile(dir + "/exported.sketchystyle"));
    check(exported.metadata.name == "Sketch edges" &&
              exported.metadata.description == "Dashed sketch edges" &&
              exported.metadata.labels == QStringList{"sketch", "edge"},
          "--export-style writes the name, description and labels");
    check(appearanceOf(exported) == appearanceOf(loadDocument(styled)),
          "The exported style carries the document's appearance");
    check(sha256(readFile(styled)) == styledBefore, "--export-style leaves --input unchanged");

    run = cli({"--input", styled, "--export-style", dir + "/base.sketchystyle"}, env);
    check(run.exitCode == 0 &&
              decodeStyleBundle(readFile(dir + "/base.sketchystyle")).metadata.name == "base",
          "--export-style names the style after the file when --name is absent");

    // A style exported and imported again into another model keeps its appearance.
    run = cli({"--input", plain, "--import-style", dir + "/exported.sketchystyle", "--output",
               dir + "/again.sketchyup"},
              env);
    check(run.exitCode == 0, "The exported style imports into another model");
    check(appearanceOf(loadDocument(dir + "/again.sketchyup")) == appearanceOf(styledDoc),
          "A style exported and imported again keeps its appearance");
}

// Every refusal of the style flags: an existing output, a missing or mismatched texture, a wrong
// suffix and conflicting flags. Each one publishes nothing.
void styleRefusals(const QProcessEnvironment &env, const QString &dir) {
    const auto styled = sourcePath("tests/fixtures/edge-styles-v31.sketchyup");
    const auto plain = dir + "/refusals-plain.sketchyup";
    savePlainModel(plain);
    const auto style = sourcePath("tests/fixtures/edge-style-v1.sketchystyle");

    writeFile(dir + "/taken.sketchyup", "keep me");
    auto run =
        cli({"--input", plain, "--import-style", style, "--output", dir + "/taken.sketchyup"}, env);
    check(run.exitCode == 1 && run.code() == "INVALID_REQUEST",
          "An existing --output is refused as INVALID_REQUEST");
    check(readFile(dir + "/taken.sketchyup") == "keep me", "An existing --output is not replaced");

    writeFile(dir + "/missing.sketchystyle", damagedStyle(style, Damage::Missing));
    refused({"--input", plain, "--import-style", dir + "/missing.sketchystyle", "--output",
             dir + "/from-missing.sketchyup"},
            env, dir, "INPUT_ERROR", "A missing texture");
    run = cli({"--input", plain, "--import-style", dir + "/missing.sketchystyle", "--output",
               dir + "/from-missing.sketchyup"},
              env);
    check(run.message().contains("'Sketch edge'") && run.message().contains("(asset 1)") &&
              run.message().contains("missing"),
          "A missing texture is named by name and asset: " + run.err.toStdString());

    writeFile(dir + "/mismatch.sketchystyle", damagedStyle(style, Damage::HashMismatch));
    run = cli({"--input", plain, "--import-style", dir + "/mismatch.sketchystyle", "--output",
               dir + "/from-mismatch.sketchyup"},
              env);
    check(run.exitCode == 1 && run.code() == "INPUT_ERROR" &&
              run.message().contains("'Sketch edge'") && run.message().contains("hash-mismatched"),
          "A mismatched texture is refused and named");
    check(!QFileInfo::exists(dir + "/from-mismatch.sketchyup"),
          "A mismatched texture writes nothing");

    writeFile(dir + "/exists.sketchystyle", "keep me too");
    refused({"--input", styled, "--export-style", dir + "/exists.sketchystyle"}, env, dir,
            "INVALID_REQUEST", "An existing --export-style path");
    check(readFile(dir + "/exists.sketchystyle") == "keep me too",
          "An existing style file is not replaced");
    refused({"--input", styled, "--export-style", dir + "/plain.txt"}, env, dir, "INVALID_REQUEST",
            "An export without the .sketchystyle suffix");

    const QStringList conflicts[] = {
        // Two operations at once, and each operation's missing or foreign options.
        {"--input", plain, "--import-style", style, "--export-style", dir + "/x.sketchystyle",
         "--output", dir + "/x.sketchyup"},
        {"--input", plain, "--import-style", style},
        {"--import-style", style, "--output", dir + "/y.sketchyup"},
        {"--input", styled, "--export-style", dir + "/y.sketchystyle", "--output",
         dir + "/y.sketchyup"},
        {"--input", styled, "--export-style", dir + "/y.sketchystyle", "--entries", "Oak"},
        {"--input", styled, "--export-style", dir + "/y.sketchystyle", "--catalog-style"},
        {"--input", styled, "--export-style", dir + "/y.sketchystyle", "--to-library"},
        {"--input", styled, "--export-style", dir + "/y.sketchystyle", "extra"},
        {"--list-catalog", sourcePath("tests/fixtures/catalog/basic.sketchycat"), "--input",
         styled},
        {"--input", plain, "--import-catalog",
         sourcePath("tests/fixtures/catalog/basic.sketchycat"), "--output", dir + "/z.sketchyup"},
        {"--input", styled, "--export-catalog", dir + "/z.txt", "--catalog-style"},
        {"--input", styled, "--export-catalog", dir + "/z.sketchycat"},
        {"--input", styled, "--export-catalog", dir + "/z.sketchycat", "--catalog-style",
         "--catalog-component", "0"},
        {"--input", styled, "--export-catalog", dir + "/z.sketchycat", "--catalog-style",
         "--catalog-material", "x1"},
        {"--input", styled, "--export-catalog", dir + "/z.sketchycat", "--catalog-style",
         "--to-library"},
    };
    for (const auto &args : conflicts)
        refused(args, env, dir, "INVALID_REQUEST",
                "A conflicting or incomplete request " + args.join(' ').toStdString());
}

// --list-catalog, --import-catalog and --export-catalog, for files and for library names.
void catalogFlags(const QString &home, const QProcessEnvironment &env, const QString &dir) {
    const auto styled = sourcePath("tests/fixtures/edge-styles-v31.sketchyup");
    const auto catalog = sourcePath("tests/fixtures/catalog/basic.sketchycat");
    const auto catalogBytes = readFile(catalog);
    const auto package = decodeCatalog(catalogBytes);
    const auto expected = encodeCatalogListing(package);
    const auto plain = dir + "/catalog-plain.sketchyup";
    const auto plainBefore = savePlainModel(plain);

    auto run = cli({"--list-catalog", catalog}, env);
    check(run.exitCode == 0 && run.json() == expected,
          "--list-catalog prints the catalog.list result of a file");
    check(QFile::copy(catalog, libraryFolder(home) + "/Basic.sketchycat"),
          "The fixture is placed in the isolated library");
    run = cli({"--list-catalog", "Basic"}, env);
    check(run.exitCode == 0 && run.json() == expected,
          "--list-catalog names a catalog in the library folder");
    refused({"--list-catalog", "Absent"}, env, dir, "NOT_FOUND", "An unknown library name");

    // A tampered or incomplete catalog still lists: the listing says why each entry is refused.
    run = cli({"--list-catalog", sourcePath("tests/fixtures/catalog/tampered-entry.sketchycat")},
              env);
    check(run.exitCode == 0, "A tampered catalog lists");
    bool flagged = false;
    for (const auto &entry : run.json()["entries"].toArray())
        flagged = flagged || (!entry.toObject()["importable"].toBool() &&
                              !entry.toObject()["reason"].toString().isEmpty());
    check(flagged, "The listing marks the tampered entry not importable, with its reason");

    // --import-catalog: a named material and a named component enter a model.
    QString materialName, componentName;
    for (const auto &entry : package.entries) {
        if (entry.kind == CatalogEntryKind::Material && materialName.isEmpty())
            materialName = entry.name;
        if (entry.kind == CatalogEntryKind::Component && componentName.isEmpty())
            componentName = entry.name;
    }
    check(!materialName.isEmpty() && !componentName.isEmpty(),
          "The fixture has a material and a component");
    run = cli({"--input", plain, "--import-catalog", catalog, "--entries", materialName, "--output",
               dir + "/material.sketchyup"},
              env);
    check(run.exitCode == 0 && is(run.json()["status"], "imported"),
          "--import-catalog imports a named entry from a file");
    const auto withMaterial = loadDocument(dir + "/material.sketchyup");
    bool found = false;
    for (const auto &[id, material] : withMaterial.materials())
        found = found || QString::fromStdString(material->name) == materialName;
    check(found, "The named material is in the new model");
    check(sha256(readFile(plain)) == plainBefore, "--import-catalog leaves --input unchanged");
    check(readFile(catalog) == catalogBytes, "--import-catalog leaves the catalog unchanged");

    run = cli({"--input", plain, "--import-catalog", "Basic", "--entries", componentName,
               "--output", dir + "/component.sketchyup"},
              env);
    check(run.exitCode == 0 && run.json()["entries"].toArray().size() >= 1,
          "--import-catalog takes an entry from the library by name");
    check(loadDocument(dir + "/component.sketchyup").definitions().size() >
              loadDocument(plain).definitions().size(),
          "The named component's definition is in the new model");

    // Refusals publish nothing and name what refused them.
    refused({"--input", plain, "--import-catalog", catalog, "--entries", "Nobody", "--output",
             dir + "/nobody.sketchyup"},
            env, dir, "INVALID_REQUEST", "An unknown entry name");
    for (const auto &entry : package.entries)
        if (entry.kind == CatalogEntryKind::Template) {
            refused({"--input", plain, "--import-catalog", catalog, "--entries", entry.name,
                     "--output", dir + "/template.sketchyup"},
                    env, dir, "INPUT_ERROR", "A template, which opens as a new model");
        }
    refused({"--input", plain, "--import-catalog", catalog, "--entries", materialName, "--entries",
             "Nobody", "--output", dir + "/both.sketchyup"},
            env, dir, "INVALID_REQUEST", "A request with one unknown entry");

    const struct {
        const char *file;
        const char *label;
    } damaged[] = {
        {"tests/fixtures/catalog/tampered-entry.sketchycat", "A tampered entry"},
        {"tests/fixtures/catalog/tampered-asset.sketchycat", "An entry whose asset is tampered"},
        {"tests/fixtures/catalog/missing-dependency.sketchycat",
         "An entry with a missing dependency"},
    };
    for (const auto &item : damaged) {
        const auto path = sourcePath(item.file);
        const auto listing = decodeCatalog(readFile(path));
        for (const auto &entry : listing.entries) {
            if (entry.importable)
                continue;
            const auto before = names(dir);
            run = cli({"--input", plain, "--import-catalog", path, "--entries", entry.name,
                       "--output", dir + "/damaged.sketchyup"},
                      env);
            check(run.exitCode == 1 && run.code() == "INPUT_ERROR" &&
                      run.message().contains(entry.name) && run.message().contains(entry.reason),
                  std::string(item.label) +
                      " is refused, naming the entry and its reason: " + run.err.toStdString());
            check(names(dir) == before && !QFileInfo::exists(dir + "/damaged.sketchyup"),
                  std::string(item.label) + " publishes nothing");
        }
    }
    // One refused entry refuses the whole import, even with a good entry beside it.
    const auto tampered = sourcePath("tests/fixtures/catalog/tampered-entry.sketchycat");
    QString goodName, badName;
    for (const auto &entry : decodeCatalog(readFile(tampered)).entries) {
        if (entry.importable && entry.kind != CatalogEntryKind::Template && goodName.isEmpty())
            goodName = entry.name;
        if (!entry.importable && badName.isEmpty())
            badName = entry.name;
    }
    check(!goodName.isEmpty() && !badName.isEmpty(),
          "The tampered fixture has good and refused entries");
    refused({"--input", plain, "--import-catalog", tampered, "--entries", goodName, "--entries",
             badName, "--output", dir + "/whole.sketchyup"},
            env, dir, "INPUT_ERROR", "A request with one refused entry");

    // --export-catalog: the chosen components, materials and style, imported again elsewhere.
    run = cli({"--input", dir + "/material.sketchyup", "--export-catalog",
               dir + "/parts.sketchycat", "--name", "Parts", "--description", "Oak and its catalog",
               "--label", "parts", "--catalog-version", "2.1.0", "--catalog-license", "CC0-1.0",
               "--catalog-material", "1", "--catalog-style"},
              env);
    check(run.exitCode == 0 && is(run.json()["status"], "exported"),
          "--export-catalog exports to a file");
    const auto parts = decodeCatalog(readFile(dir + "/parts.sketchycat"));
    check(parts.metadata.name == "Parts" && parts.metadata.version == "2.1.0" &&
              parts.metadata.licenseIdentifier == "CC0-1.0" &&
              parts.metadata.labels == QStringList{"parts"},
          "--export-catalog writes the name, version, licence and label");
    check(parts.entries.size() == 2, "The chosen material and the document style are the entries");
    for (const auto &entry : parts.entries)
        check(entry.importable, "Every exported entry is importable");

    // A document's style leaves a catalog and arrives unchanged, its texture included.
    run = cli({"--input", styled, "--export-catalog", dir + "/style.sketchycat", "--name", "Style",
               "--catalog-style"},
              env);
    check(run.exitCode == 0, "--export-catalog exports a style");
    run = cli({"--input", plain, "--import-catalog", dir + "/style.sketchycat", "--entries",
               "Document style", "--output", dir + "/style-in.sketchyup"},
              env);
    check(run.exitCode == 0, "The exported document style imports from a catalog");
    check(appearanceOf(loadDocument(dir + "/style-in.sketchyup")) ==
              appearanceOf(loadDocument(styled)),
          "A style that leaves a catalog and comes back keeps its appearance, texture included");

    // Export refusals: an existing path, a wrong suffix and an empty selection.
    writeFile(dir + "/taken.sketchycat", "keep me");
    refused({"--input", styled, "--export-catalog", dir + "/taken.sketchycat", "--catalog-style"},
            env, dir, "INVALID_REQUEST", "An existing catalog path");
    check(readFile(dir + "/taken.sketchycat") == "keep me",
          "An existing catalog file is not replaced");
    refused({"--input", styled, "--export-catalog", dir + "/catalog.zip", "--catalog-style"}, env,
            dir, "INVALID_REQUEST", "A catalog path without the .sketchycat suffix");
    refused({"--input", styled, "--export-catalog", dir + "/empty.sketchycat"}, env, dir,
            "INVALID_REQUEST", "An export with nothing chosen");
    refused({"--input", styled, "--export-catalog", dir + "/missing.sketchycat",
             "--catalog-component", "987654", "--catalog-style"},
            env, dir, "INVALID_REQUEST", "A component ID the model does not have");
}

// --export-catalog --to-library: the chosen records go into the library folder under their name.
void libraryExport(const QString &home, const QProcessEnvironment &env) {
    const auto styled = sourcePath("tests/fixtures/edge-styles-v31.sketchyup");
    const auto library = libraryFolder(home);
    const auto before = names(library);
    auto run = cli(
        {"--input", styled, "--export-catalog", "Mine", "--to-library", "--catalog-style"}, env);
    check(run.exitCode == 0 && QFileInfo::exists(library + "/Mine.sketchycat"),
          "--to-library writes NAME.sketchycat into the catalog library folder");
    check(names(library).size() == before.size() + 1, "Exactly one file is added to the library");
    check(cli({"--list-catalog", "Mine"}, env).exitCode == 0,
          "The new library catalog lists by name");
    refused({"--input", styled, "--export-catalog", "Mine", "--to-library", "--catalog-style"}, env,
            library, "INVALID_REQUEST", "A library name that is taken");
    refused({"--input", styled, "--export-catalog", "Bad/Name", "--to-library", "--catalog-style"},
            env, library, "INVALID_REQUEST", "A library name with a separator");
    refused({"--input", styled, "--export-catalog", "Bad Name.", "--to-library", "--catalog-style"},
            env, library, "INVALID_REQUEST", "A library name that ends in a dot");
}

// D-083: a --script batch looks in the same catalog library that --export-catalog --to-library
// writes and --list-catalog reads, so catalog.import by library name finds the exported catalog.
void scriptLibraryImport(const QProcessEnvironment &env, const QString &dir) {
    const auto styled = sourcePath("tests/fixtures/edge-styles-v31.sketchyup");
    auto run =
        cli({"--input", styled, "--export-catalog", "Scripted", "--to-library", "--catalog-style"},
            env);
    check(run.exitCode == 0, "--to-library exports the catalog the script will import");
    const auto entries = cli({"--list-catalog", "Scripted"}, env).json()["entries"].toArray();
    check(!entries.isEmpty() && entries[0].toObject()["importable"].toBool(),
          "The exported library catalog has an importable entry");
    const auto script = dir + "/library-import.json";
    writeFile(script,
              QJsonDocument(QJsonArray{QJsonObject{
                                {"command", "catalog.import"},
                                {"library", "Scripted"},
                                {"entries", QJsonArray{entries[0].toObject()["name"].toString()}}}})
                  .toJson(QJsonDocument::Compact));
    run = cli({"--script", script, "--output", dir + "/library-import.sketchyup"}, env);
    check(run.exitCode == 0, "--script imports a library catalog entry by name");
    check(run.json()["catalogOperations"].toArray().size() == 1 &&
              is(run.json()["catalogOperations"].toArray()[0].toObject()["name"], "Scripted"),
          "The script's catalog.import names the library catalog it read");
}

} // namespace

int main() {
#if !defined(Q_OS_LINUX)
    std::cout << "style and catalog CLI tests skipped: the user data folder is isolated only on "
                 "Linux (XDG_DATA_HOME)\n";
    return 77;
#else
    try {
        QTemporaryDir sandbox;
        check(sandbox.isValid(), "A temporary folder");
        const auto home = sandbox.path();
        const auto env = isolated(home);
        const auto dir = home + "/work";
        check(QDir().mkpath(dir) && QDir().mkpath(libraryFolder(home)),
              "A work folder and a library folder");
        discovery(env);
        styleFlags(env, dir);
        styleRefusals(env, dir);
        catalogFlags(home, env, dir);
        libraryExport(home, env);
        scriptLibraryImport(env, dir);
        std::cout << "style and catalog CLI tests passed\n";
    } catch (const std::exception &error) {
        std::cerr << "style and catalog CLI tests failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
#endif
}
