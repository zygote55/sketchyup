#include "core/model.hpp"
#include <charconv>
#include <iostream>
#include <random>
#include <string>
using namespace sketchy;
namespace {
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
bool same(const Body &a, const Body &b) {
    return a.id == b.id && a.name == b.name && a.color == b.color && a.transform == b.transform &&
           a.parent == b.parent && a.properties == b.properties &&
           a.surface.vertices == b.surface.vertices && a.surface.faces == b.surface.faces &&
           a.surface.wires == b.surface.wires && a.topology.edges == b.topology.edges;
}
void invariant(const Document &doc) {
    for (const auto &[id, b] : doc.bodies()) {
        require(id == b->id && id < doc.nextId(), "Body allocator invariant");
        b->surface.validate();
        b->topology.validate(b->surface);
        const auto adjacency = b->topology.adjacency(b->surface);
        size_t loopUses = 0, radialUses = 0;
        for (const auto &[face, loops] : adjacency.faceLoops) {
            require(b->surface.faces.contains(face), "Face incidence identity");
            for (const auto &loop : loops)
                loopUses += loop.size();
        }
        for (const auto &[edge, uses] : adjacency.edgeFaces) {
            require(b->topology.edges.contains(edge), "Radial edge identity");
            radialUses += uses.size();
        }
        require(loopUses == radialUses, "Every oriented loop use has one radial incidence");
        for (auto t : doc.worldTriangles(id)) {
            checkPoint(t.a);
            checkPoint(t.b);
            checkPoint(t.c);
        }
    }
}
std::uint32_t parse(const char *text) {
    std::uint32_t value{};
    auto end = text + std::char_traits<char>::length(text);
    auto result = std::from_chars(text, end, value);
    if (result.ec != std::errc{} || result.ptr != end)
        throw std::runtime_error("Expected uint32 argument");
    return value;
}
} // namespace
int main(int argc, char **argv) {
    std::uint32_t first = 1, count = 48, steps = 24, seed = 0, step = 0;
    std::vector<std::string> trace;
    try {
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (i + 1 >= argc)
                throw std::runtime_error("Expected --seed N, --count N or --steps N");
            auto value = parse(argv[++i]);
            if (arg == "--seed") {
                first = value;
                count = 1;
            } else if (arg == "--count")
                count = value;
            else if (arg == "--steps")
                steps = value;
            else
                throw std::runtime_error("Unknown fuzz option");
        }
        if (count == 0 || count > 10000 || steps == 0 || steps > 10000 ||
            std::uint64_t(first) + count > UINT32_MAX)
            throw std::runtime_error("Fuzz run exceeds bounded seed/step range");
        size_t accepted = 0, rejected = 0, noops = 0;
        std::array<size_t, 8> successes{};
        for (seed = first; seed < first + count; ++seed) {
            trace.clear();
            std::mt19937 rng(seed);
            const double width = 1 + double(rng() % 100) / 10, depth = 1 + double(rng() % 100) / 10;
            Document doc;
            auto id = doc.addFace({{{0, 0, 0}, {width, 0, 0}, {width, depth, 0}, {0, depth, 0}}});
            doc.pushPull(id, doc.bodies().at(id)->surface.faces.begin()->first, 2);
            const auto neighbor = doc.addFace({{{100, 0, 0}, {101, 0, 0}, {100, 1, 0}}});
            const auto untouched = doc.bodies().at(neighbor);
            Id vertexFloor = doc.bodies().at(id)->surface.nextId,
               edgeFloor = doc.bodies().at(id)->topology.nextId;
            for (step = 0; step < steps; ++step) {
                const auto before = doc.bodies().at(id);
                const auto revision = doc.revision();
                const auto history = doc.historyBytes();
                auto operation = step == 0 ? 1u : rng() % 8;
                std::string description = std::to_string(operation);
                Id entity = 0;
                double amount = 0;
                if (operation == 0 || operation == 4) {
                    if (before->topology.edges.empty()) {
                        ++noops;
                        continue;
                    }
                    auto edge = before->topology.edges.begin();
                    std::advance(edge, rng() % before->topology.edges.size());
                    entity = edge->first;
                }
                if (operation == 1 || operation == 3) {
                    if (before->surface.faces.empty()) {
                        ++noops;
                        continue;
                    }
                    auto face = before->surface.faces.begin();
                    std::advance(face, rng() % before->surface.faces.size());
                    entity = face->first;
                }
                amount = (1 + double(rng() % 100)) / 100;
                description +=
                    " entity=" + std::to_string(entity) + " amount=" + std::to_string(amount);
                trace.push_back(description);
                bool failed = false;
                try {
                    switch (operation) {
                    case 0:
                        doc.splitEdge(id, entity, .2 + .6 * amount);
                        break;
                    case 1:
                        doc.pushPull(id, entity, amount);
                        break;
                    case 2:
                        doc.move(id, {amount, -amount, .1 * amount});
                        break;
                    case 3:
                        doc.eraseFace(id, entity);
                        break;
                    case 4:
                        doc.healFace(id, entity, {0, 0, 0}, {0, 0, 1});
                        break;
                    case 5:
                        doc.cleanup(id);
                        break;
                    case 6:
                        doc.insertEdges(id, {0, 0, 0}, {0, 0, 1},
                                        {{{{width * amount, 0, 0}, {width * amount, depth, 0}}}});
                        break;
                    case 7:
                        doc.pushPull(id, entity, NAN);
                        break;
                    }
                } catch (const std::exception &error) {
                    failed = true;
                    require(std::string(error.what()).size() < 4096,
                            "Geometry diagnostics must remain bounded");
                }
                if (failed) {
                    ++rejected;
                    require(doc.revision() == revision && doc.historyBytes() == history &&
                                doc.bodies().at(id) == before,
                            "Failed operation published state");
                } else if (doc.revision() == revision)
                    ++noops;
                else {
                    ++accepted;
                    ++successes[operation];
                    auto after = doc.bodies().at(id);
                    invariant(doc);
                    doc.undo();
                    require(same(*doc.bodies().at(id), *before),
                            "Undo failed exact topology/scene restoration");
                    invariant(doc);
                    doc.redo();
                    require(same(*doc.bodies().at(id), *after),
                            "Redo failed exact topology/scene restoration");
                    require(doc.revision() > revision, "Revision did not advance monotonically");
                }
                invariant(doc);
                require(doc.bodies().at(neighbor) == untouched,
                        "Local operation mutated neighboring body");
                auto current = doc.bodies().at(id);
                require(current->surface.nextId >= vertexFloor &&
                            current->topology.nextId >= edgeFloor,
                        "Allocator floor regressed");
                vertexFloor = current->surface.nextId;
                edgeFloor = current->topology.nextId;
            }
        }
        if (count >= 48 && steps >= 24)
            for (auto op : {0, 1, 2, 3, 4, 6})
                require(successes[op] > 0, "Corpus missed an operation class");
        std::cout << "{\"firstSeed\":" << first << ",\"seeds\":" << count << ",\"steps\":" << steps
                  << ",\"accepted\":" << accepted << ",\"rejected\":" << rejected
                  << ",\"noops\":" << noops << ",\"passed\":true}\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Geometry failure seed=" << seed << " step=" << step << ": " << error.what()
                  << "\nReplay: geometry_fuzz_tests --seed " << seed << " --steps " << step + 1
                  << '\n';
        for (size_t i = 0; i < trace.size(); ++i)
            std::cerr << i << ": " << trace[i] << '\n';
        return 1;
    }
}
