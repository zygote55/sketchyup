#include "core/terrain_sources.hpp"
#include "core/terrain.hpp"
#include "core/terrain_edit.hpp"
#include "core/terrain_edit_mesh.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <map>
#include <stdexcept>
#include <tuple>
namespace sketchy {
namespace {
constexpr double xyTolerance = 1e-7; // the adapter's merge tolerance
constexpr double zTolerance = 1e-9;
std::uint64_t bitsOf(double v) { return std::bit_cast<std::uint64_t>(v == 0 ? 0.0 : v); }
std::uint64_t mix(std::uint64_t h) {
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33;
    return h;
}
bool closedPolyline(const TerrainSource &source) {
    return (source.role == TerrainSourceRole::Contour ||
            source.role == TerrainSourceRole::Breakline) &&
           source.points.size() >= 4 && source.points.front() == source.points.back();
}
} // namespace

TerrainPlanKey TerrainPlanKey::of(double x, double y) { return {bitsOf(x), bitsOf(y)}; }
std::size_t TerrainPlanHash::operator()(const TerrainPlanKey &key) const {
    return std::size_t(mix(key.x * 0x9E3779B97F4A7C15ULL ^ mix(key.y)));
}
std::size_t TerrainSourceState::SegmentHash::operator()(const SegmentKey &key) const {
    const TerrainPlanHash hash;
    return hash(key.a) * 31 + hash(key.b);
}

std::vector<std::array<TerrainPlanKey, 2>> terrainSourceSegments(const TerrainSource &source) {
    std::vector<std::array<TerrainPlanKey, 2>> result;
    if (source.role != TerrainSourceRole::Contour && source.role != TerrainSourceRole::Breakline)
        return result;
    const auto &p = source.points;
    for (std::size_t i = 0; i + 1 < p.size(); ++i) {
        auto a = TerrainPlanKey::of(p[i].x, p[i].y), b = TerrainPlanKey::of(p[i + 1].x, p[i + 1].y);
        if (a == b)
            continue;
        if (b < a)
            std::swap(a, b);
        result.push_back({a, b});
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

TerrainSourceState::TerrainSourceState(const TerrainSources &base) {
    slots_.reserve(base.size());
    byId_.reserve(base.size());
    std::size_t points = 0;
    for (const auto &source : base)
        points += source.points.size();
    positions_.reserve(points);
    segments_.reserve(points);
    for (const auto &source : base) {
        maxBaseId_ = std::max(maxBaseId_, source.id);
        if (!byId_.try_emplace(source.id, slots_.size()).second)
            continue; // validation refuses duplicate ids; the derivation keeps the first
        slots_.push_back(std::make_shared<const TerrainSource>(source));
        index(source, 1);
    }
}
std::vector<const TerrainSource *> TerrainSourceState::sources() const {
    std::vector<const TerrainSource *> result;
    result.reserve(byId_.size());
    for (const auto &slot : slots_)
        if (slot)
            result.push_back(slot.get());
    return result;
}
const TerrainSource *TerrainSourceState::find(std::uint32_t id) const {
    const auto found = byId_.find(id);
    return found == byId_.end() ? nullptr : slots_[found->second].get();
}
void TerrainSourceState::index(const TerrainSource &source, int sign) {
    std::vector<TerrainPlanKey> keys;
    keys.reserve(source.points.size());
    for (const auto &p : source.points)
        keys.push_back(TerrainPlanKey::of(p.x, p.y));
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    const bool contour = source.role == TerrainSourceRole::Contour;
    for (const auto &key : keys) {
        auto &uses = positions_[key];
        uses.all += std::uint32_t(sign);
        uses.contour += contour ? std::uint32_t(sign) : 0;
        if (!uses.all)
            positions_.erase(key);
    }
    for (const auto &[a, b] : terrainSourceSegments(source)) {
        auto &uses = segments_[{a, b}];
        uses += std::uint32_t(sign);
        if (!uses)
            segments_.erase({a, b});
    }
    vertices_ = sign > 0 ? vertices_ + source.points.size() : vertices_ - source.points.size();
}
void TerrainSourceState::add(TerrainSource source) {
    if (!source.id || byId_.contains(source.id))
        throw std::invalid_argument("The terrain source id is in use");
    byId_[source.id] = slots_.size();
    index(source, 1);
    slots_.push_back(std::make_shared<const TerrainSource>(std::move(source)));
}
void TerrainSourceState::replace(TerrainSource source) {
    const auto found = byId_.find(source.id);
    if (found == byId_.end())
        throw std::invalid_argument("The terrain source does not exist");
    auto &slot = slots_[found->second];
    index(*slot, -1);
    index(source, 1);
    slot = std::make_shared<const TerrainSource>(std::move(source));
}
void TerrainSourceState::remove(std::uint32_t id) {
    const auto found = byId_.find(id);
    if (found == byId_.end())
        throw std::invalid_argument("The terrain source does not exist");
    index(*slots_[found->second], -1);
    slots_[found->second].reset();
    byId_.erase(found);
}
std::uint32_t TerrainSourceState::vertexUses(TerrainPlanKey at) const {
    const auto found = positions_.find(at);
    return found == positions_.end() ? 0 : found->second.all;
}
std::uint32_t TerrainSourceState::contourUses(TerrainPlanKey at) const {
    const auto found = positions_.find(at);
    return found == positions_.end() ? 0 : found->second.contour;
}
std::uint32_t TerrainSourceState::segmentUses(TerrainPlanKey a, TerrainPlanKey b) const {
    if (b < a)
        std::swap(a, b);
    const auto found = segments_.find({a, b});
    return found == segments_.end() ? 0 : found->second;
}

void applyTerrainSourceEffect(TerrainSourceState &state, const TerrainEditKind &kind,
                              const TerrainEdit &edit) {
    try {
        if (kind.sources)
            kind.sources(state, edit);
    } catch (const std::invalid_argument &) {
        // An entry that does not fit the state changes nothing (the replay skips it too).
    }
    if (kind.protects)
        for (auto &polygon : kind.protects(edit))
            state.protectedRegions.push_back({edit.id, std::move(polygon)});
}

namespace {
struct CachedState {
    // The base sources and the entries are held, so their addresses stay unique while cached.
    TerrainSources sources;
    const void *block{};
    std::vector<TerrainEditPtr> edits;
    std::shared_ptr<TerrainSourceState> state;
    std::uint64_t used{};
};
std::mutex stateMutex;
std::vector<CachedState> stateCache;
std::uint64_t stateClock = 0;
constexpr std::size_t stateCapacity = 4;
bool readsSources(const TerrainEditKind *kind) {
    return kind && (kind->sources || kind->protects);
}
} // namespace
std::shared_ptr<const TerrainSourceState> terrainSourceState(const TerrainRecord &record) {
    std::lock_guard lock(stateMutex);
    // The longest cached prefix of this record's list with the same base block.
    CachedState *best = nullptr;
    for (auto &entry : stateCache) {
        if (entry.block != record.sources.block() || entry.edits.size() > record.edits.size())
            continue;
        if (!std::equal(entry.edits.begin(), entry.edits.end(), record.edits.begin()))
            continue;
        if (!best || entry.edits.size() > best->edits.size())
            best = &entry;
    }
    if (best && best->edits.size() == record.edits.size()) {
        best->used = ++stateClock;
        return best->state;
    }
    std::shared_ptr<TerrainSourceState> state;
    std::size_t from = 0;
    if (best) {
        from = best->edits.size();
        // In place when the cache holds the only reference, otherwise on a copy.
        if (best->state.use_count() == 1) {
            state = std::move(best->state);
            best->block = nullptr;
            best->edits.clear();
        } else
            state = std::make_shared<TerrainSourceState>(*best->state);
    } else
        state = std::make_shared<TerrainSourceState>(record.sources);
    for (std::size_t i = from; i < record.edits.size(); ++i) {
        const auto &edit = *record.edits[i];
        if (const auto *kind = findTerrainEditKind(edit.kind); readsSources(kind))
            applyTerrainSourceEffect(*state, *kind, edit);
    }
    std::erase_if(stateCache, [](const CachedState &entry) { return !entry.block; });
    if (stateCache.size() >= stateCapacity) {
        auto oldest = std::min_element(stateCache.begin(), stateCache.end(),
                                       [](const auto &a, const auto &b) { return a.used < b.used; });
        stateCache.erase(oldest);
    }
    stateCache.push_back({record.sources, record.sources.block(), record.edits, state, ++stateClock});
    return state;
}
void clearTerrainSourceStateCache() {
    std::lock_guard lock(stateMutex);
    stateCache.clear();
}

// ---- Cleaning ------------------------------------------------------------------------------
namespace {
bool sameXy(const TerrainVec3 &a, const TerrainVec3 &b) {
    return std::abs(a.x - b.x) <= xyTolerance && std::abs(a.y - b.y) <= xyTolerance;
}
bool duplicate(const TerrainVec3 &a, const TerrainVec3 &b) {
    return sameXy(a, b) && std::abs(a.z - b.z) <= zTolerance;
}
int orient(const TerrainVec3 &a, const TerrainVec3 &b, const TerrainVec3 &c) {
    return terrainOrientation({a.x, a.y}, {b.x, b.y}, {c.x, c.y});
}
bool lexLess(const TerrainVec3 &a, const TerrainVec3 &b) {
    return std::tie(a.x, a.y, a.z) < std::tie(b.x, b.y, b.z);
}
} // namespace
void cleanTerrainPolyline(std::vector<TerrainVec3> &points, bool closed,
                          TerrainPolylineCleaning &counts) {
    std::vector<TerrainVec3> out;
    out.reserve(points.size());
    auto spike = [](const TerrainVec3 &a, const TerrainVec3 &b, const TerrainVec3 &c) {
        if (orient(a, b, c) != 0)
            return false;
        const double dot = (b.x - a.x) * (c.x - b.x) + (b.y - a.y) * (c.y - b.y);
        return dot < 0;
    };
    for (const auto &p : points) {
        if (!out.empty() && duplicate(out.back(), p)) {
            ++counts.zeroLength;
            continue;
        }
        out.push_back(p);
        while (out.size() >= 3 && spike(out[out.size() - 3], out[out.size() - 2], out.back())) {
            out.erase(out.end() - 2);
            ++counts.spikes;
            if (duplicate(out[out.size() - 2], out.back())) {
                out.pop_back();
                ++counts.zeroLength;
            }
        }
    }
    if (closed) {
        bool changed = true;
        while (changed && out.size() >= 2) {
            changed = false;
            if (duplicate(out.back(), out.front())) {
                out.pop_back();
                ++counts.zeroLength;
                changed = true;
                continue;
            }
            if (out.size() >= 3) {
                const auto n = out.size();
                if (spike(out[n - 2], out[n - 1], out[0])) {
                    out.pop_back();
                    ++counts.spikes;
                    changed = true;
                } else if (spike(out[n - 1], out[0], out[1])) {
                    out.erase(out.begin());
                    ++counts.spikes;
                    changed = true;
                }
            }
        }
    }
    points = std::move(out);
}
void canonicalTerrainPolyline(std::vector<TerrainVec3> &p, bool closed) {
    if (p.size() < 2)
        return;
    if (!closed) {
        if (lexLess(p.back(), p.front()))
            std::reverse(p.begin(), p.end());
        return;
    }
    std::rotate(p.begin(), std::min_element(p.begin(), p.end(), lexLess), p.end());
    long double area = 0;
    for (std::size_t i = 0; i < p.size(); ++i) {
        const auto &a = p[i], &b = p[(i + 1) % p.size()];
        area += (long double)a.x * b.y - (long double)b.x * a.y;
    }
    if (area < 0)
        std::reverse(p.begin() + 1, p.end());
}

// ---- C1 ------------------------------------------------------------------------------------
TerrainSourceConsistency checkTerrainSourceConsistency(const Document &doc, Id terrain,
                                                       const std::vector<std::uint32_t> &skipped) {
    if (!isTerrain(doc, terrain))
        throw TerrainError("NOT_FOUND", "Terrain body does not exist");
    TerrainSourceConsistency report;
    const auto &record = *doc.bodies().at(terrain)->terrain;
    std::shared_ptr<const TerrainSourceState> state;
    if (skipped.empty())
        state = terrainSourceState(record);
    else {
        auto applied = std::make_shared<TerrainSourceState>(record.sources);
        for (const auto &edit : record.edits)
            if (std::find(skipped.begin(), skipped.end(), edit->id) == skipped.end())
                if (const auto *kind = findTerrainEditKind(edit->kind))
                    applyTerrainSourceEffect(*applied, *kind, *edit);
        state = std::move(applied);
    }
    const auto mesh = terrainEditMeshCache().get(doc, terrain, true);
    auto problem = [&](std::string text) {
        if (report.problems.size() < 32)
            report.problems.push_back(std::move(text));
    };
    auto where = [](Vec3 p) {
        return "(" + std::to_string(p.x) + ", " + std::to_string(p.y) + ", " +
               std::to_string(p.z) + ")";
    };
    for (const auto *source : state->sources()) {
        if (source->role != TerrainSourceRole::Contour &&
            source->role != TerrainSourceRole::Breakline)
            continue;
        ++report.sources;
        const auto &p = source->points;
        const bool closed = closedPolyline(*source);
        for (std::size_t i = 0; i < p.size(); ++i) {
            if (closed && i + 1 == p.size())
                break;
            ++report.vertices;
            if (mesh->trianglesAt(p[i]).empty()) {
                ++report.missingVertices;
                problem("source " + std::to_string(source->id) + " vertex " + where(p[i]) +
                        " is not a mesh vertex");
            }
        }
        for (std::size_t i = 0; i + 1 < p.size(); ++i) {
            if (p[i].x == p[i + 1].x && p[i].y == p[i + 1].y)
                continue;
            ++report.segments;
            const auto a = mesh->trianglesAt(p[i]), b = mesh->trianglesAt(p[i + 1]);
            std::vector<TerrainTriangleRef> shared;
            std::set_intersection(a.begin(), a.end(), b.begin(), b.end(),
                                  std::back_inserter(shared));
            if (shared.empty()) {
                ++report.missingSegments;
                problem("source " + std::to_string(source->id) + " segment " + where(p[i]) +
                        " to " + where(p[i + 1]) + " is not a mesh edge");
            }
        }
    }
    return report;
}
} // namespace sketchy
