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
// A PNG of width by height pixels, filled with color.
AssetPtr sized(Id id, int width, int height, QColor color) {
    QImage image(width, height, QImage::Format_RGBA8888);
    image.fill(color);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    check(image.save(&buffer, "PNG"), "PNG fixture encoded");
    return std::make_shared<AssetRecord>(AssetRecord{
        id, "Image", "image/png",
        std::make_shared<AssetPayload>(std::vector<std::uint8_t>(bytes.begin(), bytes.end()))});
}
AssetPtr asset(Id id, QColor color) { return sized(id, 2, 2, color); }
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
        check(cache.snapshot()->entries.at(2).status == "unsupported" && calls == 4,
              "MIME replacement is refused by its header and never decoded");
        cache.request({{2, unsupported}, {3, green}});
        settled(cache);
        check(calls == 5, "A refused image is not decoded again while a different image is decoded");
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
        // R118: every image within the budget keeps its stored size, and nothing is reduced.
        {
            TextureCache roomy({4096, 16, 2}, decodeTextureImage);
            roomy.request({{1, sized(1, 8, 8, Qt::red)}, {2, sized(2, 8, 8, Qt::blue)}});
            settled(roomy);
            const auto snapshot = roomy.snapshot();
            check(snapshot->bytes == 512 && snapshot->entries.at(1).image->width() == 8 &&
                      snapshot->entries.at(2).image->width() == 8 &&
                      !snapshot->entries.at(1).reduced && !snapshot->entries.at(2).reduced &&
                      snapshot->entries.at(2).status == "ready",
                  "Images within the budget keep their stored size");
        }
        // R118: the largest image is reduced first, so a smaller one keeps its size.
        {
            TextureCache largest({512, 16, 2}, decodeTextureImage);
            largest.request({{1, sized(1, 4, 4, Qt::red)}, {2, sized(2, 16, 16, Qt::blue)}});
            settled(largest);
            const auto snapshot = largest.snapshot();
            check(snapshot->bytes == 320 && snapshot->entries.at(1).image->width() == 4 &&
                      !snapshot->entries.at(1).reduced &&
                      snapshot->entries.at(2).image->width() == 8 &&
                      snapshot->entries.at(2).reduced,
                  "The largest image is halved first and the total then fits");
        }
        // R118: equal images are reduced in asset-ID order, and the result is the same every time.
        for (int round = 0; round < 2; ++round) {
            TextureCache ties({512, 16, 2}, decodeTextureImage);
            ties.request({{1, sized(1, 8, 8, Qt::red)},
                          {2, sized(2, 8, 8, Qt::green)},
                          {3, sized(3, 8, 8, Qt::blue)}});
            settled(ties);
            const auto snapshot = ties.snapshot();
            check(snapshot->bytes == 384 && snapshot->entries.at(1).image->width() == 4 &&
                      snapshot->entries.at(1).reduced && snapshot->entries.at(2).image->width() == 4 &&
                      snapshot->entries.at(2).reduced && snapshot->entries.at(3).image->width() == 8 &&
                      !snapshot->entries.at(3).reduced,
                  "Equal images are halved by ascending asset ID until the total fits");
        }
        // R118: at the floor, an image that still does not fit has no pixels and reports "budget".
        {
            TextureCache floorCache({128, 16, 4}, decodeTextureImage);
            floorCache.request({{1, sized(1, 8, 8, Qt::red)},
                                {2, sized(2, 8, 8, Qt::green)},
                                {3, sized(3, 8, 8, Qt::blue)}});
            settled(floorCache);
            const auto snapshot = floorCache.snapshot();
            check(snapshot->bytes == 128 && snapshot->entries.at(1).image->width() == 4 &&
                      snapshot->entries.at(2).image->width() == 4 &&
                      !snapshot->entries.at(3).image && snapshot->entries.at(3).status == "budget",
                  "Images at the floor that still do not fit get no pixels and report the budget");
        }
        // R118: an image that cannot be decoded is a placeholder with its decoder status, not a budget.
        {
            TextureCache broken;
            auto garbage = std::make_shared<AssetRecord>(
                AssetRecord{2, "Broken", "image/png",
                            std::make_shared<AssetPayload>(std::vector<std::uint8_t>{1, 2, 3, 4, 5, 6, 7, 8})});
            broken.request({{1, sized(1, 8, 8, Qt::red)}, {2, garbage}});
            settled(broken);
            const auto snapshot = broken.snapshot();
            check(snapshot->entries.at(1).image && snapshot->entries.at(1).status == "ready" &&
                      !snapshot->entries.at(2).image && snapshot->entries.at(2).status == "invalid",
                  "A decode failure keeps its status and does not become a budget refusal");
        }
        // R118: a changed request reuses reduced images that keep their size, and decodes only new ones.
        {
            std::atomic<int> reuseCalls{};
            TextureCache reuse({512, 16, 2}, [&](const AssetRecord &record) {
                ++reuseCalls;
                return decodeTextureImage(record);
            });
            // The same records in both requests: reuse compares payload identity, as the viewport's assets do.
            const auto one = sized(1, 8, 8, Qt::red), two = sized(2, 8, 8, Qt::green),
                       three = sized(3, 8, 8, Qt::blue), four = sized(4, 2, 2, Qt::yellow);
            reuse.request({{1, one}, {2, two}, {3, three}});
            settled(reuse);
            const auto first = reuseCalls.load();
            reuse.request({{1, one}, {2, two}, {3, three}, {4, four}});
            settled(reuse);
            check(first == 3 && reuseCalls == 4 && reuse.snapshot()->entries.at(4).image &&
                      reuse.snapshot()->entries.at(1).reduced,
                  "A changed request decodes only the image it adds, and keeps the reduced images");
        }
        // R118: the header probe reports the stored size and the refusals decode would give.
        {
            const auto probed = probeTextureImage(*sized(9, 8, 4, Qt::red));
            check(probed.status == TextureImageStatus::Ready && probed.width == 8 && probed.height == 4,
                  "Header probe reads the stored size without decoding");
            check(probeTextureImage(AssetRecord{10, "Icon", "image/svg+xml",
                                                std::make_shared<AssetPayload>(std::vector<std::uint8_t>{1})})
                          .status == TextureImageStatus::Unsupported,
                  "Header probe refuses an unsupported media type");
            check(probeTextureImage(AssetRecord{11, "Empty", "image/png", nullptr}).status ==
                      TextureImageStatus::Missing,
                  "Header probe reports a missing payload");
        }
        // R118: reduction averages the premultiplied colour, so a transparent pixel does not darken its neighbours.
        {
            TextureImage checker(2, 2, {255, 0, 0, 255, 0, 0, 0, 0, 255, 0, 0, 255, 0, 0, 0, 0});
            const auto half = reduceTextureImage(checker, 1, 1);
            check(half->rgba() == std::vector<std::uint8_t>({255, 0, 0, 128}) && half->hasTransparency(),
                  "Transparent pixels do not darken the reduced red");
            std::vector<std::uint8_t> uniform(4 * 4 * 4);
            for (size_t i = 0; i < uniform.size(); i += 4) {
                uniform[i] = 10;
                uniform[i + 1] = 20;
                uniform[i + 2] = 30;
                uniform[i + 3] = 255;
            }
            const auto flat = reduceTextureImage(TextureImage(4, 4, uniform), 3, 3);
            for (size_t i = 0; i < flat->rgba().size(); i += 4)
                check(flat->rgba()[i] == 10 && flat->rgba()[i + 1] == 20 && flat->rgba()[i + 2] == 30 &&
                          flat->rgba()[i + 3] == 255,
                      "A uniform image stays uniform when reduced to an uneven size");
            check(reduceTextureImage(TextureImage(2, 2, {255, 255, 255, 255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255, 255}), 1, 1)
                          ->rgba()[0] >= 127,
                  "Opaque black and white average to grey");
        }
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
            << "Texture cache budgets, reduction, reuse, invalidation, supersession and teardown passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
