#include "io/document_io.hpp"
#include "io/native_format.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtEndian>
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
        const auto valid = fixtures.filePath("m4-complete-v11.sketchyup"),
                   bad = files.filePath("bad.sketchyup"),
                   out = files.filePath("rejected.sketchyup");
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
        std::cout << migrated
                  << " historical native fixtures validate and migrate without source mutation\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
