#include "integrations/glb_export.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F action) {
    try {
        action();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Invalid environment export accepted");
}
QByteArray read(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Open fixture");
    return file.readAll();
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir root;
        const auto path = root.filePath("lighting.hdr");
        QByteArray pixels = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 2\n";
        for (int i = 0; i < 2; ++i)
            pixels.append(char(128)).append(char(64)).append(char(32)).append(char(129));
        QFile file(path);
        check(file.open(QIODevice::WriteOnly) && file.write(pixels) == pixels.size(),
              "Write environment fixture");
        file.close();
        QJsonObject environment{{"path", path}, {"strength", .75}, {"rotationDegrees", -45}};
        const QJsonObject settings{{"apiVersion", 1}, {"environment", environment}};
        const auto options = parseRenderOptions(settings);
        check(options.environment && options.environment->image == pixels,
              "Settings capture exact bounded image");
        for (const auto &key : {"unexpected", "strength", "rotationDegrees"}) {
            auto bad = environment;
            bad[key] = "invalid";
            rejects([&] { parseRenderOptions({{"apiVersion", 1}, {"environment", bad}}); });
        }
        Document document;
        document.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
        const auto revision = document.revision();
        const auto snapshot = RenderSnapshot::capture(document, options);
        check(QFile::remove(path), "Remove original source");
        document.addFace({{{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}});
        const auto exported = exportGlb(snapshot);
        check(exported.environment == pixels &&
                  exported.manifest["revision"] == QString::number(revision) &&
                  exported.manifest["environment"] ==
                      describeRenderEnvironment(*options.environment) &&
                  exported.manifest["losses"].toObject()["environmentLightingOmitted"] == 1,
              "GLB packages frozen environment and reports standalone lighting omission");
        const auto output = root.filePath("relocated");
        writeGlbExport(exported, output);
        check(read(output + "/environment.hdr") == pixels &&
                  read(output + "/scene.glb") == exported.glb &&
                  QJsonDocument::fromJson(read(output + "/manifest.json")).object() ==
                      exported.manifest,
              "Relocated package is self contained and byte exact");
        for (bool bytes : {false, true}) {
            auto bad = exported;
            if (bytes)
                bad.environment.append('x');
            else {
                auto metadata = bad.manifest["environment"].toObject();
                metadata["sha256"] = "forged";
                bad.manifest["environment"] = metadata;
            }
            const auto destination = root.filePath("invalid");
            rejects([&] { writeGlbExport(bad, destination); });
            check(!QFileInfo::exists(destination), "Invalid publication leaves no directory");
        }
        const auto plain = exportGlb(RenderSnapshot::capture(document));
        check(plain.environment.isEmpty() && !plain.manifest.contains("environment") &&
                  plain.manifest["losses"].toObject()["environmentLightingOmitted"] == 0,
              "Default exports do not add an environment");
        std::cout
            << "Captured HDR settings, immutable export, relocation and atomic validation passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
