#include "app/texture_cache.hpp"
#include <QBuffer>
#include <QCoreApplication>
#include <QImage>
#include <QThreadPool>
#include <atomic>
#include <condition_variable>
#include <future>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
AssetPtr asset(Id id, QColor color) {
    QImage image(2, 2, QImage::Format_RGBA8888);
    image.fill(color);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    check(image.save(&buffer, "PNG"), "PNG fixture encoded");
    return std::make_shared<AssetRecord>(AssetRecord{
        id, "Image", "image/png",
        std::make_shared<AssetPayload>(std::vector<std::uint8_t>(bytes.begin(), bytes.end()))});
}
void settled(TextureCache &cache) {
    check(QThreadPool::globalInstance()->waitForDone(10000), "Worker finishes within deadline");
    check(!cache.pending(), "Latest request published");
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        const auto red = asset(1, Qt::red), blue = asset(2, Qt::blue), green = asset(3, Qt::green);
        std::atomic<int> calls{};
        auto decode = [&](const AssetRecord &record) {
            ++calls;
            return decodeTextureImage(record);
        };
        TextureCache cache({32, 128}, decode);
        check(cache.request({{1, red}, {2, blue}, {3, green}}), "Initial request queued");
        settled(cache);
        auto result = cache.snapshot();
        check(result->bytes == 32 && result->entries.at(1).image && result->entries.at(2).image &&
                  result->entries.at(3).status == "budget" && calls == 2,
              "Deterministic byte admission stops decoding at full budget");
        result.reset();
        auto renamed = std::make_shared<AssetRecord>(*red);
        renamed->name = "New name";
        check(!cache.request({{1, renamed}, {2, blue}, {3, green}}) && calls == 2,
              "Metadata rename reuses immutable payload without a job");
        cache.request({{2, blue}, {3, green}});
        settled(cache);
        check(cache.snapshot()->entries.at(3).image && calls == 3,
              "Removing a used image admits a previously deferred image and reuses blue");
        const auto replaced = asset(2, Qt::yellow);
        check(!TextureCache::find(*cache.snapshot(), replaced),
              "Replacement bytes cannot see the old image before worker completion");
        cache.request({{2, replaced}, {3, green}});
        settled(cache);
        check(cache.snapshot()->entries.at(2).image->rgba()[1] == 255 && calls == 4,
              "Same asset ID with new payload invalidates decoding");
        auto unsupported = std::make_shared<AssetRecord>(*replaced);
        unsupported->mediaType = "image/svg+xml";
        cache.request({{2, unsupported}});
        settled(cache);
        check(cache.snapshot()->entries.at(2).status == "unsupported" && calls == 5,
              "MIME replacement invalidates pixels and publishes explicit failure");
        cache.request({{2, unsupported}, {3, green}});
        settled(cache);
        check(calls == 6, "Failure is cached while a different image is decoded");
        cache.request({});
        settled(cache);
        check(cache.snapshot()->entries.empty() && cache.snapshot()->bytes == 0,
              "Empty view releases all cache entries");
        TextureCache countCache({1024, 1});
        countCache.request({{1, red}, {2, blue}});
        settled(countCache);
        check(countCache.snapshot()->entries.at(2).status == "budget",
              "Texture object count is bounded independently of byte budget");
        TextureCache smallCache({8, 1});
        smallCache.request({{1, red}});
        settled(smallCache);
        check(smallCache.snapshot()->bytes == 0 &&
                  smallCache.snapshot()->entries.at(1).status == "budget",
              "Decoded image larger than remaining budget is discarded");
        // Deliberately hold one codec so supersession/destruction are deterministic,
        // without timing assumptions or sleeping in the GUI thread.
        std::promise<void> started, release;
        auto gate = release.get_future().share();
        std::atomic<int> blockedCalls{};
        TextureCache latest({1024, 10}, [&](const AssetRecord &record) {
            if (++blockedCalls == 1) {
                started.set_value();
                gate.wait();
            }
            return decodeTextureImage(record);
        });
        latest.request({{1, red}, {2, blue}});
        check(started.get_future().wait_for(std::chrono::seconds(5)) == std::future_status::ready,
              "Controlled decoder started");
        latest.request({{3, green}});
        release.set_value();
        settled(latest);
        check(latest.snapshot()->entries.size() == 1 && latest.snapshot()->entries.contains(3) &&
                  blockedCalls == 2,
              "Superseded remainder is not decoded or published");
        std::promise<void> dyingStarted, dyingRelease;
        auto dyingGate = dyingRelease.get_future().share();
        auto dying =
            std::make_unique<TextureCache>(TextureCache::Limits{}, [&](const AssetRecord &record) {
                dyingStarted.set_value();
                dyingGate.wait();
                return decodeTextureImage(record);
            });
        dying->request({{1, red}});
        check(dyingStarted.get_future().wait_for(std::chrono::seconds(5)) ==
                  std::future_status::ready,
              "Destroyable decoder started");
        dying.reset(); // Must return while the decoder is still blocked.
        dyingRelease.set_value();
        check(QThreadPool::globalInstance()->waitForDone(5000),
              "Destroyed owner drops result safely");
        std::cout
            << "Texture cache budgets, reuse, invalidation, supersession and teardown passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
