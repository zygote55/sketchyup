#include "core/report.hpp"
#include "core/material_records.hpp"
#include "core/selection.hpp"
#include "core/tag_records.hpp"
#include "core/tags.hpp"
#include "geometry/solid.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <stdexcept>
#include <string_view>
#include <tuple>

namespace sketchy {
namespace {

// Groups sort "All" first, then by ID for tags, materials and definitions and by name for
// kinds (ADR 0175 §6.1). Keys without an ID ("untagged", "—", kind names) carry ID zero.
struct GroupKey {
    Id id{};
    std::string label;
    bool operator<(const GroupKey &other) const {
        return std::tie(id, label) < std::tie(other.id, other.label);
    }
};

const GroupKey allGroup{0, "All"};
const char *const noneLabel = "—";

struct CountAccumulator {
    std::size_t contexts{}, instances{};
    std::set<Id> definitions;
};

struct VolumeAccumulator {
    double volume{};
    std::size_t unvalidated{};
};

// The tag and its folder ancestors (a folder aggregates its descendant tags, §1).
std::vector<Id> tagChain(const Document &doc, Id tag) {
    std::vector<Id> chain;
    for (auto current = tag; current;) {
        const auto found = doc.tags().find(current);
        if (found == doc.tags().end() || chain.size() > 32)
            break;
        chain.push_back(current);
        current = found->second->parent;
    }
    return chain;
}

// The body and its ancestors, nearest first. The hierarchy is validated acyclic on load; the bound
// only keeps a corrupt in-memory document from looping.
std::vector<Id> ancestorsOrSelf(const Document &doc, Id body) {
    std::vector<Id> chain;
    for (auto current = body; current;) {
        const auto found = doc.bodies().find(current);
        if (found == doc.bodies().end())
            break;
        if (chain.size() > 128)
            throw std::runtime_error("Report hierarchy is too deep");
        chain.push_back(current);
        current = found->second->parent;
    }
    return chain;
}

std::string decimal(Id id) { return std::to_string(id); }

// The triangle rule of measureEntity and Document::worldArea (ADR 0175 §2.3): each world triangle
// contributes half the length of the cross product of its transformed edge vectors, so a mirror
// (negative determinant) changes nothing.
double worldTriangleArea(const Transform &world, const Triangle &triangle) {
    return length(cross(world.vector(triangle.b - triangle.a),
                        world.vector(triangle.c - triangle.a))) *
           .5;
}

std::string familyOf(const Body &body, bool placed) {
    if (body.referenceImage || body.kind == BodyKind::ReferenceImage)
        return "reference_image";
    if (placed)
        return "component";
    if (body.kind == BodyKind::Group)
        return "group";
    if (body.kind == BodyKind::Terrain || body.terrainTile)
        return "terrain";
    return "geometry";
}

// ADR 0175 §6.1: the statuses the invalid table lists, in the CSV's fixed order. Any other status
// (none is expected) follows in byte order rather than being dropped.
constexpr std::array<std::string_view, 10> invalidOrder{"empty",
                                                        "loose_geometry",
                                                        "open_boundary",
                                                        "non_manifold",
                                                        "inconsistent_winding",
                                                        "degenerate",
                                                        "self_intersection",
                                                        "ambiguous_containment",
                                                        "multiple_shells",
                                                        "analysis_limit"};

} // namespace

ReportResult runReport(const Document &doc, const ReportRequest &request,
                       const std::set<Id> *selectedBodies) {
    if (!request.counts && !request.areas && !request.volumes)
        throw std::invalid_argument("A report needs at least one metric");
    if (request.selection && !selectedBodies)
        throw std::invalid_argument("Report selection requires explicit editor state");

    // A tag is in scope when it is a requested tag or lies below one (a folder aggregates).
    const bool filterTags = !request.tags.empty();
    std::set<Id> tagScope;
    for (const auto &[tag, record] : doc.tags())
        for (const auto id : tagChain(doc, tag))
            if (request.tags.contains(id)) {
                tagScope.insert(tag);
                break;
            }

    const Selection persisted;
    // The All row exists even when nothing is in scope, so every report has its totals.
    std::map<GroupKey, CountAccumulator> counts{{allGroup, {}}};
    std::map<std::pair<GroupKey, Id>, double> areas;
    std::map<GroupKey, VolumeAccumulator> volumes;
    std::map<std::string, std::size_t> invalid;
    std::map<Id, ReportTerrainRow> terrain;

    for (const auto &[id, record] : doc.bodies()) {
        const auto &body = *record;
        const auto chain = ancestorsOrSelf(doc, id);

        // Scope (ADR 0175 §1, §2.2). Hidden content is in scope unless `visible` was asked for;
        // the persistent half of Selection::hidden is the whole rule (the session's temporary hide
        // set is not part of it).
        if (request.visibility == ReportVisibility::Visible &&
            persisted.hidden(doc, {id, SelectionKind::Body, 0}))
            continue;
        // A record carries its own tag and every ancestor's (inheritedTags, ADR 0175 §1).
        const auto carried = inheritedTags(doc, id);
        if (filterTags && std::none_of(carried.begin(), carried.end(),
                                       [&](Id tag) { return tagScope.contains(tag); }))
            continue;
        bool placed = false, matchesDefinition = request.definitions.empty();
        Id outerDefinition = 0;
        for (const auto ancestor : chain)
            if (const auto instance = doc.instances().find(ancestor);
                instance != doc.instances().end()) {
                placed = true;
                outerDefinition = instance->second->definition;
                matchesDefinition |= request.definitions.contains(outerDefinition);
            }
        if (!matchesDefinition)
            continue;
        if (request.selection) {
            bool selected = false;
            for (const auto ancestor : chain)
                selected |= selectedBodies->contains(ancestor);
            if (!selected)
                continue;
        }

        // The group memberships of this record (ADR 0175 §1 groupBy). A nested placement falls
        // under the outermost definition that owns it; a tag record also falls under each folder.
        const auto instance = doc.instances().find(id);
        const bool isPlacement = instance != doc.instances().end();
        const Id ownDefinition = isPlacement ? instance->second->definition : 0;
        std::vector<GroupKey> groups{allGroup};
        const auto faceMaterial = [&](Id face) { return faceMaterials(body, face).front; };
        switch (request.groupBy) {
        case ReportGroup::None:
            break;
        case ReportGroup::Kind:
            groups.push_back({0, familyOf(body, placed)});
            break;
        case ReportGroup::Tag: {
            // Each carried tag and each folder above it (a folder aggregates its descendants).
            if (carried.empty())
                groups.push_back({0, "untagged"});
            std::set<Id> keys;
            for (const auto tag : carried)
                for (const auto key : tagChain(doc, tag))
                    keys.insert(key);
            for (const auto key : keys)
                groups.push_back({key, decimal(key)});
            break;
        }
        case ReportGroup::Material: {
            // One material per record, the front material of its lowest face, so a record's
            // counts and volume are never split across groups (areas are split per face).
            const auto first = body.surface.faces.begin();
            const auto material =
                first == body.surface.faces.end() ? Id{} : faceMaterial(first->first);
            groups.push_back(material ? GroupKey{material, decimal(material)}
                                      : GroupKey{0, noneLabel});
            break;
        }
        case ReportGroup::Definition:
            groups.push_back(placed ? GroupKey{outerDefinition, decimal(outerDefinition)}
                                    : GroupKey{0, noneLabel});
            break;
        }

        for (const auto &group : groups) {
            auto &count = counts[group];
            ++count.contexts;
            if (isPlacement) {
                ++count.instances;
                count.definitions.insert(ownDefinition);
            }
        }
        if (body.kind == BodyKind::Terrain) {
            auto &row = terrain[id];
            row.terrain = id;
            ++row.contexts;
        }

        // Areas and terrain tiles (ADR 0175 §2.3, §2.5). Only geometry records have faces.
        if (!body.surface.faces.empty()) {
            const auto world = doc.worldTransform(id);
            double recordArea = 0;
            std::size_t recordTriangles = 0;
            for (const auto &[face, faceRecord] : body.surface.faces) {
                const auto material = faceMaterial(face);
                const auto triangles = body.surface.triangulate(face);
                double faceArea = 0;
                for (const auto &triangle : triangles)
                    faceArea += worldTriangleArea(world, triangle);
                recordArea += faceArea;
                recordTriangles += triangles.size();
                // The materials filter restricts the areas rows only (§1).
                if (!request.areas ||
                    (!request.materials.empty() && !request.materials.contains(material)))
                    continue;
                const auto areaGroups =
                    request.groupBy == ReportGroup::Material
                        ? std::vector<GroupKey>{allGroup,
                                                material ? GroupKey{material, decimal(material)}
                                                         : GroupKey{0, noneLabel}}
                        : groups;
                for (const auto &group : areaGroups)
                    areas[{group, material}] += faceArea;
            }
            if (body.terrainTile) {
                auto &row = terrain[body.parent];
                row.terrain = body.parent;
                ++row.contexts;
                row.triangles += recordTriangles;
                row.area += recordArea;
            }
        }
        if (body.terrainTile)
            continue; // Terrain is not a solid (ADR 0175 §2.5).

        // Volumes and invalid solids (ADR 0175 §2.3, §2.4). Containers (groups, placement roots,
        // terrain containers, reference images) hold no solid of their own and are not listed.
        if (request.volumes && body.kind == BodyKind::Geometry) {
            const auto report = inspectSolid(body.surface, body.topology);
            if (report.status == "solid" && report.volume) {
                const auto known = *report.volume * std::abs(doc.worldTransform(id).determinant());
                for (const auto &group : groups)
                    volumes[group].volume += known;
            } else {
                ++invalid[report.status];
                for (const auto &group : groups)
                    ++volumes[group].unvalidated;
            }
        }
    }

    ReportResult result;
    result.documentId = doc.identity();
    result.revision = doc.revision();
    result.request = request;
    result.precision = request.precision.value_or(doc.displayPrecision());
    if (request.counts)
        for (const auto &[group, count] : counts)
            result.counts.push_back(
                {group.label, count.contexts, count.instances, count.definitions.size()});
    if (request.areas)
        for (const auto &[key, area] : areas)
            result.areas.push_back({key.first.label, key.second, area});
    if (request.volumes) {
        for (const auto &[group, count] : counts)
            volumes.try_emplace(group);
        for (const auto &[group, volume] : volumes)
            result.volumes.push_back({group.label, volume.volume, volume.unvalidated});
        for (const auto status : invalidOrder)
            if (const auto found = invalid.find(std::string(status)); found != invalid.end())
                result.invalid.push_back({found->first, found->second});
        for (const auto &[status, contexts] : invalid)
            if (std::find(invalidOrder.begin(), invalidOrder.end(), status) == invalidOrder.end())
                result.invalid.push_back({status, contexts});
    }
    for (const auto &[id, row] : terrain)
        result.terrain.push_back(row);
    result.sheets = doc.drawingSheets().size();
    result.views = doc.drawingViews().size();
    result.items = doc.drawingItems().size();
    return result;
}

} // namespace sketchy
