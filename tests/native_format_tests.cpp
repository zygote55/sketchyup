#include "io/document_io.hpp"
#include "io/native_format.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QtEndian>
#include <functional>
#include <future>
#include <iostream>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
QByteArray read(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Read fixture");
    return file.readAll();
}
void write(const QString &path, const QByteArray &data) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(data) == data.size(), "Write fixture");
}
template <class F> void rejects(F op) {
    bool failed = false;
    try {
        op();
    } catch (const std::exception &) {
        failed = true;
    }
    check(failed, "Unsafe native operation must fail");
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir files;
        check(files.isValid(), "Migration fixture directory");
        QDir fixtures(QString(SOURCE_DIR) + "/tests/fixtures");
        const auto names =
            fixtures.entryList({"*.sketchyup", "raw-*.json"}, QDir::Files, QDir::Name);
        check(names.size() >= 20, "Historical fixture corpus present");
        int migrated{};
        for (const auto &name : names) {
            const auto source = fixtures.filePath(name),
                       output = files.filePath(name + ".current.sketchyup");
            const auto before = read(source), model = encodeDocument(loadDocument(source));
            const auto inspected = inspectNativeFile(source);
            check(inspected["valid"] == true && inspected["documentVersion"].toInt() >= 1,
                  "Historical file fully validates");
            const auto report = migrateNativeFile(source, output);
            check(report["sourceUnmodified"] == true && report["outputDocumentVersion"] == 24,
                  "Migration report identifies public schema");
            check(read(source) == before && encodeDocument(loadDocument(output)) == model,
                  "Migration preserves source bytes and all model records");
            check(!inspectNativeFile(output)["migrationNeeded"].toBool(),
                  "Current container requires no migration");
            rejects([&] { migrateNativeFile(source, output); });
            check(encodeDocument(loadDocument(output)) == model,
                  "Existing migration destination is not replaced");
            ++migrated;
        }
        // Exercise every accepted raw schema version, including versions with no
        // surviving feature-rich binary fixture in the repository.
        for (int version = 1; version <= 24; ++version) {
            auto tree = QJsonDocument::fromJson(encodeDocument(Document{})).object();
            tree["version"] = version;
            for (const auto &[introduced, fields] : std::vector<std::pair<int, QStringList>>{
                     {8, {"definitions", "instances", "nextDefinitionId"}},
                     {9, {"tags", "nextTagId"}},
                     {10, {"materials", "nextMaterialId"}},
                     {11, {"assets", "nextAssetId", "assetStorage"}},
                     {12, {"displayUnits"}},
                     {15, {"hosted"}},
                     {17, {"style"}},
                     {18, {"scenes", "nextSceneId"}},
                     {19, {"sections", "nextSectionId", "activeSections"}},
                     {21, {"annotations", "nextAnnotationId"}},
                     {23, {"solar"}}})
                if (version < introduced)
                    for (const auto &field : fields)
                        tree.remove(field);
            if (version == 1)
                tree.remove("revision");
            const auto source = files.filePath(QString("raw-v%1.json").arg(version));
            const auto destination = source + ".sketchyup";
            const auto original = QJsonDocument(tree).toJson();
            write(source, original);
            const auto report = migrateNativeFile(source, destination);
            check(report["documentVersion"] == version && read(source) == original &&
                      encodeDocument(loadDocument(source)) ==
                          encodeDocument(loadDocument(destination)),
                  "Every supported raw document version migrates on a copy");
        }
        const auto valid = fixtures.filePath("m4-complete-v11.sketchyup"),
                   bad = files.filePath("bad.sketchyup"),
                   out = files.filePath("rejected.sketchyup");
        auto mutateManifest = [&](const std::function<void(QJsonObject &)> &mutate) {
            auto bytes = read(valid);
            const auto length = qFromLittleEndian<quint32>(bytes.constData() + 12);
            auto manifest = QJsonDocument::fromJson(bytes.mid(16, length)).object();
            mutate(manifest);
            const auto encoded = QJsonDocument(manifest).toJson(QJsonDocument::Compact);
            auto header = bytes.first(16);
            qToLittleEndian<quint32>(encoded.size(), header.data() + 12);
            write(bad, header + encoded + bytes.mid(16 + length));
            rejects([&] { migrateNativeFile(bad, out); });
            check(!QFileInfo::exists(out), "Unknown required records cannot publish output");
        };
        mutateManifest([](QJsonObject &manifest) {
            auto features = manifest["requiredFeatures"].toArray();
            features.append("unknown-required-v1");
            manifest["requiredFeatures"] = features;
        });
        mutateManifest([](QJsonObject &manifest) {
            auto chunks = manifest["chunks"].toArray();
            auto chunk = chunks[0].toObject();
            chunk["kind"] = "unknown";
            chunks[0] = chunk;
            manifest["chunks"] = chunks;
        });
        auto bytes = read(valid);
        qToLittleEndian<quint32>(999, bytes.data() + 8);
        write(bad, bytes);
        rejects([&] { inspectNativeFile(bad); });
        rejects([&] { migrateNativeFile(bad, out); });
        check(!QFileInfo::exists(out), "Future container cannot publish a migration");
        bytes = read(valid);
        bytes[bytes.size() - 1] ^= 1;
        write(bad, bytes);
        rejects([&] { migrateNativeFile(bad, out); });
        auto tree = QJsonDocument::fromJson(encodeDocument(loadDocument(valid))).object();
        tree["unknownRequiredRecord"] = true;
        write(bad, QJsonDocument(tree).toJson());
        rejects([&] { migrateNativeFile(bad, out); });
        tree.remove("unknownRequiredRecord");
        tree["version"] = 25;
        write(bad, QJsonDocument(tree).toJson());
        rejects([&] { inspectNativeFile(bad); });
        const auto original = read(valid);
        rejects([&] { migrateNativeFile(valid, valid); });
        check(read(valid) == original, "In-place migration rejected");
        const auto link = files.filePath("symlink.sketchyup");
        check(QFile::link(valid, link), "Create destination link");
        rejects([&] { migrateNativeFile(valid, link); });
        check(read(valid) == original, "Symlink cannot redirect migration into source");
        const auto racing = files.filePath("race.sketchyup");
        auto attempt = [&] {
            try {
                migrateNativeFile(valid, racing);
                return true;
            } catch (const std::exception &) {
                return false;
            }
        };
        auto a = std::async(std::launch::async, attempt),
             b = std::async(std::launch::async, attempt);
        check(int(a.get()) + int(b.get()) == 1,
              "Concurrent publication admits exactly one writer without replacement");
        check(encodeDocument(loadDocument(racing)) == encodeDocument(loadDocument(valid)),
              "Racing publication is a complete verified container");
#ifdef CLI_PATH
        auto cli = [&](const QStringList &arguments, bool success = true) {
            QProcess process;
            process.start(CLI_PATH, arguments);
            check(process.waitForFinished(15000) && process.exitStatus() == QProcess::NormalExit &&
                      (process.exitCode() == 0) == success,
                  "Native format CLI exit status");
            const auto bytes =
                success ? process.readAllStandardOutput() : process.readAllStandardError();
            const auto object = QJsonDocument::fromJson(bytes).object();
            check(!object.empty(), "Native CLI reports structured JSON");
            return object;
        };
        check(cli({"--format-capabilities"}) == nativeFormatCapabilities(),
              "CLI describes exact format contract");
        check(cli({"--inspect-native", valid})["documentVersion"] == 11 &&
                  cli({"--validate-native", valid})["valid"] == true,
              "CLI validates historical input");
        const auto cliOutput = files.filePath("cli.sketchyup");
        check(cli({"--migrate-native", valid, "--output", cliOutput})["status"] == "migrated",
              "CLI publishes migration copy");
        cli({"--migrate-native", valid, "--output", cliOutput}, false);
        cli({"--migrate-native", valid, "--output", valid}, false);
        cli({"--inspect-native", valid, "--output", cliOutput}, false);
        cli({"--format-capabilities", "--input", valid}, false);
        cli({"--inspect-native", valid, "--validate-native", valid}, false);
        cli({"--migrate-native", valid}, false);
        check(read(valid) == original, "CLI never overwrites its input");
#endif
        std::cout << migrated
                  << " historical native fixtures validate and migrate without source mutation\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
