// R082.ee software measurement: the pre-R082.ee expanded inference index versus
// the shared definition-local index on the real_model_benchmark fixture families.
// Headless and synchronous: no viewport, GL or worker; not hardware evidence.
#include "core/components.hpp"
#include "core/groups.hpp"
#include "legacy_inference_index.hpp"
#include <chrono>
#include <cmath>
#include <iostream>
#include <malloc.h>
#include <numbers>
using namespace sketchy;
namespace {
using Clock = std::chrono::steady_clock;
double ms(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
double p95(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values[std::min(values.size() - 1, size_t(std::ceil(values.size() * .95)) - 1)];
}
size_t allocated() { return mallinfo2().uordblks; }
using Matrix = std::array<double, 16>;
Matrix multiply(const Matrix &a, const Matrix &b) {
    Matrix r{};
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 4; ++k)
                r[c * 4 + row] += a[k * 4 + row] * b[c * 4 + k];
    return r;
}
// Fixed oblique perspective framing the fixture, 1920x1080 logical pixels.
InferenceCamera frame(Vec3 low, Vec3 high) {
    const auto center = (low + high) * .5, extent = high - low;
    const auto radius = length(extent) * .5;
    const auto eye = center + normalized({-.6, -1, .9}) * radius * 1.6;
    const auto f = normalized(center - eye), s = normalized(cross(f, {0, 0, 1})),
               u = cross(s, f);
    const Matrix view{s.x, u.x, -f.x, 0, s.y, u.y, -f.y, 0, s.z, u.z, -f.z, 0,
                      -dot(s, eye), -dot(u, eye), dot(f, eye), 1};
    const double near = radius * .02, far = radius * 4, t = 1 / std::tan(.4),
                 aspect = 1920. / 1080;
    const Matrix projection{t / aspect, 0, 0, 0, 0, t, 0, 0, 0, 0, -(far + near) / (far - near),
                            -1, 0, 0, -2 * far * near / (far - near), 0};
    InferenceCamera camera;
    camera.clipFromWorld = multiply(projection, view);
    // Analytic inverse of this view/projection pair.
    const Matrix inverseProjection{aspect / t, 0, 0, 0, 0, 1 / t, 0, 0, 0, 0, 0,
                                   -(far - near) / (2 * far * near), 0, 0, -1,
                                   (far + near) / (2 * far * near)};
    const Matrix inverseView{s.x, s.y, s.z, 0, u.x, u.y, u.z, 0, -f.x, -f.y, -f.z, 0,
                             eye.x, eye.y, eye.z, 1};
    camera.worldFromClip = multiply(inverseView, inverseProjection);
    camera.width = 1920;
    camera.height = 1080;
    return camera;
}
} // namespace
int main(int argc, char **argv) {
    const int count = argc > 1 ? std::atoi(argv[1]) : 1000;
    const std::string scenario = argc > 2 ? argv[2] : "repeated";
    Document doc;
    // Fixture geometry and placement as in tests/real_model_benchmark.cpp.
    auto prism = [&](double radius) {
        std::vector<Vec3> loop;
        for (int i = 0; i < 26; ++i) {
            const auto angle = 2 * std::numbers::pi * i / 26;
            loop.push_back({radius * std::cos(angle), radius * std::sin(angle), 0});
        }
        const auto body = doc.addFace({loop});
        doc.extrude(body, doc.bodies().at(body)->surface.faces.begin()->first, 1);
        return body;
    };
    const int columns = int(std::ceil(std::sqrt(count)));
    std::vector<Id> instances;
    if (scenario == "unique") {
        for (int i = 0; i < count; ++i) {
            const auto body = prism(1 + .2 * i / count);
            doc.transform(body,
                          Transform::translation({4.0 * (i % columns), 4.0 * (i / columns), 0}));
            instances.push_back(body);
        }
    } else {
        const auto component = createComponent(doc, prism(1), "26-sided benchmark prism");
        instances.push_back(component.instance);
        for (int i = 1; i < count; ++i)
            instances.push_back(placeComponent(doc, component.definition,
                                               Transform::translation({4.0 * (i % columns),
                                                                       4.0 * (i / columns), 0}))
                                    .instance);
    }
    const int outerGroups = scenario == "deep" ? 32 : scenario == "far" ? 1 : 0;
    const Vec3 offset = scenario == "far" ? Vec3{900000, -900000, 900000} : Vec3{};
    if (outerGroups) {
        auto group = createGroup(doc, std::set<Id>(instances.begin(), instances.end()), "Root");
        for (int i = 1; i < outerGroups; ++i)
            group = createGroup(doc, {group}, "Nesting " + std::to_string(i));
        if (scenario == "far")
            doc.transform(group, Transform::translation(offset));
    }
    Vec3 low{1e300, 1e300, 1e300}, high{-1e300, -1e300, -1e300};
    for (const auto id : instances) {
        const auto p = doc.worldTransform(id).point({});
        low = {std::min(low.x, p.x - 1), std::min(low.y, p.y - 1), std::min(low.z, p.z)};
        high = {std::max(high.x, p.x + 1), std::max(high.y, p.y + 1), std::max(high.z, p.z + 1)};
    }
    const auto camera = frame(low, high);
    auto measure = [&](auto &index, const char *name) {
        malloc_trim(0);
        const auto before = allocated();
        auto start = Clock::now();
        index.sync(doc);
        const auto buildMs = ms(start);
        const auto heldBytes = allocated() - before;
        std::vector<double> queries;
        size_t visited = 0, candidates = 0;
        for (int round = 0; round < 11; ++round)
            for (int i = 0; i < 50; ++i) {
                const auto placement = instances[size_t(i) * (count - 1) / 49];
                const auto screen = camera.project(doc.worldTransform(placement).point({0, 0, 1}));
                if (!screen)
                    continue;
                const InferenceQuery q{camera, screen->x, screen->y, 8};
                start = Clock::now();
                const auto result = index.query(q);
                if (round)
                    queries.push_back(ms(start));
                visited = std::max(visited, result.visitedPrimitives);
                candidates += result.candidates.size();
            }
        std::vector<double> updates;
        const auto editing = instances.front();
        const auto base = doc.bodies().at(editing)->transform;
        for (int i = 0; i < 100; ++i) {
            doc.transform(editing, Transform::translation({0, 0, .01 * (i + 1)}) * base);
            start = Clock::now();
            index.sync(doc);
            updates.push_back(ms(start));
        }
        for (int i = 0; i < 100; ++i)
            doc.undo();
        index.sync(doc);
        std::cout << "{\"scenario\":\"" << scenario << "\",\"placements\":" << count
                  << ",\"index\":\"" << name << "\",\"bodies\":" << doc.bodies().size()
                  << ",\"primitives\":" << index.primitiveCount() << ",\"storedPrimitives\":";
        if constexpr (requires { index.indexedPrimitiveCount(); })
            std::cout << index.indexedPrimitiveCount() << ",\"localBuilds\":" << index.localBuilds();
        else
            std::cout << index.primitiveCount();
        std::cout << ",\"indexBytesEstimate\":" << index.indexBytes()
                  << ",\"mallocHeldBytes\":" << heldBytes << ",\"buildMs\":" << buildMs
                  << ",\"queryP95Ms\":" << p95(queries) << ",\"queryMeanMs\":"
                  << std::accumulate(queries.begin(), queries.end(), 0.) / queries.size()
                  << ",\"updateP95Ms\":" << p95(updates) << ",\"visitedMax\":" << visited
                  << ",\"candidates\":" << candidates << "}\n";
    };
    {
        legacy::InferenceIndex old;
        measure(old, "expanded-before");
    }
    {
        InferenceIndex shared;
        measure(shared, "shared-after");
    }
}
