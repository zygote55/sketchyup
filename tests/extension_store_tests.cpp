#include "automation/extension_store.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <iostream>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F run) {
    try {
        run();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Expected registry rejection");
}
QByteArray read(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Read store fixture");
    return file.readAll();
}
void write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(),
          "Write store fixture");
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir scratch;
        const auto source =
            read(QStringLiteral(SOURCE_DIR) + "/examples/extensions/panel.sketchyext");
        const auto folder = scratch.filePath("registry");
        const auto path = QDir(folder).filePath("extensions.json");
        ExtensionStore store(folder);
        check(store.entries().empty() && !QFile::exists(path),
              "Empty registry is read-only until install");
        store.install(source);
        check(store.entries().size() == 1 && !store.entries().at("org.sketchyup.panel").enabled &&
                  store.entries().at("org.sketchyup.panel").source == source,
              "Install copies source exactly and starts disabled");
        rejects([&] { store.enabledManifest("org.sketchyup.panel"); });
        const auto installed = read(path);
        rejects([&] { store.install(source); });
        rejects([&] { store.install("{}"); });
        check(read(path) == installed, "Duplicate or invalid installs preserve registry");
        store.setEnabled("org.sketchyup.panel", true);
        ExtensionStore reopened(folder);
        check(reopened.enabledManifest("org.sketchyup.panel").source == source,
              "Enabled state and independent copied source survive reload");
        reopened.recordFailure("org.sketchyup.panel", "Worker terminated unexpectedly");
        ExtensionStore failed(folder);
        check(!failed.entries().at("org.sketchyup.panel").enabled &&
                  failed.entries().at("org.sketchyup.panel").error ==
                      "Worker terminated unexpectedly",
              "Worker failure persists disabled/error state");
        rejects([&] { failed.enabledManifest("org.sketchyup.panel"); });
        // Older store instances cannot overwrite another window's state.
        const auto failureBytes = read(path);
        rejects([&] { store.setEnabled("org.sketchyup.panel", true); });
        check(read(path) == failureBytes,
              "Concurrent stale registry update cannot overwrite changes");
        failed.setEnabled("org.sketchyup.panel", true);
        check(failed.entries().at("org.sketchyup.panel").error.isEmpty(),
              "Explicit enable clears a prior failure");
        failed.setEnabled("org.sketchyup.panel", false);
        rejects([&] { failed.enabledManifest("org.sketchyup.panel"); });
        failed.remove("org.sketchyup.panel");
        check(ExtensionStore(folder).entries().empty(),
              "Uninstall persists without affecting source package");
        failed.install(source);
        failed.setEnabled("org.sketchyup.panel", true);
        auto registry = QJsonDocument::fromJson(read(path)).object();
        auto entries = registry["entries"].toArray();
        auto entry = entries[0].toObject();
        auto futureManifest = QJsonDocument::fromJson(source).object();
        futureManifest["commandApiVersion"] = 2;
        const auto futureBytes = QJsonDocument(futureManifest).toJson(QJsonDocument::Compact);
        entry["manifest"] = QString::fromLatin1(futureBytes.toBase64());
        entry["sha256"] = QString::fromLatin1(
            QCryptographicHash::hash(futureBytes, QCryptographicHash::Sha256).toHex());
        entries[0] = entry;
        registry["entries"] = entries;
        write(path, QJsonDocument(registry).toJson());
        ExtensionStore incompatible(folder);
        check(
            !incompatible.entries().at("org.sketchyup.panel").manifest &&
                !incompatible.entries().at("org.sketchyup.panel").enabled &&
                incompatible.entries().at("org.sketchyup.panel").source == futureBytes,
            "Incompatible packages remain inspectable with source retained and execution disabled");
        rejects([&] { incompatible.setEnabled("org.sketchyup.panel", true); });
        incompatible.remove("org.sketchyup.panel");
        const auto blocked = scratch.filePath("blocked");
        write(blocked, "retained");
        ExtensionStore unwritable(blocked);
        rejects([&] { unwritable.install(source); });
        check(unwritable.entries().empty() && read(blocked) == "retained",
              "Failed persistence never publishes in-memory installation");
        write(path, "broken registry");
        rejects([&] { ExtensionStore invalid(folder); });
        check(read(path) == "broken registry",
              "Invalid registry is not silently reset or overwritten");
        const auto linked = scratch.filePath("linked");
        check(QDir().mkdir(linked), "Link directory");
        check(QFile::link(path, QDir(linked).filePath("extensions.json")),
              "Registry symlink fixture");
        rejects([&] { ExtensionStore link(linked); });
        std::cout << "Extension store: install/enable/disable/error/remove, reload, compatibility, "
                     "source and atomic state protection passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
