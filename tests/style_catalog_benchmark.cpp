// R100.g (ADR 0174 section 9): the catalog and style budgets, measured in a Release build. A manual
// tool, like edge_stroke_benchmark, so it is not a CI line. It writes its own large catalog and
// times the paths the budget rows name. Usage:
//   style_catalog_benchmark write DIR           writes DIR/big.sketchycat (about 128 MiB)
//   style_catalog_benchmark time FILE RUNS      decode, load, scan, one entry's insert, and the
//                                               catalog dialog's dry run by file and by library
//                                               name
//   style_catalog_benchmark style FACES RUNS    style.apply against the equal document.style edit
// Each timing line gives the sample count, the median, the 95th percentile and the largest sample
// (ms), and the process's peak resident set (VmHWM, kB) at that point. The load average is printed
// first, because the timings depend on it.
#include "automation/commands.hpp"
#include "automation/inspection.hpp"
#include "core/assets.hpp"
#include "core/components.hpp"
#include "core/materials.hpp"
#include "core/model.hpp"
#include "io/catalog_insert.hpp"
#include "io/catalog_io.hpp"
#include "io/model_style_io.hpp"
#include "io/texture_image.hpp"
#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

using namespace sketchy;

namespace {

void check(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}

QByteArray readFile(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Cannot read " + path.toStdString());
    return file.readAll();
}

// VmHWM from /proc/self/status, in kB.
long peakResidentKb() {
    QFile status(QStringLiteral("/proc/self/status"));
    if (!status.open(QIODevice::ReadOnly))
        return -1;
    for (const auto &line : status.readAll().split('\n')) {
        if (line.startsWith("VmHWM:"))
            return line.mid(6).trimmed().split(' ').first().toLong();
    }
    return -1;
}

QString loadAverage() {
    QFile file(QStringLiteral("/proc/loadavg"));
    if (!file.open(QIODevice::ReadOnly))
        return QStringLiteral("unknown");
    return QString::fromLatin1(file.readAll().split(' ').value(0));
}

// A payload of random bytes. The catalog and the document take it as an opaque asset: only a
// component's texture is decoded, and that decode is the one the budgets measure.
QByteArray randomBytes(qsizetype size, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    QByteArray bytes(size, Qt::Uninitialized);
    auto *words = reinterpret_cast<std::uint64_t *>(bytes.data());
    for (qsizetype i = 0; i < size / 8; ++i)
        words[i] = rng();
    return bytes;
}

AssetPayloadPtr payloadOf(const QByteArray &bytes) {
    return std::make_shared<AssetPayload>(std::vector<std::uint8_t>(bytes.cbegin(), bytes.cend()));
}

void report(const std::string &label, std::vector<double> samples) {
    std::sort(samples.begin(), samples.end());
    const auto count = samples.size();
    const auto p95 = samples[std::min(count - 1, size_t(std::ceil(0.95 * double(count))) - 1)];
    std::cout << label << ": n=" << count << ", median " << samples[count / 2] << " ms, p95 " << p95
              << " ms, max " << samples.back() << " ms, peak RSS " << peakResidentKb()
              << " kB, load " << loadAverage().toStdString() << "\n";
}

QJsonObject batchFor(const Document &doc, const QJsonArray &commands) {
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(doc.identity())},
            {"expectedRevision", QString::number(doc.revision())},
            {"commands", commands}};
}

// The catalog dialog's dry run (catalog_dialog.cpp, CatalogDialog::commands and refreshInsert):
// the catalog as unpadded base64url when it is not in the library folder, or by its name when it
// is.
QJsonArray dialogCommands(bool byName, const QString &value, const QString &entry) {
    QJsonObject command{{"command", "catalog.import"}};
    command[byName ? "library" : "catalog"] = value;
    command["entries"] = QJsonArray{entry};
    return {command};
}

QString base64Url(const QByteArray &file) {
    return QString::fromLatin1(
        file.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

int write(const QString &directory) {
    // Each shared texture is 16 MiB less 16 KiB, and so is each component's texture: together the
    // payload region is 128 MiB less 128 KiB, under the 128 MiB region limit and the 64 MiB asset
    // total (per side), so the file is the largest this envelope takes with this composition.
    const qsizetype size = (qsizetype(16) << 20) - (qsizetype(16) << 10);
    CatalogMetadata meta;
    meta.catalogId = QStringLiteral("r100g.budget");
    meta.name = QStringLiteral("Budget catalog");
    meta.description =
        QStringLiteral("Generated by style_catalog_benchmark for ADR 0174 section 9.");
    meta.labels = {QStringLiteral("budget")};
    meta.author = QStringLiteral("R100.g measurement");
    meta.licenseText = QStringLiteral("Generated for the R100.g budget measurement.");
    meta.licenseIdentifier = QStringLiteral("LicenseRef-SketchyUp-Tests");
    meta.version = QStringLiteral("1.0.0");
    CatalogBuilder builder(meta);
    for (int i = 0; i < 4; ++i) {
        const auto name = QStringLiteral("Shared stone %1").arg(i + 1);
        const auto asset = builder.addAsset(name, QStringLiteral("image/png"),
                                            randomBytes(size, 100 + std::uint64_t(i)));
        builder.addMaterial(name, QStringLiteral("A shared texture"),
                            {name.toStdString(), {0.5f, 0.5f, 0.5f}, 1, asset});
    }
    for (int i = 0; i < 4; ++i) {
        Document doc(DisplayUnit::Millimeters);
        const auto asset = createAsset(doc, "Stone texture", "image/png",
                                       payloadOf(randomBytes(size, 200 + std::uint64_t(i))));
        const auto material = createMaterial(doc, "Stone", {0.5f, 0.5f, 0.5f}, 1, asset);
        const auto body =
            doc.addFace({{{0, 0, 0}, {1000, 0, 0}, {1000, 1000, 0}, {0, 1000, 0}}}, "Slab");
        assignMaterial(doc, body, std::nullopt, material);
        const auto component = createComponent(doc, body, "Big");
        builder.addComponent(QStringLiteral("Big %1").arg(i + 1),
                             QStringLiteral("One slab with its own texture"), doc,
                             component.definition);
    }
    QElapsedTimer timer;
    timer.start();
    const auto bytes = builder.encode();
    const auto encodeMs = timer.nsecsElapsed() / 1e6;
    writeCatalog(bytes, QDir(directory).filePath(QStringLiteral("big.sketchycat")));
    std::cout << "wrote big.sketchycat: " << bytes.size() << " bytes (" << bytes.size() / 1048576.0
              << " MiB) in " << encodeMs << " ms (encode and check)\n";
    return 0;
}

int time(const QString &path, int runs) {
    const auto file = readFile(path);
    const auto folder = catalogLibraryFolder();
    QDir().mkpath(folder);
    const auto libraryPath = QDir(folder).filePath(QStringLiteral("big.sketchycat"));
    QFile::remove(libraryPath);
    check(QFile::copy(path, libraryPath), "Cannot copy the catalog into the library folder");
    std::cout << "file " << file.size() << " bytes (" << file.size() / 1048576.0 << " MiB)\n";

    std::vector<double> decode, load, scan, insert, dialogFile, encode, unpack, dialogLibrary, list;
    CatalogPackage package = decodeCatalog(file);
    for (int i = 0; i < runs; ++i) {
        QElapsedTimer t;
        t.start();
        package = decodeCatalog(file);
        decode.push_back(t.nsecsElapsed() / 1e6);
    }
    for (int i = 0; i < runs; ++i) {
        QElapsedTimer t;
        t.start();
        const auto loaded = loadCatalog(path);
        load.push_back(t.nsecsElapsed() / 1e6);
        check(loaded.entries.size() == package.entries.size(),
              "The load disagrees with the decode");
    }
    for (int i = 0; i < runs; ++i) {
        QElapsedTimer t;
        t.start();
        const auto library = scanCatalogLibrary(folder);
        scan.push_back(t.nsecsElapsed() / 1e6);
        check(library.entries.size() == 1 && library.entries[0].error.isEmpty(),
              "The library scan did not list the catalog");
    }
    for (int i = 0; i < runs; ++i) {
        Document doc(DisplayUnit::Millimeters);
        QElapsedTimer t;
        t.start();
        const auto result = importCatalogEntries(doc, package, {QStringLiteral("Big 1")});
        insert.push_back(t.nsecsElapsed() / 1e6);
        check(result.entries.size() == 1, "The insert did not place the entry");
    }
    // The dialog's refresh for a catalog that is not in the library folder: the base64url encode of
    // the whole file, then the shared dry run (previewBatch) of the batch that carries it.
    Document model(DisplayUnit::Millimeters);
    for (int i = 0; i < runs; ++i) {
        QElapsedTimer t;
        t.start();
        const auto encoded = base64Url(file);
        encode.push_back(t.nsecsElapsed() / 1e6);
        QElapsedTimer total;
        total.start();
        const auto request =
            batchFor(model, dialogCommands(false, encoded, QStringLiteral("Big 1")));
        previewBatch(model, request);
        dialogFile.push_back(total.nsecsElapsed() / 1e6);
    }
    // The engine's first step on the file form: the base64url decode of the request's bytes.
    const auto encodedFile = base64Url(file);
    for (int i = 0; i < runs; ++i) {
        QElapsedTimer t;
        t.start();
        const auto decoded = QByteArray::fromBase64Encoding(
            encodedFile.toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
        unpack.push_back(t.nsecsElapsed() / 1e6);
        check(decoded.decoded == file, "The base64url round trip changed the bytes");
    }
    // The dialog's refresh for a catalog in the library folder: the batch names it, and the engine
    // reads and decodes the file on each dry run.
    for (int i = 0; i < runs; ++i) {
        QElapsedTimer t;
        t.start();
        const auto request =
            batchFor(model, dialogCommands(true, QStringLiteral("big"), QStringLiteral("Big 1")));
        previewBatch(model, request);
        dialogLibrary.push_back(t.nsecsElapsed() / 1e6);
    }
    // The read-only listing (catalog.library and the list query's decode) for the same file.
    for (int i = 0; i < runs; ++i) {
        QElapsedTimer t;
        t.start();
        const auto listing = encodeCatalogListing(decodeCatalog(file));
        list.push_back(t.nsecsElapsed() / 1e6);
        check(!listing.isEmpty(), "The listing is empty");
    }
    report("decodeCatalog, the whole file checked", decode);
    report("loadCatalog, read and decode", load);
    report("scanCatalogLibrary, the one-file folder", scan);
    report("importCatalogEntries, one entry, decoded package", insert);
    report("base64url encode of the file (dialog, file form)", encode);
    report("base64url decode of the request bytes (engine, file form)", unpack);
    report("dialog refresh, file form (encode, batch, dry run)", dialogFile);
    report("dialog refresh, library form (read, decode, dry run)", dialogLibrary);
    report("listing after decode", list);
    return 0;
}

// A grid of separate quads with profiles on, the same shape as edge_stroke_benchmark.
Document grid(int faces) {
    Document doc(DisplayUnit::Millimeters);
    const int side = std::max(1, int(std::ceil(std::sqrt(double(faces)))));
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x)
            doc.addFace({{{x * 1.2, y * 1.2, 0},
                          {x * 1.2 + 1, y * 1.2, 0},
                          {x * 1.2 + 1, y * 1.2 + 1, 0},
                          {x * 1.2, y * 1.2 + 1, 0}}});
    return doc;
}

QJsonObject batch(const Document &doc, const QJsonArray &commands) {
    return batchFor(doc, commands);
}

// The style.export query's `data` (canonical base64, the form style.apply takes).
QString exportStyle(const Document &doc, const QString &name) {
    const QJsonObject request{{"apiVersion", 1},
                              {"documentId", QString::fromStdString(doc.identity())},
                              {"expectedRevision", QString::number(doc.revision())},
                              {"query", "style.export"},
                              {"name", name},
                              {"labels", QJsonArray{"benchmark"}}};
    const auto response = inspectDocument(doc, request);
    if (!response.contains("data"))
        throw std::runtime_error(
            "style.export: " +
            QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString());
    return response["data"].toObject()["data"].toString();
}

int style(int faces, int runs) {
    // Two styles that differ in every field the edit changes: A has an edge stroke with a texture,
    // B has none. Alternating them makes each batch a real change, never a no-op refusal.
    Document source(DisplayUnit::Millimeters);
    const auto texture = createAsset(
        source, "Edge texture", "image/png",
        payloadOf(encodeTexturePng(TextureImage(2, 1, {255, 0, 0, 255, 0, 255, 0, 255}))));
    auto styleA = source.style();
    styleA.profiles = true;
    styleA.profileWidth = 2;
    styleA.edgeStroke = EdgeStroke{3, 0.5, EdgeDash::Dashed, texture};
    source.setStyle(styleA);
    Document plainSource(DisplayUnit::Millimeters);
    auto styleB = plainSource.style();
    styleB.profiles = true;
    styleB.profileWidth = 4;
    plainSource.setStyle(styleB);
    const auto fileA = exportStyle(source, "Benchmark A");
    const auto fileB = exportStyle(plainSource, "Benchmark B");
    check(!fileA.isEmpty() && !fileB.isEmpty(), "style.export returned no data");

    Document model = grid(faces);
    // The texture is already in the model, so style.apply reuses it (ADR 0131) and the two paths
    // change the same things: the record and no asset.
    createAsset(model, "Edge texture", "image/png",
                payloadOf(encodeTexturePng(TextureImage(2, 1, {255, 0, 0, 255, 0, 255, 0, 255}))));
    std::vector<double> applySamples, editSamples;
    for (int i = 0; i < runs; ++i) {
        const bool useA = i % 2 == 0;
        QElapsedTimer t;
        t.start();
        executeBatch(
            model,
            batch(model, {QJsonObject{{"command", "style.apply"}, {"data", useA ? fileA : fileB}}}),
            BatchResponse::Changes);
        applySamples.push_back(t.nsecsElapsed() / 1e6);
    }
    for (int i = 0; i < runs; ++i) {
        const bool useA = i % 2 == 0;
        // The equal labelled edit: document.style with the same record that style.apply sets.
        const auto record = useA ? styleA : styleB;
        QElapsedTimer t;
        t.start();
        executeBatch(model,
                     batch(model, {QJsonObject{{"command", "document.style"},
                                               {"style", encodeModelStyle(record)}}}),
                     BatchResponse::Changes);
        editSamples.push_back(t.nsecsElapsed() / 1e6);
    }
    report(std::string("style.apply, ") + std::to_string(faces) + " faces", applySamples);
    report(std::string("document.style edit, ") + std::to_string(faces) + " faces", editSamples);
    return 0;
}

} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("SketchyUp");
    QCoreApplication::setApplicationName("SketchyUp");
    QTemporaryDir home;
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + "/data").toUtf8());
    try {
        const QString mode = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
        if (mode == "write" && argc == 3)
            return write(QString::fromLocal8Bit(argv[2]));
        if (mode == "time" && argc == 4)
            return time(QString::fromLocal8Bit(argv[2]), std::atoi(argv[3]));
        if (mode == "style" && argc == 4)
            return style(std::atoi(argv[2]), std::atoi(argv[3]));
        std::cerr
            << "usage: style_catalog_benchmark write DIR | time FILE RUNS | style FACES RUNS\n";
        return 2;
    } catch (const std::exception &error) {
        std::cerr << "style_catalog_benchmark: " << error.what() << "\n";
        return 1;
    }
}
