// Manual, synthetic file-size benchmark; never a variable-host timing gate.
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <fcntl.h>
#include <iostream>
#include <sys/resource.h>
#include <unistd.h>
using namespace sketchy;
namespace {
QString hashFile(const QString &path) {
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file))
        throw std::runtime_error("Cannot hash benchmark file");
    return QString::fromLatin1(hash.result().toHex());
}
Document fixture() {
    std::map<Id, BodyPtr> bodies;
    for (Id id = 1; id <= 1000; ++id) {
        auto body = std::make_shared<Body>();
        body->id = id;
        body->name = "Metadata stress face " + std::to_string(id);
        const double x = double(id % 40) * 2, y = double(id / 40) * 2;
        body->surface.addFace({{{x, y, 0}, {x + 1, y, 0}, {x + 1, y + 1, 0}, {x, y + 1, 0}}});
        for (int property = 0; property < 16; ++property)
            body->properties["benchmark_" + std::to_string(property)] =
                std::string(2040, char('a' + property));
        bodies.emplace(id, body);
    }
    AssetRecords assets;
    for (Id id = 1; id <= 4; ++id) {
        std::vector<std::uint8_t> bytes(AssetPayload::limit);
        std::uint32_t state = std::uint32_t(id);
        for (auto &byte : bytes) {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            byte = std::uint8_t(state);
        }
        assets.emplace(id, std::make_shared<const AssetRecord>(
                               AssetRecord{id, "Synthetic local payload " + std::to_string(id),
                                           "application/octet-stream",
                                           std::make_shared<const AssetPayload>(bytes)}));
    }
    Document doc;
    doc.restore("0000000000000000000000000000082c", 1001, std::move(bodies), 0, {}, {}, 1, {}, 1,
                {}, 1, std::move(assets), 5);
    return doc;
}
void cachePolicy(const QString &path, bool adviseCold) {
    if (adviseCold) {
        const int fd = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_CLOEXEC);
        if (fd < 0)
            throw std::runtime_error("Cannot open benchmark file for cache advice");
        const int result = posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
        ::close(fd);
        if (result != 0)
            throw std::runtime_error("Per-file cache advice failed");
    } else {
        // Read the whole file immediately before the measured load.
        (void)hashFile(path);
    }
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        const auto args = app.arguments();
        const bool prepare = args.size() == 3 && args[1] == "--prepare";
        const bool measure = args.size() == 5 && args[1] == "--measure" &&
                             (args[4] == "warm" || args[4] == "advised-cold");
        if (!prepare && !measure)
            throw std::runtime_error("Use --prepare NEW_FILE or --measure INPUT NEW_FILE "
                                     "warm|advised-cold");
        const auto output = args[prepare ? 2 : 3];
        if (QFileInfo::exists(output) || QFileInfo(output).isSymLink())
            throw std::runtime_error("Benchmark output must not already exist");
        QJsonObject report{{"fixtureVersion", 1}, {"releaseAcceptance", false}};
        if (prepare) {
            auto doc = fixture();
            saveDocument(doc, output);
            const auto bytes = QFileInfo(output).size();
            if (bytes < 100000000 || bytes > 101000000)
                throw std::runtime_error("Fixture must occupy 100 to 101 decimal MB");
            report["nativeBytes"] = bytes;
            report["fixtureSha256"] = hashFile(output);
            report["bodies"] = 1000;
            report["assetBytes"] = qint64(assetTotalLimit);
        } else {
            const auto input = args[2];
            cachePolicy(input, args[4] == "advised-cold");
            QElapsedTimer timer;
            timer.start();
            auto doc = loadDocument(input);
            const double openMs = timer.nsecsElapsed() / 1e6;
            timer.restart();
            saveDocument(doc, output);
            const double saveMs = timer.nsecsElapsed() / 1e6;
            rusage usage{};
            if (getrusage(RUSAGE_SELF, &usage) != 0)
                throw std::runtime_error("Cannot read benchmark RSS");
            const auto expected = hashFile(input);
            if (hashFile(output) != expected)
                throw std::runtime_error("Load/save changed canonical native bytes");
            report["fixtureSha256"] = expected;
            report["nativeBytes"] = QFileInfo(input).size();
            report["openMs"] = openMs;
            report["saveMs"] = saveMs;
            report["peakRssBytes"] = qint64(usage.ru_maxrss) * 1024;
            report["cachePolicy"] = args[4];
            report["coldCacheVerified"] = false;
            report["byteExactRoundTrip"] = true;
        }
        report["compiler"] = __VERSION__;
        report["qt"] = qVersion();
#ifdef NDEBUG
        report["build"] = "Release";
#else
        report["build"] = "Debug";
#endif
        report["timingScope"] = "Synchronous core load/validation and durable save; excludes "
                                "fixture construction, post-check hashing and UI responsiveness";
        std::cout << QJsonDocument(report).toJson().toStdString();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
