#include "core/transform.hpp"
#include "geometry/hosted_opening.hpp"
#include "geometry/solid.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
using namespace sketchy;
namespace {
void check(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}
void near(double value, double expected, const char *message, double epsilon = 1e-7) {
    check(std::abs(value - expected) < epsilon, std::string(message) + ": expected " +
                                                    std::to_string(expected) + ", got " +
                                                    std::to_string(value));
}
template <class F> void rejects(const char *code, F operation) {
    try {
        operation();
    } catch (const OpeningError &error) {
        check(error.code() == code, std::string("Expected ") + code + ": " + error.what());
        return;
    }
    throw std::runtime_error(std::string("Expected ") + code);
}
Surface box(Vec3 origin, Vec3 size) {
    Surface surface;
    const auto face =
        surface.addFace({{origin, origin + Vec3{size.x, 0, 0}, origin + Vec3{size.x, size.y, 0},
                          origin + Vec3{0, size.y, 0}}});
    surface.extrude(face, size.z);
    return surface;
}
void append(Surface &target, const Surface &source, bool reverse = false) {
    for (const auto &[id, face] : source.faces) {
        std::vector<std::vector<Vec3>> loops;
        for (const auto &loop : face.loops) {
            loops.emplace_back();
            for (auto vertex : loop)
                loops.back().push_back(source.vertices.at(vertex));
            if (reverse)
                std::reverse(loops.back().begin(), loops.back().end());
        }
        target.addFace(loops);
    }
}
Id facing(const Surface &surface, Vec3 normal, double coordinate) {
    for (const auto &[id, face] : surface.faces)
        if (dot(surface.normal(id), normal) > .999 &&
            std::abs(dot(surface.vertices.at(face.loops.front().front()), normal) - coordinate) <
                tolerance)
            return id;
    throw std::runtime_error("Expected fixture face");
}
double volume(const Surface &surface) {
    const auto result = analyzeSolidShells(surface, Topology::rebuild(surface, {}));
    check(result.report.status == "validated_shells", result.report.status);
    return *result.report.volume;
}
double nativeVolume(const Surface &surface) {
    const auto origin = surface.vertices.begin()->second;
    long double result = 0;
    for (const auto &[id, face] : surface.faces)
        for (const auto &loop : face.loops) {
            const auto a = surface.vertices.at(loop.front()) - origin;
            for (size_t i = 1; i + 1 < loop.size(); ++i)
                result += dot(a, cross(surface.vertices.at(loop[i]) - origin,
                                       surface.vertices.at(loop[i + 1]) - origin)) /
                          6;
        }
    return double(result);
}
std::vector<Vec3> rectangle(double x, double y, double z, double width = 2, double height = 2) {
    return {{x, y, z}, {x + width, y, z}, {x + width, y + height, z}, {x, y + height, z}};
}
void openings() {
    const auto host = box({}, {10, 10, 1}), before = host;
    const auto entry = facing(host, {0, 0, 1}, 1), exit = facing(host, {0, 0, -1}, 0);
    const auto opening = cutHostedOpening(host, entry, rectangle(1, 1, 1));
    const auto &after = opening.edit.surface;
    check(host == before, "Host input stays immutable");
    check(opening.entry == entry && opening.exit == exit && opening.jambs.size() == 4,
          "Entry/exit identities and all generated jambs are explicit");
    near(opening.depth, 1, "Wall thickness");
    near(opening.removedVolume, 4, "Analytical removed volume");
    near(volume(after), 96, "Analytical remaining volume");
    for (const auto &[id, face] : host.faces) {
        check(after.faces.contains(id), "All existing face identities remain");
        if (id != entry && id != exit)
            check(after.faces.at(id) == face, "Unrelated native face records stay identical");
    }
    for (const auto &[id, point] : host.vertices)
        check(after.vertices.at(id) == point, "All existing vertices stay identical");
    const auto topology = Topology::rebuild(host, {});
    const auto updated = Topology::rebuild(after, topology);
    for (const auto &[id, edge] : topology.edges)
        check(updated.edges.at(id).a == edge.a && updated.edges.at(id).b == edge.b,
              "Unchanged native edges retain identities");
    check(opening.edit.faces.at(entry).size() == 5 &&
              opening.edit.faces.at(exit) == std::vector<Id>{exit},
          "Host descendants include reveals for explicit appearance transfer");
    const auto second = cutHostedOpening(after, entry, rectangle(5, 5, 1));
    near(volume(second.edit.surface), 92, "Independent second opening retains first opening");
    check(second.edit.surface.faces.at(entry).loops.size() == 3 &&
              second.edit.surface.faces.at(exit).loops.size() == 3,
          "Both host faces accumulate exactly two openings");
    auto reversed = rectangle(1, 1, 1);
    std::reverse(reversed.begin(), reversed.end());
    near(volume(cutHostedOpening(host, entry, reversed).edit.surface), 96,
         "Cut profile winding is independent of component mirroring");
    rejects("OPENING_PROFILE", [&] { cutHostedOpening(after, entry, rectangle(2, 2, 1)); });
    rejects("OPENING_PROFILE", [&] { cutHostedOpening(after, entry, rectangle(.5, .5, 1, 3, 3)); });
    rejects("OPENING_PROFILE", [&] { cutHostedOpening(host, entry, rectangle(0, 2, 1)); });
    rejects("OPENING_PROFILE",
            [&] { cutHostedOpening(host, entry, rectangle(2 * tolerance, 2, 1)); });
    rejects("OPENING_PROFILE", [&] { cutHostedOpening(host, entry, rectangle(9, 2, 1)); });
    rejects("OPENING_PROFILE", [&] { cutHostedOpening(host, entry, rectangle(1, 1, .9)); });
    auto snapped = rectangle(1, 1, 1 + tolerance * .5);
    near(volume(cutHostedOpening(host, entry, snapped).edit.surface), 96,
         "Within-tolerance profile snaps to entry plane");
}
void roomAndObstructions() {
    auto room = box({}, {10, 10, 10});
    append(room, box({.5, .5, .5}, {9, 9, 9}), true);
    const auto entry = facing(room, {0, -1, 0}, 0);
    const auto exit = facing(room, {0, 1, 0}, .5);
    const auto farWall = facing(room, {0, -1, 0}, -9.5);
    const std::vector<Vec3> profile{{2, 0, 2}, {4, 0, 2}, {4, 0, 4}, {2, 0, 4}};
    const auto opening = cutHostedOpening(room, entry, profile);
    check(opening.exit == exit && opening.edit.surface.faces.at(farWall) == room.faces.at(farWall),
          "Room opening stops at first wall exit and leaves opposite wall untouched");
    near(opening.depth, .5, "First material thickness, not complete room bounds");
    near(volume(opening.edit.surface), 269, "Room 1000 - 729 - 2 material oracle");

    auto obstacle = box({}, {10, 10, 10});
    append(obstacle, box({1.5, 1.5, 3}, {1, 1, 2}), true);
    const auto top = facing(obstacle, {0, 0, 1}, 10);
    const auto before = obstacle;
    rejects("OPENING_OBSTRUCTED",
            [&] { cutHostedOpening(obstacle, top, rectangle(1, 1, 10, 3, 3)); });
    check(obstacle == before, "A fully enclosed intervening cavity is not silently bridged");

    auto sloped = box({}, {10, 10, 3});
    for (auto &[id, point] : sloped.vertices)
        if (point.z == 0)
            point.z = point.x * .1;
    rejects("OPENING_EXIT",
            [&] { cutHostedOpening(sloped, facing(sloped, {0, 0, 1}, 3), rectangle(1, 1, 3)); });
}
void concavityAndTransforms() {
    Surface host;
    auto base = host.addFace(
        {{{0, 0, 0}, {6, 0, 0}, {6, 6, 0}, {4, 6, 0}, {4, 2, 0}, {2, 2, 0}, {2, 6, 0}, {0, 6, 0}}});
    host.extrude(base, 1);
    const auto top = facing(host, {0, 0, 1}, 1);
    // All corners lie in material, but edges bridge the U-shaped void.
    rejects("OPENING_PROFILE", [&] { cutHostedOpening(host, top, rectangle(1, 3, 1, 4, 2)); });
    const std::vector<Vec3> concave{{.3, .3, 1}, {1.7, .3, 1}, {1.7, 1.7, 1},
                                    {1, 1.7, 1}, {1, 1, 1},    {.3, 1, 1}};
    const auto cut = cutHostedOpening(host, top, concave);
    near(cut.removedVolume, 1.47, "Concave simple profile area times thickness");
    near(volume(cut.edit.surface), 28 - 1.47, "Concave host remaining material");

    const auto original = box({}, {10, 10, 1});
    const auto entry = facing(original, {0, 0, 1}, 1);
    for (double sign : {-1., 1.}) {
        auto transformed = original;
        const auto frame = Transform::translation({50000, -70000, 90000}) *
                           Transform::rotation({1, 2, 3}, .7) *
                           Transform::scaling({sign * 2, 3, .5});
        for (auto &[id, point] : transformed.vertices)
            point = frame.point(point);
        if (sign < 0)
            for (auto &[id, face] : transformed.faces)
                for (auto &loop : face.loops)
                    std::reverse(loop.begin(), loop.end());
        auto profile = rectangle(1, 1, 1);
        for (auto &point : profile)
            point = frame.point(point);
        const auto result = cutHostedOpening(transformed, entry, profile);
        near(result.depth, .5, "Transformed wall thickness", 1e-6);
        near(result.removedVolume, 12, "Reflected/nonuniform cut volume", 1e-6);
        near(volume(result.edit.surface), 288, "Transformed remaining volume", 1e-5);
    }
    for (double size : {1e-3, .1, 100.}) {
        auto scaled = original;
        for (auto &[id, point] : scaled.vertices)
            point = point * size;
        auto profile = rectangle(1, 1, 1);
        for (auto &point : profile)
            point = point * size;
        const auto result = cutHostedOpening(scaled, entry, profile);
        near(result.removedVolume, 4 * size * size * size, "Scale-relative opening volume",
             4 * size * size * size * 1e-6);
        near(volume(result.edit.surface), 96 * size * size * size,
             "Small/large feature material volume", 96 * size * size * size * 1e-6);
    }
    auto sheared = original;
    Transform shear;
    shear.m[8] = .4;
    shear.m[4] = .2;
    for (auto &[id, point] : sheared.vertices)
        point = shear.point(point);
    auto profile = rectangle(2, 2, 1);
    for (auto &point : profile)
        point = shear.point(point);
    const auto result = cutHostedOpening(sheared, entry, profile);
    near(nativeVolume(result.edit.surface), 96, "Exact native sheared host material", 1e-9);
    // The solid analyzer uses face-local triangulation quantized to 1e-7.
    near(volume(result.edit.surface), 96, "Sheared triangulated material volume", 1e-6);
}
void failures() {
    auto host = box({}, {10, 10, 1});
    const auto top = facing(host, {0, 0, 1}, 1);
    rejects("OPENING_HOST", [&] { cutHostedOpening(host, 999, rectangle(1, 1, 1)); });
    rejects("OPENING_PROFILE", [&] { cutHostedOpening(host, top, std::vector<Vec3>(257)); });
    rejects("OPENING_PROFILE",
            [&] { cutHostedOpening(host, top, {{1, 1, 1}, {3, 3, 1}, {1, 3, 1}, {3, 1, 1}}); });
    const auto closed = host;
    host.faces.erase(host.faces.begin());
    rejects("OPENING_HOST", [&] { cutHostedOpening(host, top, rectangle(1, 1, 1)); });
    host = closed;
    for (auto &[id, face] : host.faces)
        for (auto &loop : face.loops)
            std::reverse(loop.begin(), loop.end());
    rejects("OPENING_HOST", [&] { cutHostedOpening(host, top, rectangle(1, 1, 1)); });
    host = closed;
    host.faces.at(top).loops[0] = std::vector<Id>(4097, 1);
    rejects("OPENING_LIMIT", [&] { cutHostedOpening(host, top, rectangle(1, 1, 1)); });
}
} // namespace
int main() {
    try {
        openings();
        roomAndObstructions();
        concavityAndTransforms();
        failures();
        std::cout << "Bounded hosted openings, first wall exit, native identities, multiple holes "
                     "and transformed material volumes passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
