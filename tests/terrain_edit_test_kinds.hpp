#pragma once
// R096.a test-only terrain edit kinds (ADR 0169 "Implementation split", R096.a: "a test-only
// operation, so the one-tile budget is measurable first"). Production registers no kind in
// R096.a; tests, terrain_edit_benchmark and the edit-list fuzz target register these two:
//
// - "test.lift" {tile: [row, column, path, part], amount}: raises every vertex of one tile that
//   no other tile shares by `amount` (Z only). It touches exactly that tile. Skipped when the
//   tile no longer exists.
// - "test.split" {point: [x, y]}: splits the triangle under a plan point into three at that
//   point, which takes the triangle's plane height (a topology change; the new triangles land
//   in the cells of their centroids). Skipped when no triangle is under the point.
//
// - "test.refine" {point: [x, y], rounds}: replaces the triangle under a plan point by
//   3^rounds coplanar triangles (each round splits every triangle at its centroid), so one edit
//   can push a cell over 2,048 triangles. Skipped when no triangle is under the point.
//
// All are pure functions of the mesh's geometry and their parameters, as every kind must be.
#include "core/terrain_edit.hpp"
#include "io/terrain_io.hpp"
#include <QJsonArray>
#include <cmath>
#include <optional>
#include <stdexcept>
namespace terrain_test_kinds {
using namespace sketchy;
struct Lift final : TerrainEditData {
    TerrainTileKey tile;
    double amount{};
    bool equals(const TerrainEditData &other) const override {
        const auto *o = dynamic_cast<const Lift *>(&other);
        return o && o->tile == tile && o->amount == amount;
    }
    std::size_t bytes() const override { return sizeof(Lift) + tile.path.size(); }
};
struct Split final : TerrainEditData {
    double x{}, y{};
    bool equals(const TerrainEditData &other) const override {
        const auto *o = dynamic_cast<const Split *>(&other);
        return o && o->x == x && o->y == y;
    }
    std::size_t bytes() const override { return sizeof(Split); }
};
struct Refine final : TerrainEditData {
    double x{}, y{};
    int rounds{};
    bool equals(const TerrainEditData &other) const override {
        const auto *o = dynamic_cast<const Refine *>(&other);
        return o && o->x == x && o->y == y && o->rounds == rounds;
    }
    std::size_t bytes() const override { return sizeof(Refine); }
};
inline std::shared_ptr<const TerrainEditData> refine(double x, double y, int rounds) {
    auto data = std::make_shared<Refine>();
    data->x = x;
    data->y = y;
    data->rounds = rounds;
    return data;
}
inline std::shared_ptr<const TerrainEditData> lift(TerrainTileKey tile, double amount) {
    auto data = std::make_shared<Lift>();
    data->tile = std::move(tile);
    data->amount = amount;
    return data;
}
inline std::shared_ptr<const TerrainEditData> split(double x, double y) {
    auto data = std::make_shared<Split>();
    data->x = x;
    data->y = y;
    return data;
}
inline TerrainEditOutcome applyLift(const TerrainEditMesh &mesh, const TerrainEdit &edit) {
    const auto &data = dynamic_cast<const Lift &>(*edit.data);
    const auto *tile = mesh.tile(data.tile);
    if (!tile)
        return {std::nullopt, "test.lift: the tile no longer exists"};
    TerrainRegionChange change;
    std::vector<Vec3> seen;
    for (const auto &p : tile->content->vertices) {
        const auto triangles = mesh.trianglesAt(p);
        bool interior = true;
        for (const auto &ref : triangles)
            interior = interior && ref.tile == data.tile;
        if (interior)
            change.lifted.push_back({p, p.z + data.amount});
    }
    return {std::move(change), {}};
}
// The triangle strictly under (x, y), as (key, index, corners), or nothing.
struct Under {
    TerrainTileKey key;
    std::uint32_t index{};
    Vec3 a, b, c;
    double u{}, v{}, w{};
};
inline std::optional<Under> triangleUnder(const TerrainEditMesh &mesh, double x, double y) {
    for (const auto *tile : mesh.tilesAt(x, y, 0)) {
        const auto &content = *tile->content;
        for (std::uint32_t t = 0; t < content.triangles.size(); ++t) {
            const auto a = content.vertices[content.triangles[t][0]],
                       b = content.vertices[content.triangles[t][1]],
                       c = content.vertices[content.triangles[t][2]];
            const double area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
            if (!(area > 0))
                continue;
            const double u = ((b.x - x) * (c.y - y) - (c.x - x) * (b.y - y)) / area,
                         v = ((c.x - x) * (a.y - y) - (a.x - x) * (c.y - y)) / area,
                         w = 1 - u - v;
            if (u < 1e-3 || v < 1e-3 || w < 1e-3)
                continue;
            return Under{content.key, t, a, b, c, u, v, w};
        }
    }
    return std::nullopt;
}
inline TerrainEditOutcome applyRefine(const TerrainEditMesh &mesh, const TerrainEdit &edit) {
    const auto &data = dynamic_cast<const Refine &>(*edit.data);
    const auto under = triangleUnder(mesh, data.x, data.y);
    if (!under)
        return {std::nullopt, "test.refine: no triangle under the point"};
    std::vector<std::array<Vec3, 3>> triangles{{under->a, under->b, under->c}};
    for (int round = 0; round < data.rounds; ++round) {
        std::vector<std::array<Vec3, 3>> next;
        for (const auto &[a, b, c] : triangles) {
            const Vec3 m{(a.x + b.x + c.x) / 3, (a.y + b.y + c.y) / 3, (a.z + b.z + c.z) / 3};
            next.push_back({a, b, m});
            next.push_back({b, c, m});
            next.push_back({c, a, m});
        }
        triangles = std::move(next);
    }
    TerrainRegionChange change;
    change.removed.push_back({under->key, under->index});
    change.added = std::move(triangles);
    return {std::move(change), {}};
}
inline TerrainEditOutcome applySplit(const TerrainEditMesh &mesh, const TerrainEdit &edit) {
    const auto &data = dynamic_cast<const Split &>(*edit.data);
    for (const auto *tile : mesh.tilesAt(data.x, data.y, 0)) {
        const auto &content = *tile->content;
        for (std::uint32_t t = 0; t < content.triangles.size(); ++t) {
            const auto a = content.vertices[content.triangles[t][0]],
                       b = content.vertices[content.triangles[t][1]],
                       c = content.vertices[content.triangles[t][2]];
            const double area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
            if (!(area > 0))
                continue;
            const double u = ((b.x - data.x) * (c.y - data.y) - (c.x - data.x) * (b.y - data.y)) / area,
                         v = ((c.x - data.x) * (a.y - data.y) - (a.x - data.x) * (c.y - data.y)) / area,
                         w = 1 - u - v;
            // Strictly inside, away from the edges, so the three new triangles are proper.
            if (u < 1e-3 || v < 1e-3 || w < 1e-3)
                continue;
            const Vec3 p{data.x, data.y, u * a.z + v * b.z + w * c.z};
            TerrainRegionChange change;
            change.removed.push_back({content.key, t});
            change.added = {{a, b, p}, {b, c, p}, {c, a, p}};
            return {std::move(change), {}};
        }
    }
    return {std::nullopt, "test.split: no triangle under the point"};
}
inline QJsonArray keyJson(const TerrainTileKey &key) {
    return {double(key.row), double(key.column), QString::fromStdString(key.path), double(key.part)};
}
inline void registerKinds() {
    registerTerrainEditKind({"test.lift", applyLift, [](const TerrainEdit &) {
                                 return std::array<double, 4>{0, 0, 0, 0};
                             }});
    registerTerrainEditKind({"test.split", applySplit, [](const TerrainEdit &edit) {
                                 const auto &d = dynamic_cast<const Split &>(*edit.data);
                                 return std::array<double, 4>{d.x, d.y, d.x, d.y};
                             }});
    registerTerrainEditCodec(
        {"test.lift",
         [](const TerrainEditData &data) {
             const auto &lift = dynamic_cast<const Lift &>(data);
             return QJsonObject{{"tile", keyJson(lift.tile)}, {"amount", lift.amount}};
         },
         [](const QJsonObject &object) -> std::shared_ptr<const TerrainEditData> {
             if (object.size() != 2 || !object["tile"].isArray() || !object["amount"].isDouble())
                 throw std::runtime_error("Invalid test.lift entry");
             const auto key = decodeTerrainTileMarker(QJsonObject{{"key", object["tile"]}});
             const auto amount = object["amount"].toDouble();
             if (!std::isfinite(amount) || std::abs(amount) > 1000)
                 throw std::runtime_error("Invalid test.lift amount");
             return lift(key, amount);
         }});
    registerTerrainEditKind({"test.refine", applyRefine, [](const TerrainEdit &edit) {
                                 const auto &d = dynamic_cast<const Refine &>(*edit.data);
                                 return std::array<double, 4>{d.x, d.y, d.x, d.y};
                             }});
    registerTerrainEditCodec(
        {"test.refine",
         [](const TerrainEditData &data) {
             const auto &r = dynamic_cast<const Refine &>(data);
             return QJsonObject{{"point", QJsonArray{r.x, r.y}}, {"rounds", r.rounds}};
         },
         [](const QJsonObject &object) -> std::shared_ptr<const TerrainEditData> {
             if (object.size() != 2 || !object["point"].isArray() ||
                 object["point"].toArray().size() != 2 || !object["rounds"].isDouble())
                 throw std::runtime_error("Invalid test.refine entry");
             const auto point = object["point"].toArray();
             const auto rounds = object["rounds"].toDouble();
             for (const auto &v : point)
                 if (!v.isDouble() || !std::isfinite(v.toDouble()) || std::abs(v.toDouble()) > 1e6)
                     throw std::runtime_error("Invalid test.refine point");
             if (rounds != std::floor(rounds) || rounds < 0 || rounds > 8)
                 throw std::runtime_error("Invalid test.refine rounds");
             return refine(point[0].toDouble(), point[1].toDouble(), int(rounds));
         }});
    registerTerrainEditCodec(
        {"test.split",
         [](const TerrainEditData &data) {
             const auto &s = dynamic_cast<const Split &>(data);
             return QJsonObject{{"point", QJsonArray{s.x, s.y}}};
         },
         [](const QJsonObject &object) -> std::shared_ptr<const TerrainEditData> {
             if (object.size() != 1 || !object["point"].isArray() ||
                 object["point"].toArray().size() != 2)
                 throw std::runtime_error("Invalid test.split entry");
             const auto point = object["point"].toArray();
             for (const auto &v : point)
                 if (!v.isDouble() || !std::isfinite(v.toDouble()) || std::abs(v.toDouble()) > 1e6)
                     throw std::runtime_error("Invalid test.split point");
             return split(point[0].toDouble(), point[1].toDouble());
         }});
}
} // namespace terrain_test_kinds
