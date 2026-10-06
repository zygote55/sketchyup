#include "integrations/render_environment.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
QByteArray fixture(int width, int height, bool rle) {
    QByteArray bytes =
        "#?RADIANCE\n# Deterministic linear RGBE fixture\nFORMAT=32-bit_rle_rgbe\n\n-Y " +
        QByteArray::number(height) + " +X " + QByteArray::number(width) + "\n";
    for (int y = 0; y < height; ++y) {
        if (rle) {
            bytes.append(char(2)).append(char(2)).append(char(width >> 8)).append(char(width));
            for (int value : {128, 64, 32, 129})
                bytes.append(char(128 + width)).append(char(value));
        } else {
            for (int x = 0; x < width; ++x)
                bytes.append(char(128)).append(char(64)).append(char(32)).append(char(129));
        }
    }
    return bytes;
}
void write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(),
          "Write HDR fixture");
}
template <class F> void rejects(F action) {
    try {
        action();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Invalid environment accepted");
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir root;
        const auto path = root.filePath("environment.hdr");
        for (bool rle : {false, true}) {
            auto bytes = fixture(rle ? 8 : 2, rle ? 4 : 1, rle);
            write(path, bytes);
            const auto image = readRenderEnvironment(path, 2, 90);
            check(image.image == bytes && image.width == (rle ? 8 : 2) && image.strength == 2 &&
                      image.rotationDegrees == 90,
                  "Immutable exact environment and settings");
            check(describeRenderEnvironment(image)["sha256"].toString().size() == 64,
                  "Content-addressed packaged image");
            QFile::remove(path);
            validateRenderEnvironment(image);
            for (qsizetype size = 0; size < bytes.size(); ++size) {
                auto bad = image;
                bad.image = bytes.left(size);
                rejects([&] { validateRenderEnvironment(bad); });
            }
            auto bad = image;
            bad.image.append('x');
            rejects([&] { validateRenderEnvironment(bad); });
            bad = image;
            ++bad.width;
            rejects([&] { validateRenderEnvironment(bad); });
            for (double value : {-1., 101., std::numeric_limits<double>::infinity(),
                                 std::numeric_limits<double>::quiet_NaN()}) {
                bad = image;
                bad.strength = value;
                rejects([&] { validateRenderEnvironment(bad); });
            }
            bad = image;
            bad.rotationDegrees = 361;
            rejects([&] { validateRenderEnvironment(bad); });
        }
        for (const auto &bytes :
             {fixture(2, 2, false), QByteArray("#?RADIANCE\n") + QByteArray(8192, 'a'),
              QByteArray("#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2049 +X 4098\n")}) {
            write(path, bytes);
            rejects([&] { readRenderEnvironment(path); });
        }
        auto bytes = fixture(8, 4, true);
        const auto offset = bytes.indexOf("-Y 4 +X 8\n") + 10;
        for (auto [index, value] :
             {std::pair{offset + 3, 9}, {offset + 4, 0}, {offset + 4, 137}, {offset + 11, 255}}) {
            auto bad = bytes;
            bad[index] = char(value);
            write(path, bad);
            rejects([&] { readRenderEnvironment(path); });
        }
        write(path, fixture(2, 1, false));
        const auto link = root.filePath("link.hdr");
        check(QFile::link(path, link), "Create symlink fixture");
        rejects([&] { readRenderEnvironment(link); });
        std::cout
            << "Bounded RGBE capture, RLE, truncation, settings, radiance and relocation passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
