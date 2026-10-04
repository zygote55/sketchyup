#include "core/assets.hpp"
#include "io/recovery.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>
using namespace sketchy;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
QByteArray read(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Read CLI fixture");
    return file.readAll();
}
QJsonObject run(const QStringList &arguments, bool success = true) {
    QProcess process;
    process.start(CLI_PATH, arguments);
    check(process.waitForFinished(30000) && process.exitStatus() == QProcess::NormalExit &&
              (process.exitCode() == 0) == success,
          "CLI exit status matches expected outcome");
    const auto bytes = success ? process.readAllStandardOutput() : process.readAllStandardError();
    const auto result = QJsonDocument::fromJson(bytes);
    check(result.isObject(), "CLI returns structured JSON");
    return result.object();
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir files;
        const auto root = files.filePath("recovery"), source = files.filePath("original.sketchyup");
        Document doc;
        doc.addFace({{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}});
        saveDocument(doc, source);
        const auto before = read(source);
        RecoveryContext context{source, doc.revision(), QDateTime::currentDateTimeUtc()};
        createAsset(doc, "Missing preview.png", "image/png");
        doc.move(1, {1, 0, 0});
        QString key;
        {
            RecoveryWriter writer(root, QString::fromStdString(doc.identity()));
            key = writer.key();
            writer.write(captureRecovery(doc, context));
        }
        const auto path = QDir(root).filePath(key), pointer = QDir(path).filePath("CURRENT");
        const auto originalPointer = read(pointer);
        const auto listed = run({"--recovery-list", root})["recoveries"].toArray();
        check(listed.size() == 1 && listed[0].toObject()["verified"] == true &&
                  listed[0].toObject()["missingAssets"].toArray().size() == 1,
              "CLI lists verified copies and missing resources");
        const auto output = files.filePath("recovered.sketchyup");
        const auto saved = run({"--recover", path + "/", "--output", output});
        check(saved["recoveryReport"].toObject()["revision"] == QString::number(doc.revision()) &&
                  encodeContainer(loadDocument(output)) == encodeContainer(doc) &&
                  read(source) == before && read(pointer) == originalPointer,
              "Headless recovery preserves exact native records and both source files");
        const auto sourceAlias = files.filePath("source-alias.sketchyup");
        check(QFile::link(source, sourceAlias), "Source alias fixture");
        for (const auto &target : {source, sourceAlias, pointer})
            check(
                run({"--recover", path, "--output", target}, false)["error"].toString().startsWith(
                    "Recovery output"),
                "Unsafe recovery output rejected");
        const auto directoryAlias = files.filePath("session-alias");
        check(QFile::link(path, directoryAlias), "Recovery directory alias fixture");
        check(run({"--recover", path, "--output", QDir(directoryAlias).filePath("new.sketchyup")},
                  false)["error"]
                  .toString()
                  .contains("outside"),
              "Output through linked recovery parent rejected");
        check(read(source) == before && read(pointer) == originalPointer,
              "Failed CLI recovery never alters source or evidence");
        run({"--recover", path, "--input", source}, false);
        run({"--recovery-list", root, "--output", output}, false);
        {
            RecoveryWriter active(root, QString::fromStdString(doc.identity()));
            active.write(captureRecovery(doc));
            run({"--recover", QDir(root).filePath(active.key())}, false);
            active.discard();
        }
        QFile damaged(pointer);
        check(damaged.open(QIODevice::WriteOnly | QIODevice::Truncate) && damaged.write("bad") == 3,
              "Corrupt pointer fixture");
        damaged.close();
        run({"--recover", path}, false);
        check(run({"--recovery-list", root})["recoveries"].toArray()[0].toObject()["verified"] ==
                  false,
              "Unreadable candidate is not claimed as recovered");
        std::cout << "CLI recovery inspection, native output, missing assets, source/alias "
                     "preservation, argument validation and active/corrupt rejection passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
