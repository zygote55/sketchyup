#include "integrations/render_environment.hpp"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <cmath>
#include <stdexcept>
#include <vector>
namespace sketchy {
namespace {
constexpr qsizetype encodedLimit = 40 * 1024 * 1024;
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
std::pair<int, int> inspect(const QByteArray &bytes) {
    require(bytes.size() > 0 && bytes.size() <= encodedLimit,
            "HDR environment must contain at most 40 MiB");
    qsizetype cursor{};
    auto line = [&] {
        const auto end = bytes.indexOf('\n', cursor);
        require(end >= cursor && end < 8192, "Invalid or oversized Radiance header");
        auto result = bytes.mid(cursor, end - cursor);
        if (result.endsWith('\r'))
            result.chop(1);
        cursor = end + 1;
        return result;
    };
    const auto magic = line();
    require(magic == "#?RADIANCE" || magic == "#?RGBE", "Choose a Radiance RGBE .hdr image");
    bool format{};
    for (;;) {
        const auto value = line();
        if (value.isEmpty())
            break;
        if (value.startsWith("FORMAT=")) {
            require(!format && value == "FORMAT=32-bit_rle_rgbe", "Unsupported Radiance encoding");
            format = true;
        }
    }
    require(format, "Radiance image has no RGBE format declaration");
    const auto resolution = QString::fromLatin1(line());
    const auto match =
        QRegularExpression("^-Y ([1-9][0-9]{0,4}) \\+X ([1-9][0-9]{0,4})$").match(resolution);
    require(match.hasMatch(), "HDR environment requires standard -Y +X orientation");
    const int height = match.captured(1).toInt(), width = match.captured(2).toInt();
    require(width == height * 2 && width <= 4096 && height <= 2048,
            "HDR environment must be a 2:1 panorama up to 4096 by 2048 pixels");
    auto byte = [&]() -> unsigned char {
        require(cursor < bytes.size(), "Truncated Radiance pixels");
        return static_cast<unsigned char>(bytes[cursor++]);
    };
    std::vector<unsigned char> row(size_t(width) * 4);
    for (int y = 0; y < height; ++y) {
        const auto start = cursor;
        unsigned char first[4];
        for (auto &value : first)
            value = byte();
        if (width >= 8 && first[0] == 2 && first[1] == 2 && !(first[2] & 128)) {
            require((int(first[2]) * 256 + first[3]) == width, "Radiance scanline width mismatch");
            for (int channel = 0; channel < 4; ++channel) {
                int x{};
                while (x < width) {
                    const auto count = byte();
                    require(count != 0, "Invalid Radiance run");
                    const int length = count > 128 ? count - 128 : count;
                    require(length <= width - x, "Radiance run exceeds scanline");
                    if (count > 128) {
                        const auto value = byte();
                        for (int i = 0; i < length; ++i)
                            row[size_t(x++) * 4 + channel] = value;
                    } else {
                        for (int i = 0; i < length; ++i)
                            row[size_t(x++) * 4 + channel] = byte();
                    }
                }
            }
        } else {
            cursor = start;
            for (auto &value : row)
                value = byte();
            for (int x = 0; x < width; ++x)
                require(!(row[size_t(x) * 4] == 1 && row[size_t(x) * 4 + 1] == 1 &&
                          row[size_t(x) * 4 + 2] == 1),
                        "Legacy Radiance repeat encoding is unsupported; resave as modern RGBE");
        }
        for (int x = 0; x < width; ++x) {
            const auto exponent = row[size_t(x) * 4 + 3];
            if (!exponent)
                continue;
            for (int channel = 0; channel < 3; ++channel)
                require(std::ldexp((double(row[size_t(x) * 4 + channel]) + .5),
                                   int(exponent) - 136) <= 1e6,
                        "HDR environment radiance exceeds one million");
        }
    }
    require(cursor == bytes.size(), "Unexpected trailing Radiance image data");
    return {width, height};
}
} // namespace
RenderEnvironment readRenderEnvironment(const QString &path, double strength,
                                        double rotationDegrees) {
    QFile file(path);
    require(QFileInfo(path).isFile() && !QFileInfo(path).isSymLink() &&
                file.open(QIODevice::ReadOnly) && file.size() <= encodedLimit,
            "Cannot read a bounded regular HDR environment image");
    RenderEnvironment result;
    result.image = file.read(encodedLimit + 1);
    require(file.error() == QFileDevice::NoError, "Cannot read complete HDR environment image");
    const auto [width, height] = inspect(result.image);
    result.width = width;
    result.height = height;
    result.strength = strength;
    result.rotationDegrees = rotationDegrees;
    validateRenderEnvironment(result);
    return result;
}
void validateRenderEnvironment(const RenderEnvironment &environment) {
    const auto [width, height] = inspect(environment.image);
    require(width == environment.width && height == environment.height,
            "HDR environment dimensions do not match its pixels");
    require(std::isfinite(environment.strength) && environment.strength >= 0 &&
                environment.strength <= 100 && std::isfinite(environment.rotationDegrees) &&
                environment.rotationDegrees >= -360 && environment.rotationDegrees <= 360,
            "HDR environment strength must be 0–100 and rotation -360–360 degrees");
}
QJsonObject describeRenderEnvironment(const RenderEnvironment &environment) {
    return {{"file", "environment.hdr"},
            {"mediaType", "image/vnd.radiance"},
            {"bytes", environment.image.size()},
            {"sha256",
             QString::fromLatin1(
                 QCryptographicHash::hash(environment.image, QCryptographicHash::Sha256).toHex())},
            {"width", environment.width},
            {"height", environment.height},
            {"strength", environment.strength},
            {"rotationDegrees", environment.rotationDegrees},
            {"projection", "equirectangular"},
            {"colorSpace", "Linear Rec.709"}};
}
} // namespace sketchy
