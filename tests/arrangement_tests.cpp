#include "geometry/arrangement.hpp"
#include <chrono>
#include <iostream>
using namespace sketchy;
void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
int main() {
    try {
        auto start = std::chrono::steady_clock::now();
        Surface source;
        auto face = source.addFace({{{0,0,0},{6,0,0},{6,4,0},{0,4,0}},
                                    {{.2,.2,0},{.2,3.8,0},{5.8,3.8,0},{5.8,.2,0}}});
        auto original = source;
        EdgeIdentityIndex edges;
        edges.reconcile(source);
        auto before = edges.records();
        edges.reconcile(source);
        check(edges.records() == before, "Unchanged edges keep identity");
        auto split = partitionFace(source, face, {3,0,0}, {1,0,0});
        check(source == original, "Partition leaves source immutable");
        check(split.surface.faces.size() == 2, "Wall ring partitions into two open outlines");
        check(split.descendants.at(face).size() == 2 && !split.surface.faces.contains(face),
              "Old face retired with two traceable descendants");
        double area = 0;
        for (auto id : split.descendants.at(face)) {
            check(id > face, "Split faces have new IDs");
            check(split.surface.faces.at(id).loops.size() == 1, "Cut opens hole into boundary");
            area += split.surface.area(id);
        }
        check(std::abs(area - 3.84) < 1e-8, "Wall area conserved");
        auto edgeMap = edges.reconcile(split.surface);
        std::cout << "face " << face << " ->";
        for (auto id : split.descendants.at(face)) std::cout << " " << id;
        std::cout << "\n";
        for (const auto &[id, children] : edgeMap) {
            std::cout << "edge " << id << " ->";
            for (auto child : children) std::cout << " " << child;
            std::cout << "\n";
        }
        int divided = 0;
        for (const auto &[old, children] : edgeMap)
            if (children.size() == 2) ++divided;
        check(divided == 4, "Four split boundary edges map to two children each");
        int survivors = 0;
        for (const auto &[key, id] : before)
            if (edges.records().contains(key)) {
                check(edges.records().at(key) == id, "Untouched edge survives split");
                ++survivors;
            }
        check(survivors == 4, "Four untouched wall ring edges retain identity");
        auto edgeFloor = edges.nextId();
        auto mergedMap = edges.reconcile(source);
        int merged = 0;
        for (const auto &[old, children] : mergedMap)
            if (children.size() == 1 && children[0] >= edgeFloor) ++merged;
        check(merged == 8, "Eight split edge children map into four merged edges");
        for (const auto &[key, id] : before)
            if (key[0] == 1 && key[1] == 2)
                check(edges.records().at(key) >= edgeFloor, "Recreated edge never reuses retired ID");
        auto outside = partitionFace(source, face, {10,0,0}, {1,0,0});
        check(outside.surface == source && outside.descendants.at(face) == std::vector<Id>{face},
              "Outside cut has no identity churn");
        auto tangent = partitionFace(source, face, {0,0,0}, {1,0,0});
        check(tangent.surface == source, "Tangent cut is no-op");
        auto hole = partitionFace(source, face, {.1,0,0}, {1,0,0});
        size_t holes = 0;
        for (const auto &[id, record] : hole.surface.faces) holes += record.loops.size() - 1;
        check(holes == 1, "Uncrossed hole remains attached to its containing face");
        Surface tilted;
        auto tiltedFace = tilted.addFace({{{0,0,0},{4,0,4},{4,3,4},{0,3,0}}});
        auto tiltedSplit = partitionFace(tilted, tiltedFace, {2,0,2}, {1,0,0});
        check(tiltedSplit.surface.faces.size() == 2, "Arbitrary-plane split");
        Surface site;
        auto siteFace = site.addFace({{{999990,999990,100},{999996,999990,100},
                                      {999996,999994,100},{999990,999994,100}}});
        auto siteSplit = partitionFace(site, siteFace, {999993,999990,100}, {1,0,0});
        check(siteSplit.surface.faces.size() == 2, "Site-coordinate split");
        bool rejected = false;
        try { (void)partitionFace(source, face, {0,0,0}, {1e-10,0,1}); }
        catch (const std::exception &) { rejected = true; }
        check(rejected && source == original, "Near-coplanar cut rejected without mutation");
        for (int i = 0; i < 100; ++i) {
            auto cut = partitionFace(source, face, {.21 + i*.055,0,0}, {1,0,0});
            check(cut.surface.faces.size() == 2, "Deterministic wall partition corpus");
        }
        const auto ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        std::cout << "Planar arrangement corpus passed: wall ring, holes, tangent, tilted, site, "
                     "near-coplanar rejection, face lineage, stable/retired edge IDs; elapsedMs="
                  << ms << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
