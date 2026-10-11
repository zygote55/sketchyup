// Opt-in R082.ff construction-scaling harness; timings are not a CTest pass gate.
// It uses only public API present before and after the incremental validator, so
// the same source measures both. The scaled target links a private core copy built
// with SKETCHYUP_BENCHMARK_LIMIT_SCALE; production limits are unchanged.
#include "core/components.hpp"
#include "core/document_limits.hpp"
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <numbers>
#include <string>
#include <vector>
using namespace sketchy;
namespace {
struct Digest {
    std::uint64_t value = 1469598103934665603ull;
    void add(const void *data, size_t size) {
        const auto *bytes = static_cast<const unsigned char *>(data);
        for (size_t i = 0; i < size; ++i)
            value = (value ^ bytes[i]) * 1099511628211ull;
    }
    template <class T> void add(const T &item) { add(&item, sizeof(item)); }
};
std::uint64_t digest(const Document &doc) {
    Digest result;
    for (const auto &[id, body] : doc.bodies()) {
        result.add(id);
        result.add(body->parent);
        result.add(int(body->kind));
        const auto probe = body->transform.point({1, 2, 3});
        result.add(probe);
        result.add(body->surface.nextId);
        result.add(body->topology.nextId);
        for (const auto &[vertex, point] : body->surface.vertices) {
            result.add(vertex);
            result.add(point);
        }
        for (const auto &[face, record] : body->surface.faces)
            for (const auto &loop : record.loops)
                for (auto vertex : loop)
                    result.add(vertex);
        for (const auto &[edge, record] : body->topology.edges) {
            result.add(edge);
            result.add(record.a);
            result.add(record.b);
        }
    }
    for (const auto &[root, instance] : doc.instances()) {
        result.add(root);
        result.add(instance->definition);
        for (const auto &[member, target] : instance->members) {
            result.add(member);
            result.add(target);
        }
    }
    result.add(doc.nextId());
    result.add(doc.revision());
    return result.value;
}
} // namespace
int main(int argc, char **argv) {
    try {
        const int count = argc == 2 ? std::atoi(argv[1]) : 1000;
        if (argc > 2 || count < 1 || count > 10000)
            throw std::runtime_error("Usage: component_placement_scaling [1..10000]");
        const auto start = std::chrono::steady_clock::now();
        auto elapsed = [&] {
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                             start)
                .count();
        };
        Document doc;
        std::vector<Vec3> loop;
        for (int i = 0; i < 26; ++i) {
            const auto angle = 2 * std::numbers::pi * i / 26;
            loop.push_back({std::cos(angle), std::sin(angle), 0});
        }
        const auto body = doc.addFace({loop});
        doc.extrude(body, doc.bodies().at(body)->surface.faces.begin()->first, 1);
        const auto component = createComponent(doc, body, "26-sided benchmark prism");
        const int columns = int(std::ceil(std::sqrt(count)));
        std::string checkpoints;
        for (int i = 1; i < count; ++i) {
            placeComponent(doc, component.definition,
                           Transform::translation({4.0 * (i % columns), 4.0 * (i / columns), 0}));
            const int placed = i + 1;
            if (placed % 1000 == 0 || placed == count)
                checkpoints += (checkpoints.empty() ? "" : ", ") + std::string("{\"instances\": ") +
                               std::to_string(placed) +
                               ", \"cumulativeMs\": " + std::to_string(elapsed()) + "}";
        }
        const double constructionMs = elapsed();
        size_t vertices = 0;
        for (const auto &[id, record] : doc.bodies())
            vertices += record->surface.vertices.size();
        if (doc.instances().size() != size_t(count) || doc.bodies().size() != size_t(count) * 2)
            throw std::runtime_error("Unexpected benchmark fixture");
        std::cout << "{\n  \"instances\": " << count << ",\n  \"bodies\": " << doc.bodies().size()
                  << ",\n  \"vertices\": " << vertices
                  << ",\n  \"limitScale\": " << DocumentLimits::scale
                  << ",\n  \"constructionMs\": " << constructionMs << ",\n  \"checkpoints\": ["
                  << checkpoints << "],\n  \"documentDigest\": \"" << std::hex << digest(doc)
                  << std::dec
                  << "\",\n  \"timingScope\": \"Public face/extrude/component creation and "
                     "placement only\",\n  \"releaseAcceptance\": false\n}\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
